;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file mod_cache_clflush.asm
;; @brief CLFLUSH-based cache line flush module for Pentium+ CPUs
;;
;; JIT-assembled runtime module for 16-bit DOS packet driver TSR.
;; Uses CLFLUSH to flush individual cache lines covering a buffer range,
;; providing fine-grained cache coherency without flushing the entire cache.
;;
;; CLFLUSH is available on Pentium 4, Pentium M, and later CPUs.
;; Detected via CPUID.01h:EDX bit 19.
;;
;; Last Updated: 2026-02-01 11:02:20 CET
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16

%include "patch_macros.inc"

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Constants
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
CPU_REQ         equ 4               ; Pentium+ (CLFLUSH requires SSE2-era CPU)
CAP_FLAGS       equ 0x0008          ; CLFLUSH capability
PATCH_COUNT     equ 1               ; Cache line size patch point

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Module segment
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
section .text class=MODULE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 64-byte module header
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _mod_cache_clflush_header
_mod_cache_clflush_header:
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
;; cache_flush_pre_dma - Flush cache lines covering buffer before DMA
;;
;; Writes back and invalidates each cache line in the specified buffer
;; range so the DMA controller reads coherent memory.
;;
;; Input:
;;   DS:BX = buffer linear address (aligned or unaligned)
;;   CX    = buffer length in bytes
;; Output:
;;   None
;; Clobbers: EAX, CX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
cache_flush_pre_dma:
    push    bx

    ;; Calculate end address
    mov     ax, bx
    add     ax, cx                  ; AX = end address

    ;; Align start down to cache line boundary
    PATCH_POINT pp_cache_line_size
    mov     cl, 32                  ; Placeholder: patched to cache line size (IMM8)

    push    ax                      ; Save end address
    mov     ax, bx
    mov     ch, cl
    dec     ch                      ; CH = cache_line_size - 1 (mask)
    not     ch
    and     al, ch                  ; Align start down to cache line
    mov     bx, ax                  ; BX = aligned start

    pop     ax                      ; AX = end address

    ;; Flush loop: CLFLUSH each cache line from start to end
cache_flush_loop:
    cmp     bx, ax
    jae     cache_flush_done

    ;; CLFLUSH [bx] - flush cache line at DS:BX
    ;; In 16-bit mode, we use the address in BX
    ;; Encoding: 0F AE /7 with mod=00 rm=111 -> 0F AE 3F for [BX]
    ;; But standard CLFLUSH uses [eax], so we load EAX first
    db 0x66                         ; Operand size prefix for 32-bit
    xor     ax, ax                  ; Clear high word of EAX
    mov     ax, bx                  ; AX = address (low 16 bits)
    db 0x0F, 0xAE, 0x38            ; CLFLUSH [eax] (address-size is 16-bit)

    add     bx, cx                  ; Advance by cache line size
    jmp     cache_flush_loop

cache_flush_done:
    pop     bx
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; cache_invalidate_post_dma - Invalidate cache lines after DMA completes
;;
;; Invalidates cache lines covering the buffer so CPU reads fresh data
;; written by the DMA controller.
;;
;; Input:
;;   DS:BX = buffer linear address
;;   CX    = buffer length in bytes
;; Output:
;;   None
;; Clobbers: EAX, CX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
cache_invalidate_post_dma:
    push    bx

    ;; Calculate end address
    mov     ax, bx
    add     ax, cx

    ;; Align start down to cache line boundary
    mov     cl, 32                  ; Patched to cache line size
    mov     ch, cl
    dec     ch
    not     ch
    push    ax
    mov     ax, bx
    and     al, ch
    mov     bx, ax
    pop     ax

    ;; Invalidate loop: CLFLUSH each cache line
cache_inv_loop:
    cmp     bx, ax
    jae     cache_inv_done

    db 0x66
    xor     ax, ax
    mov     ax, bx
    db 0x0F, 0xAE, 0x38            ; CLFLUSH [eax]

    add     bx, cx
    jmp     cache_inv_loop

cache_inv_done:
    ;; Memory fence to ensure all flushes complete before continuing
    ;; MFENCE encoding: 0F AE F0
    db 0x0F, 0xAE, 0xF0            ; MFENCE

    pop     bx
    ret

hot_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Patch table
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
patch_table:
    PATCH_TABLE_ENTRY pp_cache_line_size, 7 ; IMM8 - cache line size (32 or 64)
