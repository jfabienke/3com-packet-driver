; @file enhanced_pnp.asm
; @brief Enhanced PnP implementations for Groups 6A & 6B
;
; 3Com Packet Driver - Enhanced PnP detection with LFSR generation
; Implements enhanced PnP interfaces with defensive programming patterns
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
; ENHANCED PNP INTERFACE IMPLEMENTATIONS
;=============================================================================

;-----------------------------------------------------------------------------
; pnp_enumerate_devices - Enumerate all PnP devices
;
; Input:  ES:DI = buffer for device information
;         CX = maximum number of devices to enumerate
; Output: AX = number of devices enumerated
;         Buffer filled with device information
; Uses:   All registers
;-----------------------------------------------------------------------------
pnp_enumerate_devices PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ENTER_CRITICAL_REENTRANT
        
        ; Initialize PnP system if not already done
        cmp     [pnp_available], 0
        jne     .pnp_ready
        
        call    pnp_init_system
        cmp     ax, HW_SUCCESS
        jne     .pnp_init_failed
        
.pnp_ready:
        ; Generate LFSR sequence if needed
        call    pnp_generate_lfsr_sequence
        
        ; Perform device isolation
        call    pnp_isolate_devices
        mov     bx, ax                      ; Save device count
        
        ; Limit to requested maximum
        cmp     bx, cx
        jbe     .copy_devices
        mov     bx, cx                      ; Use requested maximum
        
.copy_devices:
        ; Copy device information to user buffer
        mov     cx, bx                      ; Number of devices to copy
        cmp     cx, 0
        je      .no_devices
        
        mov     si, OFFSET pnp_device_table
        
.copy_loop:
        push    cx
        
        ; Copy device structure
        mov     cx, SIZE PNP_DEVICE_INFO
        rep     movsb
        
        pop     cx
        dec     cx
        jnz     .copy_loop
        
        mov     ax, bx                      ; Return device count
        jmp     .exit
        
.no_devices:
        mov     ax, 0
        jmp     .exit
        
.pnp_init_failed:
        mov     ax, 0
        
.exit:
        EXIT_CRITICAL_REENTRANT
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
pnp_enumerate_devices ENDP

;-----------------------------------------------------------------------------
; pnp_get_device_resources - Get resource requirements for a device
;
; Input:  AL = device index
;         ES:DI = buffer for resource information
; Output: AX = HW_SUCCESS or error code
;         Buffer filled with resource requirements
; Uses:   AX, BX, CX, SI, DI
;-----------------------------------------------------------------------------
pnp_get_device_resources PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si

        ; Validate device index
        cmp     al, [pnp_device_count]
        jae     .invalid_index
        
        ; Calculate offset in device table
        mov     bl, al
        mov     bh, 0
        mov     ax, bx
        mov     cx, SIZE PNP_DEVICE_INFO
        mul     cx
        mov     si, ax
        add     si, OFFSET pnp_device_table
        
        ; Check if device is valid
        cmp     [si + PNP_DEVICE_INFO.vendor_id], 0
        je      .invalid_device
        
        ; Copy basic resource information
        mov     ax, [si + PNP_DEVICE_INFO.io_base]
        stosw                               ; Store I/O base
        mov     al, [si + PNP_DEVICE_INFO.irq]
        stosb                               ; Store IRQ
        
        ; Read additional resource data from device
        mov     al, bl                      ; Device index
        call    pnp_read_resource_data
        
        mov     ax, HW_SUCCESS
        jmp     .exit
        
.invalid_index:
        mov     ax, HW_ERROR_INVALID_PARAM
        jmp     .exit
        
.invalid_device:
        mov     ax, HW_ERROR_NO_DEVICE
        
.exit:
        pop     si
        pop     cx
        pop     bx
        pop     bp
        ret
pnp_get_device_resources ENDP

;-----------------------------------------------------------------------------
; pnp_assign_resources - Assign resources to a PnP device
;
; Input:  AL = device index
;         DX = I/O base address
;         BL = IRQ number
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
pnp_assign_resources PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Validate device index
        cmp     al, [pnp_device_count]
        jae     .invalid_index
        
        ; Validate I/O address
        VALIDATE_IO_ADDRESS dx, ISA_IO_MIN, EISA_IO_MAX
        jc      .invalid_io
        
        ; Validate IRQ
        VALIDATE_IRQ bl
        jc      .invalid_irq
        
        ; Calculate offset in device table
        push    bx                          ; Save IRQ
        push    dx                          ; Save I/O base
        mov     bl, al
        mov     bh, 0
        mov     ax, bx
        mov     cx, SIZE PNP_DEVICE_INFO
        mul     cx
        mov     si, ax
        add     si, OFFSET pnp_device_table
        pop     dx                          ; Restore I/O base
        pop     bx                          ; Restore IRQ
        
        ; Wake the device
        mov     cl, [si + PNP_DEVICE_INFO.csn] ; Card select number
        call    pnp_wake_device
        cmp     ax, HW_SUCCESS
        jne     .wake_failed
        
        ; Assign I/O base address
        call    pnp_set_io_base
        cmp     ax, HW_SUCCESS
        jne     .assign_failed
        
        ; Assign IRQ
        call    pnp_set_irq
        cmp     ax, HW_SUCCESS
        jne     .assign_failed
        
        ; Update device table
        mov     [si + PNP_DEVICE_INFO.io_base], dx
        mov     [si + PNP_DEVICE_INFO.irq], bl
        mov     [si + PNP_DEVICE_INFO.state], HW_STATE_CONFIGURED
        
        mov     ax, HW_SUCCESS
        jmp     .exit
        
.invalid_index:
        mov     ax, HW_ERROR_INVALID_PARAM
        jmp     .exit
        
.invalid_io:
        mov     ax, HW_ERROR_IO_CONFLICT
        jmp     .exit
        
.invalid_irq:
        mov     ax, HW_ERROR_IRQ_CONFLICT
        jmp     .exit
        
.wake_failed:
        mov     ax, HW_ERROR_ACTIVATION
        jmp     .exit
        
.assign_failed:
        mov     ax, HW_ERROR_CONFIG_INVALID
        
.exit:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
pnp_assign_resources ENDP

;-----------------------------------------------------------------------------
; pnp_get_lfsr_table - Get LFSR lookup table
;
; Input:  ES:DI = buffer for LFSR table (256 bytes)
; Output: AX = HW_SUCCESS or error code
;         Buffer filled with LFSR sequence
; Uses:   AX, CX, SI, DI
;-----------------------------------------------------------------------------
pnp_get_lfsr_table PROC
        push    bp
        mov     bp, sp
        push    cx
        push    si

        ; Check if LFSR table is initialized
        cmp     [lfsr_initialized], 0
        jne     .table_ready
        
        ; Generate LFSR sequence
        call    pnp_generate_lfsr_sequence
        cmp     ax, HW_SUCCESS
        jne     .generation_failed
        
.table_ready:
        ; Copy LFSR table to user buffer
        mov     si, OFFSET lfsr_lookup_table
        mov     cx, 256
        rep     movsb
        
        mov     ax, HW_SUCCESS
        jmp     .exit
        
.generation_failed:
        mov     ax, HW_ERROR_GENERIC
        
.exit:
        pop     si
        pop     cx
        pop     bp
        ret
pnp_get_lfsr_table ENDP

;-----------------------------------------------------------------------------
; pnp_generate_lfsr_sequence - Generate LFSR sequence for PnP isolation
;
; Input:  None
; Output: AX = HW_SUCCESS or error code
;         LFSR table filled with sequence
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
pnp_generate_lfsr_sequence PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Check if already initialized
        cmp     [lfsr_initialized], 1
        je      .already_initialized
        
        ; Initialize LFSR with seed value
        mov     ax, PNP_LFSR_SEED           ; Start with 0x6A
        mov     si, OFFSET lfsr_lookup_table
        mov     cx, 256                     ; Generate 256 values
        
.lfsr_loop:
        ; Store current value
        mov     [si], al
        inc     si
        
        ; Generate next LFSR value
        ; Algorithm: lrs_state <<= 1; lrs_state = lrs_state & 0x100 ? lrs_state ^ 0xcf : lrs_state;
        shl     ax, 1                       ; Shift left
        test    ax, 0x100                   ; Check bit 8
        jz      .no_xor
        xor     ax, 0xCF                    ; XOR with 0xCF if bit 8 set
        
.no_xor:
        and     ax, 0xFF                    ; Keep only lower 8 bits
        
        dec     cx
        jnz     .lfsr_loop
        
        ; Mark as initialized
        mov     [lfsr_initialized], 1
        
.already_initialized:
        mov     ax, HW_SUCCESS
        
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
pnp_generate_lfsr_sequence ENDP

;-----------------------------------------------------------------------------
; pnp_shutdown_system - Shutdown PnP system
;
; Input:  None
; Output: AX = HW_SUCCESS
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
pnp_shutdown_system PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ENTER_CRITICAL_REENTRANT
        
        ; Put all PnP devices to sleep
        mov     dx, PNP_ADDRESS_PORT
        mov     al, PNP_CMD_CONFIG_CONTROL
        out     dx, al
        
        mov     dx, PNP_WRITE_DATA_PORT
        mov     al, 03h                     ; Sleep command
        out     dx, al
        
        ; Reset PnP system state
        mov     [pnp_available], 0
        mov     [pnp_device_count], 0
        mov     [lfsr_initialized], 0
        
        ; Clear device table
        mov     cx, SIZE PNP_DEVICE_INFO
        mov     ax, PNP_MAX_DEVICES
        mul     cx
        mov     cx, ax                      ; Total bytes to clear
        
        push    es
        push    di
        mov     ax, ds
        mov     es, ax
        mov     di, OFFSET pnp_device_table
        xor     ax, ax
        rep     stosb
        pop     di
        pop     es
        
        mov     ax, HW_SUCCESS
        
        EXIT_CRITICAL_REENTRANT
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
pnp_shutdown_system ENDP

;-----------------------------------------------------------------------------
; pnp_activate_device - Activate a configured PnP device
;
; Input:  AL = device index
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
pnp_activate_device PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Validate device index
        cmp     al, [pnp_device_count]
        jae     .invalid_index
        
        ; Calculate offset in device table
        mov     bl, al
        mov     bh, 0
        mov     ax, bx
        mov     cx, SIZE PNP_DEVICE_INFO
        mul     cx
        mov     si, ax
        add     si, OFFSET pnp_device_table
        
        ; Check if device is configured
        cmp     [si + PNP_DEVICE_INFO.state], HW_STATE_CONFIGURED
        jb      .not_configured
        
        ; Wake the device
        mov     cl, [si + PNP_DEVICE_INFO.csn]
        call    pnp_wake_device
        cmp     ax, HW_SUCCESS
        jne     .activation_failed
        
        ; Send activate command
        mov     dx, PNP_ADDRESS_PORT
        mov     al, PNP_CMD_ACTIVATE
        out     dx, al
        
        mov     dx, PNP_WRITE_DATA_PORT
        mov     al, 1                       ; Activate
        out     dx, al
        
        ; Update device state
        mov     [si + PNP_DEVICE_INFO.state], HW_STATE_ACTIVE
        
        mov     ax, HW_SUCCESS
        jmp     .exit
        
.invalid_index:
        mov     ax, HW_ERROR_INVALID_PARAM
        jmp     .exit
        
.not_configured:
        mov     ax, HW_ERROR_CONFIG_INVALID
        jmp     .exit
        
.activation_failed:
        mov     ax, HW_ERROR_ACTIVATION
        
.exit:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
pnp_activate_device ENDP

;=============================================================================
; PNP UTILITY FUNCTIONS
;=============================================================================

;-----------------------------------------------------------------------------
; pnp_wake_device - Wake a specific PnP device
;
; Input:  CL = card select number
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, DX
;-----------------------------------------------------------------------------
pnp_wake_device PROC
        push    dx
        
        ; Send wake command
        mov     dx, PNP_ADDRESS_PORT
        mov     al, PNP_CMD_WAKE
        out     dx, al
        
        mov     dx, PNP_WRITE_DATA_PORT
        mov     al, cl                      ; Card select number
        out     dx, al
        
        ; Small delay for device wake-up
        call    delay_10ms
        
        mov     ax, HW_SUCCESS
        
        pop     dx
        ret
pnp_wake_device ENDP

;-----------------------------------------------------------------------------
; pnp_set_io_base - Set I/O base address for current device
;
; Input:  DX = I/O base address
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, DX
;-----------------------------------------------------------------------------
pnp_set_io_base PROC
        push    bx
        push    dx
        
        ; Set I/O base high byte
        push    dx
        mov     dx, PNP_ADDRESS_PORT
        mov     al, PNP_REG_IO_BASE_HIGH
        out     dx, al
        pop     dx
        
        push    dx
        mov     bx, dx
        mov     dx, PNP_WRITE_DATA_PORT
        mov     al, bh                      ; High byte
        out     dx, al
        pop     dx
        
        ; Set I/O base low byte
        push    dx
        mov     dx, PNP_ADDRESS_PORT
        mov     al, PNP_REG_IO_BASE_LOW
        out     dx, al
        pop     dx
        
        mov     bx, dx
        mov     dx, PNP_WRITE_DATA_PORT
        mov     al, bl                      ; Low byte
        out     dx, al
        
        mov     ax, HW_SUCCESS
        
        pop     dx
        pop     bx
        ret
pnp_set_io_base ENDP

;-----------------------------------------------------------------------------
; pnp_set_irq - Set IRQ for current device
;
; Input:  BL = IRQ number
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, DX
;-----------------------------------------------------------------------------
pnp_set_irq PROC
        push    dx
        
        ; Set IRQ register
        mov     dx, PNP_ADDRESS_PORT
        mov     al, PNP_REG_IRQ_SELECT
        out     dx, al
        
        mov     dx, PNP_WRITE_DATA_PORT
        mov     al, bl                      ; IRQ number
        out     dx, al
        
        mov     ax, HW_SUCCESS
        
        pop     dx
        ret
pnp_set_irq ENDP

_TEXT ENDS

END