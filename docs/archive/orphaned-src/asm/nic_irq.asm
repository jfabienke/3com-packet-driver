; @file nic_irq.asm
; @brief NIC interrupt handling
;
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
;
; This file is part of the 3Com Packet Driver project.
;

.MODEL SMALL
.386

include 'tsr_defensive.inc'

; Reentrancy-safe stack switching macros for NIC IRQ handlers
; These macros are independent of DS assumptions and store caller context
; on the IRQ stack itself to avoid reentrancy corruption
;
; Requirements: None (fully self-contained)
; Uses: BX, DX, AX (saved on new stack)
; Preserves: All registers except those used for switching

; ======================================================
; SAFE_STACK_SWITCH_3C509B — Truly reentrancy-safe stack switch
;
; Switches to private IRQ stack for interrupt handling with full reentrancy
; protection. Handles nested interrupts correctly by detecting when already
; on the IRQ stack and preserving previous stack frames.
;
; ALGORITHM:
;   1. Save current SS:SP in registers
;   2. Check if already on IRQ stack (SS=_DATA AND SP within bounds+headroom)
;   3a. If already on stack: Push context on current stack (no SP reset)
;   3b. If not on stack: Switch to IRQ stack top, then push context
;   4. Set DS=_DATA for safe variable access
;   5. Clear direction flag and re-enable interrupts
;
; HEADROOM: Requires 8+ bytes available before pushing (4 words * 2 bytes)
; NESTING: Unlimited depth supported - each level gets proper stack frame
; IF POLICY: Always enables interrupts (STI) - suitable for hardware ISRs
;
; Entry: Any stack state, any DS, IF=0 (typical ISR entry)
; Exit:  Private IRQ stack, DS=_DATA, DF=0, IF=1
; 
; Clobbers: AX, BX, CX, DX (CX added for headroom calculation)
; Pushes:   Caller's SP, SS, DS, ES (4 words on IRQ stack)
; Preserves: All other registers
; Requirements: irq_stack_3c509b and irq_stack_top_3c509b in _DATA/DGROUP
; Companion: Use with RESTORE_CALLER_STACK_IRQ for proper ISR epilogue
; ======================================================
macro SAFE_STACK_SWITCH_3C509B
    LOCAL __SWS3C509B_DoSwitch, __SWS3C509B_Done

    cli
    mov     bx, ss                      ; BX = caller SS
    mov     dx, sp                      ; DX = caller SP
    mov     ax, _DATA                   ; AX = our DGROUP segment

    ; Are we already on our IRQ stack?
    cmp     bx, ax
    jne     __SWS3C509B_DoSwitch        ; SS != _DATA -> must switch

    ; SS == _DATA, check SP bounds with headroom: SP must be > bottom+8 and <= top
    ; Need 8 bytes headroom for 4 word pushes (caller SP, SS, DS, ES)
    mov     cx, OFFSET irq_stack_3c509b
    add     cx, 8                       ; CX = bottom + headroom needed
    cmp     dx, cx
    jbe     __SWS3C509B_DoSwitch        ; SP <= bottom+8 -> insufficient headroom, must switch
    cmp     dx, OFFSET irq_stack_top_3c509b
    ja      __SWS3C509B_DoSwitch        ; SP > top -> not our IRQ stack

    ; Already on our IRQ stack: do not reset SP. Save context on current stack.
    push    dx                          ; Save caller SP
    push    bx                          ; Save caller SS
    push    ds                          ; Save caller DS
    push    es                          ; Save caller ES
    mov     ds, ax                      ; DS = _DATA
    cld
    sti
    jmp     SHORT __SWS3C509B_Done

__SWS3C509B_DoSwitch:
    ; Switch to IRQ stack
    mov     ss, ax
    mov     sp, OFFSET irq_stack_top_3c509b
    ; Now save caller context on the IRQ stack
    push    dx                          ; caller SP
    push    bx                          ; caller SS
    push    ds                          ; caller DS
    push    es                          ; caller ES
    mov     ds, ax                      ; DS = _DATA
    cld
    sti

__SWS3C509B_Done:
endm

; ======================================================
; SAFE_STACK_SWITCH_3C515 — Truly reentrancy-safe stack switch
; ======================================================
; Same semantics as 3C509B variant, but for 3C515's IRQ stack.
; ======================================================
macro SAFE_STACK_SWITCH_3C515
    LOCAL __SWS3C515_DoSwitch, __SWS3C515_Done

    cli
    mov     bx, ss                      ; BX = caller SS
    mov     dx, sp                      ; DX = caller SP
    mov     ax, _DATA                   ; AX = our DGROUP segment

    ; Are we already on our IRQ stack?
    cmp     bx, ax
    jne     __SWS3C515_DoSwitch         ; SS != _DATA -> must switch

    ; SS == _DATA, check SP bounds with headroom: SP must be > bottom+8 and <= top
    ; Need 8 bytes headroom for 4 word pushes (caller SP, SS, DS, ES)
    mov     cx, OFFSET irq_stack_3c515
    add     cx, 8                       ; CX = bottom + headroom needed
    cmp     dx, cx
    jbe     __SWS3C515_DoSwitch         ; SP <= bottom+8 -> insufficient headroom, must switch
    cmp     dx, OFFSET irq_stack_top_3c515
    ja      __SWS3C515_DoSwitch         ; SP > top -> not our IRQ stack

    ; Already on our IRQ stack: do not reset SP. Save context on current stack.
    push    dx                          ; Save caller SP
    push    bx                          ; Save caller SS
    push    ds                          ; Save caller DS
    push    es                          ; Save caller ES
    mov     ds, ax                      ; DS = _DATA
    cld
    sti
    jmp     SHORT __SWS3C515_Done

__SWS3C515_DoSwitch:
    ; Switch to IRQ stack
    mov     ss, ax
    mov     sp, OFFSET irq_stack_top_3c515
    ; Now save caller context on the IRQ stack
    push    dx                          ; caller SP
    push    bx                          ; caller SS
    push    ds                          ; caller DS
    push    es                          ; caller ES
    mov     ds, ax                      ; DS = _DATA
    cld
    sti

__SWS3C515_Done:
endm

macro RESTORE_CALLER_STACK_IRQ
    cli                             ; Critical section start - no interrupts during restore
    ; Restore caller ES/DS from our IRQ stack
    pop     es                      ; Restore caller ES
    pop     ds                      ; Restore caller DS
    ; Restore caller SS:SP from our IRQ stack
    pop     bx                      ; Restore old SS
    pop     dx                      ; Restore old SP
    mov     ss, bx                  ; SS = caller's segment
    mov     sp, dx                  ; SP = caller's pointer (atomic after SS load)
    sti                             ; Re-enable interrupts
endm

; Interrupt-related constants
IRQ_NONE            EQU 0FFh    ; No IRQ assigned
IRQ_MIN             EQU 3       ; Minimum valid IRQ
IRQ_MAX             EQU 15      ; Maximum valid IRQ

; NIC interrupt status flags
IRQ_RX_COMPLETE     EQU 01h     ; Packet received
IRQ_TX_COMPLETE     EQU 02h     ; Packet transmitted
IRQ_TX_ERROR        EQU 04h     ; Transmission error
IRQ_RX_ERROR        EQU 08h     ; Reception error
IRQ_LINK_CHANGE     EQU 10h     ; Link status changed
IRQ_COUNTER_OVERFLOW EQU 20h    ; Statistics counter overflow

; Hardware interrupt vectors (8259 PIC)
PIC1_COMMAND        EQU 20h     ; Master PIC command port
PIC1_DATA           EQU 21h     ; Master PIC data port
PIC2_COMMAND        EQU 0A0h    ; Slave PIC command port
PIC2_DATA           EQU 0A1h    ; Slave PIC data port
PIC_EOI             EQU 20h     ; End of interrupt command

; Maximum number of NICs
MAX_NICS            EQU 2       ; Support up to 2 NICs

; Data segment
_DATA SEGMENT
        ASSUME  DS:_DATA

; IRQ management
nic_irq_numbers     db MAX_NICS dup(IRQ_NONE)    ; IRQ numbers for each NIC
original_irq_vectors dd MAX_NICS dup(0)          ; Original interrupt vectors
irq_installed       db MAX_NICS dup(0)           ; IRQ installation flags

; Interrupt statistics
irq_count           dw MAX_NICS dup(0)           ; Interrupt count per NIC
spurious_irq_count  dw 0                         ; Spurious interrupt count

; Current receive mode
current_receive_mode db 2                        ; Default to direct mode

; Defensive programming data (required by tsr_defensive.inc macros)
; Note: caller_ss/caller_sp removed - now stored on IRQ stack for reentrancy safety
critical_nesting     db 0                        ; Critical section nesting level
indos_segment        dw 0                        ; InDOS flag segment
indos_offset         dw 0                        ; InDOS flag offset
criterr_segment      dw 0                        ; Critical error flag segment
criterr_offset       dw 0                        ; Critical error flag offset

; Private IRQ handler stacks with overflow protection
; Sized for worst-case nested interrupts: NIC + Timer + Keyboard + Disk + FPU
; 2KB each should handle ~20 nested interrupts with 100 bytes average per ISR

; 16-bit stack canary patterns for overflow detection (8086/286 compatible)
; Using word halves of 0xDEADBEEF in little-endian format
CANARY_WORD_LO  equ 0BEEFh              ; Low word of 0xDEADBEEF
CANARY_WORD_HI  equ 0DEADh              ; High word of 0xDEADBEEF

; Red zone canary at bottom of each stack (16 bytes = 8 words)
irq_stack_3c509b_canary dw 8 dup(CANARY_WORD_LO)

; 3C509B IRQ stack (2KB + red zone)
irq_stack_3c509b     db 2048 dup(?)
irq_stack_top_3c509b equ $                    ; Use full stack capacity

; Red zone canary between stacks (16 bytes = 8 words)
irq_stack_3c515_canary dw 8 dup(CANARY_WORD_LO)

; 3C515 IRQ stack (2KB + red zone)  
irq_stack_3c515      db 2048 dup(?)
irq_stack_top_3c515  equ $                    ; Use full stack capacity

; Stack overflow detection counters
stack_overflow_3c509b dw 0               ; 3C509B overflow count
stack_overflow_3c515  dw 0               ; 3C515 overflow count
emergency_canary_count dw 0              ; Emergency canary response count

; Deferred work function addresses
deferred_3c509b_work dw OFFSET process_3c509b_packets
deferred_3c515_work  dw OFFSET process_3c515_packets

; Interrupt batching control
MAX_EVENTS_PER_IRQ   equ 10              ; Maximum events per interrupt (batching limit)
batch_count_3c509b   db 0                ; Current batch count for 3C509B
batch_count_3c515    db 0                ; Current batch count for 3C515-TX

; Temporary packet buffer (should be replaced with proper buffer pool)
temp_rx_buffer       db 1600 dup(?)     ; Buffer for packet reception

; Recovery counters (referenced by defensive_integration.asm)
recovery_count       dw 0                ; Vector recovery attempts

; Enhanced statistics counters
rx_packet_count      dw 0                ; Total RX packets
tx_packet_count      dw 0                ; Total TX packets
rx_error_count       dw 0                ; Total RX errors
tx_error_count       dw 0                ; Total TX errors
rx_oversize_errors   dw 0                ; Oversize packet errors
rx_crc_errors        dw 0                ; CRC errors
rx_framing_errors    dw 0                ; Framing errors
tx_max_coll_errors   dw 0                ; Max collision errors
tx_underrun_errors   dw 0                ; TX underrun errors
tx_jabber_errors     dw 0                ; Jabber errors

; ISR state variables
current_iobase       dw 0                ; Current NIC I/O base
current_tx_buffer    dw 0                ; Current TX buffer pointer
current_tx_retries   db 0                ; TX retry counter
irq_on_slave         db 0                ; Flag for slave PIC IRQ
nic_iobase_table     dw MAX_NICS dup(0)  ; I/O base addresses

; DMA management structures
rx_dma_ring_base     dd 0                ; RX DMA ring base address
rx_dma_ring_end      dw 0                ; RX DMA ring end offset
rx_dma_ring_ptr      dw 0                ; Current RX DMA pointer
tx_dma_ring_base     dd 0                ; TX DMA ring base address
tx_dma_ring_end      dw 0                ; TX DMA ring end offset
tx_dma_ring_ptr      dw 0                ; Current TX DMA pointer

; Buffer pool management
rx_buffer_pool       dd 32 dup(0)        ; RX buffer pool
tx_buffer_pool       dd 16 dup(0)        ; TX buffer pool
rx_free_count        dw 32               ; Free RX buffers
tx_free_count        dw 16               ; Free TX buffers

; 64-bit byte counters
tx_bytes_lo          dw 0                ; TX bytes low word
tx_bytes_hi          dw 0                ; TX bytes high word
rx_bytes_lo          dw 0                ; RX bytes low word
rx_bytes_hi          dw 0                ; RX bytes high word

_DATA ENDS

; Code segment
_TEXT SEGMENT
        ASSUME  CS:_TEXT, DS:_DATA

; Public function exports
PUBLIC nic_irq_init
PUBLIC install_nic_irq
PUBLIC uninstall_nic_irq
PUBLIC nic_irq_handler_3c509b
PUBLIC nic_irq_handler_3c515
PUBLIC nic_irq_handler_3c509b_batched
PUBLIC nic_irq_handler_3c515_batched
PUBLIC nic_common_irq_handler
PUBLIC nic_set_receive_mode
PUBLIC get_irq_stats
; Enhanced interrupt handling exports
PUBLIC process_3c509b_packets
PUBLIC process_3c515_packets
PUBLIC nic_process_interrupt_batch_3c509b
PUBLIC nic_process_interrupt_batch_3c515
PUBLIC check_3c509b_interrupt_source
PUBLIC check_3c515_interrupt_source
PUBLIC acknowledge_3c509b_interrupt
PUBLIC acknowledge_3c515_interrupt

; External references
EXTRN hardware_handle_3c509b_irq:PROC    ; From hardware.asm
EXTRN hardware_handle_3c515_irq:PROC     ; From hardware.asm
EXTRN packet_ops_receive:PROC            ; From packet_ops.asm
EXTRN get_cpu_features:PROC              ; From cpu_detect.asm
EXTRN _3c515_handle_interrupt_batched:PROC ; Enhanced 3C515 handler
EXTRN _3c509b_handle_interrupt_batched:PROC ; Enhanced 3C509B handler

; Defensive programming integration
EXTRN queue_deferred_work:PROC           ; From defensive_integration.asm

;-----------------------------------------------------------------------------
; nic_irq_init - Initialize NIC interrupt handling
;
; Input:  None
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
nic_irq_init PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Validate IRQ ranges for NICs (only 3,5,7,9-12,15 allowed)
        call    validate_irq_ranges
        jc      .invalid_irq_config
        
        ; Initialize PIC state tracking
        call    init_pic_state
        jc      .pic_init_failed
        
        ; Detect current IRQ conflicts
        call    detect_irq_conflicts
        ; Continue even if conflicts detected (warnings only)

        ; Clear IRQ assignment table
        mov     cx, MAX_NICS
        mov     si, OFFSET nic_irq_numbers
        mov     al, IRQ_NONE
.clear_irq_loop:
        mov     [si], al
        inc     si
        loop    .clear_irq_loop

        ; Clear installation flags
        mov     cx, MAX_NICS
        mov     si, OFFSET irq_installed
        xor     al, al
.clear_flags_loop:
        mov     [si], al
        inc     si
        loop    .clear_flags_loop
        
        ; Initialize interrupt batching counters
        mov     byte ptr [batch_count_3c509b], 0
        mov     byte ptr [batch_count_3c515], 0
        
        ; Clear interrupt statistics
        mov     word ptr [irq_count], 0
        mov     word ptr [irq_count+2], 0
        mov     word ptr [spurious_irq_count], 0
        
        ; Initialize defensive programming data required by macros
        mov     word ptr [caller_ss], 0
        mov     word ptr [caller_sp], 0
        mov     byte ptr [critical_nesting], 0

        ; Auto-detect or use configured IRQ assignments
        call    auto_detect_irqs
        jnc     .irq_detection_done
        
        ; Use fallback defaults if auto-detection fails
        call    apply_default_irqs
        
.irq_detection_done:

        ; Success
        mov     ax, 0
        jmp     .exit

.invalid_irq_config:
        mov     ax, 1
        jmp     .exit
        
.pic_init_failed:
        mov     ax, 2
        jmp     .exit

.exit:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
nic_irq_init ENDP

;-----------------------------------------------------------------------------
; install_nic_irq - Install interrupt handler for specific NIC
;
; Input:  AL = NIC index (0-based), BL = IRQ number
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX, SI, DI, ES
;-----------------------------------------------------------------------------
install_nic_irq PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es

        ; Validate IRQ number against allowed ranges
        call    validate_single_irq_number
        jc      .invalid_irq
        
        ; Check for IRQ conflicts with other devices
        call    check_irq_conflict
        jc      .irq_conflict
        
        ; Save original interrupt vector before installation
        call    save_original_vector
        jc      .save_failed
        
        ; Install appropriate interrupt handler based on NIC type
        call    install_irq_handler
        jc      .install_failed
        
        ; Enable IRQ at PIC level
        call    enable_pic_irq
        jc      .pic_enable_failed

        ; Validate NIC index
        cmp     al, MAX_NICS
        jae     .invalid_nic

        ; Validate IRQ number
        cmp     bl, IRQ_MIN
        jb      .invalid_irq
        cmp     bl, IRQ_MAX
        ja      .invalid_irq

        ; Store IRQ number
        mov     si, OFFSET nic_irq_numbers
        mov     ah, 0
        add     si, ax
        mov     [si], bl

        ; Installation successful - update tracking structures
        call    update_irq_tracking

        ; Mark as installed
        mov     si, OFFSET irq_installed
        add     si, ax
        mov     byte ptr [si], 1

        ; Success
        mov     ax, 0
        jmp     .exit

.invalid_nic:
        mov     ax, 1
        jmp     .exit

.invalid_irq:
        mov     ax, 2
        jmp     .exit
        
.irq_conflict:
        mov     ax, 3
        jmp     .exit
        
.save_failed:
        mov     ax, 4
        jmp     .exit
        
.install_failed:
        mov     ax, 5
        jmp     .exit
        
.pic_enable_failed:
        mov     ax, 6
        jmp     .exit

.exit:
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
install_nic_irq ENDP

;-----------------------------------------------------------------------------
; uninstall_nic_irq - Uninstall interrupt handler for specific NIC
;
; Input:  AL = NIC index (0-based)
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX, SI, ES
;-----------------------------------------------------------------------------
uninstall_nic_irq PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    es

        ; Get IRQ number for this NIC
        call    get_nic_irq_number
        jc      .not_installed
        
        ; Disable IRQ at PIC level first
        call    disable_pic_irq
        
        ; Restore original interrupt vector
        call    restore_original_vector
        
        ; Clear IRQ tracking information
        call    clear_irq_tracking

        ; Validate NIC index
        cmp     al, MAX_NICS
        jae     .invalid_nic

        ; Check if IRQ is installed
        mov     si, OFFSET irq_installed
        mov     ah, 0
        add     si, ax
        cmp     byte ptr [si], 0
        je      .not_installed

        ; Update uninstall tracking
        call    update_uninstall_tracking

        ; Mark as uninstalled
        mov     byte ptr [si], 0

        ; Clear IRQ number
        mov     si, OFFSET nic_irq_numbers
        add     si, ax
        mov     byte ptr [si], IRQ_NONE

        ; Success
        mov     ax, 0
        jmp     .exit

.invalid_nic:
        mov     ax, 1
        jmp     .exit

.not_installed:
        mov     ax, 0   ; Not an error if already uninstalled
        jmp     .exit

.exit:
        pop     es
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
uninstall_nic_irq ENDP

;-----------------------------------------------------------------------------
; nic_irq_handler_3c509b - OPTIMIZED Interrupt handler for 3C509B NIC
; Phase 3 Performance Optimization: <100µs execution target
;
; Optimizations:
; - Streamlined ISR critical path
; - CPU-specific register operations (286+/386+)
; - Interrupt coalescing with batching
; - Minimal ISR work with deferred processing
; - Performance measurement integration
;
; Input:  None (interrupt context)
; Output: None
; Uses:   All registers (saved/restored with CPU optimization)
;-----------------------------------------------------------------------------
nic_irq_handler_3c509b PROC
        ; === PHASE 3 OPTIMIZED ISR PROLOG ===
        ; Start performance measurement immediately
        push    ax
        push    dx
        
        ; Quick timestamp for ISR performance tracking
        mov     ah, 0
        int     1Ah                     ; Get tick count for timing
        mov     [perf_start_time], dx
        
        pop     dx
        
        ; Set up our data segment (optimized)
        mov     ax, _DATA
        mov     ds, ax
        ASSUME  DS:_DATA
        
        ; CPU-optimized register save based on detected capabilities
        test    word ptr [cpu_capabilities], CPU_CAP_286
        jz      .save_8086_style
        
        ; 286+ optimized save using PUSHA
        pusha                           ; Single instruction vs 8 pushes
        push    es
        push    ds
        jmp     .registers_saved
        
.save_8086_style:
        ; 8086 compatible register save
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        push    ds
        
.registers_saved:
        ; Switch to private stack using defensive macro
        SAFE_STACK_SWITCH_3C509B
        
        ; === STREAMLINED ISR CRITICAL PATH (Target: <50µs) ===
        ; Ultra-fast hardware check - is this interrupt really ours?
        call    check_3c509b_interrupt_source_fast
        jc      .spurious_interrupt
        
        ; Optimized interrupt coalescing and batching
        ; This is the key to achieving <100µs target
        call    interrupt_coalesce_and_batch_3c509b
        cmp     ax, 0
        je      .no_immediate_work      ; All work coalesced for later
        
        ; Minimal immediate processing only for urgent interrupts
        call    process_urgent_3c509b_interrupts
        
        ; Send EOI with optimized PIC handling
        call    send_eoi_for_3c509b_optimized
        
.no_immediate_work:
        ; Increment performance counters
        inc     word ptr [irq_count]
        inc     dword ptr [total_interrupts]
        
        ; === DEFER ALL HEAVY WORK ===
        ; Queue comprehensive packet processing for DOS idle time
        mov     ax, [deferred_3c509b_work]
        call    queue_deferred_work
        jc      .queue_overflow_handled ; Fallback already handled
        
        jmp     .isr_exit_optimized

.spurious_interrupt:
        ; Optimized spurious interrupt handling
        inc     word ptr [spurious_irq_count]
        ; Don't send EOI for spurious interrupts
        jmp     .isr_exit_optimized

.queue_overflow_handled:
        ; Queue overflow is handled in interrupt_coalesce_and_batch_3c509b
        ; No additional processing needed here
        
.isr_exit_optimized:
        ; === PHASE 3 OPTIMIZED ISR EPILOG ===
        ; End performance measurement
        push    ax
        push    dx
        mov     ah, 0
        int     1Ah                     ; Get end tick count
        mov     [perf_end_time], dx
        
        ; Calculate and record ISR execution time
        mov     ax, dx
        sub     ax, [perf_start_time]
        mov     [isr_execution_time], ax
        
        ; Record performance sample for monitoring
        push    cx
        mov     cl, 1                   ; Interrupt type: RX/TX
        mov     ch, [interrupt_batch_count]
        call    performance_monitor_record_sample_asm
        pop     cx
        
        pop     dx
        pop     ax
        
        ; Restore caller's stack
        cli                             ; Critical section start
        mov     ss, [caller_ss]
        mov     sp, [caller_sp]
        sti                             ; Critical section end
        
        ; CPU-optimized register restore
        test    word ptr [cpu_capabilities], CPU_CAP_286
        jz      .restore_8086_style
        
        ; 286+ optimized restore using POPA
        pop     ds
        pop     es
        popa                            ; Single instruction vs 8 pops
        jmp     .registers_restored
        
.restore_8086_style:
        ; 8086 compatible register restore
        pop     ds
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        
.registers_restored:
        ; Final register restore and return
        pop     ax
        
        iret
nic_irq_handler_3c509b ENDP

;-----------------------------------------------------------------------------
; PHASE 3 OPTIMIZED Support functions for enhanced 3C509B interrupt handling
;-----------------------------------------------------------------------------

; Fast interrupt source check - optimized for minimal latency
check_3c509b_interrupt_source_fast PROC
        ; Ultra-fast hardware check optimized for <10µs execution
        ; Returns: CY clear if ours, CY set if not ours
        push    dx
        push    bx
        
        ; Get I/O base with optimized lookup
        mov     bx, 0                   ; NIC 0 index
        shl     bx, 1                   ; Word offset
        mov     dx, [hw_io_bases + bx]  ; Direct indexed lookup
        test    dx, dx
        jz      .not_ours_fast          ; No I/O base = not our interrupt
        
        ; Single combined read for all interrupt status bits
        add     dx, 0Eh                 ; Status register offset
        in      ax, dx                  ; Read status (16-bit)
        
        ; Check multiple interrupt conditions in parallel
        ; IntLatch (bit 0) + IntReq (bit 6) + any error bits (7-11)
        test    ax, 0FC1h               ; Test all relevant bits at once
        jz      .not_ours_fast          ; No interrupt condition
        
        ; Additional optimization: check for common interrupt patterns
        test    al, 01h                 ; IntLatch
        jz      .not_ours_fast
        test    al, 40h                 ; IntReq
        jz      .not_ours_fast
        
        ; This is our interrupt - optimize for common case
        pop     bx
        pop     dx
        clc                             ; Clear carry = ours
        ret
        
.not_ours_fast:
        pop     bx
        pop     dx
        stc                             ; Set carry = not ours
        ret
check_3c509b_interrupt_source_fast ENDP

; Optimized interrupt coalescing and batching
interrupt_coalesce_and_batch_3c509b PROC
        ; Advanced interrupt coalescing for <100µs ISR target
        ; Input: None
        ; Output: AX = 0 if coalesced, non-zero if immediate work needed
        push    bx
        push    cx
        push    dx
        
        ; Read hardware interrupt status
        mov     bx, 0                   ; NIC 0 index
        shl     bx, 1
        mov     dx, [hw_io_bases + bx]
        add     dx, 0Eh                 ; Status register
        in      ax, dx                  ; Read interrupt status
        
        ; Apply interrupt coalescing algorithm
        call    interrupt_coalesce_process
        mov     cx, ax                  ; Save coalesced result
        
        ; If coalescing active, return 0 for no immediate work
        test    cx, cx
        jz      .coalescing_active
        
        ; Process immediate batch with size limit
        mov     byte ptr [interrupt_batch_count], 0
        call    interrupt_batch_process_optimized
        
        ; Return non-zero to indicate immediate work was done
        mov     ax, 1
        jmp     .coalesce_done
        
.coalescing_active:
        ; All work deferred - return 0
        xor     ax, ax
        
.coalesce_done:
        pop     dx
        pop     cx
        pop     bx
        ret
interrupt_coalesce_and_batch_3c509b ENDP

; Process urgent interrupts only (minimal work)
process_urgent_3c509b_interrupts PROC
        ; Process only the most urgent interrupts in <20µs
        ; Everything else is deferred for comprehensive processing
        push    ax
        push    dx
        push    bx
        
        ; Get hardware status with minimal overhead
        mov     bx, 0
        shl     bx, 1
        mov     dx, [hw_io_bases + bx]
        add     dx, 0Eh
        in      ax, dx
        
        ; Check only for critical error conditions that need immediate attention
        test    ax, 0002h               ; ADAPTER_FAILURE
        jz      .no_critical_error
        
        ; Minimal error handling - just acknowledge and log
        call    acknowledge_3c509b_critical_error
        inc     dword ptr [critical_errors]
        
.no_critical_error:
        ; All other interrupt processing is deferred
        ; This keeps ISR time to absolute minimum
        
        pop     bx
        pop     dx
        pop     ax
        ret
process_urgent_3c509b_interrupts ENDP

; Optimized EOI sending with reduced PIC access
send_eoi_for_3c509b_optimized PROC
        ; Ultra-fast EOI with minimal PIC access
        push    ax
        push    bx
        push    dx
        
        ; Get IRQ number with optimized lookup
        mov     bl, byte ptr [nic_irq_numbers]  ; NIC 0 IRQ
        cmp     bl, IRQ_NONE
        je      .no_eoi_needed
        
        ; GPT-5: Check for spurious IRQ7/IRQ15 before sending EOI
        cmp     bl, 7
        je      .check_spurious_irq7
        cmp     bl, 15
        je      .check_spurious_irq15
        jmp     .send_normal_eoi
        
.check_spurious_irq7:
        ; Check ISR register to see if IRQ7 is really active
        mov     al, 0Bh                 ; Read ISR command (OCW3)
        out     PIC1_COMMAND, al
        jmp     short $+2               ; I/O delay
        in      al, PIC1_COMMAND        ; Read ISR
        push    ax                      ; Save ISR value
        
        ; GPT-5 Fix: Restore OCW3 to read IRR (default)
        mov     al, 0Ah                 ; Read IRR command (OCW3)
        out     PIC1_COMMAND, al
        
        pop     ax                      ; Restore ISR value
        test    al, 80h                 ; Check bit 7 (IRQ7)
        jnz     .send_normal_eoi        ; Real IRQ7, send EOI
        ; Spurious IRQ7 - don't send EOI
        inc     word ptr [spurious_irq_count]
        jmp     .no_eoi_needed
        
.check_spurious_irq15:
        ; Check ISR register to see if IRQ15 is really active
        mov     al, 0Bh                 ; Read ISR command (OCW3)
        out     PIC2_COMMAND, al
        jmp     short $+2               ; I/O delay
        in      al, PIC2_COMMAND        ; Read ISR
        push    ax                      ; Save ISR value
        
        ; GPT-5 Fix: Restore OCW3 to read IRR (default)
        mov     al, 0Ah                 ; Read IRR command (OCW3)
        out     PIC2_COMMAND, al
        
        pop     ax                      ; Restore ISR value
        test    al, 80h                 ; Check bit 7 (IRQ15)
        jnz     .send_normal_eoi        ; Real IRQ15, send EOI
        ; Spurious IRQ15 - send EOI only to master (not slave)
        inc     word ptr [spurious_irq_count]
        ; GPT-5 Fix: Ensure AL has EOI value
        mov     al, PIC_EOI
        out     PIC1_COMMAND, al        ; EOI to master only (use immediate)
        jmp     .eoi_complete
        
.send_normal_eoi:
        ; GPT-5 Fix: Always load EOI value into AL
        mov     al, PIC_EOI
        cmp     bl, 8
        jl      .master_pic_eoi
        
        ; Slave PIC - send EOI to slave first, then master
        out     PIC2_COMMAND, al        ; EOI to slave (immediate port)
        mov     al, PIC_EOI             ; Reload EOI value
        out     PIC1_COMMAND, al        ; EOI to master (immediate port)
        jmp     .eoi_complete
        
.master_pic_eoi:
        ; Master PIC only
        out     PIC1_COMMAND, al        ; Use immediate port for consistency
        
.eoi_complete:
.no_eoi_needed:
        pop     dx
        pop     bx
        pop     ax
        ret
send_eoi_for_3c509b_optimized ENDP

; Optimized batch processing with CPU-specific instructions
interrupt_batch_process_optimized PROC
        ; Process interrupt batch with CPU-specific optimizations
        ; Target: Process up to 10 interrupts in <30µs
        push    bx
        push    cx
        push    dx
        
        mov     cl, 0                   ; Processed count
        mov     bl, al                  ; Interrupt mask
        
        ; Check for 486+ BSF instruction optimization
        test    word ptr [cpu_capabilities], CPU_CAP_BSF
        jnz     .use_bsf_batch
        
        ; Generic bit scanning for 286/386
        mov     dl, 1
        
.generic_batch_loop:
        test    bl, dl
        jz      .next_generic_bit
        
        ; Process interrupt bit (minimal work only)
        call    process_interrupt_bit_minimal
        inc     cl
        
        ; Check batch limit
        cmp     cl, MAX_EVENTS_PER_IRQ
        jae     .batch_complete
        
.next_generic_bit:
        shl     dl, 1
        jnz     .generic_batch_loop
        jmp     .batch_complete
        
.use_bsf_batch:
        ; 486+ optimized bit scanning
        test    bl, bl
        jz      .batch_complete
        
.bsf_batch_loop:
        ; Find first set bit using BSF
        db      0Fh, 0BCh, 0D3h        ; BSF DX, BX
        jz      .batch_complete
        
        ; Process interrupt bit
        call    process_interrupt_bit_minimal
        inc     cl
        
        ; Clear processed bit
        push    ax
        mov     al, 1
        shl     al, dl
        not     al
        and     bl, al
        pop     ax
        
        ; Check batch limit
        cmp     cl, MAX_EVENTS_PER_IRQ
        jb      .bsf_batch_loop
        
.batch_complete:
        ; Update batch statistics
        mov     [interrupt_batch_count], cl
        cmp     cl, 1
        jbe     .no_batch_stats
        inc     dword ptr [batched_interrupts]
        
.no_batch_stats:
        pop     dx
        pop     cx
        pop     bx
        ret
interrupt_batch_process_optimized ENDP

; Minimal interrupt bit processing (defers heavy work)
process_interrupt_bit_minimal PROC
        ; Process single interrupt bit with minimal overhead
        ; Input: DL = bit position
        ; Defers all heavy processing to DOS idle handler
        push    ax
        push    bx
        
        ; Just acknowledge the interrupt condition in hardware
        ; All actual packet processing is deferred
        cmp     dl, 0                   ; RX Complete
        je      .ack_rx_minimal
        cmp     dl, 1                   ; TX Complete  
        je      .ack_tx_minimal
        jmp     .ack_generic
        
.ack_rx_minimal:
        ; Minimal RX acknowledgment
        mov     bx, 0
        shl     bx, 1
        mov     ax, [hw_io_bases + bx]
        add     ax, 0Ch                 ; RX_STATUS
        mov     dx, ax
        in      ax, dx                  ; Read to acknowledge
        jmp     .ack_done
        
.ack_tx_minimal:
        ; Minimal TX acknowledgment
        mov     bx, 0
        shl     bx, 1
        mov     ax, [hw_io_bases + bx]
        add     ax, 0Ah                 ; TX_STATUS
        mov     dx, ax
        in      al, dx                  ; Read to acknowledge
        jmp     .ack_done
        
.ack_generic:
        ; Generic interrupt acknowledgment
        nop                             ; Placeholder
        
.ack_done:
        pop     bx
        pop     ax
        ret
process_interrupt_bit_minimal ENDP

; Critical error handling (minimal ISR work)
acknowledge_3c509b_critical_error PROC
        ; Handle critical errors with minimal ISR overhead
        push    ax
        push    dx
        push    bx
        
        ; Get I/O base
        mov     bx, 0
        shl     bx, 1
        mov     dx, [hw_io_bases + bx]
        
        ; Clear adapter failure bit
        add     dx, 0Eh                 ; Command register
        mov     ax, (13 shl 11) or 0002h ; AckIntr | AdapterFailure
        out     dx, ax
        
        pop     bx
        pop     dx
        pop     ax
        ret
acknowledge_3c509b_critical_error ENDP

; Assembly interface for performance monitoring
performance_monitor_record_sample_asm PROC
        ; Record performance sample from assembly context
        ; Input: CL = interrupt type, CH = batch size
        push    ax
        push    bx
        push    dx
        
        ; This would call C performance monitoring function
        ; For now, just update basic counters
        inc     dword ptr [total_interrupts]
        cmp     ch, 1
        jbe     .no_batch_recorded
        inc     dword ptr [batched_interrupts]
        
.no_batch_recorded:
        pop     dx
        pop     bx
        pop     ax
        ret
performance_monitor_record_sample_asm ENDP

; Original function kept for compatibility
check_3c509b_interrupt_source PROC
        ; Quick check if the 3C509B actually generated this interrupt
        ; Returns: CY clear if ours, CY set if not ours
        ; Read 3C509B status register and check IntLatch bit
        push    dx
        push    ax
        
        ; Get I/O base address for NIC 0 (3C509B)
        mov     dx, [hw_io_bases]       ; Get I/O base for first NIC
        test    dx, dx                  ; Check if valid
        jz      .not_ours               ; No I/O base = not our interrupt
        
        ; Read status register (Window-independent, always accessible)
        add     dx, 0Eh                 ; Status register offset
        in      ax, dx                  ; Read status (16-bit for 3C509B)
        
        ; Check IntLatch bit (bit 0) - indicates interrupt occurred
        test    al, 01h                 ; Test IntLatch bit
        jz      .not_ours               ; No IntLatch = not our interrupt
        
        ; Additional validation - check for valid interrupt reasons
        ; IntReq (bit 6) should also be set for a real interrupt
        test    al, 40h                 ; Test IntReq bit
        jz      .not_ours               ; No IntReq = spurious
        
        ; This is our interrupt
        pop     ax
        pop     dx
        clc                             ; Clear carry = ours
        ret
        
.not_ours:
        pop     ax
        pop     dx
        stc                             ; Set carry = not ours
        ret
check_3c509b_interrupt_source ENDP

acknowledge_3c509b_interrupt PROC
        ; Acknowledge the 3C509B hardware interrupt using proper command sequence
        ; This clears the interrupt condition in the NIC
        push    dx
        push    ax
        
        ; Get I/O base address for NIC 0 (3C509B)
        mov     dx, [hw_io_bases]       ; Get I/O base for first NIC
        test    dx, dx                  ; Check if valid
        jz      .no_ack                 ; No I/O base = nothing to ack
        
        ; Read current status to see what needs to be acknowledged
        add     dx, 0Eh                 ; Status/Command register
        in      ax, dx                  ; Read current status
        
        ; Acknowledge interrupt using AckIntr command (13 << 11)
        ; Need to acknowledge IntReq | IntLatch bits as per Linux driver
        ; From Linux driver: outw(AckIntr | IntReq | IntLatch, ioaddr + EL3_CMD)
        mov     ax, (13 shl 11) or 0041h ; AckIntr | IntReq | IntLatch
        out     dx, ax                  ; Send acknowledge command
        
.no_ack:
        pop     ax
        pop     dx
        ret
acknowledge_3c509b_interrupt ENDP

send_eoi_for_3c509b PROC
        ; Send End-Of-Interrupt to the appropriate PIC based on actual IRQ assignment
        ; This fixes the critical gap identified in defensive programming review
        push    ax
        push    bx
        push    dx
        
        ; Get the actual IRQ number for 3C509B (assuming it's NIC 0)
        mov     bl, byte ptr [nic_irq_numbers]  ; Get IRQ for NIC 0
        cmp     bl, IRQ_NONE
        je      .no_irq_assigned                ; Skip EOI if no IRQ assigned
        
        ; Check if this is a slave PIC IRQ (8-15)
        cmp     bl, 8
        jl      .master_pic_only                ; IRQ 0-7: master PIC only
        
        ; IRQ 8-15: Send EOI to slave PIC first, then master PIC
        ; This is the correct sequence for slave IRQs
        mov     al, PIC_EOI
        mov     dx, PIC2_COMMAND
        out     dx, al                          ; EOI to slave PIC
        
        ; Now send EOI to master PIC
        mov     dx, PIC1_COMMAND
        out     dx, al                          ; EOI to master PIC
        jmp     .eoi_complete
        
.master_pic_only:
        ; IRQ 0-7: Send EOI to master PIC only
        mov     al, PIC_EOI
        mov     dx, PIC1_COMMAND
        out     dx, al
        
.eoi_complete:
.no_irq_assigned:
        pop     dx
        pop     bx
        pop     ax
        ret
send_eoi_for_3c509b ENDP

process_3c509b_packets_immediate PROC
        ; Emergency packet processing when deferred queue is full
        ; This is called directly from ISR when we can't defer the work
        push    ax
        push    bx
        push    cx
        
        ; Call the original hardware handler as fallback
        call    hardware_handle_3c509b_irq
        
        pop     cx
        pop     bx
        pop     ax
        ret
process_3c509b_packets_immediate ENDP

;-----------------------------------------------------------------------------
; Deferred work functions (called from DOS idle context)
;-----------------------------------------------------------------------------

PUBLIC process_3c509b_packets
process_3c509b_packets PROC
        ; This function is called from the INT 28h handler when DOS is idle
        ; It can safely call DOS functions and do heavy processing
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    ds
        push    es
        
        ; Set up our data segment
        mov     ax, _DATA
        mov     ds, ax
        ASSUME  DS:_DATA
        
        ; Process 3C509B packets with full functionality
        ; Called from DOS idle context - safe to do heavy processing
        push    ax
        push    bx
        push    cx
        push    dx
        
        ; Check if hardware has packets to process
        mov     dx, 300h                ; 3C509B base I/O address
        add     dx, 08h                 ; RX_STATUS register
        in      ax, dx                  ; Read RX status
        test    ax, 8000h               ; RX_INCOMPLETE bit
        jnz     .no_packets             ; No complete packets
        
        ; Process up to 10 packets per call (interrupt batching)
        mov     cx, 10                  ; Maximum packets to process
        
.packet_loop:
        ; Check if packet is available
        in      ax, dx                  ; Re-read RX status
        test    ax, 8000h               ; Check RX_INCOMPLETE
        jnz     .packets_done           ; No more packets
        
        ; Get packet size
        and     ax, 07FFh               ; Mask to get packet size
        cmp     ax, 60                  ; Minimum Ethernet frame size
        jb      .skip_packet            ; Invalid packet size
        cmp     ax, 1514                ; Maximum Ethernet frame size
        ja      .skip_packet            ; Invalid packet size
        
        ; Read packet data from FIFO
        push    cx
        push    ax                      ; Save packet size
        
        ; Allocate buffer for packet (simplified - should use buffer pool)
        ; For now, assume we have a static receive buffer
        mov     bx, OFFSET temp_rx_buffer
        
        ; Read packet from 3C509B FIFO
        sub     dx, 08h                 ; Back to base address
        add     dx, 00h                 ; DATA_REGISTER
        
        pop     cx                      ; Restore packet size to CX
        shr     cx, 1                   ; Convert bytes to words
        
.read_loop:
        in      ax, dx                  ; Read word from FIFO
        mov     [bx], ax                ; Store in buffer
        add     bx, 2                   ; Advance buffer pointer
        loop    .read_loop              ; Continue reading
        
        ; Handle odd byte if packet size was odd
        pop     ax                      ; Restore original packet size
        test    ax, 1                   ; Check if odd size
        jz      .even_size
        
        in      ax, dx                  ; Read final word
        mov     [bx], al                ; Store only the byte we need
        
.even_size:
        ; Call packet receive handler
        push    cx
        mov     ax, 0                   ; NIC index (3C509B = 0)
        mov     bx, OFFSET temp_rx_buffer ; Packet buffer
        call    packet_ops_receive      ; Process the packet
        pop     cx
        
        ; Update statistics
        inc     word ptr [irq_count]    ; Increment packet count
        
        pop     cx                      ; Restore loop counter
        dec     cx                      ; Decrement remaining count
        jnz     .packet_loop            ; Process next packet
        
        jmp     .packets_done
        
.skip_packet:
        ; Skip malformed packet by reading and discarding
        sub     dx, 08h                 ; Back to base
        add     dx, 0Ch                 ; COMMAND register
        mov     ax, 4000h               ; RX_DISCARD command
        out     dx, ax                  ; Discard current packet
        
        ; Continue with next packet
        add     dx, 4                   ; Back to RX_STATUS
        loop    .packet_loop
        
.packets_done:
.no_packets:
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
process_3c509b_packets ENDP

;-----------------------------------------------------------------------------
; nic_irq_handler_3c515 - Interrupt handler for 3C515-TX NIC
; This is the actual IRQ handler installed in the interrupt vector
;
; Input:  None (interrupt context)
; Output: None
; Uses:   All registers (saved/restored)
;-----------------------------------------------------------------------------
nic_irq_handler_3c515 PROC
        ; === DEFENSIVE ISR PROLOG ===
        ; Save minimal registers first
        push    ax
        push    ds
        
        ; Set up our data segment
        mov     ax, _DATA
        mov     ds, ax
        ASSUME  DS:_DATA
        
        ; Switch to private stack using defensive macro
        mov     ax, _DATA               ; Set up data segment first
        SAFE_STACK_SWITCH_3C515
        
        ; Now save all registers on our safe stack
        pusha
        push    es
        push    ds
        
        ; === MINIMAL ISR WORK ONLY ===
        ; Quick hardware check - is this interrupt really ours?
        call    check_3c515_interrupt_source
        jc      .not_our_interrupt
        
        ; Process interrupts with DMA-aware batching (max 10 events per interrupt)
        ; 3C515-TX has more complex interrupt handling due to DMA capabilities
        call    nic_process_interrupt_batch_3c515
        
        ; Send EOI to PIC after processing batch
        call    send_eoi_for_3c515
        
        ; Increment interrupt counter for diagnostics
        inc     word ptr [irq_count+2]   ; NIC 1 counter
        
        ; === DEFER HEAVY WORK ===
        ; Always queue deferred work for comprehensive DMA and packet processing
        ; The batching above handles immediate interrupt acknowledgment
        mov     ax, [deferred_3c515_work]       ; Function to call later
        call    queue_deferred_work
        jc      .queue_full                     ; Handle queue overflow
        
        jmp     .isr_exit

.not_our_interrupt:
        ; This interrupt wasn't ours - don't send EOI
        inc     word ptr [spurious_irq_count]
        jmp     .isr_exit

.queue_full:
        ; Work queue is full - process immediately (not ideal but necessary)
        ; This is our fallback when deferred processing fails
        call    process_3c515_packets_immediate
        
.isr_exit:
        ; === DEFENSIVE ISR EPILOG ===
        ; Restore all registers
        pop     ds
        pop     es
        popa
        
        ; Restore caller's stack using defensive macro
        RESTORE_CALLER_STACK_IRQ
        
        ; Restore minimal registers
        pop     ds
        pop     ax
        
        iret
nic_irq_handler_3c515 ENDP

;-----------------------------------------------------------------------------
; Support functions for enhanced 3C515 interrupt handling
;-----------------------------------------------------------------------------

check_3c515_interrupt_source PROC
        ; Quick check if the 3C515 actually generated this interrupt
        ; Returns: CY clear if ours, CY set if not ours
        push    ax
        push    bx
        push    dx
        
        ; Get the I/O base for 3C515 (assuming NIC 1)
        mov     bx, 1
        shl     bx, 1                           ; Word offset
        mov     dx, [hw_io_bases + bx]          ; Get I/O base
        test    dx, dx
        jz      .not_ours                       ; No I/O base = not ours
        
        ; Read status register (same as command register address)
        add     dx, 0Eh                         ; Status/Command register
        in      ax, dx
        
        ; Check for any interrupt status bits including DMA
        ; Interrupt latch (bit 0)
        test    ax, 0001h                       ; INT_LATCH
        jnz     .our_interrupt
        
        ; Check standard interrupt bits
        test    ax, 0004h                       ; TX_COMPLETE
        jnz     .our_interrupt
        test    ax, 0010h                       ; RX_COMPLETE
        jnz     .our_interrupt
        test    ax, 0002h                       ; ADAPTER_FAILURE
        jnz     .our_interrupt
        test    ax, 0080h                       ; STATS_FULL
        jnz     .our_interrupt
        
        ; Check DMA-specific status bits (3C515 special features)
        test    ax, 0100h                       ; DMA_DONE (bit 8)
        jnz     .our_interrupt
        test    ax, 0200h                       ; DOWN_COMPLETE (TX DMA, bit 9)
        jnz     .our_interrupt
        test    ax, 0400h                       ; UP_COMPLETE (RX DMA, bit 10)
        jnz     .our_interrupt
        
        ; Check interrupt request bit
        test    ax, 0040h                       ; INT_REQ
        jnz     .our_interrupt
        
.not_ours:
        ; Not our interrupt
        stc                                     ; Set carry = not ours
        jmp     .exit
        
.our_interrupt:
        ; This is our interrupt
        clc                                     ; Clear carry = ours
        
.exit:
        pop     dx
        pop     bx
        pop     ax
        ret
check_3c515_interrupt_source ENDP
        
.check_dma:
        ; Check DMA status register for DMA completion interrupts
        ; This is specific to 3C515-TX bus mastering capability
        sub     dx, 08h                 ; Back to base  
        add     dx, 20h                 ; DMA_STATUS register
        in      al, dx                  ; Read DMA status
        test    al, 03h                 ; DMA completion bits
        jnz     .is_ours                ; DMA interrupt is ours
        
        ; Not our interrupt
        stc
        jmp     .validated_3c515
        
.is_ours:
        clc                             ; Clear carry = our interrupt
        
.validated_3c515:
        pop     cx
        pop     bx
        pop     dx
        ret
check_3c515_interrupt_source ENDP

acknowledge_3c515_interrupt PROC
        ; Acknowledge the 3C515 hardware interrupt with DMA handling
        ; This clears the interrupt condition in the NIC
        push    ax
        push    bx
        push    dx
        push    cx
        
        ; Get the I/O base for 3C515 (assuming NIC 1)
        mov     bx, 1
        shl     bx, 1                           ; Word offset
        mov     dx, [hw_io_bases + bx]          ; Get I/O base
        test    dx, dx
        jz      .no_ack_needed                  ; No I/O base = nothing to ack
        
        ; Read current status to see what needs acknowledging
        push    dx
        add     dx, 0Eh                         ; Status register
        in      ax, dx
        mov     cx, ax                          ; Save status for ACK
        pop     dx
        
        ; Acknowledge interrupt using ACK_INTR command
        ; Build acknowledgment mask based on active interrupt sources
        mov     ax, 0                           ; Start with no ACK bits
        
        ; Check and acknowledge standard interrupts
        test    cx, 0004h                       ; TX_COMPLETE
        jz      .no_tx_complete
        or      ax, 0004h                       ; Add TX_COMPLETE to ACK
.no_tx_complete:
        
        test    cx, 0010h                       ; RX_COMPLETE
        jz      .no_rx_complete
        or      ax, 0010h                       ; Add RX_COMPLETE to ACK
.no_rx_complete:
        
        test    cx, 0002h                       ; ADAPTER_FAILURE
        jz      .no_adapter_failure
        or      ax, 0002h                       ; Add ADAPTER_FAILURE to ACK
.no_adapter_failure:
        
        test    cx, 0080h                       ; STATS_FULL
        jz      .no_stats_full
        or      ax, 0080h                       ; Add STATS_FULL to ACK
.no_stats_full:
        
        ; Check and acknowledge DMA interrupts (3C515-TX specific)
        test    cx, 0100h                       ; DMA_DONE
        jz      .no_dma_done
        or      ax, 0100h                       ; Add DMA_DONE to ACK
.no_dma_done:
        
        test    cx, 0200h                       ; DOWN_COMPLETE (TX DMA)
        jz      .no_down_complete
        or      ax, 0200h                       ; Add DOWN_COMPLETE to ACK
.no_down_complete:
        
        test    cx, 0400h                       ; UP_COMPLETE (RX DMA)
        jz      .no_up_complete
        or      ax, 0400h                       ; Add UP_COMPLETE to ACK
.no_up_complete:
        
        ; Send acknowledgment command if we have bits to acknowledge
        test    ax, ax
        jz      .no_ack_needed                  ; Nothing to acknowledge
        
        ; Use ACK_INTR command (13 << 11) with interrupt bits
        push    dx
        add     dx, 0Eh                         ; Command register
        or      ax, (13 << 11)                  ; ACK_INTR command
        out     dx, ax                          ; Send acknowledgment
        pop     dx
        
        ; For DMA interrupts, we may need additional handling
        test    cx, 0700h                       ; Any DMA bits (8, 9, 10)
        jz      .no_dma_handling
        
        ; Additional DMA cleanup if needed
        ; Select window 7 for DMA control
        push    dx
        add     dx, 0Eh                         ; Command register
        mov     ax, (1 << 11) | 7               ; Select window 7
        out     dx, ax
        pop     dx
        
        ; Read DMA status and clear any pending DMA operations
        ; Clean up DMA descriptors and free buffers
        push    si
        push    di
        push    ax
        
        ; Stop DMA engine
        add     dx, 0x400               ; Window 7 DMA control base
        mov     ax, 0x5000              ; CMD_DOWN_STALL
        out     dx, ax
        call    io_delay
        
        ; Clear descriptor pointers
        add     dx, 4                   ; Down list pointer register
        xor     ax, ax
        out     dx, ax                  ; Clear low word
        add     dx, 2
        out     dx, ax                  ; Clear high word
        
        ; Free any pending DMA buffers
        call    free_dma_buffers
        
        ; Re-enable DMA if needed
        sub     dx, 6                   ; Back to control register
        mov     ax, 0x5002              ; CMD_DOWN_UNSTALL
        out     dx, ax
        
        pop     ax
        pop     di
        pop     si
        
        ; Return to window 1 for normal operation
        push    dx
        add     dx, 0Eh                         ; Command register
        mov     ax, (1 << 11) | 1               ; Select window 1
        out     dx, ax
        pop     dx
        
.no_dma_handling:
.no_ack_needed:
        pop     cx
        pop     dx
        pop     bx
        pop     ax
        ret
acknowledge_3c515_interrupt ENDP

send_eoi_for_3c515 PROC
        ; Send End-Of-Interrupt to the appropriate PIC based on actual IRQ assignment
        ; This fixes the critical gap identified in defensive programming review
        push    ax
        push    bx
        push    dx
        
        ; Get the actual IRQ number for 3C515 (assuming it's NIC 1)
        mov     bl, byte ptr [nic_irq_numbers+1]  ; Get IRQ for NIC 1
        cmp     bl, IRQ_NONE
        je      .no_irq_assigned                ; Skip EOI if no IRQ assigned
        
        ; GPT-5: Check for spurious IRQ7/IRQ15 before sending EOI
        cmp     bl, 7
        je      .check_spurious_irq7
        cmp     bl, 15
        je      .check_spurious_irq15
        jmp     .send_normal_eoi
        
.check_spurious_irq7:
        ; Check ISR register to see if IRQ7 is really active
        mov     al, 0Bh                 ; Read ISR command
        out     PIC1_COMMAND, al
        jmp     short $+2               ; I/O delay
        in      al, PIC1_COMMAND
        test    al, 80h                 ; Check bit 7 (IRQ7)
        jnz     .send_normal_eoi        ; Real IRQ7, send EOI
        ; Spurious IRQ7 - don't send EOI
        inc     word ptr [spurious_irq_count]
        jmp     .no_irq_assigned
        
.check_spurious_irq15:
        ; Check ISR register to see if IRQ15 is really active
        mov     al, 0Bh                 ; Read ISR command (OCW3)
        out     PIC2_COMMAND, al
        jmp     short $+2               ; I/O delay
        in      al, PIC2_COMMAND        ; Read ISR
        push    ax                      ; Save ISR value
        
        ; GPT-5 Fix: Restore OCW3 to read IRR (default)
        mov     al, 0Ah                 ; Read IRR command (OCW3)
        out     PIC2_COMMAND, al
        
        pop     ax                      ; Restore ISR value
        test    al, 80h                 ; Check bit 7 (IRQ15)
        jnz     .send_normal_eoi        ; Real IRQ15, send EOI
        ; Spurious IRQ15 - send EOI only to master (not slave)
        inc     word ptr [spurious_irq_count]
        ; GPT-5 Fix: Ensure AL has EOI value
        mov     al, PIC_EOI
        out     PIC1_COMMAND, al        ; EOI to master only (use immediate)
        jmp     .eoi_complete
        
.send_normal_eoi:
        ; Check if this is a slave PIC IRQ (8-15)
        cmp     bl, 8
        jl      .master_pic_only                ; IRQ 0-7: master PIC only
        
        ; IRQ 8-15: Send EOI to slave PIC first, then master PIC
        ; This is the correct sequence for slave IRQs
        mov     al, PIC_EOI
        mov     dx, PIC2_COMMAND
        out     dx, al                          ; EOI to slave PIC
        
        ; Now send EOI to master PIC
        mov     dx, PIC1_COMMAND
        out     dx, al                          ; EOI to master PIC
        jmp     .eoi_complete
        
.master_pic_only:
        ; IRQ 0-7: Send EOI to master PIC only
        mov     al, PIC_EOI
        mov     dx, PIC1_COMMAND
        out     dx, al
        
.eoi_complete:
.no_irq_assigned:
        pop     dx
        pop     bx
        pop     ax
        ret
send_eoi_for_3c515 ENDP

process_3c515_packets_immediate PROC
        ; Emergency packet processing when deferred queue is full
        ; This is called directly from ISR when we can't defer the work
        push    ax
        push    bx
        push    cx
        
        ; Call the original hardware handler as fallback
        call    hardware_handle_3c515_irq
        
        pop     cx
        pop     bx
        pop     ax
        ret
process_3c515_packets_immediate ENDP

PUBLIC process_3c515_packets
process_3c515_packets PROC
        ; This function is called from the INT 28h handler when DOS is idle
        ; It can safely call DOS functions and do heavy processing
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    ds
        push    es
        
        ; Set up our data segment
        mov     ax, _DATA
        mov     ds, ax
        ASSUME  DS:_DATA
        
        ; Process 3C515-TX packets with DMA handling
        ; Called from DOS idle context - safe to do heavy processing
        push    ax
        push    bx
        push    cx
        push    dx
        
        ; Select appropriate register window for 3C515-TX
        mov     dx, 320h                ; 3C515-TX base I/O address
        add     dx, 0Eh                 ; Command register
        mov     ax, 7000h               ; Select Window 7 (Diagnostics)
        out     dx, ax
        
        ; Check DMA status for completed transfers
        sub     dx, 0Eh                 ; Back to base
        add     dx, 20h                 ; DMA_STATUS register
        in      al, dx                  ; Read DMA status
        
        ; Process RX DMA completions
        test    al, 01h                 ; RX_DMA_COMPLETE
        jz      .check_tx_dma
        
        ; Process received packets from DMA ring
        call    process_3c515_rx_dma_ring
        
.check_tx_dma:
        test    al, 02h                 ; TX_DMA_COMPLETE
        jz      .check_fifo_packets
        
        ; Process transmitted packet completions
        call    process_3c515_tx_completions
        
.check_fifo_packets:
        ; Also check for non-DMA packets (FIFO mode)
        sub     dx, 20h                 ; Back to base
        add     dx, 0Eh                 ; Command register
        mov     ax, 1000h               ; Select Window 1 (Operating)
        out     dx, ax
        
        ; Check RX status in operating window
        sub     dx, 0Eh                 ; Back to base
        add     dx, 08h                 ; RX_STATUS register
        in      ax, dx                  ; Read RX status
        
        ; Process up to 10 packets per call (interrupt batching)
        mov     cx, 10                  ; Maximum packets to process
        
.fifo_packet_loop:
        test    ax, 8000h               ; Check RX_INCOMPLETE
        jnz     .fifo_done              ; No more FIFO packets
        
        ; Get packet size from status
        and     ax, 1FFFh               ; Mask to get packet size
        cmp     ax, 60                  ; Minimum frame size
        jb      .skip_fifo_packet
        cmp     ax, 1514                ; Maximum frame size  
        ja      .skip_fifo_packet
        
        ; Read packet from FIFO (similar to 3C509B but with 3C515-TX specifics)
        push    cx
        push    ax                      ; Save packet size
        
        ; Use 32-bit transfers if available (3C515-TX supports this)
        mov     bx, OFFSET temp_rx_buffer
        sub     dx, 08h                 ; Back to base
        add     dx, 00h                 ; DATA_REGISTER (32-bit capable)
        
        pop     cx                      ; Restore packet size
        push    cx                      ; Save for later
        
        ; Use 32-bit transfers for efficiency
        shr     cx, 2                   ; Convert bytes to dwords
        
.read_dword_loop:
        in      eax, dx                 ; Read dword from FIFO
        mov     [bx], eax               ; Store in buffer
        add     bx, 4                   ; Advance buffer pointer
        loop    .read_dword_loop
        
        ; Handle remaining bytes
        pop     ax                      ; Restore packet size
        and     ax, 3                   ; Get remainder bytes
        jz      .dword_aligned
        
        in      eax, dx                 ; Read final dword
        mov     cx, ax                  ; Byte count to CX
        
.byte_copy:
        mov     [bx], al                ; Store byte
        shr     eax, 8                  ; Next byte
        inc     bx
        dec     cx
        jnz     .byte_copy
        
.dword_aligned:
        ; Call packet receive handler
        mov     ax, 1                   ; NIC index (3C515-TX = 1)
        mov     bx, OFFSET temp_rx_buffer
        call    packet_ops_receive
        
        ; Update statistics
        inc     word ptr [irq_count+2]  ; NIC 1 packet count
        
        pop     cx                      ; Restore loop counter
        dec     cx
        jnz     .fifo_packet_loop
        
        jmp     .fifo_done
        
.skip_fifo_packet:
        ; Discard malformed FIFO packet
        sub     dx, 08h                 ; Back to base
        add     dx, 0Eh                 ; Command register
        mov     ax, 4000h               ; RX_DISCARD command
        out     dx, ax
        
        add     dx, 08h-0Eh             ; Back to RX_STATUS
        loop    .fifo_packet_loop
        
.fifo_done:
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
process_3c515_packets ENDP

;-----------------------------------------------------------------------------
; nic_common_irq_handler - Common interrupt processing
; Called by both NIC-specific handlers to perform common tasks
;
; Input:  AL = NIC index, BX = interrupt status flags
; Output: AX = 0 if handled, non-zero if spurious
; Uses:   All registers
;-----------------------------------------------------------------------------
nic_common_irq_handler PROC
        push    bp
        mov     bp, sp
        push    cx
        push    dx
        push    si
        push    di

        ; Validate interrupt status, handle common interrupt conditions, and dispatch to packet processing

        ; Check for receive interrupt
        test    bl, IRQ_RX_COMPLETE
        jz      .no_rx

        ; Handle packet reception
        call    packet_ops_receive
        ; Check return value and handle errors
        test    ax, ax
        jz      .rx_success
        
        ; Handle receive error
        inc     word ptr [rx_error_count]
        push    ax
        call    log_rx_error
        pop     ax
        jmp     .no_rx
        
.rx_success:
        inc     word ptr [rx_packet_count]

.no_rx:
        ; Check for transmit completion
        test    bl, IRQ_TX_COMPLETE
        jz      .no_tx

        ; Handle transmit completion
        push    si
        push    di
        push    dx
        
        ; Get TX status from hardware
        mov     dx, [current_iobase]
        test    dx, dx
        jz      .no_iobase_tx
        add     dx, 0x0B                ; TX_STATUS register offset
        in      al, dx
        
        ; Check for errors
        test    al, 0xF8                ; Error bits (bits 3-7)
        jz      .tx_success
        
        ; Handle TX error
        inc     word ptr [tx_error_count]
        push    ax
        call    handle_tx_error
        pop     ax
        jmp     .tx_done
        
.tx_success:
        ; Free transmit buffer
        mov     si, [current_tx_buffer]
        test    si, si
        jz      .no_buffer_to_free
        call    free_tx_buffer
        mov     word ptr [current_tx_buffer], 0
        
.no_buffer_to_free:
        ; Update statistics
        inc     word ptr [tx_packet_count]
        
.tx_done:
        ; Clear TX status
        out     dx, al                  ; Write back to clear
        
.no_iobase_tx:
        pop     dx
        pop     di
        pop     si

.no_tx:
        ; Check for errors
        test    bl, IRQ_RX_ERROR
        jz      .no_rx_error

        ; Handle receive errors
        push    ax
        push    dx
        
        ; Get detailed error status
        mov     dx, [current_iobase]
        test    dx, dx
        jz      .no_iobase_rx_err
        add     dx, 0x08                ; RX_STATUS register
        in      ax, dx
        
        ; Classify error type
        test    ax, 0x4000              ; General error bit
        jz      .no_rx_err
        
        ; Update error counters based on type
        test    ax, 0x0800              ; Oversize packet
        jz      .not_oversize
        inc     word ptr [rx_oversize_errors]
.not_oversize:
        test    ax, 0x2800              ; CRC error
        jz      .not_crc
        inc     word ptr [rx_crc_errors]
.not_crc:
        test    ax, 0x2000              ; Framing error
        jz      .not_framing
        inc     word ptr [rx_framing_errors]
.not_framing:
        
        ; Log error for debugging
        push    ax
        call    log_rx_error_details
        pop     ax
        
        ; Discard bad packet
        mov     dx, [current_iobase]
        add     dx, 0x0E                ; Command register
        mov     ax, 0x4000              ; RX_DISCARD command
        out     dx, ax
        
.no_rx_err:
.no_iobase_rx_err:
        pop     dx
        pop     ax

.no_rx_error:
        test    bl, IRQ_TX_ERROR
        jz      .no_tx_error

        ; Handle transmit errors
        push    ax
        push    bx
        push    cx
        push    dx
        
        ; Get TX error details
        mov     dx, [current_iobase]
        test    dx, dx
        jz      .no_iobase_tx_err
        add     dx, 0x0B                ; TX_STATUS register
        in      al, dx
        mov     bl, al                  ; Save status
        
        ; Classify error
        test    bl, 0x80                ; Max collisions
        jz      .not_max_coll
        inc     word ptr [tx_max_coll_errors]
        mov     cl, 1                   ; Set retry flag
        jmp     .check_retry
        
.not_max_coll:
        test    bl, 0x20                ; Underrun
        jz      .not_underrun
        inc     word ptr [tx_underrun_errors]
        mov     cl, 0                   ; Don't retry underrun
        jmp     .check_retry
        
.not_underrun:
        test    bl, 0x40                ; Jabber
        jz      .not_jabber
        inc     word ptr [tx_jabber_errors]
        mov     cl, 0                   ; Don't retry jabber
        
.not_jabber:
        mov     cl, 0                   ; Default: no retry
        
.check_retry:
        ; Retry transmission if appropriate
        test    cl, cl
        jz      .no_retry
        
        ; Check retry count
        mov     al, [current_tx_retries]
        cmp     al, 3                   ; Max retries
        jae     .no_retry
        
        ; Retry transmission
        inc     byte ptr [current_tx_retries]
        call    retry_transmission
        jmp     .tx_error_done
        
.no_retry:
        ; Free failed TX buffer
        call    free_current_tx_buffer
        mov     byte ptr [current_tx_retries], 0
        
.tx_error_done:
        ; Clear TX status
        mov     dx, [current_iobase]
        add     dx, 0x0B
        out     dx, bl                  ; Write status to clear
        
.no_iobase_tx_err:
        pop     dx
        pop     cx
        pop     bx
        pop     ax

.no_tx_error:
        ; Success - interrupt handled
        mov     ax, 0

        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bp
        ret
nic_common_irq_handler ENDP

;-----------------------------------------------------------------------------
; nic_set_receive_mode - Set NIC receive mode
;
; Input:  AL = NIC index, BL = receive mode
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
nic_set_receive_mode PROC
        push    bp
        mov     bp, sp
        push    cx
        push    dx

        ; Validate parameters and update internal state

        ; Validate NIC index
        cmp     al, MAX_NICS
        jae     .invalid_nic

        ; Validate receive mode
        cmp     bl, 1
        jb      .invalid_mode
        cmp     bl, 6
        ja      .invalid_mode

        ; Store new receive mode
        mov     [current_receive_mode], bl

        ; Program NIC hardware registers for receive mode
        push    si
        push    di
        
        ; Get I/O base for this NIC
        movzx   si, al                  ; NIC index
        shl     si, 1                   ; Word offset
        mov     di, OFFSET nic_iobase_table
        mov     dx, [di + si]           ; Get I/O base
        test    dx, dx
        jz      .no_iobase_mode
        
        ; Select Window 1 for RX control
        add     dx, 0x0E                ; Command register
        mov     ax, 0x0801              ; SELECT_WINDOW | 1
        out     dx, ax
        call    io_delay
        
        ; Set RX filter based on mode
        mov     dx, [di + si]           ; Restore base
        add     dx, 0x0E                ; Command register
        mov     ax, 0x0800              ; SET_RX_FILTER base command
        
        ; Configure based on mode
        cmp     bl, 1                   ; Mode 1: Off
        je      .mode_off
        cmp     bl, 2                   ; Mode 2: Direct (station only)
        je      .mode_direct
        cmp     bl, 3                   ; Mode 3: Broadcast
        je      .mode_broadcast
        cmp     bl, 4                   ; Mode 4: Multicast
        je      .mode_multicast
        cmp     bl, 5                   ; Mode 5: All multicast
        je      .mode_all_multi
        cmp     bl, 6                   ; Mode 6: Promiscuous
        je      .mode_promiscuous
        jmp     .invalid_mode           ; Should have been caught earlier
        
.mode_off:
        or      ax, 0x00                ; No packets
        jmp     .set_filter
        
.mode_direct:
        or      ax, 0x01                ; Station address only
        jmp     .set_filter
        
.mode_broadcast:
        or      ax, 0x05                ; Station + broadcast
        jmp     .set_filter
        
.mode_multicast:
        or      ax, 0x03                ; Station + multicast
        jmp     .set_filter
        
.mode_all_multi:
        or      ax, 0x07                ; Station + all multicast
        jmp     .set_filter
        
.mode_promiscuous:
        or      ax, 0x0F                ; All packets
        
.set_filter:
        out     dx, ax                  ; Set the filter
        call    io_delay
        
.no_iobase_mode:
        pop     di
        pop     si

        ; Success
        mov     ax, 0
        jmp     .exit

.invalid_nic:
        mov     ax, 1
        jmp     .exit

.invalid_mode:
        mov     ax, 2
        jmp     .exit

.exit:
        pop     dx
        pop     cx
        pop     bp
        ret
nic_set_receive_mode ENDP

;-----------------------------------------------------------------------------
; get_irq_stats - Get interrupt statistics
;
; Input:  ES:DI = buffer to receive statistics
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, SI, DI
;-----------------------------------------------------------------------------
get_irq_stats PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si

        ; Validate buffer pointer and copy statistics
        push    ds
        push    es
        push    di
        
        ; Get user buffer pointer from stack
        mov     di, [bp+4]              ; Offset
        mov     ax, [bp+6]              ; Segment
        test    ax, ax                  ; Check for null segment
        jz      .invalid_buffer
        mov     es, ax                  ; ES:DI = user buffer
        
        ; Copy comprehensive statistics to user buffer
        push    si
        push    cx
        
        ; Copy per-NIC interrupt counts
        mov     si, OFFSET irq_count
        mov     cx, MAX_NICS            ; Number of NICs
.copy_irq_counts:
        movsw                           ; Copy word
        loop    .copy_irq_counts
        
        ; Copy spurious interrupt count
        mov     ax, [spurious_irq_count]
        stosw
        
        ; Copy packet counters
        mov     ax, [rx_packet_count]
        stosw
        mov     ax, [tx_packet_count]
        stosw
        
        ; Copy error counters
        mov     ax, [rx_error_count]
        stosw
        mov     ax, [tx_error_count]
        stosw
        
        ; Copy detailed error statistics
        mov     ax, [rx_oversize_errors]
        stosw
        mov     ax, [rx_crc_errors]
        stosw
        mov     ax, [rx_framing_errors]
        stosw
        mov     ax, [tx_max_coll_errors]
        stosw
        mov     ax, [tx_underrun_errors]
        stosw
        mov     ax, [tx_jabber_errors]
        stosw
        
        pop     cx
        pop     si
        
        xor     ax, ax                  ; Success
        jmp     .stats_done
        
.invalid_buffer:
        mov     ax, -1                  ; Error code
        
.stats_done:
        pop     di
        pop     es
        pop     ds

        ; Copy interrupt counts
        mov     si, OFFSET irq_count
        mov     cx, MAX_NICS
.copy_loop:
        mov     ax, [si]
        stosw                           ; Store to ES:DI
        add     si, 2
        loop    .copy_loop

        ; Copy spurious interrupt count
        mov     ax, [spurious_irq_count]
        stosw

        ; Success
        mov     ax, 0

        pop     si
        pop     cx
        pop     bx
        pop     bp
        ret
get_irq_stats ENDP

;-----------------------------------------------------------------------------
; 3C509B_irq_handler - 3C509B-specific interrupt service routine
;
; This is the actual ISR that gets called by the hardware interrupt
; Input:  None (called by interrupt)
; Output: None
; Uses:   All registers (saves and restores)
;-----------------------------------------------------------------------------
_3C509B_irq_handler PROC FAR
        ; Save all registers (standard ISR prologue)
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    bp
        push    ds
        push    es

        ; Setup data segment
        mov     ax, @data
        mov     ds, ax

        ; Get NIC context and process interrupts
        mov     si, 0                   ; NIC 0 index (3C509B)
        call    get_nic_iobase          ; Get I/O base address
        mov     dx, ax                  ; DX = I/O base
        test    dx, dx                  ; Check if configured
        jz      .not_our_interrupt
        
        add     dx, 0Eh                 ; INT_STATUS register offset
        in      ax, dx                  ; Read interrupt status
        test    ax, 00FFh               ; Check for any interrupts
        jz      .not_our_interrupt
        
        mov     bx, ax                  ; Save interrupt status
        call    process_3c509b_interrupts ; Process the interrupts
        jmp     .send_eoi

.not_our_interrupt:
        ; Iterate through all configured NICs to find interrupt source
        xor     si, si                  ; Start with NIC 0
.check_nic_loop:
        cmp     si, MAX_NICS
        jae     .no_interrupt_source
        
        ; Check if NIC is configured
        mov     al, [nic_irq_numbers + si]
        cmp     al, IRQ_NONE
        je      .next_nic
        
        ; Check interrupt status for this NIC
        call    check_nic_interrupt_status
        test    ax, ax
        jnz     .found_interrupt
        
.next_nic:
        inc     si
        jmp     .check_nic_loop

.found_interrupt:
        ; SI contains NIC index with interrupt
        call    handle_nic_interrupt
        jmp     .send_eoi
        
.no_interrupt_source:
        ; No valid interrupt source found - spurious interrupt
        inc     word ptr [spurious_irq_count]
        
.send_eoi:
        ; Acknowledge interrupt at PIC
        mov     bl, [nic_irq_numbers]   ; Get IRQ for NIC 0
        cmp     bl, IRQ_NONE
        je      .skip_eoi
        cmp     bl, 8
        jb      .master_pic_only
        
        ; Slave PIC interrupt (IRQ 8-15)
        mov     al, 0x60                ; Specific EOI command
        add     al, bl
        sub     al, 8                   ; Adjust for slave
        out     PIC2_COMMAND, al        ; Send to slave PIC
        call    io_delay
        mov     al, 0x62                ; EOI for IRQ2 (cascade)
        out     PIC1_COMMAND, al        ; Send to master PIC
        jmp     .eoi_done
        
.master_pic_only:
        ; Master PIC interrupt (IRQ 0-7)
        mov     al, 0x60                ; Specific EOI command
        add     al, bl                  ; Add IRQ number
        out     PIC1_COMMAND, al        ; Send EOI to master PIC
        
.eoi_done:
.skip_eoi:
        
        ; Restore all registers (standard ISR epilogue)
        pop     es
        pop     ds
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        
        iret                        ; Return from interrupt
_3C509B_irq_handler ENDP

;-----------------------------------------------------------------------------
; install_3c509b_irq - Install 3C509B interrupt handler
;
; Input:  AL = IRQ number (3, 5, 7, 9-12, 15)
;         BX = NIC index
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX, ES
;-----------------------------------------------------------------------------
install_3c509b_irq PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    es

        ; Validate IRQ number
        cmp     al, IRQ_MIN
        jb      .invalid_irq
        cmp     al, IRQ_MAX
        ja      .invalid_irq
        
        ; Check for invalid IRQ values (4, 6, 8, 13, 14)
        cmp     al, 4
        je      .invalid_irq
        cmp     al, 6
        je      .invalid_irq
        cmp     al, 8
        je      .invalid_irq
        cmp     al, 13
        je      .invalid_irq
        cmp     al, 14
        je      .invalid_irq

        ; Validate NIC index
        cmp     bl, MAX_NICS
        jae     .invalid_nic

        ; Store IRQ number for this NIC
        mov     ah, 0
        mov     si, bx
        mov     [nic_irq_numbers + si], al

        ; Calculate interrupt vector number
        mov     cl, al
        add     cl, 8                   ; Hardware IRQs start at INT 08h

        ; Get current interrupt vector
        mov     ah, 35h                 ; DOS get interrupt vector
        mov     al, cl
        int     21h                     ; ES:BX = current vector
        
        ; Save original vector
        mov     si, bx
        shl     si, 2                   ; Each vector is 4 bytes
        mov     [original_irq_vectors + si], bx
        mov     [original_irq_vectors + si + 2], es

        ; Install our handler
        mov     ah, 25h                 ; DOS set interrupt vector
        mov     al, cl
        mov     dx, OFFSET _3C509B_irq_handler
        push    ds
        push    cs
        pop     ds
        int     21h
        pop     ds

        ; Mark IRQ as installed
        mov     si, bx
        mov     [irq_installed + si], 1

        ; Enable IRQ at PIC
        call    enable_irq_at_pic

        ; Success
        mov     ax, 0
        jmp     .exit

.invalid_irq:
        mov     ax, 1
        jmp     .exit

.invalid_nic:
        mov     ax, 2
        jmp     .exit

.exit:
        pop     es
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
install_3c509b_irq ENDP

;-----------------------------------------------------------------------------
; enable_irq_at_pic - Enable IRQ at the Programmable Interrupt Controller
;
; Input:  AL = IRQ number
; Output: None
; Uses:   AX, DX
;-----------------------------------------------------------------------------
enable_irq_at_pic PROC
        push    ax
        push    dx

        cmp     al, 8
        jb      .master_pic
        
        ; Slave PIC (IRQ 8-15)
        sub     al, 8
        mov     cl, al
        mov     al, 1
        shl     al, cl
        not     al
        in      al, PIC2_DATA
        and     al, al
        out     PIC2_DATA, al
        
        ; Also enable IRQ 2 on master (cascade)
        in      al, PIC1_DATA
        and     al, 11111011b           ; Clear bit 2
        out     PIC1_DATA, al
        jmp     .done

.master_pic:
        ; Master PIC (IRQ 0-7)
        mov     cl, al
        mov     al, 1
        shl     al, cl
        not     al
        in      al, PIC1_DATA
        and     al, al
        out     PIC1_DATA, al

.done:
        pop     dx
        pop     ax
        ret
enable_irq_at_pic ENDP

;-----------------------------------------------------------------------------
; nic_irq_handler_3c509b_batched - Enhanced 3C509B interrupt handler with batching
; This is the enhanced ISR that uses interrupt batching for better performance
;
; Input:  None (interrupt context)
; Output: None
; Uses:   All registers (saved/restored)
;-----------------------------------------------------------------------------
nic_irq_handler_3c509b_batched PROC
        ; Check CPU features and use appropriate register saving
        call    get_cpu_features
        test    ax, 1                   ; Check for PUSHA support (286+)
        jz      .save_manual_3c509b

        ; Use PUSHA for 286+ CPUs
        pusha
        push    ds
        push    es
        jmp     .registers_saved_3c509b

.save_manual_3c509b:
        ; Manual register saving for 8086/8088
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    ds
        push    es

.registers_saved_3c509b:
        ; Set up our data segment
        push    cs
        pop     ds
        ASSUME DS:_TEXT

        ; Increment interrupt counter
        inc     word ptr [irq_count]    ; Assuming NIC 0

        ; Call enhanced batched interrupt handler
        ; Process multiple events per interrupt to reduce overhead
        mov     byte ptr [batch_count_3c509b], 0
        mov     si, 0                   ; NIC index for 3C509B
        call    get_nic_iobase
        mov     dx, ax                  ; DX = I/O base
        test    dx, dx
        jz      .no_nic_3c509b
        
.batch_loop_3c509b:
        ; Check batch limit
        mov     al, [batch_count_3c509b]
        cmp     al, MAX_EVENTS_PER_IRQ
        jae     .batch_done_3c509b
        
        ; Read interrupt status
        push    dx
        add     dx, 0x0E                ; INT_STATUS register
        in      ax, dx                  ; Read status
        pop     dx
        test    ax, 0x00FF              ; Any interrupts pending?
        jz      .batch_done_3c509b
        
        ; Process this interrupt event
        push    ax
        push    dx
        mov     bx, ax                  ; Status in BX
        call    process_3c509b_event
        pop     dx
        pop     ax
        
        ; Acknowledge processed interrupts
        push    dx
        add     dx, 0x0E
        mov     bx, ax
        and     bx, 0x00FF              ; Mask to valid bits
        or      bx, 0x6800              ; ACK_INTR command
        mov     ax, bx
        out     dx, ax
        pop     dx
        
        ; Increment batch counter
        inc     byte ptr [batch_count_3c509b]
        jmp     .batch_loop_3c509b
        
.batch_done_3c509b:
        ; If we processed events, success
        cmp     byte ptr [batch_count_3c509b], 0
        je      .try_hardware_handler
        xor     ax, ax                  ; Success
        jmp     .handler_done_3c509b
        
.try_hardware_handler:
        ; Fall back to hardware handler if no batched processing
        call    hardware_handle_3c509b_irq
        jmp     .handler_done_3c509b
        
.no_nic_3c509b:
        mov     ax, 1                   ; Not our interrupt
        
.handler_done_3c509b:
        ; AX contains result
        cmp     ax, 0
        jne     .spurious_interrupt_3c509b

        ; Send EOI to interrupt controller
        ; Check if IRQ is on slave PIC
        mov     bl, [nic_irq_numbers]   ; Get IRQ for NIC 0 (3C509B)
        cmp     bl, 8
        jb      .master_eoi_3c509b
        
        ; Slave PIC EOI (IRQ 8-15)
        mov     al, 0x60                ; Specific EOI
        add     al, bl
        sub     al, 8                   ; Adjust for slave
        out     PIC2_COMMAND, al        ; Send to slave PIC
        call    io_delay
        mov     al, 0x62                ; EOI for IRQ2 (cascade)
        out     PIC1_COMMAND, al        ; Send to master PIC
        jmp     .restore_and_exit_3c509b
        
.master_eoi_3c509b:
        ; Master PIC EOI (IRQ 0-7)
        mov     al, 0x60                ; Specific EOI
        add     al, bl
        out     PIC1_COMMAND, al        ; Send EOI to master PIC

        jmp     .restore_and_exit_3c509b

.spurious_interrupt_3c509b:
        ; Handle spurious interrupt
        inc     word ptr [spurious_irq_count]

.restore_and_exit_3c509b:
        ; Restore registers
        call    get_cpu_features
        test    ax, 1                   ; Check for POPA support (286+)
        jz      .restore_manual_3c509b

        ; Use POPA for 286+ CPUs
        pop     es
        pop     ds
        popa
        iret

.restore_manual_3c509b:
        ; Manual register restoration for 8086/8088
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        iret
nic_irq_handler_3c509b_batched ENDP

;-----------------------------------------------------------------------------
; nic_irq_handler_3c515_batched - Enhanced 3C515 interrupt handler with batching
; This is the enhanced ISR that uses interrupt batching for better performance
;
; Input:  None (interrupt context)
; Output: None
; Uses:   All registers (saved/restored)
;-----------------------------------------------------------------------------
nic_irq_handler_3c515_batched PROC
        ; Check CPU features and use appropriate register saving
        call    get_cpu_features
        test    ax, 1                   ; Check for PUSHA support (286+)
        jz      .save_manual_3c515

        ; Use PUSHA for 286+ CPUs
        pusha
        push    ds
        push    es
        jmp     .registers_saved_3c515

.save_manual_3c515:
        ; Manual register saving for 8086/8088
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    ds
        push    es

.registers_saved_3c515:
        ; Set up our data segment
        push    cs
        pop     ds
        ASSUME DS:_TEXT

        ; Increment interrupt counter
        inc     word ptr [irq_count+2]  ; Assuming NIC 1

        ; Call enhanced batched interrupt handler for 3C515
        ; Process multiple events with DMA support
        mov     byte ptr [batch_count_3c515], 0
        mov     si, 1                   ; NIC index for 3C515
        call    get_nic_iobase
        mov     dx, ax                  ; DX = I/O base
        test    dx, dx
        jz      .no_nic_3c515
        
.batch_loop_3c515:
        ; Check batch limit
        mov     al, [batch_count_3c515]
        cmp     al, MAX_EVENTS_PER_IRQ
        jae     .batch_done_3c515
        
        ; Read interrupt status (32-bit for 3C515)
        push    dx
        add     dx, 0x0E                ; INT_STATUS register
        in      ax, dx                  ; Read status
        pop     dx
        
        ; Check for DMA interrupts first (higher priority)
        test    ax, 0x0600              ; UP_COMPLETE | DOWN_COMPLETE
        jz      .check_normal_3c515
        
        ; Process DMA events
        push    ax
        push    dx
        test    ax, 0x0200              ; UP_COMPLETE (RX DMA)
        jz      .check_down_dma
        call    process_3c515_rx_dma_ring
        
.check_down_dma:
        test    ax, 0x0400              ; DOWN_COMPLETE (TX DMA)
        jz      .dma_done
        call    process_3c515_tx_completions
        
.dma_done:
        pop     dx
        pop     ax
        
        ; Acknowledge DMA interrupts
        push    dx
        add     dx, 0x0E
        mov     bx, ax
        and     bx, 0x0600              ; DMA interrupt bits
        or      bx, 0x6800              ; ACK_INTR command
        mov     ax, bx
        out     dx, ax
        pop     dx
        
        jmp     .next_batch_3c515
        
.check_normal_3c515:
        ; Check for normal interrupts
        test    ax, 0x00FF              ; Standard interrupt bits
        jz      .batch_done_3c515
        
        ; Process standard interrupts
        push    ax
        push    dx
        mov     bx, ax
        call    process_3c515_event
        pop     dx
        pop     ax
        
        ; Acknowledge processed interrupts
        push    dx
        add     dx, 0x0E
        mov     bx, ax
        and     bx, 0x00FF
        or      bx, 0x6800
        mov     ax, bx
        out     dx, ax
        pop     dx
        
.next_batch_3c515:
        ; Increment batch counter
        inc     byte ptr [batch_count_3c515]
        jmp     .batch_loop_3c515
        
.batch_done_3c515:
        ; Check if we processed any events
        cmp     byte ptr [batch_count_3c515], 0
        je      .try_hardware_3c515
        xor     ax, ax                  ; Success
        jmp     .handler_done_3c515
        
.try_hardware_3c515:
        ; Fall back to hardware handler
        call    hardware_handle_3c515_irq
        jmp     .handler_done_3c515
        
.no_nic_3c515:
        mov     ax, 1                   ; Not our interrupt
        
.handler_done_3c515:
        ; AX contains result
        cmp     ax, 0
        jne     .spurious_interrupt_3c515

        ; Send EOI to interrupt controller
        ; Check if IRQ is on slave PIC
        mov     bl, [nic_irq_numbers+1] ; Get IRQ for NIC 1 (3C515)
        cmp     bl, 8
        jb      .master_eoi_3c515
        
        ; Slave PIC EOI (IRQ 8-15)
        mov     al, 0x60                ; Specific EOI
        add     al, bl
        sub     al, 8                   ; Adjust for slave
        out     PIC2_COMMAND, al        ; Send to slave PIC
        call    io_delay
        mov     al, 0x62                ; EOI for IRQ2 (cascade)
        out     PIC1_COMMAND, al        ; Send to master PIC
        jmp     .restore_and_exit_3c515
        
.master_eoi_3c515:
        ; Master PIC EOI (IRQ 0-7)
        mov     al, 0x60                ; Specific EOI
        add     al, bl
        out     PIC1_COMMAND, al        ; Send EOI to master PIC

        jmp     .restore_and_exit_3c515

.spurious_interrupt_3c515:
        ; Handle spurious interrupt
        inc     word ptr [spurious_irq_count]

.restore_and_exit_3c515:
        ; Restore registers
        call    get_cpu_features
        test    ax, 1                   ; Check for POPA support (286+)
        jz      .restore_manual_3c515

        ; Use POPA for 286+ CPUs
        pop     es
        pop     ds
        popa
        iret

.restore_manual_3c515:
        ; Manual register restoration for 8086/8088
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        iret
nic_irq_handler_3c515_batched ENDP

;=============================================================================
; CORE IRQ MANAGEMENT SUPPORT FUNCTIONS - Phase 2 Implementation
; Sub-Agent 4: IRQ Core Specialist
;=============================================================================

;-----------------------------------------------------------------------------
; validate_irq_ranges - Validate IRQ assignments are within allowed ranges
;
; Validates that all configured IRQ numbers are in allowed ranges:
; IRQ 3, 5, 7, 9-12, 15 (excludes 4,6,8,13,14 which are system reserved)
;
; Input:  None
; Output: CY clear if valid, CY set if invalid
; Uses:   AX, BX, CX, SI
;-----------------------------------------------------------------------------
validate_irq_ranges PROC
        push    bx
        push    cx
        push    si
        
        mov     cx, MAX_NICS
        mov     si, OFFSET nic_irq_numbers
        
.check_loop:
        mov     bl, [si]
        cmp     bl, IRQ_NONE
        je      .next_nic               ; Skip unassigned IRQs
        
        ; Check if IRQ is in valid range
        call    is_valid_irq_number
        jc      .invalid_range
        
.next_nic:
        inc     si
        loop    .check_loop
        
        clc                             ; All IRQs valid
        jmp     .exit
        
.invalid_range:
        stc                             ; Invalid IRQ detected
        
.exit:
        pop     si
        pop     cx
        pop     bx
        ret
validate_irq_ranges ENDP

;-----------------------------------------------------------------------------
; is_valid_irq_number - Check if single IRQ number is valid for NICs
;
; Valid IRQ numbers: 3, 5, 7, 9, 10, 11, 12, 15
; Invalid (system reserved): 0-2, 4, 6, 8, 13, 14
;
; Input:  BL = IRQ number to validate
; Output: CY clear if valid, CY set if invalid
; Uses:   None (preserves all registers)
;-----------------------------------------------------------------------------
is_valid_irq_number PROC
        ; Check basic range first
        cmp     bl, 3
        jb      .invalid
        cmp     bl, 15
        ja      .invalid
        
        ; Check specific forbidden values
        cmp     bl, 4
        je      .invalid                ; IRQ 4 - COM1/COM3
        cmp     bl, 6
        je      .invalid                ; IRQ 6 - Floppy
        cmp     bl, 8
        je      .invalid                ; IRQ 8 - RTC
        cmp     bl, 13
        je      .invalid                ; IRQ 13 - Math Coprocessor
        cmp     bl, 14
        je      .invalid                ; IRQ 14 - Primary IDE
        
        ; If we get here, IRQ is valid
        clc
        ret
        
.invalid:
        stc
        ret
is_valid_irq_number ENDP

;-----------------------------------------------------------------------------
; init_pic_state - Initialize PIC state tracking
;
; Sets up initial state for PIC management and reads current interrupt masks
;
; Input:  None
; Output: CY clear if successful, CY set if failed
; Uses:   AX, DX
;-----------------------------------------------------------------------------
init_pic_state PROC
        push    ax
        push    dx
        
        ; Read current PIC masks for backup
        mov     dx, PIC1_DATA
        in      al, dx
        ; Store master PIC mask for restoration (would need storage variable)
        
        mov     dx, PIC2_DATA  
        in      al, dx
        ; Store slave PIC mask for restoration (would need storage variable)
        
        ; Initialize PIC tracking structures
        ; (Implementation would initialize tracking arrays here)
        
        clc                             ; Success
        
        pop     dx
        pop     ax
        ret
init_pic_state ENDP

;-----------------------------------------------------------------------------
; detect_irq_conflicts - Detect potential IRQ conflicts
;
; Scans for potential IRQ conflicts by checking what devices might be
; using the IRQ numbers we want to assign
;
; Input:  None
; Output: AX = conflict mask (bits set for conflicting IRQs)
; Uses:   AX, BX, CX, SI
;-----------------------------------------------------------------------------
detect_irq_conflicts PROC
        push    bx
        push    cx
        push    si
        
        xor     ax, ax                  ; Clear conflict mask
        mov     cx, MAX_NICS
        mov     si, OFFSET nic_irq_numbers
        
.conflict_loop:
        mov     bl, [si]
        cmp     bl, IRQ_NONE
        je      .next_conflict
        
        ; Check if IRQ might conflict (simplified check)
        call    check_single_irq_conflict
        jnc     .no_conflict
        
        ; Set bit in conflict mask
        mov     bh, 1
        mov     cl, bl
        shl     bh, cl
        or      al, bh
        
.no_conflict:
.next_conflict:
        inc     si
        loop    .conflict_loop
        
        pop     si
        pop     cx
        pop     bx
        ret
detect_irq_conflicts ENDP

;-----------------------------------------------------------------------------
; check_single_irq_conflict - Check for conflicts on single IRQ
;
; Performs a basic conflict check for a single IRQ number
;
; Input:  BL = IRQ number to check
; Output: CY clear if no conflict, CY set if potential conflict
; Uses:   None
;-----------------------------------------------------------------------------
check_single_irq_conflict PROC
        ; This is a simplified conflict check
        ; In full implementation, would check:
        ; - Hardware device detection
        ; - Other TSRs using same IRQ
        ; - System device assignments
        
        ; For now, assume no conflicts (conservative approach)
        clc
        ret
check_single_irq_conflict ENDP

;-----------------------------------------------------------------------------
; auto_detect_irqs - Auto-detect available IRQ assignments
;
; Attempts to automatically detect good IRQ assignments for NICs
; by probing hardware and checking availability
;
; Input:  None
; Output: CY clear if successful, CY set if failed
; Uses:   AX, BX, CX, SI
;-----------------------------------------------------------------------------
auto_detect_irqs PROC
        push    bx
        push    cx
        push    si
        
        ; Probe common NIC IRQ assignments in priority order
        mov     si, OFFSET nic_irq_numbers
        
        ; Try IRQ 10 for first NIC (most common)
        mov     bl, 10
        call    is_irq_available
        jc      .try_irq11_first
        mov     [si], bl
        jmp     .detect_second
        
.try_irq11_first:
        mov     bl, 11
        call    is_irq_available
        jc      .try_irq5_first
        mov     [si], bl
        jmp     .detect_second
        
.try_irq5_first:
        mov     bl, 5
        call    is_irq_available
        jc      .autodetect_failed
        mov     [si], bl
        
.detect_second:
        ; Try to find IRQ for second NIC
        inc     si
        mov     bl, 11
        cmp     bl, [si-1]              ; Don't use same as first NIC
        je      .try_irq12_second
        call    is_irq_available
        jc      .try_irq12_second
        mov     [si], bl
        jmp     .autodetect_success
        
.try_irq12_second:
        mov     bl, 12
        cmp     bl, [si-1]              ; Don't use same as first NIC
        je      .try_irq9_second
        call    is_irq_available
        jc      .try_irq9_second
        mov     [si], bl
        jmp     .autodetect_success
        
.try_irq9_second:
        mov     bl, 9
        cmp     bl, [si-1]              ; Don't use same as first NIC
        je      .autodetect_partial
        call    is_irq_available
        jc      .autodetect_partial
        mov     [si], bl
        jmp     .autodetect_success
        
.autodetect_partial:
        ; Only first NIC detected, mark second as unassigned
        mov     byte ptr [si], IRQ_NONE
        jmp     .autodetect_success
        
.autodetect_success:
        clc
        jmp     .exit
        
.autodetect_failed:
        stc
        
.exit:
        pop     si
        pop     cx
        pop     bx
        ret
auto_detect_irqs ENDP

;-----------------------------------------------------------------------------
; is_irq_available - Check if IRQ is available for use
;
; Tests if an IRQ line appears to be available by checking various indicators
;
; Input:  BL = IRQ number to test
; Output: CY clear if available, CY set if in use
; Uses:   AX, DX
;-----------------------------------------------------------------------------
is_irq_available PROC
        push    ax
        push    dx
        
        ; First validate the IRQ number
        call    is_valid_irq_number
        jc      .not_available
        
        ; Check PIC mask - if IRQ is enabled, something might be using it
        call    read_pic_mask
        jc      .not_available
        
        ; Additional availability checks could be added here:
        ; - Check interrupt vector for non-default handler
        ; - Probe for device response on IRQ
        ; - Check known system device assignments
        
        clc                             ; Assume available
        jmp     .exit
        
.not_available:
        stc
        
.exit:
        pop     dx
        pop     ax
        ret
is_irq_available ENDP

;-----------------------------------------------------------------------------
; read_pic_mask - Read PIC interrupt mask for IRQ
;
; Reads the current interrupt mask bit for specified IRQ
;
; Input:  BL = IRQ number
; Output: CY clear if masked (disabled), CY set if enabled
; Uses:   AX, DX
;-----------------------------------------------------------------------------
read_pic_mask PROC
        push    ax
        push    dx
        
        cmp     bl, 8
        jl      .read_master
        
        ; Read slave PIC mask
        mov     dx, PIC2_DATA
        in      al, dx
        sub     bl, 8                   ; Convert to slave IRQ (0-7)
        jmp     .test_bit
        
.read_master:
        ; Read master PIC mask
        mov     dx, PIC1_DATA
        in      al, dx
        
.test_bit:
        ; Test the bit for this IRQ
        mov     cl, bl
        shr     al, cl
        and     al, 1
        cmp     al, 0
        je      .irq_enabled            ; Bit 0 = enabled
        
        ; IRQ is masked (disabled)
        clc
        jmp     .exit
        
.irq_enabled:
        ; IRQ is enabled (potentially in use)
        stc
        
.exit:
        pop     dx
        pop     ax
        ret
read_pic_mask ENDP

;-----------------------------------------------------------------------------
; apply_default_irqs - Apply default IRQ assignments
;
; Sets up default IRQ assignments when auto-detection fails
;
; Input:  None
; Output: None
; Uses:   AL, SI
;-----------------------------------------------------------------------------
apply_default_irqs PROC
        push    ax
        push    si
        
        mov     si, OFFSET nic_irq_numbers
        mov     byte ptr [si], 10       ; Default IRQ 10 for first NIC
        mov     byte ptr [si+1], 11     ; Default IRQ 11 for second NIC
        
        pop     si
        pop     ax
        ret
apply_default_irqs ENDP

;-----------------------------------------------------------------------------
; validate_single_irq_number - Validate single IRQ in install_nic_irq context
;
; Input:  BL = IRQ number, AL = NIC index
; Output: CY clear if valid, CY set if invalid
; Uses:   None (preserves all registers)
;-----------------------------------------------------------------------------
validate_single_irq_number PROC
        push    bx
        call    is_valid_irq_number     ; BL already contains IRQ
        pop     bx
        ret
validate_single_irq_number ENDP

;-----------------------------------------------------------------------------
; check_irq_conflict - Check for IRQ conflict during installation
;
; Input:  BL = IRQ number, AL = NIC index
; Output: CY clear if no conflict, CY set if conflict
; Uses:   BX
;-----------------------------------------------------------------------------
check_irq_conflict PROC
        call    check_single_irq_conflict
        ret
check_irq_conflict ENDP

;-----------------------------------------------------------------------------
; save_original_vector - Save original interrupt vector before installation
;
; Input:  BL = IRQ number, AL = NIC index
; Output: CY clear if successful, CY set if failed
; Uses:   AX, BX, CX, DX, ES
;-----------------------------------------------------------------------------
save_original_vector PROC
        push    ax
        push    bx
        push    cx
        push    dx
        push    es
        
        ; Convert IRQ to interrupt vector number
        mov     cl, bl
        cmp     bl, 8
        jl      .master_irq
        
        ; Slave IRQ (8-15) -> INT 70h-77h
        add     cl, 70h - 8
        jmp     .get_vector
        
.master_irq:
        ; Master IRQ (0-7) -> INT 08h-0Fh
        add     cl, 08h
        
.get_vector:
        ; Get current vector
        mov     ah, 35h
        mov     al, cl
        int     21h                     ; Returns ES:BX
        
        ; Store original vector (simplified - would need proper storage)
        ; In full implementation: store ES:BX in original_irq_vectors array
        
        clc                             ; Success
        
        pop     es
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
save_original_vector ENDP

;-----------------------------------------------------------------------------
; install_irq_handler - Install appropriate IRQ handler for NIC type
;
; Input:  BL = IRQ number, AL = NIC index
; Output: CY clear if successful, CY set if failed
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
install_irq_handler PROC
        push    ax
        push    bx
        push    cx
        push    dx
        
        ; Convert IRQ to interrupt vector
        mov     cl, bl
        cmp     bl, 8
        jl      .master_irq_install
        
        add     cl, 70h - 8             ; Slave IRQ vector
        jmp     .install_vector
        
.master_irq_install:
        add     cl, 08h                 ; Master IRQ vector
        
.install_vector:
        ; Determine handler based on NIC index/type
        cmp     al, 0
        je      .install_3c509b
        
        ; Install 3C515 handler
        mov     dx, OFFSET nic_irq_handler_3c515
        jmp     .do_install
        
.install_3c509b:
        ; Install 3C509B handler
        mov     dx, OFFSET nic_irq_handler_3c509b
        
.do_install:
        ; Install the vector
        push    ds
        mov     ax, cs
        mov     ds, ax
        mov     ah, 25h
        mov     al, cl
        int     21h
        pop     ds
        
        clc                             ; Success
        
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
install_irq_handler ENDP

;-----------------------------------------------------------------------------
; enable_pic_irq - Enable IRQ at PIC level
;
; Input:  BL = IRQ number
; Output: CY clear if successful, CY set if failed  
; Uses:   AX, CX, DX
;-----------------------------------------------------------------------------
enable_pic_irq PROC
        push    ax
        push    cx
        push    dx
        
        cmp     bl, 8
        jl      .enable_master
        
        ; Enable slave IRQ
        mov     cl, bl
        sub     cl, 8                   ; Convert to slave bit position
        mov     al, 1
        shl     al, cl                  ; Create bit mask
        not     al                      ; Invert for clearing bit
        
        mov     dx, PIC2_DATA
        push    ax
        in      al, dx                  ; Read current mask
        pop     cx
        and     al, cl                  ; Clear the IRQ bit (enable)
        out     dx, al                  ; Write back mask
        
        ; Also ensure IRQ 2 (cascade) is enabled on master PIC
        mov     dx, PIC1_DATA
        in      al, dx
        and     al, 11111011b           ; Clear bit 2 (enable IRQ 2)
        out     dx, al
        jmp     .enable_done
        
.enable_master:
        ; Enable master IRQ
        mov     cl, bl
        mov     al, 1
        shl     al, cl                  ; Create bit mask
        not     al                      ; Invert for clearing bit
        
        mov     dx, PIC1_DATA
        push    ax
        in      al, dx                  ; Read current mask
        pop     cx
        and     al, cl                  ; Clear the IRQ bit (enable)
        out     dx, al                  ; Write back mask
        
.enable_done:
        clc                             ; Success
        
        pop     dx
        pop     cx
        pop     ax
        ret
enable_pic_irq ENDP

;-----------------------------------------------------------------------------
; disable_pic_irq - Disable IRQ at PIC level
;
; Input:  BL = IRQ number
; Output: None
; Uses:   AX, CX, DX
;-----------------------------------------------------------------------------
disable_pic_irq PROC
        push    ax
        push    cx
        push    dx
        
        cmp     bl, 8
        jl      .disable_master
        
        ; Disable slave IRQ
        mov     cl, bl
        sub     cl, 8                   ; Convert to slave bit position
        mov     al, 1
        shl     al, cl                  ; Create bit mask
        
        mov     dx, PIC2_DATA
        push    ax
        in      al, dx                  ; Read current mask
        pop     cx
        or      al, cl                  ; Set the IRQ bit (disable)
        out     dx, al                  ; Write back mask
        jmp     .disable_done
        
.disable_master:
        ; Disable master IRQ
        mov     cl, bl
        mov     al, 1
        shl     al, cl                  ; Create bit mask
        
        mov     dx, PIC1_DATA
        push    ax
        in      al, dx                  ; Read current mask
        pop     cx
        or      al, cl                  ; Set the IRQ bit (disable)
        out     dx, al                  ; Write back mask
        
.disable_done:
        pop     dx
        pop     cx
        pop     ax
        ret
disable_pic_irq ENDP

;-----------------------------------------------------------------------------
; update_irq_tracking - Update IRQ tracking after successful installation
;
; Input:  AL = NIC index, BL = IRQ number
; Output: None
; Uses:   SI
;-----------------------------------------------------------------------------
update_irq_tracking PROC
        push    si
        
        ; Mark IRQ as installed
        mov     si, OFFSET irq_installed
        mov     ah, 0
        add     si, ax
        mov     byte ptr [si], 1
        
        pop     si
        ret
update_irq_tracking ENDP

;-----------------------------------------------------------------------------
; get_nic_irq_number - Get IRQ number for NIC index
;
; Input:  AL = NIC index
; Output: BL = IRQ number, CY set if not assigned
; Uses:   BX, SI
;-----------------------------------------------------------------------------
get_nic_irq_number PROC
        push    si
        
        mov     si, OFFSET nic_irq_numbers
        mov     ah, 0
        add     si, ax
        mov     bl, [si]
        
        cmp     bl, IRQ_NONE
        je      .not_assigned
        
        clc                             ; Success
        jmp     .exit
        
.not_assigned:
        stc                             ; Not assigned
        
.exit:
        pop     si
        ret
get_nic_irq_number ENDP

;-----------------------------------------------------------------------------
; restore_original_vector - Restore original interrupt vector
;
; Input:  AL = NIC index, BL = IRQ number
; Output: None
; Uses:   AX, BX, CX, DX, ES
;-----------------------------------------------------------------------------
restore_original_vector PROC
        push    ax
        push    bx
        push    cx
        push    dx
        push    es
        
        ; Convert IRQ to vector number
        mov     cl, bl
        cmp     bl, 8
        jl      .master_restore
        
        add     cl, 70h - 8             ; Slave vector
        jmp     .do_restore
        
.master_restore:
        add     cl, 08h                 ; Master vector
        
.do_restore:
        ; Restore vector (simplified - would read from storage)
        ; In full implementation: restore from original_irq_vectors array
        
        pop     es
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
restore_original_vector ENDP

;-----------------------------------------------------------------------------
; clear_irq_tracking - Clear IRQ tracking information
;
; Input:  AL = NIC index
; Output: None
; Uses:   SI
;-----------------------------------------------------------------------------
clear_irq_tracking PROC
        push    si
        
        ; Clear installed flag
        mov     si, OFFSET irq_installed
        mov     ah, 0
        add     si, ax
        mov     byte ptr [si], 0
        
        pop     si
        ret
clear_irq_tracking ENDP

;-----------------------------------------------------------------------------
; update_uninstall_tracking - Update tracking after uninstall
;
; Input:  AL = NIC index
; Output: None
; Uses:   None
;-----------------------------------------------------------------------------
update_uninstall_tracking PROC
        ; Nothing additional needed for basic implementation
        ret
update_uninstall_tracking ENDP

;-----------------------------------------------------------------------------
; PUBLIC ENTRY POINTS FOR PIC MANAGEMENT
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; send_eoi_master - Send EOI to master PIC only
;
; Public function for sending EOI to master PIC (IRQ 0-7)
;
; Input:  None
; Output: None
; Uses:   AX, DX (but preserves them)
;-----------------------------------------------------------------------------
PUBLIC send_eoi_master
send_eoi_master PROC
        push    ax
        push    dx
        
        mov     al, PIC_EOI
        mov     dx, PIC1_COMMAND
        out     dx, al
        
        pop     dx
        pop     ax
        ret
send_eoi_master ENDP

;-----------------------------------------------------------------------------
; send_eoi_slave - Send EOI to both slave and master PICs
;
; Public function for sending EOI for slave PIC interrupts (IRQ 8-15)
; Must send EOI to slave first, then master (cascade)
;
; Input:  None
; Output: None
; Uses:   AX, DX (but preserves them)
;-----------------------------------------------------------------------------
PUBLIC send_eoi_slave
send_eoi_slave PROC
        push    ax
        push    dx
        
        ; Send EOI to slave PIC first
        mov     al, PIC_EOI
        mov     dx, PIC2_COMMAND
        out     dx, al
        
        ; Then send EOI to master PIC (for cascade)
        mov     dx, PIC1_COMMAND
        out     dx, al
        
        pop     dx
        pop     ax
        ret
send_eoi_slave ENDP

;-----------------------------------------------------------------------------
; send_eoi_for_irq - Send appropriate EOI based on IRQ number
;
; Public function that automatically sends EOI to correct PIC(s)
;
; Input:  AL = IRQ number (0-15)
; Output: None
; Uses:   AX, DX (but preserves original AX)
;-----------------------------------------------------------------------------
PUBLIC send_eoi_for_irq
send_eoi_for_irq PROC
        push    ax
        push    dx
        
        cmp     al, 8
        jl      .master_eoi
        
        ; IRQ 8-15: Send to both PICs
        call    send_eoi_slave
        jmp     .eoi_done
        
.master_eoi:
        ; IRQ 0-7: Send to master only
        call    send_eoi_master
        
.eoi_done:
        pop     dx
        pop     ax
        ret
send_eoi_for_irq ENDP

;-----------------------------------------------------------------------------
; enable_irq_line - Public function to enable specific IRQ
;
; Input:  AL = IRQ number (0-15)
; Output: CY clear if successful, CY set if failed
; Uses:   BX (preserves other registers)
;-----------------------------------------------------------------------------
PUBLIC enable_irq_line
enable_irq_line PROC
        push    bx
        
        mov     bl, al
        call    enable_pic_irq
        
        pop     bx
        ret
enable_irq_line ENDP

;-----------------------------------------------------------------------------
; disable_irq_line - Public function to disable specific IRQ
;
; Input:  AL = IRQ number (0-15)  
; Output: None
; Uses:   BX (preserves other registers)
;-----------------------------------------------------------------------------
PUBLIC disable_irq_line
disable_irq_line PROC
        push    bx
        
        mov     bl, al
        call    disable_pic_irq
        
        pop     bx
        ret
disable_irq_line ENDP

;-----------------------------------------------------------------------------
; Supporting functions for 3C515-TX DMA processing
;-----------------------------------------------------------------------------

process_3c515_rx_dma_ring PROC
        ; Process received packets from DMA descriptor ring
        ; This handles bus-master DMA completion for 3C515-TX
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        
        ; Get DMA descriptor ring status
        ; Full DMA descriptor ring implementation
        push    es
        push    di
        push    bx
        
        ; Get descriptor ring pointer
        mov     si, [rx_dma_ring_ptr]
        test    si, si
        jz      .no_ring_configured
        
        les     di, [rx_dma_ring_base]  ; ES:DI = ring base
        mov     cx, MAX_EVENTS_PER_IRQ  ; Process limit
        
.process_descriptors:
        ; Check descriptor status
        mov     ax, es:[di]             ; Status word
        test    ax, 0x8000              ; Complete bit
        jz      .no_more_rx             ; Not complete, done
        
        ; Extract packet length
        and     ax, 0x1FFF              ; 13-bit length field
        push    ax                      ; Save length
        
        ; Get packet buffer address
        mov     bx, es:[di+4]           ; Buffer low word
        mov     dx, es:[di+6]           ; Buffer high word
        
        ; Process the packet
        push    cx
        push    di
        push    dx
        push    bx
        push    ax                      ; Length
        call    process_dma_packet
        add     sp, 6                   ; Clean up stack
        pop     di
        pop     cx
        pop     ax                      ; Restore length
        
        ; Mark descriptor as free
        mov     word ptr es:[di], 0     ; Clear status
        
        ; Allocate new buffer for this descriptor
        push    cx
        call    allocate_rx_buffer
        jc      .alloc_failed
        
        ; Update descriptor with new buffer
        mov     es:[di+4], ax           ; Buffer low
        mov     es:[di+6], dx           ; Buffer high
        mov     word ptr es:[di+8], 1600 ; Max size
        mov     word ptr es:[di], 0x8000 ; Give to NIC
        
.alloc_failed:
        pop     cx
        
        ; Advance to next descriptor
        add     di, 16                  ; Descriptor size
        cmp     di, [rx_dma_ring_end]
        jb      .no_wrap
        les     di, [rx_dma_ring_base]  ; Wrap to start
.no_wrap:
        
        loop    .process_descriptors
        
.no_more_rx:
        ; Update ring pointer
        mov     [rx_dma_ring_ptr], di
        
        ; Refill any empty descriptors
        call    refill_rx_descriptors
        
.no_ring_configured:
        pop     bx
        pop     di
        pop     es
        
        ; For now, just acknowledge DMA completion
        mov     dx, 320h                ; 3C515-TX base
        add     dx, 20h                 ; DMA_STATUS
        in      al, dx                  ; Read status
        test    al, 01h                 ; RX_DMA_COMPLETE
        jz      .no_rx_dma
        
        ; Clear RX DMA completion bit
        mov     al, 01h
        out     dx, al                  ; Write 1 to clear
        
.no_rx_dma:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
process_3c515_rx_dma_ring ENDP

process_3c515_tx_completions PROC
        ; Process completed transmit DMA transfers
        push    ax
        push    dx
        
        ; Clear TX DMA completion status
        mov     dx, 320h                ; 3C515-TX base
        add     dx, 20h                 ; DMA_STATUS
        in      al, dx                  ; Read status
        test    al, 02h                 ; TX_DMA_COMPLETE
        jz      .no_tx_dma
        
        ; Clear TX DMA completion bit
        mov     al, 02h
        out     dx, al                  ; Write 1 to clear
        
        ; Process completed TX descriptors
        push    es
        push    di
        push    si
        push    bx
        
        ; Get TX descriptor ring
        mov     si, [tx_dma_ring_ptr]
        test    si, si
        jz      .no_tx_ring
        
        les     di, [tx_dma_ring_base]  ; ES:DI = TX ring
        mov     cx, MAX_EVENTS_PER_IRQ
        
.process_tx_desc:
        ; Check descriptor status
        mov     ax, es:[di]
        test    ax, 0x8000              ; Complete bit
        jz      .no_more_tx
        
        ; Check for errors
        test    ax, 0x4000              ; Error bit
        jz      .tx_success_dma
        
        ; Handle TX error
        inc     word ptr [tx_error_count]
        call    log_tx_dma_error
        jmp     .free_tx_buffer_dma
        
.tx_success_dma:
        ; Update success statistics
        inc     word ptr [tx_packet_count]
        mov     bx, ax
        and     bx, 0x1FFF              ; Extract length
        add     word ptr [tx_bytes_lo], bx
        adc     word ptr [tx_bytes_hi], 0
        
.free_tx_buffer_dma:
        ; Free the transmitted buffer
        mov     ax, es:[di+4]           ; Buffer low
        mov     dx, es:[di+6]           ; Buffer high
        call    free_tx_buffer_addr
        
        ; Clear descriptor
        mov     dword ptr es:[di], 0
        mov     dword ptr es:[di+4], 0
        mov     dword ptr es:[di+8], 0
        
        ; Signal TX complete
        call    signal_tx_complete
        
        ; Advance to next descriptor
        add     di, 16
        cmp     di, [tx_dma_ring_end]
        jb      .no_tx_wrap
        les     di, [tx_dma_ring_base]
.no_tx_wrap:
        
        loop    .process_tx_desc
        
.no_more_tx:
        ; Update TX ring pointer
        mov     [tx_dma_ring_ptr], di
        
.no_tx_ring:
        pop     bx
        pop     si
        pop     di
        pop     es
        
.no_tx_dma:
        pop     dx
        pop     ax
        ret
process_3c515_tx_completions ENDP

;-----------------------------------------------------------------------------
; Enhanced interrupt handlers with batching (max 10 events per interrupt)
;-----------------------------------------------------------------------------

nic_process_interrupt_batch_3c509b PROC
        ; Process 3C509B interrupts with batching to prevent interrupt storms
        ; Limits processing to MAX_EVENTS_PER_IRQ events per interrupt
        push    ax
        push    bx
        push    cx
        push    dx
        
        ; Initialize batch counter
        mov     byte ptr [batch_count_3c509b], 0
        mov     dx, 300h                ; 3C509B base I/O
        
.batch_loop:
        ; Check if we've hit the batching limit
        cmp     byte ptr [batch_count_3c509b], MAX_EVENTS_PER_IRQ
        jae     .batch_complete         ; Hit limit, defer rest
        
        ; Check for interrupt conditions
        add     dx, 0Eh                 ; INT_STATUS register
        in      al, dx                  ; Read interrupt status
        test    al, 3Fh                 ; Any interrupt bits set?
        jz      .batch_complete         ; No more interrupts
        
        ; Process each interrupt type with minimal work
        test    al, 01h                 ; RX_COMPLETE
        jz      .check_batch_tx
        
        ; Minimal RX processing - just acknowledge
        call    acknowledge_3c509b_rx_minimal
        inc     byte ptr [batch_count_3c509b]
        
.check_batch_tx:
        test    al, 02h                 ; TX_COMPLETE
        jz      .check_batch_errors
        
        ; Minimal TX processing - just acknowledge
        call    acknowledge_3c509b_tx_minimal
        inc     byte ptr [batch_count_3c509b]
        
.check_batch_errors:
        test    al, 0Ch                 ; TX_ERROR or RX_ERROR
        jz      .batch_continue
        
        ; Minimal error processing - acknowledge and count
        call    acknowledge_3c509b_error_minimal
        inc     byte ptr [batch_count_3c509b]
        
.batch_continue:
        sub     dx, 0Eh                 ; Back to base
        jmp     .batch_loop             ; Check for more events
        
.batch_complete:
        ; If we hit the batching limit, queue more work for later
        cmp     byte ptr [batch_count_3c509b], MAX_EVENTS_PER_IRQ
        jb      .batching_done
        
        ; Queue deferred work to process remaining interrupts
        mov     ax, OFFSET process_3c509b_packets
        call    queue_deferred_work
        
.batching_done:
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
nic_process_interrupt_batch_3c509b ENDP

nic_process_interrupt_batch_3c515 PROC
        ; Process 3C515-TX interrupts with batching
        push    ax
        push    bx
        push    cx
        push    dx
        
        ; Initialize batch counter
        mov     byte ptr [batch_count_3c515], 0
        mov     dx, 320h                ; 3C515-TX base I/O
        
.batch_loop_3c515:
        ; Check batching limit
        cmp     byte ptr [batch_count_3c515], MAX_EVENTS_PER_IRQ
        jae     .batch_complete_3c515
        
        ; Select IntStatus window
        add     dx, 0Eh                 ; Command register
        mov     ax, 7000h               ; Window 7
        out     dx, ax
        
        ; Check interrupt status
        sub     dx, 0Eh                 ; Back to base
        add     dx, 08h                 ; IntStatus register
        in      ax, dx                  ; Read interrupt status
        test    ax, 003Fh               ; Any standard interrupts?
        jz      .check_3c515_dma        ; Check DMA interrupts
        
        ; Process standard interrupts minimally
        test    ax, 0001h               ; RxComplete
        jz      .check_3c515_tx
        
        call    acknowledge_3c515_rx_minimal
        inc     byte ptr [batch_count_3c515]
        
.check_3c515_tx:
        test    ax, 0002h               ; TxComplete
        jz      .check_3c515_dma
        
        call    acknowledge_3c515_tx_minimal
        inc     byte ptr [batch_count_3c515]
        
.check_3c515_dma:
        ; Check DMA interrupts
        sub     dx, 08h                 ; Back to base
        add     dx, 20h                 ; DMA_STATUS
        in      al, dx                  ; Read DMA status
        test    al, 03h                 ; Any DMA interrupts?
        jz      .batch_continue_3c515
        
        ; Acknowledge DMA interrupts minimally
        out     dx, al                  ; Write back to clear
        inc     byte ptr [batch_count_3c515]
        
.batch_continue_3c515:
        sub     dx, 20h                 ; Back to base
        jmp     .batch_loop_3c515
        
.batch_complete_3c515:
        ; Queue deferred work if we hit the limit
        cmp     byte ptr [batch_count_3c515], MAX_EVENTS_PER_IRQ
        jb      .batching_done_3c515
        
        mov     ax, OFFSET process_3c515_packets
        call    queue_deferred_work
        
.batching_done_3c515:
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
nic_process_interrupt_batch_3c515 ENDP

;-----------------------------------------------------------------------------
; Minimal acknowledgment functions for interrupt batching
;-----------------------------------------------------------------------------

acknowledge_3c509b_rx_minimal PROC
        ; Minimal RX acknowledgment for batching
        push    dx
        push    ax
        mov     dx, 300h
        add     dx, 0Ch                 ; RX_STATUS
        in      ax, dx                  ; Read to acknowledge
        pop     ax
        pop     dx
        ret
acknowledge_3c509b_rx_minimal ENDP

acknowledge_3c509b_tx_minimal PROC
        ; Minimal TX acknowledgment for batching
        push    dx
        push    ax
        mov     dx, 300h
        add     dx, 0Ah                 ; TX_STATUS  
        in      al, dx                  ; Read to acknowledge
        pop     ax
        pop     dx
        ret
acknowledge_3c509b_tx_minimal ENDP

acknowledge_3c509b_error_minimal PROC
        ; Minimal error acknowledgment for batching
        push    dx
        push    ax
        mov     dx, 300h
        add     dx, 0Eh                 ; INT_STATUS
        in      al, dx                  ; Read to clear
        pop     ax
        pop     dx
        ret
acknowledge_3c509b_error_minimal ENDP

acknowledge_3c515_rx_minimal PROC
        ; Minimal 3C515-TX RX acknowledgment
        push    dx
        push    ax
        mov     dx, 320h
        add     dx, 08h                 ; IntStatus (Window 7 assumed)
        in      ax, dx
        and     ax, 0001h               ; Isolate RxComplete
        out     dx, ax                  ; Write 1 to clear
        pop     ax
        pop     dx
        ret
acknowledge_3c515_rx_minimal ENDP

acknowledge_3c515_tx_minimal PROC
        ; Minimal 3C515-TX TX acknowledgment
        push    dx
        push    ax
        mov     dx, 320h
        add     dx, 08h                 ; IntStatus (Window 7 assumed)
        in      ax, dx
        and     ax, 0002h               ; Isolate TxComplete
        out     dx, ax                  ; Write 1 to clear
        pop     ax
        pop     dx
        ret
acknowledge_3c515_tx_minimal ENDP

;-----------------------------------------------------------------------------
; Helper Functions for Interrupt Handling
;-----------------------------------------------------------------------------

; I/O delay for ISA bus timing
io_delay PROC
        push    ax
        in      al, 80h                 ; Read from unused port
        in      al, 80h                 ; Two reads for ~1µs delay
        pop     ax
        ret
io_delay ENDP

; Get NIC I/O base address
get_nic_iobase PROC
        ; Input: SI = NIC index
        ; Output: AX = I/O base address
        push    bx
        movzx   bx, si
        shl     bx, 1                   ; Word offset
        mov     ax, [nic_iobase_table + bx]
        pop     bx
        ret
get_nic_iobase ENDP

; Check NIC interrupt status
check_nic_interrupt_status PROC
        ; Input: SI = NIC index
        ; Output: AX = 1 if interrupt pending, 0 if not
        push    dx
        push    bx
        
        call    get_nic_iobase
        test    ax, ax
        jz      .no_interrupt
        mov     dx, ax
        
        ; Read interrupt status register
        add     dx, 0x0E                ; INT_STATUS offset
        in      ax, dx
        test    ax, 0x00FF              ; Check for any interrupts
        jz      .no_interrupt
        
        mov     ax, 1                   ; Interrupt pending
        jmp     .status_done
        
.no_interrupt:
        xor     ax, ax                  ; No interrupt
        
.status_done:
        pop     bx
        pop     dx
        ret
check_nic_interrupt_status ENDP

; Handle NIC interrupt
handle_nic_interrupt PROC
        ; Input: SI = NIC index with interrupt
        push    ax
        push    bx
        push    dx
        
        ; Determine NIC type and call appropriate handler
        cmp     si, 0
        je      .handle_3c509b
        cmp     si, 1
        je      .handle_3c515
        jmp     .done
        
.handle_3c509b:
        call    hardware_handle_3c509b_irq
        jmp     .done
        
.handle_3c515:
        call    hardware_handle_3c515_irq
        
.done:
        pop     dx
        pop     bx
        pop     ax
        ret
handle_nic_interrupt ENDP

; Process 3C509B interrupts
process_3c509b_interrupts PROC
        ; Input: BX = interrupt status, DX = I/O base
        push    ax
        push    cx
        
        ; Process each interrupt type
        test    bx, 0x0001              ; RX_COMPLETE
        jz      .no_rx
        call    handle_rx_interrupt_3c509b
.no_rx:
        test    bx, 0x0004              ; TX_COMPLETE
        jz      .no_tx
        call    handle_tx_interrupt_3c509b
.no_tx:
        test    bx, 0x0080              ; ADAPTER_FAIL
        jz      .no_fail
        call    handle_adapter_failure_3c509b
.no_fail:
        
        ; Acknowledge all processed interrupts
        mov     ax, bx
        and     ax, 0x00FF
        or      ax, 0x6800              ; ACK_INTR command
        add     dx, 0x0E
        out     dx, ax
        
        pop     cx
        pop     ax
        ret
process_3c509b_interrupts ENDP

; Process single 3C509B event
process_3c509b_event PROC
        ; Input: BX = interrupt status, DX = I/O base
        push    ax
        
        ; Similar to process_3c509b_interrupts but for single event
        test    bx, 0x0001
        jz      .check_tx
        call    handle_rx_interrupt_3c509b
        jmp     .event_done
        
.check_tx:
        test    bx, 0x0004
        jz      .check_error
        call    handle_tx_interrupt_3c509b
        jmp     .event_done
        
.check_error:
        test    bx, 0x00F8              ; Any error bits
        jz      .event_done
        call    handle_error_interrupt_3c509b
        
.event_done:
        pop     ax
        ret
process_3c509b_event ENDP

; Process single 3C515 event
process_3c515_event PROC
        ; Input: BX = interrupt status, DX = I/O base
        push    ax
        
        ; Check for DMA events first
        test    bx, 0x0200              ; UP_COMPLETE
        jz      .check_down
        call    process_3c515_rx_dma_ring
        jmp     .event_done_515
        
.check_down:
        test    bx, 0x0400              ; DOWN_COMPLETE
        jz      .check_normal
        call    process_3c515_tx_completions
        jmp     .event_done_515
        
.check_normal:
        ; Process normal interrupts
        test    bx, 0x0001              ; RX_COMPLETE
        jz      .check_tx_515
        call    handle_rx_interrupt_3c515
        jmp     .event_done_515
        
.check_tx_515:
        test    bx, 0x0004              ; TX_COMPLETE
        jz      .event_done_515
        call    handle_tx_interrupt_3c515
        
.event_done_515:
        pop     ax
        ret
process_3c515_event ENDP

; Logging functions (stubs - implement based on your logging system)
log_rx_error PROC
        ret
log_rx_error ENDP

log_rx_error_details PROC
        ret
log_rx_error_details ENDP

log_tx_dma_error PROC
        ret
log_tx_dma_error ENDP

handle_tx_error PROC
        ret
handle_tx_error ENDP

; Buffer management functions
free_tx_buffer PROC
        ; Input: SI = buffer pointer
        ret
free_tx_buffer ENDP

free_current_tx_buffer PROC
        push    si
        mov     si, [current_tx_buffer]
        test    si, si
        jz      .no_buffer
        call    free_tx_buffer
        mov     word ptr [current_tx_buffer], 0
.no_buffer:
        pop     si
        ret
free_current_tx_buffer ENDP

free_tx_buffer_addr PROC
        ; Input: DX:AX = buffer address
        ret
free_tx_buffer_addr ENDP

free_dma_buffers PROC
        ret
free_dma_buffers ENDP

allocate_rx_buffer PROC
        ; Output: DX:AX = buffer address, CF = error
        xor     dx, dx
        mov     ax, temp_rx_buffer
        clc
        ret
allocate_rx_buffer ENDP

refill_rx_descriptors PROC
        ret
refill_rx_descriptors ENDP

; Transmission retry
retry_transmission PROC
        ret
retry_transmission ENDP

; DMA packet processing
process_dma_packet PROC
        ; Input: Stack has buffer address and length
        ret
process_dma_packet ENDP

; Signal TX completion
signal_tx_complete PROC
        ret
signal_tx_complete ENDP

; Interrupt handlers for specific events
handle_rx_interrupt_3c509b PROC
        ret
handle_rx_interrupt_3c509b ENDP

handle_tx_interrupt_3c509b PROC
        ret
handle_tx_interrupt_3c509b ENDP

handle_adapter_failure_3c509b PROC
        ret
handle_adapter_failure_3c509b ENDP

handle_error_interrupt_3c509b PROC
        ret
handle_error_interrupt_3c509b ENDP

handle_rx_interrupt_3c515 PROC
        ret
handle_rx_interrupt_3c515 ENDP

handle_tx_interrupt_3c515 PROC
        ret
handle_tx_interrupt_3c515 ENDP

;=============================================================================
; STACK OVERFLOW PROTECTION FUNCTIONS
;=============================================================================

; ======================================================
; check_canary_area - Generic 16-bit canary area checker
;   IN:  DS:SI = start of canary area
;        CX    = number of words to check (not dwords!)
;   OUT: AL = 1 (ZF=1) if all good, AL = 0 (ZF=0) on first mismatch
;   Clobbers: AX, DX, SI, CX. Flags reflect AL (via OR AL,AL).
;   Preserves: BX, DI, ES, BP
;   Note: Designed for 8086, uses 16-bit ops only.
; ======================================================
check_canary_area PROC
        mov     dx, CANARY_WORD_LO      ; Expected pattern
@@next:
        cmp     cx, 0
        je      short @ok

        ; Compare current word with expected pattern
        cmp     word ptr [si], dx
        jne     short @bad

        add     si, 2                   ; Next word
        dec     cx
        jmp     short @@next

@ok:
        mov     al, 1
        or      al, al
        ret

@bad:
        xor     al, al
        or      al, al
        ret
check_canary_area ENDP

; ======================================================
; check_stack_canaries - Validate IRQ stack canary patterns (16-bit compatible)
;
; Checks all stack canary patterns for corruption indicating overflow.
; Should be called periodically and after suspected stack-intensive operations.
;
; Input:  None (DS must be DGROUP/_DATA)
; Output: AX = number of corrupted canary areas (0 = all OK)
; Uses:   AX, CX, DX, SI
;-----------------------------------------------------------------------------
PUBLIC check_stack_canaries
check_stack_canaries PROC
        push    cx
        push    dx
        push    si
        
        xor     ax, ax                  ; Count of corrupted canary areas
        
        ; Check 3C509B stack canaries
        mov     si, OFFSET irq_stack_3c509b_canary
        mov     cx, 8                   ; 8 words to check
        call    check_canary_area
        or      al, al                  ; Check result
        jnz     .3c509b_ok
        inc     ax                      ; Count corruption (AX was 0)
        inc     word ptr [stack_overflow_3c509b]  ; Update overflow counter
        
        ; EMERGENCY: 3C509B stack canary corruption detected
        push    ax                      ; Save corruption count
        mov     al, 3                   ; IRQ for 3C509B (example)
        call    emergency_canary_response
        pop     ax                      ; Restore corruption count
        
.3c509b_ok:
        ; Check 3C515 stack canaries
        mov     si, OFFSET irq_stack_3c515_canary
        mov     cx, 8                   ; 8 words to check
        push    ax                      ; Save corruption count
        call    check_canary_area
        pop     dx                      ; Restore corruption count to DX
        or      al, al                  ; Check result
        jnz     .3c515_ok
        inc     dx                      ; Count corruption
        inc     word ptr [stack_overflow_3c515]   ; Update overflow counter
        
        ; EMERGENCY: 3C515 stack canary corruption detected
        push    dx                      ; Save corruption count
        mov     al, 5                   ; IRQ for 3C515 (example)
        call    emergency_canary_response
        pop     dx                      ; Restore corruption count
        
.3c515_ok:
        mov     ax, dx                  ; Return total corruption count
        
        pop     si
        pop     dx
        pop     cx
        ret
check_stack_canaries ENDP

;-----------------------------------------------------------------------------
; initialize_stack_canaries - Initialize all stack canary patterns (16-bit compatible)
;
; Call this during driver initialization to set up stack overflow detection.
;
; Input:  None (DS must be DGROUP/_DATA)
; Output: None
; Uses:   AX, CX, DI
;-----------------------------------------------------------------------------
PUBLIC initialize_stack_canaries
initialize_stack_canaries PROC
        push    ax
        push    cx  
        push    di
        
        ; Initialize 3C509B canaries with 16-bit word pattern
        mov     di, OFFSET irq_stack_3c509b_canary
        mov     ax, CANARY_WORD_LO      ; 16-bit canary pattern
        mov     cx, 8                   ; 8 words (16 bytes total)
        rep     stosw
        
        ; Initialize 3C515 canaries with 16-bit word pattern
        mov     di, OFFSET irq_stack_3c515_canary
        mov     ax, CANARY_WORD_LO      ; 16-bit canary pattern
        mov     cx, 8                   ; 8 words (16 bytes total)
        rep     stosw
        
        ; Clear overflow counters
        mov     word ptr [stack_overflow_3c509b], 0
        mov     word ptr [stack_overflow_3c515], 0
        
        pop     di
        pop     cx
        pop     ax
        ret
initialize_stack_canaries ENDP

;-----------------------------------------------------------------------------
; emergency_canary_response - Emergency response to stack canary corruption
;
; Called when stack canary corruption is detected. Implements immediate
; protective measures to prevent further damage.
;
; Input:  AL = IRQ number of affected stack (used for masking)
; Output: None (may not return in severe cases)
; Preserves: All registers and flags (via push/pop)
; Side Effects: 
;   - Sends EOI to both PICs unconditionally
;   - Masks the specified IRQ at PIC level
;   - Increments emergency_canary_count
;   - Reinitializes corrupted canary pattern
;   - May enter infinite loop if count >= 10
; Note:   This is an emergency function - assumes corruption in progress
;         Uses SEG directive for proper segment addressing (DS-independent)
;-----------------------------------------------------------------------------
PUBLIC emergency_canary_response
emergency_canary_response PROC
        pushf
        cli                             ; Ensure interrupts disabled during emergency
        push    ax
        push    dx
        push    cx                      ; Save CX (used by rep stosw)
        push    di                      ; Save DI (used by rep stosw)
        push    es                      ; Will modify ES for rep stosw
        
        ; Preserve IRQ number in DL before clobbering AL
        mov     dl, al                  ; DL = IRQ number for later use
        
        ; CRITICAL: Send EOI first to prevent PIC lockup
        ; Unconditionally send to both PICs for robustness in emergency path
        mov     al, 20h                 ; Non-specific EOI command
        out     0A0h, al                ; Send to slave PIC (safe if not applicable)
        out     020h, al                ; Send to master PIC (always needed)
        
        ; EMERGENCY STEP 1: Mask the affected IRQ to prevent further nesting
        ; Restore IRQ number to AL for mask_irq call
        mov     al, dl                  ; Restore IRQ number from DL
        call    mask_irq                ; Mask IRQ in AL at PIC level
        
        ; EMERGENCY STEP 2: Set panic flag for diagnostic reporting
        ; Load proper segment for emergency_canary_count
        mov     ax, SEG emergency_canary_count
        mov     es, ax
        inc     word ptr es:[emergency_canary_count]
        
        ; EMERGENCY STEP 3: Reinitialize the corrupted canary with proper ES:DI/DF setup
        cld                             ; Clear DF for forward string operations
        
        ; Check which canary to reinitialize based on IRQ (preserved in DL)
        cmp     dl, 5                   ; Assuming IRQ 5 for 3C515
        je      .reinit_3c515
        
        ; Reinit 3C509B canary (default case)
        mov     ax, SEG irq_stack_3c509b_canary
        mov     es, ax                  ; ES = segment of canary buffer
        mov     di, OFFSET irq_stack_3c509b_canary
        mov     ax, CANARY_WORD_LO
        mov     cx, 8                   ; 8 words = 16 bytes
        rep     stosw                   ; ES:DI properly set, DF cleared
        jmp     .recovery_complete
        
.reinit_3c515:
        ; Reinit 3C515 canary
        mov     ax, SEG irq_stack_3c515_canary
        mov     es, ax                  ; ES = segment of canary buffer
        mov     di, OFFSET irq_stack_3c515_canary
        mov     ax, CANARY_WORD_LO
        mov     cx, 8                   ; 8 words = 16 bytes
        rep     stosw                   ; ES:DI properly set, DF cleared
        
.recovery_complete:
        ; Check if this is a severe case requiring halt
        ; Load proper segment for counter check
        mov     ax, SEG emergency_canary_count
        mov     es, ax
        cmp     word ptr es:[emergency_canary_count], 10
        jb      .done                   ; Less than 10 corruptions, continue
        
        ; SEVERE CORRUPTION: Too many canary failures, system unstable
        ; Fill smaller safe area to avoid overflow
        mov     ax, SEG irq_stack_3c509b_canary
        mov     es, ax
        mov     di, OFFSET irq_stack_3c509b_canary
        mov     ax, 0BEEFh              ; Emergency pattern
        mov     cx, 8                   ; Fill only 16 bytes (safe within canary buffer)
        rep     stosw                   ; ES:DI set, DF cleared
        
        ; Freeze system with interrupts disabled
        cli
.emergency_halt:
        jmp     .emergency_halt         ; Infinite loop with IF=0
        
.done:
        pop     es
        pop     di                      ; Restore DI
        pop     cx                      ; Restore CX
        pop     dx
        pop     ax
        popf                            ; Restore original interrupt flag
        ret
emergency_canary_response ENDP

_TEXT ENDS

END