;
; @file vortex_pio_fast.asm  
; @brief Optimized PIO routines for Vortex family NICs
;
; Implements window-minimized PIO loops from DRIVER_TUNING.md.
; Selects window once, then bursts through contiguous TX/RX FIFOs.
; Uses rep outsw/insw for maximum throughput.
;

SEGMENT _TEXT PUBLIC CLASS=CODE USE16

; External data
EXTERN _io_base: WORD

; Public functions
PUBLIC _vortex_tx_fast
PUBLIC _vortex_rx_fast
PUBLIC _vortex_select_window
PUBLIC _vortex_tx_burst
PUBLIC _vortex_rx_burst
PUBLIC _vortex_batch_stats

; Register offsets (Window-independent)
COMMAND_REG     EQU 0Eh        ; Command register
INT_STATUS      EQU 0Eh        ; Interrupt status (same as command)

; Window 0 registers
W0_EEPROM_CMD   EQU 0Ah
W0_EEPROM_DATA  EQU 0Ch

; Window 1 registers (TX/RX operations)
W1_TX_FIFO      EQU 00h        ; TX FIFO port
W1_RX_FIFO      EQU 00h        ; RX FIFO port (same as TX)
W1_TX_STATUS    EQU 0Bh        ; TX status
W1_TX_FREE      EQU 0Ch        ; TX free bytes
W1_RX_STATUS    EQU 08h        ; RX status port

; Window 3 registers
W3_INTERNAL_CFG EQU 00h
W3_MAC_CONTROL  EQU 06h

; Window 4 registers  
W4_MEDIA_STATUS EQU 0Ah
W4_BAD_SSD      EQU 0Ch

; Window 6 registers (Statistics)
W6_TX_PACKETS   EQU 00h
W6_RX_PACKETS   EQU 02h
W6_TX_BYTES     EQU 04h
W6_RX_BYTES     EQU 06h

; Command bits
CMD_SELECT_WINDOW EQU 0800h    ; Select register window
CMD_TX_ENABLE   EQU 0900h      ; Enable transmitter
CMD_RX_ENABLE   EQU 0A00h      ; Enable receiver
CMD_TX_RESET    EQU 0B00h      ; Reset transmitter
CMD_RX_RESET    EQU 0C00h      ; Reset receiver
CMD_ACK_INTR    EQU 0D00h      ; Acknowledge interrupt

; Status bits
RX_COMPLETE     EQU 8000h      ; RX complete in status
TX_COMPLETE     EQU 0004h      ; TX complete bit

;-----------------------------------------------------------------------------
; vortex_select_window - Select a register window
; Entry: AL = window number (0-7)
; Preserves: All except AX, DX
;-----------------------------------------------------------------------------
_vortex_select_window PROC NEAR
        push    dx
        
        mov     dx, [_io_base]
        add     dx, COMMAND_REG
        
        and     ax, 07h         ; Mask to valid window
        or      ax, CMD_SELECT_WINDOW
        out     dx, ax
        
        pop     dx
        ret
_vortex_select_window ENDP

;-----------------------------------------------------------------------------
; vortex_tx_fast - Optimized TX for even-sized packets
; Entry: DS:SI -> TX buffer (word-aligned)
;        CX = packet length (must be even)
; Exit:  AX = 0 on success, -1 on error
; Preserves: BX, BP
;-----------------------------------------------------------------------------
_vortex_tx_fast PROC NEAR
        push    dx
        push    cx
        push    si
        
        ; Select Window 1 once (TX/RX operations)
        mov     dx, [_io_base]
        add     dx, COMMAND_REG
        mov     ax, CMD_SELECT_WINDOW | 1
        out     dx, ax
        
        ; Check TX space available
        mov     dx, [_io_base]
        add     dx, W1_TX_FREE
        in      ax, dx
        
        pop     si              ; Restore SI
        pop     cx              ; Restore CX
        push    cx              ; Save CX again
        push    si              ; Save SI again
        
        cmp     ax, cx          ; Enough space?
        jb      .no_space
        
        ; Send TX length first
        mov     dx, [_io_base]
        add     dx, 10h         ; TX length register
        mov     ax, cx          ; Packet length
        out     dx, ax
        
        ; Stream PIO to TX FIFO
        mov     dx, [_io_base]
        add     dx, W1_TX_FIFO
        shr     cx, 1           ; Convert to words
        
        ; Fast burst using REP OUTSW
        cld                     ; Forward direction
        rep     outsw           ; Burst the frame
        
        ; Issue TX start command
        mov     dx, [_io_base]
        add     dx, COMMAND_REG
        mov     ax, 4800h       ; TxEnable command
        out     dx, ax
        
        xor     ax, ax          ; Success
        jmp     .done
        
.no_space:
        mov     ax, -1          ; Error
        
.done:
        pop     si
        pop     cx
        pop     dx
        ret
_vortex_tx_fast ENDP

;-----------------------------------------------------------------------------
; vortex_rx_fast - Optimized RX with minimal window switching
; Entry: ES:DI -> RX buffer (word-aligned)
; Exit:  AX = packet length, 0 if no packet
; Preserves: BX, BP
;-----------------------------------------------------------------------------
_vortex_rx_fast PROC NEAR
        push    dx
        push    cx
        push    di
        
        ; Already in Window 1 from TX (or select it)
        mov     dx, [_io_base]
        add     dx, COMMAND_REG
        mov     ax, CMD_SELECT_WINDOW | 1
        out     dx, ax
        
        ; Read RX status
        mov     dx, [_io_base]
        add     dx, W1_RX_STATUS
        in      ax, dx
        
        test    ax, RX_COMPLETE
        jz      .no_packet
        
        ; Get packet length (lower 11 bits)
        and     ax, 07FFh       ; Mask to get length
        mov     cx, ax          ; Save length
        push    cx              ; Save for return
        
        ; Check for odd length
        test    cx, 1
        jz      .even_length
        inc     cx              ; Round up for word transfer
        
.even_length:
        shr     cx, 1           ; Convert to words
        
        ; Stream from RX FIFO
        mov     dx, [_io_base]
        add     dx, W1_RX_FIFO
        
        ; Fast burst using REP INSW
        cld                     ; Forward direction
        rep     insw            ; Burst read
        
        ; Discard RX packet from FIFO
        mov     dx, [_io_base]
        add     dx, COMMAND_REG
        mov     ax, 4000h       ; RxDiscard command
        out     dx, ax
        
        pop     ax              ; Return packet length
        jmp     .done
        
.no_packet:
        xor     ax, ax          ; No packet
        
.done:
        pop     di
        pop     cx
        pop     dx
        ret
_vortex_rx_fast ENDP

;-----------------------------------------------------------------------------
; vortex_tx_burst - Transmit multiple packets without window changes
; Entry: DS:SI -> array of packet descriptors
;        CX = number of packets
;        Descriptor format:
;          +0: WORD length
;          +2: DWORD pointer to data
; Exit:  AX = packets sent
;-----------------------------------------------------------------------------
_vortex_tx_burst PROC NEAR
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        
        xor     bx, bx          ; Packet counter
        
        ; Select Window 1 once for all packets
        mov     dx, [_io_base]
        add     dx, COMMAND_REG
        mov     ax, CMD_SELECT_WINDOW | 1
        out     dx, ax
        
.packet_loop:
        test    cx, cx
        jz      .done
        
        ; Get packet length
        mov     ax, [si]        ; Length
        test    ax, ax
        jz      .skip_packet
        
        push    cx              ; Save packet count
        mov     cx, ax          ; CX = length
        
        ; Get packet pointer
        push    ds
        lds     di, [si+2]      ; DS:DI -> packet data
        
        ; Check TX space
        mov     dx, [_io_base]
        add     dx, W1_TX_FREE
        in      ax, dx
        cmp     ax, cx
        jb      .restore_ds     ; No space
        
        ; Send length
        mov     dx, [_io_base]
        add     dx, 10h
        mov     ax, cx
        out     dx, ax
        
        ; Burst data
        mov     dx, [_io_base]
        add     dx, W1_TX_FIFO
        push    si
        mov     si, di          ; DS:SI -> data
        shr     cx, 1
        rep     outsw
        pop     si
        
        inc     bx              ; Count packet
        
.restore_ds:
        pop     ds
        pop     cx
        
.skip_packet:
        add     si, 6           ; Next descriptor
        dec     cx
        jmp     .packet_loop
        
.done:
        mov     ax, bx          ; Return count
        
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
_vortex_tx_burst ENDP

;-----------------------------------------------------------------------------
; vortex_batch_stats - Read all statistics in one window operation
; Entry: ES:DI -> statistics buffer (8 words)
; Exit:  None
; Preserves: All except AX, CX, DX, DI
;-----------------------------------------------------------------------------
_vortex_batch_stats PROC NEAR
        push    cx
        push    dx
        push    di
        
        ; Select Window 6 (Statistics)
        mov     dx, [_io_base]
        add     dx, COMMAND_REG
        mov     ax, CMD_SELECT_WINDOW | 6
        out     dx, ax
        
        ; Read all stats in sequence
        mov     dx, [_io_base]
        add     dx, W6_TX_PACKETS
        mov     cx, 8           ; 8 statistics registers
        
.read_loop:
        in      ax, dx
        stosw                   ; Store and advance DI
        add     dx, 2           ; Next register
        loop    .read_loop
        
        ; Return to Window 1 (operational)
        mov     dx, [_io_base]
        add     dx, COMMAND_REG
        mov     ax, CMD_SELECT_WINDOW | 1
        out     dx, ax
        
        pop     di
        pop     dx
        pop     cx
        ret
_vortex_batch_stats ENDP

;-----------------------------------------------------------------------------
; vortex_rx_burst - Receive multiple packets without window changes
; Entry: ES:DI -> buffer array
;        CX = max packets to receive
; Exit:  AX = packets received
;-----------------------------------------------------------------------------
_vortex_rx_burst PROC NEAR
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        
        xor     bx, bx          ; Packet counter
        
        ; Select Window 1 once
        mov     dx, [_io_base]
        add     dx, COMMAND_REG
        mov     ax, CMD_SELECT_WINDOW | 1
        out     dx, ax
        
.packet_loop:
        test    cx, cx
        jz      .done
        
        ; Check RX status
        mov     dx, [_io_base]
        add     dx, W1_RX_STATUS
        in      ax, dx
        
        test    ax, RX_COMPLETE
        jz      .done           ; No more packets
        
        ; Get length
        and     ax, 07FFh
        push    ax              ; Save length
        
        ; Store length at buffer start
        stosw
        
        ; Read packet data
        mov     cx, ax
        test    cx, 1
        jz      .even
        inc     cx
.even:
        shr     cx, 1
        
        mov     dx, [_io_base]
        add     dx, W1_RX_FIFO
        rep     insw
        
        ; Discard from FIFO
        mov     dx, [_io_base]
        add     dx, COMMAND_REG
        mov     ax, 4000h       ; RxDiscard
        out     dx, ax
        
        pop     ax              ; Get length
        add     di, 1536        ; Advance to next buffer
        sub     di, ax          ; Adjust for actual length
        
        inc     bx              ; Count packet
        dec     cx
        jmp     .packet_loop
        
.done:
        mov     ax, bx          ; Return count
        
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
_vortex_rx_burst ENDP

ENDS

END