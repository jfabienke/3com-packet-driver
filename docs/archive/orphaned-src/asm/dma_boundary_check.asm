;-----------------------------------------------------------------------------
; @file dma_boundary_check.asm
; @brief DMA boundary validation for ISA bus limitations
;
; GPT-5 Requirement: Complete DMA boundary checking (64KB + 16MB)
; 
; ISA DMA controllers have two critical limitations:
; 1. Cannot cross 64KB boundaries (8237 DMA controller limitation)
; 2. Cannot access above 16MB (ISA bus 24-bit address limitation)
;
; This module provides comprehensive boundary checking with SMC optimization
;-----------------------------------------------------------------------------

.MODEL SMALL
.386

include 'patch_macros.inc'

_TEXT SEGMENT
        ASSUME  CS:_TEXT

;=============================================================================
; PUBLIC FUNCTIONS
;=============================================================================

;-----------------------------------------------------------------------------
; validate_dma_buffer - Complete DMA buffer validation
; 
; Input:  ES:DI = Buffer address
;         CX = Buffer length in bytes
; Output: CF = 0 if buffer is DMA-safe
;         CF = 1 if buffer crosses boundary or exceeds limits
;         AX = Error code if CF=1:
;              1 = Crosses 64KB boundary
;              2 = Exceeds 16MB limit
;              3 = Both violations
; Preserves: All other registers
;-----------------------------------------------------------------------------
PUBLIC validate_dma_buffer
validate_dma_buffer PROC
        push    bx
        push    dx
        push    si
        
        xor     ax, ax              ; Clear error code
        
        ; First check: 64KB boundary crossing
        ; Calculate end address in current segment
        mov     bx, di              ; Start offset
        add     bx, cx              ; Add length
        jnc     .check_16mb         ; No carry = no 64KB crossing
        
        ; 64KB boundary crossed
        or      ax, 1               ; Set bit 0 for 64KB violation
        
.check_16mb:
        ; Second check: 16MB limit (ISA bus limitation)
        ; Convert ES:DI to 20-bit physical address
        mov     dx, es
        mov     si, dx
        shl     dx, 4               ; Segment * 16 (low 16 bits)
        shr     si, 12              ; High 4 bits of segment
        
        ; DX:SI now contains high portion of physical address
        ; Add offset to get full physical address
        add     dx, di
        adc     si, 0               ; Add any carry
        
        ; Check if address exceeds 16MB (0xFFFFFF)
        test    si, si              ; Any bits in high word?
        jnz     .above_16mb
        
        ; Check end of buffer
        add     dx, cx              ; Add buffer length
        adc     si, 0
        test    si, si              ; Check again after adding length
        jnz     .above_16mb
        
        ; Also check if bits 20-23 are set in high byte of DX
        test    dh, 0F0h            ; Bits 20-23
        jz      .check_result       ; All clear, buffer is OK
        
.above_16mb:
        or      ax, 2               ; Set bit 1 for 16MB violation
        
.check_result:
        ; Set carry flag if any violations detected
        test    ax, ax
        jz      .safe
        
        stc                         ; Set carry for unsafe buffer
        jmp     .done
        
.safe:
        clc                         ; Clear carry for safe buffer
        
.done:
        pop     si
        pop     dx
        pop     bx
        ret
validate_dma_buffer ENDP

;-----------------------------------------------------------------------------
; find_safe_dma_buffer - Find next DMA-safe address
;
; Given an unsafe buffer, finds the next safe address that doesn't
; violate DMA constraints.
;
; Input:  ES:DI = Current (unsafe) buffer address
;         CX = Required buffer length
; Output: ES:DI = Next safe buffer address
;         CF = 0 if safe address found
;         CF = 1 if no safe address available
;-----------------------------------------------------------------------------
PUBLIC find_safe_dma_buffer
find_safe_dma_buffer PROC
        push    ax
        push    bx
        push    dx
        
        ; Strategy: Align to next 64KB boundary if needed
        mov     ax, di
        add     ax, cx
        jnc     .check_16mb_align   ; No 64KB crossing needed
        
        ; Align to next 64KB boundary
        xor     di, di              ; Start of next 64KB segment
        add     es, 1000h           ; Move to next segment (64KB)
        
.check_16mb_align:
        ; Verify we haven't exceeded 16MB
        mov     dx, es
        shl     dx, 4
        add     dx, di
        jc      .no_safe_buffer     ; Overflow = exceeded 16MB
        
        ; Check high bits
        test    dh, 0F0h
        jnz     .no_safe_buffer
        
        ; Found safe buffer
        clc
        jmp     .done
        
.no_safe_buffer:
        stc
        
.done:
        pop     dx
        pop     bx
        pop     ax
        ret
find_safe_dma_buffer ENDP

;-----------------------------------------------------------------------------
; split_dma_transfer - Split transfer at DMA boundaries
;
; For transfers that cross boundaries, splits them into safe chunks
;
; Input:  ES:DI = Buffer address
;         CX = Total transfer length
; Output: CX = Safe transfer length (may be less than requested)
;         DX = Remaining bytes after this transfer
;-----------------------------------------------------------------------------
PUBLIC split_dma_transfer
split_dma_transfer PROC
        push    ax
        push    bx
        
        ; Calculate bytes until 64KB boundary
        mov     ax, di
        neg     ax                  ; Distance to boundary
        jz      .at_boundary        ; Already at boundary
        
        ; AX = bytes until boundary
        cmp     ax, cx
        jae     .no_split           ; Transfer fits
        
        ; Need to split at boundary
        mov     dx, cx
        sub     dx, ax              ; Remaining after split
        mov     cx, ax              ; This transfer size
        jmp     .done
        
.at_boundary:
        ; At boundary, can transfer up to 64KB
        mov     ax, 0FFFFh          ; Max 64KB - 1
        cmp     cx, ax
        jbe     .no_split
        
        ; Split at 64KB
        mov     dx, cx
        sub     dx, ax
        mov     cx, ax
        jmp     .done
        
.no_split:
        xor     dx, dx              ; No remaining bytes
        
.done:
        pop     bx
        pop     ax
        ret
split_dma_transfer ENDP

;=============================================================================
; PATCHABLE ENTRY POINTS FOR SMC OPTIMIZATION
;=============================================================================

;-----------------------------------------------------------------------------
; Patchable DMA validation
; Can be optimized to NOP for PCI devices with bus mastering
;-----------------------------------------------------------------------------
PUBLIC patch_dma_validate
patch_dma_validate:
        PATCH_POINT_CALL dma_validate_full, validate_dma_buffer

;-----------------------------------------------------------------------------
; Patchable boundary check
; Can be simplified for 32-bit capable buses
;-----------------------------------------------------------------------------
PUBLIC patch_boundary_check
patch_boundary_check:
        PATCH_POINT_CALL boundary_check_full, boundary_check_simple

;=============================================================================
; OPTIMIZED IMPLEMENTATIONS
;=============================================================================

;-----------------------------------------------------------------------------
; Simple boundary check for PCI (no 16MB limit)
;-----------------------------------------------------------------------------
boundary_check_simple PROC NEAR
        push    ax
        
        ; Only check 64KB boundary for PCI
        mov     ax, di
        add     ax, cx
        jc      .unsafe
        
        clc
        jmp     .done
        
.unsafe:
        stc
        
.done:
        pop     ax
        ret
boundary_check_simple ENDP

;-----------------------------------------------------------------------------
; Full boundary check (ISA compatibility)
;-----------------------------------------------------------------------------
boundary_check_full PROC NEAR
        call    validate_dma_buffer
        ret
boundary_check_full ENDP

;=============================================================================
; DIAGNOSTIC FUNCTIONS
;=============================================================================

;-----------------------------------------------------------------------------
; Report DMA statistics
;-----------------------------------------------------------------------------
PUBLIC dma_boundary_stats
dma_boundary_stats:
        .crosses_64kb   dw 0        ; Count of 64KB crossings
        .exceeds_16mb   dw 0        ; Count of 16MB violations
        .splits_done    dw 0        ; Count of split transfers
        .safe_buffers   dw 0        ; Count of safe buffers

PUBLIC report_dma_stats
report_dma_stats PROC
        push    ax
        push    dx
        
        ; Return stats in registers
        mov     ax, [dma_boundary_stats.crosses_64kb]
        mov     bx, [dma_boundary_stats.exceeds_16mb]
        mov     cx, [dma_boundary_stats.splits_done]
        mov     dx, [dma_boundary_stats.safe_buffers]
        
        pop     dx
        pop     ax
        ret
report_dma_stats ENDP

_TEXT ENDS
END