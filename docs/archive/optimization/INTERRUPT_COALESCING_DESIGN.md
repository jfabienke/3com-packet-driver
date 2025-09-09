# Interrupt Coalescing Design

## Overview

Interrupt coalescing reduces CPU overhead by processing multiple packets per interrupt instead of generating an interrupt for every packet. This design achieves 85% interrupt reduction (from 1 interrupt per packet to 1 per 8-32 packets) while maintaining low latency for interactive traffic.

## Design Goals

1. **Reduce interrupt rate** by 8-32x for bulk traffic
2. **Maintain low latency** for interactive/real-time traffic
3. **Prevent packet loss** through intelligent budgeting
4. **Adapt to traffic patterns** dynamically
5. **Work with all 3Com NIC generations** (Vortex through Tornado)

## Coalescing Strategies

### Three-Tier Approach

```
┌─────────────────────────────────────┐
│        Coalescing Strategy          │
├─────────────────────────────────────┤
│                                     │
│  1. TX-Side: Selective Interrupts  │
│     • Mark every Kth descriptor    │
│     • Or when queue was empty      │
│                                     │
│  2. RX-Side: Batch Processing      │
│     • Process N packets per IRQ    │
│     • Time-bounded operation       │
│                                     │
│  3. Adaptive: Traffic-Based        │
│     • Low latency for light load   │
│     • High throughput for bulk     │
│                                     │
└─────────────────────────────────────┘
```

## TX-Side Coalescing

### Selective TX Interrupt Algorithm

```c
// TX coalescing configuration
struct tx_coalescing_config {
    uint16_t packet_threshold;    // Interrupt every K packets
    uint32_t byte_threshold;      // Or every B bytes
    uint16_t time_threshold_ms;   // Or after T milliseconds
    uint8_t always_if_empty;      // Always interrupt if queue empty
};

// Default configuration
#define TX_COAL_PACKETS     8      // Every 8 packets
#define TX_COAL_BYTES       8192   // Or 8KB
#define TX_COAL_TIMER_MS    10     // Or 10ms
#define TX_COAL_EMPTY       1      // Always if empty

// TX coalescing state
struct tx_coal_state {
    uint16_t packets_since_irq;
    uint32_t bytes_since_irq;
    uint32_t last_irq_time;
    uint16_t inflight_count;
    uint16_t last_irq_descriptor;
    uint32_t total_interrupts_saved;
};
```

### TX Interrupt Decision Logic (DOS Real-Mode Optimized)

```c
// Simplified decision logic for DOS real mode
#define TX_INT_BIT   0x8000  // Per-descriptor interrupt flag
#define K_PKTS       8       // Request interrupt every 8 packets

static unsigned tx_since_irq = 0;
static unsigned tx_inflight  = 0;

// Determine if TX descriptor should request interrupt
static bool should_request_tx_interrupt(struct el3_device *dev,
                                       uint16_t packet_len,
                                       uint8_t descriptor_idx) {
    // CRITICAL: Simple logic for real-mode efficiency
    
    // Case 1: Queue was empty - need interrupt for forward progress
    if (tx_inflight == 0) {
        tx_since_irq = 0;
        return true;
    }
    
    // Case 2: Every Kth packet (simplified from multiple thresholds)
    if ((tx_since_irq % K_PKTS) == 0) {
        tx_since_irq = 0;
        return true;
    }
    
    // Case 3: Ring about to wrap
    if (descriptor_idx == (TX_RING_SIZE - 1)) {
        return true;
    }
    
    // No interrupt needed
    tx_since_irq++;
    dev->stats.interrupts_saved++;
    
    return false;
}

// Actual implementation from tuning guide
void tx_post(void* buf_phys, unsigned len, struct tx_desc* d)
{
    d->buf_phys = (uint32_t)buf_phys;
    d->len      = (uint16_t)len;
    d->flags    = 0;

    // Request an IRQ only when needed
    if (tx_inflight == 0 || (tx_since_irq % K_PKTS) == 0) {
        d->flags |= TX_INT_BIT;  // Set per-descriptor flag
        tx_since_irq = 0;
    }

    // Enqueue and kick once (doorbell batching)
    ring_push_tx(d);
    tx_inflight++;
    nic_kick_tx_once(); // Single write to DN_LIST_PTR
}
```

### TX Descriptor Setup with Coalescing

```c
int el3_transmit_with_coalescing(struct el3_device *dev,
                                 const void *data,
                                 uint16_t len) {
    struct tx_descriptor *desc;
    uint8_t desc_idx;
    
    // Get next descriptor
    desc_idx = dev->tx_head;
    desc = &dev->tx_ring[desc_idx];
    
    // Setup descriptor
    desc->addr = virt_to_phys(data);
    desc->length = len;
    
    // Determine if interrupt needed
    if (should_request_tx_interrupt(dev, len, desc_idx)) {
        // Request interrupt on this descriptor
        desc->length |= TX_INT_ENABLE_BIT;  // 0x80000000
        dev->tx_coal.last_irq_descriptor = desc_idx;
        dev->stats.tx_interrupts_requested++;
    } else {
        // No interrupt for this descriptor
        desc->length &= ~TX_INT_ENABLE_BIT;
        dev->stats.tx_interrupts_suppressed++;
    }
    
    // Update state
    dev->tx_coal.inflight_count++;
    dev->tx_head = (dev->tx_head + 1) & TX_RING_MASK;
    
    // Kick hardware (may be batched separately)
    if (dev->tx_coal.inflight_count == 1 || 
        (desc_idx & 7) == 7) {  // Every 8th or first
        kick_tx_dma(dev);
    }
    
    return 0;
}
```

### TX Completion Handling

```c
// Process TX completions in batch
int process_tx_completions_batch(struct el3_device *dev, int budget) {
    struct tx_descriptor *desc;
    int completed = 0;
    
    while (completed < budget) {
        desc = &dev->tx_ring[dev->tx_tail];
        
        // Check if completed
        if (desc->status & TX_OWN_BIT)
            break;  // Still owned by hardware
        
        // Free resources
        if (desc->flags & TX_SMALL_BUFFER) {
            free_small_buffer(desc->buffer_id);
        } else {
            free_large_buffer(desc->buffer_id);
        }
        
        // Clear descriptor
        desc->status = 0;
        desc->buffer_id = 0;
        
        // Update counters
        dev->tx_tail = (dev->tx_tail + 1) & TX_RING_MASK;
        dev->tx_coal.inflight_count--;
        completed++;
        
        // Check if this was an IRQ descriptor
        if (dev->tx_tail == dev->tx_coal.last_irq_descriptor) {
            dev->stats.tx_interrupt_batches++;
            dev->stats.tx_batch_size += completed;
        }
    }
    
    // Wake TX queue if needed
    if (dev->tx_stopped && 
        dev->tx_coal.inflight_count < TX_WAKE_THRESHOLD) {
        dev->tx_stopped = 0;
        netif_wake_queue(dev);
    }
    
    return completed;
}
```

## RX-Side Coalescing

### RX Batch Processing Strategy

```c
// RX coalescing configuration
struct rx_coalescing_config {
    uint16_t packet_budget;       // Max packets per interrupt
    uint32_t byte_budget;         // Max bytes per interrupt
    uint16_t time_budget_ms;      // Max time per interrupt
    uint8_t adaptive_mode;        // Enable adaptive coalescing
};

// Default configuration
#define RX_COAL_PACKETS     32     // Process up to 32 packets
#define RX_COAL_BYTES       49152  // Or 48KB
#define RX_COAL_TIME_MS     2      // Or 2ms
#define RX_ADAPTIVE         1      // Enable adaptation

// RX coalescing state
struct rx_coal_state {
    uint32_t packets_processed;
    uint32_t bytes_processed;
    uint32_t batch_count;
    uint32_t max_batch_size;
    uint32_t interrupts_total;
    uint16_t current_budget;      // Adaptive budget
};
```

### RX Batch Processing with Budgets

```c
// Process RX packets with multiple budget constraints
int el3_process_rx_coalesced(struct el3_device *dev) {
    struct rx_coal_state *coal = &dev->rx_coal;
    uint32_t start_time = get_timer_ms();
    int processed = 0;
    uint32_t bytes = 0;
    
    // Adaptive budget based on recent history
    uint16_t packet_budget = coal->current_budget;
    
    while (processed < packet_budget) {
        struct rx_descriptor *desc;
        uint16_t pkt_len;
        
        // Check time budget
        if ((get_timer_ms() - start_time) >= RX_COAL_TIME_MS)
            break;
        
        // Check byte budget
        if (bytes >= RX_COAL_BYTES)
            break;
        
        // Get next RX descriptor
        desc = &dev->rx_ring[dev->rx_head];
        if (desc->status & RX_OWN_BIT)
            break;  // No more packets
        
        // Extract packet info
        pkt_len = (desc->status >> 16) & 0x1FFF;
        
        // Process packet with copy-break
        if (pkt_len <= COPY_BREAK_THRESHOLD) {
            // Small packet - copy and recycle
            void *staging = get_rx_staging(dev);
            memcpy(staging, desc->buffer, pkt_len);
            desc->status = RX_OWN_BIT;  // Give back immediately
            deliver_packet(staging, pkt_len);
            dev->stats.rx_copybreak++;
        } else {
            // Large packet - zero copy
            deliver_packet(desc->buffer, pkt_len);
            desc->buffer = alloc_rx_buffer(dev);
            desc->status = RX_OWN_BIT;
            dev->stats.rx_zerocopy++;
        }
        
        // Update counters
        dev->rx_head = (dev->rx_head + 1) & RX_RING_MASK;
        processed++;
        bytes += pkt_len;
    }
    
    // Update statistics
    coal->packets_processed += processed;
    coal->bytes_processed += bytes;
    coal->batch_count++;
    if (processed > coal->max_batch_size)
        coal->max_batch_size = processed;
    
    // Adaptive budget adjustment
    adjust_rx_budget(dev, processed);
    
    return processed;
}
```

### Adaptive RX Budget Algorithm

```c
// Dynamically adjust RX budget based on traffic pattern
static void adjust_rx_budget(struct el3_device *dev, int last_batch) {
    struct rx_coal_state *coal = &dev->rx_coal;
    static int history[8];
    static int history_idx = 0;
    int avg_batch, i;
    
    // Record history
    history[history_idx] = last_batch;
    history_idx = (history_idx + 1) & 7;
    
    // Calculate average batch size
    avg_batch = 0;
    for (i = 0; i < 8; i++) {
        avg_batch += history[i];
    }
    avg_batch >>= 3;  // Divide by 8
    
    // Adjust budget based on pattern
    if (avg_batch < 4) {
        // Low traffic - minimize latency
        coal->current_budget = 8;
    } else if (avg_batch < 16) {
        // Moderate traffic
        coal->current_budget = 16;
    } else if (avg_batch < 28) {
        // High traffic
        coal->current_budget = 32;
    } else {
        // Very high traffic - maximize throughput
        coal->current_budget = 64;
    }
}
```

## Combined Interrupt Handling

### Interrupt Handler with Coalescing

```c
// Top-half ISR with coalescing awareness
void __interrupt el3_coalesced_isr(void) {
    uint16_t status;
    struct el3_device *dev = current_device;
    
    // Read and ACK status
    status = inw(dev->iobase + INT_STATUS_REG);
    outw(status, dev->iobase + INT_STATUS_REG);
    
    // Check interrupt type
    if (status & (INT_RX_COMPLETE | INT_RX_EARLY)) {
        // RX interrupt - expect multiple packets
        dev->rx_coal.interrupts_total++;
        queue_work(WORK_RX_BATCH, dev->id);
    }
    
    if (status & INT_TX_COMPLETE) {
        // TX interrupt - expect multiple completions
        dev->tx_coal.interrupts_total++;
        queue_work(WORK_TX_BATCH, dev->id);
    }
    
    // Optional: Mask interrupts if queue deep
    if (work_queue_depth() > COAL_MASK_THRESHOLD) {
        mask_device_interrupts(dev);
        dev->irq_masked = 1;
    }
    
    // EOI
    send_eoi(dev->irq);
}
```

### Bottom-Half with Coalescing

```c
// Bottom-half worker optimized for coalescing
void el3_coalesced_bottom_half(void) {
    struct work_item item;
    int total_rx = 0, total_tx = 0;
    
    while (dequeue_work(&item)) {
        struct el3_device *dev = &devices[item.device_id];
        
        switch (item.type) {
        case WORK_RX_BATCH:
            // Process RX with coalescing
            total_rx += el3_process_rx_coalesced(dev);
            
            // Refill RX ring if needed
            if (dev->rx_free < RX_REFILL_THRESHOLD) {
                bulk_rx_refill(dev);
            }
            break;
            
        case WORK_TX_BATCH:
            // Process TX completions in batch
            total_tx += process_tx_completions_batch(dev, TX_BUDGET);
            break;
        }
    }
    
    // Re-enable interrupts if masked
    unmask_if_needed();
    
    // Update global statistics
    stats.total_rx_coalesced += total_rx;
    stats.total_tx_coalesced += total_tx;
}
```

## Hardware-Specific Implementations

### Vortex (3C59x) - Software Coalescing Only

```c
// Vortex has no hardware coalescing - do it in software
struct vortex_coalescing {
    // PIO FIFO - must read all available packets
    uint16_t fifo_packets_per_irq;
    uint16_t max_fifo_drain;
};

int vortex_rx_coalesce(struct el3_device *dev, int budget) {
    int processed = 0;
    
    // Drain FIFO up to budget
    while (processed < budget) {
        uint16_t rx_status = inw(dev->iobase + RX_STATUS);
        
        if (!(rx_status & RX_STATUS_COMPLETE))
            break;
        
        // Read packet from FIFO
        uint16_t len = rx_status & 0x1FFF;
        read_fifo_packet(dev, len);
        processed++;
    }
    
    dev->vortex_coal.fifo_packets_per_irq = processed;
    return processed;
}
```

### Boomerang/Cyclone (3C90x/B) - Descriptor-Based

```c
// Boomerang/Cyclone support selective TX interrupts via descriptors
void boomerang_setup_tx_coalescing(struct el3_device *dev) {
    // Configure TX descriptor interrupt bits
    dev->tx_ring[0].control = TX_INT_ENABLE;  // First always
    
    for (int i = 1; i < TX_RING_SIZE; i++) {
        if ((i & 7) == 7) {  // Every 8th
            dev->tx_ring[i].control = TX_INT_ENABLE;
        } else {
            dev->tx_ring[i].control = 0;
        }
    }
}
```

### Tornado (3C905C) - Hardware Assist

```c
// Tornado has additional coalescing features
struct tornado_coalescing {
    uint8_t rx_max_coalesced_frames;
    uint8_t tx_max_coalesced_frames;
    uint16_t rx_coalesce_timer;      // In microseconds
    uint16_t tx_coalesce_timer;
};

void tornado_setup_hw_coalescing(struct el3_device *dev) {
    // Enable hardware RX coalescing
    select_window(dev->iobase, WIN_7);
    
    // Set RX coalescing parameters
    outb(16, dev->iobase + RX_COAL_FRAMES);     // 16 frames
    outw(100, dev->iobase + RX_COAL_TIMER);     // 100 us
    
    // Set TX coalescing parameters  
    outb(8, dev->iobase + TX_COAL_FRAMES);      // 8 frames
    outw(200, dev->iobase + TX_COAL_TIMER);     // 200 us
    
    // Enable coalescing
    outw(COAL_ENABLE, dev->iobase + COAL_CONTROL);
    
    select_window(dev->iobase, WIN_1);
}
```

## Doorbell Batching

### Batch Multiple TX Kicks

```c
// Doorbell coalescing - reduce I/O writes
struct doorbell_state {
    uint8_t pending_kicks;
    uint16_t last_kick_time;
    uint16_t kick_threshold;
};

void el3_batch_doorbell(struct el3_device *dev) {
    struct doorbell_state *db = &dev->doorbell;
    
    db->pending_kicks++;
    
    // Kick if threshold reached or timeout
    if (db->pending_kicks >= db->kick_threshold ||
        (get_timer_ms() - db->last_kick_time) > 1) {
        
        // Single doorbell for all pending
        outl(dev->tx_ring_phys + 
             (dev->tx_head * sizeof(struct tx_descriptor)),
             dev->iobase + DN_LIST_PTR);
        
        db->pending_kicks = 0;
        db->last_kick_time = get_timer_ms();
        dev->stats.doorbells++;
    } else {
        dev->stats.doorbells_saved++;
    }
}
```

## Performance Metrics

### Coalescing Statistics

```c
struct coalescing_statistics {
    // TX statistics
    uint32_t tx_interrupts_requested;
    uint32_t tx_interrupts_suppressed;
    uint32_t tx_packets_coalesced;
    uint32_t tx_max_coalesced;
    float tx_avg_coalesced;
    
    // RX statistics
    uint32_t rx_interrupts_total;
    uint32_t rx_packets_coalesced;
    uint32_t rx_max_batch_size;
    float rx_avg_batch_size;
    
    // Doorbell statistics
    uint32_t doorbells_total;
    uint32_t doorbells_saved;
    
    // Efficiency metrics
    float interrupts_per_packet;
    float packets_per_interrupt;
    uint32_t cpu_cycles_saved;
};

void print_coalescing_stats(struct el3_device *dev) {
    struct coalescing_statistics *s = &dev->coal_stats;
    
    printf("Interrupt Coalescing Statistics:\n");
    printf("TX Coalescing:\n");
    printf("  Interrupts: %lu requested, %lu suppressed (%.1f%% saved)\n",
           s->tx_interrupts_requested,
           s->tx_interrupts_suppressed,
           100.0 * s->tx_interrupts_suppressed / 
           (s->tx_interrupts_requested + s->tx_interrupts_suppressed));
    printf("  Avg TX batch: %.1f packets\n", s->tx_avg_coalesced);
    printf("  Max TX batch: %lu packets\n", s->tx_max_coalesced);
    
    printf("RX Coalescing:\n");
    printf("  Total interrupts: %lu\n", s->rx_interrupts_total);
    printf("  Avg RX batch: %.1f packets\n", s->rx_avg_batch_size);
    printf("  Max RX batch: %lu packets\n", s->rx_max_batch_size);
    
    printf("Overall:\n");
    printf("  Packets/interrupt: %.1f\n", s->packets_per_interrupt);
    printf("  CPU cycles saved: %lu million\n", s->cpu_cycles_saved / 1000000);
}
```

## Configuration Tuning

### Static Configuration

```c
// Compile-time defaults in el3_config.h
#define DEFAULT_TX_COAL_PACKETS    8
#define DEFAULT_TX_COAL_BYTES      8192
#define DEFAULT_TX_COAL_TIMER_MS   10
#define DEFAULT_RX_COAL_PACKETS    32
#define DEFAULT_RX_COAL_BYTES      49152
#define DEFAULT_RX_COAL_TIME_MS    2
```

### Runtime Tuning API

```c
// Allow runtime adjustment via packet driver extensions
int el3_set_coalescing(struct el3_device *dev,
                      struct coalescing_params *params) {
    // Validate parameters
    if (params->tx_packets > TX_RING_SIZE / 2)
        return -EINVAL;
    if (params->rx_packets > RX_RING_SIZE)
        return -EINVAL;
    
    // Update TX coalescing
    dev->tx_coal_config.packet_threshold = params->tx_packets;
    dev->tx_coal_config.byte_threshold = params->tx_bytes;
    dev->tx_coal_config.time_threshold_ms = params->tx_time_ms;
    
    // Update RX coalescing
    dev->rx_coal_config.packet_budget = params->rx_packets;
    dev->rx_coal_config.byte_budget = params->rx_bytes;
    dev->rx_coal_config.time_budget_ms = params->rx_time_ms;
    
    // Apply hardware-specific settings if available
    if (dev->generation == EL3_GEN_TORNADO) {
        tornado_update_hw_coalescing(dev);
    }
    
    return 0;
}
```

## Testing Scenarios

### 1. Bulk Transfer Test
- **Traffic**: 1500-byte packets at maximum rate
- **Expected**: Maximum coalescing (32 packets/interrupt)
- **Metrics**: Throughput, CPU usage, interrupts/second

### 2. Interactive Test
- **Traffic**: Mixed small packets (ping, SSH)
- **Expected**: Low coalescing (1-4 packets/interrupt)
- **Metrics**: Latency, jitter

### 3. Adaptive Test
- **Traffic**: Varying load patterns
- **Expected**: Budget adjusts automatically
- **Metrics**: Response time to load changes

### 4. Stress Test
- **Traffic**: Burst patterns exceeding budgets
- **Expected**: No packet loss
- **Metrics**: Queue depths, overflow events

## Expected Performance

### Before Coalescing
- Interrupts: 20,000/second at 20K pps
- CPU usage: 15% at 10K pps
- Latency: 50 μs average

### After Coalescing
- Interrupts: 625-2,500/second at 20K pps (8-32x reduction)
- CPU usage: 5% at 10K pps (67% reduction)
- Latency: 55 μs average (10% increase acceptable)

### Efficiency Gains
- **85% fewer interrupts** for bulk traffic
- **60% less CPU** overhead
- **Minimal latency impact** (<10% increase)
- **No throughput degradation**

## Conclusion

Interrupt coalescing provides dramatic CPU usage reduction with minimal latency impact through:
- Selective TX interrupt marking
- Batched RX processing with budgets
- Adaptive algorithms for varying traffic
- Hardware-specific optimizations where available
- Comprehensive monitoring and tuning capabilities

This design maintains DOS real-mode compatibility while achieving performance comparable to modern network drivers.