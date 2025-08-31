;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file nic_irq_smc.asm
;; @brief NIC Interrupt Handler with Self-Modifying Code patch points
;;
;; Implements optimized interrupt handlers for 3C509B and 3C515 NICs with
;; CPU-specific patches applied during initialization.
;;
;; Constraints:
;; - ISR execution <100μs
;; - No reentrancy (protected)
;; - Proper EOI to PIC
;; - All segment registers preserved
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        .8086                           ; Base compatibility
        .model small
        .code

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Module Header (64 bytes)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        align 16
module_header:
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

        ; External references
        extern  packet_buffer:byte
        extern  packet_process:near
        extern  statistics:byte

        ; Constants
        ISR_REENTRY_FLAG    equ 0x01
        MAX_PACKETS_BATCH   equ 10      ; Process max 10 packets per IRQ

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Main NIC IRQ Handler (Patched for specific NIC)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
nic_irq_handler:
        ; Save all registers (constraint requirement)
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    bp
        push    ds
        push    es

        ; Set up data segment
        mov     ax, seg _DATA
        mov     ds, ax

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
        cmp     byte ptr [nic_irq], 8
        jb      .master_only
        
        ; Slave PIC - send EOI to slave first
        mov     al, 20h
        out     0A0h, al                ; EOI to slave PIC FIRST
        
.master_only:
        ; Master PIC - always send
        mov     al, 20h
        out     20h, al                 ; EOI to master PIC SECOND

.done:
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
;; 3C509B Specific Handler
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
nic_3c509_handler:
        push    cx
        mov     cx, MAX_PACKETS_BATCH

.rx_loop:
        ; Check RX status
        mov     dx, [nic_io_base]
        add     dx, 0x08                ; RX_STATUS register
        in      ax, dx

        test    ax, 0x8000              ; RX_COMPLETE bit
        jz      .rx_done

        ; Get packet length
        and     ax, 0x07FF              ; Mask length bits
        mov     [packet_length], ax

        ; Read packet data
        mov     cx, ax
        mov     di, offset packet_buffer
        mov     dx, [nic_io_base]
        add     dx, 0x00                ; RX_DATA register

        ; PATCH POINT: Optimized packet read
PATCH_3c509_read:
        rep insb                        ; 2 bytes: 8086 default
        nop                             ; 3 bytes padding
        nop
        nop
        ; Will be patched to:
        ; 286: REP INSW (2 bytes)
        ; 386+: REP INSD with prefix (3 bytes)

        ; Process packet
        call    packet_process

        ; Acknowledge RX
        mov     dx, [nic_io_base]
        add     dx, 0x0E                ; COMMAND register
        mov     ax, 0x4000              ; RX_DISCARD
        out     dx, ax

        ; Check for more packets
        dec     cx
        jnz     .rx_loop

.rx_done:
        ; Update statistics
        inc     word ptr [packets_received]

        pop     cx
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 3C515 Specific Handler (Bus Master)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
nic_3c515_handler:
        push    cx
        push    si

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
        mov     cx, MAX_PACKETS_BATCH

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
PATCH_3c515_transfer:
        call    transfer_pio            ; 3 bytes: default PIO
        nop                             ; 2 bytes padding
        nop
        ; Will be patched to:
        ; If DMA capable: call transfer_dma

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

        pop     si
        pop     cx
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Transfer Methods (Patchable)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
transfer_pio:
        ; Programmed I/O transfer
        push    dx
        push    di

        mov     di, offset packet_buffer
        mov     dx, [nic_io_base]

        ; PATCH POINT: Optimized PIO
PATCH_pio_loop:
        rep insb                        ; 2 bytes: default
        nop                             ; 3 bytes padding
        nop
        nop
        ; Patched for CPU-specific optimization

        pop     di
        pop     dx
        ret

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

patch_count     equ     7

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; IRQ Handler Initialization
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
irq_handler_init:
        push    ax
        push    dx
        push    es

        ; Get NIC IRQ from detection
        mov     al, [detected_nic_irq]
        mov     [nic_irq], al

        ; Calculate interrupt vector (IRQ + 8 for master, IRQ + 0x70 for slave)
        cmp     al, 8
        jb      .master_pic
        add     al, 0x70 - 8            ; Slave PIC vectors
        jmp     .set_vector
.master_pic:
        add     al, 0x08                ; Master PIC vectors

.set_vector:
        ; Install interrupt handler
        mov     ah, 0x25                ; Set interrupt vector
        mov     dx, offset nic_irq_handler
        int     21h

        ; Enable IRQ in PIC
        call    enable_irq_in_pic

        pop     es
        pop     dx
        pop     ax
        ret

enable_irq_in_pic:
        push    ax
        push    dx

        mov     al, [nic_irq]
        cmp     al, 8
        jb      .enable_master

        ; Enable in slave PIC
        sub     al, 8
        mov     cl, al
        mov     al, 1
        shl     al, cl
        not     al
        mov     dx, 0xA1                ; Slave PIC mask
        in      al, dx
        and     al, ah
        out     dx, al
        jmp     .done

.enable_master:
        ; Enable in master PIC
        mov     cl, al
        mov     al, 1
        shl     al, cl
        not     al
        mov     dx, 0x21                ; Master PIC mask
        in      ah, dx
        and     ah, al
        mov     al, ah
        out     dx, al

.done:
        pop     dx
        pop     ax
        ret

cold_section_end:

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
int_status          dw      0

        ; Statistics
packets_received    dw      0
packets_sent        dw      0
reentry_count       dw      0
unhandled_irqs      dw      0
dma_boundary_hits   dw      0
cache_flushes       dw      0

        ; Packet handling
packet_length       dw      0
packet_buffer_phys  dd      0
packet_buffer_seg   dw      0       ; Segment for CLFLUSH
packet_buffer_off   dw      0       ; Offset for CLFLUSH

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