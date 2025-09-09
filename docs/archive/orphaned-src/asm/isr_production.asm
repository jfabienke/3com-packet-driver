;-----------------------------------------------------------------------------
; @file isr_production.asm
; @brief Production ISR - Ultra-minimal with PIC safety and SMC optimization
;
; Merges isr_tiny.asm (SMC optimization) with isr_pic_safe.asm (spurious IRQ)
; Target: <50μs latency on 3C515-TX, <100μs on 3C509B
;
; GPT-5 Validated: PIC-only, no APIC, spurious IRQ handling
; ISA Reality: Optimized for 5.55 MB/s sustained throughput
;-----------------------------------------------------------------------------

SECTION .text

;=============================================================================
; CONSTANTS
;=============================================================================

; PIC ports
PIC1_CMD                equ 20h
PIC1_DATA               equ 21h
PIC2_CMD                equ 0A0h
PIC2_DATA               equ 0A1h
OCW2_EOI                equ 20h
OCW3_READ_ISR           equ 0Bh

; 3Com status bits
STATUS_INT_LATCH        equ 0001h
STATUS_TX_COMPLETE      equ 0008h
STATUS_RX_COMPLETE      equ 0010h

; Work queue constants
WORK_QUEUE_SIZE         equ 32      ; 32 entries
WORK_ENTRY_SIZE         equ 4       ; 4 bytes per entry

;=============================================================================
; DATA SEGMENT
;=============================================================================

SECTION .data

; SMC patch targets (filled during init)
smc_io_base:            dw 0        ; Patched with actual I/O base
smc_status_offset:      dw 0Eh      ; Status register offset
smc_eoi_port:           dw 0        ; PIC port for EOI
smc_irq_mask:           db 0        ; IRQ mask for spurious check

; Work queue (lock-free SPSC)
work_queue:             times WORK_QUEUE_SIZE dd 0
queue_head:             dw 0        ; Producer index (ISR)
queue_tail:             dw 0        ; Consumer index (bottom half)

; Statistics
isr_count:              dd 0
spurious_count:         dw 0
queue_overflow:         dw 0

; Original vector for chaining
orig_vector_off:        dw 0
orig_vector_seg:        dw 0

SECTION .text

;=============================================================================
; ISR ENTRY - ULTRA-MINIMAL HOT PATH
;=============================================================================

GLOBAL isr_entry
isr_entry:
        ; === CRITICAL PATH START ===
        ; Target: 8-12 instructions max
        
        ; Save minimal context (6 bytes pushed)
        push    ax
        push    dx
        push    ds
        
        ; Load data segment (2 instructions)
        mov     ax, seg smc_io_base
        mov     ds, ax
        
        ; Read device status (3 instructions, SMC-patched)
        mov     dx, 0DEADh              ; Patched with I/O base
io_base_patch equ $-2
        add     dx, 0BEEFh              ; Patched with status offset  
status_patch equ $-2
        in      ax, dx                  ; Read status
        
        ; Check if ours (2 instructions)
        test    ax, STATUS_INT_LATCH
        jz      .not_ours               ; Jump to slow path
        
        ; === CRITICAL: Acknowledge device BEFORE PIC EOI ===
        ; This prevents missing edges on edge-triggered ISA PICs
        out     dx, ax                  ; Write clears device interrupt
        
        ; Queue work - ultra fast with overflow check
        push    bx
        mov     bx, [queue_head]
        
        ; Check for queue overflow
        push    ax
        mov     ax, bx
        add     ax, WORK_ENTRY_SIZE
        and     ax, (WORK_QUEUE_SIZE * WORK_ENTRY_SIZE - 1)
        cmp     ax, [queue_tail]
        pop     ax
        je      .queue_full
        
        mov     [work_queue + bx], ax   ; Store status
        mov     [work_queue + bx + 2], dx ; Store I/O base
        
        ; Update queue head
        add     bx, WORK_ENTRY_SIZE
        and     bx, (WORK_QUEUE_SIZE * WORK_ENTRY_SIZE - 1)
        mov     [queue_head], bx
        
.queue_done:
        pop     bx
        
        ; === Send EOI AFTER device ack (GPT-5 critical) ===
        mov     al, OCW2_EOI
        mov     dx, 0FEEDh              ; Patched with PIC port
eoi_port_patch equ $-2
        out     dx, al
        
        ; Check if cascade EOI needed (IRQ 8-15)
        cmp     byte [irq_patch], 8
        jb      .eoi_done
        ; For IRQ 8-15: EOI slave first, then master
        ; AL still contains OCW2_EOI from above
        mov     dx, PIC1_CMD
        out     dx, al                  ; Send EOI to master too
.eoi_done:
        
        ; Update counter and exit (4 instructions)
        inc     word [isr_count]
        adc     word [isr_count+2], 0
        
        ; Restore and return (4 instructions)
        pop     ds
        pop     dx
        pop     ax
        iret
        
        ; === CRITICAL PATH END ===
        ; Total: ~30 instructions in hot path
        
.queue_full:
        ; Queue overflow - increment counter
        inc     word [queue_overflow]
        ; TODO: Mask NIC RX interrupt to prevent storm
        ; For now, just drop the event
        jmp     .queue_done

;-----------------------------------------------------------------------------
; SLOW PATH - Spurious IRQ and chaining
;-----------------------------------------------------------------------------
.not_ours:
        ; === Optimized spurious check - only for IRQ 7/15 ===
        mov     al, 0FFh                ; Patched with IRQ number
irq_patch equ $-1
        cmp     al, 7
        je      .check_spurious_7
        cmp     al, 15
        je      .check_spurious_15
        
        ; Not IRQ 7/15 - no spurious check needed, just chain
        pop     ds
        pop     dx
        pop     ax
        jmp     far [cs:orig_vector_off]

.check_spurious_7:
        ; Read master PIC ISR register
        push    bx
        mov     al, OCW3_READ_ISR
        out     PIC1_CMD, al
        jmp     short $+2               ; I/O delay
        in      al, PIC1_CMD
        mov     bl, 80h                 ; IRQ 7 mask
        test    al, bl                  ; IRQ 7 in service?
        pop     bx
        jnz     .chain                  ; Yes, real interrupt
        
        ; Spurious IRQ 7
        inc     word [spurious_count]
        jmp     .exit_no_eoi

.check_spurious_15:
        ; Read slave PIC ISR register
        push    bx
        mov     al, OCW3_READ_ISR
        out     PIC2_CMD, al
        jmp     short $+2               ; I/O delay
        in      al, PIC2_CMD
        mov     bl, 80h                 ; IRQ 15 mask (bit 7 of slave)
        test    al, bl                  ; IRQ 15 in service?
        pop     bx
        jnz     .chain                  ; Yes, real interrupt
        
        ; === CRITICAL: Spurious IRQ 15 - EOI to master ONLY ===
        ; Do NOT send EOI to slave for spurious IRQ 15!
        inc     word [spurious_count]
        mov     al, OCW2_EOI
        out     PIC1_CMD, al            ; Master only
        jmp     .exit_no_eoi

.chain:
        pop     ds
        pop     dx
        pop     ax
        jmp     far [cs:orig_vector_off]

.exit_no_eoi:
        pop     ds
        pop     dx
        pop     ax
        iret

;=============================================================================
; INSTALLATION AND SMC PATCHING
;=============================================================================

;-----------------------------------------------------------------------------
; install_isr - Install and patch ISR for specific device
;
; Input: AX = I/O base address
;        BL = IRQ number (0-15)
;-----------------------------------------------------------------------------
GLOBAL install_isr
install_isr:
        push    cx
        push    dx
        push    si
        push    di
        push    es
        
        ; === Apply SMC patches ===
        
        ; Patch I/O base
        mov     [smc_io_base], ax
        mov     [cs:io_base_patch], ax
        
        ; Patch status offset (0Eh for 3Com)
        mov     word [cs:status_patch], 0Eh
        
        ; Patch IRQ number for spurious check
        mov     [cs:irq_patch], bl
        
        ; Determine PIC EOI port
        cmp     bl, 8
        jb      .master_pic
        
        ; Slave PIC - need cascade EOI
        mov     word [cs:eoi_port_patch], PIC2_CMD
        jmp     .save_vector
        
.master_pic:
        mov     word [cs:eoi_port_patch], PIC1_CMD
        
.save_vector:
        ; Calculate interrupt vector
        mov     al, bl
        add     al, 8                   ; Hardware IRQs start at INT 8
        cmp     bl, 8
        jb      .vector_ready
        add     al, 68h                 ; High IRQs at INT 70h
        
.vector_ready:
        ; Save original vector
        push    ax
        mov     ah, 35h                 ; Get vector
        int     21h
        mov     [orig_vector_seg], es
        mov     [orig_vector_off], bx
        
        ; Install our handler
        pop     ax
        push    ds
        push    cs
        pop     ds
        mov     dx, isr_entry
        mov     ah, 25h                 ; Set vector
        int     21h
        pop     ds
        
        ; Unmask IRQ in PIC
        call    unmask_irq
        
        ; === Critical: Flush prefetch after SMC ===
        ; Multiple methods for different CPU generations
        jmp     short .flush_1          ; Method 1: Near jump
.flush_1:
        jmp     short $+2               ; Method 2: Jump over nothing
        ; Method 3: For 486+, a serializing instruction would go here
        ; but we keep it simple for compatibility
        
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        ret

;-----------------------------------------------------------------------------
; unmask_irq - Enable IRQ in PIC
;
; Input: BL = IRQ number
;-----------------------------------------------------------------------------
unmask_irq:
        push    ax
        push    cx
        
        mov     cl, bl
        cmp     cl, 8
        jb      .unmask_master
        
        ; Unmask slave PIC
        sub     cl, 8
        in      al, PIC2_DATA
        mov     ah, 1
        shl     ah, cl
        not     ah
        and     al, ah
        out     PIC2_DATA, al
        
        ; Ensure cascade (IRQ2) is unmasked
        in      al, PIC1_DATA
        and     al, 0FBh
        out     PIC1_DATA, al
        jmp     .done
        
.unmask_master:
        in      al, PIC1_DATA
        mov     ah, 1
        shl     ah, cl
        not     ah
        and     al, ah
        out     PIC1_DATA, al
        
.done:
        pop     cx
        pop     ax
        ret

;=============================================================================
; WORK QUEUE BOTTOM HALF (Called from main loop)
;=============================================================================

;-----------------------------------------------------------------------------
; process_work_queue - Process pending work from ISR
;
; Returns: AX = number of items processed
;-----------------------------------------------------------------------------
GLOBAL process_work_queue
process_work_queue:
        push    bx
        push    cx
        push    dx
        push    si
        
        xor     cx, cx                  ; Items processed
        mov     si, [queue_tail]
        
.process_loop:
        ; Check if queue empty
        cmp     si, [queue_head]
        je      .done
        
        ; Get work item
        mov     ax, [work_queue + si]   ; Status
        mov     dx, [work_queue + si + 2] ; I/O base
        
        ; Process based on status bits
        test    ax, STATUS_TX_COMPLETE
        jz      .check_rx
        call    handle_tx_complete
        
.check_rx:
        test    ax, STATUS_RX_COMPLETE
        jz      .next_item
        call    handle_rx_complete
        
.next_item:
        ; Update tail
        add     si, WORK_ENTRY_SIZE
        and     si, (WORK_QUEUE_SIZE * WORK_ENTRY_SIZE - 1)
        inc     cx
        
        ; Process up to 16 items per call (budget)
        cmp     cx, 16
        jb      .process_loop
        
.done:
        mov     [queue_tail], si
        mov     ax, cx                  ; Return count
        
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret

;-----------------------------------------------------------------------------
; Stub handlers (to be implemented)
;-----------------------------------------------------------------------------
handle_tx_complete:
        ; TODO: Implement TX completion
        ret

handle_rx_complete:
        ; TODO: Implement RX processing
        ret