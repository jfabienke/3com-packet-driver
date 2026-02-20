;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file mod_3c509b_rt.asm
;; @brief JIT runtime module for 3Com 3C509B EtherLink III ISA NIC
;;
;; 3Com Packet Driver - NIC-specific runtime module
;; Assembled at load time by the JIT patch engine into the TSR resident image.
;;
;; This module implements the hot-path packet I/O for the 3C509B:
;;   - send_packet   : transmit a frame via the TX FIFO (PIO)
;;   - recv_packet   : receive a frame from the RX FIFO (PIO)
;;   - handle_interrupt : top-half ISR dispatching RX/TX/error events
;;
;; All port I/O addresses are expressed as offsets from a patchable io_base
;; so the JIT engine can relocate them to the detected base address.
;;
;; Last Updated: 2026-02-01 11:37:23 CET
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16
cpu 8086

%include "patch_macros.inc"

;; ---- Additional patch type constants (module-local) -----------------------
PATCH_TYPE_IMM16        equ 6
PATCH_TYPE_IMM8         equ 7
PATCH_TYPE_RELOC_NEAR   equ 8

;; ---- NIC identity ---------------------------------------------------------
NIC_TYPE_3C509B         equ 1
CPU_REQ_8086            equ 0
CAP_FLAGS_NONE          equ 0

;; ---- 3C509B register offsets (from io_base) --------------------------------
;; Window 0-7 selected via SelectWindow command written to CommandReg.
;; Data port at +00h, status at +0Ah (IntStatus), command at +0Eh.
REG_DATA                equ 00h     ; 16-bit data port (FIFO)
REG_TX_STATUS           equ 04h     ; TX status (byte, window 1)
REG_RX_STATUS           equ 08h     ; RX status (word, window 1)
REG_INT_STATUS          equ 0Ah     ; Interrupt status register
REG_COMMAND             equ 0Eh     ; Command register (write)
REG_WINDOW              equ 0Eh     ; Window register (same port)

;; 3C509B commands (written to REG_COMMAND)
CMD_SELECT_WINDOW       equ 0800h   ; OR with window number 0-7
CMD_TX_ENABLE           equ 9800h
CMD_RX_ENABLE           equ 2000h
CMD_SET_RX_FILTER       equ 8000h
CMD_ACK_INT             equ 6800h   ; OR with bits to acknowledge
CMD_TX_RESET            equ 5800h
CMD_RX_DISCARD          equ 8000h   ; Discard top RX packet

;; Interrupt status bits
INT_LATCH               equ 0001h
INT_ADAPTER_FAIL        equ 0002h
INT_TX_COMPLETE         equ 0004h
INT_TX_AVAILABLE        equ 0008h
INT_RX_COMPLETE         equ 0010h
INT_INT_REQUESTED       equ 0020h
INT_UPDATE_STATS        equ 0040h

;; TX/RX FIFO constants
TX_FIFO_SIZE            equ 2048
RX_FIFO_SIZE            equ 2048
MAX_FRAME_SIZE          equ 1514    ; Ethernet MTU + header

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; MODULE SEGMENT
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
section MOD_3C509B_TEXT class=MODULE

global _mod_3c509b_header

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 64-byte module header
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
_mod_3c509b_header:
header:
    db 'PKTDRV',0               ; 7 bytes - signature
    db 1                        ; version_major
    db 0                        ; version_minor
    dw hot_start                ; hot section start offset
    dw hot_end                  ; hot section end offset
    dw 0                        ; cold_start (unused for modules)
    dw 0                        ; cold_end   (unused for modules)
    dw patch_table              ; patch table offset
    dw PATCH_COUNT              ; number of patch entries
    dw (hot_end - header)       ; module_size
    dw (hot_end - hot_start)    ; required_memory
    db CPU_REQ_8086             ; cpu_requirements
    db NIC_TYPE_3C509B          ; nic_type
    dw CAP_FLAGS_NONE           ; cap_flags
    times (64 - ($ - header)) db 0  ; pad to 64 bytes

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; HOT SECTION - resident code patched into TSR
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
hot_start:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; C-CALLABLE WRAPPER EXPORTS
;;
;; These wrappers convert from Watcom large model calling convention:
;;   DX:AX = far pointer (first param)
;;   BX    = integer param (second param)
;;   CX    = integer param (third param)
;; To internal register-based calling:
;;   DS:SI = buffer pointer
;;   CX    = length
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; _3c509b_send_packet_ - C-callable wrapper for send_packet
;; Input:  DX:AX = buffer far pointer, BX = length
;; Return: AX = 0 on success, -1 on error
global _3c509b_send_packet_
_3c509b_send_packet_:
    push    ds
    push    si
    mov     ds, dx              ; DS = buffer segment
    mov     si, ax              ; SI = buffer offset
    mov     cx, bx              ; CX = length
    call    send_packet
    pop     si
    pop     ds
    jc      .error
    xor     ax, ax              ; return 0 = success
    retf
.error:
    mov     ax, -1              ; return -1 = error
    retf

;; _3c509b_receive_packet_ - C-callable wrapper for recv_packet
;; Input:  DX:AX = buffer far pointer, BX = buffer size
;; Return: AX = bytes received, or -1 on error/no packet
global _3c509b_receive_packet_
_3c509b_receive_packet_:
    push    es
    push    di
    mov     es, dx              ; ES = buffer segment
    mov     di, ax              ; DI = buffer offset
    mov     cx, bx              ; CX = buffer size
    call    recv_packet
    pop     di
    pop     es
    jc      .no_packet
    mov     ax, cx              ; return bytes received
    retf
.no_packet:
    mov     ax, -1              ; return -1 = no packet
    retf

;; _3c509b_handle_interrupt_ - C-callable wrapper for handle_interrupt
;; Input:  none
;; Return: void (flags preserved in CF for chained ISR)
global _3c509b_handle_interrupt_
_3c509b_handle_interrupt_:
    call    handle_interrupt
    retf

;; _3c509b_check_interrupt_ - Check if interrupt is from this NIC
;; Input:  none
;; Return: AX = 1 if ours, 0 if not
global _3c509b_check_interrupt_
_3c509b_check_interrupt_:
    call    handle_interrupt
    jc      .not_ours
    mov     ax, 1
    retf
.not_ours:
    xor     ax, ax
    retf

;; _3c509b_enable_interrupts_ - Enable NIC interrupts
;; Input:  none
;; Return: AX = 0
global _3c509b_enable_interrupts_
_3c509b_enable_interrupts_:
    ;; Set IntMask to enable desired interrupts
    PATCH_POINT pp_enable_int
    mov     dx, 0               ; patched: io_base + REG_COMMAND
    mov     ax, 0A01Fh          ; SetIntrMask | (TxComplete|RxComplete|IntLatch)
    out     dx, ax
    xor     ax, ax
    retf

;; _3c509b_disable_interrupts_ - Disable NIC interrupts
;; Input:  none
;; Return: AX = 0
global _3c509b_disable_interrupts_
_3c509b_disable_interrupts_:
    PATCH_POINT pp_disable_int
    mov     dx, 0               ; patched: io_base + REG_COMMAND
    mov     ax, 0A000h          ; SetIntrMask | 0 (disable all)
    out     dx, ax
    xor     ax, ax
    retf

;; _3c509b_get_link_status_ - Get link status
;; Return: AX = 1 if link up, 0 if down
global _3c509b_get_link_status_
_3c509b_get_link_status_:
    ;; 3C509B doesn't have explicit link status; assume link up if detected
    mov     ax, 1
    retf

;; _3c509b_get_link_speed_ - Get link speed
;; Return: AX = 10 (10 Mbps)
global _3c509b_get_link_speed_
_3c509b_get_link_speed_:
    mov     ax, 10              ; 3C509B is 10 Mbps only
    retf

;; Stub wrappers for functions not yet implemented (return 0)
global _3c509b_read_reg_
global _3c509b_write_reg_
global _3c509b_select_window_
global _3c509b_wait_for_cmd_busy_
global _3c509b_write_command_
global _3c509b_process_single_event_
global _3c509b_check_interrupt_batched_
global _3c509b_handle_interrupt_batched_
global _3c509b_set_promiscuous_
global _3c509b_set_multicast_
global _3c509b_receive_packet_buffered_
global _3c509b_send_packet_direct_pio_
global _3c509b_pio_prepare_rx_buffer_
global _3c509b_pio_complete_rx_buffer_
global _3c509b_pio_prepare_tx_buffer_
global _3c509b_receive_packet_cache_safe_

_3c509b_read_reg_:
_3c509b_wait_for_cmd_busy_:
_3c509b_process_single_event_:
_3c509b_check_interrupt_batched_:
_3c509b_set_promiscuous_:
_3c509b_set_multicast_:
_3c509b_receive_packet_buffered_:
_3c509b_pio_prepare_rx_buffer_:
_3c509b_pio_complete_rx_buffer_:
_3c509b_pio_prepare_tx_buffer_:
_3c509b_receive_packet_cache_safe_:
    xor     ax, ax
    retf

_3c509b_write_reg_:
_3c509b_select_window_:
_3c509b_write_command_:
_3c509b_handle_interrupt_batched_:
    retf

_3c509b_send_packet_direct_pio_:
    ;; Direct PIO send - same as regular send for 3C509B
    push    ds
    push    si
    mov     ds, dx
    mov     si, ax
    mov     cx, bx
    call    send_packet
    pop     si
    pop     ds
    jc      .pio_error
    xor     ax, ax
    retf
.pio_error:
    mov     ax, -1
    retf

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; INTERNAL FUNCTIONS
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; send_packet - Transmit a frame through the 3C509B TX FIFO
;;
;; Input:  DS:SI = pointer to packet data
;;         CX    = packet length in bytes
;; Output: CF=0 success, CF=1 error (AX=error code)
;; Clobbers: AX, DX, SI, CX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
send_packet:
    push    bx
    push    cx                      ; save length for later

    ;; ---- Validate packet length ----------------------------------------
    cmp     cx, MAX_FRAME_SIZE
    ja      tx_too_large
    or      cx, cx
    jz      tx_too_large

    ;; ---- Select Window 1 for TX operations -----------------------------
    PATCH_POINT pp_tx_iobase1
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_SELECT_WINDOW | 1
    out     dx, ax

    ;; ---- Check TX free bytes (Window 1, offset +0Ch) -------------------
    PATCH_POINT pp_tx_iobase2
    mov     dx, 0                   ; patched: io_base + 0Ch (TxFree)
    in      ax, dx
    pop     bx                      ; BX = packet length
    push    bx
    cmp     ax, bx
    jb      tx_fifo_busy            ; not enough room

    ;; ---- Write TX preamble: length | 0x00 (no interrupt on success) ----
    PATCH_POINT pp_tx_iobase3
    mov     dx, 0                   ; patched: io_base + REG_DATA
    pop     cx                      ; CX = packet length
    push    cx
    mov     ax, cx                  ; TX preamble: just the byte count
    out     dx, ax

    ;; ---- Blast packet data to FIFO (word-at-a-time, 8086 compatible) ---
    ;; DX already points to data port
    pop     cx                      ; CX = packet length
    push    cx
    inc     cx                      ; round up to word count
    shr     cx, 1
tx_outsw_loop:
    lodsw                           ; load word from DS:SI into AX
    out     dx, ax                  ; write word to port
    loop    tx_outsw_loop

    ;; ---- Pad to DWORD boundary (3C509B requirement) --------------------
    pop     cx
    and     cx, 3
    jz      tx_pad_done
    mov     ax, 0
    out     dx, ax                  ; pad word
tx_pad_done:

    ;; ---- Wait for TX complete ------------------------------------------
    PATCH_POINT pp_tx_iobase4
    mov     dx, 0                   ; patched: io_base + REG_TX_STATUS
    mov     cx, 2000                ; timeout loop
tx_wait:
    in      al, dx
    test    al, 80h                 ; txComplete bit
    jnz     tx_done
    loop    tx_wait
    ;; TX timeout - non-fatal, frame was queued
tx_done:
    ;; Clear TX status by writing back
    out     dx, al

    pop     bx
    clc
    ret

tx_too_large:
    pop     bx
    mov     ax, 1                   ; error: bad length
    stc
    ret

tx_fifo_busy:
    pop     cx                      ; discard saved length
    pop     bx
    mov     ax, 2                   ; error: FIFO busy
    stc
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; recv_packet - Receive a frame from the 3C509B RX FIFO
;;
;; Input:  ES:DI = destination buffer
;;         CX    = buffer size (max bytes to read)
;; Output: CF=0 success, CX = actual bytes read
;;         CF=1 no packet available (or error)
;; Clobbers: AX, DX, DI, CX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
recv_packet:
    push    bx

    ;; ---- Select Window 1 for RX operations -----------------------------
    PATCH_POINT pp_rx_iobase1
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_SELECT_WINDOW | 1
    out     dx, ax

    ;; ---- Check RX status -----------------------------------------------
    PATCH_POINT pp_rx_iobase2
    mov     dx, 0                   ; patched: io_base + REG_RX_STATUS
    in      ax, dx

    test    ax, 8000h               ; bit 15 = incomplete
    jnz     rx_none                 ; packet not ready
    test    ax, 4000h               ; bit 14 = error
    jnz     rx_error

    ;; ---- Extract packet length (bits 0-10) -----------------------------
    and     ax, 07FFh               ; mask length
    mov     bx, ax                  ; BX = packet length
    cmp     bx, cx
    jbe     rx_len_ok
    mov     bx, cx                  ; truncate to buffer size
rx_len_ok:

    ;; ---- Read packet from FIFO (word-at-a-time, 8086 compatible) -------
    PATCH_POINT pp_rx_iobase3
    mov     dx, 0                   ; patched: io_base + REG_DATA
    mov     cx, bx
    inc     cx
    shr     cx, 1                   ; word count
rx_insw_loop:
    in      ax, dx                  ; read word from port
    stosw                           ; store to ES:DI
    loop    rx_insw_loop

    ;; ---- Issue RX discard to advance to next packet --------------------
    PATCH_POINT pp_rx_iobase4
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_RX_DISCARD
    out     dx, ax

    mov     cx, bx                  ; return actual length
    pop     bx
    clc
    ret

rx_error:
    ;; Discard errored packet
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
;; handle_interrupt - Top-half ISR for 3C509B
;;
;; Input:  (called from TSR ISR wrapper)
;; Output: CF=0 if interrupt was ours, CF=1 if not
;; Clobbers: AX, DX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
handle_interrupt:
    push    bx

    ;; ---- Read IntStatus ------------------------------------------------
    PATCH_POINT pp_isr_iobase1
    mov     dx, 0                   ; patched: io_base + REG_INT_STATUS
    in      ax, dx

    test    ax, INT_LATCH
    jz      isr_not_ours            ; not our interrupt

    mov     bx, ax                  ; save status

    ;; ---- Handle RX complete --------------------------------------------
    test    bx, INT_RX_COMPLETE
    jz      isr_check_tx
    ;; Signal upper layer that RX is ready (set flag in shared TSR data)
    ;; The main TSR loop will call recv_packet
    PATCH_POINT pp_isr_iobase2
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_RX_COMPLETE
    out     dx, ax

isr_check_tx:
    ;; ---- Handle TX complete --------------------------------------------
    test    bx, INT_TX_COMPLETE
    jz      isr_check_err
    PATCH_POINT pp_isr_iobase3
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_TX_COMPLETE
    out     dx, ax

    ;; Read and clear TX status
    PATCH_POINT pp_isr_iobase4
    mov     dx, 0                   ; patched: io_base + REG_TX_STATUS
    in      al, dx
    out     dx, al                  ; clear by writing back

isr_check_err:
    ;; ---- Handle adapter failure ----------------------------------------
    test    bx, INT_ADAPTER_FAIL
    jz      isr_done
    PATCH_POINT pp_isr_iobase5
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_ADAPTER_FAIL
    out     dx, ax

isr_done:
    ;; ---- Acknowledge interrupt latch -----------------------------------
    PATCH_POINT pp_isr_iobase6
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_LATCH
    out     dx, ax

    pop     bx
    clc                             ; interrupt was ours
    ret

isr_not_ours:
    pop     bx
    stc                             ; not our interrupt
    ret

hot_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; PATCH TABLE
;;
;; Each entry: PATCH_TABLE_ENTRY <patch_point_label>, <patch_type>
;; The JIT engine uses this to locate and fill in io_base-relative addresses.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
patch_table:
    ;; send_packet I/O base patches
    PATCH_TABLE_ENTRY pp_tx_iobase1,  PATCH_TYPE_IO   ; command reg
    PATCH_TABLE_ENTRY pp_tx_iobase2,  PATCH_TYPE_IO   ; TxFree reg
    PATCH_TABLE_ENTRY pp_tx_iobase3,  PATCH_TYPE_IO   ; data port
    PATCH_TABLE_ENTRY pp_tx_iobase4,  PATCH_TYPE_IO   ; TX status
    ;; recv_packet I/O base patches
    PATCH_TABLE_ENTRY pp_rx_iobase1,  PATCH_TYPE_IO   ; command reg
    PATCH_TABLE_ENTRY pp_rx_iobase2,  PATCH_TYPE_IO   ; RX status
    PATCH_TABLE_ENTRY pp_rx_iobase3,  PATCH_TYPE_IO   ; data port
    PATCH_TABLE_ENTRY pp_rx_iobase4,  PATCH_TYPE_IO   ; command reg
    PATCH_TABLE_ENTRY pp_rx_iobase5,  PATCH_TYPE_IO   ; command reg (error)
    ;; handle_interrupt I/O base patches
    PATCH_TABLE_ENTRY pp_isr_iobase1, PATCH_TYPE_IO   ; IntStatus
    PATCH_TABLE_ENTRY pp_isr_iobase2, PATCH_TYPE_IO   ; command (ack RX)
    PATCH_TABLE_ENTRY pp_isr_iobase3, PATCH_TYPE_IO   ; command (ack TX)
    PATCH_TABLE_ENTRY pp_isr_iobase4, PATCH_TYPE_IO   ; TX status
    PATCH_TABLE_ENTRY pp_isr_iobase5, PATCH_TYPE_IO   ; command (ack err)
    PATCH_TABLE_ENTRY pp_isr_iobase6, PATCH_TYPE_IO   ; command (ack latch)
    ;; interrupt enable/disable patches
    PATCH_TABLE_ENTRY pp_enable_int,  PATCH_TYPE_IO   ; command (enable)
    PATCH_TABLE_ENTRY pp_disable_int, PATCH_TYPE_IO   ; command (disable)
PATCH_COUNT equ ($ - patch_table) / 4
