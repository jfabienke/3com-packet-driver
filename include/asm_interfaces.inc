; @file asm_interfaces.inc
; @brief Assembly interface definitions for hardware detection and management
;
; 3Com Packet Driver - Assembly interfaces for Groups 6A & 6B implementation
; Provides clean interfaces for hardware detection, IRQ management, and PnP operations
;
; This file is part of the 3Com Packet Driver project.

ifndef ASM_INTERFACES_INC
ASM_INTERFACES_INC equ 1

; Include defensive programming patterns
include "tsr_defensive.inc"

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
; Parameters: io_base (register), window (immediate)
; Modifies: AX, DX (if io_base is not DX)
macro SELECT_3C509B_WINDOW io_base, window
    mov dx, io_base
    add dx, 0Eh                     ; Command register offset
    mov ax, (1 << 11) | window      ; Select window command
    out dx, ax
    ; Small delay for window switch
    mov ax, 3
%%delay_loop:
    dec ax
    jnz %%delay_loop
endm

; 3C515-TX window selection macro
; Parameters: io_base (register), window (immediate)
; Modifies: AX, DX (if io_base is not DX)
macro SELECT_3C515_WINDOW io_base, window
    mov dx, io_base
    add dx, 0Eh                     ; Command register offset
    mov ax, (1 << 11) | window      ; Select window command
    out dx, ax
    ; Small delay for window switch
    mov ax, 3
%%delay_loop:
    dec ax
    jnz %%delay_loop
endm

; Generic window selection macro
; Parameters: io_base (register), window (immediate), hw_type (register)
; Modifies: AX, DX
macro SELECT_WINDOW io_base, window, hw_type
    cmp hw_type, HW_TYPE_3C509B
    je %%select_3c509b
    cmp hw_type, HW_TYPE_3C515TX
    je %%select_3c515tx
    jmp %%done                      ; Unknown type, skip

%%select_3c509b:
    SELECT_3C509B_WINDOW io_base, window
    jmp %%done

%%select_3c515tx:
    SELECT_3C515_WINDOW io_base, window

%%done:
endm

;=============================================================================
; HARDWARE ACCESS PATTERN MACROS
;=============================================================================

; Safe I/O byte read with timeout
; Parameters: port (register), timeout_count (immediate), result_reg (register)
; Sets CY on timeout, clears CY on success
; Result in result_reg if successful
macro SAFE_IO_READ_BYTE port, timeout_count, result_reg
    push cx
    mov cx, timeout_count
    mov dx, port

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
    mov result_reg, al
    clc

%%read_done:
    pop cx
endm

; Safe I/O word read with timeout
; Parameters: port (register), timeout_count (immediate), result_reg (register)
; Sets CY on timeout, clears CY on success
macro SAFE_IO_READ_WORD port, timeout_count, result_reg
    push cx
    mov cx, timeout_count
    mov dx, port

%%read_loop:
    in ax, dx
    dec cx
    jnz %%read_success             ; Assume word reads are reliable
    
    ; Timeout (shouldn't happen for word reads, but defensive)
    stc
    jmp %%read_done

%%read_success:
    mov result_reg, ax
    clc

%%read_done:
    pop cx
endm

; Safe I/O byte write with verification
; Parameters: port (register), value (register), timeout_count (immediate)
; Sets CY on verification failure, clears CY on success
macro SAFE_IO_WRITE_BYTE port, value, timeout_count
    push ax
    push cx
    push dx
    
    mov dx, port
    mov al, value
    out dx, al
    
    ; Verify write if possible (some registers are write-only)
    mov cx, timeout_count
%%verify_loop:
    in al, dx
    cmp al, value
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
endm

; Hardware reset with timeout
; Parameters: io_base (register), hw_type (register), timeout_ms (immediate)
; Sets CY on timeout, clears CY on success
macro HARDWARE_RESET io_base, hw_type, timeout_ms
    push ax
    push cx
    push dx
    
    ; Select window 0 for reset command
    SELECT_WINDOW io_base, 0, hw_type
    
    ; Issue total reset command
    mov dx, io_base
    add dx, 0Eh                     ; Command register
    mov ax, 0                       ; Total reset command
    out dx, ax
    
    ; Wait for reset completion
    mov cx, timeout_ms
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
endm

;=============================================================================
; DEFENSIVE PROGRAMMING PATTERNS
;=============================================================================

; Validate hardware instance index
; Parameters: instance_idx (register), max_instances (immediate)
; Sets CY if invalid, clears CY if valid
; Preserves all registers except flags
macro VALIDATE_HW_INSTANCE instance_idx, max_instances
    cmp instance_idx, max_instances
    jae %%invalid_instance
    clc                             ; Valid instance
    jmp %%instance_done
%%invalid_instance:
    stc                             ; Invalid instance
%%instance_done:
endm

; Validate I/O address range
; Parameters: io_addr (register), min_addr (immediate), max_addr (immediate)
; Sets CY if invalid, clears CY if valid
macro VALIDATE_IO_ADDRESS io_addr, min_addr, max_addr
    cmp io_addr, min_addr
    jb %%invalid_addr
    cmp io_addr, max_addr
    ja %%invalid_addr
    clc                             ; Valid address
    jmp %%addr_done
%%invalid_addr:
    stc                             ; Invalid address
%%addr_done:
endm

; Validate IRQ number
; Parameters: irq_num (register)
; Sets CY if invalid, clears CY if valid
macro VALIDATE_IRQ irq_num
    cmp irq_num, IRQ_AUTO_ASSIGN
    je %%valid_irq                  ; Auto-assign is valid
    cmp irq_num, IRQ_MIN
    jb %%invalid_irq
    cmp irq_num, IRQ_MAX
    ja %%invalid_irq
%%valid_irq:
    clc                             ; Valid IRQ
    jmp %%irq_done
%%invalid_irq:
    stc                             ; Invalid IRQ
%%irq_done:
endm

; Hardware operation with retry
; Parameters: operation_proc (label), max_retries (immediate), delay_ms (immediate)
; Sets CY if all retries failed, clears CY on success
macro RETRY_HARDWARE_OPERATION operation_proc, max_retries, delay_ms
    push cx
    mov cx, max_retries

%%retry_loop:
    call operation_proc
    jnc %%operation_success
    
    ; Operation failed, wait and retry
    push cx
    mov cx, delay_ms
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
endm

;=============================================================================
; DATA STRUCTURE DEFINITIONS
;=============================================================================

; Hardware instance descriptor
struc HW_INSTANCE
    .type               db ?        ; Hardware type (HW_TYPE_*)
    .state              db ?        ; Current state (HW_STATE_*)
    .io_base            dw ?        ; I/O base address
    .io_range           dw ?        ; I/O address range size
    .irq                db ?        ; IRQ number
    .mac_address        db 6 dup(?) ; MAC address
    .pnp_csn            db ?        ; PnP card select number
    .error_count        dw ?        ; Error counter
    .last_error         db ?        ; Last error code
    .flags              db ?        ; Status flags
    .reserved           db 6 dup(?) ; Reserved for future use
endstruc

; PnP device information structure
struc PNP_DEVICE_INFO
    .vendor_id          dw ?        ; Vendor ID
    .device_id          dw ?        ; Device ID
    .serial_id          dd ?        ; Serial number
    .logical_device     db ?        ; Logical device number
    .csn                db ?        ; Card select number
    .io_base            dw ?        ; Assigned I/O base
    .irq                db ?        ; Assigned IRQ
    .state              db ?        ; Device state
    .reserved           db 4 dup(?) ; Reserved
endstruc

; IRQ handler context structure
struc IRQ_CONTEXT
    .handler_offset     dw ?        ; Handler offset
    .handler_segment    dw ?        ; Handler segment
    .device_index       db ?        ; Associated device index
    .irq_number         db ?        ; IRQ number
    .call_count         dd ?        ; Number of calls
    .error_count        dw ?        ; Error count
    .flags              db ?        ; Handler flags
    .reserved           db ?        ; Reserved
endstruc

;=============================================================================
; FUNCTION INTERFACE DECLARATIONS
;=============================================================================

; Core hardware management functions
EXTERN hardware_init_system:PROC           ; Initialize hardware subsystem
EXTERN hardware_shutdown_system:PROC       ; Shutdown hardware subsystem
EXTERN hardware_detect_all_devices:PROC    ; Detect all supported devices
EXTERN hardware_reset_all_devices:PROC     ; Reset all devices

; 3C509B-specific functions
EXTERN detect_3c509b_device:PROC           ; Detect 3C509B device
EXTERN configure_3c509b_device:PROC        ; Configure 3C509B device
EXTERN activate_3c509b_device:PROC         ; Activate 3C509B device
EXTERN reset_3c509b_device:PROC            ; Reset 3C509B device
EXTERN read_3c509b_eeprom:PROC             ; Read 3C509B EEPROM
EXTERN setup_3c509b_irq:PROC               ; Setup 3C509B IRQ handler

; 3C515-TX-specific functions  
EXTERN detect_3c515_device:PROC            ; Detect 3C515-TX device
EXTERN configure_3c515_device:PROC         ; Configure 3C515-TX device
EXTERN activate_3c515_device:PROC          ; Activate 3C515-TX device
EXTERN reset_3c515_device:PROC             ; Reset 3C515-TX device
EXTERN read_3c515_eeprom:PROC              ; Read 3C515-TX EEPROM
EXTERN setup_3c515_irq:PROC                ; Setup 3C515-TX IRQ handler
EXTERN configure_3c515_dma:PROC            ; Configure 3C515-TX DMA

; PnP management functions
EXTERN pnp_init_system:PROC                ; Initialize PnP system
EXTERN pnp_shutdown_system:PROC            ; Shutdown PnP system
EXTERN pnp_detect_devices:PROC             ; Detect PnP devices
EXTERN pnp_configure_device:PROC           ; Configure PnP device
EXTERN pnp_activate_device:PROC            ; Activate PnP device
EXTERN pnp_generate_lfsr_sequence:PROC     ; Generate LFSR sequence

; IRQ management functions
EXTERN irq_install_handler:PROC            ; Install IRQ handler
EXTERN irq_remove_handler:PROC             ; Remove IRQ handler
EXTERN irq_chain_handler:PROC              ; Chain to previous handler
EXTERN irq_dispatch:PROC                   ; Dispatch IRQ to devices
EXTERN irq_enable_line:PROC                ; Enable IRQ line
EXTERN irq_disable_line:PROC               ; Disable IRQ line

; Utility functions
EXTERN delay_1ms:PROC                      ; 1 millisecond delay
EXTERN delay_10ms:PROC                     ; 10 millisecond delay
EXTERN validate_io_address:PROC            ; Validate I/O address
EXTERN validate_irq_number:PROC            ; Validate IRQ number
EXTERN get_system_timestamp:PROC           ; Get system timestamp

;=============================================================================
; EXPORTED INTERFACE FUNCTIONS
;=============================================================================

; Main interface functions that other modules can call
PUBLIC hardware_detect_and_configure       ; Detect and configure all hardware
PUBLIC hardware_get_device_info            ; Get device information
PUBLIC hardware_set_device_state           ; Set device state
PUBLIC hardware_handle_interrupt           ; Handle hardware interrupt
PUBLIC hardware_validate_configuration     ; Validate hardware configuration

; PnP interface functions
PUBLIC pnp_enumerate_devices               ; Enumerate PnP devices
PUBLIC pnp_get_device_resources            ; Get device resource requirements
PUBLIC pnp_assign_resources                ; Assign resources to device
PUBLIC pnp_get_lfsr_table                  ; Get LFSR lookup table

; IRQ interface functions
PUBLIC irq_setup_shared_handler            ; Setup shared IRQ handler
PUBLIC irq_get_handler_info                ; Get IRQ handler information
PUBLIC irq_update_statistics               ; Update IRQ statistics

;=============================================================================
; GLOBAL DATA DECLARATIONS
;=============================================================================

; Hardware instance table
EXTERN hw_instance_table:HW_INSTANCE       ; Array of hardware instances
EXTERN hw_instance_count:BYTE              ; Number of detected instances

; PnP device table
EXTERN pnp_device_table:PNP_DEVICE_INFO    ; Array of PnP devices
EXTERN pnp_device_count:BYTE               ; Number of PnP devices

; IRQ handler table
EXTERN irq_handler_table:IRQ_CONTEXT       ; Array of IRQ handlers
EXTERN irq_handler_count:BYTE              ; Number of installed handlers

; LFSR lookup table (256 bytes for PnP isolation)
EXTERN lfsr_lookup_table:BYTE              ; LFSR sequence lookup table

; System state variables
EXTERN hardware_initialized:BYTE           ; Hardware subsystem initialized flag
EXTERN pnp_available:BYTE                  ; PnP system available flag
EXTERN last_error_code:BYTE               ; Last error code
EXTERN debug_flags:BYTE                   ; Debug control flags

endif ; ASM_INTERFACES_INC