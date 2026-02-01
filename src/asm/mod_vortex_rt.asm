;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file mod_vortex_rt.asm
;; @brief JIT runtime module for 3Com Vortex PCI PIO NICs (3C590/595/597)
;;
;; 3Com Packet Driver - NIC-specific runtime module
;; Assembled at load time by the JIT patch engine into the TSR resident image.
;;
;; The Vortex generation uses PCI bus with Programmed I/O (no bus-master DMA).
;; Register access is window-based like EtherLink III.  On 386+ systems the
;; module uses REP INSD/OUTSD for 32-bit FIFO access; an 8086/286 fallback
;; uses REP INSW/OUTSW.
;;
;; Functions:
;;   - send_packet      : transmit via TX FIFO (PIO, 16/32-bit)
;;   - recv_packet      : receive from RX FIFO (PIO, 16/32-bit)
;;   - handle_interrupt  : top-half ISR
;;
;; Last Updated: 2026-02-01 11:02:20 CET
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16
cpu 286

%include "patch_macros.inc"

;; ---- Additional patch type constants ------------------------------------
PATCH_TYPE_IMM16        equ 6
PATCH_TYPE_IMM8         equ 7
PATCH_TYPE_RELOC_NEAR   equ 8

;; ---- NIC identity -------------------------------------------------------
NIC_TYPE_VORTEX         equ 3
CPU_REQ_286             equ 2
CAP_FLAGS_PCI_BUS       equ 0010h

;; ---- Vortex register offsets (from io_base) -----------------------------
REG_DATA32              equ 00h     ; 32-bit data port (FIFO)
REG_DATA16              equ 00h     ; 16-bit data port (shared)
REG_TX_FREE             equ 0Ch     ; TX free bytes (Window 1)
REG_TX_STATUS           equ 04h     ; TX status register
REG_RX_STATUS           equ 08h     ; RX status (Window 1)
REG_INT_STATUS          equ 0Ah     ; Interrupt status
REG_COMMAND             equ 0Eh     ; Command register

;; Vortex-specific registers (Window 4 - diagnostics)
REG_NET_DIAG            equ 06h     ; Network diagnostics
REG_FIFO_DIAG           equ 04h     ; FIFO diagnostics
REG_MEDIA_STATUS        equ 0Ah     ; Media status

;; Commands
CMD_SELECT_WINDOW       equ 0800h
CMD_ACK_INT             equ 6800h
CMD_TX_ENABLE           equ 9800h
CMD_RX_ENABLE           equ 2000h
CMD_RX_DISCARD          equ 8000h
CMD_SET_INT_ENABLE      equ 7000h
CMD_STATS_ENABLE        equ 0A800h

;; Interrupt bits
INT_LATCH               equ 0001h
INT_HOST_ERROR          equ 0002h
INT_TX_COMPLETE         equ 0004h
INT_TX_AVAILABLE        equ 0008h
INT_RX_COMPLETE         equ 0010h
INT_RX_EARLY            equ 0020h
INT_UPDATE_STATS        equ 0080h

MAX_FRAME_SIZE          equ 1514

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; MODULE SEGMENT
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
section MOD_VORTEX_TEXT class=MODULE

global _mod_vortex_header

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 64-byte module header
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
_mod_vortex_header:
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
    db CPU_REQ_286              ; cpu_requirements
    db NIC_TYPE_VORTEX          ; nic_type
    dw CAP_FLAGS_PCI_BUS        ; cap_flags
    times (64 - ($ - header)) db 0

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; HOT SECTION
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
hot_start:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; send_packet - Transmit via Vortex PIO FIFO
;;
;; Input:  DS:SI = packet data, CX = length
;; Output: CF=0 success, CF=1 error
;; Clobbers: AX, DX, SI, CX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
send_packet:
    push    bx
    push    cx

    cmp     cx, MAX_FRAME_SIZE
    ja      tx_err
    or      cx, cx
    jz      tx_err

    ;; ---- Select Window 1 -----------------------------------------------
    PATCH_POINT pp_tx_iobase1
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_SELECT_WINDOW | 1
    out     dx, ax

    ;; ---- Check TX free space -------------------------------------------
    PATCH_POINT pp_tx_iobase2
    mov     dx, 0                   ; patched: io_base + REG_TX_FREE
    in      ax, dx
    pop     bx                      ; BX = length
    push    bx
    cmp     ax, bx
    jb      tx_busy

    ;; ---- Write TX preamble (packet length) -----------------------------
    PATCH_POINT pp_tx_iobase3
    mov     dx, 0                   ; patched: io_base + REG_DATA16
    pop     cx
    push    cx
    mov     ax, cx
    out     dx, ax

    ;; ---- Transfer data via PIO -----------------------------------------
    ;; The JIT engine patches this block: on 386+ it becomes REP OUTSD,
    ;; on 286/8086 it stays as REP OUTSW.
    pop     cx
    push    cx

    ;; 16-bit PIO path (default, safe for all CPUs)
    PATCH_POINT pp_tx_pio_mode
    ;; If 386+, this NOP sled is replaced with:
    ;;   shr cx, 2 / rep outsd / ... remainder handling
    ;; Default 16-bit path:
    inc     cx
    shr     cx, 1
    rep     outsw

    ;; ---- Pad to DWORD boundary -----------------------------------------
    pop     cx
    and     cx, 3
    jz      tx_pad_done
    xor     ax, ax
    out     dx, ax
tx_pad_done:

    ;; ---- Poll TX status for completion ---------------------------------
    PATCH_POINT pp_tx_iobase4
    mov     dx, 0                   ; patched: io_base + REG_TX_STATUS
    mov     cx, 3000
tx_wait:
    in      al, dx
    test    al, 80h
    jnz     tx_complete
    loop    tx_wait
tx_complete:
    out     dx, al                  ; clear TX status

    pop     bx
    clc
    ret

tx_busy:
    pop     cx
    pop     bx
    mov     ax, 2
    stc
    ret

tx_err:
    pop     cx
    pop     bx
    stc
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; recv_packet - Receive from Vortex RX FIFO
;;
;; Input:  ES:DI = buffer, CX = buffer size
;; Output: CF=0 success (CX=bytes read), CF=1 no packet
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
recv_packet:
    push    bx

    ;; ---- Select Window 1 -----------------------------------------------
    PATCH_POINT pp_rx_iobase1
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_SELECT_WINDOW | 1
    out     dx, ax

    ;; ---- Check RX status -----------------------------------------------
    PATCH_POINT pp_rx_iobase2
    mov     dx, 0                   ; patched: io_base + REG_RX_STATUS
    in      ax, dx

    test    ax, 8000h               ; incomplete?
    jnz     rx_none
    test    ax, 4000h               ; error?
    jnz     rx_error

    ;; ---- Extract length ------------------------------------------------
    and     ax, 07FFh
    mov     bx, ax
    cmp     bx, cx
    jbe     rx_len_ok
    mov     bx, cx
rx_len_ok:

    ;; ---- Read via PIO --------------------------------------------------
    PATCH_POINT pp_rx_iobase3
    mov     dx, 0                   ; patched: io_base + REG_DATA16
    mov     cx, bx

    ;; 16-bit path (patched to 32-bit on 386+)
    PATCH_POINT pp_rx_pio_mode
    inc     cx
    shr     cx, 1
    rep     insw

    ;; ---- Discard packet from FIFO --------------------------------------
    PATCH_POINT pp_rx_iobase4
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_RX_DISCARD
    out     dx, ax

    mov     cx, bx
    pop     bx
    clc
    ret

rx_error:
    PATCH_POINT pp_rx_iobase5
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_RX_DISCARD
    out     dx, ax
rx_none:
    pop     bx
    xor     cx, cx
    stc
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; handle_interrupt - Vortex ISR
;;
;; Input:  (from TSR wrapper)
;; Output: CF=0 ours, CF=1 not ours
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
handle_interrupt:
    push    bx

    PATCH_POINT pp_isr_iobase1
    mov     dx, 0                   ; patched: io_base + REG_INT_STATUS
    in      ax, dx

    test    ax, INT_LATCH
    jz      isr_not_ours

    mov     bx, ax

    ;; ---- RX complete ---------------------------------------------------
    test    bx, INT_RX_COMPLETE
    jz      isr_chk_tx
    PATCH_POINT pp_isr_iobase2
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_RX_COMPLETE
    out     dx, ax

isr_chk_tx:
    ;; ---- TX complete ---------------------------------------------------
    test    bx, INT_TX_COMPLETE
    jz      isr_chk_avail
    PATCH_POINT pp_isr_iobase3
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_TX_COMPLETE
    out     dx, ax

    PATCH_POINT pp_isr_iobase4
    mov     dx, 0                   ; patched: io_base + REG_TX_STATUS
    in      al, dx
    out     dx, al

isr_chk_avail:
    ;; ---- TX available --------------------------------------------------
    test    bx, INT_TX_AVAILABLE
    jz      isr_chk_stats
    PATCH_POINT pp_isr_iobase5
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_TX_AVAILABLE
    out     dx, ax

isr_chk_stats:
    ;; ---- Update statistics ---------------------------------------------
    test    bx, INT_UPDATE_STATS
    jz      isr_chk_err
    PATCH_POINT pp_isr_iobase6
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_UPDATE_STATS
    out     dx, ax

isr_chk_err:
    ;; ---- Host error ----------------------------------------------------
    test    bx, INT_HOST_ERROR
    jz      isr_ack
    PATCH_POINT pp_isr_iobase7
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_HOST_ERROR
    out     dx, ax

isr_ack:
    PATCH_POINT pp_isr_iobase8
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_LATCH
    out     dx, ax

    pop     bx
    clc
    ret

isr_not_ours:
    pop     bx
    stc
    ret

hot_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; PATCH TABLE
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
patch_table:
    ;; send_packet
    PATCH_TABLE_ENTRY pp_tx_iobase1,  PATCH_TYPE_IO       ; command
    PATCH_TABLE_ENTRY pp_tx_iobase2,  PATCH_TYPE_IO       ; TxFree
    PATCH_TABLE_ENTRY pp_tx_iobase3,  PATCH_TYPE_IO       ; data port
    PATCH_TABLE_ENTRY pp_tx_pio_mode, PATCH_TYPE_COPY     ; 16->32 PIO
    PATCH_TABLE_ENTRY pp_tx_iobase4,  PATCH_TYPE_IO       ; TX status
    ;; recv_packet
    PATCH_TABLE_ENTRY pp_rx_iobase1,  PATCH_TYPE_IO       ; command
    PATCH_TABLE_ENTRY pp_rx_iobase2,  PATCH_TYPE_IO       ; RX status
    PATCH_TABLE_ENTRY pp_rx_iobase3,  PATCH_TYPE_IO       ; data port
    PATCH_TABLE_ENTRY pp_rx_pio_mode, PATCH_TYPE_COPY     ; 16->32 PIO
    PATCH_TABLE_ENTRY pp_rx_iobase4,  PATCH_TYPE_IO       ; command (discard)
    PATCH_TABLE_ENTRY pp_rx_iobase5,  PATCH_TYPE_IO       ; command (err)
    ;; ISR
    PATCH_TABLE_ENTRY pp_isr_iobase1, PATCH_TYPE_IO       ; IntStatus
    PATCH_TABLE_ENTRY pp_isr_iobase2, PATCH_TYPE_IO       ; ack RX
    PATCH_TABLE_ENTRY pp_isr_iobase3, PATCH_TYPE_IO       ; ack TX
    PATCH_TABLE_ENTRY pp_isr_iobase4, PATCH_TYPE_IO       ; TX status
    PATCH_TABLE_ENTRY pp_isr_iobase5, PATCH_TYPE_IO       ; ack TxAvail
    PATCH_TABLE_ENTRY pp_isr_iobase6, PATCH_TYPE_IO       ; ack stats
    PATCH_TABLE_ENTRY pp_isr_iobase7, PATCH_TYPE_IO       ; ack host err
    PATCH_TABLE_ENTRY pp_isr_iobase8, PATCH_TYPE_IO       ; ack latch
PATCH_COUNT equ ($ - patch_table) / 4
