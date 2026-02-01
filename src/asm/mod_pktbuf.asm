;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file mod_pktbuf.asm
;; @brief Ring buffer management for RX packets (JIT-assembled)
;;
;; Implements a circular ring buffer for received packets. Provides alloc,
;; free, full, and empty checks. Packet copy and checksum processing are
;; performed via patchable calls (CPU-specific optimizations).
;;
;; Hot path size target: ~1.0KB
;;
;; Last Updated: 2026-02-01 11:02:20 CET
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16
cpu 286

%include "patch_macros.inc"

;; ============================================================================
;; Patch type constants (local aliases)
;; ============================================================================
PATCH_TYPE_IMM16        equ 6
PATCH_TYPE_IMM8         equ 7
PATCH_TYPE_RELOC_NEAR   equ 8

;; ============================================================================
;; Ring buffer constants
;; ============================================================================
RING_SLOT_SIZE          equ 1520        ; Max Ethernet frame + 6 byte header
DEFAULT_RING_SLOTS      equ 8           ; Default number of ring slots

;; Ring slot header layout (6 bytes prepended to each slot):
;;   +0: word  packet_length
;;   +2: word  flags (bit 0 = occupied)
;;   +4: word  reserved

;; ############################################################################
;; MODULE SEGMENT
;; ############################################################################
segment MODULE class=MODULE align=16

;; ============================================================================
;; 64-byte Module Header
;; ============================================================================
global _mod_pktbuf_header
_mod_pktbuf_header:
    db  'PKTDRV', 0             ; +00  7 bytes: module signature
    db  1                       ; +07  1 byte:  major version
    db  0                       ; +08  1 byte:  minor version
    db  0                       ; +09  1 byte:  cpu_req
    db  0                       ; +0A  1 byte:  nic_type
    db  0                       ; +0B  1 byte:  cap_flags
    dw  hot_end - hot_start     ; +0C  2 bytes: hot code size
    dw  patch_table             ; +0E  2 bytes: offset to patch table
    dw  patch_table_end - patch_table  ; +10  2 bytes: patch table size
    dw  hot_start               ; +12  2 bytes: offset to hot_start
    dw  hot_end                 ; +14  2 bytes: offset to hot_end
    times 64 - ($ - _mod_pktbuf_header) db 0  ; Pad to 64 bytes

;; ============================================================================
;; HOT PATH START
;; ============================================================================
hot_start:

;; ----------------------------------------------------------------------------
;; ring_alloc - Allocate next ring buffer slot
;;
;; Input:  none
;; Output: ES:DI -> allocated slot data area (after 6-byte header)
;;         CX    =  max payload size (RING_SLOT_SIZE - 6)
;;         CF    =  set if ring full (DI undefined)
;; Clobbers: AX, BX
;; ----------------------------------------------------------------------------
global ring_alloc
ring_alloc:
    ;; Check if ring is full first
    call    ring_is_full
    jc      .alloc_fail

    ;; Calculate slot address: base + head * RING_SLOT_SIZE
    mov     ax, [ring_head]
    mov     bx, RING_SLOT_SIZE
    mul     bx                              ; DX:AX = head * slot_size
    lea     di, [ring_buffer]
    add     di, ax                          ; DI -> slot start

    ;; Mark slot as occupied
    mov     word [di+2], 1                  ; flags = occupied
    mov     word [di], 0                    ; length = 0 (caller fills)

    ;; Advance head pointer (wrap around)
    mov     ax, [ring_head]
    inc     ax
    cmp     ax, [ring_slots]
    jb      .no_head_wrap
    xor     ax, ax
.no_head_wrap:
    mov     [ring_head], ax

    ;; Return pointer past slot header
    add     di, 6                           ; Skip slot header
    push    cs
    pop     es                              ; ES = our segment
    mov     cx, RING_SLOT_SIZE - 6          ; Max payload
    clc
    ret

.alloc_fail:
    stc
    ret

;; ----------------------------------------------------------------------------
;; ring_free - Release the tail ring buffer slot
;;
;; Input:  none
;; Output: CF = set if ring empty (nothing to free)
;; Clobbers: AX, BX, DI
;; ----------------------------------------------------------------------------
global ring_free
ring_free:
    ;; Check if ring is empty
    call    ring_is_empty
    jc      .free_fail

    ;; Calculate tail slot address
    mov     ax, [ring_tail]
    mov     bx, RING_SLOT_SIZE
    mul     bx
    lea     di, [ring_buffer]
    add     di, ax

    ;; Clear occupied flag
    mov     word [di+2], 0

    ;; Advance tail pointer (wrap around)
    mov     ax, [ring_tail]
    inc     ax
    cmp     ax, [ring_slots]
    jb      .no_tail_wrap
    xor     ax, ax
.no_tail_wrap:
    mov     [ring_tail], ax

    clc
    ret

.free_fail:
    stc
    ret

;; ----------------------------------------------------------------------------
;; ring_is_full - Check if the ring buffer is full
;;
;; Input:  none
;; Output: CF = set if full
;; Clobbers: AX
;; ----------------------------------------------------------------------------
global ring_is_full
ring_is_full:
    mov     ax, [ring_head]
    inc     ax
    cmp     ax, [ring_slots]
    jb      .no_wrap_check
    xor     ax, ax
.no_wrap_check:
    cmp     ax, [ring_tail]
    je      .is_full
    clc
    ret
.is_full:
    stc
    ret

;; ----------------------------------------------------------------------------
;; ring_is_empty - Check if the ring buffer is empty
;;
;; Input:  none
;; Output: CF = set if empty
;; Clobbers: AX
;; ----------------------------------------------------------------------------
global ring_is_empty
ring_is_empty:
    mov     ax, [ring_head]
    cmp     ax, [ring_tail]
    je      .is_empty
    clc
    ret
.is_empty:
    stc
    ret

;; ----------------------------------------------------------------------------
;; ring_copy_pkt - Copy packet into ring slot with optional checksum
;;
;; Input:  DS:SI -> source packet data
;;         ES:DI -> destination ring slot (from ring_alloc)
;;         CX    =  byte count
;; Output: AX    =  checksum (if computed), 0 otherwise
;; Clobbers: SI, DI, CX
;; ----------------------------------------------------------------------------
global ring_copy_pkt
ring_copy_pkt:
    ;; Invoke CPU-specific packet copy (patched to REP MOVSD on 386+)
    PATCH_POINT_CALL pp_pkt_copy, ring_copy_pkt_copy_fb

    ;; Invoke CPU-specific checksum (patched to optimized routine)
    PATCH_POINT_CALL pp_checksum, ring_copy_pkt_csum_fb

    ret

ring_copy_pkt_copy_fb:
    ;; Generic 286 byte copy
    rep movsb
    ret

ring_copy_pkt_csum_fb:
    ;; No checksum offload - return 0
    xor     ax, ax
    ret

;; ============================================================================
;; Local data (within hot path)
;; ============================================================================
ring_head:
    dw  0                                   ; Current head index
ring_tail:
    dw  0                                   ; Current tail index
ring_slots:
    dw  DEFAULT_RING_SLOTS                  ; Number of slots (set at init)

;; Ring buffer storage starts here - sized at init, minimum 1 slot
ring_buffer:
    times RING_SLOT_SIZE db 0               ; First slot (placeholder)
    ;; Additional slots allocated by init beyond this point

hot_end:

;; ============================================================================
;; PATCH TABLE
;; ============================================================================
patch_table:
    PATCH_TABLE_ENTRY  pp_pkt_copy,  PATCH_TYPE_RELOC_NEAR
    PATCH_TABLE_ENTRY  pp_checksum,  PATCH_TYPE_RELOC_NEAR
patch_table_end:
