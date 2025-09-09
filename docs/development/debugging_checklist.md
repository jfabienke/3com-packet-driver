# Debugging Checklist for 3Com Packet Driver

## Overview

This comprehensive debugging checklist provides systematic troubleshooting procedures for the 3Com Packet Driver, covering hardware detection, IRQ configuration, memory management, and packet processing issues. The checklist is organized by problem category with specific diagnostic steps and resolution procedures.

## Table of Contents

1. [Hardware Detection Issues](#hardware-detection-issues)
2. [IRQ Configuration Problems](#irq-configuration-problems)
3. [Memory Management Issues](#memory-management-issues)
4. [Packet Processing Problems](#packet-processing-problems)
5. [Multi-NIC Integration Issues](#multi-nic-integration-issues)
6. [Performance and Timing Problems](#performance-and-timing-problems)
7. [DOS Integration Issues](#dos-integration-issues)
8. [Assembly/C Interface Problems](#assemblyc-interface-problems)

## Hardware Detection Issues

### Problem: No NICs Detected

#### Initial Diagnostics
- [ ] **Verify Hardware Installation**
  ```
  Action: Physical inspection of NIC installation
  Check: NIC seated properly in ISA/PCI slot
  Check: Power connections secure
  Check: LED indicators on NIC (if present)
  ```

- [ ] **Check I/O Address Configuration**
  ```
  Command: DEBUG.COM or similar I/O port scanner
  Test ports: 0x100-0x3FF (ISA), 0x1000-0x9FFF (EISA)
  Expected: Read response from NIC registers
  
  Manual test:
  - Load DEBUG.COM
  - Try: i 300 (test port 0x300)
  - Try: i 320 (test port 0x320) 
  - Look for consistent values vs. 0xFF
  ```

- [ ] **Verify Hardware Detection Logic**
  ```c
  // Enable debug output in hardware.c
  #define DEBUG_HARDWARE_DETECTION 1
  
  // Check hardware_detect_all() output
  printf("Scanning I/O range 0x%04X-0x%04X\n", start_addr, end_addr);
  printf("Found signature at 0x%04X: 0x%04X\n", addr, signature);
  ```

#### Advanced Diagnostics

- [ ] **3C509B Specific Detection**
  ```assembly
  ; Manual 3C509B detection sequence
  mov dx, 0x100           ; ID port
  mov al, 0               ; Reset sequence
  out dx, al
  
  ; Send activation sequence  
  mov cx, 255             ; Activation loop count
  activation_loop:
      mov al, 0xFF        ; Activation pattern
      out dx, al
      loop activation_loop
      
  ; Try to read manufacturer ID
  mov al, 0x87            ; Read manufacturer ID command
  out dx, al
  in ax, dx               ; Should return 3Com ID
  ```

- [ ] **3C515-TX PCI Detection**
  ```c
  // PCI configuration space access
  outportl(0xCF8, 0x80000000 | (bus << 16) | (dev << 11) | (func << 8));
  vendor_id = inportw(0xCFC);
  device_id = inportw(0xCFE);
  
  // Check for 3Com vendor ID (0x10B7) and 3C515 device ID
  if (vendor_id == 0x10B7 && device_id == 0x5900) {
      printf("Found 3C515-TX at PCI %d:%d:%d\n", bus, dev, func);
  }
  ```

#### Resolution Steps

- [ ] **Manual I/O Base Configuration**
  ```
  CONFIG.SYS line: DEVICE=3CDRV.COM /IO1=0x300 /IO2=0x320
  
  Test with known-good addresses:
  - 0x300, 0x320, 0x340 (common ISA addresses)
  - Avoid conflicts with sound cards (0x220, 0x240)
  - Avoid conflicts with COM ports (0x3F8, 0x2F8)
  ```

- [ ] **BIOS/CMOS Configuration Check**
  ```
  Check: PnP OS setting (should be "No" for DOS)
  Check: PCI IRQ assignments
  Check: ISA slot configurations
  Verify: No resource conflicts with other devices
  ```

### Problem: Partial Hardware Detection

#### Diagnostic Steps
- [ ] **Check Detection Logic Flow**
  ```c
  // Add debug output to each detection stage
  printf("Stage 1: I/O scanning - %s\n", 
         io_scan_result ? "PASS" : "FAIL");
  printf("Stage 2: Signature validation - %s\n",
         signature_valid ? "PASS" : "FAIL");  
  printf("Stage 3: MAC address read - %s\n",
         mac_read_success ? "PASS" : "FAIL");
  ```

- [ ] **Hardware State Verification**
  ```assembly
  ; Check NIC initialization state
  mov dx, [nic_io_base]
  add dx, REG_STATUS
  in ax, dx
  test ax, STATUS_RESET_COMPLETE
  jz still_resetting
  ```

#### Resolution Procedures
- [ ] **Add Hardware Delays**
  ```c
  // Add delays between detection operations
  delay_microseconds(1000);  // 1ms delay after reset
  delay_microseconds(500);   // 500μs delay after commands
  ```

- [ ] **Implement Detection Retry Logic**
  ```c
  int detect_attempts = 3;
  while (detect_attempts-- > 0) {
      if (detect_nic_hardware(nic_id) == 0) {
          break;  // Success
      }
      delay_milliseconds(10);  // Wait before retry
  }
  ```

## IRQ Configuration Problems

### Problem: Interrupts Not Firing

#### Initial Checks
- [ ] **Verify IRQ Availability**
  ```
  Check: IRQ not used by other devices
  Common conflicts: IRQ 3 (COM2), IRQ 4 (COM1), IRQ 5 (LPT2)
  Preferred IRQs: 9, 10, 11, 12 (usually available)
  
  DOS command: MSD.EXE (Microsoft Diagnostics)
  Check Hardware section for IRQ usage
  ```

- [ ] **Test IRQ Installation**
  ```c
  // Verify interrupt vector installation
  void (interrupt far *old_handler)();
  void (interrupt far *current_handler)();
  
  old_handler = getvect(irq_vector);
  setvect(irq_vector, new_handler);
  current_handler = getvect(irq_vector);
  
  if (current_handler != new_handler) {
      printf("ERROR: IRQ vector not installed properly\n");
  }
  ```

#### Hardware IRQ Diagnostics

- [ ] **Manual IRQ Trigger Test**
  ```assembly
  ; Enable interrupts on NIC and trigger manually
  mov dx, [nic_io_base]
  add dx, REG_COMMAND
  mov ax, CMD_ENABLE_IRQ
  out dx, ax
  
  ; Trigger interrupt by enabling receiver
  mov ax, CMD_RX_ENABLE
  out dx, ax
  ```

- [ ] **IRQ Acknowledgment Check**
  ```c
  volatile int irq_count = 0;
  
  void interrupt irq_handler() {
      irq_count++;
      
      // Read interrupt status
      unsigned int status = inportw(nic_base + REG_INT_STATUS);
      printf("IRQ fired, status: 0x%04X\n", status);
      
      // Clear interrupt
      outportw(nic_base + REG_COMMAND, CMD_ACK_INTERRUPT);
      
      // Send EOI
      outportb(0x20, 0x20);
  }
  ```

#### Advanced IRQ Debugging

- [ ] **Interrupt Controller Programming**
  ```c
  // Check 8259 PIC configuration
  unsigned char pic1_mask = inportb(0x21);
  unsigned char pic2_mask = inportb(0xA1);
  
  printf("PIC1 mask: 0x%02X, PIC2 mask: 0x%02X\n", 
         pic1_mask, pic2_mask);
         
  // Ensure IRQ is not masked
  int irq_line = nic_irq;
  if (irq_line >= 8) {
      // IRQ on secondary PIC
      irq_line -= 8;
      pic2_mask &= ~(1 << irq_line);
      outportb(0xA1, pic2_mask);
      
      // Enable cascade on primary PIC
      pic1_mask &= ~(1 << 2);
      outportb(0x21, pic1_mask);
  } else {
      // IRQ on primary PIC
      pic1_mask &= ~(1 << irq_line);
      outportb(0x21, pic1_mask);
  }
  ```

#### Resolution Procedures

- [ ] **IRQ Conflict Resolution**
  ```
  Step 1: Identify conflicting device
  Step 2: Reconfigure conflicting device or disable
  Step 3: Use /IRQ1= and /IRQ2= parameters
  Step 4: Test with known-good IRQ (e.g., IRQ 10)
  
  Example: DEVICE=3CDRV.COM /IRQ1=10 /IRQ2=11
  ```

- [ ] **Shared IRQ Implementation**
  ```c
  void interrupt shared_irq_handler() {
      // Check if this NIC generated the interrupt
      if (check_nic_interrupt_status(0)) {
          handle_nic0_interrupt();
      }
      if (check_nic_interrupt_status(1)) {
          handle_nic1_interrupt();
      }
      
      // Chain to previous handler if interrupt not ours
      if (!interrupt_handled) {
          old_irq_handler();
      }
  }
  ```

### Problem: Spurious Interrupts

#### Diagnostic Steps
- [ ] **Interrupt Source Identification**
  ```c
  void interrupt debug_irq_handler() {
      static int spurious_count = 0;
      unsigned int status0 = inportw(nic0_base + REG_INT_STATUS);
      unsigned int status1 = inportw(nic1_base + REG_INT_STATUS);
      
      if (status0 == 0 && status1 == 0) {
          spurious_count++;
          printf("Spurious interrupt #%d\n", spurious_count);
      }
      
      // Continue with normal processing
      normal_irq_handler();
  }
  ```

- [ ] **Hardware Status Monitoring**
  ```assembly
  ; Check all possible interrupt sources
  mov dx, [nic_base]
  add dx, REG_INT_STATUS
  in ax, dx
  
  test ax, INT_TX_COMPLETE
  jnz handle_tx_done
  test ax, INT_RX_COMPLETE  
  jnz handle_rx_done
  test ax, INT_ERROR
  jnz handle_error
  
  ; No known interrupt source - spurious
  jmp spurious_interrupt
  ```

#### Resolution Steps
- [ ] **Implement Interrupt Filtering**
  ```c
  #define MAX_SPURIOUS_IRQS 10
  static int spurious_irq_count = 0;
  
  void interrupt filtered_irq_handler() {
      if (!validate_interrupt_source()) {
          spurious_irq_count++;
          if (spurious_irq_count > MAX_SPURIOUS_IRQS) {
              disable_nic_interrupts();
              printf("Too many spurious IRQs - disabling\n");
          }
          goto irq_exit;
      }
      
      process_valid_interrupt();
      
  irq_exit:
      outportb(0x20, 0x20);  // EOI
  }
  ```

## Memory Management Issues

### Problem: Memory Allocation Failures

#### Initial Diagnostics
- [ ] **Check Available Memory**
  ```c
  // Check conventional memory
  unsigned long conv_mem = get_conventional_memory();
  printf("Conventional memory: %ld KB available\n", conv_mem / 1024);
  
  // Check XMS memory
  if (xms_available()) {
      unsigned long xms_mem = get_xms_memory();
      printf("XMS memory: %ld KB available\n", xms_mem / 1024);
  } else {
      printf("XMS memory: Not available\n");
  }
  ```

- [ ] **Memory Fragmentation Analysis**
  ```c
  void analyze_memory_fragmentation() {
      size_t largest_block = 0;
      size_t total_free = 0;
      int free_blocks = 0;
      
      // Walk memory pool and analyze
      struct memory_block *block = memory_pool_head;
      while (block) {
          if (block->free) {
              free_blocks++;
              total_free += block->size;
              if (block->size > largest_block) {
                  largest_block = block->size;
              }
          }
          block = block->next;
      }
      
      printf("Free memory: %zu bytes in %d blocks\n", 
             total_free, free_blocks);
      printf("Largest block: %zu bytes\n", largest_block);
  }
  ```

#### Advanced Memory Diagnostics

- [ ] **Memory Leak Detection**
  ```c
  #ifdef DEBUG_MEMORY
  struct allocation_record {
      void *ptr;
      size_t size;
      char file[32];
      int line;
      struct allocation_record *next;
  };
  
  static struct allocation_record *allocations = NULL;
  
  void *debug_malloc(size_t size, char *file, int line) {
      void *ptr = malloc(size);
      if (ptr) {
          struct allocation_record *rec = malloc(sizeof(*rec));
          rec->ptr = ptr;
          rec->size = size;
          strncpy(rec->file, file, 31);
          rec->line = line;
          rec->next = allocations;
          allocations = rec;
      }
      return ptr;
  }
  
  #define malloc(size) debug_malloc(size, __FILE__, __LINE__)
  #endif
  ```

- [ ] **Buffer Overflow Detection**
  ```c
  #define GUARD_MAGIC 0xDEADBEEF
  
  typedef struct {
      unsigned long guard_start;
      size_t size;
      char data[1];  // Variable length data
      // guard_end follows data
  } guarded_buffer_t;
  
  void *alloc_guarded_buffer(size_t size) {
      size_t total_size = sizeof(guarded_buffer_t) + size + sizeof(unsigned long);
      guarded_buffer_t *buf = malloc(total_size);
      
      if (buf) {
          buf->guard_start = GUARD_MAGIC;
          buf->size = size;
          *(unsigned long *)(buf->data + size) = GUARD_MAGIC;
      }
      
      return buf ? buf->data : NULL;
  }
  
  int check_buffer_guards(void *ptr) {
      guarded_buffer_t *buf = (guarded_buffer_t *)((char *)ptr - offsetof(guarded_buffer_t, data));
      unsigned long *guard_end = (unsigned long *)(buf->data + buf->size);
      
      if (buf->guard_start != GUARD_MAGIC) {
          printf("Buffer underrun detected at %p\n", ptr);
          return -1;
      }
      
      if (*guard_end != GUARD_MAGIC) {
          printf("Buffer overrun detected at %p\n", ptr);
          return -1;
      }
      
      return 0;
  }
  ```

#### Resolution Procedures

- [ ] **Implement Memory Pool Management**
  ```c
  // Pre-allocate fixed-size memory pools
  #define SMALL_BUFFER_SIZE   128
  #define MEDIUM_BUFFER_SIZE  512
  #define LARGE_BUFFER_SIZE   1514
  
  #define SMALL_POOL_COUNT    32
  #define MEDIUM_POOL_COUNT   16
  #define LARGE_POOL_COUNT    8
  
  static char small_buffers[SMALL_POOL_COUNT][SMALL_BUFFER_SIZE];
  static char medium_buffers[MEDIUM_POOL_COUNT][MEDIUM_BUFFER_SIZE];
  static char large_buffers[LARGE_POOL_COUNT][LARGE_BUFFER_SIZE];
  
  static unsigned long small_pool_mask = 0;  // Bit mask for allocation
  static unsigned long medium_pool_mask = 0;
  static unsigned long large_pool_mask = 0;
  
  void *pool_alloc(size_t size) {
      if (size <= SMALL_BUFFER_SIZE) {
          return alloc_from_pool(small_buffers, &small_pool_mask, 
                               SMALL_POOL_COUNT, SMALL_BUFFER_SIZE);
      } else if (size <= MEDIUM_BUFFER_SIZE) {
          return alloc_from_pool(medium_buffers, &medium_pool_mask,
                               MEDIUM_POOL_COUNT, MEDIUM_BUFFER_SIZE);
      } else if (size <= LARGE_BUFFER_SIZE) {
          return alloc_from_pool(large_buffers, &large_pool_mask,
                               LARGE_POOL_COUNT, LARGE_BUFFER_SIZE);
      }
      
      // Fall back to system malloc for oversized allocations
      return malloc(size);
  }
  ```

### Problem: DMA Memory Alignment Issues

#### Diagnostic Steps
- [ ] **Check DMA Buffer Alignment**
  ```c
  void check_dma_alignment(void *buffer, size_t size) {
      unsigned long addr = (unsigned long)buffer;
      
      // Check 4-byte alignment (minimum for 80386 DMA)
      if (addr & 0x03) {
          printf("ERROR: DMA buffer not 4-byte aligned: 0x%08lX\n", addr);
      }
      
      // Check 64KB boundary crossing
      if ((addr & 0xFFFF0000) != ((addr + size - 1) & 0xFFFF0000)) {
          printf("ERROR: DMA buffer crosses 64KB boundary\n");
          printf("Start: 0x%08lX, End: 0x%08lX\n", addr, addr + size - 1);
      }
      
      // Check 16MB limit for ISA DMA
      if (addr + size > 0x01000000) {
          printf("ERROR: DMA buffer above 16MB limit\n");
      }
  }
  ```

#### Resolution Steps
- [ ] **Implement Aligned DMA Allocation**
  ```c
  void *alloc_dma_buffer(size_t size, int alignment) {
      // Allocate extra space for alignment
      size_t alloc_size = size + alignment - 1;
      char *raw_buffer = malloc(alloc_size);
      
      if (!raw_buffer) {
          return NULL;
      }
      
      // Calculate aligned address
      unsigned long addr = (unsigned long)raw_buffer;
      unsigned long aligned_addr = (addr + alignment - 1) & ~(alignment - 1);
      char *aligned_buffer = (char *)aligned_addr;
      
      // Check 64KB boundary
      if ((aligned_addr & 0xFFFF0000) != 
          ((aligned_addr + size - 1) & 0xFFFF0000)) {
          // Realign to start of next 64KB boundary
          aligned_addr = (aligned_addr + 0x10000) & 0xFFFF0000;
          aligned_buffer = (char *)aligned_addr;
          
          // Check if we still fit in allocated space
          if (aligned_addr + size > (unsigned long)raw_buffer + alloc_size) {
              free(raw_buffer);
              return alloc_dma_buffer_large(size, alignment);
          }
      }
      
      return aligned_buffer;
  }
  ```

## Packet Processing Problems

### Problem: Packet Transmission Failures

#### Initial Diagnostics
- [ ] **Check Transmit FIFO Status**
  ```c
  int check_transmit_status(int nic_id) {
      unsigned int status = read_nic_register(nic_id, REG_TX_STATUS);
      
      printf("TX Status: 0x%04X\n", status);
      
      if (status & TX_STATUS_FIFO_FULL) {
          printf("ERROR: Transmit FIFO full\n");
          return -1;
      }
      
      if (status & TX_STATUS_UNDERRUN) {
          printf("ERROR: Transmit underrun\n");
          return -2;
      }
      
      if (status & TX_STATUS_JABBER) {
          printf("ERROR: Jabber detected\n");
          return -3;
      }
      
      return 0;
  }
  ```

- [ ] **Validate Packet Contents**
  ```c
  int validate_packet_structure(void *packet, int length) {
      unsigned char *pkt = (unsigned char *)packet;
      
      // Check minimum Ethernet frame size
      if (length < 64) {
          printf("ERROR: Packet too small (%d bytes)\n", length);
          return -1;
      }
      
      // Check maximum Ethernet frame size
      if (length > 1518) {
          printf("ERROR: Packet too large (%d bytes)\n", length);
          return -2;
      }
      
      // Validate Ethernet header structure
      printf("Dst MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
             pkt[0], pkt[1], pkt[2], pkt[3], pkt[4], pkt[5]);
      printf("Src MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", 
             pkt[6], pkt[7], pkt[8], pkt[9], pkt[10], pkt[11]);
      printf("EtherType: 0x%04X\n", *(unsigned int *)(pkt + 12));
      
      return 0;
  }
  ```

#### Hardware-Specific Diagnostics

- [ ] **3C509B Transmission Debug**
  ```assembly
  ; Check 3C509B specific transmission status
  mov dx, [nic_io_base]
  add dx, REG_STATUS
  in ax, dx
  
  test ax, STATUS_TX_AVAILABLE
  jz tx_not_ready
  
  ; Check FIFO space
  add dx, REG_FREE_BYTES
  in ax, dx
  cmp ax, [packet_length]
  jb insufficient_fifo_space
  ```

- [ ] **3C515-TX DMA Debug**
  ```c
  // Check 3C515-TX DMA status
  void debug_3c515_dma(int nic_id) {
      unsigned int dma_ctrl = read_nic_register(nic_id, REG_DMA_CTRL);
      unsigned int dma_status = read_nic_register(nic_id, REG_DMA_STATUS);
      
      printf("DMA Control: 0x%04X\n", dma_ctrl);
      printf("DMA Status: 0x%04X\n", dma_status);
      
      if (!(dma_ctrl & DMA_CTRL_ENABLE)) {
          printf("ERROR: DMA not enabled\n");
      }
      
      if (dma_status & DMA_STATUS_ERROR) {
          printf("ERROR: DMA error detected\n");
      }
  }
  ```

#### Resolution Procedures

- [ ] **Implement Transmission Retry Logic**
  ```c
  int reliable_packet_send(int nic_id, void *packet, int length) {
      int attempts = 3;
      int result;
      
      while (attempts-- > 0) {
          result = hardware_send_packet(nic_id, packet, length);
          
          if (result == 0) {
              break;  // Success
          }
          
          if (result == ERR_NIC_BUSY) {
              // Wait and retry
              delay_milliseconds(1);
              continue;
          }
          
          if (result == ERR_TX_UNDERRUN) {
              // Reset transmitter and retry
              reset_transmitter(nic_id);
              delay_milliseconds(10);
              continue;
          }
          
          // Unrecoverable error
          break;
      }
      
      return result;
  }
  ```

### Problem: Packet Reception Issues

#### Diagnostic Steps
- [ ] **Check Receive FIFO Status**
  ```c
  void debug_receive_status(int nic_id) {
      unsigned int rx_status = read_nic_register(nic_id, REG_RX_STATUS);
      unsigned int fifo_bytes = read_nic_register(nic_id, REG_RX_BYTES);
      
      printf("RX Status: 0x%04X\n", rx_status);
      printf("FIFO Bytes: %d\n", fifo_bytes);
      
      if (rx_status & RX_STATUS_ERROR) {
          printf("Receive error flags: ");
          if (rx_status & RX_ERROR_CRC) printf("CRC ");
          if (rx_status & RX_ERROR_FRAME) printf("FRAME ");
          if (rx_status & RX_ERROR_OVERRUN) printf("OVERRUN ");
          printf("\n");
      }
  }
  ```

- [ ] **Monitor Receive Buffer Usage**
  ```c
  void monitor_rx_buffers() {
      static int max_buffers_used = 0;
      int current_buffers = count_allocated_rx_buffers();
      
      if (current_buffers > max_buffers_used) {
          max_buffers_used = current_buffers;
          printf("Peak RX buffer usage: %d\n", max_buffers_used);
      }
      
      if (current_buffers >= (MAX_RX_BUFFERS * 0.8)) {
          printf("WARNING: RX buffer pool nearly exhausted (%d/%d)\n",
                 current_buffers, MAX_RX_BUFFERS);
      }
  }
  ```

## Multi-NIC Integration Issues

### Problem: NIC Selection/Routing Failures

#### Diagnostic Procedures
- [ ] **Debug Routing Decision Process**
  ```c
  int debug_route_selection(void *packet, int length) {
      struct routing_decision decision;
      
      // Analyze packet for routing hints
      analyze_packet_flow(packet, length, &decision);
      printf("Flow hash: 0x%08X\n", decision.flow_hash);
      printf("Protocol: %d\n", decision.protocol);
      
      // Check routing table
      int preferred_nic = lookup_route_table(&decision);
      printf("Route table result: NIC %d\n", preferred_nic);
      
      // Apply load balancing
      int selected_nic = apply_load_balancing(preferred_nic, &decision);
      printf("Load balancing result: NIC %d\n", selected_nic);
      
      // Validate NIC availability
      if (!is_nic_available(selected_nic)) {
          printf("Selected NIC %d not available, failing over\n", selected_nic);
          selected_nic = find_fallback_nic(selected_nic);
      }
      
      return selected_nic;
  }
  ```

#### Resolution Steps
- [ ] **Implement Failover Logic**
  ```c
  int robust_nic_selection(void *packet, int length) {
      // Primary selection algorithm
      int primary_nic = route_select_nic_primary(packet, length);
      
      if (is_nic_healthy(primary_nic)) {
          return primary_nic;
      }
      
      // Failover to secondary NIC
      int secondary_nic = (primary_nic == 0) ? 1 : 0;
      if (is_nic_healthy(secondary_nic)) {
          log_warning("Failed over from NIC %d to NIC %d", 
                     primary_nic, secondary_nic);
          return secondary_nic;
      }
      
      // No healthy NICs available
      return -1;
  }
  ```

This debugging checklist provides systematic procedures for identifying and resolving common issues in the 3Com Packet Driver. The combination of diagnostic steps and resolution procedures enables efficient troubleshooting of hardware, software, and integration problems.