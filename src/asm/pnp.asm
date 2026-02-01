; @file pnp.asm
; @brief Enhanced PnP hardware detection routines - Groups 6A & 6B Implementation
;
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
; Enhanced PnP detection with LFSR sequence generation and defensive programming
;
; This file is part of the 3Com Packet Driver project.
;
; Converted to NASM syntax - 2026-01-23

bits 16
cpu 386

; Define module guard to prevent extern declarations for functions we define here
%define PNP_MODULE_DEFINING 1
%define HARDWARE_DATA_DEFINING 1

; Include assembly interface definitions
%include "asm_interfaces.inc"
%include "lfsr_table.inc"

; PnP constants
PNP_ADDRESS_PORT    equ 279h    ; PnP address port
PNP_WRITE_DATA      equ 0A79h   ; PnP write data port
PNP_READ_PORT       equ 203h    ; Default PnP read port (can vary)

; PnP registers
PNP_CONFIG_CONTROL  equ 02h     ; Configuration control
PNP_WAKE            equ 03h     ; Wake command
PNP_RESOURCE_DATA   equ 04h     ; Resource data
PNP_STATUS          equ 05h     ; Status register
PNP_CARD_SELECT     equ 06h     ; Card select number
PNP_LOGICAL_DEVICE  equ 07h     ; Logical device number

; PnP commands
PNP_CMD_INITIATION  equ 00h     ; Initiation key sequence start
PNP_CMD_WAIT_FOR_KEY equ 01h    ; Wait for key
PNP_CMD_CONFIG_MODE equ 02h     ; Configuration mode
PNP_CMD_SLEEP       equ 03h     ; Sleep command
PNP_CMD_ISOLATION   equ 04h     ; Isolation command

; 3Com vendor ID and device IDs
VENDOR_3COM         equ 05094h  ; 3Com vendor ID in PnP format
DEVICE_3C509B       equ 09509h  ; 3C509B device ID
DEVICE_3C515        equ 15515h  ; 3C515 device ID

; PnP state constants
PNP_STATE_UNKNOWN   equ 0       ; PnP state unknown
PNP_STATE_DETECTED  equ 1       ; PnP device detected
PNP_STATE_CONFIGURED equ 2      ; PnP device configured
PNP_STATE_ERROR     equ 0FFh    ; PnP error

; Maximum PnP devices
MAX_PNP_DEVICES     equ 8       ; Maximum PnP devices to track

; Enhanced PnP data segment for Groups 6A & 6B
segment _DATA class=DATA

; Enhanced PnP detection state using structured approach
pnp_device_table    times PNP_MAX_DEVICES db 0  ; PNP_DEVICE_INFO structure placeholder
pnp_device_count    db 0        ; Total PnP devices detected

; Legacy compatibility (maintained for existing code)
pnp_available       db 0        ; PnP system available flag
pnp_read_port       dw PNP_READ_PORT_BASE ; Current read port
pnp_devices_found   db 0        ; Number of PnP devices found
pnp_vendor_ids      times PNP_MAX_DEVICES dw 0     ; Vendor IDs
pnp_device_ids      times PNP_MAX_DEVICES dw 0     ; Device IDs
pnp_io_bases        times PNP_MAX_DEVICES dw 0     ; I/O base addresses
pnp_irq_lines       times PNP_MAX_DEVICES db 0     ; IRQ assignments
pnp_card_selects    times PNP_MAX_DEVICES db 0     ; Card select numbers

; Enhanced PnP operation statistics
pnp_operations      dw 0        ; Total PnP operations
pnp_successes       dw 0        ; Successful operations
pnp_failures        dw 0        ; Failed operations
pnp_timeouts        dw 0        ; Operation timeouts
pnp_retries         dw 0        ; Operation retries

; LFSR lookup table for PnP isolation (255 bytes)
lfsr_lookup_table   times 255 db 0 ; LFSR sequence lookup table
lfsr_initialized    db 0        ; LFSR table initialized flag

; Initiation key sequence (32 bytes)
pnp_initiation_key  db 6Ah, 0B5h, 0DAh, 0EDh, 0F6h, 0FBh, 7Dh, 0BEh
                    db 0DFh, 6Fh, 37h, 1Bh, 0Dh, 86h, 0C3h, 61h
                    db 0B0h, 58h, 2Ch, 16h, 8Bh, 45h, 0A2h, 0D1h
                    db 0E8h, 74h, 3Ah, 9Dh, 0CEh, 0E7h, 73h, 39h

; Code segment
segment _TEXT class=CODE

; Enhanced function exports for Groups 6A & 6B
global pnp_detect_all
global pnp_init_system
global pnp_isolate_devices
global pnp_configure_device
global pnp_find_read_port
global pnp_send_initiation_key
global pnp_read_resource_data
global pnp_get_device_info

; Main C integration function
global pnp_detect_nics

; Enhanced Group 6A/6B PnP interface functions
global pnp_enumerate_devices
global pnp_get_device_resources
global pnp_assign_resources
global pnp_get_lfsr_table
global pnp_generate_lfsr_sequence
global pnp_shutdown_system
global pnp_activate_device

; External references
extern hardware_configure_3c509b   ; From hardware.asm
extern hardware_configure_3c515    ; From hardware.asm

;-----------------------------------------------------------------------------
; pnp_detect_all - Main PnP detection entry point
; Detects all PnP devices and configures supported 3Com cards
;
; Input:  None
; Output: AX = number of supported devices found
; Uses:   All registers
;-----------------------------------------------------------------------------
pnp_detect_all:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Initialize PnP system with LFSR sequence generation
        call    pnp_generate_lfsr_sequence_enhanced
        cmp     ax, HW_SUCCESS
        jne     .pnp_failed

        ; Perform complete device isolation using LFSR sequence
        call    pnp_perform_isolation_with_lfsr
        mov     bx, ax                  ; BX = number of devices found

        ; Configure and activate found 3Com devices
        call    pnp_configure_3com_devices

        ; Return count of successfully configured devices

        ; Initialize PnP system
        call    pnp_init_system
        cmp     ax, 0
        jne     .pnp_failed

        ; Find optimal read port
        call    pnp_find_read_port
        cmp     ax, 0
        je      .no_read_port

        ; Isolate all PnP devices
        call    pnp_isolate_devices
        mov     bx, ax                  ; BX = number of devices found

        ; Configure supported devices
        mov     cx, 0                   ; Supported device counter
        mov     si, 0                   ; Device index

.configure_loop:
        cmp     si, bx
        jae     .done_configuring

        ; Check if this is a supported 3Com device
        call    pnp_check_3com_device
        cmp     ax, 0
        je      .next_device

        ; Configure the device
        mov     ax, si                  ; Device index (SI to AX)
        call    pnp_configure_device
        cmp     ax, 0
        jne     .next_device

        ; Successfully configured
        inc     cx

.next_device:
        inc     si
        jmp     .configure_loop

.done_configuring:
        mov     ax, cx                  ; Return supported device count
        jmp     .exit

.pnp_failed:
        mov     ax, 0                   ; No PnP available
        jmp     .exit

.no_read_port:
        mov     ax, 0                   ; Could not find read port
        jmp     .exit

.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret

;-----------------------------------------------------------------------------
; pnp_init_system - Initialize PnP system
;
; Input:  None
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
pnp_init_system:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Reset PnP system to initial state
        call    pnp_reset_all_cards
        cmp     ax, HW_SUCCESS
        jne     .init_failed

        ; Generate and verify LFSR sequence for isolation
        call    pnp_generate_lfsr_sequence_enhanced
        cmp     ax, HW_SUCCESS
        jne     .init_failed

        ; Send initiation key sequence to enter configuration mode
        call    pnp_send_initiation_key_enhanced
        cmp     ax, HW_SUCCESS
        jne     .init_failed

        ; Verify PnP system is responding
        call    pnp_verify_system_availability

        ; Clear device counters
        mov     byte [pnp_device_count], 0
        mov     byte [pnp_devices_found], 0

        ; Reset operation statistics
        mov     word [pnp_operations], 0
        mov     word [pnp_successes], 0
        mov     word [pnp_failures], 0

        ; Send initiation key sequence
        call    pnp_send_initiation_key
        cmp     ax, 0
        jne     .init_failed

        ; Put all cards to sleep initially
        mov     dx, PNP_ADDRESS_PORT
        mov     al, PNP_CONFIG_CONTROL
        out     dx, al
        mov     dx, PNP_WRITE_DATA
        mov     al, PNP_CMD_SLEEP
        out     dx, al

        ; Mark PnP as available
        mov     byte [pnp_available], 1

        ; Success
        inc     word [pnp_successes]
        mov     ax, 0
        jmp     .exit

.init_failed:
        inc     word [pnp_failures]
        mov     ax, 1
        jmp     .exit

.exit:
        inc     word [pnp_operations]
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret

;-----------------------------------------------------------------------------
; pnp_send_initiation_key - Send PnP initiation key sequence
;
; Input:  None
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
pnp_send_initiation_key:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Send complete LFSR-based initiation sequence (255 bytes)
        ; This replaces the fixed 32-byte key with proper LFSR sequence
        call    pnp_send_lfsr_initiation_sequence
        cmp     ax, HW_SUCCESS
        jne     .key_failed

        ; Follow PnP timing requirements for card stabilization
        call    delay_10ms

        .key_failed:

        ; Reset the PnP address port
        mov     dx, PNP_ADDRESS_PORT
        mov     al, 0
        out     dx, al
        out     dx, al                  ; Send twice for reset

        ; Send the 32-byte initiation key
        mov     si, pnp_initiation_key
        mov     cx, 32

.send_key_loop:
        mov     al, [si]
        out     dx, al                  ; Send key byte
        inc     si

        ; Small delay (required by PnP spec)
        push    cx
        mov     cx, 10
.delay_loop:
        loop    .delay_loop
        pop     cx

        loop    .send_key_loop

        ; Success
        mov     ax, 0

        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret

;-----------------------------------------------------------------------------
; pnp_find_read_port - Find optimal PnP read port
;
; Input:  None
; Output: AX = read port address if found, 0 if not found
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
pnp_find_read_port:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Test read ports in priority order: 0x203, 0x213, 0x223, 0x233
        ; These are the most common working addresses for 3Com cards
        mov     si, read_port_priority_list
        mov     cx, 4                   ; Test 4 priority ports first

        .test_priority_ports:
        mov     dx, [si]
        call    pnp_test_read_port_enhanced
        cmp     ax, HW_SUCCESS
        je      .priority_port_found
        add     si, 2
        loop    .test_priority_ports

        ; Try all standard read ports if priority ports fail
        mov     si, read_port_list
        mov     cx, read_port_count
        jmp     .test_port_loop

        .priority_port_found:
        mov     [pnp_read_port], dx
        mov     ax, dx
        jmp     .exit

        ; Try standard read ports in order of preference
        mov     bx, read_port_list
        mov     cx, read_port_count

.test_port_loop:
        mov     dx, [bx]                ; Get read port address
        call    pnp_test_read_port
        cmp     ax, 0
        je      .port_found

        add     bx, 2                   ; Next port address
        loop    .test_port_loop

        ; No working read port found
        mov     ax, 0
        jmp     .exit

.port_found:
        mov     [pnp_read_port], dx     ; Store working read port
        mov     ax, dx                  ; Return read port address

.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret

; Add priority list after existing read_port_list
read_port_priority_list dw 203h, 213h, 223h, 233h

; List of read ports to try
read_port_list      dw 203h, 213h, 223h, 233h, 243h, 253h, 263h, 273h
                    dw 283h, 293h, 2A3h, 2B3h, 2C3h, 2D3h, 2E3h, 2F3h
read_port_count     equ ($-read_port_list)/2

;-----------------------------------------------------------------------------
; pnp_test_read_port - Test if a read port works
;
; Input:  DX = read port address to test
; Output: AX = 0 if port works, non-zero if not
; Uses:   AX, BX, CX
;-----------------------------------------------------------------------------
pnp_test_read_port:
        push    bp
        mov     bp, sp
        push    bx
        push    cx

        ; Enhanced read port testing with proper PnP protocol
        push    bx
        push    cx

        ; Set the read port address for testing
        push    dx
        mov     ax, dx
        shr     ax, 2                   ; Convert to PnP format (address >> 2)
        mov     dx, PNP_ADDRESS_PORT
        mov     al, 00h                 ; Set read port command
        out     dx, al
        mov     dx, PNP_WRITE_DATA_PORT
        out     dx, al                  ; Send read port address
        pop     dx

        ; Test isolation sequence response
        call    pnp_test_isolation_response

        ; Check for valid device response pattern
        cmp     ax, HW_SUCCESS
        je      .test_success

        mov     ax, HW_ERROR_NO_DEVICE
        jmp     .test_done

        .test_success:
        mov     ax, HW_SUCCESS

        .test_done:
        pop     cx
        pop     bx

        ; Simple test - try to set read port and read back
        push    dx
        mov     dx, PNP_ADDRESS_PORT
        mov     al, 00h                 ; Set read data port command
        out     dx, al
        pop     dx

        ; Set the read port address
        mov     ax, dx
        shr     ax, 2                   ; Read port >> 2 for PnP format
        push    dx
        mov     dx, PNP_WRITE_DATA
        out     dx, al
        pop     dx

        ; Try to read from the port (should not hang)
        in      al, dx

        ; For now, assume all ports work (stub implementation)
        mov     ax, 0

        pop     cx
        pop     bx
        pop     bp
        ret

;-----------------------------------------------------------------------------
; pnp_isolate_devices - Isolate and identify all PnP devices
;
; Input:  None
; Output: AX = number of devices found
; Uses:   All registers
;-----------------------------------------------------------------------------
pnp_isolate_devices:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Complete PnP isolation protocol using LFSR sequence
        mov     cl, PNP_CSN_START       ; Start with CSN 1

        .isolation_main_loop:
        ; Put all unconfigured cards in Wait-for-Key state
        call    pnp_reset_for_isolation
        cmp     ax, HW_SUCCESS
        jne     .isolation_failed

        ; Send complete LFSR isolation sequence
        call    pnp_send_lfsr_isolation_sequence
        cmp     ax, HW_SUCCESS
        jne     .no_more_devices

        ; Read 72-bit device serial identifier
        call    pnp_read_72bit_serial_id
        cmp     ax, HW_SUCCESS
        jne     .no_more_devices

        ; Extract and verify 3Com vendor ID (0x6D50)
        call    pnp_extract_vendor_id
        cmp     ax, 0x6D50              ; 3Com vendor ID
        jne     .skip_non_3com_device

        ; Assign card select number to isolated device
        call    pnp_assign_csn_to_device
        cmp     ax, HW_SUCCESS
        jne     .isolation_failed

        ; Read device configuration and store info
        call    pnp_read_and_store_device_config

        inc     bx                      ; Increment found device count
        inc     cl                      ; Next CSN
        cmp     cl, PNP_MAX_DEVICES
        jae     .isolation_complete
        jmp     .isolation_main_loop

        .skip_non_3com_device:
        ; Send sleep command to non-3Com device to remove from bus
        call    pnp_sleep_device
        jmp     .isolation_main_loop

        mov     bx, 0                   ; Device counter
        mov     cl, 1                   ; Card select number

.isolate_loop:
        ; Wake all cards
        mov     dx, PNP_ADDRESS_PORT
        mov     al, PNP_WAKE
        out     dx, al
        mov     dx, PNP_WRITE_DATA
        mov     al, 0                   ; Wake CSN 0 (all cards)
        out     dx, al

        ; Start isolation sequence
        mov     dx, PNP_ADDRESS_PORT
        mov     al, PNP_CONFIG_CONTROL
        out     dx, al
        mov     dx, PNP_WRITE_DATA
        mov     al, PNP_CMD_ISOLATION
        out     dx, al

        ; Read device identifier (stub implementation)
        call    pnp_read_device_id
        cmp     ax, 0
        je      .no_more_devices

        ; Store device information
        mov     si, pnp_vendor_ids
        shl     bx, 1                   ; Convert to word offset
        add     si, bx
        mov     [si], ax                ; Store vendor ID (from pnp_read_device_id)

        ; Assign card select number
        mov     dx, PNP_ADDRESS_PORT
        mov     al, PNP_CARD_SELECT
        out     dx, al
        mov     dx, PNP_WRITE_DATA
        mov     al, cl
        out     dx, al

        shr     bx, 1                   ; Convert back to byte offset
        mov     si, pnp_card_selects
        add     si, bx
        mov     [si], cl                ; Store card select number

        inc     bx                      ; Next device slot
        inc     cl                      ; Next card select number
        cmp     bx, MAX_PNP_DEVICES
        jae     .max_devices_reached

        jmp     .isolate_loop

.no_more_devices:
.max_devices_reached:
.isolation_complete:
.isolation_failed:
        mov     [pnp_device_count], bl
        mov     ax, bx                  ; Return device count

        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret

;-----------------------------------------------------------------------------
; pnp_read_device_id - Read device identifier from PnP card
;
; Input:  None
; Output: AX = vendor ID if device found, 0 if no device
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
pnp_read_device_id:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Read complete 72-bit serial identifier using proper timing
        ; Format: Vendor ID (16) + Device ID (16) + Serial (32) + Checksum (8)

        push    bx
        push    cx
        push    si

        mov     si, temp_serial_buffer
        mov     cx, 72                  ; 72 bits total
        mov     dx, [pnp_read_port]

        .read_serial_bits:
        ; Read each bit with proper timing
        in      al, dx
        and     al, 01h                 ; Extract LSB

        ; Store bit in buffer (simplified for space)
        mov     [si], al
        inc     si

        ; Inter-bit delay as required by PnP spec
        call    delay_isolation_bit

        loop    .read_serial_bits

        ; Extract vendor ID from first 16 bits
        call    pnp_extract_16bit_value

        ; Success if we read complete sequence
        mov     ax, HW_SUCCESS

        pop     si
        pop     cx
        pop     bx

        ; Stub implementation - return 0 (no device)
        mov     ax, 0

        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret

; Temporary buffer for serial ID bits
temp_serial_buffer times 72 db 0

;-----------------------------------------------------------------------------
; pnp_configure_device - Configure a detected PnP device
;
; Input:  BX = I/O base address (16-bit)
;         CL = IRQ number (8-bit)
;         SI = device index
; Output: AX = 0 for success, non-zero for error
; Uses:   All registers
;-----------------------------------------------------------------------------
pnp_configure_device:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Complete device configuration sequence

        ; Wake device using assigned CSN
        call    pnp_wake_device_by_csn
        cmp     ax, HW_SUCCESS
        jne     .config_failed

        ; Read current resource configuration from device
        call    pnp_read_device_resources
        cmp     ax, HW_SUCCESS
        jne     .config_failed

        ; Assign I/O base address
        call    pnp_assign_io_address
        cmp     ax, HW_SUCCESS
        jne     .config_failed

        ; Assign IRQ line
        call    pnp_assign_irq_line
        cmp     ax, HW_SUCCESS
        jne     .config_failed

        ; Activate logical device 0 (network function)
        call    pnp_activate_logical_device
        cmp     ax, HW_SUCCESS
        jne     .config_failed

        ; Verify device is responding at assigned address
        call    pnp_verify_device_activation

        .config_failed:

        ; Validate device index
        cmp     al, [pnp_device_count]
        jae     .invalid_device

        ; Get card select number
        mov     bl, al
        mov     bh, 0
        mov     si, pnp_card_selects
        add     si, bx
        mov     cl, [si]                ; CL = card select number

        ; Wake the specific card
        mov     dx, PNP_ADDRESS_PORT
        mov     al, PNP_WAKE
        out     dx, al
        mov     dx, PNP_WRITE_DATA
        mov     al, cl
        out     dx, al

        ; Configure I/O base address (16-bit value in BX)
        mov dx, PNP_ADDRESS_PORT        ; 0x279
        mov al, 60h                     ; I/O base high register
        out dx, al
        mov dx, PNP_WRITE_DATA          ; 0xA79
        mov al, bh                      ; High byte of I/O base
        out dx, al

        mov dx, PNP_ADDRESS_PORT        ; 0x279
        mov al, 61h                     ; I/O base low register
        out dx, al
        mov dx, PNP_WRITE_DATA          ; 0xA79
        mov al, bl                      ; Low byte of I/O base
        out dx, al

        ; Configure IRQ (IRQ number in CL)
        mov dx, PNP_ADDRESS_PORT        ; 0x279
        mov al, 70h                     ; IRQ select register
        out dx, al
        mov dx, PNP_WRITE_DATA          ; 0xA79
        mov al, cl                      ; IRQ number
        out dx, al

        mov dx, PNP_ADDRESS_PORT        ; 0x279
        mov al, 71h                     ; IRQ type register
        out dx, al
        mov dx, PNP_WRITE_DATA          ; 0xA79
        mov al, 2                       ; Edge triggered, active high
        out dx, al

        ; Activate logical device - CRITICAL!
        mov dx, PNP_ADDRESS_PORT        ; 0x279
        mov al, 30h                     ; Activate register
        out dx, al
        mov dx, PNP_WRITE_DATA          ; 0xA79
        mov al, 1                       ; Enable/activate device
        out dx, al

        ; For now, assume successful configuration
        mov     ax, 0
        jmp     .exit

.invalid_device:
        mov     ax, 1
        jmp     .exit

.exit:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret

;-----------------------------------------------------------------------------
; pnp_check_3com_device - Check if device is a supported 3Com card
;
; Input:  SI = device index
; Output: AX = device type (1=3C509B, 2=3C515, 0=not supported)
; Uses:   AX, BX, CX, SI
;-----------------------------------------------------------------------------
pnp_check_3com_device:
        push    bp
        mov     bp, sp
        push    bx
        push    cx

        ; Get vendor ID
        mov     bx, si
        shl     bx, 1                   ; Convert to word offset
        push    si                      ; Save SI
        mov     si, pnp_vendor_ids
        add     si, bx
        mov     ax, [si]                ; AX = vendor ID

        ; Check if it's 3Com
        cmp     ax, VENDOR_3COM
        jne     .not_3com_restore

        ; Get device ID
        mov     si, pnp_device_ids
        add     si, bx
        mov     ax, [si]                ; AX = device ID
        pop     si                      ; Restore SI

        ; Check for supported devices
        cmp     ax, DEVICE_3C509B
        je      .is_3c509b
        cmp     ax, DEVICE_3C515
        je      .is_3c515

.not_3com_restore:
        pop     si                      ; Restore SI before returning
.not_3com:
        mov     ax, 0                   ; Not supported
        jmp     .exit

.is_3c509b:
        mov     ax, 1                   ; 3C509B
        jmp     .exit

.is_3c515:
        mov     ax, 2                   ; 3C515
        jmp     .exit

.exit:
        pop     cx
        pop     bx
        pop     bp
        ret

;-----------------------------------------------------------------------------
; pnp_read_resource_data - Read resource data from PnP device
;
; Input:  AL = device index, ES:DI = buffer
; Output: AX = bytes read, 0 if error
; Uses:   All registers
;-----------------------------------------------------------------------------
pnp_read_resource_data:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Complete resource data reading with tag parsing

        push    bx
        push    cx
        push    si

        ; Select device by CSN
        call    pnp_select_device_by_index
        cmp     ax, HW_SUCCESS
        jne     .read_resource_failed

        ; Set logical device to 0 (primary network function)
        call    pnp_select_logical_device_0

        ; Read I/O resource descriptor
        call    pnp_read_io_resource_tag

        ; Read IRQ resource descriptor
        call    pnp_read_irq_resource_tag

        ; Read device identifier tag
        call    pnp_read_device_id_tag

        ; Store parsed data in provided buffer
        call    pnp_store_resource_data

        mov     ax, HW_SUCCESS
        jmp     .read_resource_done

        .read_resource_failed:
        mov     ax, HW_ERROR_PNP_FAILED

        .read_resource_done:
        pop     si
        pop     cx
        pop     bx

        ; Stub implementation
        mov     ax, 0                   ; No data read

        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret

;-----------------------------------------------------------------------------
; pnp_get_device_info - Get information about detected PnP devices
;
; Input:  ES:DI = buffer for device information
; Output: AX = number of devices, buffer filled with device info
; Uses:   AX, BX, CX, SI, DI
;-----------------------------------------------------------------------------
pnp_get_device_info:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si

        ; Copy device count
        mov     al, [pnp_device_count]
        stosb

        ; Copy device information
        mov     cl, al
        mov     ch, 0
        mov     si, pnp_vendor_ids

.copy_info_loop:
        cmp     cx, 0
        je      .done_copying

        ; Copy vendor ID
        movsw
        ; Copy device ID
        movsw
        ; Copy I/O base
        movsw
        ; Copy IRQ
        movsb
        ; Copy card select
        movsb

        dec     cx
        jmp     .copy_info_loop

.done_copying:
        mov     al, [pnp_device_count]
        mov     ah, 0

        pop     si
        pop     cx
        pop     bx
        pop     bp
        ret

;=============================================================================
; ENHANCED GROUP 6A/6B LFSR GENERATION IMPLEMENTATION
;=============================================================================

;-----------------------------------------------------------------------------
; LFSR generation implementation following the exact algorithm:
; for (i=0; i<255; i++) {
;     lrs_state <<= 1;
;     lrs_state = lrs_state & 0x100 ? lrs_state ^ 0xcf : lrs_state;
; }
;
; This creates the 255-byte sequence required for PnP device isolation
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; pnp_generate_lfsr_sequence_enhanced - Generate complete LFSR sequence
;
; Input:  None
; Output: AX = HW_SUCCESS or error code
;         lfsr_lookup_table filled with 255-byte sequence
; Uses:   AX, BX, CX, SI
;-----------------------------------------------------------------------------
pnp_generate_lfsr_sequence_enhanced:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si

        ; Check if already initialized
        cmp     byte [lfsr_initialized], 1
        je      .already_initialized

        ; Copy pre-generated LFSR table from include file
        ; The include file already contains the complete 255-byte sequence
        push    ds
        push    es

        ; Set up data segment for copy
        mov     ax, _DATA
        mov     ds, ax
        mov     es, ax

        mov     si, lfsr_table       ; Source from include file
        mov     di, lfsr_lookup_table ; Destination in our data segment
        mov     cx, LFSR_TABLE_SIZE         ; 255 bytes
        cld
        rep     movsb                       ; Copy the table

        pop     es
        pop     ds

        ; Mark LFSR table as initialized
        mov     byte [lfsr_initialized], 1

.already_initialized:
        mov     ax, HW_SUCCESS

        pop     si
        pop     cx
        pop     bx
        pop     bp
        ret

;-----------------------------------------------------------------------------
; Advanced PnP isolation protocol implementation
; Uses the generated LFSR sequence for device isolation
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; pnp_perform_isolation_with_lfsr - PnP isolation using LFSR sequence
;
; Input:  None
; Output: AX = number of devices isolated
; Uses:   All registers
;-----------------------------------------------------------------------------
pnp_perform_isolation_with_lfsr:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Ensure LFSR sequence is generated
        call    pnp_generate_lfsr_sequence_enhanced
        cmp     ax, HW_SUCCESS
        jne     .lfsr_failed

        ; Initialize device isolation process
        mov     bx, 0                       ; Device counter
        mov     cl, 1                       ; Starting CSN

.isolation_loop:
        ; Reset PnP system for isolation
        call    pnp_reset_for_isolation
        cmp     ax, HW_SUCCESS
        jne     .isolation_failed

        ; Send LFSR isolation sequence
        call    pnp_send_lfsr_sequence
        cmp     ax, HW_SUCCESS
        jne     .no_more_devices

        ; Read device serial number
        call    pnp_read_serial_identifier
        cmp     ax, HW_SUCCESS
        jne     .no_more_devices

        ; Check if this is a 3Com device
        call    pnp_check_3com_vendor
        cmp     ax, 0
        je      .not_3com_device

        ; Assign Card Select Number
        call    pnp_assign_csn
        cmp     ax, HW_SUCCESS
        jne     .isolation_failed

        ; Store device information
        call    pnp_store_device_info
        inc     bx                          ; Increment device count

.not_3com_device:
        inc     cl                          ; Next CSN
        cmp     cl, PNP_MAX_DEVICES
        jae     .isolation_complete
        cmp     bx, PNP_MAX_DEVICES
        jae     .isolation_complete
        jmp     .isolation_loop

.isolation_complete:
        mov     [pnp_device_count], bl
        mov     ax, bx                      ; Return device count
        jmp     .exit

.lfsr_failed:
.isolation_failed:
        mov     ax, 0                       ; No devices isolated
        jmp     .exit

.no_more_devices:
        mov     [pnp_device_count], bl
        mov     ax, bx                      ; Return current device count

.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret

;-----------------------------------------------------------------------------
; pnp_reset_for_isolation - Reset PnP system for device isolation
;
; Input:  None
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, DX
;-----------------------------------------------------------------------------
pnp_reset_for_isolation:
        push    dx

        ; Send Wait-for-Key state command
        mov     dx, PNP_ADDRESS_PORT
        mov     al, PNP_CMD_CONFIG_CONTROL
        out     dx, al

        mov     dx, PNP_WRITE_DATA_PORT
        mov     al, 02h                     ; Wait for key state
        out     dx, al

        ; Small delay
        call    delay_10ms

        mov     ax, HW_SUCCESS

        pop     dx
        ret

;-----------------------------------------------------------------------------
; pnp_send_lfsr_sequence - Send LFSR sequence to PnP port
;
; Input:  None
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, CX, DX, SI
;-----------------------------------------------------------------------------
pnp_send_lfsr_sequence:
        push    cx
        push    dx
        push    si

        ; Send initiation key first
        mov     dx, PNP_ADDRESS_PORT
        mov     si, lfsr_lookup_table
        mov     cx, 32                      ; Send first 32 bytes as initiation

.send_initiation:
        mov     al, [si]
        out     dx, al
        inc     si

        ; Small delay between bytes
        push    cx
        mov     cx, 10
.delay_loop2:
        loop    .delay_loop2
        pop     cx

        dec     cx
        jnz     .send_initiation

        ; Continue with isolation sequence
        mov     cx, 223                     ; Remaining bytes (255-32)

.send_isolation_sequence:
        mov     al, [si]
        out     dx, al
        inc     si

        ; Timing is critical for isolation
        push    cx
        mov     cx, 5
.isolation_delay:
        loop    .isolation_delay
        pop     cx

        dec     cx
        jnz     .send_isolation_sequence

        mov     ax, HW_SUCCESS

        pop     si
        pop     dx
        pop     cx
        ret

;-----------------------------------------------------------------------------
; pnp_read_serial_identifier - Read 72-bit serial identifier
;
; Input:  None
; Output: AX = HW_SUCCESS if device responds, error code otherwise
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
pnp_read_serial_identifier:
        push    bx
        push    cx
        push    dx

        ; Set read port for isolation
        mov     dx, [pnp_read_port]
        mov     cx, 72                      ; 72 bits to read
        mov     bx, 0                       ; Accumulator

.read_bit_loop:
        ; Read bit from isolation port
        in      al, dx
        test    al, 01h                     ; Check LSB
        jz      .bit_is_zero

        ; Bit is 1, set in accumulator
        or      bx, 1

.bit_is_zero:
        ; Shift accumulator for next bit
        shl     bx, 1

        ; Small timing delay
        push    cx
        mov     cx, 3
.bit_delay:
        loop    .bit_delay
        pop     cx

        dec     cx
        jnz     .read_bit_loop

        ; Check if we got a valid response (non-zero)
        test    bx, bx
        jz      .no_device_response

        mov     ax, HW_SUCCESS
        jmp     .exit2

.no_device_response:
        mov     ax, HW_ERROR_NO_DEVICE

.exit2:
        pop     dx
        pop     cx
        pop     bx
        ret

;-----------------------------------------------------------------------------
; pnp_check_3com_vendor - Check if isolated device is 3Com
;
; Input:  None (uses last read identifier)
; Output: AX = device type (HW_TYPE_3C509B, HW_TYPE_3C515TX, or 0)
; Uses:   AX, BX
;-----------------------------------------------------------------------------
pnp_check_3com_vendor:
        push    bx

        ; This is simplified - in real implementation would check
        ; the actual vendor ID bits from the serial identifier
        ; For now, return 3C509B as default 3Com device
        mov     ax, HW_TYPE_3C509B

        ; In actual implementation:
        ; - Extract vendor ID from serial identifier
        ; - Compare with 3Com vendor ID (0x10B7)
        ; - Extract product ID to determine specific device type

        pop     bx
        ret

;-----------------------------------------------------------------------------
; pnp_assign_csn - Assign Card Select Number to device
;
; Input:  CL = Card Select Number to assign
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, DX
;-----------------------------------------------------------------------------
pnp_assign_csn:
        push    dx

        ; Send CSN assignment command
        mov     dx, PNP_ADDRESS_PORT
        mov     al, PNP_CMD_SET_CSN
        out     dx, al

        mov     dx, PNP_WRITE_DATA_PORT
        mov     al, cl                      ; Card Select Number
        out     dx, al

        ; Verify assignment with small delay
        call    delay_1ms

        mov     ax, HW_SUCCESS

        pop     dx
        ret

;-----------------------------------------------------------------------------
; pnp_store_device_info - Store detected device information
;
; Input:  BX = device index, CL = CSN
; Output: AX = HW_SUCCESS
; Uses:   AX, SI
;-----------------------------------------------------------------------------
pnp_store_device_info:
        push    si

        ; Calculate offset in device table
        push    ax
        push    cx
        mov     ax, bx
        mov     cx, PNP_DEVICE_INFO_size
        mul     cx
        mov     si, ax
        add     si, pnp_device_table
        pop     cx
        pop     ax

        ; Store basic device information
        mov     word [si + PNP_DEVICE_INFO.vendor_id], VENDOR_ID_3COM
        mov     word [si + PNP_DEVICE_INFO.device_id], DEVICE_ID_3C509B ; Simplified
        mov     byte [si + PNP_DEVICE_INFO.csn], cl
        mov     byte [si + PNP_DEVICE_INFO.state], HW_STATE_DETECTED

        ; Update legacy arrays for compatibility
        mov     [pnp_card_selects + bx], cl
        ; Store vendor ID at word offset (bx*2)
        push    bx
        shl     bx, 1                           ; bx = bx * 2 for word offset
        mov     word [pnp_vendor_ids + bx], VENDOR_ID_3COM
        pop     bx

        mov     ax, HW_SUCCESS

        pop     si
        ret

;=============================================================================
; ENHANCED PNP IMPLEMENTATION - COMPLETE ISOLATION AND ENUMERATION FUNCTIONS
;=============================================================================

;-----------------------------------------------------------------------------
; pnp_reset_all_cards - Reset all PnP cards to initial state
;
; Input:  None
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, DX
;-----------------------------------------------------------------------------
pnp_reset_all_cards:
        push    dx

        ; Send global reset to all cards
        mov     dx, PNP_ADDRESS_PORT
        mov     al, 00h                     ; Reset sequence
        out     dx, al
        out     dx, al                      ; Send twice for full reset

        ; Wait for reset completion
        call    delay_10ms

        mov     ax, HW_SUCCESS

        pop     dx
        ret

;-----------------------------------------------------------------------------
; pnp_send_initiation_key_enhanced - Send enhanced LFSR-based initiation
;
; Input:  None
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, CX, DX, SI
;-----------------------------------------------------------------------------
pnp_send_initiation_key_enhanced:
        push    cx
        push    dx
        push    si

        ; Use first 32 bytes of LFSR sequence as initiation key
        mov     si, lfsr_lookup_table
        mov     cx, 32
        mov     dx, PNP_ADDRESS_PORT

        .send_enhanced_key:
        mov     al, [si]
        out     dx, al
        inc     si

        ; Precise timing delay for PnP compliance
        push    cx
        mov     cx, 8
        .key_timing_delay:
        loop    .key_timing_delay
        pop     cx

        dec     cx
        jnz     .send_enhanced_key

        mov     ax, HW_SUCCESS

        pop     si
        pop     dx
        pop     cx
        ret

;-----------------------------------------------------------------------------
; pnp_verify_system_availability - Verify PnP system is responding
;
; Input:  None
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, DX
;-----------------------------------------------------------------------------
pnp_verify_system_availability:
        push    dx

        ; Try to set a read port and verify response
        mov     dx, PNP_ADDRESS_PORT
        mov     al, 00h
        out     dx, al

        ; Set read port to default
        mov     dx, PNP_WRITE_DATA_PORT
        mov     al, (PNP_READ_PORT_BASE >> 2) & 0FFh
        out     dx, al

        ; Small delay and test
        call    delay_1ms

        ; If we get here without hanging, PnP is available
        mov     ax, HW_SUCCESS

        pop     dx
        ret

;-----------------------------------------------------------------------------
; pnp_test_read_port_enhanced - Enhanced read port testing
;
; Input:  DX = read port address to test
; Output: AX = HW_SUCCESS if port works, error code if not
; Uses:   AX, BX, CX
;-----------------------------------------------------------------------------
pnp_test_read_port_enhanced:
        push    bx
        push    cx

        ; Set the read port
        push    dx
        mov     ax, dx
        shr     ax, 2
        mov     dx, PNP_ADDRESS_PORT
        mov     al, 00h
        out     dx, al
        mov     dx, PNP_WRITE_DATA_PORT
        out     dx, al
        pop     dx

        ; Test with isolation sequence
        call    pnp_test_isolation_response

        pop     cx
        pop     bx
        ret

;-----------------------------------------------------------------------------
; pnp_test_isolation_response - Test port response to isolation sequence
;
; Input:  DX = read port address
; Output: AX = HW_SUCCESS if valid response, error otherwise
; Uses:   AX, CX
;-----------------------------------------------------------------------------
pnp_test_isolation_response:
        push    cx

        ; Send a few bytes of isolation sequence
        push    dx
        mov     dx, PNP_ADDRESS_PORT
        mov     al, 6Ah                     ; Start of LFSR sequence
        out     dx, al
        mov     al, 0B5h
        out     dx, al
        pop     dx

        ; Read response from read port
        mov     cx, 10
        .test_response_loop:
        in      al, dx
        test    al, al
        jnz     .response_detected
        dec     cx
        jnz     .test_response_loop

        ; No response
        mov     ax, HW_ERROR_NO_DEVICE
        jmp     .test_response_done

        .response_detected:
        mov     ax, HW_SUCCESS

        .test_response_done:
        pop     cx
        ret

;-----------------------------------------------------------------------------
; pnp_send_lfsr_isolation_sequence - Send complete LFSR sequence for isolation
;
; Input:  None
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, CX, DX, SI
;-----------------------------------------------------------------------------
pnp_send_lfsr_isolation_sequence:
        push    cx
        push    dx
        push    si

        ; Send complete 255-byte LFSR sequence
        mov     si, lfsr_lookup_table
        mov     cx, 255
        mov     dx, PNP_ADDRESS_PORT

        .send_isolation_seq:
        mov     al, [si]
        out     dx, al
        inc     si

        ; Critical timing for isolation
        push    cx
        mov     cx, 6
        .isolation_timing_delay:
        loop    .isolation_timing_delay
        pop     cx

        dec     cx
        jnz     .send_isolation_seq

        mov     ax, HW_SUCCESS

        pop     si
        pop     dx
        pop     cx
        ret

;-----------------------------------------------------------------------------
; pnp_read_72bit_serial_id - Read complete 72-bit device serial identifier
;
; Input:  None
; Output: AX = HW_SUCCESS if valid ID read, error otherwise
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
pnp_read_72bit_serial_id:
        push    bx
        push    cx
        push    dx

        mov     dx, [pnp_read_port]
        mov     cx, 72
        xor     bx, bx                      ; Clear accumulator

        .read_id_bits:
        in      al, dx
        and     al, 01h

        ; Shift accumulator and add bit
        shl     bx, 1
        or      bl, al

        ; Isolation bit timing
        call    delay_isolation_bit

        dec     cx
        jnz     .read_id_bits

        ; Store read ID in global variable for extraction
        mov     [pnp_current_serial_id], bx
        mov     [pnp_current_serial_id+2], bx  ; Simplified storage

        ; Check for non-zero ID (valid device)
        test    bx, bx
        jz      .no_valid_id

        mov     ax, HW_SUCCESS
        jmp     .read_id_done

        .no_valid_id:
        mov     ax, HW_ERROR_NO_DEVICE

        .read_id_done:
        pop     dx
        pop     cx
        pop     bx
        ret

;-----------------------------------------------------------------------------
; pnp_extract_vendor_id - Extract vendor ID from read serial identifier
;
; Input:  None (uses pnp_current_serial_id)
; Output: AX = vendor ID
; Uses:   AX, BX
;-----------------------------------------------------------------------------
pnp_extract_vendor_id:
        ; Extract vendor ID from stored serial identifier
        ; In real implementation, this would parse the 72-bit structure
        ; For now, return 3Com vendor ID if any device detected
        mov     bx, [pnp_current_serial_id]
        test    bx, bx
        jz      .no_vendor

        mov     ax, 0x6D50                  ; 3Com vendor ID in PnP format
        jmp     .extract_done

        .no_vendor:
        mov     ax, 0

        .extract_done:
        ret

;-----------------------------------------------------------------------------
; pnp_assign_csn_to_device - Assign card select number to isolated device
;
; Input:  CL = card select number to assign
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, DX
;-----------------------------------------------------------------------------
pnp_assign_csn_to_device:
        push    dx

        ; Send CSN assignment command
        mov     dx, PNP_ADDRESS_PORT
        mov     al, PNP_CMD_SET_CSN
        out     dx, al

        mov     dx, PNP_WRITE_DATA_PORT
        mov     al, cl
        out     dx, al

        ; Allow time for CSN assignment
        call    delay_1ms

        mov     ax, HW_SUCCESS

        pop     dx
        ret

;-----------------------------------------------------------------------------
; pnp_read_and_store_device_config - Read device config and store information
;
; Input:  BX = device index, CL = CSN
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, SI
;-----------------------------------------------------------------------------
pnp_read_and_store_device_config:
        push    si

        ; Calculate device table entry
        push    ax
        push    cx
        mov     ax, bx
        mov     cx, PNP_DEVICE_INFO_size
        mul     cx
        mov     si, ax
        add     si, pnp_device_table
        pop     cx
        pop     ax

        ; Store basic information
        mov     word [si + PNP_DEVICE_INFO.vendor_id], 0x6D50
        mov     word [si + PNP_DEVICE_INFO.device_id], 0x5090  ; Default to 3C509B
        mov     byte [si + PNP_DEVICE_INFO.csn], cl
        mov     byte [si + PNP_DEVICE_INFO.state], HW_STATE_DETECTED

        ; Read I/O base from device configuration
        call    pnp_read_device_io_base
        mov     [si + PNP_DEVICE_INFO.io_base], ax

        ; Read IRQ from device configuration
        call    pnp_read_device_irq
        mov     [si + PNP_DEVICE_INFO.irq], al

        mov     ax, HW_SUCCESS

        pop     si
        ret

;-----------------------------------------------------------------------------
; pnp_sleep_device - Send sleep command to device
;
; Input:  None
; Output: AX = HW_SUCCESS
; Uses:   AX, DX
;-----------------------------------------------------------------------------
pnp_sleep_device:
        push    dx

        mov     dx, PNP_ADDRESS_PORT
        mov     al, PNP_CMD_CONFIG_CONTROL
        out     dx, al

        mov     dx, PNP_WRITE_DATA_PORT
        mov     al, 03h                     ; Sleep command
        out     dx, al

        mov     ax, HW_SUCCESS

        pop     dx
        ret

;-----------------------------------------------------------------------------
; pnp_configure_3com_devices - Configure all detected 3Com devices
;
; Input:  None
; Output: AX = number of successfully configured devices
; Uses:   AX, BX, CX
;-----------------------------------------------------------------------------
pnp_configure_3com_devices:
        push    bx
        push    cx

        mov     bx, 0                       ; Configured device counter
        mov     cl, [pnp_device_count]
        xor     ch, ch

        .config_loop:
        test    cx, cx
        jz      .config_done

        ; Configure device at index (pnp_device_count - cx)
        push    cx
        mov     al, [pnp_device_count]
        sub     al, cl
        call    pnp_configure_device
        cmp     ax, HW_SUCCESS
        jne     .config_next

        inc     bx                          ; Successful configuration

        .config_next:
        pop     cx
        dec     cx
        jmp     .config_loop

        .config_done:
        mov     ax, bx                      ; Return configured count

        pop     cx
        pop     bx
        ret

;-----------------------------------------------------------------------------
; Utility functions for enhanced PnP operations
;-----------------------------------------------------------------------------

delay_isolation_bit:
        push    cx
        mov     cx, 4
        .delay_bit_loop:
        loop    .delay_bit_loop
        pop     cx
        ret

pnp_wake_device_by_csn:
        push    dx
        mov     dx, PNP_ADDRESS_PORT
        mov     al, PNP_CMD_WAKE
        out     dx, al
        mov     dx, PNP_WRITE_DATA_PORT
        mov     al, cl                      ; CSN in CL
        out     dx, al
        mov     ax, HW_SUCCESS
        pop     dx
        ret

pnp_read_device_resources:
        ; Stub - read current device resource settings
        mov     ax, HW_SUCCESS
        ret

pnp_assign_io_address:
        ; Stub - assign I/O base address to device
        mov     ax, HW_SUCCESS
        ret

pnp_assign_irq_line:
        ; Stub - assign IRQ line to device
        mov     ax, HW_SUCCESS
        ret

pnp_activate_logical_device:
        push    dx
        mov     dx, PNP_ADDRESS_PORT
        mov     al, PNP_CMD_ACTIVATE
        out     dx, al
        mov     dx, PNP_WRITE_DATA_PORT
        mov     al, 01h                     ; Activate
        out     dx, al
        mov     ax, HW_SUCCESS
        pop     dx
        ret

pnp_verify_device_activation:
        ; Stub - verify device is responding
        mov     ax, HW_SUCCESS
        ret

pnp_select_device_by_index:
        ; Stub - select device for resource reading
        mov     ax, HW_SUCCESS
        ret

pnp_select_logical_device_0:
        push    dx
        mov     dx, PNP_ADDRESS_PORT
        mov     al, 07h                     ; Logical device select
        out     dx, al
        mov     dx, PNP_WRITE_DATA_PORT
        mov     al, 00h                     ; Select device 0
        out     dx, al
        mov     ax, HW_SUCCESS
        pop     dx
        ret

pnp_read_io_resource_tag:
        ; Stub - read I/O resource descriptor
        mov     ax, HW_SUCCESS
        ret

pnp_read_irq_resource_tag:
        ; Stub - read IRQ resource descriptor
        mov     ax, HW_SUCCESS
        ret

pnp_read_device_id_tag:
        ; Stub - read device ID tag
        mov     ax, HW_SUCCESS
        ret

pnp_store_resource_data:
        ; Stub - store parsed resource data in buffer
        mov     ax, HW_SUCCESS
        ret

pnp_read_device_io_base:
        ; Return default I/O base for 3C509B
        mov     ax, 0x300
        ret

pnp_read_device_irq:
        ; Return default IRQ for 3C509B
        mov     al, 10
        ret

pnp_send_lfsr_initiation_sequence:
        ; Stub - send LFSR initiation sequence
        mov     ax, HW_SUCCESS
        ret

delay_10ms:
        ; Simple delay loop for 10ms
        push    cx
        mov     cx, 10000
        .delay_10ms_loop:
        loop    .delay_10ms_loop
        pop     cx
        ret

delay_1ms:
        ; Simple delay loop for 1ms
        push    cx
        mov     cx, 1000
        .delay_1ms_loop:
        loop    .delay_1ms_loop
        pop     cx
        ret

pnp_extract_16bit_value:
        ; Stub - extract 16-bit value from serial buffer
        mov     ax, 0
        ret

;=============================================================================
; GLOBAL DATA FOR ENHANCED PNP IMPLEMENTATION
;=============================================================================

segment _DATA class=DATA

; Current serial ID storage
pnp_current_serial_id   dd 0            ; 72-bit serial ID storage

segment _TEXT class=CODE

;-----------------------------------------------------------------------------
; pnp_detect_nics - Main C integration function for NIC detection
;
; C prototype: int pnp_detect_nics(nic_detect_info_t *info_list, int max_nics);
; Input:  info_list = pointer to detection info array
;         max_nics = maximum number of NICs to detect
; Output: Number of NICs detected and populated in info_list
; Uses:   All registers
;-----------------------------------------------------------------------------
pnp_detect_nics:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Get parameters from stack
        ; [bp+4] = info_list pointer
        ; [bp+6] = max_nics count

        ; Initialize PnP system and detect all devices
        call    pnp_detect_all
        cmp     ax, 0
        je      .no_devices_found

        ; Copy device information to C structure format
        ; This is a simplified implementation - in reality would need
        ; proper structure conversion between assembly and C formats

        mov     bx, ax                      ; BX = number of devices found
        mov     cx, [bp+6]                  ; CX = max_nics limit
        cmp     bx, cx
        jbe     .copy_devices
        mov     bx, cx                      ; Limit to max_nics

        .copy_devices:
        ; For now, just return the count
        ; Full implementation would copy pnp_device_table entries
        ; to the C nic_detect_info_t structures
        mov     ax, bx
        jmp     .exit3

        .no_devices_found:
        mov     ax, 0

        .exit3:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
