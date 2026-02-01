;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file mod_tornado_rt.asm
;; @brief JIT runtime module for 3Com Tornado PCI DMA+SG+VLAN (3C905C/556)
;;
;; 3Com Packet Driver - NIC-specific runtime module
;; Assembled at load time by the JIT patch engine into the TSR resident image.
;;
;; The Tornado generation extends Cyclone with:
;;   - Scatter-gather TX: multiple DPD fragments per packet
;;   - VLAN tag insertion on TX and extraction on RX (802.1Q)
;;   - Extended interrupt status word (32-bit)
;;   - Improved DMA engine with larger descriptor rings
;;
;; Functions:
;;   - send_packet      : submit scatter-gather DPD with VLAN tag insertion
;;   - recv_packet      : consume UPD with VLAN tag extraction + checksum
;;   - handle_interrupt  : ISR with extended 32-bit status handling
;;
;; Requires 386 for 32-bit descriptor and DMA access.
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
NIC_TYPE_TORNADO        equ 6
CPU_REQ_386             equ 3
CAP_PCI_BUS             equ 0010h
CAP_BUSMASTER_DMA       equ 0002h
CAP_DESC_RING           equ 0100h
CAP_FLAGS               equ CAP_PCI_BUS | CAP_BUSMASTER_DMA | CAP_DESC_RING
                        ;; = 0112h

;; ---- Tornado register offsets (from io_base) ----------------------------
REG_INT_STATUS          equ 0Ah     ; Interrupt status (16-bit legacy)
REG_INT_STATUS_EXT      equ 24h     ; Extended interrupt status (Tornado)
REG_COMMAND             equ 0Eh     ; Command register
REG_TX_STATUS           equ 04h     ; TX status
REG_DN_LIST_PTR         equ 24h     ; Download list pointer
REG_DN_POLL             equ 2Dh     ; Download poll
REG_UP_LIST_PTR         equ 38h     ; Upload list pointer
REG_UP_PKT_STATUS       equ 30h     ; Upload packet status
REG_DMA_CTRL            equ 20h     ; DMA control

;; Tornado VLAN registers (Window 7)
REG_VLAN_ETHER_TYPE     equ 04h     ; VLAN EtherType (default 8100h)
REG_VLAN_MASK           equ 00h     ; VLAN ID mask for filtering
REG_TX_VLAN_TAG         equ 08h     ; TX VLAN tag insertion register

;; Tornado checksum registers (Window 7)
REG_TX_CSUM_START       equ 0Ah
REG_TX_CSUM_STUFF       equ 0Ch
REG_RX_CSUM_STATUS      equ 0Ah     ; Window 1
REG_RX_CSUM_VALUE       equ 0Ch     ; Window 1

;; Commands
CMD_SELECT_WINDOW       equ 0800h
CMD_ACK_INT             equ 6800h
CMD_DN_STALL            equ 3002h
CMD_DN_UNSTALL          equ 3003h
CMD_UP_STALL            equ 3000h
CMD_UP_UNSTALL          equ 3001h
CMD_STATS_ENABLE        equ 0A800h
CMD_TX_ENABLE           equ 4800h   ; TxEnable (Window 4, offset 800h)
CMD_TX_RESET            equ 5800h   ; TxReset
CMD_RX_RESET            equ 2800h   ; RxReset
CMD_RX_ENABLE           equ 2000h   ; RxEnable
CMD_SET_VLAN_TAG        equ 9000h   ; Tornado-specific: set VLAN tag

;; Interrupt bits (16-bit legacy)
INT_LATCH               equ 0001h
INT_HOST_ERROR          equ 0002h
INT_TX_COMPLETE         equ 0004h
INT_RX_COMPLETE         equ 0010h
INT_DN_COMPLETE         equ 0020h
INT_UP_COMPLETE         equ 0040h
INT_UPDATE_STATS        equ 0080h
INT_LINK_EVENT          equ 0100h

;; Extended interrupt bits (Tornado 32-bit status)
INT_EXT_TX_UNDERRUN     equ 00010000h
INT_EXT_RX_OVERRUN      equ 00020000h
INT_EXT_VLAN_TAG        equ 00040000h  ; VLAN tag detected on RX

;; ---- Scatter-gather DPD (Tornado extended) ------------------------------
;; Tornado DPD supports up to 63 fragments per descriptor.
;; DPD layout (variable length):
;;   +00h: DnNextPtr      (dword)
;;   +04h: FrameStartHdr  (dword) - length | flags | SG fragment count
;;   +08h: Frag0.Addr     (dword) - physical address of fragment 0
;;   +0Ch: Frag0.Length    (dword) - byte count | last-frag flag
;;   +10h: Frag1.Addr     (dword) - (optional) fragment 1
;;   +14h: Frag1.Length    (dword)
;;   ...
DPD_MIN_SIZE            equ 16      ; 1-fragment DPD
DPD_FRAG_SIZE           equ 8       ; each additional fragment adds 8 bytes
DPD_MAX_FRAGS           equ 63
DPD_LAST_FRAG           equ 80000000h
DPD_CSUM_ENABLE         equ 20000000h
DPD_ADD_IP_CSUM         equ 02000000h
DPD_ADD_TCP_CSUM        equ 04000000h
DPD_VLAN_TAG_INSERT     equ 10000000h  ; Insert VLAN tag from TX_VLAN_TAG reg

;; UPD status bits
UPD_COMPLETE            equ 00008000h
UPD_ERROR               equ 00004000h
UPD_LEN_MASK            equ 00001FFFh
UPD_CSUM_CHECKED        equ 00010000h
UPD_TCP_CSUM_OK         equ 00020000h
UPD_IP_CSUM_OK          equ 00040000h
UPD_VLAN_TAGGED         equ 00080000h  ; Packet had VLAN tag (stripped)
UPD_VLAN_TAG_SHIFT      equ 20         ; VLAN tag in bits 31:20

UPD_SIZE                equ 16
TX_RING_SIZE            equ 32
RX_RING_SIZE            equ 64
MAX_FRAME_SIZE          equ 1514
MAX_SG_FRAGS            equ 8       ; practical limit for packet driver

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; MODULE SEGMENT
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
section MOD_TORNADO_TEXT class=MODULE

global _mod_tornado_header

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 64-byte module header
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
_mod_tornado_header:
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
    db NIC_TYPE_TORNADO         ; nic_type
    dw CAP_FLAGS                ; cap_flags
    times (64 - ($ - header)) db 0

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; HOT SECTION
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
hot_start:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; send_packet - Submit scatter-gather DPD with VLAN tag insertion
;;
;; Input:  DS:SI = scatter-gather list pointer (array of addr/len pairs)
;;         CX = total packet length
;;         BX = TX ring producer index
;;         DL = flags: bit 0 = IP csum, bit 1 = TCP csum, bit 2 = VLAN insert
;;         DH = fragment count (1-8)
;;         [bp+4] = VLAN TCI (if DL bit 2 set)
;; Output: CF=0 success, CF=1 error
;; Clobbers: EAX, EDX, ECX, EBX, ESI, EDI
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
send_packet:
    push    ebp
    mov     ebp, esp
    push    ebx
    push    esi
    push    edi

    cmp     cx, MAX_FRAME_SIZE
    ja      send_tx_err
    or      cx, cx
    jz      send_tx_err
    cmp     dh, MAX_SG_FRAGS
    ja      send_tx_err

    ;; ---- VLAN tag insertion setup (Window 7) ---------------------------
    push    dx                      ; save flags
    test    dl, 04h                 ; VLAN insert requested?
    jz      send_skip_vlan_setup

    PATCH_POINT pp_tx_vlan1
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_SELECT_WINDOW | 7
    out     dx, ax

    ;; Write VLAN TCI to TX tag register
    mov     ax, [ebp+4]            ; VLAN TCI from caller
    PATCH_POINT pp_tx_vlan2
    mov     dx, 0                   ; patched: io_base + REG_TX_VLAN_TAG
    out     dx, ax

send_skip_vlan_setup:
    pop     dx                      ; restore flags

    ;; ---- Calculate DPD address -----------------------------------------
    movzx   eax, bx
    ;; DPD size varies: 16 + (frag_count - 1) * 8, but we allocate max
    ;; Use fixed 80-byte slots (8 frags max): 16 + 7*8 = 72, round to 80
    imul    eax, 80
    PATCH_POINT pp_dpd_ring_base
    mov     edi, 0                  ; patched: DPD ring physical base
    add     edi, eax

    ;; ---- Populate DPD header -------------------------------------------
    ;; DnNextPtr = 0
    xor     eax, eax
    mov     [edi], eax

    ;; FrameStartHeader = total length | flags
    movzx   eax, cx
    test    dl, 01h
    jz      send_no_ip_csum
    or      eax, DPD_ADD_IP_CSUM | DPD_CSUM_ENABLE
send_no_ip_csum:
    test    dl, 02h
    jz      send_no_tcp_csum
    or      eax, DPD_ADD_TCP_CSUM | DPD_CSUM_ENABLE
send_no_tcp_csum:
    test    dl, 04h
    jz      send_no_vlan_flag
    or      eax, DPD_VLAN_TAG_INSERT
send_no_vlan_flag:
    mov     [edi+4], eax            ; DPD.FrameStartHeader

    ;; ---- Populate scatter-gather fragment list -------------------------
    ;; DS:SI points to array of {dword phys_addr, dword length} pairs
    movzx   ecx, dh                 ; fragment count
    lea     ebx, [edi+8]            ; point to first fragment slot
    push    ecx                     ; save frag count

send_sg_loop:
    cmp     ecx, 1
    je      send_sg_last

    ;; Non-last fragment: copy addr and length
    lodsd                           ; EAX = fragment physical address
    mov     [ebx], eax
    lodsd                           ; EAX = fragment length
    mov     [ebx+4], eax            ; no LAST_FRAG flag
    add     ebx, 8
    dec     ecx
    jmp     send_sg_loop

send_sg_last:
    ;; Last fragment: set LAST_FRAG bit
    lodsd
    mov     [ebx], eax              ; address
    lodsd
    or      eax, DPD_LAST_FRAG      ; mark as last
    mov     [ebx+4], eax

    pop     ecx                     ; restore frag count (unused)

    ;; ---- TX checksum offsets (if checksum enabled) ---------------------
    ;; (Reuse from Cyclone - same register layout)
    ;; Already set up via VLAN window switch above if needed

    ;; ---- Stall, program DnListPtr, unstall -----------------------------
    PATCH_POINT pp_tx_iobase1
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_DN_STALL
    out     dx, ax

    PATCH_POINT pp_tx_iobase2
    mov     dx, 0                   ; patched: io_base + REG_DMA_CTRL
    mov     cx, 1000
send_stall_wait:
    in      eax, dx
    test    eax, 00004000h
    jnz     send_stall_ok
    loop    send_stall_wait
send_stall_ok:

    PATCH_POINT pp_tx_iobase3
    mov     dx, 0                   ; patched: io_base + REG_DN_LIST_PTR
    mov     eax, edi
    out     dx, eax

    PATCH_POINT pp_tx_iobase4
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_DN_UNSTALL
    out     dx, ax

    ;; ---- Kick download engine via poll register ------------------------
    PATCH_POINT pp_tx_iobase5
    mov     dx, 0                   ; patched: io_base + REG_DN_POLL
    mov     al, 01h
    out     dx, al

    pop     edi
    pop     esi
    pop     ebx
    pop     ebp
    clc
    ret

send_tx_err:
    pop     edi
    pop     esi
    pop     ebx
    pop     ebp
    stc
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; recv_packet - Consume UPD with VLAN extraction and checksum status
;;
;; Input:  ES:DI = buffer, CX = buffer size, BX = RX ring consumer index
;; Output: CF=0 success
;;           CX = packet length
;;           AH = checksum status (bit 0=checked, bit 1=tcp_ok, bit 2=ip_ok)
;;           DX = VLAN TCI (0 if no VLAN tag)
;;         CF=1 no packet
;; Clobbers: EAX, ECX, EBX, EDX, ESI
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
recv_packet:
    push    ebx
    push    esi
    push    edi

    ;; ---- Calculate UPD address -----------------------------------------
    movzx   eax, bx
    shl     eax, 4                  ; * UPD_SIZE
    PATCH_POINT pp_upd_ring_base
    mov     esi, 0                  ; patched: UPD ring physical base
    add     esi, eax

    ;; ---- Check UPD status ----------------------------------------------
    mov     eax, [esi+4]            ; UpPktStatus
    test    eax, UPD_COMPLETE
    jz      recv_rx_none

    test    eax, UPD_ERROR
    jnz     recv_rx_error

    ;; ---- Save status for checksum and VLAN extraction ------------------
    mov     ebx, eax                ; save full 32-bit status

    ;; ---- Extract VLAN tag if present -----------------------------------
    xor     edx, edx                ; default: no VLAN
    test    ebx, UPD_VLAN_TAGGED
    jz      recv_no_vlan_tag

    ;; Read VLAN tag from extended UPD status (bits 31:20)
    mov     edx, ebx
    shr     edx, UPD_VLAN_TAG_SHIFT
    and     dx, 0FFFh               ; 12-bit VLAN ID
recv_no_vlan_tag:
    push    dx                      ; save VLAN TCI

    ;; ---- Extract checksum status ---------------------------------------
    mov     eax, ebx
    shr     eax, 16
    ;; AL: bit 0 = csum_checked, bit 1 = tcp_ok, bit 2 = ip_ok
    push    ax                      ; save csum status

    ;; ---- Extract packet length -----------------------------------------
    mov     eax, ebx
    and     eax, UPD_LEN_MASK
    movzx   ecx, cx
    cmp     eax, ecx
    jbe     recv_rx_len_ok
    mov     eax, ecx
recv_rx_len_ok:
    mov     ecx, eax                ; ECX = actual length

    ;; ---- Reset UPD for reuse -------------------------------------------
    mov     dword [esi+4], 0

    ;; ---- Unstall upload engine -----------------------------------------
    PATCH_POINT pp_rx_iobase1
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_UP_UNSTALL
    out     dx, ax

    ;; ---- Return values -------------------------------------------------
    pop     ax                      ; AH = checksum status
    mov     ah, al
    pop     dx                      ; DX = VLAN TCI
    movzx   cx, cx                  ; CX = packet length

    pop     edi
    pop     esi
    pop     ebx
    clc
    ret

recv_rx_error:
    mov     dword [esi+4], 0        ; reset UPD
recv_rx_none:
    pop     edi
    pop     esi
    pop     ebx
    xor     cx, cx
    xor     dx, dx
    stc
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; handle_interrupt - Tornado ISR with extended 32-bit status
;;
;; The Tornado uses a 32-bit interrupt status register that includes
;; additional bits for TX underrun, RX overrun, and VLAN tag detection.
;;
;; Input:  (from TSR wrapper)
;; Output: CF=0 ours, CF=1 not ours
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
handle_interrupt:
    push    ebx

    ;; ---- Read legacy 16-bit IntStatus first ----------------------------
    PATCH_POINT pp_isr_iobase1
    mov     dx, 0                   ; patched: io_base + REG_INT_STATUS
    in      ax, dx

    test    ax, INT_LATCH
    jz      isr_not_ours

    mov     bx, ax                  ; save 16-bit status

    ;; ---- Read extended 32-bit status (Tornado-specific) ----------------
    PATCH_POINT pp_isr_ext1
    mov     dx, 0                   ; patched: io_base + REG_INT_STATUS_EXT
    in      eax, dx
    push    eax                     ; save extended status

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
    out     dx, al                  ; clear TX status

isr_chk_rx:
    ;; ---- RX complete ---------------------------------------------------
    test    bx, INT_RX_COMPLETE
    jz      isr_chk_stats
    PATCH_POINT pp_isr_iobase6
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_RX_COMPLETE
    out     dx, ax

isr_chk_stats:
    ;; ---- Statistics update ---------------------------------------------
    test    bx, INT_UPDATE_STATS
    jz      isr_chk_link
    PATCH_POINT pp_isr_iobase7
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_UPDATE_STATS
    out     dx, ax

isr_chk_link:
    ;; ---- Link event ----------------------------------------------------
    test    bx, INT_LINK_EVENT
    jz      isr_chk_ext
    PATCH_POINT pp_isr_iobase8
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_LINK_EVENT
    out     dx, ax

isr_chk_ext:
    ;; ---- Extended status: TX underrun ----------------------------------
    pop     eax                     ; restore extended status
    push    eax

    test    eax, INT_EXT_TX_UNDERRUN
    jz      isr_chk_rxover
    ;; TX underrun: reset TX engine (handled by TSR wrapper)
    PATCH_POINT pp_isr_ext2
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_TX_RESET        ; TxReset
    out     dx, ax
    ;; Re-enable TX
    mov     ax, CMD_TX_ENABLE       ; TxEnable
    out     dx, ax

isr_chk_rxover:
    pop     eax
    push    eax
    test    eax, INT_EXT_RX_OVERRUN
    jz      isr_chk_vlan
    ;; RX overrun: reset RX engine
    PATCH_POINT pp_isr_ext3
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_RX_RESET        ; RxReset
    out     dx, ax
    mov     ax, CMD_RX_ENABLE       ; RxEnable
    out     dx, ax

isr_chk_vlan:
    pop     eax
    test    eax, INT_EXT_VLAN_TAG
    jz      isr_chk_err
    ;; VLAN tag detected: the UPD status already contains the tag.
    ;; No special handling needed in ISR; recv_packet extracts it.

isr_chk_err:
    ;; ---- Host error ----------------------------------------------------
    test    bx, INT_HOST_ERROR
    jz      isr_ack
    PATCH_POINT pp_isr_iobase9
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_HOST_ERROR
    out     dx, ax

isr_ack:
    ;; ---- Acknowledge latch ---------------------------------------------
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
    PATCH_TABLE_ENTRY pp_tx_vlan1,      PATCH_TYPE_IO     ; command (win 7)
    PATCH_TABLE_ENTRY pp_tx_vlan2,      PATCH_TYPE_IO     ; VLAN tag reg
    PATCH_TABLE_ENTRY pp_dpd_ring_base, PATCH_TYPE_IMM16  ; DPD ring base
    PATCH_TABLE_ENTRY pp_tx_iobase1,    PATCH_TYPE_IO     ; command (stall)
    PATCH_TABLE_ENTRY pp_tx_iobase2,    PATCH_TYPE_IO     ; DMA ctrl
    PATCH_TABLE_ENTRY pp_tx_iobase3,    PATCH_TYPE_IO     ; DnListPtr
    PATCH_TABLE_ENTRY pp_tx_iobase4,    PATCH_TYPE_IO     ; command (unstall)
    PATCH_TABLE_ENTRY pp_tx_iobase5,    PATCH_TYPE_IO     ; DnPoll
    ;; recv_packet
    PATCH_TABLE_ENTRY pp_upd_ring_base, PATCH_TYPE_IMM16  ; UPD ring base
    PATCH_TABLE_ENTRY pp_rx_iobase1,    PATCH_TYPE_IO     ; command (unstall)
    ;; ISR
    PATCH_TABLE_ENTRY pp_isr_iobase1,   PATCH_TYPE_IO     ; IntStatus
    PATCH_TABLE_ENTRY pp_isr_ext1,      PATCH_TYPE_IO     ; ExtIntStatus
    PATCH_TABLE_ENTRY pp_isr_iobase2,   PATCH_TYPE_IO     ; ack upComplete
    PATCH_TABLE_ENTRY pp_isr_iobase3,   PATCH_TYPE_IO     ; ack dnComplete
    PATCH_TABLE_ENTRY pp_isr_iobase4,   PATCH_TYPE_IO     ; ack txComplete
    PATCH_TABLE_ENTRY pp_isr_iobase5,   PATCH_TYPE_IO     ; TX status
    PATCH_TABLE_ENTRY pp_isr_iobase6,   PATCH_TYPE_IO     ; ack rxComplete
    PATCH_TABLE_ENTRY pp_isr_iobase7,   PATCH_TYPE_IO     ; ack updateStats
    PATCH_TABLE_ENTRY pp_isr_iobase8,   PATCH_TYPE_IO     ; ack linkEvent
    PATCH_TABLE_ENTRY pp_isr_ext2,      PATCH_TYPE_IO     ; command (TxReset)
    PATCH_TABLE_ENTRY pp_isr_ext3,      PATCH_TYPE_IO     ; command (RxReset)
    PATCH_TABLE_ENTRY pp_isr_iobase9,   PATCH_TYPE_IO     ; ack hostError
    PATCH_TABLE_ENTRY pp_isr_iobase10,  PATCH_TYPE_IO     ; ack latch
PATCH_COUNT equ ($ - patch_table) / 4
