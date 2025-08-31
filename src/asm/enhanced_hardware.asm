; @file enhanced_hardware.asm
; @brief Enhanced hardware interface implementations for Groups 6A & 6B
;
; 3Com Packet Driver - Enhanced hardware detection and IRQ management
; Implements the assembly interfaces defined in asm_interfaces.inc
;
; This file is part of the 3Com Packet Driver project.

.MODEL SMALL
.386

; Include interface definitions
include "asm_interfaces.inc"

; Code segment
_TEXT SEGMENT
        ASSUME  CS:_TEXT, DS:_DATA

;=============================================================================
; ENHANCED HARDWARE DETECTION AND CONFIGURATION
;=============================================================================

;-----------------------------------------------------------------------------
; hardware_detect_and_configure - Main entry point for hardware setup
;
; Input:  None
; Output: AX = number of devices detected and configured
;         CY = set if no devices found or configuration failed
; Uses:   All registers
;-----------------------------------------------------------------------------
hardware_detect_and_configure PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ENTER_CRITICAL_REENTRANT
        
        ; Initialize hardware subsystem
        call    hardware_init_system
        cmp     ax, HW_SUCCESS
        jne     .init_failed
        
        ; Detect all devices
        call    hardware_detect_all_devices  
        cmp     ax, 0
        je      .no_devices_found
        mov     bx, ax                      ; Save device count
        
        ; Configure detected devices
        mov     cx, 0                       ; Configured device counter
        mov     si, 0                       ; Device index
        
.configure_loop:
        cmp     si, bx
        jae     .done_configuring
        
        ; Get device type for this instance
        mov     ax, si
        call    hardware_get_device_type
        cmp     ax, HW_TYPE_UNKNOWN
        je      .next_device
        
        ; Configure based on device type
        cmp     ax, HW_TYPE_3C509B
        je      .configure_3c509b
        cmp     ax, HW_TYPE_3C515TX
        je      .configure_3c515tx
        jmp     .next_device
        
.configure_3c509b:
        mov     al, sil                     ; Device index
        call    configure_3c509b_device
        cmp     ax, HW_SUCCESS
        jne     .next_device
        inc     cx                          ; Increment configured count
        jmp     .next_device
        
.configure_3c515tx:
        mov     al, sil                     ; Device index  
        call    configure_3c515_device
        cmp     ax, HW_SUCCESS
        jne     .next_device
        inc     cx                          ; Increment configured count
        
.next_device:
        inc     si
        jmp     .configure_loop
        
.done_configuring:
        ; Validate final configuration
        call    hardware_validate_configuration
        cmp     ax, HW_SUCCESS
        jne     .config_invalid
        
        mov     ax, cx                      ; Return configured device count
        clc                                 ; Success
        jmp     .exit
        
.init_failed:
        mov     [last_error_code], al
        mov     ax, 0
        stc
        jmp     .exit
        
.no_devices_found:
        mov     [last_error_code], HW_ERROR_NO_DEVICE
        mov     ax, 0
        stc
        jmp     .exit
        
.config_invalid:
        mov     [last_error_code], HW_ERROR_CONFIG_INVALID
        mov     ax, 0
        stc
        jmp     .exit

.exit:
        EXIT_CRITICAL_REENTRANT
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
hardware_detect_and_configure ENDP

;-----------------------------------------------------------------------------
; hardware_get_device_info - Get information about a hardware device
;
; Input:  AL = device index
;         ES:DI = buffer for device information
; Output: AX = HW_SUCCESS or error code
;         Buffer filled with device information
; Uses:   AX, BX, CX, SI, DI
;-----------------------------------------------------------------------------
hardware_get_device_info PROC
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
        mov     ax, bx
        mov     cx, SIZE HW_INSTANCE
        mul     cx
        mov     si, ax
        add     si, OFFSET hw_instance_table
        
        ; Copy structure to user buffer
        mov     cx, SIZE HW_INSTANCE
        rep     movsb
        
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
hardware_get_device_info ENDP

;-----------------------------------------------------------------------------
; hardware_set_device_state - Set device state
;
; Input:  AL = device index
;         AH = new state (HW_STATE_*)
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, CX, SI
;-----------------------------------------------------------------------------
hardware_set_device_state PROC
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
        mov     cx, SIZE HW_INSTANCE
        mul     cx
        mov     si, ax
        add     si, OFFSET hw_instance_table
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
hardware_set_device_state ENDP

;-----------------------------------------------------------------------------
; hardware_handle_interrupt - Main interrupt dispatcher
;
; Input:  None (called from IRQ handler)
; Output: AX = HW_SUCCESS if handled, error code if not
; Uses:   All registers (interrupt context)
;-----------------------------------------------------------------------------
hardware_handle_interrupt PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        
        ; Check each active device for interrupt status
        mov     cx, [hw_instance_count]
        cmp     cx, 0
        je      .no_devices
        
        mov     si, 0                       ; Device index
        
.check_devices_loop:
        cmp     si, cx
        jae     .not_handled
        
        ; Get device instance
        push    cx
        push    si
        mov     ax, si
        mov     cx, SIZE HW_INSTANCE
        mul     cx
        mov     di, ax
        add     di, OFFSET hw_instance_table
        pop     si
        pop     cx
        
        ; Check if device is active
        cmp     [di + HW_INSTANCE.state], HW_STATE_ACTIVE
        jne     .next_device_check
        
        ; Dispatch based on device type
        mov     al, [di + HW_INSTANCE.type]
        cmp     al, HW_TYPE_3C509B
        je      .handle_3c509b_irq
        cmp     al, HW_TYPE_3C515TX
        je      .handle_3c515tx_irq
        jmp     .next_device_check
        
.handle_3c509b_irq:
        ; Check if 3C509B caused the interrupt
        mov     dx, [di + HW_INSTANCE.io_base]
        call    check_3c509b_interrupt
        cmp     ax, 1
        je      .handled_3c509b
        jmp     .next_device_check
        
.handled_3c509b:
        ; Handle 3C509B interrupt
        call    hardware_handle_3c509b_irq
        mov     ax, HW_SUCCESS
        jmp     .exit
        
.handle_3c515tx_irq:
        ; Check if 3C515-TX caused the interrupt
        mov     dx, [di + HW_INSTANCE.io_base]
        call    check_3c515_interrupt
        cmp     ax, 1
        je      .handled_3c515tx
        jmp     .next_device_check
        
.handled_3c515tx:
        ; Handle 3C515-TX interrupt
        call    hardware_handle_3c515_irq
        mov     ax, HW_SUCCESS
        jmp     .exit
        
.next_device_check:
        inc     si
        jmp     .check_devices_loop
        
.no_devices:
        mov     ax, HW_ERROR_NO_DEVICE
        jmp     .exit
        
.not_handled:
        mov     ax, HW_ERROR_GENERIC
        
.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
hardware_handle_interrupt ENDP

;-----------------------------------------------------------------------------
; hardware_validate_configuration - Validate hardware configuration
;
; Input:  None
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
hardware_validate_configuration PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Check if any devices are configured
        cmp     [hw_instance_count], 0
        je      .no_devices
        
        ; Validate each configured device
        mov     cx, [hw_instance_count]
        mov     si, 0                       ; Device index
        
.validate_loop:
        cmp     si, cx
        jae     .validation_complete
        
        ; Get device instance
        push    cx
        push    si
        mov     ax, si
        mov     cx, SIZE HW_INSTANCE
        mul     cx
        mov     di, ax
        add     di, OFFSET hw_instance_table
        pop     si
        pop     cx
        
        ; Skip undetected devices
        cmp     [di + HW_INSTANCE.state], HW_STATE_UNDETECTED
        je      .next_validation
        
        ; Validate I/O address range
        mov     ax, [di + HW_INSTANCE.io_base]
        VALIDATE_IO_ADDRESS ax, ISA_IO_MIN, EISA_IO_MAX
        jc      .invalid_io
        
        ; Validate IRQ
        mov     al, [di + HW_INSTANCE.irq]
        VALIDATE_IRQ al
        jc      .invalid_irq
        
        ; Check for I/O address conflicts
        call    check_io_conflicts
        cmp     ax, 1
        je      .io_conflict
        
        ; Check for IRQ conflicts  
        call    check_irq_conflicts
        cmp     ax, 1
        je      .irq_conflict
        
.next_validation:
        inc     si
        jmp     .validate_loop
        
.validation_complete:
        mov     ax, HW_SUCCESS
        jmp     .exit
        
.no_devices:
        mov     ax, HW_ERROR_NO_DEVICE
        jmp     .exit
        
.invalid_io:
        mov     ax, HW_ERROR_IO_CONFLICT
        jmp     .exit
        
.invalid_irq:
        mov     ax, HW_ERROR_IRQ_CONFLICT
        jmp     .exit
        
.io_conflict:
        mov     ax, HW_ERROR_IO_CONFLICT
        jmp     .exit
        
.irq_conflict:
        mov     ax, HW_ERROR_IRQ_CONFLICT
        
.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
hardware_validate_configuration ENDP

;=============================================================================
; DEVICE-SPECIFIC DETECTION FUNCTIONS
;=============================================================================

;-----------------------------------------------------------------------------
; detect_3c509b_device - Enhanced 3C509B detection with defensive programming
;
; Input:  None
; Output: AX = I/O base address if found, 0 if not found
;         CY = set if detection failed
; Uses:   All registers
;-----------------------------------------------------------------------------
detect_3c509b_device PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Try standard I/O addresses with defensive timeouts
        mov     si, OFFSET c509b_io_addresses
        mov     cx, c509b_io_count
        
.probe_loop:
        mov     dx, [si]                    ; Get I/O address to try
        
        ; Validate I/O address range
        VALIDATE_IO_ADDRESS dx, ISA_IO_MIN, ISA_IO_MAX
        jc      .next_address
        
        ; Attempt device detection with timeout
        RETRY_HARDWARE_OPERATION probe_3c509b_at_address, 3, 100
        jnc     .device_found
        
.next_address:
        add     si, 2                       ; Next address
        dec     cx
        jnz     .probe_loop
        
        ; Device not found
        mov     ax, 0
        stc
        jmp     .exit
        
.device_found:
        mov     ax, dx                      ; Return I/O base address
        clc
        
.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret

; Standard 3C509B I/O addresses to try
c509b_io_addresses dw 300h, 310h, 320h, 330h, 340h, 350h, 360h, 370h
c509b_io_count     equ ($ - c509b_io_addresses) / 2

detect_3c509b_device ENDP

;-----------------------------------------------------------------------------
; detect_3c515_device - Enhanced 3C515-TX detection
;
; Input:  None  
; Output: AX = I/O base address if found, 0 if not found
;         CY = set if detection failed
; Uses:   All registers
;-----------------------------------------------------------------------------
detect_3c515_device PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Try EISA addresses first (more likely for 3C515-TX)
        mov     si, OFFSET c515_io_addresses
        mov     cx, c515_io_count
        
.probe_loop:
        mov     dx, [si]                    ; Get I/O address to try
        
        ; Validate I/O address range  
        VALIDATE_IO_ADDRESS dx, EISA_IO_MIN, EISA_IO_MAX
        jc      .try_isa_range
        
        ; Attempt device detection with timeout
        RETRY_HARDWARE_OPERATION probe_3c515_at_address, 3, 100
        jnc     .device_found
        
.try_isa_range:
        ; Also try ISA range for combo cards
        VALIDATE_IO_ADDRESS dx, ISA_IO_MIN, ISA_IO_MAX  
        jc      .next_address
        
        RETRY_HARDWARE_OPERATION probe_3c515_at_address, 3, 100
        jnc     .device_found
        
.next_address:
        add     si, 2                       ; Next address
        dec     cx
        jnz     .probe_loop
        
        ; Device not found
        mov     ax, 0
        stc
        jmp     .exit
        
.device_found:
        mov     ax, dx                      ; Return I/O base address
        clc
        
.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret

; Standard 3C515-TX I/O addresses to try
c515_io_addresses dw 1000h, 2000h, 3000h, 4000h, 5000h, 6000h, 7000h, 8000h
c515_io_count     equ ($ - c515_io_addresses) / 2

detect_3c515_device ENDP

;=============================================================================
; UTILITY FUNCTIONS
;=============================================================================

;-----------------------------------------------------------------------------
; hardware_get_device_type - Get device type for given index
;
; Input:  AX = device index
; Output: AX = device type (HW_TYPE_*)
; Uses:   AX, BX, CX
;-----------------------------------------------------------------------------
hardware_get_device_type PROC
        push    bx
        push    cx
        
        ; Validate index
        cmp     ax, MAX_HW_INSTANCES
        jae     .invalid_index
        
        ; Get device type from instance table
        mov     bx, ax
        mov     cx, SIZE HW_INSTANCE
        mul     cx
        mov     bx, ax
        add     bx, OFFSET hw_instance_table
        mov     al, [bx + HW_INSTANCE.type]
        mov     ah, 0
        jmp     .exit
        
.invalid_index:
        mov     ax, HW_TYPE_UNKNOWN
        
.exit:
        pop     cx
        pop     bx
        ret
hardware_get_device_type ENDP

;-----------------------------------------------------------------------------
; check_3c509b_interrupt - Check if 3C509B caused interrupt
;
; Input:  DX = I/O base address
; Output: AX = 1 if interrupt detected, 0 otherwise
; Uses:   AX, DX
;-----------------------------------------------------------------------------
check_3c509b_interrupt PROC
        push    dx
        
        ; Select window 1 for status register access
        add     dx, 0Eh                     ; Command register
        mov     ax, (1 << 11) | 1           ; Select window 1
        out     dx, ax
        
        ; Read status register
        in      ax, dx
        test    ax, 0001h                   ; Check interrupt latch bit
        jz      .no_interrupt
        
        mov     ax, 1                       ; Interrupt detected
        jmp     .exit
        
.no_interrupt:
        mov     ax, 0                       ; No interrupt
        
.exit:
        pop     dx
        ret
check_3c509b_interrupt ENDP

;-----------------------------------------------------------------------------
; check_3c515_interrupt - Check if 3C515-TX caused interrupt
;
; Input:  DX = I/O base address
; Output: AX = 1 if interrupt detected, 0 otherwise
; Uses:   AX, DX
;-----------------------------------------------------------------------------
check_3c515_interrupt PROC
        push    dx
        
        ; Select window 1 for status register access
        add     dx, 0Eh                     ; Command register
        mov     ax, (1 << 11) | 1           ; Select window 1
        out     dx, ax
        
        ; Read status register
        in      ax, dx
        test    ax, 0001h                   ; Check interrupt latch bit
        jz      .no_interrupt
        
        mov     ax, 1                       ; Interrupt detected
        jmp     .exit
        
.no_interrupt:
        mov     ax, 0                       ; No interrupt
        
.exit:
        pop     dx
        ret
check_3c515_interrupt ENDP

;-----------------------------------------------------------------------------
; check_io_conflicts - Check for I/O address conflicts between devices
;
; Input:  SI = current device index (from validation loop)
; Output: AX = 1 if conflict found, 0 otherwise
; Uses:   AX, BX, CX, DX, DI
;-----------------------------------------------------------------------------
check_io_conflicts PROC
        push    bx
        push    cx
        push    dx
        push    di
        
        ; Get current device's I/O base
        mov     ax, si
        mov     cx, SIZE HW_INSTANCE
        mul     cx
        mov     di, ax
        add     di, OFFSET hw_instance_table
        mov     dx, [di + HW_INSTANCE.io_base]
        
        ; Check against all other devices
        mov     cx, [hw_instance_count]
        mov     bx, 0                       ; Other device index
        
.conflict_check_loop:
        cmp     bx, si                      ; Skip self
        je      .next_conflict_check
        cmp     bx, cx                      ; Check bounds
        jae     .no_conflicts
        
        ; Get other device's I/O base
        push    ax
        push    cx
        mov     ax, bx
        mov     cx, SIZE HW_INSTANCE
        mul     cx
        mov     di, ax
        add     di, OFFSET hw_instance_table
        pop     cx
        pop     ax
        
        ; Skip undetected devices
        cmp     [di + HW_INSTANCE.state], HW_STATE_UNDETECTED
        je      .next_conflict_check
        
        ; Check for address overlap
        mov     ax, [di + HW_INSTANCE.io_base]
        cmp     ax, dx
        je      .conflict_found             ; Exact match
        
        ; Check for range overlap (simplified check)
        sub     ax, dx
        cmp     ax, 32                      ; Assume 32-byte ranges
        jb      .conflict_found
        
.next_conflict_check:
        inc     bx
        jmp     .conflict_check_loop
        
.no_conflicts:
        mov     ax, 0                       ; No conflicts
        jmp     .exit
        
.conflict_found:
        mov     ax, 1                       ; Conflict found
        
.exit:
        pop     di
        pop     dx
        pop     cx
        pop     bx
        ret
check_io_conflicts ENDP

;-----------------------------------------------------------------------------
; check_irq_conflicts - Check for IRQ conflicts between devices
;
; Input:  SI = current device index (from validation loop)  
; Output: AX = 1 if conflict found, 0 otherwise
; Uses:   AX, BX, CX, DI
;-----------------------------------------------------------------------------
check_irq_conflicts PROC
        push    bx
        push    cx
        push    di
        
        ; Get current device's IRQ
        mov     ax, si
        mov     cx, SIZE HW_INSTANCE
        mul     cx
        mov     di, ax
        add     di, OFFSET hw_instance_table
        mov     al, [di + HW_INSTANCE.irq]
        
        ; Skip auto-assigned IRQs
        cmp     al, IRQ_AUTO_ASSIGN
        je      .no_conflicts
        
        mov     dl, al                      ; Save current IRQ
        
        ; Check against all other devices
        mov     cx, [hw_instance_count]
        mov     bx, 0                       ; Other device index
        
.irq_check_loop:
        cmp     bx, si                      ; Skip self
        je      .next_irq_check
        cmp     bx, cx                      ; Check bounds
        jae     .no_conflicts
        
        ; Get other device's IRQ
        push    ax
        push    cx
        mov     ax, bx
        mov     cx, SIZE HW_INSTANCE
        mul     cx
        mov     di, ax
        add     di, OFFSET hw_instance_table
        pop     cx
        pop     ax
        
        ; Skip undetected devices
        cmp     [di + HW_INSTANCE.state], HW_STATE_UNDETECTED
        je      .next_irq_check
        
        ; Skip auto-assigned IRQs
        mov     al, [di + HW_INSTANCE.irq]
        cmp     al, IRQ_AUTO_ASSIGN
        je      .next_irq_check
        
        ; Check for IRQ match
        cmp     al, dl
        je      .irq_conflict_found
        
.next_irq_check:
        inc     bx
        jmp     .irq_check_loop
        
.no_conflicts:
        mov     ax, 0                       ; No conflicts
        jmp     .exit
        
.irq_conflict_found:
        mov     ax, 1                       ; Conflict found
        
.exit:
        pop     di
        pop     cx
        pop     bx
        ret
check_irq_conflicts ENDP

_TEXT ENDS

END