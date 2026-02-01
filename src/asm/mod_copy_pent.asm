;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file mod_copy_pent.asm
;; @brief Pentium-optimized packet copy module
;;
;; JIT-assembled runtime module for 16-bit DOS packet driver TSR.
;; Uses REP MOVSD with dword alignment and Pentium pipeline optimization.
;; Pre-reads the next cache line while copying to minimize stalls.
;;
;; On Pentium, REP MOVSD with aligned source and destination achieves
;; near-burst transfer rates. The prefetch hint (reading ahead) helps
;; keep the pipeline fed on cache misses.
;;
;; Last Updated: 2026-02-01 11:02:20 CET
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16

%include "patch_macros.inc"

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Constants
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
CPU_REQ         equ 4               ; Pentium required
CAP_FLAGS       equ 0x0000          ; No special capabilities
PATCH_COUNT     equ 0               ; No patch points
CACHE_LINE      equ 32              ; Pentium L1 cache line size

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Module segment
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
section .text class=MODULE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 64-byte module header
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _mod_copy_pent_header
_mod_copy_pent_header:
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
;; packet_copy - Pentium-optimized packet copy with prefetch hint
;;
;; Aligns to dword boundary, pre-reads the first cache line ahead,
;; then uses REP MOVSD for maximum throughput. Handles tail bytes.
;;
;; Input:
;;   DS:SI = source buffer
;;   ES:DI = destination buffer
;;   CX    = byte count to copy
;; Output:
;;   DS:SI = updated past end of source data
;;   ES:DI = updated past end of destination data
;;   CX    = 0
;; Clobbers: EAX, CX, SI, DI
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
packet_copy:
    cld                             ; Ensure forward direction
    jcxz    .done                   ; Exit if count is zero

    ;; Prefetch hint: read first cache line of source into cache
    ;; On Pentium, a simple MOV triggers a cache line fill
    mov     al, [ds:si]             ; Touch first byte -> fills cache line
    cmp     cx, CACHE_LINE
    jbe     .small_copy
    mov     al, [ds:si+CACHE_LINE]  ; Pre-read next cache line

.small_copy:
    ;; Handle leading bytes to align to dword boundary
    mov     ax, si
    and     ax, 3
    jz      .aligned

    mov     dx, 4
    sub     dx, ax                  ; Bytes to alignment
    cmp     dx, cx
    jbe     .align_ok
    mov     dx, cx
.align_ok:
    sub     cx, dx
    xchg    cx, dx
    rep movsb                       ; Align to dword
    mov     cx, dx

.aligned:
    ;; Bulk dword copy with pipeline optimization
    ;; On Pentium, REP MOVSD with aligned operands uses burst mode
    mov     ax, cx
    shr     cx, 2                   ; CX = dword count
    jcxz    .tail

    ;; REP MOVSD: 0x66 prefix promotes MOVSW to MOVSD in 16-bit mode
    db 0x66                         ; Operand size prefix
    rep movsw                       ; Becomes REP MOVSD

.tail:
    ;; Handle remaining 0-3 bytes
    mov     cx, ax
    and     cx, 3
    jcxz    .done
    rep movsb

.done:
    ret

hot_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Patch table (empty)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
patch_table:
