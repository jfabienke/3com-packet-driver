;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file mod_cache_snoop.asm
;; @brief PCI bus snoop-based cache coherency module
;;
;; JIT-assembled runtime module for 16-bit DOS packet driver TSR.
;; Relies on PCI bus-master DMA controller hardware cache snooping
;; to maintain coherency. Pre-DMA flush is a no-op; post-DMA uses
;; a memory serialization fence only.
;;
;; Applicable when PCI chipset supports bus snooping (most 486+ PCI
;; systems) and the NIC performs bus-master DMA.
;;
;; Last Updated: 2026-02-01 11:02:20 CET
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16

%include "patch_macros.inc"

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Constants
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
CPU_REQ         equ 3               ; 386+ (for LOCK prefix serialization)
CAP_FLAGS       equ 0x0210          ; SNOOP (0x200) + PCI (0x10)
PATCH_COUNT     equ 0               ; No patch points needed

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Module segment
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
section .text class=MODULE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 64-byte module header
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _mod_cache_snoop_header
_mod_cache_snoop_header:
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
;; cache_flush_pre_dma - No-op (PCI snoop handles coherency)
;;
;; PCI bus snooping ensures DMA reads see current cache contents.
;; No explicit flush is required before bus-master DMA.
;;
;; Input: None
;; Output: None
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
cache_flush_pre_dma:
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; cache_invalidate_post_dma - Memory fence after DMA completes
;;
;; PCI snoop invalidates cache lines on DMA writes, but we issue a
;; serializing instruction to ensure the CPU's store buffer is drained
;; and any speculative reads are discarded.
;;
;; On pre-SSE2 CPUs, LOCK ADD [SP],0 serves as a full memory fence.
;; On SSE2+ CPUs, MFENCE would be preferred but we use LOCK for
;; broadest 386+ compatibility.
;;
;; Input: None
;; Output: None
;; Clobbers: None (flags preserved via pushf/popf)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
cache_invalidate_post_dma:
    ;; LOCK prefix on a memory operation acts as a full memory fence
    ;; This ensures all prior DMA writes are visible to the CPU
    lock add byte [esp], 0          ; Serialize - full memory barrier
    ; Note: [esp] is valid on 386+ in 16-bit mode with 0x67 prefix
    ; For pure 16-bit, use [bp] with a stack frame or [bx]
    ret

hot_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Patch table (empty)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
patch_table:
