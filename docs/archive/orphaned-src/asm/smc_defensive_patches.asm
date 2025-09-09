;-----------------------------------------------------------------------------
; @file smc_defensive_patches.asm
; @brief SMC patches for TSR defensive programming integration
;
; GPT-5 Requirement: Integrate TSR defensive programming into SMC framework
; This module provides patchable entry points for safety checks that get
; optimized based on runtime environment detection.
;-----------------------------------------------------------------------------

.MODEL SMALL
.386

include 'tsr_defensive.inc'
include 'patch_macros.inc'

; External references
EXTERN caller_ss:WORD
EXTERN caller_sp:WORD
EXTERN isr_stack_top:WORD
EXTERN indos_segment:WORD
EXTERN indos_offset:WORD
EXTERN criterr_segment:WORD
EXTERN criterr_offset:WORD
EXTERN work_queue:DWORD
EXTERN queue_head:WORD
EXTERN queue_tail:WORD

_TEXT SEGMENT
        ASSUME  CS:_TEXT

;=============================================================================
; PATCHABLE SAFETY CHECK POINTS
; These 5-byte patch points get replaced with optimized code during init
;=============================================================================

;-----------------------------------------------------------------------------
; Patchable DOS safety check
; Default: Full safety check
; V86 Mode: Full check with VDS awareness
; Real Mode + Safe: NOP sled (no check needed)
;-----------------------------------------------------------------------------
PUBLIC patch_dos_safety_check
patch_dos_safety_check:
        PATCH_POINT_CALL dos_safety_full, dos_safety_fallback

;-----------------------------------------------------------------------------
; Patchable stack switch
; Default: Full stack switch with validation
; TSR Mode: Private stack switch
; App Mode: No switch needed (NOP)
;-----------------------------------------------------------------------------
PUBLIC patch_stack_switch_enter
patch_stack_switch_enter:
        PATCH_POINT_CALL stack_switch_full, stack_switch_fallback

PUBLIC patch_stack_switch_exit
patch_stack_switch_exit:
        PATCH_POINT_CALL stack_restore_full, stack_restore_fallback

;-----------------------------------------------------------------------------
; Patchable DMA boundary check
; Default: Full 64KB + 16MB checking
; PCI Mode: 4GB capable (minimal check)
; ISA Mode: Full boundary validation
;-----------------------------------------------------------------------------
PUBLIC patch_dma_boundary_check
patch_dma_boundary_check:
        PATCH_POINT_CALL dma_check_full, dma_check_fallback

;-----------------------------------------------------------------------------
; Patchable interrupt masking
; Default: PIC manipulation
; V86 Mode: VDS-aware masking
; APIC Mode: Advanced controller support
;-----------------------------------------------------------------------------
PUBLIC patch_interrupt_mask
patch_interrupt_mask:
        PATCH_POINT_CALL int_mask_pic, int_mask_fallback

PUBLIC patch_interrupt_unmask
patch_interrupt_unmask:
        PATCH_POINT_CALL int_unmask_pic, int_unmask_fallback

;=============================================================================
; FALLBACK IMPLEMENTATIONS (Used if patches not applied)
;=============================================================================

;-----------------------------------------------------------------------------
; Full DOS safety check fallback
;-----------------------------------------------------------------------------
dos_safety_fallback PROC NEAR
        push    ax
        push    es
        push    bx
        
        ; Check InDOS flag
        mov     es, [indos_segment]
        mov     bx, [indos_offset]
        cmp     byte ptr [es:bx], 0
        jnz     .not_safe
        
        ; Check critical error flag if available
        cmp     word ptr [criterr_segment], 0
        jz      .safe
        
        mov     es, [criterr_segment]
        mov     bx, [criterr_offset]
        test    byte ptr [es:bx], 01h
        jnz     .not_safe
        
.safe:
        clc                     ; Clear carry = safe
        jmp     .done
        
.not_safe:
        stc                     ; Set carry = not safe
        
.done:
        pop     bx
        pop     es
        pop     ax
        ret
dos_safety_fallback ENDP

;-----------------------------------------------------------------------------
; Full stack switch fallback with SS:SP preservation
;-----------------------------------------------------------------------------
stack_switch_fallback PROC NEAR
        ; Save caller's stack
        mov     [caller_ss], ss
        mov     [caller_sp], sp
        
        ; Switch to private ISR stack
        push    ax
        mov     ax, seg isr_stack_top
        mov     ss, ax
        mov     sp, offset isr_stack_top
        pop     ax
        
        ret
stack_switch_fallback ENDP

;-----------------------------------------------------------------------------
; Stack restore fallback
;-----------------------------------------------------------------------------
stack_restore_fallback PROC NEAR
        ; Restore caller's stack
        mov     ss, [caller_ss]
        mov     sp, [caller_sp]
        ret
stack_restore_fallback ENDP

;-----------------------------------------------------------------------------
; Full DMA boundary check (64KB + 16MB)
;-----------------------------------------------------------------------------
dma_check_fallback PROC NEAR
        push    ax
        push    dx
        
        ; Check for 64KB boundary crossing
        ; Input: ES:DI = buffer address, CX = length
        mov     ax, di
        add     ax, cx
        jc      .boundary_crossed    ; Carry = crossed 64KB
        
        ; Check for 16MB ISA limit (24-bit address)
        ; Convert ES:DI to linear address
        mov     dx, es
        shl     dx, 4               ; Segment * 16
        add     dx, di              ; Add offset
        jc      .above_16mb         ; Overflow = above 16MB
        
        ; Check if high byte indicates > 16MB
        test    dh, 0F0h            ; Check bits 20-23
        jnz     .above_16mb
        
        ; Buffer is safe
        clc
        jmp     .done
        
.boundary_crossed:
.above_16mb:
        stc                         ; Set carry = unsafe
        
.done:
        pop     dx
        pop     ax
        ret
dma_check_fallback ENDP

;-----------------------------------------------------------------------------
; PIC interrupt masking fallback
;-----------------------------------------------------------------------------
int_mask_fallback PROC NEAR
        push    ax
        
        ; Mask interrupt on 8259 PIC
        ; Input: BL = IRQ number (0-15)
        cmp     bl, 8
        jb      .master_pic
        
        ; Slave PIC (IRQ 8-15)
        sub     bl, 8
        mov     al, 1
        shl     al, bl              ; Create mask bit
        in      al, 0A1h            ; Read slave mask
        or      al, al              ; Set IRQ bit
        out     0A1h, al            ; Write back
        jmp     .done
        
.master_pic:
        ; Master PIC (IRQ 0-7)
        mov     al, 1
        shl     al, bl              ; Create mask bit
        in      al, 21h             ; Read master mask
        or      al, al              ; Set IRQ bit
        out     21h, al             ; Write back
        
.done:
        pop     ax
        ret
int_mask_fallback ENDP

;-----------------------------------------------------------------------------
; PIC interrupt unmasking fallback
;-----------------------------------------------------------------------------
int_unmask_fallback PROC NEAR
        push    ax
        push    cx
        
        ; Unmask interrupt on 8259 PIC
        ; Input: BL = IRQ number (0-15)
        cmp     bl, 8
        jb      .master_pic
        
        ; Slave PIC (IRQ 8-15)
        sub     bl, 8
        mov     cl, bl
        mov     al, 1
        shl     al, cl              ; Create mask bit
        not     al                  ; Invert for clearing
        in      al, 0A1h            ; Read slave mask
        and     al, al              ; Clear IRQ bit
        out     0A1h, al            ; Write back
        jmp     .done
        
.master_pic:
        ; Master PIC (IRQ 0-7)
        mov     cl, bl
        mov     al, 1
        shl     al, cl              ; Create mask bit
        not     al                  ; Invert for clearing
        in      al, 21h             ; Read master mask
        and     al, al              ; Clear IRQ bit
        out     21h, al             ; Write back
        
.done:
        pop     cx
        pop     ax
        ret
int_unmask_fallback ENDP

;=============================================================================
; OPTIMIZED PATCH TEMPLATES (5-byte replacements)
;=============================================================================

.DATA

; NOP sled for disabled checks
PUBLIC patch_nop_5byte
patch_nop_5byte         db 90h, 90h, 90h, 90h, 90h

; Direct far call for essential checks (no patch needed)
PUBLIC patch_call_direct
patch_call_direct       db 0E8h, ?, ?, 90h, 90h    ; CALL rel16 + 2 NOPs

; Conditional jump over check (for known-safe environments)
PUBLIC patch_skip_check
patch_skip_check        db 0EBh, 03h, 90h, 90h, 90h ; JMP +3 + 3 NOPs

; WBINVD for cache flush (486+ in ring 0)
PUBLIC patch_wbinvd
patch_wbinvd           db 0Fh, 09h, 90h, 90h, 90h  ; WBINVD + 3 NOPs

; CLFLUSH for selective flush (P4+)
PUBLIC patch_clflush
patch_clflush          db 26h, 0Fh, 0AEh, 3Dh, 90h ; CLFLUSH ES:[DI] + NOP

_TEXT ENDS
END