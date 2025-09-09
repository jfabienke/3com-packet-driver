# ISR Deferral Architecture Design

## Overview

This document describes the top-half/bottom-half interrupt service routine (ISR) architecture for the DOS packet driver. This design reduces interrupt latency from ~100μs to <10μs while maintaining high throughput through deferred packet processing.

## Design Principles

1. **Minimal ISR (Top Half)**: Do absolute minimum in interrupt context
2. **Deferred Processing (Bottom Half)**: Handle actual work outside interrupt context
3. **Lock-Free Queuing**: Single-producer/single-consumer queue for work items
4. **Batched Operations**: Process multiple packets per bottom-half invocation
5. **DOS Compatibility**: Work within real-mode constraints

## Architecture Overview

```
Hardware Interrupt
       │
       ▼
┌──────────────┐
│  Fast ISR    │ ◄── ~20 instructions
│  (Top Half)  │     <10μs execution
├──────────────┤
│ • Read status│
│ • ACK device │
│ • Queue work │
│ • EOI to PIC │
└──────┬───────┘
       │
    Queue Work
       │
       ▼
┌──────────────┐
│ Work Queue   │ ◄── Lock-free SPSC
│   (Memory)   │     256 entries
└──────┬───────┘
       │
    Trigger
       │
       ▼
┌──────────────┐
│ Bottom Half  │ ◄── Deferred execution
│   Worker     │     Process batches
├──────────────┤
│ • RX packets │
│ • TX reclaim │
│ • Statistics │
│ • Refill buf │
└──────────────┘
```

## Top Half (Fast ISR) Design

### ISR Requirements

- **Maximum Instructions**: 20-30 assembly instructions
- **Maximum Time**: 10 microseconds
- **No Loops**: Straight-line code only
- **No Complex Logic**: Simple read, write, queue operations
- **Register Preservation**: Minimal register saves

### Fast ISR Implementation (DOS Tuning Guide Version)

```asm
; Tiny ISR from tuning guide - absolute minimum
; Total: ~15 instructions, <10 microseconds

; Simple SPSC work queue from ISR -> worker
volatile uint8_t rx_work_pending;

void __interrupt __far nic_isr(void)
{
    nic_ack_cause();     // ACK NIC cause reg to drop INTx (3-4 instructions)
    rx_work_pending = 1; // Mark work; ISR stays tiny (1 instruction)
    pic_eoi();           // EOI to 8259 (2 instructions)
}

; Assembly version for maximum speed
GLOBAL _nic_isr_tiny
_nic_isr_tiny:
    push ax
    push dx
    
    ; ACK device - device specific, typically:
    mov dx, [iobase]
    add dx, INT_STATUS_REG
    in ax, dx
    out dx, ax           ; Write back to clear
    
    ; Set work flag
    mov byte [rx_work_pending], 1
    
    ; EOI to PIC
    mov al, 20h
    out 20h, al
    
    pop dx
    pop ax
    iret
    
; Total: 11 instructions (even smaller!)
```

### C Structure for ISR Data

```c
// Aligned for cache efficiency
struct isr_data {
    // Cache line 1: ISR critical path
    volatile uint16_t work_queue_head;    // ISR writes
    volatile uint16_t work_queue_tail;    // Worker reads
    volatile uint8_t work_pending;        // Work flag
    uint8_t current_irq;                  // Active IRQ
    uint16_t iobase;                      // Device I/O base
    uint16_t last_status;                  // Last interrupt status
    uint8_t padding1[6];                  // Align to 16 bytes
    
    // Cache line 2: Work queue
    uint16_t work_items[WORK_QUEUE_SIZE]; // Status values
} __attribute__((aligned(16)));
```

## Work Queue Design

### Lock-Free SPSC Queue

The work queue uses a Single-Producer Single-Consumer (SPSC) design:
- **Producer**: ISR (top half) - adds items
- **Consumer**: Worker (bottom half) - removes items
- **No Locks**: Uses careful ordering and volatile accesses

```c
#define WORK_QUEUE_SIZE 256
#define QUEUE_MASK (WORK_QUEUE_SIZE - 1)

struct work_queue {
    struct work_item {
        uint16_t status;      // Device interrupt status
        uint16_t device_id;   // Which device (for multi-NIC)
        uint32_t timestamp;   // Optional timing info
    } items[WORK_QUEUE_SIZE];
    
    volatile uint16_t head;   // ISR writes, worker reads
    volatile uint16_t tail;   // Worker writes, ISR reads
    volatile uint8_t pending; // Work available flag
};

// ISR enqueue (producer)
static inline void enqueue_work(uint16_t status, uint16_t device) {
    uint16_t next_head = (work_queue.head + 1) & QUEUE_MASK;
    
    // Check for queue full (should never happen with proper sizing)
    if (next_head == work_queue.tail) {
        stats.queue_overflows++;
        return;
    }
    
    work_queue.items[work_queue.head].status = status;
    work_queue.items[work_queue.head].device_id = device;
    work_queue.head = next_head;
    work_queue.pending = 1;
}

// Worker dequeue (consumer)
static inline int dequeue_work(struct work_item *item) {
    if (work_queue.tail == work_queue.head) {
        work_queue.pending = 0;
        return 0; // Queue empty
    }
    
    *item = work_queue.items[work_queue.tail];
    work_queue.tail = (work_queue.tail + 1) & QUEUE_MASK;
    return 1;
}
```

## Bottom Half (Worker) Design

### Worker Responsibilities

1. **Drain Work Queue**: Process all pending work items
2. **Handle RX Packets**: Process received packets with copy-break
3. **Reclaim TX Buffers**: Free completed transmit buffers
4. **Refill RX Ring**: Replenish receive descriptors
5. **Update Statistics**: Maintain performance counters
6. **Re-enable Interrupts**: Unmask device interrupts if masked

### Worker Implementation (DOS Tuning Guide Version)

```c
// Simplified worker from tuning guide
void rx_worker(void)
{
    const unsigned N_BUDGET = 32;  // Max packets per run
    const unsigned COPY_BREAK = 192;
    unsigned n = 0;

    while (rx_work_pending) {
        rx_work_pending = 0;  // Clear flag

        // Drain completions up to budget
        while (n < N_BUDGET && rx_desc_done()) {
            unsigned len = rx_desc_len();
            void*    buf = rx_desc_buf();

            if (len <= COPY_BREAK) {
                // Small: copy out to UMB buffer & recycle slot
                memcpy(umb_small_rx, buf, len);
                rx_recycle_slot_small();
            } else {
                // Large: hand off DMA buffer & re-post fresh one
                deliver_large(buf, len);
                rx_replenish_large();
            }
            n++;
        }

        // Bulk top-up if ring below low-watermark
        rx_bulk_refill();  // Write UP_LIST_PTR once for batch

        // If more work raced in, loop; otherwise exit
    }
}

// Enhanced version with TX handling
void el3_bottom_half_worker(void) {
    const unsigned RX_BUDGET = 32;
    const unsigned TX_BUDGET = 16;
    unsigned rx_done = 0, tx_done = 0;
    
    // Process pending work
    while (rx_work_pending) {
        rx_work_pending = 0;
        
        // RX processing with budget and copy-break
        while (rx_done < RX_BUDGET && rx_desc_done()) {
            unsigned len = rx_desc_len();
            void* buf = rx_desc_buf();
            
            if (len <= 192) {  // Copy-break threshold
                memcpy(small_buffer, buf, len);
                rx_recycle_immediate();
                deliver_small(small_buffer, len);
            } else {
                deliver_large(buf, len);
                rx_replenish_buffer();
            }
            rx_done++;
        }
        
        // TX reclaim (opportunistic, no interrupt)
        while (tx_done < TX_BUDGET && tx_desc_done()) {
            free_tx_buffer(tx_ring[tx_tail].buffer);
            tx_tail = (tx_tail + 1) & TX_RING_MASK;
            tx_inflight--;
            tx_done++;
        }
        
        // Bulk operations
        if (rx_ring_low()) {
            rx_bulk_refill();  // Single UP_LIST_PTR write
        }
    }
    
    stats.worker_runs++;
    stats.rx_processed += rx_done;
    stats.tx_reclaimed += tx_done;
}
```

### Batch Processing Functions

```c
// Process RX packets in batch with budget
int process_rx_batch(struct el3_device *dev, int budget, uint32_t deadline) {
    int processed = 0;
    
    while (processed < budget && get_timer_ticks() < deadline) {
        struct rx_descriptor *desc = get_next_rx_desc(dev);
        if (!desc || desc->owned_by_hw)
            break;
        
        uint16_t len = desc->length & 0x1FFF;
        
        // Apply copy-break
        if (len <= COPY_BREAK_THRESHOLD) {
            // Small packet - copy and recycle immediately
            void *staging = get_rx_staging_buffer(dev);
            memcpy(staging, desc->buffer, len);
            desc->owned_by_hw = 1;  // Give back to hardware
            deliver_packet(staging, len);
            stats.copy_break_hits++;
        } else {
            // Large packet - zero copy or bounce
            deliver_packet(desc->buffer, len);
            desc->buffer = allocate_rx_buffer(dev);
            desc->owned_by_hw = 1;
            stats.zero_copy_packets++;
        }
        
        processed++;
    }
    
    return processed;
}

// Reclaim TX descriptors in batch
int reclaim_tx_batch(struct el3_device *dev, int budget) {
    int reclaimed = 0;
    
    while (reclaimed < budget) {
        struct tx_descriptor *desc = get_completed_tx_desc(dev);
        if (!desc)
            break;
        
        // Free buffer based on type
        if (desc->flags & TX_SMALL_BUFFER) {
            free_small_tx_buffer(dev, desc->buffer);
        } else {
            free_large_tx_buffer(dev, desc->buffer);
        }
        
        desc->owned_by_hw = 0;
        dev->tx_free_count++;
        reclaimed++;
    }
    
    // Wake up TX if it was stopped
    if (dev->tx_stopped && dev->tx_free_count > TX_WAKE_THRESHOLD) {
        dev->tx_stopped = 0;
        netif_wake_queue(dev);
    }
    
    return reclaimed;
}
```

## Triggering Mechanisms

### Method 1: Software Interrupt (Recommended)

Use the packet driver software interrupt (INT 60h) to trigger bottom half:

```c
// In ISR: trigger software interrupt
void trigger_bottom_half(void) {
    _asm {
        pushf
        int 60h     ; Packet driver software interrupt
        popf
    }
}

// INT 60h handler
void __interrupt __far pkt_driver_int60(void) {
    // Check if called for bottom half processing
    if (work_queue.pending) {
        el3_bottom_half_worker();
    }
    
    // Also handle normal packet driver API calls
    // ... existing API handling ...
}
```

### Method 2: Timer Integration

Hook into timer interrupt for periodic processing:

```c
static void (__interrupt __far *old_timer_handler)();

void __interrupt __far timer_hook(void) {
    static uint8_t tick_count = 0;
    
    // Call original timer
    (*old_timer_handler)();
    
    // Check every 4th tick (~4.5ms)
    if ((++tick_count & 3) == 0) {
        if (work_queue.pending && !in_isr) {
            el3_bottom_half_worker();
        }
    }
}

void install_timer_hook(void) {
    old_timer_handler = _dos_getvect(0x08);
    _dos_setvect(0x08, timer_hook);
}
```

### Method 3: Main Loop Polling

For drivers with a main loop:

```c
// Main driver loop
void driver_main_loop(void) {
    while (driver_active) {
        // Check for work
        if (work_queue.pending) {
            el3_bottom_half_worker();
        }
        
        // Other maintenance tasks
        check_link_status();
        update_statistics();
        
        // Yield CPU
        _asm { hlt }
    }
}
```

## Interrupt Masking Strategy

### Optional Interrupt Masking

For heavy load situations, mask device interrupts during processing:

```c
// Enhanced ISR with masking
void el3_isr_with_masking(void) {
    uint16_t status = inw(iobase + STATUS_REG);
    
    // ACK and mask if high load
    outw(status, iobase + STATUS_REG);  // ACK
    
    if (work_queue_depth() > HIGH_LOAD_THRESHOLD) {
        outw(INT_MASK_ALL, iobase + INT_MASK_REG);
        device->irq_masked = 1;
    }
    
    enqueue_work(status, device_id);
}

// Bottom half unmasks when done
void unmask_after_processing(struct el3_device *dev) {
    if (dev->irq_masked) {
        outw(INT_UNMASK_ALL, dev->iobase + INT_MASK_REG);
        dev->irq_masked = 0;
    }
}
```

## Performance Budgeting

### Configurable Budgets

```c
// Budget configuration
#define RX_BUDGET_DEFAULT      32    // Packets per bottom half
#define TX_BUDGET_DEFAULT      16    // TX descriptors per bottom half
#define TIME_BUDGET_MS         2     // Max milliseconds per run
#define HIGH_LOAD_THRESHOLD    128   // Queue depth for masking

struct budget_config {
    uint16_t rx_budget;        // Max RX packets to process
    uint16_t tx_budget;        // Max TX descriptors to reclaim
    uint16_t time_budget_ms;   // Max time in bottom half
    uint16_t queue_threshold;  // When to mask interrupts
};

// Dynamic budget adjustment based on load
void adjust_budgets(struct el3_device *dev) {
    uint32_t pkt_rate = calculate_packet_rate(dev);
    
    if (pkt_rate > HIGH_RATE_THRESHOLD) {
        // High load - increase batching
        dev->budget.rx_budget = 64;
        dev->budget.time_budget_ms = 4;
    } else if (pkt_rate < LOW_RATE_THRESHOLD) {
        // Low load - reduce latency
        dev->budget.rx_budget = 8;
        dev->budget.time_budget_ms = 1;
    }
}
```

## Statistics and Monitoring

### Performance Metrics

```c
struct isr_deferral_stats {
    // ISR statistics
    uint32_t isr_count;            // Total ISR invocations
    uint32_t isr_time_total;       // Total time in ISR (ticks)
    uint32_t isr_time_max;         // Maximum ISR time
    
    // Work queue statistics
    uint32_t queue_max_depth;      // Maximum queue depth seen
    uint32_t queue_overflows;      // Queue full events
    uint32_t work_items_queued;    // Total items queued
    
    // Bottom half statistics
    uint32_t bottom_half_runs;     // Total BH invocations
    uint32_t bh_time_total;        // Total time in BH
    uint32_t bh_time_max;          // Maximum BH run time
    
    // Batch statistics
    uint32_t rx_batch_total;       // Total RX batch size
    uint32_t rx_batch_count;       // Number of RX batches
    uint32_t tx_batch_total;       // Total TX batch size
    uint32_t tx_batch_count;       // Number of TX batches
    
    // Masking statistics
    uint32_t irq_masks;            // Times interrupts masked
    uint32_t mask_duration_total;  // Total masked time
};

void print_isr_stats(void) {
    printf("ISR Deferral Statistics:\n");
    printf("  ISR count: %lu\n", stats.isr_count);
    printf("  Avg ISR time: %lu us\n", 
           stats.isr_time_total / stats.isr_count);
    printf("  Max ISR time: %lu us\n", stats.isr_time_max);
    printf("  Work queue max: %lu\n", stats.queue_max_depth);
    printf("  Bottom half runs: %lu\n", stats.bottom_half_runs);
    printf("  Avg RX batch: %.1f\n", 
           (float)stats.rx_batch_total / stats.rx_batch_count);
    printf("  Avg TX batch: %.1f\n",
           (float)stats.tx_batch_total / stats.tx_batch_count);
}
```

## Memory Layout

### Optimized Data Structure Layout

```c
// Align hot data for cache efficiency
struct driver_hot_data {
    // Cache line 1: ISR critical (16 bytes)
    struct isr_data isr;
    
    // Cache line 2: Work queue head (16 bytes)
    struct work_queue queue;
    
    // Cache line 3: Device state (16 bytes)
    struct device_state {
        uint16_t iobase[MAX_DEVICES];
        uint8_t irq_masked[MAX_DEVICES];
        uint8_t device_count;
    } devices;
} __attribute__((packed, aligned(16)));
```

## Testing Strategy

### Latency Testing

1. **ISR Latency**: Measure time from interrupt to ISR completion
   - Target: <10 microseconds
   - Method: Hardware timer or logic analyzer

2. **End-to-End Latency**: Measure packet receive to delivery
   - Target: <50 microseconds for small packets
   - Method: Timestamp in ISR and delivery

### Throughput Testing

1. **Maximum Packet Rate**: Flood with minimum-size packets
   - Measure: Packets per second handled
   - Target: 50,000+ pps

2. **Sustained Load**: Extended high-rate test
   - Duration: 1 hour minimum
   - Monitor: Queue depth, overflows

### Stress Testing

1. **Queue Overflow**: Generate bursts exceeding queue size
2. **Budget Exhaustion**: Sustained load exceeding budgets
3. **Interrupt Storm**: Pathological interrupt patterns

## Conclusion

The ISR deferral architecture achieves dramatic latency reduction (90%) while maintaining high throughput through:
- Ultra-minimal ISR (~20 instructions)
- Lock-free work queue
- Batched bottom-half processing
- Flexible triggering mechanisms
- Optional interrupt masking

This design is fully compatible with DOS real-mode constraints while providing performance comparable to modern operating systems.