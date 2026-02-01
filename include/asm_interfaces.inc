; @file asm_interfaces.inc
; @brief Assembly interface definitions for hardware detection and management
;
; 3Com Packet Driver - Assembly interfaces for Groups 6A & 6B implementation
; Provides clean interfaces for hardware detection, IRQ management, and PnP operations
;
; This file is part of the 3Com Packet Driver project.
;
; Converted from MASM/WASM to NASM syntax: 2026-01-25 10:57:26

%ifndef ASM_INTERFACES_INC
%define ASM_INTERFACES_INC 1

; Include defensive programming patterns (NASM version with WAIT_FOR_CONDITION macro)
%include "tsr_defensive.inc"

;=============================================================================
; HARDWARE ERROR CODES (matching C layer)
;=============================================================================

; Success and generic errors
HW_SUCCESS                  equ 0       ; Operation successful
HW_ERROR_GENERIC           equ 1       ; Generic hardware error
HW_ERROR_TIMEOUT           equ 2       ; Operation timeout
HW_ERROR_NO_DEVICE         equ 3       ; Device not found
HW_ERROR_INVALID_PARAM     equ 4       ; Invalid parameter
HW_ERROR_NO_MEMORY         equ 5       ; Insufficient memory
HW_ERROR_IO_ERROR          equ 6       ; I/O operation failed
HW_ERROR_BUSY              equ 7       ; Hardware busy

; Hardware-specific errors
HW_ERROR_3C509B_NOT_FOUND  equ 10      ; 3C509B not detected
HW_ERROR_3C515_NOT_FOUND   equ 11      ; 3C515-TX not detected
HW_ERROR_PNP_FAILED        equ 12      ; PnP detection failed
HW_ERROR_IRQ_CONFLICT      equ 13      ; IRQ conflict detected
HW_ERROR_IO_CONFLICT       equ 14      ; I/O address conflict
HW_ERROR_EEPROM_READ       equ 15      ; EEPROM read failure
HW_ERROR_ACTIVATION        equ 16      ; Device activation failed
HW_ERROR_RESET_FAILED      equ 17      ; Hardware reset failed
HW_ERROR_CONFIG_INVALID    equ 18      ; Invalid configuration

;=============================================================================
; HARDWARE CONSTANTS
;=============================================================================

; Maximum hardware instances
MAX_HW_INSTANCES           equ 8       ; Maximum NICs supported

; Hardware types
HW_TYPE_UNKNOWN           equ 0       ; Unknown hardware type
HW_TYPE_3C509B            equ 1       ; 3Com 3C509B (10 Mbps ISA)
HW_TYPE_3C515TX           equ 2       ; 3Com 3C515-TX (100 Mbps ISA)

; Hardware states
HW_STATE_UNDETECTED       equ 0       ; Not detected
HW_STATE_DETECTED         equ 1       ; Detected but not configured
HW_STATE_CONFIGURED       equ 2       ; Configured and ready
HW_STATE_ACTIVE           equ 3       ; Active and operational
HW_STATE_ERROR            equ 0FFh    ; Error state

; I/O address ranges
ISA_IO_MIN                equ 200h    ; Minimum ISA I/O address
ISA_IO_MAX                equ 3FFh    ; Maximum ISA I/O address
EISA_IO_MIN               equ 1000h   ; Minimum EISA I/O address
EISA_IO_MAX               equ 9FFFh   ; Maximum EISA I/O address

; IRQ definitions
IRQ_MIN                   equ 2       ; Minimum valid IRQ
IRQ_MAX                   equ 15      ; Maximum valid IRQ
IRQ_AUTO_ASSIGN           equ 0FFh    ; Auto-assign IRQ flag

;=============================================================================
; PNP CONSTANTS
;=============================================================================

; PnP I/O ports
PNP_ADDRESS_PORT          equ 279h    ; PnP address port
PNP_WRITE_DATA_PORT       equ 0A79h   ; PnP write data port
PNP_READ_PORT_BASE        equ 203h    ; Base PnP read port

; PnP commands
PNP_CMD_INITIATION        equ 00h     ; Initiation sequence
PNP_CMD_WAKE              equ 03h     ; Wake command
PNP_CMD_CONFIG_CONTROL    equ 02h     ; Configuration control
PNP_CMD_SET_CSN           equ 06h     ; Set card select number
PNP_CMD_ACTIVATE          equ 30h     ; Activate device

; PnP resource registers
PNP_REG_IO_BASE_HIGH      equ 60h     ; I/O base high byte
PNP_REG_IO_BASE_LOW       equ 61h     ; I/O base low byte
PNP_REG_IRQ_SELECT        equ 70h     ; IRQ selection
PNP_REG_DMA_SELECT        equ 74h     ; DMA selection

; 3Com PnP IDs
VENDOR_ID_3COM            equ 0x10B7  ; 3Com vendor ID
DEVICE_ID_3C509B          equ 0x5090  ; 3C509B device ID
DEVICE_ID_3C515TX         equ 0x5051  ; 3C515-TX device ID

; PnP state constants
PNP_MAX_DEVICES           equ 8       ; Maximum PnP devices
PNP_LFSR_SEED             equ 0x6A    ; LFSR seed for isolation
PNP_CSN_START             equ 1       ; Starting card select number

;=============================================================================
; WINDOW SELECTION MACROS
;=============================================================================

; 3C509B window selection macro
; Parameters: %1 = io_base (register), %2 = window (immediate)
; Modifies: AX, DX (if io_base is not DX)
%macro SELECT_3C509B_WINDOW 2
    mov dx, %1
    add dx, 0Eh                     ; Command register offset
    mov ax, (1 << 11) | %2          ; Select window command
    out dx, ax
    ; Small delay for window switch
    mov ax, 3
%%delay_loop:
    dec ax
    jnz %%delay_loop
%endmacro

; 3C515-TX window selection macro
; Parameters: %1 = io_base (register), %2 = window (immediate)
; Modifies: AX, DX (if io_base is not DX)
%macro SELECT_3C515_WINDOW 2
    mov dx, %1
    add dx, 0Eh                     ; Command register offset
    mov ax, (1 << 11) | %2          ; Select window command
    out dx, ax
    ; Small delay for window switch
    mov ax, 3
%%delay_loop:
    dec ax
    jnz %%delay_loop
%endmacro

; Generic window selection macro
; Parameters: %1 = io_base (register), %2 = window (immediate), %3 = hw_type (register or immediate)
; Modifies: AX, DX, BL (preserved)
%macro SELECT_WINDOW 3
    push bx
    mov bl, %3                      ; Load hw_type into register (works for both register and immediate)
    cmp bl, HW_TYPE_3C509B
    je %%select_3c509b
    cmp bl, HW_TYPE_3C515TX
    je %%select_3c515tx
    jmp %%done                      ; Unknown type, skip

%%select_3c509b:
    SELECT_3C509B_WINDOW %1, %2
    jmp %%done

%%select_3c515tx:
    SELECT_3C515_WINDOW %1, %2

%%done:
    pop bx
%endmacro

;=============================================================================
; HARDWARE ACCESS PATTERN MACROS
;=============================================================================

; Safe I/O byte read with timeout
; Parameters: %1 = port (register), %2 = timeout_count (immediate), %3 = result_reg (register)
; Sets CY on timeout, clears CY on success
; Result in result_reg if successful
%macro SAFE_IO_READ_BYTE 3
    push cx
    mov cx, %2
    mov dx, %1

%%read_loop:
    in al, dx
    test al, al                     ; Basic validity check
    jz %%read_success              ; Zero is usually invalid
    dec cx
    jnz %%read_loop

    ; Timeout occurred
    stc
    jmp %%read_done

%%read_success:
    mov %3, al
    clc

%%read_done:
    pop cx
%endmacro

; Safe I/O word read with timeout
; Parameters: %1 = port (register), %2 = timeout_count (immediate), %3 = result_reg (register)
; Sets CY on timeout, clears CY on success
%macro SAFE_IO_READ_WORD 3
    push cx
    mov cx, %2
    mov dx, %1

%%read_loop:
    in ax, dx
    dec cx
    jnz %%read_success             ; Assume word reads are reliable

    ; Timeout (shouldn't happen for word reads, but defensive)
    stc
    jmp %%read_done

%%read_success:
    mov %3, ax
    clc

%%read_done:
    pop cx
%endmacro

; Safe I/O byte write with verification
; Parameters: %1 = port (register), %2 = value (register), %3 = timeout_count (immediate)
; Sets CY on verification failure, clears CY on success
%macro SAFE_IO_WRITE_BYTE 3
    push ax
    push cx
    push dx

    mov dx, %1
    mov al, %2
    out dx, al

    ; Verify write if possible (some registers are write-only)
    mov cx, %3
%%verify_loop:
    in al, dx
    cmp al, %2
    je %%write_success
    dec cx
    jnz %%verify_loop

    ; Verification failed or timeout
    stc
    jmp %%write_done

%%write_success:
    clc

%%write_done:
    pop dx
    pop cx
    pop ax
%endmacro

; Hardware reset with timeout
; Parameters: %1 = io_base (register), %2 = hw_type (register), %3 = timeout_ms (immediate)
; Sets CY on timeout, clears CY on success
%macro HARDWARE_RESET 3
    push ax
    push cx
    push dx

    ; Select window 0 for reset command
    SELECT_WINDOW %1, 0, %2

    ; Issue total reset command
    mov dx, %1
    add dx, 0Eh                     ; Command register
    mov ax, 0                       ; Total reset command
    out dx, ax

    ; Wait for reset completion
    mov cx, %3
%%reset_wait:
    call delay_1ms                  ; 1ms delay

    ; Check if reset completed (read status register)
    in ax, dx
    test ax, 1000h                  ; Command in progress bit
    jz %%reset_complete

    dec cx
    jnz %%reset_wait

    ; Reset timeout
    stc
    jmp %%reset_done

%%reset_complete:
    clc

%%reset_done:
    pop dx
    pop cx
    pop ax
%endmacro

;=============================================================================
; DEFENSIVE PROGRAMMING PATTERNS
;=============================================================================

; Validate hardware instance index
; Parameters: %1 = instance_idx (register), %2 = max_instances (immediate)
; Sets CY if invalid, clears CY if valid
; Preserves all registers except flags
%macro VALIDATE_HW_INSTANCE 2
    cmp %1, %2
    jae %%invalid_instance
    clc                             ; Valid instance
    jmp %%instance_done
%%invalid_instance:
    stc                             ; Invalid instance
%%instance_done:
%endmacro

; Validate I/O address range
; Parameters: %1 = io_addr (register), %2 = min_addr (immediate), %3 = max_addr (immediate)
; Sets CY if invalid, clears CY if valid
%macro VALIDATE_IO_ADDRESS 3
    cmp %1, %2
    jb %%invalid_addr
    cmp %1, %3
    ja %%invalid_addr
    clc                             ; Valid address
    jmp %%addr_done
%%invalid_addr:
    stc                             ; Invalid address
%%addr_done:
%endmacro

; Validate IRQ number
; Parameters: %1 = irq_num (register)
; Sets CY if invalid, clears CY if valid
%macro VALIDATE_IRQ 1
    cmp %1, IRQ_AUTO_ASSIGN
    je %%valid_irq                  ; Auto-assign is valid
    cmp %1, IRQ_MIN
    jb %%invalid_irq
    cmp %1, IRQ_MAX
    ja %%invalid_irq
%%valid_irq:
    clc                             ; Valid IRQ
    jmp %%irq_done
%%invalid_irq:
    stc                             ; Invalid IRQ
%%irq_done:
%endmacro

; Hardware operation with retry
; Parameters: %1 = operation_proc (label), %2 = max_retries (immediate), %3 = delay_ms (immediate)
; Sets CY if all retries failed, clears CY on success
%macro RETRY_HARDWARE_OPERATION 3
    push cx
    mov cx, %2

%%retry_loop:
    call %1
    jnc %%operation_success

    ; Operation failed, wait and retry
    push cx
    mov cx, %3
%%delay_loop:
    call delay_1ms
    dec cx
    jnz %%delay_loop
    pop cx

    dec cx
    jnz %%retry_loop

    ; All retries failed
    stc
    jmp %%retry_done

%%operation_success:
    clc

%%retry_done:
    pop cx
%endmacro

;=============================================================================
; DATA STRUCTURE DEFINITIONS
;=============================================================================

; Hardware instance descriptor
struc HW_INSTANCE
    .type:              resb 1      ; Hardware type (HW_TYPE_*)
    .state:             resb 1      ; Current state (HW_STATE_*)
    .io_base:           resw 1      ; I/O base address
    .io_range:          resw 1      ; I/O address range size
    .irq:               resb 1      ; IRQ number
    .mac_address:       resb 6      ; MAC address
    .pnp_csn:           resb 1      ; PnP card select number
    .error_count:       resw 1      ; Error counter
    .last_error:        resb 1      ; Last error code
    .flags:             resb 1      ; Status flags
    .reserved:          resb 6      ; Reserved for future use
endstruc

; PnP device information structure
struc PNP_DEVICE_INFO
    .vendor_id:         resw 1      ; Vendor ID
    .device_id:         resw 1      ; Device ID
    .serial_id:         resd 1      ; Serial number
    .logical_device:    resb 1      ; Logical device number
    .csn:               resb 1      ; Card select number
    .io_base:           resw 1      ; Assigned I/O base
    .irq:               resb 1      ; Assigned IRQ
    .state:             resb 1      ; Device state
    .reserved:          resb 4      ; Reserved
endstruc

; IRQ handler context structure
struc IRQ_CONTEXT
    .handler_offset:    resw 1      ; Handler offset
    .handler_segment:   resw 1      ; Handler segment
    .device_index:      resb 1      ; Associated device index
    .irq_number:        resb 1      ; IRQ number
    .call_count:        resd 1      ; Number of calls
    .error_count:       resw 1      ; Error count
    .flags:             resb 1      ; Handler flags
    .reserved:          resb 1      ; Reserved
endstruc

;=============================================================================
; FUNCTION INTERFACE DECLARATIONS
; Note: Skip extern declarations when included from implementation modules
;=============================================================================

%ifndef HARDWARE_MODULE_DEFINING
; Core hardware management functions
extern hardware_init_system            ; Initialize hardware subsystem
extern hardware_shutdown_system        ; Shutdown hardware subsystem
extern hardware_detect_all_devices     ; Detect all supported devices
extern hardware_reset_all_devices      ; Reset all devices

; 3C509B-specific functions
extern detect_3c509b_device            ; Detect 3C509B device
extern configure_3c509b_device         ; Configure 3C509B device
extern activate_3c509b_device          ; Activate 3C509B device
extern reset_3c509b_device             ; Reset 3C509B device
extern read_3c509b_eeprom              ; Read 3C509B EEPROM
extern setup_3c509b_irq                ; Setup 3C509B IRQ handler

; 3C515-TX-specific functions
extern detect_3c515_device             ; Detect 3C515-TX device
extern configure_3c515_device          ; Configure 3C515-TX device
extern activate_3c515_device           ; Activate 3C515-TX device
extern reset_3c515_device              ; Reset 3C515-TX device
extern read_3c515_eeprom               ; Read 3C515-TX EEPROM
extern setup_3c515_irq                 ; Setup 3C515-TX IRQ handler
extern configure_3c515_dma             ; Configure 3C515-TX DMA
%endif ; HARDWARE_MODULE_DEFINING

%ifndef PNP_MODULE_DEFINING
; PnP management functions
extern pnp_init_system                 ; Initialize PnP system
extern pnp_shutdown_system             ; Shutdown PnP system
extern pnp_detect_devices              ; Detect PnP devices
extern pnp_configure_device            ; Configure PnP device
extern pnp_activate_device             ; Activate PnP device
extern pnp_generate_lfsr_sequence      ; Generate LFSR sequence
%endif ; PNP_MODULE_DEFINING

%ifndef IRQ_MODULE_DEFINING
; IRQ management functions
extern irq_install_handler             ; Install IRQ handler
extern irq_remove_handler              ; Remove IRQ handler
extern irq_chain_handler               ; Chain to previous handler
extern irq_dispatch                    ; Dispatch IRQ to devices
extern irq_enable_line                 ; Enable IRQ line
extern irq_disable_line                ; Disable IRQ line
%endif ; IRQ_MODULE_DEFINING

; Utility functions (commonly needed)
extern delay_1ms                       ; 1 millisecond delay
extern delay_10ms                      ; 10 millisecond delay
extern validate_io_address             ; Validate I/O address
extern validate_irq_number             ; Validate IRQ number
extern get_system_timestamp            ; Get system timestamp

;=============================================================================
; EXPORTED INTERFACE FUNCTIONS
;=============================================================================

; Main interface functions that other modules can call
global hardware_detect_and_configure   ; Detect and configure all hardware
global hardware_get_device_info        ; Get device information
global hardware_set_device_state       ; Set device state
global hardware_handle_interrupt       ; Handle hardware interrupt
global hardware_validate_configuration ; Validate hardware configuration

; PnP interface functions
global pnp_enumerate_devices           ; Enumerate PnP devices
global pnp_get_device_resources        ; Get device resource requirements
global pnp_assign_resources            ; Assign resources to device
global pnp_get_lfsr_table              ; Get LFSR lookup table

; IRQ interface functions
global irq_setup_shared_handler        ; Setup shared IRQ handler
global irq_get_handler_info            ; Get IRQ handler information
global irq_update_statistics           ; Update IRQ statistics

;=============================================================================
; GLOBAL DATA DECLARATIONS
;=============================================================================

%ifndef HARDWARE_DATA_DEFINING
; Hardware instance table
extern hw_instance_table               ; Array of hardware instances
extern hw_instance_count               ; Number of detected instances

; PnP device table
extern pnp_device_table                ; Array of PnP devices
extern pnp_device_count                ; Number of PnP devices

; IRQ handler table
extern irq_handler_table               ; Array of IRQ handlers
extern irq_handler_count               ; Number of installed handlers

; LFSR lookup table (256 bytes for PnP isolation)
extern lfsr_lookup_table               ; LFSR sequence lookup table

; System state variables
extern hardware_initialized            ; Hardware subsystem initialized flag
extern pnp_available                   ; PnP system available flag
extern last_error_code                 ; Last error code
extern debug_flags                     ; Debug control flags
%endif ; HARDWARE_DATA_DEFINING

%endif ; ASM_INTERFACES_INC
