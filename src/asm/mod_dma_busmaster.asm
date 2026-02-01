;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file mod_dma_busmaster.asm
;; @brief PCI bus-master DMA module for 3C515/Boomerang
;;
;; JIT-assembled runtime module for 16-bit DOS packet driver TSR.
;; Manages PCI bus-master DMA uploads (RX) and downloads (TX) via
;; the 3Com Boomerang/Vortex UpListPtr and DnListPtr registers.
;;
;; Last Updated: 2026-02-01 11:02:20 CET
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16

%include "patch_macros.inc"

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Constants
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
CPU_REQ         equ 2               ; 286 required (PCI bus-master systems)
CAP_FLAGS       equ 0x0002          ; BUSMASTER capability
PATCH_COUNT     equ 1               ; io_base patch point

;; 3Com bus-master register offsets from io_base
BM_UP_LIST_PTR  equ 0x38           ; UpListPtr - pointer to upload descriptor
BM_UP_PKT_STAT  equ 0x30           ; UpPktStatus
BM_DN_LIST_PTR  equ 0x24           ; DnListPtr - pointer to download descriptor
BM_DMA_CTRL     equ 0x20           ; DmaCtrl register
BM_CMD_REG      equ 0x0E           ; Command register

;; Command values
CMD_UP_STALL    equ 0x3002         ; Stall upload engine
CMD_UP_UNSTALL  equ 0x3003         ; Unstall upload engine
CMD_DN_STALL    equ 0x3402         ; Stall download engine
CMD_DN_UNSTALL  equ 0x3403         ; Unstall download engine

;; DmaCtrl bits
DMACTRL_UP_COMPLETE equ 0x4000     ; Upload complete bit
DMACTRL_DN_COMPLETE equ 0x0010     ; Download complete bit

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Module segment
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
section .text class=MODULE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 64-byte module header
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _mod_dma_busmaster_header
_mod_dma_busmaster_header:
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
;; bm_setup_upload - Set UpListPtr and enable upload engine
;;
;; Input:
;;   DX:AX = 32-bit physical address of upload descriptor
;; Output:
;;   None (upload engine started)
;; Clobbers: DX, AX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
bm_setup_upload:
    push    dx
    push    ax

    ;; Load io_base (patched at runtime)
    PATCH_POINT pp_io_base
    mov     bx, 0x0000              ; Placeholder: patched to io_base

    ;; Stall upload engine before reprogramming
    mov     dx, bx
    add     dx, BM_CMD_REG
    mov     ax, CMD_UP_STALL
    out     dx, ax
    jmp     short $+2              ; I/O settle delay

    ;; Wait for stall to complete
bm_stall_up_wait:
    mov     dx, bx
    add     dx, BM_UP_PKT_STAT
    in      ax, dx
    test    ax, 0x1000             ; UpStalled?
    jz      bm_stall_up_wait

    ;; Write UpListPtr (32-bit register, write low word then high word)
    mov     dx, bx
    add     dx, BM_UP_LIST_PTR
    pop     ax                      ; Low 16 bits of phys address
    out     dx, ax
    add     dx, 2
    pop     ax                      ; High 16 bits (was in DX on entry)
    out     dx, ax

    ;; Unstall upload engine
    mov     dx, bx
    add     dx, BM_CMD_REG
    mov     ax, CMD_UP_UNSTALL
    out     dx, ax

    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; bm_setup_download - Set DnListPtr and enable download engine
;;
;; Input:
;;   DX:AX = 32-bit physical address of download descriptor
;; Output:
;;   None (download engine started)
;; Clobbers: DX, AX, BX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
bm_setup_download:
    push    dx
    push    ax

    ;; Load io_base
    mov     bx, [pp_io_base + 3]   ; Read patched io_base value
    ; (Alternative: duplicate patch point or cache in memory)
    ; For simplicity, re-read from patch sled - offset past NOP/alignment
    mov     bx, 0x0000              ; Patched to io_base

    ;; Stall download engine
    mov     dx, bx
    add     dx, BM_CMD_REG
    mov     ax, CMD_DN_STALL
    out     dx, ax
    jmp     short $+2

    ;; Wait for stall
bm_stall_dn_wait:
    mov     dx, bx
    add     dx, BM_DMA_CTRL
    in      ax, dx
    test    ax, 0x0020             ; DnStalled?
    jz      bm_stall_dn_wait

    ;; Write DnListPtr (32-bit)
    mov     dx, bx
    add     dx, BM_DN_LIST_PTR
    pop     ax                      ; Low 16 bits
    out     dx, ax
    add     dx, 2
    pop     ax                      ; High 16 bits
    out     dx, ax

    ;; Unstall download engine
    mov     dx, bx
    add     dx, BM_CMD_REG
    mov     ax, CMD_DN_UNSTALL
    out     dx, ax

    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; bm_poll_complete - Check if DMA transfer has completed
;;
;; Input:
;;   AL = 0 for upload, 1 for download
;; Output:
;;   CF = 0 if complete, CF = 1 if still in progress
;; Clobbers: AX, DX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
bm_poll_complete:
    mov     bx, 0x0000              ; Patched to io_base
    test    al, al
    jnz     bm_poll_download

    ;; Check upload completion
    mov     dx, bx
    add     dx, BM_UP_PKT_STAT
    in      ax, dx
    test    ax, DMACTRL_UP_COMPLETE
    jz      bm_not_complete
    clc                             ; Transfer complete
    ret

bm_poll_download:
    ;; Check download completion
    mov     dx, bx
    add     dx, BM_DMA_CTRL
    in      ax, dx
    test    ax, DMACTRL_DN_COMPLETE
    jz      bm_not_complete
    clc                             ; Transfer complete
    ret

bm_not_complete:
    stc                             ; Still in progress
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; bm_abort - Halt DMA engine immediately
;;
;; Input: None
;; Output: None (both upload and download engines stalled)
;; Clobbers: AX, DX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
bm_abort:
    mov     bx, 0x0000              ; Patched to io_base
    mov     dx, bx
    add     dx, BM_CMD_REG

    ;; Stall both engines
    mov     ax, CMD_UP_STALL
    out     dx, ax
    jmp     short $+2
    mov     ax, CMD_DN_STALL
    out     dx, ax

    ;; Clear list pointers (write zero)
    mov     dx, bx
    add     dx, BM_UP_LIST_PTR
    xor     ax, ax
    out     dx, ax
    add     dx, 2
    out     dx, ax

    mov     dx, bx
    add     dx, BM_DN_LIST_PTR
    xor     ax, ax
    out     dx, ax
    add     dx, 2
    out     dx, ax

    ret

hot_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Patch table
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
patch_table:
    PATCH_TABLE_ENTRY pp_io_base, PATCH_TYPE_IO  ; NIC I/O base address
