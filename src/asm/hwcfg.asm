; @file hwcfg.asm
; @brief Hardware configuration functions for 3C509B and 3C515-TX NICs
;
; 3Com Packet Driver - Configuration module extracted from hardware.asm
; Contains device configuration, reset, IRQ setup, and DMA configuration
;
; This is a COLD section module - discarded after driver load
;
; This file is part of the 3Com Packet Driver project.
;
; Created: 2026-01-25 from hardware.asm modularization

bits 16
cpu 386

; C symbol naming bridge (maps C symbols to _symbol)
%include "csym.inc"

; Include assembly interface definitions
%define HARDWARE_MODULE_DEFINING
%include "asm_interfaces.inc"

;=============================================================================
; REGISTER CONSTANTS
;=============================================================================

; Common 3Com registers (relative to base)
REG_COMMAND         EQU 0Eh     ; Command register
REG_WINDOW          EQU 0Eh     ; Window select register

;=============================================================================
; GLOBAL EXPORTS
;=============================================================================

global hardware_configure_3c509b
global configure_3c509b_device
global configure_3c515_device
global reset_3c509b_device
global reset_3c515_device
global setup_3c509b_irq
global setup_3c515_irq
global hardware_set_device_state
global configure_3c515_dma

;=============================================================================
; EXTERNAL REFERENCES - Functions
;=============================================================================

extern read_3c509b_eeprom
extern read_3c515_eeprom
extern read_mac_from_eeprom_3c509b
extern delay_1ms

;=============================================================================
; EXTERNAL REFERENCES - Data
;=============================================================================

extern hw_instances
extern hw_io_bases
extern hw_irq_lines
extern hw_mac_addresses
extern hw_instance_table

;=============================================================================
; JIT MODULE HEADER
;=============================================================================
segment MODULE class=MODULE align=16

global _mod_hwcfg_header
_mod_hwcfg_header:
    db  'PKTDRV', 0             ; +00  7 bytes: module signature
    db  1                       ; +07  1 byte:  major version
    db  0                       ; +08  1 byte:  minor version
    db  0                       ; +09  1 byte:  cpu_req
    db  0                       ; +0A  1 byte:  nic_type (0 = generic)
    db  1                       ; +0B  1 byte:  cap_flags (1 = MOD_CAP_CORE)
    dw  hot_end - hot_start     ; +0C  2 bytes: hot code size
    dw  patch_table             ; +0E  2 bytes: offset to patch table
    dw  patch_table_end - patch_table  ; +10  2 bytes: patch table size
    dw  hot_start               ; +12  2 bytes: offset to hot_start
    dw  hot_end                 ; +14  2 bytes: offset to hot_end
    times 64 - ($ - _mod_hwcfg_header) db 0  ; Pad to 64 bytes

;=============================================================================
; CODE SECTION - COLD (discarded after load)
;=============================================================================

section .text
hot_start:

;-----------------------------------------------------------------------------
; hardware_configure_3c509b - Full 3C509B configuration sequence
;
; Input:  AL = instance index
;         DX = I/O base address
; Output: AX = 0 on success, 1 on failure
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
hardware_configure_3c509b:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Complete 3C509B configuration sequence
        ; Save instance and I/O base
        push    ax                      ; Save instance index
        mov     si, dx                  ; SI = I/O base

        ; Step 1: Reset the adapter
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 0000h               ; Global reset command
        out     dx, ax

        ; Wait for reset to complete (~1ms)
        mov     cx, 300
.reset_wait:
        in      al, 80h                 ; I/O delay (~3.3us)
        loop    .reset_wait

        ; Step 2: Select Window 0 for EEPROM access
        mov     dx, si
        add     dx, REG_WINDOW
        mov     ax, 0800h               ; CMD_SELECT_WINDOW | 0
        out     dx, ax

        ; Step 3: Read MAC address from EEPROM
        pop     ax                      ; Restore instance index
        push    ax
        call    read_mac_from_eeprom_3c509b

        ; Step 4: Select Window 2 to set station address
        mov     dx, si
        add     dx, REG_WINDOW
        mov     ax, 0802h               ; CMD_SELECT_WINDOW | 2
        out     dx, ax

        ; Write MAC address to station address registers
        pop     ax                      ; Get instance index
        push    ax
        mov     bl, al
        xor     bh, bh
        shl     bx, 1                   ; Word offset
        shl     bx, 1                   ; x4
        shl     bx, 1                   ; x8 for 6-byte MAC
        mov     di, hw_mac_addresses
        add     di, bx

        ; Write 6 bytes of MAC to Window 2, registers 0-5
        mov     cx, 3                   ; 3 words
        mov     dx, si                  ; Base address
.write_mac:
        mov     ax, [di]
        out     dx, ax
        add     dx, 2
        add     di, 2
        loop    .write_mac

        ; Step 5: Configure media type in Window 3
        mov     dx, si
        add     dx, REG_WINDOW
        mov     ax, 0803h               ; CMD_SELECT_WINDOW | 3
        out     dx, ax

        ; Configure internal config register (offset 0)
        mov     dx, si
        in      ax, dx
        or      ax, 0030h               ; Enable 10BaseT
        out     dx, ax

        ; Configure MAC control (offset 6)
        mov     dx, si
        add     dx, 6
        in      ax, dx
        and     ax, 0FF8Fh              ; Clear media bits
        or      ax, 0000h               ; 10BaseT (bits 6:4 = 000)
        out     dx, ax

        ; Step 6: Select transceiver
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 3000h               ; CMD_SELECT_XCVR | 10BaseT
        out     dx, ax

        ; Step 7: Select Window 1 for operation
        mov     ax, 0801h               ; CMD_SELECT_WINDOW | 1
        out     dx, ax

        ; Step 8: Set RX filter (station + broadcast)
        mov     ax, 0800h | 05h         ; SET_RX_FILTER | Station+Broadcast
        out     dx, ax

        ; Step 9: Set RX early threshold
        mov     ax, 1000h | 40h         ; SET_RX_EARLY | 64 bytes
        out     dx, ax

        ; Step 10: Set TX available threshold
        mov     ax, 9800h | 40h         ; SET_TX_AVAIL | 64 bytes
        out     dx, ax

        ; Step 11: Enable receiver
        mov     ax, 2000h               ; RX_ENABLE
        out     dx, ax

        ; Step 12: Enable transmitter
        mov     ax, 4800h               ; TX_ENABLE
        out     dx, ax

        ; Step 13: Set interrupt mask
        mov     ax, 7000h | 00FFh       ; SET_INTR_MASK | All interrupts
        out     dx, ax

        ; Step 14: Clear any pending interrupts
        mov     ax, 6800h | 00FFh       ; ACK_INTR | All
        out     dx, ax

        ; Validate instance index
        pop     ax                      ; Restore instance
        cmp     al, MAX_HW_INSTANCES
        jae     .invalid_instance

        ; Mark as configured
        mov     bl, al
        mov     bh, 0
        mov     si, hw_instances
        add     si, bx
        mov     byte [si], HW_STATE_CONFIGURED

        ; Success
        mov     ax, 0
        jmp     .exit

.invalid_instance:
        mov     ax, 1
        jmp     .exit

.exit:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end hardware_configure_3c509b

;-----------------------------------------------------------------------------
; configure_3c509b_device - Enhanced 3C509B device configuration
;
; Input:  AL = device index
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
configure_3c509b_device:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Validate device index
        VALIDATE_HW_INSTANCE al, MAX_HW_INSTANCES
        jc      .invalid_index

        ; Get device I/O base from instance table
        mov     bl, al
        mov     bh, 0
        mov     si, bx
        shl     si, 1                       ; Word offset for I/O bases
        mov     dx, [hw_io_bases + si]
        test    dx, dx
        jz      .no_io_base

        ; Reset the device first
        HARDWARE_RESET dx, HW_TYPE_3C509B, 1000
        jc      .reset_failed

        ; Configure device registers using window selection
        SELECT_3C509B_WINDOW dx, 0

        ; Read and validate EEPROM
        call    read_3c509b_eeprom
        cmp     ax, HW_SUCCESS
        jne     .eeprom_failed

        ; Setup IRQ if specified
        mov     al, [hw_irq_lines + bx]
        cmp     al, 0
        je      .skip_irq_setup
        call    setup_3c509b_irq

.skip_irq_setup:
        ; Mark device as configured
        mov     al, bl                      ; Restore device index
        mov     ah, HW_STATE_CONFIGURED
        call    hardware_set_device_state

        mov     ax, HW_SUCCESS
        jmp     .exit

.invalid_index:
        mov     ax, HW_ERROR_INVALID_PARAM
        jmp     .exit

.no_io_base:
        mov     ax, HW_ERROR_IO_ERROR
        jmp     .exit

.reset_failed:
        mov     ax, HW_ERROR_RESET_FAILED
        jmp     .exit

.eeprom_failed:
        mov     ax, HW_ERROR_EEPROM_READ

.exit:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end configure_3c509b_device

;-----------------------------------------------------------------------------
; configure_3c515_device - Enhanced 3C515-TX configuration
;
; Input:  AL = device index
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
configure_3c515_device:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Validate device index
        VALIDATE_HW_INSTANCE al, MAX_HW_INSTANCES
        jc      .invalid_index

        ; Get device I/O base from instance table
        mov     bl, al
        mov     bh, 0
        mov     si, bx
        shl     si, 1                       ; Word offset for I/O bases
        mov     dx, [hw_io_bases + si]
        test    dx, dx
        jz      .no_io_base

        ; Reset the device first
        HARDWARE_RESET dx, HW_TYPE_3C515TX, 1000
        jc      .reset_failed

        ; Configure device registers
        SELECT_3C515_WINDOW dx, 0

        ; Read and validate EEPROM
        call    read_3c515_eeprom
        cmp     ax, HW_SUCCESS
        jne     .eeprom_failed

        ; Setup DMA if supported
        call    configure_3c515_dma

        ; Setup IRQ if specified
        mov     al, [hw_irq_lines + bx]
        cmp     al, 0
        je      .skip_irq_setup
        call    setup_3c515_irq

.skip_irq_setup:
        ; Mark device as configured
        mov     al, bl                      ; Restore device index
        mov     ah, HW_STATE_CONFIGURED
        call    hardware_set_device_state

        mov     ax, HW_SUCCESS
        jmp     .exit

.invalid_index:
        mov     ax, HW_ERROR_INVALID_PARAM
        jmp     .exit

.no_io_base:
        mov     ax, HW_ERROR_IO_ERROR
        jmp     .exit

.reset_failed:
        mov     ax, HW_ERROR_RESET_FAILED
        jmp     .exit

.eeprom_failed:
        mov     ax, HW_ERROR_EEPROM_READ

.exit:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end configure_3c515_device

;-----------------------------------------------------------------------------
; hardware_set_device_state - Set device state
;
; Input:  AL = device index
;         AH = new state (HW_STATE_*)
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, CX, SI
;-----------------------------------------------------------------------------
hardware_set_device_state:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si

        ; Validate device index
        VALIDATE_HW_INSTANCE al, MAX_HW_INSTANCES
        jc      .invalid_index

        ; Calculate offset in hardware instance table
        mov     bl, al
        mov     bh, 0
        push    ax                          ; Save state value
        mov     ax, bx
        mov     cx, HW_INSTANCE_size
        mul     cx
        mov     si, ax
        add     si, hw_instance_table
        pop     ax                          ; Restore state value

        ; Set new state
        mov     [si + HW_INSTANCE.state], ah

        ; Update legacy state array for compatibility
        mov     [hw_instances + bx], ah

        mov     ax, HW_SUCCESS
        jmp     .exit

.invalid_index:
        mov     ax, HW_ERROR_INVALID_PARAM

.exit:
        pop     si
        pop     cx
        pop     bx
        pop     bp
        ret
;; end hardware_set_device_state

;-----------------------------------------------------------------------------
; setup_3c509b_irq - Setup 3C509B IRQ handler
;
; Input:  AL = IRQ number, DX = I/O base
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
setup_3c509b_irq:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Validate IRQ
        VALIDATE_IRQ al
        jc      .invalid_irq

        ; Configure interrupt enable mask
        SELECT_3C509B_WINDOW dx, 1

        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (14 << 11) | 00BFh      ; Set interrupt enable mask
        out     dx, ax
        pop     dx

        mov     ax, HW_SUCCESS
        jmp     .exit

.invalid_irq:
        mov     ax, HW_ERROR_IRQ_CONFLICT

.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end setup_3c509b_irq

;-----------------------------------------------------------------------------
; setup_3c515_irq - Setup 3C515-TX IRQ handler
;
; Input:  AL = IRQ number, DX = I/O base
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
setup_3c515_irq:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Validate IRQ
        VALIDATE_IRQ al
        jc      .invalid_irq

        ; Configure interrupt enable mask for 3C515-TX
        SELECT_3C515_WINDOW dx, 1

        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (14 << 11) | 07FFh      ; Enhanced interrupt mask for DMA
        out     dx, ax
        pop     dx

        mov     ax, HW_SUCCESS
        jmp     .exit

.invalid_irq:
        mov     ax, HW_ERROR_IRQ_CONFLICT

.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end setup_3c515_irq

;-----------------------------------------------------------------------------
; configure_3c515_dma - Configure 3C515-TX DMA settings
;
; Input:  DX = I/O base address
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
configure_3c515_dma:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Select window 7 for DMA control
        SELECT_3C515_WINDOW dx, 7

        ; Initialize DMA descriptor pointers
        push    dx
        add     dx, 404h                    ; Down list pointer
        xor     ax, ax                      ; NULL pointer initially
        out     dx, ax
        add     dx, 2
        out     dx, ax                      ; High word

        add     dx, 14h - 6                 ; Up list pointer (418h - 404h = 14h)
        out     dx, ax
        add     dx, 2
        out     dx, ax                      ; High word
        pop     dx

        mov     ax, HW_SUCCESS

        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end configure_3c515_dma

;-----------------------------------------------------------------------------
; reset_3c509b_device - Reset 3C509B device
;
; Input:  AL = device index
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, DX
;-----------------------------------------------------------------------------
reset_3c509b_device:
        push    bp
        mov     bp, sp
        push    bx
        push    dx

        ; Get I/O base for device
        mov     bl, al
        mov     bh, 0
        shl     bx, 1
        mov     dx, [hw_io_bases + bx]

        ; Perform hardware reset
        HARDWARE_RESET dx, HW_TYPE_3C509B, 1000
        jc      .reset_failed

        mov     ax, HW_SUCCESS
        jmp     .exit

.reset_failed:
        mov     ax, HW_ERROR_RESET_FAILED

.exit:
        pop     dx
        pop     bx
        pop     bp
        ret
;; end reset_3c509b_device

;-----------------------------------------------------------------------------
; reset_3c515_device - Reset 3C515-TX device
;
; Input:  AL = device index
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, DX
;-----------------------------------------------------------------------------
reset_3c515_device:
        push    bp
        mov     bp, sp
        push    bx
        push    dx

        ; Get I/O base for device
        mov     bl, al
        mov     bh, 0
        shl     bx, 1
        mov     dx, [hw_io_bases + bx]

        ; Perform hardware reset
        HARDWARE_RESET dx, HW_TYPE_3C515TX, 1000
        jc      .reset_failed

        mov     ax, HW_SUCCESS
        jmp     .exit

.reset_failed:
        mov     ax, HW_ERROR_RESET_FAILED

.exit:
        pop     dx
        pop     bx
        pop     bp
        ret
;; end reset_3c515_device

hot_end:

patch_table:
patch_table_end:

;=============================================================================
; END OF MODULE
;=============================================================================
