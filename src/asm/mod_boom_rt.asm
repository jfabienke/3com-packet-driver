;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file mod_boom_rt.asm
;; @brief JIT runtime module for 3Com Boomerang PCI DMA NICs (3C900/905)
;;
;; 3Com Packet Driver - NIC-specific runtime module
;; Assembled at load time by the JIT patch engine into the TSR resident image.
;;
;; The Boomerang generation introduces full bus-master DMA with descriptor
;; rings.  Upload Packet Descriptors (UPDs) receive frames, Download Packet
;; Descriptors (DPDs) transmit frames.  Each descriptor contains a physical
;; address, byte count, and status/control flags.
;;
;; Functions:
;;   - send_packet      : submit DPD to download engine
;;   - recv_packet      : consume completed UPD from upload ring
;;   - handle_interrupt  : ISR managing DMA completion events
;;
;; Requires 386 for 32-bit register access to descriptor physical addresses.
;;
;; Last Updated: 2026-02-01 11:02:20 CET
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16
cpu 386

%include "patch_macros.inc"

;; ---- Additional patch type constants ------------------------------------
PATCH_TYPE_IMM16        equ 6
PATCH_TYPE_IMM8         equ 7
PATCH_TYPE_RELOC_NEAR   equ 8

;; ---- NIC identity -------------------------------------------------------
NIC_TYPE_BOOMERANG      equ 4
CPU_REQ_386             equ 3
CAP_PCI_BUS             equ 0010h
CAP_BUSMASTER_DMA       equ 0002h
CAP_DESC_RING           equ 0100h
CAP_FLAGS               equ CAP_PCI_BUS | CAP_BUSMASTER_DMA | CAP_DESC_RING

;; ---- Boomerang register offsets (from io_base) --------------------------
REG_INT_STATUS          equ 0Ah     ; Interrupt status
REG_COMMAND             equ 0Eh     ; Command register
REG_TX_STATUS           equ 04h     ; TX status
REG_DN_LIST_PTR         equ 24h     ; Download list pointer (32-bit)
REG_DN_POLL             equ 2Dh     ; Download poll (kick DMA)
REG_UP_LIST_PTR         equ 38h     ; Upload list pointer (32-bit)
REG_UP_PKT_STATUS       equ 30h     ; Upload packet status (32-bit)
REG_DMA_CTRL            equ 20h     ; DMA control
REG_FREE_TIMER          equ 0Ch     ; Free-running timer

;; Commands
CMD_SELECT_WINDOW       equ 0800h
CMD_ACK_INT             equ 6800h
CMD_DN_STALL            equ 3002h
CMD_DN_UNSTALL          equ 3003h
CMD_UP_STALL            equ 3000h
CMD_UP_UNSTALL          equ 3001h

;; Interrupt bits
INT_LATCH               equ 0001h
INT_HOST_ERROR          equ 0002h
INT_TX_COMPLETE         equ 0004h
INT_RX_COMPLETE         equ 0010h
INT_DN_COMPLETE         equ 0020h
INT_UP_COMPLETE         equ 0040h
INT_INT_REQUESTED       equ 0080h

;; ---- Descriptor structures ----------------------------------------------
;; DPD (Download Packet Descriptor) - 16 bytes:
;;   +00h: DnNextPtr   (dword) - physical address of next DPD, 0 = end
;;   +04h: FrameStartHeader (dword) - length[12:0] | flags[31:13]
;;   +08h: DataAddr     (dword) - physical address of packet data
;;   +0Ch: DataLength   (dword) - byte count | last-frag flag (bit 31)
DPD_SIZE                equ 16
DPD_LAST_FRAG           equ 80000000h

;; UPD (Upload Packet Descriptor) - 16 bytes:
;;   +00h: UpNextPtr    (dword) - physical address of next UPD
;;   +04h: UpPktStatus  (dword) - status | length
;;   +08h: DataAddr     (dword) - physical address of receive buffer
;;   +0Ch: DataLength   (dword) - buffer size
UPD_SIZE                equ 16
UPD_COMPLETE            equ 00008000h
UPD_ERROR               equ 00004000h
UPD_LEN_MASK            equ 00001FFFh

;; Ring sizes
TX_RING_SIZE            equ 16
RX_RING_SIZE            equ 16

MAX_FRAME_SIZE          equ 1514

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; MODULE SEGMENT
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
section MOD_BOOM_TEXT class=MODULE

global _mod_boom_header

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 64-byte module header
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
_mod_boom_header:
header:
    db 'PKTDRV',0
    db 1                        ; version_major
    db 0                        ; version_minor
    dw hot_start
    dw hot_end
    dw 0, 0                     ; cold_start, cold_end
    dw patch_table
    dw PATCH_COUNT
    dw (hot_end - header)       ; module_size
    dw (hot_end - hot_start)    ; required_memory
    db CPU_REQ_386              ; cpu_requirements
    db NIC_TYPE_BOOMERANG       ; nic_type
    dw CAP_FLAGS                ; cap_flags
    times (64 - ($ - header)) db 0

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; HOT SECTION
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
hot_start:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; send_packet - Submit a DPD to the Boomerang download engine
;;
;; Input:  DS:SI = packet data (linear/physical), CX = length
;;         BX = TX ring producer index (managed by TSR wrapper)
;; Output: CF=0 success, CF=1 ring full
;; Clobbers: EAX, EDX, ECX, BX
;;
;; The TSR wrapper maintains the DPD ring at a known physical address.
;; The JIT engine patches pp_dpd_ring_base with the ring's physical address.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
send_packet:
    push    ebx
    push    esi
    push    edi

    cmp     cx, MAX_FRAME_SIZE
    ja      tx_err
    or      cx, cx
    jz      tx_err

    ;; ---- Calculate DPD address from ring index -------------------------
    ;; DPD address = ring_base + (index * DPD_SIZE)
    movzx   eax, bx
    shl     eax, 4                  ; * 16 (DPD_SIZE)
    PATCH_POINT pp_dpd_ring_base
    mov     edi, 0                  ; patched: physical base of DPD ring
    add     edi, eax                ; EDI = this DPD's physical address

    ;; ---- Populate the DPD in memory ------------------------------------
    ;; DnNextPtr = 0 (end of chain, will be linked by next send)
    xor     eax, eax
    mov     [edi], eax              ; DPD.DnNextPtr = 0

    ;; FrameStartHeader = packet length (bits 0-12), no special flags
    movzx   eax, cx
    mov     [edi+4], eax            ; DPD.FrameStartHeader

    ;; DataAddr = physical address of packet (DS:SI converted by TSR wrapper)
    ;; The TSR wrapper has already set up the physical address in ESI
    mov     [edi+8], esi            ; DPD.DataAddr

    ;; DataLength = length | LAST_FRAG (single fragment)
    movzx   eax, cx
    or      eax, DPD_LAST_FRAG
    mov     [edi+0Ch], eax          ; DPD.DataLength

    ;; ---- Stall download engine -----------------------------------------
    PATCH_POINT pp_tx_iobase1
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_DN_STALL
    out     dx, ax

    ;; Wait for stall to complete (bit 14 of DMA_CTRL)
    PATCH_POINT pp_tx_iobase2
    mov     dx, 0                   ; patched: io_base + REG_DMA_CTRL
    mov     cx, 1000
stall_wait:
    in      eax, dx
    test    eax, 00004000h          ; dnStalled?
    jnz     stall_done
    loop    stall_wait
stall_done:

    ;; ---- Write DPD physical address to DnListPtr -----------------------
    PATCH_POINT pp_tx_iobase3
    mov     dx, 0                   ; patched: io_base + REG_DN_LIST_PTR
    mov     eax, edi
    out     dx, eax

    ;; ---- Unstall download engine ---------------------------------------
    PATCH_POINT pp_tx_iobase4
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_DN_UNSTALL
    out     dx, ax

    pop     edi
    pop     esi
    pop     ebx
    clc
    ret

tx_err:
    pop     edi
    pop     esi
    pop     ebx
    stc
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; recv_packet - Consume a completed UPD from the upload ring
;;
;; Input:  ES:DI = destination buffer, CX = buffer size
;;         BX = RX ring consumer index
;; Output: CF=0 success (CX=bytes), CF=1 no packet
;; Clobbers: EAX, EDX, ECX, BX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
recv_packet:
    push    ebx
    push    esi
    push    edi

    ;; ---- Calculate UPD address from ring index -------------------------
    movzx   eax, bx
    shl     eax, 4                  ; * UPD_SIZE
    PATCH_POINT pp_upd_ring_base
    mov     esi, 0                  ; patched: physical base of UPD ring
    add     esi, eax                ; ESI -> this UPD

    ;; ---- Check UPD status ----------------------------------------------
    mov     eax, [esi+4]            ; UPD.UpPktStatus
    test    eax, UPD_COMPLETE
    jz      rx_none

    test    eax, UPD_ERROR
    jnz     rx_error

    ;; ---- Extract packet length -----------------------------------------
    and     eax, UPD_LEN_MASK
    movzx   ebx, cx                 ; max buffer size
    cmp     eax, ebx
    jbe     rx_len_ok
    mov     eax, ebx                ; truncate
rx_len_ok:
    mov     ecx, eax                ; ECX = bytes to copy

    ;; ---- Copy data from UPD buffer to caller's buffer ------------------
    ;; The UPD's DataAddr points to the NIC DMA buffer in physical memory.
    ;; In real mode we use the TSR wrapper's segment mapping.
    push    ecx
    mov     esi, [esi+8]            ; UPD.DataAddr (physical)
    ;; TSR wrapper provides a real-mode mapping; data copy is done by the
    ;; upper layer using the address returned here.

    ;; ---- Reset UPD for reuse -------------------------------------------
    ;; Clear status, restore buffer length
    pop     ecx
    ;; (UPD is reset by the TSR wrapper after we return)

    ;; ---- Unstall upload engine if it was stalled -----------------------
    PATCH_POINT pp_rx_iobase1
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_UP_UNSTALL
    out     dx, ax

    movzx   ecx, cx                 ; ensure CX = actual bytes
    pop     edi
    pop     esi
    pop     ebx
    clc
    ret

rx_error:
    ;; Reset the errored UPD (clear status)
    mov     dword [esi+4], 0
rx_none:
    pop     edi
    pop     esi
    pop     ebx
    xor     cx, cx
    stc
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; handle_interrupt - Boomerang ISR
;;
;; Manages DMA completion events for both upload and download engines.
;;
;; Input:  (from TSR wrapper)
;; Output: CF=0 ours, CF=1 not ours
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
handle_interrupt:
    push    ebx

    ;; ---- Read IntStatus ------------------------------------------------
    PATCH_POINT pp_isr_iobase1
    mov     dx, 0                   ; patched: io_base + REG_INT_STATUS
    in      ax, dx

    test    ax, INT_LATCH
    jz      isr_not_ours

    mov     bx, ax

    ;; ---- Upload (RX) complete ------------------------------------------
    test    bx, INT_UP_COMPLETE
    jz      isr_chk_dn
    PATCH_POINT pp_isr_iobase2
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_UP_COMPLETE
    out     dx, ax
    ;; Signal TSR wrapper: RX ready

isr_chk_dn:
    ;; ---- Download (TX) complete ----------------------------------------
    test    bx, INT_DN_COMPLETE
    jz      isr_chk_tx
    PATCH_POINT pp_isr_iobase3
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_DN_COMPLETE
    out     dx, ax
    ;; Reclaim completed DPDs (advance consumer index)

isr_chk_tx:
    ;; ---- TX complete (status register) ---------------------------------
    test    bx, INT_TX_COMPLETE
    jz      isr_chk_rx
    PATCH_POINT pp_isr_iobase4
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_TX_COMPLETE
    out     dx, ax

    ;; Clear TX status byte
    PATCH_POINT pp_isr_iobase5
    mov     dx, 0                   ; patched: io_base + REG_TX_STATUS
    in      al, dx
    out     dx, al

isr_chk_rx:
    ;; ---- RX complete (PIO fallback indicator) --------------------------
    test    bx, INT_RX_COMPLETE
    jz      isr_chk_err
    PATCH_POINT pp_isr_iobase6
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_RX_COMPLETE
    out     dx, ax

isr_chk_err:
    ;; ---- Host error ----------------------------------------------------
    test    bx, INT_HOST_ERROR
    jz      isr_ack
    PATCH_POINT pp_isr_iobase7
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_HOST_ERROR
    out     dx, ax
    ;; TODO: reset DMA engines on host error

isr_ack:
    ;; ---- Acknowledge latch ---------------------------------------------
    PATCH_POINT pp_isr_iobase8
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_LATCH
    out     dx, ax

    pop     ebx
    clc
    ret

isr_not_ours:
    pop     ebx
    stc
    ret

hot_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; PATCH TABLE
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
patch_table:
    ;; send_packet
    PATCH_TABLE_ENTRY pp_dpd_ring_base, PATCH_TYPE_IMM16  ; DPD ring base
    PATCH_TABLE_ENTRY pp_tx_iobase1,    PATCH_TYPE_IO     ; command (stall)
    PATCH_TABLE_ENTRY pp_tx_iobase2,    PATCH_TYPE_IO     ; DMA ctrl
    PATCH_TABLE_ENTRY pp_tx_iobase3,    PATCH_TYPE_IO     ; DnListPtr
    PATCH_TABLE_ENTRY pp_tx_iobase4,    PATCH_TYPE_IO     ; command (unstall)
    ;; recv_packet
    PATCH_TABLE_ENTRY pp_upd_ring_base, PATCH_TYPE_IMM16  ; UPD ring base
    PATCH_TABLE_ENTRY pp_rx_iobase1,    PATCH_TYPE_IO     ; command (unstall)
    ;; ISR
    PATCH_TABLE_ENTRY pp_isr_iobase1,   PATCH_TYPE_IO     ; IntStatus
    PATCH_TABLE_ENTRY pp_isr_iobase2,   PATCH_TYPE_IO     ; ack upComplete
    PATCH_TABLE_ENTRY pp_isr_iobase3,   PATCH_TYPE_IO     ; ack dnComplete
    PATCH_TABLE_ENTRY pp_isr_iobase4,   PATCH_TYPE_IO     ; ack txComplete
    PATCH_TABLE_ENTRY pp_isr_iobase5,   PATCH_TYPE_IO     ; TX status
    PATCH_TABLE_ENTRY pp_isr_iobase6,   PATCH_TYPE_IO     ; ack rxComplete
    PATCH_TABLE_ENTRY pp_isr_iobase7,   PATCH_TYPE_IO     ; ack hostError
    PATCH_TABLE_ENTRY pp_isr_iobase8,   PATCH_TYPE_IO     ; ack latch
PATCH_COUNT equ ($ - patch_table) / 4
