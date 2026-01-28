;=============================================================================
; hwinit.asm - Hardware Initialization Functions
;
; 3Com Packet Driver - Hardware initialization module
;
; This module contains hardware initialization functions for 3C509B and
; 3C515-TX network interface cards. This is a COLD section module that is
; discarded after the driver loads.
;
; Created: 2026-01-25 from hardware.asm modularization
; Last Modified: 2026-01-25 11:32:45
;
; Functions:
;   - hardware_init_asm          - Main hardware initialization entry point
;   - hardware_init_system       - System-level initialization
;   - hardware_detect_all_devices- Detect all supported hardware
;   - init_3c509b                - Initialize 3C509B card
;   - init_3c515                 - Initialize 3C515-TX with DMA
;   - read_3c515_mac_address     - Read MAC from 3C515 EEPROM
;
; COLD SECTION: This module is discarded after driver initialization
;=============================================================================

        bits    16
        cpu     386

;-----------------------------------------------------------------------------
; Include files
;-----------------------------------------------------------------------------
%define HARDWARE_MODULE_DEFINING
%include "asm_interfaces.inc"

;-----------------------------------------------------------------------------
; Global exports
;-----------------------------------------------------------------------------
        global  hardware_init_asm
        global  hardware_init_system
        global  hardware_detect_all_devices
        global  init_3c509b
        global  init_3c515
        global  read_3c515_mac_address

;-----------------------------------------------------------------------------
; External references - Functions
;-----------------------------------------------------------------------------
        extern  hardware_detect_all         ; from hwdet.asm
        extern  log_hardware_error          ; from hweep.asm
        extern  detect_3c509b_device        ; detection function
        extern  detect_3c515_device         ; detection function
        extern  delay_1ms                   ; timing utility
        extern  delay_1us                   ; timing utility

;-----------------------------------------------------------------------------
; External references - Data
;-----------------------------------------------------------------------------
        extern  hw_instances
        extern  hw_types
        extern  hw_io_bases
        extern  hw_mac_addresses
        extern  hw_instance_table
        extern  hw_instance_count
        extern  hardware_initialized
        extern  io_read_count
        extern  io_write_count
        extern  io_error_count
        extern  io_timeout_count
        extern  io_retry_count
        extern  last_error_code

;=============================================================================
; CODE SECTION - COLD (discarded after load)
;=============================================================================
        section .text.cold

;-----------------------------------------------------------------------------
; hardware_init_asm - Main hardware initialization entry point
;
; Initialize hardware detection structures, clear statistics, and detect
; all available hardware.
;
; Input:  None
; Output: AX = 0 on success, 1 on failure
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
hardware_init_asm:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Initialize hardware detection structures, clear statistics, and detect all available hardware

        ; Clear hardware state
        mov     cx, MAX_HW_INSTANCES
        mov     si, hw_instances
        mov     al, HW_STATE_UNDETECTED
.clear_state_loop:
        mov     [si], al
        inc     si
        loop    .clear_state_loop

        ; Clear I/O statistics
        mov     dword [io_read_count], 0
        mov     dword [io_write_count], 0
        mov     word [io_error_count], 0

        ; Detect all hardware
        call    hardware_detect_all
        cmp     ax, 0
        jne     .detection_failed

        ; Success
        mov     ax, 0
        jmp     .exit

.detection_failed:
        ; Log detection failure
        push    ax
        push    dx
        mov     ax, HW_ERROR_NO_DEVICE
        call    log_hardware_error
        pop     dx
        pop     ax
        mov     ax, 1
        jmp     .exit

.exit:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end hardware_init_asm

;-----------------------------------------------------------------------------
; hardware_init_system - System-level hardware initialization
;
; Initialize hardware instance table, counters, and I/O statistics.
; Safe to call multiple times - checks for prior initialization.
;
; Input:  None
; Output: AX = HW_SUCCESS
; Uses:   AX, BX, CX, SI
;-----------------------------------------------------------------------------
hardware_init_system:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si

        ; Check if already initialized
        cmp     byte [hardware_initialized], 1
        je      .already_initialized

        ; Clear hardware instance table
        mov     cx, HW_INSTANCE_size
        mov     ax, MAX_HW_INSTANCES
        mul     cx
        mov     cx, ax                      ; Total bytes to clear
        mov     si, hw_instance_table

.clear_loop:
        mov     byte [si], 0
        inc     si
        dec     cx
        jnz     .clear_loop

        ; Initialize counters
        mov     byte [hw_instance_count], 0
        mov     byte [last_error_code], 0

        ; Clear I/O statistics
        mov     dword [io_read_count], 0
        mov     dword [io_write_count], 0
        mov     word [io_error_count], 0
        mov     word [io_timeout_count], 0
        mov     word [io_retry_count], 0

        ; Mark as initialized
        mov     byte [hardware_initialized], 1

.already_initialized:
        mov     ax, HW_SUCCESS

        pop     si
        pop     cx
        pop     bx
        pop     bp
        ret
;; end hardware_init_system

;-----------------------------------------------------------------------------
; hardware_detect_all_devices - Detect all supported hardware devices
;
; Scans for 3C509B and 3C515-TX network interface cards and populates
; the hardware instance table with detected devices.
;
; Input:  None
; Output: AX = number of devices detected
; Uses:   All registers
;-----------------------------------------------------------------------------
hardware_detect_all_devices:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        mov     bx, 0                       ; Device counter

        ; Try to detect 3C509B devices
        call    detect_3c509b_device
        jc      .no_3c509b

        ; Store detected 3C509B
        mov     si, hw_instance_table
        mov     cx, HW_INSTANCE_size
        mov     dx, bx
        mul     cx
        add     si, ax

        mov     byte [si + HW_INSTANCE.type], HW_TYPE_3C509B
        mov     byte [si + HW_INSTANCE.state], HW_STATE_DETECTED
        mov     [si + HW_INSTANCE.io_base], ax  ; From detect function

        ; Update legacy arrays for compatibility
        mov     [hw_types + bx], byte HW_TYPE_3C509B
        mov     [hw_instances + bx], byte HW_STATE_DETECTED
        push    bx
        shl     bx, 1
        mov     [hw_io_bases + bx], ax
        pop     bx

        inc     bx                          ; Increment device count

.no_3c509b:
        ; Try to detect 3C515-TX devices
        call    detect_3c515_device
        jc      .no_3c515

        ; Check if we have room for another device
        cmp     bx, MAX_HW_INSTANCES
        jae     .no_3c515

        ; Store detected 3C515-TX
        push    ax                          ; Save I/O base
        mov     si, hw_instance_table
        mov     cx, HW_INSTANCE_size
        mov     ax, bx
        mul     cx
        add     si, ax
        pop     ax                          ; Restore I/O base

        mov     byte [si + HW_INSTANCE.type], HW_TYPE_3C515TX
        mov     byte [si + HW_INSTANCE.state], HW_STATE_DETECTED
        mov     [si + HW_INSTANCE.io_base], ax

        ; Update legacy arrays
        mov     [hw_types + bx], byte HW_TYPE_3C515TX
        mov     [hw_instances + bx], byte HW_STATE_DETECTED
        push    bx
        shl     bx, 1
        mov     [hw_io_bases + bx], ax
        pop     bx

        inc     bx                          ; Increment device count

.no_3c515:
        ; Store final device count
        mov     [hw_instance_count], bl
        mov     ax, bx                      ; Return device count

        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end hardware_detect_all_devices

;-----------------------------------------------------------------------------
; init_3c509b - Initialize 3C509B network interface card
;
; Performs hardware reset, reads MAC address from EEPROM, programs station
; address registers, and enables receiver/transmitter.
;
; Input:  AL = NIC index (0-based)
;         DX = I/O base address
; Output: AX = 0 on success
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
init_3c509b:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Store NIC index for later use
        mov     bl, al

        ; Perform total reset first
        mov     ax, (0 << 11)          ; Total Reset command
        add     dx, 0Eh                 ; Command register offset
        out     dx, ax
        sub     dx, 0Eh                 ; Restore base address

        ; Wait for reset to complete (minimum 1ms)
        mov     cx, 10000
.reset_wait:
        nop
        loop    .reset_wait

        ; Select window 0 for configuration
        add     dx, 0Eh                 ; Command register
        mov     ax, (1 << 11) | 0       ; Select Window 0
        out     dx, ax
        sub     dx, 0Eh

        ; Read and store MAC address from EEPROM
        mov     si, 0                   ; EEPROM address counter
        mov     di, hw_mac_addresses
        mov     ah, 0
        mov     al, bl                  ; NIC index
        mov     cl, 6                   ; 6 bytes per MAC address
        mul     cl
        add     di, ax                  ; Point to this NIC's MAC storage

        ; Read MAC address (3 words = 6 bytes)
        mov     cx, 3                   ; Read 3 EEPROM words

.read_mac_loop:
        ; Set EEPROM address for read
        push    dx
        add     dx, 0Ah                 ; EEPROM command register
        mov     ax, 0080h               ; Read command
        or      ax, si                  ; Add EEPROM address
        out     dx, ax

        ; Wait for EEPROM ready (15us minimum)
        push    cx
        mov     cx, 50
.mac_eeprom_wait:
        nop
        loop    .mac_eeprom_wait
        pop     cx

        ; Read EEPROM data
        add     dx, 2                   ; EEPROM data register (0Ch)
        in      ax, dx
        pop     dx

        ; Store MAC bytes (3Com stores bytes swapped)
        mov     [di], ah                ; Store high byte first
        inc     di
        mov     [di], al                ; Store low byte
        inc     di

        inc     si                      ; Next EEPROM address
        loop    .read_mac_loop

        ; Select window 2 to set station address
        add     dx, 0Eh                 ; Command register
        mov     ax, (1 << 11) | 2       ; Select Window 2
        out     dx, ax

        ; Write MAC address to station address registers
        mov     si, hw_mac_addresses
        mov     ah, 0
        mov     al, bl                  ; NIC index
        mov     cl, 6
        mul     cl
        add     si, ax                  ; Point to this NIC's MAC

        mov     cx, 6                   ; 6 bytes to write
        mov     di, 0                   ; Register offset

.write_mac_loop:
        mov     al, [si]                ; Load MAC byte
        push    dx
        add     dx, di                  ; Add register offset
        out     dx, al                  ; Write to station address register
        pop     dx
        inc     si                      ; Next MAC byte
        inc     di                      ; Next register
        loop    .write_mac_loop

        ; Select window 1 for normal operation
        add     dx, 0Eh                 ; Command register
        mov     ax, (1 << 11) | 1       ; Select Window 1
        out     dx, ax

        ; Set receive filter (station + broadcast)
        mov     ax, (16 << 11) | 0005h  ; SetRxFilter: Station + Broadcast
        out     dx, ax

        ; Enable receiver
        mov     ax, (4 << 11)          ; RxEnable command
        out     dx, ax

        ; Enable transmitter
        mov     ax, (9 << 11)          ; TxEnable command
        out     dx, ax

        ; Success
        mov     ax, 0

        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end init_3c509b

;-----------------------------------------------------------------------------
; init_3c515 - Initialize 3C515-TX network interface card with DMA
;
; Performs hardware reset, reads MAC address, configures media type,
; sets up bus master DMA, and enables receiver/transmitter.
;
; Input:  AL = instance index (0-based)
;         DX = I/O base address
; Output: AX = 0 on success, 1 on invalid instance
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
init_3c515:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Validate instance index
        cmp     al, MAX_HW_INSTANCES
        jae     .invalid_instance

        ; Save parameters
        mov     bl, al                  ; Save instance index
        mov     si, dx                  ; Save I/O base

        ; Total reset first
        add     dx, 0Eh                 ; Command register
        mov     ax, 0                   ; Total reset command
        out     dx, ax

        ; Wait for reset to complete
        mov     cx, 1000
.reset_wait:
        call    delay_1ms
        loop    .reset_wait

        mov     dx, si                  ; Restore I/O base

        ; Select window 0 to read EEPROM and get MAC address
        add     dx, 0Eh
        mov     ax, (1 << 11) | 0       ; Select window 0
        out     dx, ax
        mov     dx, si                  ; Restore base

        ; Read MAC address from EEPROM
        call    read_3c515_mac_address

        ; Select window 2 to set MAC address
        add     dx, 0Eh
        mov     ax, (1 << 11) | 2       ; Select window 2
        out     dx, ax
        mov     dx, si                  ; Restore base

        ; Write MAC address to station address registers (0-5)
        ; Copy MAC from EEPROM to station address registers
        push    si
        push    di
        push    cx

        ; Get instance MAC address pointer
        mov     bl, al                  ; Instance index
        xor     bh, bh
        shl     bx, 1
        shl     bx, 1
        add     bx, bx                  ; x6 for MAC size
        mov     si, hw_mac_addresses
        add     si, bx

        ; Select Window 2 for station address
        add     dx, 0Eh
        mov     ax, (1 << 11) | 2       ; Select Window 2
        out     dx, ax
        mov     dx, si                  ; Restore base

        ; Write MAC to registers 0-5
        mov     cx, 3                   ; 3 words
        xor     di, di                  ; Start at register 0
.copy_mac:
        mov     ax, [si]
        out     dx, ax
        add     dx, 2
        add     si, 2
        loop    .copy_mac

        pop     cx
        pop     di
        pop     si

        ; Select window 3 for configuration
        add     dx, 0Eh
        mov     ax, (1 << 11) | 3       ; Select window 3
        out     dx, ax
        mov     dx, si                  ; Restore base

        ; Configure media type (auto-select 10/100)
        add     dx, 8                   ; Options register
        in      ax, dx
        or      ax, 8000h               ; Enable auto-select (bit 15)
        out     dx, ax
        mov     dx, si                  ; Restore base

        ; Select window 7 for bus master DMA setup
        add     dx, 0Eh
        mov     ax, (1 << 11) | 7       ; Select window 7
        out     dx, ax
        mov     dx, si                  ; Restore base

        ; Initialize DMA descriptor pointers
        add     dx, 404h                ; Down list pointer (TX)
        xor     ax, ax                  ; NULL initially
        out     dx, ax                  ; Low word
        add     dx, 2
        out     dx, ax                  ; High word

        mov     dx, si                  ; Restore base
        add     dx, 418h                ; Up list pointer (RX)
        xor     ax, ax                  ; NULL initially
        out     dx, ax                  ; Low word
        add     dx, 2
        out     dx, ax                  ; High word

        mov     dx, si                  ; Restore base

        ; Select window 1 for normal operation
        add     dx, 0Eh
        mov     ax, (1 << 11) | 1       ; Select window 1
        out     dx, ax
        mov     dx, si                  ; Restore base

        ; Set up interrupt mask (enable DMA interrupts)
        add     dx, 0Eh                 ; Command register
        mov     ax, (14 << 11) | 07FFh  ; Set interrupt enable with DMA bits
        out     dx, ax
        mov     dx, si                  ; Restore base

        ; Enable receiver
        add     dx, 0Eh
        mov     ax, (4 << 11)           ; RX Enable command
        out     dx, ax
        mov     dx, si                  ; Restore base

        ; Enable transmitter
        add     dx, 0Eh
        mov     ax, (9 << 11)           ; TX Enable command
        out     dx, ax
        mov     dx, si                  ; Restore base

        ; Set RX filter to receive unicast + broadcast
        add     dx, 0Eh
        mov     ax, (16 << 11) | 5      ; Set RX filter: station + broadcast
        out     dx, ax

        ; Mark as configured
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
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end init_3c515

;-----------------------------------------------------------------------------
; read_3c515_mac_address - Read MAC address from 3C515 EEPROM
;
; Reads the 6-byte MAC address from EEPROM locations 0, 1, 2 and stores
; it in the hw_mac_addresses buffer.
;
; Input:  DX = I/O base address
; Output: None (MAC stored in hw_mac_addresses)
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
read_3c515_mac_address:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si
        push    di

        ; Read MAC address from EEPROM locations 0, 1, 2
        mov     bx, 0                   ; EEPROM address counter
        mov     di, hw_mac_addresses    ; Destination buffer

.read_mac_loop:
        ; Set EEPROM address
        push    dx
        add     dx, 200Ah               ; EEPROM command register
        mov     ax, 80h                 ; Read command
        or      ax, bx                  ; OR with address
        out     dx, ax
        pop     dx

        ; Wait 162us for EEPROM
        mov     cx, 162
.mac_eeprom_delay:
        call    delay_1us
        loop    .mac_eeprom_delay

        ; Read EEPROM data (16-bit word)
        push    dx
        add     dx, 0Ch                 ; EEPROM data register (ISA)
        in      ax, dx
        pop     dx

        ; Store MAC bytes (little endian)
        mov     [di], al                ; Low byte
        inc     di
        mov     [di], ah                ; High byte
        inc     di

        inc     bx                      ; Next EEPROM address
        cmp     bx, 3                   ; Read 3 words (6 bytes)
        jl      .read_mac_loop

        pop     di
        pop     si
        pop     cx
        pop     bx
        pop     bp
        ret
;; end read_3c515_mac_address

;=============================================================================
; End of hwinit.asm
;=============================================================================
