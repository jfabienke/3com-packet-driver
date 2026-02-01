;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file mod_dma_bounce.asm
;; @brief Bounce buffer module for DMA above 16MB ISA limit
;;
;; JIT-assembled runtime module for 16-bit DOS packet driver TSR.
;; Provides bounce buffer copy routines for systems where packet buffers
;; reside above the 16MB ISA DMA address limit (24-bit addressing).
;;
;; Last Updated: 2026-02-01 11:02:20 CET
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16

%include "patch_macros.inc"

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Constants
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
CPU_REQ         equ 0               ; 8086 baseline
CAP_FLAGS       equ 0x0020          ; BOUNCE capability
PATCH_COUNT     equ 1               ; Bounce buffer segment/offset

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Module segment
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
section .text class=MODULE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 64-byte module header
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _mod_dma_bounce_header
_mod_dma_bounce_header:
header:
    db 'PKTDRV',0
    db 1, 0
    dw hot_start
    dw hot_end
    dw 0, 0
    dw patch_table
    dw PATCH_COUNT
    dw (hot_end - header)
    dw (hot_end - hot_start)
    db CPU_REQ
    db 0
    dw CAP_FLAGS
    times (64 - ($ - header)) db 0

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Hot section
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
hot_start:

;; Bounce buffer location (patched at runtime)
bounce_seg:     dw 0                ; Segment of bounce buffer
bounce_off:     dw 0                ; Offset of bounce buffer

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; bounce_copy_to - Copy packet data into bounce buffer (for TX DMA)
;;
;; Input:
;;   DS:SI = source packet data
;;   CX    = byte count
;; Output:
;;   ES:DI = bounce buffer address (for DMA setup)
;; Clobbers: AX, CX, SI, DI
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
bounce_copy_to:
    ;; Load bounce buffer address (patched at runtime)
    PATCH_POINT pp_bounce_addr
    mov     ax, [cs:bounce_seg]     ; Load bounce buffer segment
    mov     es, ax
    mov     di, [cs:bounce_off]     ; Load bounce buffer offset

    push    di                      ; Save start of bounce buffer
    cld                             ; Forward direction
    rep movsb                       ; Copy CX bytes: DS:SI -> ES:DI
    pop     di                      ; Restore bounce buffer start
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; bounce_copy_from - Copy received data from bounce buffer (for RX DMA)
;;
;; Input:
;;   ES:DI = destination buffer for received data
;;   CX    = byte count
;; Output:
;;   ES:DI = updated past end of copied data
;; Clobbers: AX, CX, SI, DI, DS
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
bounce_copy_from:
    push    es
    push    di

    ;; Set DS:SI = bounce buffer (source)
    mov     ax, [cs:bounce_seg]
    mov     ds, ax
    mov     si, [cs:bounce_off]

    ;; Restore ES:DI = destination
    pop     di
    pop     es

    cld
    rep movsb                       ; Copy CX bytes: DS:SI -> ES:DI
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; bounce_get_phys - Return physical address of bounce buffer
;;
;; Input: None
;; Output:
;;   DX:AX = 32-bit physical address of bounce buffer
;;           (DX = high 16 bits, AX = low 16 bits)
;; Clobbers: AX, DX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
bounce_get_phys:
    ;; Convert segment:offset to 20-bit physical address
    ;; Physical = (segment << 4) + offset
    mov     ax, [cs:bounce_seg]
    mov     dx, ax
    shr     dx, 12                  ; DX = segment >> 12 (high nibble)
    shl     ax, 4                   ; AX = segment << 4 (shift left for base)
    add     ax, [cs:bounce_off]     ; Add offset
    adc     dx, 0                   ; Carry into high word
    ret

hot_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Patch table
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
patch_table:
    PATCH_TABLE_ENTRY pp_bounce_addr, PATCH_TYPE_COPY ; Bounce buffer seg:off
