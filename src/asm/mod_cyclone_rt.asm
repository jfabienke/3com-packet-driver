;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file mod_cyclone_rt.asm
;; @brief JIT runtime module for 3Com Cyclone PCI DMA+Checksum (3C905B/920)
;;
;; 3Com Packet Driver - NIC-specific runtime module
;; Assembled at load time by the JIT patch engine into the TSR resident image.
;;
;; The Cyclone generation extends Boomerang with hardware TCP/IP checksum
;; offload.  TX packets can have checksums computed by the NIC, and RX
;; packets arrive with checksum verification status.
;;
;; Functions:
;;   - send_packet      : submit DPD with optional TX checksum enable
;;   - recv_packet      : consume UPD with RX checksum status
;;   - handle_interrupt  : ISR with extended statistics handling
;;
;; Requires 386 for 32-bit descriptor ring access.
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
NIC_TYPE_CYCLONE        equ 5
CPU_REQ_386             equ 3
CAP_PCI_BUS             equ 0010h
CAP_BUSMASTER_DMA       equ 0002h
CAP_DESC_RING           equ 0100h
CAP_FLAGS               equ CAP_PCI_BUS | CAP_BUSMASTER_DMA | CAP_DESC_RING
                        ;; = 0112h

;; ---- Cyclone register offsets (from io_base) ----------------------------
REG_INT_STATUS          equ 0Ah
REG_COMMAND             equ 0Eh
REG_TX_STATUS           equ 04h
REG_DN_LIST_PTR         equ 24h
REG_DN_POLL             equ 2Dh
REG_UP_LIST_PTR         equ 38h
REG_UP_PKT_STATUS       equ 30h
REG_DMA_CTRL            equ 20h

;; Cyclone-specific: checksum offload registers (Window 7)
REG_TX_CSUM_START       equ 0Ah     ; TX checksum start offset (Window 7)
REG_TX_CSUM_STUFF       equ 0Ch     ; TX checksum stuff offset (Window 7)
REG_RX_CSUM_STATUS      equ 0Ah     ; RX checksum status (Window 1, extended)
REG_RX_CSUM_VALUE       equ 0Ch     ; RX checksum value (Window 1, extended)

;; Cyclone-specific: statistics registers (Window 6)
REG_CARRIER_LOST        equ 00h
REG_SQE_ERRORS          equ 01h
REG_MULTI_COLLISIONS    equ 02h
REG_SINGLE_COLLISIONS   equ 03h
REG_LATE_COLLISIONS     equ 04h
REG_RX_OVERRUNS         equ 05h
REG_FRAMES_XMIT_OK     equ 06h
REG_FRAMES_RECV_OK      equ 07h
REG_FRAMES_DEFERRED     equ 08h
REG_UPPER_FRAMES_OK     equ 09h
REG_BYTES_RECV_OK       equ 0Ah
REG_BYTES_XMIT_OK       equ 0Ch

;; Commands
CMD_SELECT_WINDOW       equ 0800h
CMD_ACK_INT             equ 6800h
CMD_DN_STALL            equ 3002h
CMD_DN_UNSTALL          equ 3003h
CMD_UP_STALL            equ 3000h
CMD_UP_UNSTALL          equ 3001h
CMD_STATS_ENABLE        equ 0A800h

;; Interrupt bits
INT_LATCH               equ 0001h
INT_HOST_ERROR          equ 0002h
INT_TX_COMPLETE         equ 0004h
INT_RX_COMPLETE         equ 0010h
INT_DN_COMPLETE         equ 0020h
INT_UP_COMPLETE         equ 0040h
INT_UPDATE_STATS        equ 0080h
INT_LINK_EVENT          equ 0100h

;; DPD flags for checksum offload
DPD_LAST_FRAG           equ 80000000h
DPD_CSUM_ENABLE         equ 20000000h  ; Enable TX checksum in FrameStartHdr
DPD_ADD_IP_CSUM         equ 02000000h  ; Compute IP header checksum
DPD_ADD_TCP_CSUM        equ 04000000h  ; Compute TCP/UDP checksum

;; UPD status bits
UPD_COMPLETE            equ 00008000h
UPD_ERROR               equ 00004000h
UPD_LEN_MASK            equ 00001FFFh
UPD_CSUM_CHECKED        equ 00010000h  ; Hardware checksum was verified
UPD_TCP_CSUM_OK         equ 00020000h  ; TCP checksum correct
UPD_IP_CSUM_OK          equ 00040000h  ; IP checksum correct

DPD_SIZE                equ 16
UPD_SIZE                equ 16
TX_RING_SIZE            equ 16
RX_RING_SIZE            equ 32
MAX_FRAME_SIZE          equ 1514

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; MODULE SEGMENT
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
section MOD_CYCLONE_TEXT class=MODULE

global _mod_cyclone_header

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 64-byte module header
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
_mod_cyclone_header:
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
    db NIC_TYPE_CYCLONE         ; nic_type
    dw CAP_FLAGS                ; cap_flags
    times (64 - ($ - header)) db 0

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; HOT SECTION
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
hot_start:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; send_packet - Submit DPD with TX checksum offload
;;
;; Input:  DS:SI = packet data (physical addr in ESI)
;;         CX = length, DL = checksum flags (bit 0=IP, bit 1=TCP)
;;         BX = TX ring producer index
;; Output: CF=0 success, CF=1 ring full
;; Clobbers: EAX, EDX, ECX, EBX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
send_packet:
    push    ebx
    push    esi
    push    edi
    push    edx                     ; save checksum flags

    cmp     cx, MAX_FRAME_SIZE
    ja      tx_err
    or      cx, cx
    jz      tx_err

    ;; ---- Calculate DPD address -----------------------------------------
    movzx   eax, bx
    shl     eax, 4                  ; * DPD_SIZE
    PATCH_POINT pp_dpd_ring_base
    mov     edi, 0                  ; patched: DPD ring physical base
    add     edi, eax

    ;; ---- Populate DPD --------------------------------------------------
    ;; DnNextPtr = 0
    xor     eax, eax
    mov     [edi], eax

    ;; FrameStartHeader = length | checksum flags
    movzx   eax, cx
    pop     edx                     ; restore csum flags
    push    edx
    test    dl, 01h                 ; IP checksum?
    jz      no_ip_csum
    or      eax, DPD_ADD_IP_CSUM | DPD_CSUM_ENABLE
no_ip_csum:
    test    dl, 02h                 ; TCP checksum?
    jz      no_tcp_csum
    or      eax, DPD_ADD_TCP_CSUM | DPD_CSUM_ENABLE
no_tcp_csum:
    mov     [edi+4], eax            ; DPD.FrameStartHeader

    ;; DataAddr = physical address of packet
    mov     [edi+8], esi

    ;; DataLength = length | LAST_FRAG
    movzx   eax, cx
    or      eax, DPD_LAST_FRAG
    mov     [edi+0Ch], eax

    ;; ---- Set TX checksum offsets (Window 7) if needed ------------------
    pop     edx
    test    dl, 03h
    jz      skip_csum_setup

    PATCH_POINT pp_tx_csum1
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_SELECT_WINDOW | 7
    out     dx, ax

    ;; TX checksum start = offset 14 (after Ethernet header)
    PATCH_POINT pp_tx_csum2
    mov     dx, 0                   ; patched: io_base + REG_TX_CSUM_START
    mov     ax, 14                  ; Ethernet header size
    out     dx, ax

    ;; TX checksum stuff offset depends on protocol
    ;; IP header checksum at offset 24, TCP at offset 50
    PATCH_POINT pp_tx_csum3
    mov     dx, 0                   ; patched: io_base + REG_TX_CSUM_STUFF
    mov     ax, 24                  ; default to IP csum position
    out     dx, ax

skip_csum_setup:
    ;; ---- Stall, set DnListPtr, unstall ---------------------------------
    PATCH_POINT pp_tx_iobase1
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_DN_STALL
    out     dx, ax

    ;; Wait for stall
    PATCH_POINT pp_tx_iobase2
    mov     dx, 0                   ; patched: io_base + REG_DMA_CTRL
    mov     cx, 1000
stall_wait:
    in      eax, dx
    test    eax, 00004000h
    jnz     stall_ok
    loop    stall_wait
stall_ok:

    PATCH_POINT pp_tx_iobase3
    mov     dx, 0                   ; patched: io_base + REG_DN_LIST_PTR
    mov     eax, edi
    out     dx, eax

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
    pop     edx
    pop     edi
    pop     esi
    pop     ebx
    stc
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; recv_packet - Consume UPD with RX checksum status
;;
;; Input:  ES:DI = buffer, CX = buffer size, BX = RX ring consumer index
;; Output: CF=0 success (CX=bytes, AH=checksum status bits)
;;         CF=1 no packet
;; Clobbers: EAX, EDX, ECX, EBX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
recv_packet:
    push    ebx
    push    esi
    push    edi

    ;; ---- Calculate UPD address -----------------------------------------
    movzx   eax, bx
    shl     eax, 4
    PATCH_POINT pp_upd_ring_base
    mov     esi, 0                  ; patched: UPD ring physical base
    add     esi, eax

    ;; ---- Check UPD status ----------------------------------------------
    mov     eax, [esi+4]            ; UpPktStatus
    test    eax, UPD_COMPLETE
    jz      rx_none

    test    eax, UPD_ERROR
    jnz     rx_error

    ;; ---- Save checksum status ------------------------------------------
    mov     ebx, eax                ; save full status
    shr     ebx, 16                 ; checksum bits in BL
    ;; BL bit 0 = csum_checked, bit 1 = tcp_ok, bit 2 = ip_ok

    ;; ---- Extract length ------------------------------------------------
    and     eax, UPD_LEN_MASK
    movzx   ecx, cx
    cmp     eax, ecx
    jbe     rx_len_ok
    mov     eax, ecx
rx_len_ok:
    mov     ecx, eax                ; ECX = bytes to transfer

    ;; ---- Read RX checksum value for upper layer ------------------------
    PATCH_POINT pp_rx_csum1
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_SELECT_WINDOW | 1
    out     dx, ax

    PATCH_POINT pp_rx_csum2
    mov     dx, 0                   ; patched: io_base + REG_RX_CSUM_STATUS
    in      ax, dx                  ; AX = checksum status register

    ;; ---- Reset UPD status for reuse ------------------------------------
    mov     dword [esi+4], 0

    ;; ---- Unstall upload engine -----------------------------------------
    PATCH_POINT pp_rx_iobase1
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_UP_UNSTALL
    out     dx, ax

    ;; Return: CX = length, AH = checksum status (from BL)
    mov     ah, bl
    movzx   cx, cx
    pop     edi
    pop     esi
    pop     ebx
    clc
    ret

rx_error:
    mov     dword [esi+4], 0        ; reset UPD
rx_none:
    pop     edi
    pop     esi
    pop     ebx
    xor     cx, cx
    stc
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; handle_interrupt - Cyclone ISR with statistics handling
;;
;; Input:  (from TSR wrapper)
;; Output: CF=0 ours, CF=1 not ours
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
handle_interrupt:
    push    ebx

    PATCH_POINT pp_isr_iobase1
    mov     dx, 0                   ; patched: io_base + REG_INT_STATUS
    in      ax, dx

    test    ax, INT_LATCH
    jz      isr_not_ours

    mov     bx, ax

    ;; ---- Upload complete (RX DMA) --------------------------------------
    test    bx, INT_UP_COMPLETE
    jz      isr_chk_dn
    PATCH_POINT pp_isr_iobase2
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_UP_COMPLETE
    out     dx, ax

isr_chk_dn:
    ;; ---- Download complete (TX DMA) ------------------------------------
    test    bx, INT_DN_COMPLETE
    jz      isr_chk_tx
    PATCH_POINT pp_isr_iobase3
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_DN_COMPLETE
    out     dx, ax

isr_chk_tx:
    ;; ---- TX complete ---------------------------------------------------
    test    bx, INT_TX_COMPLETE
    jz      isr_chk_rx
    PATCH_POINT pp_isr_iobase4
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_TX_COMPLETE
    out     dx, ax

    PATCH_POINT pp_isr_iobase5
    mov     dx, 0                   ; patched: io_base + REG_TX_STATUS
    in      al, dx
    out     dx, al

isr_chk_rx:
    ;; ---- RX complete ---------------------------------------------------
    test    bx, INT_RX_COMPLETE
    jz      isr_chk_stats
    PATCH_POINT pp_isr_iobase6
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_RX_COMPLETE
    out     dx, ax

isr_chk_stats:
    ;; ---- Statistics update (Cyclone extended) --------------------------
    test    bx, INT_UPDATE_STATS
    jz      isr_chk_link
    PATCH_POINT pp_isr_iobase7
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_UPDATE_STATS
    out     dx, ax

    ;; Read and clear statistics counters (Window 6)
    PATCH_POINT pp_isr_stats1
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_SELECT_WINDOW | 6
    out     dx, ax

    ;; Read key counters to prevent overflow (auto-cleared on read)
    PATCH_POINT pp_isr_stats2
    mov     dx, 0                   ; patched: io_base + REG_CARRIER_LOST
    in      al, dx                  ; carrier lost count
    PATCH_POINT pp_isr_stats3
    mov     dx, 0                   ; patched: io_base + REG_FRAMES_XMIT_OK
    in      al, dx                  ; frames transmitted
    PATCH_POINT pp_isr_stats4
    mov     dx, 0                   ; patched: io_base + REG_FRAMES_RECV_OK
    in      al, dx                  ; frames received
    PATCH_POINT pp_isr_stats5
    mov     dx, 0                   ; patched: io_base + REG_RX_OVERRUNS
    in      al, dx                  ; RX overruns

isr_chk_link:
    ;; ---- Link event (Cyclone-specific) ---------------------------------
    test    bx, INT_LINK_EVENT
    jz      isr_chk_err
    PATCH_POINT pp_isr_iobase8
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_LINK_EVENT
    out     dx, ax

isr_chk_err:
    ;; ---- Host error ----------------------------------------------------
    test    bx, INT_HOST_ERROR
    jz      isr_ack
    PATCH_POINT pp_isr_iobase9
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_HOST_ERROR
    out     dx, ax

isr_ack:
    PATCH_POINT pp_isr_iobase10
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
    PATCH_TABLE_ENTRY pp_tx_csum1,      PATCH_TYPE_IO     ; command (win 7)
    PATCH_TABLE_ENTRY pp_tx_csum2,      PATCH_TYPE_IO     ; csum start
    PATCH_TABLE_ENTRY pp_tx_csum3,      PATCH_TYPE_IO     ; csum stuff
    PATCH_TABLE_ENTRY pp_tx_iobase1,    PATCH_TYPE_IO     ; command (stall)
    PATCH_TABLE_ENTRY pp_tx_iobase2,    PATCH_TYPE_IO     ; DMA ctrl
    PATCH_TABLE_ENTRY pp_tx_iobase3,    PATCH_TYPE_IO     ; DnListPtr
    PATCH_TABLE_ENTRY pp_tx_iobase4,    PATCH_TYPE_IO     ; command (unstall)
    ;; recv_packet
    PATCH_TABLE_ENTRY pp_upd_ring_base, PATCH_TYPE_IMM16  ; UPD ring base
    PATCH_TABLE_ENTRY pp_rx_csum1,      PATCH_TYPE_IO     ; command (win 1)
    PATCH_TABLE_ENTRY pp_rx_csum2,      PATCH_TYPE_IO     ; csum status
    PATCH_TABLE_ENTRY pp_rx_iobase1,    PATCH_TYPE_IO     ; command (unstall)
    ;; ISR
    PATCH_TABLE_ENTRY pp_isr_iobase1,   PATCH_TYPE_IO     ; IntStatus
    PATCH_TABLE_ENTRY pp_isr_iobase2,   PATCH_TYPE_IO     ; ack upComplete
    PATCH_TABLE_ENTRY pp_isr_iobase3,   PATCH_TYPE_IO     ; ack dnComplete
    PATCH_TABLE_ENTRY pp_isr_iobase4,   PATCH_TYPE_IO     ; ack txComplete
    PATCH_TABLE_ENTRY pp_isr_iobase5,   PATCH_TYPE_IO     ; TX status
    PATCH_TABLE_ENTRY pp_isr_iobase6,   PATCH_TYPE_IO     ; ack rxComplete
    PATCH_TABLE_ENTRY pp_isr_iobase7,   PATCH_TYPE_IO     ; ack updateStats
    PATCH_TABLE_ENTRY pp_isr_stats1,    PATCH_TYPE_IO     ; stats window sel
    PATCH_TABLE_ENTRY pp_isr_stats2,    PATCH_TYPE_IO     ; carrier lost
    PATCH_TABLE_ENTRY pp_isr_stats3,    PATCH_TYPE_IO     ; frames xmit
    PATCH_TABLE_ENTRY pp_isr_stats4,    PATCH_TYPE_IO     ; frames recv
    PATCH_TABLE_ENTRY pp_isr_stats5,    PATCH_TYPE_IO     ; RX overruns
    PATCH_TABLE_ENTRY pp_isr_iobase8,   PATCH_TYPE_IO     ; ack linkEvent
    PATCH_TABLE_ENTRY pp_isr_iobase9,   PATCH_TYPE_IO     ; ack hostError
    PATCH_TABLE_ENTRY pp_isr_iobase10,  PATCH_TYPE_IO     ; ack latch
PATCH_COUNT equ ($ - patch_table) / 4
