; ============================================================================
; mod_irqmit_rt.asm - Runtime Interrupt Mitigation Module
; ============================================================================
; NASM 16-bit JIT module for interrupt mitigation and batching.
; Provides runtime context management and batched interrupt processing.
;
; Created: 2026-02-01 14:30:00
; Target: 8086+ DOS Real Mode
; Assembler: NASM
; ============================================================================

        cpu 8086

; Module capabilities and requirements
MOD_CAP_CORE    equ 8000h       ; Core module capability

; Context size and limits
CTX_SIZE        equ 32          ; simplified context size per NIC
MAX_NICS        equ 8
MAX_WORK        equ 16          ; default max work per interrupt
TICK_SEG        equ 0040h       ; BIOS data segment
TICK_OFF        equ 006Ch       ; BIOS tick counter offset

; Patch configuration
PATCH_COUNT     equ 0           ; No patches in this module

%include "patch_macros.inc"

; ============================================================================
; MODULE HEADER (64 bytes)
; ============================================================================
section .text class=MODULE

global _mod_irqmit_rt_header
_mod_irqmit_rt_header:
header:
    db 'PKTDRV',0                   ; 7 bytes - module signature
    db 1, 0                         ; 2 bytes - version 1.0
    dw hot_start                    ; hot section start offset
    dw hot_end                      ; hot section end offset
    dw 0, 0                         ; cold_start, cold_end (unused)
    dw patch_table                  ; patch table offset
    dw PATCH_COUNT                  ; number of patch entries
    dw (hot_end - header)           ; module_size
    dw (hot_end - hot_start)        ; required_memory
    db 0                            ; cpu_requirements (8086)
    db 0                            ; nic_type (0 = any)
    dw 8000h                        ; capability flags (MOD_CAP_CORE)
    times (64 - ($ - header)) db 0  ; pad header to 64 bytes

hot_start:

; ============================================================================
; MODULE INITIALIZATION
; ============================================================================
_mod_irqmit_rt_init:
        ; Initialize all contexts to zero (already done by bss-like init)
        mov     byte [mit_initialized], 1
        xor     ax, ax
        retf

; ============================================================================
; Per-NIC Context Layout (32 bytes)
; ============================================================================
; Offset  Size    Field
; +0      word    enabled
; +2      word    io_base
; +4      dword   irq_count
; +8      dword   events_processed
; +12     dword   batches
; +16     word    max_work_limit
; +18     word    current_batch_size
; +20     word    last_tick
; +22     word    yield_threshold
; +24     8bytes  reserved
; ============================================================================

; ============================================================================
; FUNCTION: is_interrupt_mitigation_enabled_
; ============================================================================
; Check if interrupt mitigation is enabled for a NIC
;
; Input:   AX = nic_index
; Output:  AX = enabled flag (0 or 1)
; Clobbers: BX, CX, DX
; ============================================================================
global is_interrupt_mitigation_enabled_
is_interrupt_mitigation_enabled_:
        ; Bounds check
        cmp     ax, MAX_NICS
        jae     .out_of_bounds

        ; Calculate context offset: nic_index * CTX_SIZE
        mov     cx, CTX_SIZE
        mul     cx                  ; DX:AX = AX * CX
        mov     bx, ax              ; BX = offset into mit_contexts

        ; Get enabled flag (word at offset +0)
        mov     ax, [mit_contexts + bx]
        retf

.out_of_bounds:
        xor     ax, ax              ; Return 0 for out of bounds
        retf

; ============================================================================
; FUNCTION: get_mitigation_context_
; ============================================================================
; Get far pointer to mitigation context for a NIC
;
; Input:   AX = nic_index
; Output:  DX:AX = far pointer to context (NULL if out of bounds)
; Clobbers: BX, CX
; ============================================================================
global get_mitigation_context_
get_mitigation_context_:
        ; Bounds check
        cmp     ax, MAX_NICS
        jae     .null_ptr

        ; Calculate context offset: nic_index * CTX_SIZE
        mov     cx, CTX_SIZE
        mul     cx                  ; DX:AX = AX * CX

        ; Get segment:offset
        mov     dx, ds              ; Segment (assuming mit_contexts in DS)
        ; AX already contains offset
        add     ax, mit_contexts    ; Add base address
        retf

.null_ptr:
        xor     ax, ax
        xor     dx, dx
        retf

; ============================================================================
; FUNCTION: interrupt_mitigation_apply_runtime_
; ============================================================================
; Apply runtime configuration to mitigation context
;
; Input:   AX = nic_index
;          DX = new_max_work
; Output:  None
; Clobbers: BX, CX
; ============================================================================
global interrupt_mitigation_apply_runtime_
interrupt_mitigation_apply_runtime_:
        push    dx                  ; Save new_max_work

        ; Bounds check
        cmp     ax, MAX_NICS
        jae     .done

        ; Calculate context offset: nic_index * CTX_SIZE
        mov     cx, CTX_SIZE
        mul     cx                  ; DX:AX = AX * CX
        mov     bx, ax              ; BX = offset into mit_contexts

        ; Store new max_work_limit at offset +16
        pop     dx                  ; Restore new_max_work
        mov     [mit_contexts + bx + 16], dx
        retf

.done:
        pop     dx                  ; Clean stack
        retf

; ============================================================================
; FUNCTION: process_batched_interrupts_3c515_
; ============================================================================
; Process batched interrupts for 3Com 3C515 NIC
;
; Input:   AX = nic_index
; Output:  AX = number of events processed
; Clobbers: BX, CX, DX, SI
; ============================================================================
global process_batched_interrupts_3c515_
process_batched_interrupts_3c515_:
        push    bp
        mov     bp, sp
        push    di

        ; Bounds check
        cmp     ax, MAX_NICS
        jae     .no_events

        ; Calculate context offset: nic_index * CTX_SIZE
        mov     cx, CTX_SIZE
        mul     cx                  ; DX:AX = AX * CX
        mov     bx, ax              ; BX = offset into mit_contexts

        ; Check if enabled
        cmp     word [mit_contexts + bx], 0
        je      .no_events

        ; Get io_base (offset +2)
        mov     dx, [mit_contexts + bx + 2]
        add     dx, 0x0E            ; Status register at io_base + 0x0E

        ; Read status register
        in      al, dx

        ; Count set bits in status (simplified: just count bits in AL)
        xor     cx, cx              ; CX = event count
        mov     di, ax              ; Save original status
        and     di, 0x00FF          ; Mask to AL only

        ; Get max_work_limit (offset +16)
        mov     si, [mit_contexts + bx + 16]

.count_bits:
        test    di, di
        jz      .done_counting
        cmp     cx, si              ; Check against max_work_limit
        jae     .done_counting

        ; Count a bit
        mov     ax, di
        and     ax, 1
        add     cx, ax

        ; Shift right
        shr     di, 1
        jmp     .count_bits

.done_counting:
        ; Update statistics
        ; events_processed (offset +8) - add CX
        add     word [mit_contexts + bx + 8], cx
        adc     word [mit_contexts + bx + 10], 0

        ; batches (offset +12) - increment
        add     word [mit_contexts + bx + 12], 1
        adc     word [mit_contexts + bx + 14], 0

        ; irq_count (offset +4) - increment
        add     word [mit_contexts + bx + 4], 1
        adc     word [mit_contexts + bx + 6], 0

        ; current_batch_size (offset +18)
        mov     [mit_contexts + bx + 18], cx

        ; Return event count
        mov     ax, cx
        pop     di
        pop     bp
        retf

.no_events:
        xor     ax, ax
        pop     di
        pop     bp
        retf

; ============================================================================
; FUNCTION: process_batched_interrupts_3c509b_
; ============================================================================
; Process batched interrupts for 3Com 3C509B NIC
; Same logic as 3C515 (EL3 status register at same offset)
;
; Input:   AX = nic_index
; Output:  AX = number of events processed
; Clobbers: BX, CX, DX, SI
; ============================================================================
global process_batched_interrupts_3c509b_
process_batched_interrupts_3c509b_:
        push    bp
        mov     bp, sp
        push    di

        ; Bounds check
        cmp     ax, MAX_NICS
        jae     .no_events

        ; Calculate context offset: nic_index * CTX_SIZE
        mov     cx, CTX_SIZE
        mul     cx                  ; DX:AX = AX * CX
        mov     bx, ax              ; BX = offset into mit_contexts

        ; Check if enabled
        cmp     word [mit_contexts + bx], 0
        je      .no_events

        ; Get io_base (offset +2)
        mov     dx, [mit_contexts + bx + 2]
        add     dx, 0x0E            ; Status register at io_base + 0x0E

        ; Read status register
        in      al, dx

        ; Count set bits in status (simplified: just count bits in AL)
        xor     cx, cx              ; CX = event count
        mov     di, ax              ; Save original status
        and     di, 0x00FF          ; Mask to AL only

        ; Get max_work_limit (offset +16)
        mov     si, [mit_contexts + bx + 16]

.count_bits:
        test    di, di
        jz      .done_counting
        cmp     cx, si              ; Check against max_work_limit
        jae     .done_counting

        ; Count a bit
        mov     ax, di
        and     ax, 1
        add     cx, ax

        ; Shift right
        shr     di, 1
        jmp     .count_bits

.done_counting:
        ; Update statistics
        ; events_processed (offset +8) - add CX
        add     word [mit_contexts + bx + 8], cx
        adc     word [mit_contexts + bx + 10], 0

        ; batches (offset +12) - increment
        add     word [mit_contexts + bx + 12], 1
        adc     word [mit_contexts + bx + 14], 0

        ; irq_count (offset +4) - increment
        add     word [mit_contexts + bx + 4], 1
        adc     word [mit_contexts + bx + 6], 0

        ; current_batch_size (offset +18)
        mov     [mit_contexts + bx + 18], cx

        ; Return event count
        mov     ax, cx
        pop     di
        pop     bp
        retf

.no_events:
        xor     ax, ax
        pop     di
        pop     bp
        retf

; ============================================================================
; FUNCTION: should_yield_cpu_
; ============================================================================
; Determine if CPU should yield based on time elapsed
;
; Input:   AX = nic_index
; Output:  AX = 1 if should yield, 0 otherwise
; Clobbers: BX, CX, DX, ES
; ============================================================================
global should_yield_cpu_
should_yield_cpu_:
        push    bp
        mov     bp, sp

        ; Bounds check
        cmp     ax, MAX_NICS
        jae     .no_yield

        ; Calculate context offset: nic_index * CTX_SIZE
        mov     cx, CTX_SIZE
        mul     cx                  ; DX:AX = AX * CX
        mov     bx, ax              ; BX = offset into mit_contexts

        ; Read BIOS tick counter
        push    ds
        mov     ax, TICK_SEG
        mov     es, ax
        mov     ax, [es:TICK_OFF]   ; Current tick (low word)
        pop     ds

        ; Get last_tick (offset +20)
        mov     dx, [mit_contexts + bx + 20]

        ; Calculate delta
        sub     ax, dx              ; AX = current_tick - last_tick

        ; Get yield_threshold (offset +22)
        mov     cx, [mit_contexts + bx + 22]

        ; Compare delta with threshold
        cmp     ax, cx
        jae     .should_yield

.no_yield:
        xor     ax, ax
        pop     bp
        retf

.should_yield:
        ; Update last_tick to current tick
        push    ds
        mov     ax, TICK_SEG
        mov     es, ax
        mov     ax, [es:TICK_OFF]
        pop     ds
        mov     [mit_contexts + bx + 20], ax

        ; Return 1
        mov     ax, 1
        pop     bp
        retf

; ============================================================================
; DATA SECTION (HOT)
; ============================================================================
section .data

align 4
mit_contexts:
        times (CTX_SIZE * MAX_NICS) db 0    ; 256 bytes of context data

mit_initialized:
        db 0

hot_end:

patch_table:
; No patches in this module

; ============================================================================
; END OF MODULE
; ============================================================================
