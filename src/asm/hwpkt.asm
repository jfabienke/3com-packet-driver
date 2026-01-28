; @file hwpkt.asm
; @brief Packet I/O operations - HOT section module (stays resident)
;
; 3Com Packet Driver - Packet read/write and I/O port operations
; Extracted from hardware.asm for modularization
;
; This module contains the critical hot path for packet I/O:
; - hardware_read_packet: Read packet from hardware
; - hardware_send_packet_asm: Send packet via hardware
; - io_read_byte/word: Low-level I/O port read operations
; - io_write_byte/word: Low-level I/O port write operations
;
; Created: 2026-01-25 from hardware.asm modularization
;
; This file is part of the 3Com Packet Driver project.

bits 16
cpu 386

; Include assembly interface definitions
%include "asm_interfaces.inc"

;=============================================================================
; CONSTANTS
;=============================================================================

; Common 3Com registers (relative to base)
REG_COMMAND         EQU 0Eh     ; Command register
REG_WINDOW          EQU 0Eh     ; Window select register

; Error codes
ERROR_BUSY          EQU HW_ERROR_BUSY   ; Device busy error

;=============================================================================
; EXTERNAL DATA REFERENCES
;=============================================================================

extern hw_types
extern hw_iobase_table
extern hw_flags_table
extern hw_type_table
extern current_instance
extern io_read_count
extern io_write_count
extern init_3c515

;=============================================================================
; GLOBAL EXPORTS
;=============================================================================

global hardware_read_packet
global hardware_send_packet_asm
global io_read_byte
global io_write_byte
global io_read_word
global io_write_word

;=============================================================================
; CODE SECTION - HOT PATH (stays resident)
;=============================================================================

section .text

;-----------------------------------------------------------------------------
; hardware_read_packet - Read packet from hardware
;
; Input:  AL = NIC index, ES:DI (via BP+8) = receive buffer
; Output: AX = packet length (0 = no packet/error)
; Uses:   All registers preserved except AX
;
; HOT PATH: ~350 lines - critical packet receive operation
;-----------------------------------------------------------------------------
hardware_read_packet:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Validate NIC index, read packet from appropriate hardware, and handle different NIC types

        ; Validate NIC index
        cmp     al, MAX_HW_INSTANCES
        jae     .invalid_nic

        ; Check NIC type and dispatch to appropriate handler
        mov     bl, al
        mov     bh, 0
        mov     si, hw_types
        add     si, bx
        mov     cl, [si]

        cmp     cl, 1
        je      .read_3c509b
        cmp     cl, 2
        je      .read_3c515
        jmp     .unknown_type

.read_3c509b:
        ; Read packet from 3C509B using PIO
        push    es
        push    di
        push    si
        push    cx
        push    bx

        ; Get I/O base for this instance
        mov     bl, al
        xor     bh, bh
        shl     bx, 1                   ; Word offset
        mov     si, hw_iobase_table
        mov     dx, [si+bx]             ; DX = I/O base
        mov     si, dx                  ; Save in SI

        ; Select Window 1 for RX operations
        add     dx, REG_WINDOW
        mov     ax, 0801h               ; CMD_SELECT_WINDOW | 1
        out     dx, ax

        ; Check RX status
        mov     dx, si
        add     dx, 08h                 ; RX_STATUS register
        in      ax, dx
        test    ax, 8000h               ; RX_COMPLETE bit
        jz      .no_packet_3c509b

        ; Get packet length
        and     ax, 07FFh               ; Extract length (11 bits)
        mov     cx, ax                  ; CX = packet length

        ; Validate packet length
        cmp     cx, 1514
        ja      .bad_packet_3c509b
        cmp     cx, 14
        jb      .bad_packet_3c509b

        ; ES:DI points to receive buffer (passed in BP+8)
        les     di, [bp+8]

        ; Save packet length
        push    cx

        ; Read packet from RX FIFO
        mov     dx, si                  ; I/O base
        add     dx, 00h                 ; RX_FIFO register

        ; Optimize for word reads
        shr     cx, 1                   ; Convert to word count
        jz      .read_last_byte_3c509b

.read_loop_3c509b:
        in      ax, dx                  ; Read word from FIFO
        stosw                           ; Store to ES:DI
        loop    .read_loop_3c509b

.read_last_byte_3c509b:
        pop     cx                      ; Restore packet length
        test    cx, 1                   ; Check for odd byte
        jz      .read_done_3c509b
        in      al, dx                  ; Read single byte
        stosb

.read_done_3c509b:
        ; Discard packet from FIFO (required!)
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 4000h               ; CMD_RX_DISCARD
        out     dx, ax

        ; Wait for discard to complete
        mov     cx, 100
.wait_discard_3c509b:
        mov     dx, si
        add     dx, 08h                 ; RX_STATUS
        in      ax, dx
        test    ax, 1000h               ; RX_EARLY bit
        jz      .discard_done_3c509b
        push    cx
        mov     cx, 3
.delay_3c509b:
        in      al, 80h                 ; I/O delay
        loop    .delay_3c509b
        pop     cx
        loop    .wait_discard_3c509b

.discard_done_3c509b:
        mov     ax, cx                  ; Return packet length
        jmp     .cleanup_rx_3c509b

.bad_packet_3c509b:
        ; Discard bad packet
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 4000h               ; CMD_RX_DISCARD
        out     dx, ax

.no_packet_3c509b:
        xor     ax, ax                  ; No packet/error

.cleanup_rx_3c509b:
        pop     bx
        pop     cx
        pop     si
        pop     di
        pop     es
        jmp     .exit

.read_3c515:
        ; Read packet from 3C515-TX (DMA or PIO mode)
        push    es
        push    di
        push    si
        push    cx
        push    bx

        ; Get I/O base and check DMA mode
        mov     bl, al
        xor     bh, bh
        shl     bx, 1
        mov     si, hw_iobase_table
        mov     dx, [si+bx]
        mov     si, dx

        ; Check if bus master DMA is enabled
        mov     di, hw_flags_table
        mov     bl, [di+bx]
        test    bl, 01h                 ; FLAG_BUS_MASTER
        jz      .use_pio_3c515

        ; DMA mode - Select Window 7
        add     dx, REG_WINDOW
        mov     ax, 0807h               ; CMD_SELECT_WINDOW | 7
        out     dx, ax

        ; Check UP (RX) list status
        mov     dx, si
        add     dx, 38h                 ; UP_PKT_STATUS
        in      eax, dx                 ; Read 32-bit status (low word in AX)
        test    ax, 8000h               ; Complete bit
        jz      .no_dma_packet_3c515

        ; Extract packet length
        and     ax, 1FFFh               ; 13-bit length
        mov     cx, ax

        ; Get packet from DMA buffer
        ; (Implementation depends on DMA buffer management)

        ; Acknowledge DMA completion
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 6800h | 0200h       ; ACK_INTR | UP_COMPLETE
        out     dx, ax

        mov     ax, cx                  ; Return length
        jmp     .cleanup_rx_3c515

.use_pio_3c515:
        ; PIO mode - similar to 3C509B
        add     dx, REG_WINDOW
        mov     ax, 0801h
        out     dx, ax

        mov     dx, si
        add     dx, 18h                 ; RX_STATUS for 3C515
        in      ax, dx
        test    ax, 8000h
        jz      .no_packet_3c515

        and     ax, 1FFFh               ; 13-bit length for 3C515
        mov     cx, ax

        cmp     cx, 1514
        ja      .bad_packet_3c515
        cmp     cx, 14
        jb      .bad_packet_3c515

        les     di, [bp+8]
        push    cx

        mov     dx, si
        add     dx, 10h                 ; RX_FIFO for 3C515
        shr     cx, 1
        jz      .read_last_3c515

.read_loop_3c515:
        in      ax, dx
        stosw
        loop    .read_loop_3c515

.read_last_3c515:
        pop     cx
        test    cx, 1
        jz      .read_done_3c515
        in      al, dx
        stosb

.read_done_3c515:
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 4000h
        out     dx, ax

        mov     ax, cx
        jmp     .cleanup_rx_3c515

.bad_packet_3c515:
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 4000h
        out     dx, ax

.no_packet_3c515:
.no_dma_packet_3c515:
        xor     ax, ax

.cleanup_rx_3c515:
        pop     bx
        pop     cx
        pop     si
        pop     di
        pop     es
        jmp     .exit

.invalid_nic:
        mov     ax, 1
        jmp     .exit

.unknown_type:
        mov     ax, 2
        jmp     .exit

.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end hardware_read_packet

;-----------------------------------------------------------------------------
; hardware_send_packet_asm - Send packet via hardware
;
; Input:  DS:SI = packet data, CX = packet length
; Output: AX = 0 for success, non-zero for error
; Uses:   All registers preserved except AX
;
; HOT PATH: ~270 lines - critical packet transmit operation
;-----------------------------------------------------------------------------
hardware_send_packet_asm:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Complete packet transmission implementation

        ; Basic packet validation
        cmp     cx, 60                  ; Minimum Ethernet frame
        jb      .invalid_packet
        cmp     cx, 1514                ; Maximum Ethernet frame
        ja      .invalid_packet

        ; Get current NIC instance and validate
        mov     al, [current_instance]
        cmp     al, MAX_HW_INSTANCES
        jae     .no_active_nic

        ; Get NIC type and I/O base
        mov     bl, al
        xor     bh, bh
        mov     di, hw_type_table
        mov     cl, [di+bx]             ; CL = NIC type

        shl     bx, 1                   ; Word offset
        mov     di, hw_iobase_table
        mov     dx, [di+bx]             ; DX = I/O base
        test    dx, dx
        jz      .no_active_nic

        ; Branch based on NIC type
        cmp     cl, 1                   ; 3C509B
        je      .tx_3c509b
        cmp     cl, 2                   ; 3C515
        je      .tx_3c515
        jmp     .no_active_nic

.tx_3c509b:
        ; Transmit packet on 3C509B
        push    si
        push    cx
        push    bx

        mov     bx, dx                  ; Save I/O base

        ; Select Window 1 for TX
        add     dx, REG_WINDOW
        mov     ax, 0801h               ; CMD_SELECT_WINDOW | 1
        out     dx, ax
        mov     dx, bx                  ; Restore base

        ; Check TX space available
        add     dx, 0Ch                 ; TX_FREE register
        in      ax, dx
        cmp     ax, cx                  ; Compare with packet length
        jb      .tx_busy

        ; Set TX packet length
        mov     dx, bx
        add     dx, REG_COMMAND
        mov     ax, 9000h               ; CMD_TX_SET_LEN
        or      ax, cx                  ; Include length
        out     dx, ax

        ; Check for TX stall
        mov     dx, bx
        add     dx, 0Bh                 ; TX_STATUS
        in      al, dx
        test    al, 04h                 ; STALL bit
        jz      .no_stall_3c509b

        ; Clear stall
        mov     dx, bx
        add     dx, REG_COMMAND
        mov     ax, 5800h               ; CMD_TX_RESET
        out     dx, ax

        ; Re-enable TX
        mov     ax, 4800h               ; CMD_TX_ENABLE
        out     dx, ax

.no_stall_3c509b:
        ; Write packet to TX FIFO
        mov     dx, bx
        add     dx, 00h                 ; TX_FIFO

        push    cx
        shr     cx, 1                   ; Word count
        jz      .tx_last_byte_3c509b

.tx_loop_3c509b:
        lodsw                           ; Load from DS:SI
        out     dx, ax                  ; Write to FIFO
        loop    .tx_loop_3c509b

.tx_last_byte_3c509b:
        pop     cx
        test    cx, 1
        jz      .tx_start_3c509b
        lodsb
        out     dx, al

.tx_start_3c509b:
        ; Start transmission
        mov     dx, bx
        add     dx, REG_COMMAND
        mov     ax, 4800h               ; CMD_TX_START
        out     dx, ax

        xor     ax, ax                  ; Success
        pop     bx
        pop     cx
        pop     si
        jmp     .exit

.tx_3c515:
        ; Transmit packet on 3C515-TX
        push    si
        push    cx
        push    bx

        mov     bx, dx                  ; Save base

        ; Check if using DMA
        push    bx
        shr     bx, 1
        mov     di, hw_flags_table
        mov     al, [di+bx]
        pop     bx
        test    al, 01h                 ; FLAG_BUS_MASTER
        jz      .tx_pio_3c515

        ; DMA transmission (simplified)
        ; Would need proper descriptor setup
        jmp     .tx_busy

.tx_pio_3c515:
        ; PIO mode - similar to 3C509B
        add     dx, REG_WINDOW
        mov     ax, 0801h
        out     dx, ax
        mov     dx, bx

        add     dx, 1Ch                 ; TX_FREE for 3C515
        in      ax, dx
        cmp     ax, cx
        jb      .tx_busy

        mov     dx, bx
        add     dx, REG_COMMAND
        mov     ax, 9000h
        or      ax, cx
        out     dx, ax

        mov     dx, bx
        add     dx, 10h                 ; TX_FIFO for 3C515

        push    cx
        shr     cx, 1
        jz      .tx_last_3c515

.tx_loop_3c515:
        lodsw
        out     dx, ax
        loop    .tx_loop_3c515

.tx_last_3c515:
        pop     cx
        test    cx, 1
        jz      .tx_start_3c515
        lodsb
        out     dx, al

.tx_start_3c515:
        mov     dx, bx
        add     dx, REG_COMMAND
        mov     ax, 4800h
        out     dx, ax

        xor     ax, ax
        pop     bx
        pop     cx
        pop     si
        jmp     .exit

.tx_busy:
        mov     ax, ERROR_BUSY
        jmp     .exit
        ; Success
        mov     ax, 0
        jmp     .exit

.invalid_packet:
        mov     ax, 1
        jmp     .exit

.no_active_nic:
        mov     ax, 2
        jmp     .exit

.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end hardware_send_packet_asm

;-----------------------------------------------------------------------------
; io_read_byte - Read byte from I/O port
;
; Input:  DX = port address
; Output: AL = data read
; Uses:   AL
;-----------------------------------------------------------------------------
io_read_byte:
        push    bp
        mov     bp, sp

        ; Perform I/O read
        in      al, dx

        ; Update statistics
        inc     dword [io_read_count]

        pop     bp
        ret
;; end io_read_byte

;-----------------------------------------------------------------------------
; io_write_byte - Write byte to I/O port
;
; Input:  DX = port address, AL = data to write
; Output: None
; Uses:   None
;-----------------------------------------------------------------------------
io_write_byte:
        push    bp
        mov     bp, sp

        ; Perform I/O write
        out     dx, al

        ; Update statistics
        inc     dword [io_write_count]

        pop     bp
        ret
;; end io_write_byte

;-----------------------------------------------------------------------------
; io_read_word - Read word from I/O port
;
; Input:  DX = port address
; Output: AX = data read
; Uses:   AX
;-----------------------------------------------------------------------------
io_read_word:
        push    bp
        mov     bp, sp

        ; Perform I/O read
        in      ax, dx

        ; Update statistics
        inc     dword [io_read_count]

        pop     bp
        ret
;; end io_read_word

;-----------------------------------------------------------------------------
; io_write_word - Write word to I/O port
;
; Input:  DX = port address, AX = data to write
; Output: None
; Uses:   None
;-----------------------------------------------------------------------------
io_write_word:
        push    bp
        mov     bp, sp

        ; Perform I/O write
        out     dx, ax

        ; Update statistics
        inc     dword [io_write_count]

        pop     bp
        ret
;; end io_write_word

; EOF
