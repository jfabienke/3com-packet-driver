;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file nic_irq_smc.asm
;; @brief NIC Interrupt Handler with Self-Modifying Code patch points
;;
;; Implements optimized interrupt handlers for 3C509B and 3C515 NICs with
;; CPU-specific patches applied during initialization.
;;
;; Design Decisions:
;; - packet_buffer defined in _DATA segment for proper segment management
;; - DS and ES both set to _DATA for consistent access to variables and buffers
;; - Direction flag (DF) explicitly cleared before string operations
;; - 16-bit word transfers (rep insw) used for optimal FIFO performance
;; - Mitigation loop checks FIFO empty status for early exit
;; - NIC interrupt acknowledged before PIC EOI to prevent re-triggering
;;
;; Constraints:
;; - ISR execution <100μs
;; - No reentrancy (protected)
;; - Proper EOI to PIC (slave before master for IRQ>=8)
;; - All segment registers preserved (including ES)
;; - BP preserved for stack frame compatibility
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        .8086                           ; Base compatibility
        .model small

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; CPU Optimization Constants and Macros
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; External CPU optimization level from packet_ops.asm
EXTRN current_cpu_opt:BYTE

; CPU optimization level constants (must match packet_ops.asm)
OPT_8086            EQU 0       ; 8086/8088 baseline (no 186+ instructions)
OPT_16BIT           EQU 1       ; 186+ optimizations (INS/OUTS available)
OPT_32BIT           EQU 2       ; 386+ optimizations (32-bit registers)

;------------------------------------------------------------------------------
; INSW_SAFE - Input word array from port (8086-compatible)
; Input: ES:DI = dest buffer, DX = port, CX = word count
; Clobbers: AX, CX, DI
; Note: Caller must set direction flag (CLD) before calling
;------------------------------------------------------------------------------
INSW_SAFE MACRO
        LOCAL use_insw, insw_loop, done
        push bx
        mov bl, [current_cpu_opt]
        test bl, OPT_16BIT
        pop bx
        jnz use_insw
        ;; 8086 path: manual loop
        jcxz done               ; Skip if CX = 0
insw_loop:
        in ax, dx               ; AX = word from port
        stosw                   ; [ES:DI] = AX, DI += 2
        loop insw_loop          ; Decrement CX, loop if not zero
        jmp short done
use_insw:
        ;; 186+ path: use REP INSW
        rep insw
done:
ENDM

;------------------------------------------------------------------------------
; INSB_SAFE - Input single byte from port (always 8086-compatible)
; This is just INB followed by STOSB, which works on all CPUs
; Input: ES:DI = dest buffer, DX = port
; Clobbers: AL, DI
;------------------------------------------------------------------------------
INSB_SAFE MACRO
        in al, dx               ; AL = byte from port
        stosb                   ; [ES:DI] = AL, DI += 1
ENDM

;------------------------------------------------------------------------------
; REP_INSB_SAFE - Input byte array from port (8086-compatible)
; Input: ES:DI = dest buffer, DX = port, CX = byte count
; Clobbers: AL, CX, DI
; Note: Caller must set direction flag (CLD) before calling
;------------------------------------------------------------------------------
REP_INSB_SAFE MACRO
        LOCAL use_insb, insb_loop, done
        push bx
        mov bl, [current_cpu_opt]
        test bl, OPT_16BIT
        pop bx
        jnz use_insb
        ;; 8086 path: manual loop
        jcxz done               ; Skip if CX = 0
insb_loop:
        in al, dx               ; AL = byte from port
        stosb                   ; [ES:DI] = AL, DI += 1
        loop insb_loop          ; Decrement CX, loop if not zero
        jmp short done
use_insb:
        ;; 186+ path: use REP INSB
        rep insb
done:
ENDM

        .code

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Module Header (64 bytes)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        align 16
module_header:
nic_irq_module_header:                  ; Export for C code
        public  nic_irq_module_header   ; Make visible to patch_apply.c
        db      'PKTDRV',0              ; 7+1 bytes: Signature
        db      1, 0                    ; 2 bytes: Version 1.0
        dw      hot_section_start       ; 2 bytes: Hot start
        dw      hot_section_end         ; 2 bytes: Hot end
        dw      cold_section_start      ; 2 bytes: Cold start
        dw      cold_section_end        ; 2 bytes: Cold end
        dw      patch_table             ; 2 bytes: Patch table
        dw      patch_count             ; 2 bytes: Number of patches
        dw      module_size             ; 2 bytes: Total size
        dw      2*1024                  ; 2 bytes: Required memory (2KB)
        db      2                       ; 1 byte: Min CPU (286)
        db      0                       ; 1 byte: NIC type (any)
        db      37 dup(0)               ; 37 bytes: Reserved

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; HOT SECTION - Resident interrupt handlers
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        align 16
hot_section_start:

        ; Public exports
        public  nic_irq_handler
        public  nic_3c509_handler
        public  nic_3c515_handler
        public  PATCH_3c515_transfer    ; Export patch point
        public  PATCH_dma_boundary_check ; Export patch point
        public  PATCH_cache_flush_pre    ; Export patch point
        public  PATCH_cache_flush_post   ; Export patch point

        ; External references
        extern  packet_isr_receive:near
        extern  statistics:byte

        ; Constants
        ISR_REENTRY_FLAG    equ 0x01
        PIO_MAX_PACKETS     equ 8       ; 3C509B (PIO) batch cap per IRQ
        DMA_MAX_PACKETS     equ 32      ; 3C515 (bus master) batch cap per IRQ

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Main NIC IRQ Handler - Tiny ISR Fast Path
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
nic_irq_handler:
        ; TINY ISR OPTIMIZATION: Minimal register saves for fast path
        ; Save essential registers and establish DS
        push    ax
        push    dx
        push    ds
        
        ; CRITICAL: Establish data segment for accessing driver variables
        mov     ax, seg _DATA
        mov     ds, ax                  ; DS = _DATA for variable access
        
        ; Quick ownership check - is this interrupt for us?
        mov     dx, [nic_io_base]       ; Access from data segment
        add     dx, 0x0E                ; INT_STATUS register (16-bit on 3C509B/515)
        in      ax, dx                  ; 16-bit read per 3Com datasheet
        mov     [int_status], ax        ; Save full status
        test    ax, ax                  ; Any bits set?
        jz      .not_ours               ; Not our interrupt
        
        ; Check if only simple RX/TX bits are set (90% of cases)
        mov     dx, ax                  ; Save original status
        and     dx, 0xFFEC              ; Mask OFF RX_COMPLETE(0x10) | TX_COMPLETE(0x02) | TX_AVAIL(0x01)
        jnz     .complex_interrupt      ; Other bits set, need full handler
        
        ; FAST PATH: Simple RX or TX acknowledgment
        ; First, acknowledge interrupt in NIC (write-1-to-clear)
        mov     dx, [nic_io_base]
        add     dx, 0x0E                ; INT_STATUS register
        out     dx, ax                  ; Write back status to clear
        
        test    ax, 0x0010              ; RX_COMPLETE?
        jz      .check_tx_fast
        
        ; Minimal RX acknowledgment
        inc     word ptr [rx_ack_count] ; Update counter
        
.check_tx_fast:
        test    ax, 0x0002              ; TX_COMPLETE?
        jz      .fast_done
        
        ; Minimal TX acknowledgment  
        inc     word ptr [tx_ack_count] ; Update counter
        
.fast_done:
        ; Send EOI and exit fast path
        cmp     byte ptr [effective_nic_irq], 8
        jb      .fast_master_only
        mov     al, 20h
        out     0A0h, al                ; EOI to slave PIC
.fast_master_only:
        mov     al, 20h
        out     20h, al                 ; EOI to master PIC
        
        pop     ds
        pop     dx
        pop     ax
        iret
        
.not_ours:
        ; Not our interrupt but MUST still send EOI for spurious interrupts
        cmp     byte ptr [effective_nic_irq], 8
        jb      .not_ours_master_only
        mov     al, 20h
        out     0A0h, al                ; EOI to slave PIC
.not_ours_master_only:
        mov     al, 20h
        out     20h, al                 ; EOI to master PIC
        
        pop     ds
        pop     dx
        pop     ax
        iret
        
.complex_interrupt:
        ; Need full handler - save remaining registers
        push    bx
        push    cx
        push    si
        push    di
        push    bp
        push    ds
        push    es
        
        ; Set up data segment
        mov     ax, seg _DATA
        mov     ds, ax
        
        ; Clear direction flag for any string operations
        cld

        ; CRITICAL FIX: Switch to private stack for TSR safety
        ; Save caller's stack
        mov     [saved_ss], ss
        mov     [saved_sp], sp
        
        ; Switch to our private stack
        mov     ax, seg _DATA
        mov     ss, ax                      ; Stack segment = data segment
        mov     sp, OFFSET isr_stack_top    ; Top of our private stack

        ; Check for reentrancy
        test    byte ptr [isr_flags], ISR_REENTRY_FLAG
        jnz     .already_in_isr

        ; Mark ISR active
        or      byte ptr [isr_flags], ISR_REENTRY_FLAG

        ; PATCH POINT: NIC-specific dispatch
PATCH_nic_dispatch:
        call    generic_nic_handler     ; 3 bytes: default
        nop                             ; 2 bytes padding
        nop
        ; Will be patched to:
        ; 3C509: call nic_3c509_handler
        ; 3C515: call nic_3c515_handler

        ; Clear ISR active flag
        and     byte ptr [isr_flags], not ISR_REENTRY_FLAG

.send_eoi:
        ; Send EOI to PIC (constraint requirement)
        ; CRITICAL: Must send to slave BEFORE master for IRQ >= 8
        cmp     byte ptr [effective_nic_irq], 8
        jb      .master_only
        
        ; Slave PIC - send EOI to slave first
        mov     al, 20h
        out     0A0h, al                ; EOI to slave PIC FIRST
        
.master_only:
        ; Master PIC - always send
        mov     al, 20h
        out     20h, al                 ; EOI to master PIC SECOND

.done:
        ; CRITICAL FIX: Restore caller's stack
        mov     ss, [saved_ss]
        mov     sp, [saved_sp]
        
        ; Restore all registers
        pop     es
        pop     ds
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        iret

.already_in_isr:
        ; Already processing, just acknowledge and exit
        inc     word ptr [reentry_count]
        jmp     .send_eoi

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 3C509B Specific Handler with Interrupt Mitigation
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
nic_3c509_handler:
        ; Save ALL registers we'll use
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    ds
        push    es                      ; CRITICAL: Save ES for buffer operations
        
        ; CRITICAL: Set up segments for data access
        ; Both packet_buffer and variables are in _DATA segment
        mov     ax, seg _DATA
        mov     ds, ax                  ; DS = data segment for variables
        mov     es, ax                  ; ES = data segment for packet_buffer
        
        ; CRITICAL: Clear direction flag for string operations
        cld                             ; Ensure forward direction for rep insb
        
        ; Use internal ASM mitigation path unconditionally (no C in ISR)
.use_legacy_handler:
        ; INTERRUPT MITIGATION: Process multiple interrupts in one pass
        xor     bx, bx                  ; Clear interrupt batch counter
        
        ; Patchable batch budget for PIO NICs (3C509B)
PATCH_pio_batch_init:
        mov     cx, 8                   ; Default; SMC-patched per CPU (B9 imm16 90 90)
        nop
        nop
        mov     word ptr [packet_budget], cx

.mitigation_loop:
        ; Read and clear all interrupt sources at once
        mov     dx, [nic_io_base]       ; Now using DS which is set to _DATA
        add     dx, 0x0E                ; COMMAND/STATUS register
        in      ax, dx
        mov     [int_status], ax        ; Store in data segment
        
        ; Check for any pending interrupts
        test    ax, ax                  ; Check ALL bits (not just lower 8)
        jz      .mitigation_done
        
        ; Clear all pending interrupts atomically
        out     dx, ax                  ; Acknowledge all at once
        inc     bx                      ; Count mitigated interrupts
        
        ; Process RX interrupts
        test    ax, 0x0010              ; RX_COMPLETE
        jz      .check_tx_int

.rx_loop:
        ; Check RX status register for pending packet
        mov     dx, [nic_io_base]       ; Using DS = _DATA
        add     dx, 0x08                ; RX_STATUS register
        in      ax, dx
        
        ; Check if packet is complete and ready
        test    ax, 0x8000              ; RX_COMPLETE bit
        jz      .rx_fifo_empty          ; No more packets - exit RX loop early
        
        ; Also check packet budget before processing
        cmp     word ptr [packet_budget], 0
        jle     .check_tx_int           ; Budget exhausted
        
        ; Check for RX errors
        test    ax, 0x4000              ; RX_ERROR bit
        jz      .rx_good
        
        ; RX error - discard packet and continue
        mov     dx, [nic_io_base]
        add     dx, 0x0E                ; COMMAND register
        mov     ax, 0x4000              ; RX_DISCARD
        out     dx, ax
        inc     word ptr [rx_errors]
        jmp     .rx_next
        
.rx_good:
        ; Get packet length from status
        and     ax, 0x07FF              ; Mask length bits (0-2047)
        jz      .rx_discard             ; Zero length - discard
        cmp     ax, 1514                ; Max Ethernet frame
        ja      .rx_discard             ; Too large - discard
        
        mov     [packet_length], ax     ; Save valid length
        
        ; Read packet data from FIFO (optimized for 16-bit FIFO)
        push    cx                      ; Save packet budget counter
        mov     cx, ax                  ; Total byte count
        mov     di, offset packet_buffer ; ES:DI -> destination buffer (ES=_DATA)
        mov     dx, [nic_io_base]    
        add     dx, 0x00                ; RX_DATA register (FIFO)
        
        ; Optimize for 16-bit transfers
        push    cx                      ; Save total count
        shr     cx, 1                   ; Word count = bytes / 2
        jz      .skip_words             ; Skip if less than 2 bytes
        
        ; PATCH POINT: Optimized packet read (word transfers)
        ; Uses INSW_SAFE macro for 8086 compatibility
PATCH_3c509_read:
        INSW_SAFE                       ; CPU-adaptive: REP INSW (186+) or loop (8086)
        nop                             ; Padding for patches

.skip_words:
        pop     cx                      ; Restore total count
        test    cx, 1                   ; Check for odd byte
        jz      .read_done
        INSB_SAFE                       ; 8086-safe single byte input
        
.read_done:
        
        ; Defer packet processing to bottom-half
        ; Call: int packet_isr_receive(uint8_t *packet_data, uint16_t packet_size, uint8_t nic_index)
        ; Push args right-to-left (small model near call)
        ; NIC index argument
        mov     al, [current_nic_index]
        xor     ah, ah
        push    ax
        push    word ptr [packet_length]; Packet size
        mov     ax, offset packet_buffer
        push    ax                      ; Pointer to packet data
        call    packet_isr_receive
        add     sp, 6                   ; Clean up 3 args
        pop     cx                      ; Restore packet budget counter
        
        ; Successfully received
        inc     word ptr [packets_received]
        
.rx_discard:
        ; Discard packet from FIFO (required to advance)
        mov     dx, [nic_io_base]
        add     dx, 0x0E                ; COMMAND register
        mov     ax, 0x4000              ; RX_DISCARD command
        out     dx, ax
        
.rx_next:
        ; Check for more RX packets (up to batch limit)
        dec     word ptr [packet_budget]
        jnz     .rx_loop

.rx_fifo_empty:
        ; RX FIFO is empty - skip to TX processing
        ; This early exit improves performance when FIFO drains quickly
        
.check_tx_int:
        ; Process TX completion
        test    word ptr [int_status], 0x0004  ; TX_COMPLETE
        jz      .check_errors                   ; Skip if TX bit not set
        
        ; TX bit is set - acknowledge TX completion
        inc     word ptr [packets_sent]
        
.check_errors:
        ; Handle any error interrupts
        test    word ptr [int_status], 0x0040  ; ADAPTER_FAILURE
        jz      .continue_mitigation            ; Skip if error bit not set
        
        ; Error bit is set - log error and attempt recovery
        inc     word ptr [adapter_errors]
        call    reset_adapter_minimal
        
.continue_mitigation:
        ; Check if we should process more interrupt batches
        cmp     bx, 3                   ; Max 3 batches per ISR
        jae     .mitigation_done
        
        ; Check if we've processed maximum packets
        cmp     word ptr [packet_budget], 0
        jle     .mitigation_done
        
        ; Loop back to check for more pending interrupts
        jmp     .mitigation_loop

.mitigation_done:
        ; Update mitigation statistics
        cmp     bx, 1
        jbe     .no_mitigation
        inc     word ptr [interrupts_mitigated]
        add     word ptr [mitigation_savings], bx
        
.no_mitigation:
        ; Send EOI to PIC before returning
        cmp     byte ptr [effective_nic_irq], 8
        jb      .handler_master_only
        mov     al, 20h
        out     0A0h, al                ; EOI to slave PIC first
.handler_master_only:
        mov     al, 20h
        out     20h, al                 ; EOI to master PIC
        
        ; Restore all registers
        pop     es                      ; Restore ES
        pop     ds
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 3C515 Specific Handler (Bus Master)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
nic_3c515_handler:
        push    cx
        push    si
        push    ds
        push    es
        
        ; Set up segments
        mov     ax, seg _DATA
        mov     ds, ax
        mov     es, ax
        
        ; Use internal ASM mitigation path unconditionally (no C in ISR)
.use_legacy_3c515:
        ; Check interrupt status
        mov     dx, [nic_io_base]
        add     dx, 0x0E                ; INT_STATUS register
        in      ax, dx
        mov     [int_status], ax

        ; Check for RX complete
        test    ax, 0x0010              ; RX_COMPLETE
        jz      .check_tx

        ; Process RX ring buffer
        mov     si, [rx_ring_ptr]
        
        ; Patchable batch budget for DMA NICs (3C515)
PATCH_dma_batch_init:
        mov     cx, 32                  ; Default; SMC-patched per CPU (B9 imm16 90 90)
        nop
        nop

.rx_ring_loop:
        ; Check ring descriptor status
        mov     ax, [si]                ; Status word
        test    ax, 0x8000              ; OWN bit
        jz      .rx_ring_done           ; NIC owns it

        ; Get packet from ring
        push    si
        mov     bx, [si+2]              ; Buffer address
        mov     cx, [si+4]              ; Length

        ; PATCH POINT: DMA vs PIO transfer
        ; CRITICAL SAFETY GATE: Default to PIO until DMA verified
PATCH_3c515_transfer:
        call    transfer_pio            ; 3 bytes: SAFE default
        nop                             ; 2 bytes padding
        nop
        ; Will ONLY be patched to DMA after:
        ; 1. Bus mastering test passes
        ; 2. DMA boundary checks active  
        ; 3. Cache coherency verified

        pop     si

        ; Mark descriptor as processed
        and     word ptr [si], 0x7FFF   ; Clear OWN bit

        ; Advance ring pointer
        add     si, 8                   ; Next descriptor
        cmp     si, [rx_ring_end]
        jb      .next_packet
        mov     si, [rx_ring_start]     ; Wrap around

.next_packet:
        dec     cx
        jnz     .rx_ring_loop

.rx_ring_done:
        mov     [rx_ring_ptr], si

.check_tx:
        ; Check for TX complete
        test    word ptr [int_status], 0x0004  ; TX_COMPLETE
        jz      .done

        ; Update TX statistics
        inc     word ptr [packets_sent]

        ; Free TX buffer
        call    free_tx_buffer

.done:
        ; Acknowledge all interrupts
        mov     dx, [nic_io_base]
        add     dx, 0x0E
        mov     ax, [int_status]
        out     dx, ax

.3c515_done:
        pop     es
        pop     ds
        pop     si
        pop     cx
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Transfer Methods (Patchable)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        public  transfer_pio
transfer_pio:
        ; Programmed I/O transfer
        ; On 8086, SMC is disabled so REP_INSB_SAFE provides fallback
        push    dx
        push    di

        mov     di, offset packet_buffer
        mov     dx, [nic_io_base]

        ; PATCH POINT: Optimized PIO
        ; On 186+: May be patched to REP INSW or REP INSD
        ; On 8086: Uses loop-based byte transfer (SMC disabled)
PATCH_pio_loop:
        REP_INSB_SAFE                   ; CPU-adaptive: REP INSB (186+) or loop (8086)
        nop                             ; Padding for patches
        nop
        nop
        ; Patched for CPU-specific optimization on 186+

        pop     di
        pop     dx
        ret

        public  transfer_dma
transfer_dma:
        ; DMA transfer (3C515 only)
        push    ax
        push    cx
        push    dx

        ; PATCH POINT: DMA boundary check
PATCH_dma_boundary_check:
        nop                             ; 5-byte NOP sled
        nop                             ; Will be patched to:
        nop                             ; CALL check_dma_boundary (3 bytes)
        nop                             ; JC .use_pio_fallback (2 bytes)
        nop                             ; or 5 NOPs if PCI/safe

        ; Set up DMA descriptor
        mov     ax, [packet_buffer_phys]
        mov     [dma_desc], ax
        mov     ax, [packet_buffer_phys+2]
        mov     [dma_desc+2], ax

        ; PATCH POINT: Pre-DMA cache flush
PATCH_cache_flush_pre:
        nop                             ; 5-byte NOP sled
        nop                             ; Will be patched based on
        nop                             ; detected cache tier:
        nop                             ; CLFLUSH/WBINVD/Touch/NOP
        nop

        ; Start DMA
        mov     dx, [nic_io_base]
        add     dx, 0x24                ; DMA_CTRL
        mov     ax, 0x0001              ; START_DMA
        out     dx, ax

        ; Wait for completion (with timeout)
        mov     cx, 1000
.dma_wait:
        in      ax, dx
        test    ax, 0x0100              ; DMA_DONE
        jnz     .dma_complete
        loop    .dma_wait

.dma_complete:
        ; PATCH POINT: Post-DMA cache invalidate
PATCH_cache_flush_post:
        nop                             ; 5-byte NOP sled
        nop                             ; Will be patched based on
        nop                             ; detected cache tier:
        nop                             ; CLFLUSH/WBINVD/NOP
        nop

        pop     dx
        pop     cx
        pop     ax
        clc                             ; Success
        ret
        
.use_pio_fallback:
        ; PIO fallback when DMA unsafe
        pop     dx
        pop     cx
        pop     ax
        call    transfer_pio            ; Use PIO instead
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Safety Check Routines (Hot Section)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; DMA boundary check routine (patched in when needed)
check_dma_boundary:
        push    ax
        push    bx
        push    cx
        
        ; Get buffer physical address
        mov     ax, [packet_buffer_phys]
        mov     bx, [packet_buffer_phys+2]
        
        ; Check if buffer + length crosses 64KB boundary  
        mov     cx, [packet_length]
        or      cx, cx                  ; Check for zero length
        jz      .boundary_ok            ; Zero length = no crossing
        
        ; Calculate end address for both checks
        push    ax                      ; Save start low
        push    bx                      ; Save start high
        
        dec     cx                      ; Use length-1
        add     ax, cx                  ; Add to low word
        adc     bx, 0                   ; Propagate carry to high word
        
        ; Check 16MB ISA DMA limit on end address
        test    bh, 0FFh                ; Check bits 24-31 of end
        jnz     .restore_and_fail       ; End >16MB, can't use ISA DMA
        
        ; Check if crossed 64KB boundary
        pop     bx                      ; Restore start high
        pop     ax                      ; Restore start low
        push    bx                      ; Save again for potential bounce buffer
        push    ax
        
        ; Original 64KB check
        mov     cx, [packet_length]
        dec     cx
        add     ax, cx
        jc      .boundary_crossed       ; Carry = crossed 64KB
        
        ; No crossing, clean up stack and continue
        pop     ax                      ; Discard saved values
        pop     bx
        jmp     .boundary_ok
        
.boundary_crossed:
        pop     ax                      ; Restore original address
        pop     bx
        ; Boundary crossed - use bounce buffer
        inc     word ptr [dma_boundary_hits]
        call    allocate_bounce_buffer
        jc      .use_pio_fallback
        jmp     .boundary_ok
        
.restore_and_fail:
        pop     bx                      ; Clean up stack
        pop     ax
        jmp     .use_pio_fallback
        
.boundary_ok:
        pop     cx
        pop     bx
        pop     ax
        clc                             ; Success
        ret
        
.use_pio_fallback:
        ; Force PIO transfer
        pop     cx
        pop     bx
        pop     ax
        stc                             ; Error - use PIO
        ret

; Allocate bounce buffer for DMA
allocate_bounce_buffer:
        ; This would interface with dma_safety.c bounce buffer pool
        ; For now, return error to force PIO
        stc
        ret

; Cache flush for CLFLUSH systems (Pentium 4+)
cache_flush_clflush:
        push    ax
        push    bx
        push    cx
        push    di
        push    es
        
        ; Get segment:offset of buffer (not physical address)
        mov     ax, [packet_buffer_seg]
        mov     es, ax
        mov     di, [packet_buffer_off]
        
        ; Align DI down to 64-byte boundary
        mov     bx, di
        and     bx, 63                  ; Get misalignment amount
        sub     di, bx                  ; Align down to cache line
        
        ; Calculate number of cache lines to flush
        ; Lines = (length + misalignment + 63) / 64
        mov     cx, [packet_length]
        add     cx, bx                  ; Add misalignment
        add     cx, 63                  ; Round up
        shr     cx, 6                   ; Divide by 64
        jz      .done                   ; Nothing to flush
        
.flush_loop:
        ; CLFLUSH es:[di] - proper encoding for real mode
        db      26h                     ; ES: segment override
        db      0Fh, 0AEh, 3Dh          ; CLFLUSH [di]
        
        ; Advance to next cache line
        add     di, 64
        jnc     .no_wrap                ; Check for segment wrap
        
        ; Handle segment wrap - advance ES by 64KB
        mov     ax, es
        add     ax, 1000h               ; 64KB = 0x1000 paragraphs
        mov     es, ax
        
.no_wrap:
        loop    .flush_loop
        
.done:
        inc     word ptr [cache_flushes]
        
        pop     es
        pop     di
        pop     cx
        pop     bx
        pop     ax
        ret

; Cache flush using WBINVD (486+)
cache_flush_wbinvd:
        db      0Fh, 09h                ; WBINVD instruction
        inc     word ptr [cache_flushes]
        ret

; Software cache management (386)
cache_flush_software:
        push    si
        push    cx
        push    ax
        
        ; Touch every cache line to force write-through
        mov     si, [packet_buffer_phys]
        mov     cx, [packet_length]
        add     cx, 31                  ; Round to cache lines
        shr     cx, 5                   ; Divide by 32
        
.touch_loop:
        mov     al, [si]                ; Read to force cache line load
        add     si, 32                  ; Next cache line
        loop    .touch_loop
        
        inc     word ptr [cache_flushes]
        
        pop     ax
        pop     cx
        pop     si
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Minimal adapter reset for error recovery
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
reset_adapter_minimal:
        push    ax
        push    dx
        
        ; Issue global reset command
        mov     dx, [nic_io_base]   ; CS override for segment safety
        add     dx, 0x0E                ; COMMAND register
        mov     ax, 0x0000              ; GLOBAL_RESET
        out     dx, ax
        
        ; Brief delay for reset
        push    cx
        mov     cx, 100
.reset_delay:
        nop
        loop    .reset_delay
        pop     cx
        
        ; Re-enable receiver and transmitter
        mov     ax, 0x0101              ; RX_ENABLE | TX_ENABLE
        out     dx, ax
        
        pop     dx
        pop     ax
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Generic fallback handler
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
generic_nic_handler:
        ; Should never be called after patching
        inc     word ptr [unhandled_irqs]
        ret

hot_section_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; COLD SECTION - Initialization (discarded)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        align 16
cold_section_start:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Patch Table
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
patch_table:
        ; Patch 1: NIC dispatch
        dw      PATCH_nic_dispatch
        db      4                       ; Type: ISR
        db      5                       ; Size
        ; 8086: Generic handler
        db      0E8h, 00h, 00h, 90h, 90h  ; CALL generic
        ; 286: Same
        db      0E8h, 00h, 00h, 90h, 90h
        ; 386: Same (NIC-specific, not CPU)
        db      0E8h, 00h, 00h, 90h, 90h
        ; 486: Same
        db      0E8h, 00h, 00h, 90h, 90h
        ; Pentium: Same
        db      0E8h, 00h, 00h, 90h, 90h

        ; Patch 2: 3C509 packet read
        dw      PATCH_3c509_read
        db      2                       ; Type: IO
        db      5                       ; Size
        ; 8086: REP INSB
        db      0F3h, 6Ch, 90h, 90h, 90h
        ; 286: REP INSW
        db      0F3h, 6Dh, 90h, 90h, 90h
        ; 386: 32-bit string ops
        db      66h, 0F3h, 6Dh, 90h, 90h
        ; 486: Same as 386
        db      66h, 0F3h, 6Dh, 90h, 90h
        ; Pentium: Same
        db      66h, 0F3h, 6Dh, 90h, 90h

        ; Patch 3: 3C515 transfer method
        dw      PATCH_3c515_transfer
        db      4                       ; Type: ISR
        db      5                       ; Size
        ; All: Will be patched based on DMA capability
        db      0E8h, 00h, 00h, 90h, 90h  ; CALL transfer_pio
        db      0E8h, 00h, 00h, 90h, 90h
        db      0E8h, 00h, 00h, 90h, 90h
        db      0E8h, 00h, 00h, 90h, 90h
        db      0E8h, 00h, 00h, 90h, 90h

        ; Patch 4: PIO optimization
        dw      PATCH_pio_loop
        db      2                       ; Type: IO
        db      5                       ; Size
        ; CPU-specific I/O optimization
        db      0F3h, 6Ch, 90h, 90h, 90h  ; REP INSB
        db      0F3h, 6Dh, 90h, 90h, 90h  ; REP INSW
        db      66h, 0F3h, 6Dh, 90h, 90h  ; 32-bit
        db      66h, 0F3h, 6Dh, 90h, 90h
        db      66h, 0F3h, 6Dh, 90h, 90h

        ; Patch 5: DMA boundary check
        dw      PATCH_dma_boundary_check
        db      6                       ; Type: DMA_CHECK
        db      5                       ; Size
        ; All CPUs: either check or NOP based on bus type
        db      90h, 90h, 90h, 90h, 90h  ; NOPs (safe default)
        db      0E8h, 00h, 00h, 90h, 90h  ; CALL check_boundary (ISA)
        db      0E8h, 00h, 00h, 90h, 90h  ; CALL check_boundary (ISA)
        db      90h, 90h, 90h, 90h, 90h  ; NOPs (EISA/PCI)
        db      90h, 90h, 90h, 90h, 90h  ; NOPs (PCI)

        ; Patch: PIO batch size (mov cx, imm16; nop; nop)
        dw      PATCH_pio_batch_init
        db      4                       ; Type: ISR
        db      5                       ; Size
        ; 8086: PIO=4
        db      0B9h, 04h, 00h, 90h, 90h
        ; 286:  PIO=6
        db      0B9h, 06h, 00h, 90h, 90h
        ; 386:  PIO=8
        db      0B9h, 08h, 00h, 90h, 90h
        ; 486:  PIO=12
        db      0B9h, 0Ch, 00h, 90h, 90h
        ; Pentium: PIO=16
        db      0B9h, 10h, 00h, 90h, 90h

        ; Patch: DMA batch size (mov cx, imm16; nop; nop)
        dw      PATCH_dma_batch_init
        db      4                       ; Type: ISR
        db      5                       ; Size
        ; 8086: DMA=8 (conservative)
        db      0B9h, 08h, 00h, 90h, 90h
        ; 286:  DMA=16
        db      0B9h, 10h, 00h, 90h, 90h
        ; 386:  DMA=24
        db      0B9h, 18h, 00h, 90h, 90h
        ; 486:  DMA=32
        db      0B9h, 20h, 00h, 90h, 90h
        ; Pentium: DMA=32
        db      0B9h, 20h, 00h, 90h, 90h

        ; Patch 6: Pre-DMA cache flush
        dw      PATCH_cache_flush_pre
        db      7                       ; Type: CACHE_PRE
        db      5                       ; Size
        ; CPU-specific cache management
        db      90h, 90h, 90h, 90h, 90h  ; 8086: NOPs
        db      90h, 90h, 90h, 90h, 90h  ; 286: NOPs
        db      90h, 90h, 90h, 90h, 90h  ; 386: NOPs or touch
        db      0Fh, 09h, 90h, 90h, 90h  ; 486: WBINVD
        db      0E8h, 00h, 00h, 90h, 90h  ; Pentium: CALL flush_proc

        ; Patch 7: Post-DMA cache invalidate
        dw      PATCH_cache_flush_post
        db      8                       ; Type: CACHE_POST
        db      5                       ; Size
        ; CPU-specific cache management
        db      90h, 90h, 90h, 90h, 90h  ; 8086: NOPs
        db      90h, 90h, 90h, 90h, 90h  ; 286: NOPs
        db      90h, 90h, 90h, 90h, 90h  ; 386: NOPs or touch
        db      0Fh, 09h, 90h, 90h, 90h  ; 486: WBINVD
        db      0E8h, 00h, 00h, 90h, 90h  ; Pentium: CALL invalidate_proc

patch_count     equ     9

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; IRQ Handler Initialization
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
irq_handler_init:
        public  irq_handler_init
        push    ax
        push    bx
        push    cx
        push    dx
        push    ds
        push    es

        ; Set up data segment first for variable access
        push    ds
        mov     ax, seg _DATA
        mov     ds, ax
        
        ; Load NIC IRQ selected during detection
        mov     al, [detected_nic_irq]
        mov     [nic_irq], al
        
        pop     ds

        ; Compute effective IRQ (map 2->9)
        mov     dl, al
        cmp     al, 2
        jne     .have_eff
        mov     dl, 9
.have_eff:
        push    ds
        mov     ax, seg _DATA
        mov     ds, ax
        mov     [effective_nic_irq], dl
        
        ; Save original IMR state before masking
        push    dx
        mov     dx, 021h                ; Master PIC IMR
        in      al, dx
        mov     [original_imr_master], al
        mov     dx, 0A1h                ; Slave PIC IMR
        in      al, dx
        mov     [original_imr_slave], al
        pop     dx
        
        pop     ds

        ; CRITICAL: Mask the IRQ line immediately to minimize race window
        mov     al, dl                  ; AL = effective_nic_irq
        call    mask_irq_in_pic
        
        ; For slave IRQs, ensure cascade (IRQ2) is unmasked
        cmp     dl, 8                   ; DL still has effective_nic_irq
        jb      .skip_cascade
        push    ds
        mov     ax, seg _DATA
        mov     ds, ax
        push    ax
        pushf
        cli
        mov     dx, 021h                ; Master PIC IMR
        in      al, dx
        test    al, 04h                 ; Check if IRQ2 is masked
        jz      .cascade_already_ok     ; Already unmasked, skip
        mov     byte ptr [cascade_modified], 1  ; Mark that we modified it
        and     al, 0FBh                ; Clear bit 2 (unmask IRQ2 cascade)
        out     dx, al
.cascade_already_ok:
        popf
        pop     ax
        pop     ds
.skip_cascade:

        ; Calculate interrupt vector (IRQ2 already mapped to 9 in effective_nic_irq)
        mov     al, dl                  ; Restore AL = effective_nic_irq
        cmp     al, 8
        jb      .use_master
        ; Slave PIC vectors (IRQ8..15 -> INT 70h..77h)
        mov     bl, al
        add     bl, 0x70 - 8
        jmp     .have_vector
.use_master:
        ; Master PIC vectors (IRQ0..7 -> INT 08h..0Fh)
        mov     bl, al
        add     bl, 0x08

.have_vector:
        ; Save old vector (AH=35h, AL=vector -> ES:BX)
        mov     ah, 0x35
        mov     al, bl
        int     21h

        ; Store old vector in _DATA
        push    ds
        mov     ax, seg _DATA
        mov     ds, ax
        mov     [old_vector_off], bx
        mov     [old_vector_seg], es
        mov     [installed_vector], bl
        pop     ds

        ; Install new handler (AH=25h expects DS:DX -> handler address)
        mov     dx, offset nic_irq_handler
        push    cs
        pop     ds                      ; DS = CS so DS:DX points to code
        mov     ah, 0x25
        mov     al, bl                  ; vector number
        int     21h

        ; Leave IRQ masked; C-side phase will unmask explicitly after init

        pop     es
        pop     ds
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; Legacy enable_irq_in_pic removed - use unmask_irq_in_pic instead
; The new function takes IRQ in AL and properly handles atomicity

; Mask the NIC IRQ line in the PIC IMR (master/slave) – safe during vector change
; AL = effective IRQ number (0-15, already mapped 2->9)
mask_irq_in_pic:
        pushf
        cli                     ; Atomic IMR update
        push    ax
        push    bx
        push    cx
        push    dx

        cmp     al, 8
        jb      .mask_master

        ; Slave PIC: mask bit (IRQ8..15 -> bit 0..7)
        sub     al, 8
        mov     cl, al
        mov     bl, 1           ; Use BL for mask bit
        shl     bl, cl
        mov     dx, 0A1h
        in      al, dx          ; Read IMR into AL (8-bit I/O only!)
        or      al, bl          ; Set mask bit
        out     dx, al
        jmp     .done_mask

.mask_master:
        ; Master PIC
        mov     cl, al
        mov     bl, 1           ; Use BL for mask bit
        shl     bl, cl
        mov     dx, 021h
        in      al, dx          ; Read IMR into AL (8-bit I/O only!)
        or      al, bl          ; Set mask bit
        out     dx, al

.done_mask:
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        popf
        ret

; Unmask the NIC IRQ line in the PIC IMR (for enable_driver_interrupts)
; AL = effective IRQ number (0-15, already mapped 2->9)
unmask_irq_in_pic:
        public  unmask_irq_in_pic
        pushf
        cli                     ; Atomic IMR update
        push    ax
        push    bx
        push    cx
        push    dx

        cmp     al, 8
        jb      .unmask_master

        ; Slave PIC: unmask bit (IRQ8..15 -> bit 0..7)
        sub     al, 8
        mov     cl, al
        mov     bl, 1           ; Use BL for mask bit
        shl     bl, cl
        not     bl              ; Invert to create unmask pattern
        mov     dx, 0A1h
        in      al, dx          ; Read IMR into AL
        and     al, bl          ; Clear mask bit
        out     dx, al
        jmp     .done_unmask

.unmask_master:
        ; Master PIC
        mov     cl, al
        mov     bl, 1           ; Use BL for mask bit
        shl     bl, cl
        not     bl              ; Invert to create unmask pattern
        mov     dx, 021h
        in      al, dx          ; Read IMR into AL
        and     al, bl          ; Clear mask bit
        out     dx, al

.done_unmask:
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        popf
        ret

; Uninstall IRQ handler and restore previous vector
irq_handler_uninstall:
        public  irq_handler_uninstall
        push    ax
        push    bx
        push    cx                      ; Need CX for shift operations
        push    dx
        push    ds

        ; Set up data segment
        mov     ax, seg _DATA
        mov     ds, ax

        ; Keep IRQ masked while restoring
        mov     al, [effective_nic_irq]
        call    mask_irq_in_pic

        ; Load saved vector number
        mov     al, [installed_vector]
        or      al, al
        jz      .restore_imr_bit        ; Nothing to restore
        
        mov     bl, al                  ; Save vector number in BL

        ; Load saved old handler DS:DX from _DATA
        mov     dx, [old_vector_off]
        mov     ax, [old_vector_seg]
        or      ax, ax
        jz      .restore_imr_bit        ; No old vector saved

        ; AH=25h Set Vector, DS:DX must point to old handler
        mov     ds, ax                  ; DS = old handler segment (clobbers AL!)
        mov     ah, 0x25
        mov     al, bl                  ; Restore vector number from BL
        int     21h
        
        ; Clear tracking variables for idempotency
        mov     ax, seg _DATA
        mov     ds, ax
        mov     byte ptr [installed_vector], 0
        mov     word ptr [old_vector_off], 0
        mov     word ptr [old_vector_seg], 0

.restore_imr_bit:
        ; Restore ONLY our IRQ bit in IMR (preserve other drivers' changes)
        ; NOTE: DS is saved/restored by function prologue/epilogue
        mov     ax, seg _DATA
        mov     ds, ax
        mov     al, [effective_nic_irq]
        
        pushf
        cli                             ; Atomic IMR update
        
        cmp     al, 8
        jb      .restore_master_bit
        
        ; Slave PIC - restore only our bit
        sub     al, 8
        mov     cl, al
        mov     bl, 1
        shl     bl, cl                  ; BL = our bit mask
        not     bl                      ; BL = inverse mask (all bits except ours)
        
        mov     dx, 0A1h                ; Slave IMR
        in      al, dx                  ; Current IMR
        and     al, bl                  ; Clear our bit
        mov     ah, [original_imr_slave]
        not     bl                      ; BL = our bit mask again
        and     ah, bl                  ; Isolate original bit
        or      al, ah                  ; Restore original bit value
        out     dx, al
        jmp     .restore_done
        
.restore_master_bit:
        ; Master PIC - restore only our bit
        mov     cl, al
        mov     bl, 1
        shl     bl, cl                  ; BL = our bit mask
        not     bl                      ; BL = inverse mask
        
        mov     dx, 021h                ; Master IMR
        in      al, dx                  ; Current IMR
        and     al, bl                  ; Clear our bit
        mov     ah, [original_imr_master]
        not     bl                      ; BL = our bit mask again
        and     ah, bl                  ; Isolate original bit
        or      al, ah                  ; Restore original bit value
        out     dx, al

.restore_done:
        ; Restore IRQ2 cascade if we modified it
        cmp     byte ptr [cascade_modified], 0
        jz      .skip_cascade_restore
        push    ax
        pushf
        cli
        mov     dx, 021h                ; Master PIC IMR
        in      al, dx
        test    byte ptr [original_imr_master], 04h  ; Was IRQ2 originally masked?
        jz      .cascade_restore_unmask
        or      al, 04h                 ; Re-mask IRQ2
        jmp     .cascade_restore_write
.cascade_restore_unmask:
        and     al, 0FBh                ; Keep IRQ2 unmasked
.cascade_restore_write:
        out     dx, al
        popf
        pop     ax
.skip_cascade_restore:

        pop     ds
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

cold_section_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Binding helper - set IO base, IRQ, and NIC index from C
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        .code
nic_irq_set_binding:
        public  nic_irq_set_binding
        ; Args (small model near): [SP+2]=io_base (word), [SP+4]=irq (byte), [SP+6]=nic_index (byte)
        push    bp
        mov     bp, sp
        push    ax
        push    ds
        
        mov     ax, seg _DATA
        mov     ds, ax
        
        mov     ax, [bp+4]             ; io_base
        mov     [nic_io_base], ax
        mov     al, [bp+6]             ; irq
        mov     [detected_nic_irq], al
        mov     al, [bp+8]             ; nic_index
        mov     [current_nic_index], al
        
        pop     ds
        pop     ax
        pop     bp
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Data Section
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        .data
_DATA   segment

        ; Safety flags (set during initialization)
dma_safety_flags    dw      0
        ; Bit 0: Bus master enabled
        ; Bit 1: Cache coherency required
        ; Bit 2: Bounce buffers available
        ; Bit 3: CLFLUSH available
        ; Bit 4: WBINVD available
        ; Bit 5: Full snooping detected
        ; Bit 6: ISA DMA (needs 64KB check)
        ; Bit 7: PCI bus (no 64KB limit)

cache_tier_selected db      0       ; 0-4 based on runtime tests
bus_type            db      0       ; 0=ISA, 1=EISA, 2=PCI

        ; Runtime state
isr_flags           db      0
nic_irq             db      0
nic_io_base         dw      0
detected_nic_irq    db      0
current_nic_index   db      0
        public  current_nic_index
int_status          dw      0

        ; CRITICAL FIX: Private ISR stack (2KB)
        ; Prevents stack corruption from other TSRs
        align 16
isr_private_stack   db      2048 DUP(0)     ; 2KB private stack
isr_stack_top       equ     $               ; Top of stack (grows down)
saved_ss            dw      0               ; Saved caller's SS
saved_sp            dw      0               ; Saved caller's SP

        ; Statistics
packets_received    dw      0
packets_sent        dw      0
reentry_count       dw      0
unhandled_irqs      dw      0
dma_boundary_hits   dw      0
cache_flushes       dw      0
rx_ack_count        dw      0       ; Tiny ISR: RX fast acknowledgments
tx_ack_count        dw      0       ; Tiny ISR: TX fast acknowledgments
adapter_errors      dw      0       ; Adapter failure count
interrupts_mitigated dw     0       ; Number of interrupt batches
mitigation_savings  dw      0       ; Total interrupts saved
rx_errors           dw      0       ; RX error count

        ; Old interrupt vector storage for restore on unload
old_vector_off      dw      0
old_vector_seg      dw      0
installed_vector    db      0

        ; Effective IRQ (remaps 2->9 for AT PIC)
effective_nic_irq   db      0

        ; Original IMR state for clean restore
original_imr_master db      0       ; Original master PIC IMR
original_imr_slave  db      0       ; Original slave PIC IMR
cascade_modified    db      0       ; 1 if we unmasked IRQ2 cascade

        ; Packet handling
packet_length       dw      0
packet_buffer_phys  dd      0
packet_buffer_seg   dw      0       ; Segment for CLFLUSH
packet_buffer_off   dw      0       ; Offset for CLFLUSH
packet_budget       dw      0       ; Separate counter for mitigation loop

        ; Packet buffer - defined locally in data segment for proper ES:DS access
        align 2
packet_buffer       db      1514 dup(?)     ; Max Ethernet frame size
        public  packet_buffer           ; Export for C code if needed

        ; 3C515 ring buffer pointers
rx_ring_start       dw      0
rx_ring_end         dw      0
rx_ring_ptr         dw      0

        ; DMA descriptor
dma_desc            dd      0
dma_desc_addr       dw      0
dma_desc_len        dw      0

        ; Module size
module_size         equ     cold_section_end - module_header

_DATA   ends

        end
