;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file mod_dma_descring.asm
;; @brief PCI DMA descriptor ring management for Boomerang/Cyclone/Tornado
;;
;; JIT-assembled runtime module for 16-bit DOS packet driver TSR.
;; Manages upload (RX) and download (TX) descriptor rings using 32-bit
;; physical addresses. Requires 386+ for USE32 prefix instructions.
;;
;; Descriptor format (UPD/DPD - 16 bytes each):
;;   +0  dd  DnNextPtr / UpNextPtr    (physical addr of next descriptor)
;;   +4  dd  FrameStartHeader / UpPktStatus
;;   +8  dd  DataAddr                 (physical addr of buffer)
;;  +12  dd  DataLength | flags       (length + last-frag flag)
;;
;; Last Updated: 2026-02-01 11:02:20 CET
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16

%include "patch_macros.inc"

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Constants
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
CPU_REQ         equ 3               ; 386 required for 32-bit operations
CAP_FLAGS       equ 0x0102          ; BUSMASTER + DESCRIPTOR_RING
PATCH_COUNT     equ 2               ; ring base address, ring size

;; Descriptor ring parameters
DESC_SIZE       equ 16              ; Each UPD/DPD is 16 bytes
MAX_RING_SIZE   equ 16              ; Default max descriptors per ring

;; 3Com register offsets
REG_UP_LIST_PTR equ 0x38
REG_UP_PKT_STAT equ 0x30
REG_DN_LIST_PTR equ 0x24
REG_CMD         equ 0x0E

;; UpPktStatus bits
UP_COMPLETE     equ 0x8000          ; Upload complete flag
UP_ERROR        equ 0x4000          ; Upload error flag

;; DPD FrameStartHeader bits
DN_INDICATE     equ 0x80000000      ; Request TX complete indication
DN_DN_COMPLETE  equ 0x00010000      ; Download complete (read from status)

;; Fragment descriptor flags
LAST_FRAG       equ 0x80000000      ; Last fragment flag in DataLength

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Module segment
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
section .text class=MODULE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 64-byte module header
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _mod_dma_descring_header
_mod_dma_descring_header:
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
;; Module data (within hot section for locality)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
hot_start:

;; Ring state variables
rx_head_idx:    dw 0                ; Current RX descriptor index
rx_ring_count:  dw 0                ; Number of RX descriptors
tx_head_idx:    dw 0                ; Next TX descriptor to submit
tx_tail_idx:    dw 0                ; Next TX descriptor to reclaim
tx_ring_count:  dw 0                ; Number of TX descriptors

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; descring_init_rx - Initialize upload (RX) descriptor ring
;;
;; Input:
;;   ES:BX = pointer to descriptor ring memory (must be dword-aligned)
;;   CX    = number of descriptors
;;   DS:SI = array of CX physical buffer addresses (dd each)
;; Output:
;;   CF = 0 on success
;; Clobbers: AX, BX, CX, SI, DI
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
descring_init_rx:
    push    bp
    mov     bp, sp

    mov     [rx_ring_count], cx
    mov     word [rx_head_idx], 0

    ;; Ring base address (patched at runtime)
    PATCH_POINT pp_ring_base
    mov     di, bx                  ; DI = current descriptor offset

    ;; Initialize each UPD in the ring
    mov     ax, cx                  ; Save count
dr_init_rx_loop:
    ;; UpNextPtr: point to next descriptor (circular)
    ;; Calculate next descriptor physical address
    ;; Next = base + ((current_index + 1) % count) * DESC_SIZE
    push    cx
    push    ax

    ;; Write DnNextPtr using 32-bit store (USE32 prefix in 16-bit mode)
    ;; For simplicity, write low word of next descriptor offset
    mov     ax, di
    add     ax, DESC_SIZE           ; Point to next descriptor
    db 0x66                         ; Operand size prefix for 32-bit
    mov     [es:di], ax             ; Store UpNextPtr (low 16 bits sufficient for ring)

    ;; UpPktStatus: clear (ready for hardware)
    db 0x66
    xor     ax, ax
    db 0x66
    mov     [es:di+4], ax           ; Clear UpPktStatus

    ;; DataAddr: copy from buffer address array
    db 0x66
    mov     ax, [ds:si]             ; Load 32-bit physical buffer address
    db 0x66
    mov     [es:di+8], ax           ; Store DataAddr

    ;; DataLength: set buffer size with LAST_FRAG flag (single fragment)
    db 0x66
    mov     ax, 1536                ; Max Ethernet frame size
    db 0x66
    or      ax, word [cs:dr_last_frag_const]
    db 0x66
    mov     [es:di+12], ax          ; Store DataLength | LAST_FRAG

    add     di, DESC_SIZE           ; Advance to next descriptor
    add     si, 4                   ; Advance to next buffer address
    pop     ax
    pop     cx
    loop    dr_init_rx_loop

    ;; Fix last descriptor to point back to first (circular ring)
    sub     di, DESC_SIZE           ; Back to last descriptor
    mov     ax, bx                  ; First descriptor offset
    db 0x66
    mov     [es:di], ax             ; Last->NextPtr = first

    pop     bp
    clc
    ret

dr_last_frag_const:
    dd 0x80000000                   ; LAST_FRAG flag constant

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; descring_init_tx - Initialize download (TX) descriptor ring
;;
;; Input:
;;   ES:BX = pointer to descriptor ring memory (must be dword-aligned)
;;   CX    = number of descriptors
;; Output:
;;   CF = 0 on success
;; Clobbers: AX, BX, CX, DI
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
descring_init_tx:
    mov     [tx_ring_count], cx
    mov     word [tx_head_idx], 0
    mov     word [tx_tail_idx], 0

    mov     di, bx

    ;; Zero out all TX descriptors (DnNextPtr=0, all fields clear)
dr_init_tx_loop:
    db 0x66
    xor     ax, ax
    db 0x66
    mov     [es:di], ax             ; DnNextPtr = 0 (end of list)
    db 0x66
    mov     [es:di+4], ax           ; FrameStartHeader = 0
    db 0x66
    mov     [es:di+8], ax           ; DataAddr = 0
    db 0x66
    mov     [es:di+12], ax          ; DataLength = 0

    add     di, DESC_SIZE
    loop    dr_init_tx_loop

    clc
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; descring_advance_rx - Advance to next RX descriptor, check completion
;;
;; Input: None (uses internal ring state)
;; Output:
;;   CF = 0 if packet available, CF = 1 if no packet ready
;;   If CF=0:
;;     ES:BX = pointer to completed descriptor
;;     CX    = received packet length
;; Clobbers: AX, BX, CX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
descring_advance_rx:
    ;; Calculate current descriptor address
    mov     bx, [rx_head_idx]
    mov     ax, DESC_SIZE
    mul     bx                      ; AX = byte offset into ring
    ;; Add ring base (would be patched/stored)
    mov     bx, ax                  ; BX = descriptor offset

    ;; Check UpPktStatus for completion
    db 0x66
    mov     ax, [es:bx+4]          ; Load UpPktStatus (32-bit)
    test    ax, UP_COMPLETE         ; Check completion bit (in low word)
    jz      dr_rx_not_ready

    ;; Extract packet length from UpPktStatus[12:0]
    mov     cx, ax
    and     cx, 0x1FFF              ; Mask length field

    ;; Advance head index (wrapping)
    mov     ax, [rx_head_idx]
    inc     ax
    cmp     ax, [rx_ring_count]
    jb      dr_rx_no_wrap
    xor     ax, ax                  ; Wrap to 0
dr_rx_no_wrap:
    mov     [rx_head_idx], ax

    ;; Clear UpPktStatus for reuse
    db 0x66
    xor     ax, ax
    db 0x66
    mov     [es:bx+4], ax

    clc                             ; Packet available
    ret

dr_rx_not_ready:
    stc                             ; No packet ready
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; descring_submit_tx - Submit a TX descriptor and kick DnListPtr
;;
;; Input:
;;   DX:AX = 32-bit physical address of packet buffer
;;   CX    = packet length
;;   BX    = io_base for NIC registers
;; Output:
;;   CF = 0 on success, CF = 1 if ring full
;; Clobbers: AX, DI
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
descring_submit_tx:
    push    dx
    push    bx

    ;; Check if ring is full
    mov     di, [tx_head_idx]
    push    di
    inc     di
    cmp     di, [tx_ring_count]
    jb      dr_tx_no_wrap
    xor     di, di
dr_tx_no_wrap:
    cmp     di, [tx_tail_idx]      ; If head+1 == tail, ring is full
    je      dr_tx_ring_full

    ;; Calculate descriptor address
    pop     di                      ; Restore original head index
    push    ax                      ; Save buffer addr low
    mov     ax, DESC_SIZE
    mul     di                      ; AX = byte offset
    mov     di, ax

    ;; Fill in DPD
    ;; DnNextPtr = 0 (end of chain for now)
    db 0x66
    xor     ax, ax
    db 0x66
    mov     [es:di], ax

    ;; FrameStartHeader: packet length with TX indicate
    mov     ax, cx                  ; Packet length
    db 0x66
    mov     [es:di+4], ax

    ;; DataAddr: physical buffer address
    pop     ax                      ; Low word of buffer address
    db 0x66
    mov     [es:di+8], ax           ; 32-bit physical address

    ;; DataLength: length | LAST_FRAG
    mov     ax, cx
    db 0x66
    or      ax, word [cs:dr_last_frag_const]
    db 0x66
    mov     [es:di+12], ax

    ;; Advance head index
    mov     ax, [tx_head_idx]
    inc     ax
    cmp     ax, [tx_ring_count]
    jb      dr_tx_head_ok
    xor     ax, ax
dr_tx_head_ok:
    mov     [tx_head_idx], ax

    ;; Kick DnListPtr register to start download
    pop     bx                      ; io_base
    mov     dx, bx
    add     dx, REG_DN_LIST_PTR
    ;; Write descriptor physical address to DnListPtr
    ;; (For real use, would need actual physical address of descriptor)
    mov     ax, di                  ; Descriptor offset (simplified)
    out     dx, ax

    pop     dx
    clc
    ret

dr_tx_ring_full:
    pop     di                      ; Balance stack
    pop     bx
    pop     dx
    stc
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; descring_reclaim_tx - Reclaim completed TX descriptors
;;
;; Input: None (uses internal ring state)
;; Output:
;;   AX = number of descriptors reclaimed
;; Clobbers: AX, BX, CX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
descring_reclaim_tx:
    xor     cx, cx                  ; Reclaim count

dr_reclaim_loop:
    mov     bx, [tx_tail_idx]
    cmp     bx, [tx_head_idx]      ; tail == head means ring empty
    je      dr_reclaim_done

    ;; Calculate descriptor address
    mov     ax, DESC_SIZE
    mul     bx
    mov     bx, ax

    ;; Check FrameStartHeader for DN_COMPLETE
    db 0x66
    mov     ax, [es:bx+4]
    db 0x66
    test    ax, word [cs:dr_dn_complete_const]
    jz      dr_reclaim_done        ; Not complete yet, stop

    ;; Clear descriptor
    db 0x66
    xor     ax, ax
    db 0x66
    mov     [es:bx+4], ax

    ;; Advance tail
    mov     ax, [tx_tail_idx]
    inc     ax
    cmp     ax, [tx_ring_count]
    jb      dr_reclaim_no_wrap
    xor     ax, ax
dr_reclaim_no_wrap:
    mov     [tx_tail_idx], ax
    inc     cx
    jmp     dr_reclaim_loop

dr_reclaim_done:
    mov     ax, cx
    ret

dr_dn_complete_const:
    dd 0x00010000                   ; DN_COMPLETE flag

hot_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Patch table
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
PATCH_RING_SIZE equ 7               ; IMM8 type
patch_table:
    PATCH_TABLE_ENTRY pp_ring_base, 6    ; IMM16 - ring base address
    PATCH_TABLE_ENTRY pp_ring_base, PATCH_RING_SIZE ; IMM8 - ring size
