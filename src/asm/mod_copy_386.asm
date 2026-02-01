;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file mod_copy_386.asm
;; @brief 386 packet copy module using REP MOVSD (32-bit)
;;
;; JIT-assembled runtime module for 16-bit DOS packet driver TSR.
;; Dword-at-a-time copy using REP MOVSD via operand-size prefix,
;; providing ~4x throughput over REP MOVSB on 386+ CPUs.
;;
;; In 16-bit mode, REP MOVSD is achieved by placing the 0x66 operand
;; size prefix before REP MOVSW, which promotes the operation to 32-bit.
;;
;; Last Updated: 2026-02-01 11:02:20 CET
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16

%include "patch_macros.inc"

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Constants
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
CPU_REQ         equ 2               ; 386 required
CAP_FLAGS       equ 0x0000          ; No special capabilities
PATCH_COUNT     equ 0               ; No patch points

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Module segment
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
section .text class=MODULE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 64-byte module header
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _mod_copy_386_header
_mod_copy_386_header:
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

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; packet_copy - Copy packet data dword-at-a-time (386 optimized)
;;
;; Aligns to dword boundary, performs bulk REP MOVSD (32-bit) transfer,
;; then handles remaining bytes. ~4x faster than MOVSB on 386+.
;;
;; Input:
;;   DS:SI = source buffer
;;   ES:DI = destination buffer
;;   CX    = byte count to copy
;; Output:
;;   DS:SI = updated past end of source data
;;   ES:DI = updated past end of destination data
;;   CX    = 0
;; Clobbers: AX, CX, SI, DI
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
packet_copy:
    cld                             ; Ensure forward direction
    jcxz    .done                   ; Exit if count is zero

    ;; Handle leading bytes to align to dword boundary
    mov     ax, si
    and     ax, 3                   ; Misalignment = SI mod 4
    jz      .aligned                ; Already aligned

    ;; Copy 1-3 bytes to reach dword alignment
    mov     dx, 4
    sub     dx, ax                  ; DX = bytes needed for alignment
    cmp     dx, cx
    jbe     .align_ok
    mov     dx, cx                  ; Don't exceed total count
.align_ok:
    sub     cx, dx                  ; Reduce remaining count
    xchg    cx, dx                  ; CX = align bytes, DX = remaining
    rep movsb                       ; Copy alignment bytes
    mov     cx, dx                  ; CX = remaining bytes

.aligned:
    ;; Bulk dword copy using operand-size prefix
    mov     ax, cx
    shr     cx, 2                   ; CX = dword count
    jcxz    .remainder              ; Skip if fewer than 4 bytes remain

    ;; REP MOVSD in 16-bit mode: 0x66 prefix promotes MOVSW to MOVSD
    db 0x66                         ; Operand size prefix (16->32 bit)
    rep movsw                       ; Becomes REP MOVSD with 0x66 prefix

.remainder:
    ;; Handle remaining 0-3 bytes
    mov     cx, ax
    and     cx, 3                   ; CX = remaining bytes (0-3)
    jcxz    .done
    rep movsb                       ; Copy remaining bytes

.done:
    ret

hot_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Patch table (empty)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
patch_table:
