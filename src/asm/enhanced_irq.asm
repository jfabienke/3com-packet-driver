; @file enhanced_irq.asm
; @brief Enhanced IRQ management for Groups 6A & 6B
;
; 3Com Packet Driver - Enhanced IRQ handling with defensive programming
; Implements shared IRQ management and device-specific dispatch
;
; This file is part of the 3Com Packet Driver project.

.MODEL SMALL
.386

; Include interface definitions
include "asm_interfaces.inc"

; External function declarations
EXTERN hardware_handle_3c509b_irq:PROC
EXTERN hardware_handle_3c515_irq:PROC

; Data segment
_DATA SEGMENT
        ASSUME  DS:_DATA

; IRQ handler table
irq_handler_table   IRQ_CONTEXT MAX_HW_INSTANCES dup(<>)
irq_handler_count   db 0        ; Number of installed handlers

; IRQ statistics
irq_call_count      dd 0        ; Total IRQ calls
irq_handled_count   dd 0        ; Successfully handled IRQs
irq_spurious_count  dd 0        ; Spurious interrupts
irq_error_count     dd 0        ; IRQ handling errors

; Saved interrupt vectors
saved_irq_vectors   dd 16 dup(0) ; Saved vectors for IRQs 0-15

; IRQ enable state
irq_enabled_mask    dw 0        ; Bitmask of enabled IRQs

_DATA ENDS

; Code segment
_TEXT SEGMENT
        ASSUME  CS:_TEXT, DS:_DATA

;=============================================================================
; IRQ MANAGEMENT INTERFACE IMPLEMENTATIONS
;=============================================================================

;-----------------------------------------------------------------------------
; irq_setup_shared_handler - Setup shared IRQ handler for multiple devices
;
; Input:  AL = IRQ number
;         BL = number of devices sharing this IRQ
; Output: AX = HW_SUCCESS or error code
; Uses:   All registers
;-----------------------------------------------------------------------------
irq_setup_shared_handler PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ENTER_CRITICAL_REENTRANT

        ; Validate IRQ number
        VALIDATE_IRQ al
        jc      .invalid_irq

        ; Check if IRQ already has a handler
        mov     ah, al                      ; Save IRQ number
        call    irq_find_handler
        cmp     ax, 0FFFFh
        jne     .irq_already_handled

        ; Install new IRQ handler
        mov     al, ah                      ; Restore IRQ number
        call    irq_install_handler
        cmp     ax, HW_SUCCESS
        jne     .install_failed

        ; Enable IRQ line
        call    irq_enable_line
        cmp     ax, HW_SUCCESS
        jne     .enable_failed

        mov     ax, HW_SUCCESS
        jmp     .exit

.invalid_irq:
        mov     ax, HW_ERROR_IRQ_CONFLICT
        jmp     .exit

.irq_already_handled:
        ; IRQ already has handler, this is OK for shared IRQs
        mov     ax, HW_SUCCESS
        jmp     .exit

.install_failed:
.enable_failed:
        mov     ax, HW_ERROR_GENERIC

.exit:
        EXIT_CRITICAL_REENTRANT
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
irq_setup_shared_handler ENDP

;-----------------------------------------------------------------------------
; irq_install_handler - Install IRQ handler
;
; Input:  AL = IRQ number
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
irq_install_handler PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Validate IRQ number
        VALIDATE_IRQ al
        jc      .invalid_irq

        ; Find free slot in handler table
        mov     cx, MAX_HW_INSTANCES
        mov     si, OFFSET irq_handler_table
        mov     bl, al                      ; Save IRQ number

.find_free_slot:
        cmp     [si + IRQ_CONTEXT.irq_number], 0
        je      .found_free_slot
        add     si, SIZE IRQ_CONTEXT
        dec     cx
        jnz     .find_free_slot
        
        ; No free slots
        mov     ax, HW_ERROR_NO_MEMORY
        jmp     .exit

.found_free_slot:
        ; Save current interrupt vector
        push    bx
        mov     al, bl                      ; IRQ number
        call    irq_save_vector
        pop     bx

        ; Install our handler
        mov     al, bl                      ; IRQ number
        mov     ah, 0
        add     al, 8                       ; Convert to interrupt number
        cmp     al, 16
        jb      .primary_pic
        add     al, 70h - 16                ; Secondary PIC base

.primary_pic:
        ; Install vector using DOS function
        push    ds
        mov     ah, 25h                     ; Set interrupt vector
        mov     dx, OFFSET irq_common_handler
        push    cs
        pop     ds
        int     21h
        pop     ds

        ; Initialize handler context
        mov     [si + IRQ_CONTEXT.handler_offset], OFFSET irq_common_handler
        mov     [si + IRQ_CONTEXT.handler_segment], cs
        mov     [si + IRQ_CONTEXT.irq_number], bl
        mov     [si + IRQ_CONTEXT.call_count], 0
        mov     [si + IRQ_CONTEXT.error_count], 0
        mov     [si + IRQ_CONTEXT.flags], 0

        ; Update handler count
        inc     [irq_handler_count]

        mov     ax, HW_SUCCESS
        jmp     .exit

.invalid_irq:
        mov     ax, HW_ERROR_INVALID_PARAM

.exit:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
irq_install_handler ENDP

;-----------------------------------------------------------------------------
; irq_remove_handler - Remove IRQ handler
;
; Input:  AL = IRQ number
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
irq_remove_handler PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ENTER_CRITICAL_REENTRANT

        ; Find handler in table
        mov     bl, al                      ; Save IRQ number
        call    irq_find_handler
        cmp     ax, 0FFFFh
        je      .handler_not_found

        ; Disable IRQ line first
        mov     al, bl
        call    irq_disable_line

        ; Restore original interrupt vector
        mov     al, bl
        call    irq_restore_vector

        ; Clear handler context
        mov     si, ax                      ; Handler index from find_handler
        mov     cx, SIZE IRQ_CONTEXT
        mul     cx
        mov     si, ax
        add     si, OFFSET irq_handler_table

        ; Zero out the context
        mov     cx, SIZE IRQ_CONTEXT
        push    es
        push    di
        mov     ax, ds
        mov     es, ax
        mov     di, si
        xor     ax, ax
        rep     stosb
        pop     di
        pop     es

        ; Update handler count
        dec     [irq_handler_count]

        mov     ax, HW_SUCCESS
        jmp     .exit

.handler_not_found:
        mov     ax, HW_ERROR_NOT_FOUND

.exit:
        EXIT_CRITICAL_REENTRANT
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
irq_remove_handler ENDP

;-----------------------------------------------------------------------------
; irq_enable_line - Enable IRQ line
;
; Input:  AL = IRQ number
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, DX
;-----------------------------------------------------------------------------
irq_enable_line PROC
        push    dx

        ; Validate IRQ
        VALIDATE_IRQ al
        jc      .invalid_irq

        ; Enable IRQ in 8259 PIC
        cmp     al, 8
        jae     .secondary_pic

        ; Primary PIC (IRQ 0-7)
        mov     dx, 21h                     ; Primary PIC mask port
        push    ax
        in      al, dx                      ; Read current mask
        mov     ah, al                      ; Save current mask
        pop     ax                          ; Restore IRQ number
        
        ; Clear the IRQ bit (enable it)
        mov     cl, al
        mov     bl, 1
        shl     bl, cl                      ; Create bit mask
        not     bl                          ; Invert for clearing
        and     ah, bl                      ; Clear IRQ bit
        mov     al, ah
        out     dx, al                      ; Write new mask
        jmp     .irq_enabled

.secondary_pic:
        ; Secondary PIC (IRQ 8-15)
        sub     al, 8                       ; Convert to secondary PIC IRQ
        mov     dx, 0A1h                    ; Secondary PIC mask port
        push    ax
        in      al, dx                      ; Read current mask
        mov     ah, al                      ; Save current mask
        pop     ax                          ; Restore IRQ number
        
        ; Clear the IRQ bit
        mov     cl, al
        mov     bl, 1
        shl     bl, cl
        not     bl
        and     ah, bl
        mov     al, ah
        out     dx, al

        ; Also enable cascade IRQ 2 on primary PIC
        mov     dx, 21h
        in      al, dx
        and     al, 0FBh                    ; Clear bit 2 (IRQ 2)
        out     dx, al

.irq_enabled:
        ; Update enabled mask
        mov     al, [bp + 4]                ; Original IRQ number from stack
        mov     cl, al
        mov     ax, 1
        shl     ax, cl
        or      [irq_enabled_mask], ax

        mov     ax, HW_SUCCESS
        jmp     .exit

.invalid_irq:
        mov     ax, HW_ERROR_INVALID_PARAM

.exit:
        pop     dx
        ret
irq_enable_line ENDP

;-----------------------------------------------------------------------------
; irq_disable_line - Disable IRQ line
;
; Input:  AL = IRQ number
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, DX
;-----------------------------------------------------------------------------
irq_disable_line PROC
        push    dx

        ; Validate IRQ
        VALIDATE_IRQ al
        jc      .invalid_irq

        ; Disable IRQ in 8259 PIC
        cmp     al, 8
        jae     .secondary_pic

        ; Primary PIC (IRQ 0-7)
        mov     dx, 21h                     ; Primary PIC mask port
        push    ax
        in      al, dx                      ; Read current mask
        mov     ah, al                      ; Save current mask
        pop     ax                          ; Restore IRQ number
        
        ; Set the IRQ bit (disable it)
        mov     cl, al
        mov     bl, 1
        shl     bl, cl                      ; Create bit mask
        or      ah, bl                      ; Set IRQ bit
        mov     al, ah
        out     dx, al                      ; Write new mask
        jmp     .irq_disabled

.secondary_pic:
        ; Secondary PIC (IRQ 8-15)
        sub     al, 8                       ; Convert to secondary PIC IRQ
        mov     dx, 0A1h                    ; Secondary PIC mask port
        push    ax
        in      al, dx                      ; Read current mask
        mov     ah, al                      ; Save current mask
        pop     ax                          ; Restore IRQ number
        
        ; Set the IRQ bit
        mov     cl, al
        mov     bl, 1
        shl     bl, cl
        or      ah, bl
        mov     al, ah
        out     dx, al

.irq_disabled:
        ; Update enabled mask
        mov     al, [bp + 4]                ; Original IRQ number from stack
        mov     cl, al
        mov     ax, 1
        shl     ax, cl
        not     ax
        and     [irq_enabled_mask], ax

        mov     ax, HW_SUCCESS
        jmp     .exit

.invalid_irq:
        mov     ax, HW_ERROR_INVALID_PARAM

.exit:
        pop     dx
        ret
irq_disable_line ENDP

;-----------------------------------------------------------------------------
; irq_dispatch - Dispatch IRQ to appropriate device handlers
;
; Input:  AL = IRQ number
; Output: AX = HW_SUCCESS if handled, error code otherwise
; Uses:   All registers
;-----------------------------------------------------------------------------
irq_dispatch PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Update call statistics
        inc     dword ptr [irq_call_count]

        ; Find all devices that might be using this IRQ
        mov     bl, al                      ; Save IRQ number
        mov     cx, [hw_instance_count]
        cmp     cx, 0
        je      .no_devices

        mov     si, 0                       ; Device index

.check_devices:
        cmp     si, cx
        jae     .irq_not_handled

        ; Check if device uses this IRQ
        cmp     [hw_irq_lines + si], bl
        jne     .next_device

        ; Check device type and dispatch
        mov     al, [hw_types + si]
        cmp     al, HW_TYPE_3C509B
        je      .dispatch_3c509b
        cmp     al, HW_TYPE_3C515TX
        je      .dispatch_3c515tx
        jmp     .next_device

.dispatch_3c509b:
        ; Check if 3C509B actually caused the interrupt
        push    cx
        push    si
        mov     si, si
        shl     si, 1                       ; Word offset
        mov     dx, [hw_io_bases + si]
        call    check_3c509b_interrupt_status
        pop     si
        pop     cx
        cmp     ax, 1
        jne     .next_device

        ; Handle 3C509B interrupt
        call    hardware_handle_3c509b_irq
        inc     dword ptr [irq_handled_count]
        mov     ax, HW_SUCCESS
        jmp     .exit

.dispatch_3c515tx:
        ; Check if 3C515-TX actually caused the interrupt
        push    cx
        push    si
        mov     si, si
        shl     si, 1                       ; Word offset
        mov     dx, [hw_io_bases + si]
        call    check_3c515_interrupt_status
        pop     si
        pop     cx
        cmp     ax, 1
        jne     .next_device

        ; Handle 3C515-TX interrupt
        call    hardware_handle_3c515_irq
        inc     dword ptr [irq_handled_count]
        mov     ax, HW_SUCCESS
        jmp     .exit

.next_device:
        inc     si
        jmp     .check_devices

.no_devices:
.irq_not_handled:
        ; No device handled the interrupt - might be spurious
        inc     dword ptr [irq_spurious_count]
        mov     ax, HW_ERROR_GENERIC

.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
irq_dispatch ENDP

;-----------------------------------------------------------------------------
; irq_get_handler_info - Get IRQ handler information
;
; Input:  AL = IRQ number
;         ES:DI = buffer for handler information
; Output: AX = HW_SUCCESS or error code
;         Buffer filled with handler info
; Uses:   AX, BX, CX, SI, DI
;-----------------------------------------------------------------------------
irq_get_handler_info PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si

        ; Find handler in table
        call    irq_find_handler
        cmp     ax, 0FFFFh
        je      .handler_not_found

        ; Calculate handler context address
        mov     bx, ax                      ; Handler index
        mov     cx, SIZE IRQ_CONTEXT
        mul     cx
        mov     si, ax
        add     si, OFFSET irq_handler_table

        ; Copy handler information to buffer
        mov     cx, SIZE IRQ_CONTEXT
        rep     movsb

        mov     ax, HW_SUCCESS
        jmp     .exit

.handler_not_found:
        mov     ax, HW_ERROR_NOT_FOUND

.exit:
        pop     si
        pop     cx
        pop     bx
        pop     bp
        ret
irq_get_handler_info ENDP

;-----------------------------------------------------------------------------
; irq_update_statistics - Update IRQ statistics for a handler
;
; Input:  AL = IRQ number
;         BL = operation result (0=success, 1=error)
; Output: AX = HW_SUCCESS
; Uses:   AX, CX, SI
;-----------------------------------------------------------------------------
irq_update_statistics PROC
        push    bp
        mov     bp, sp
        push    cx
        push    si

        ; Find handler in table
        call    irq_find_handler
        cmp     ax, 0FFFFh
        je      .handler_not_found

        ; Calculate handler context address
        mov     cx, SIZE IRQ_CONTEXT
        mul     cx
        mov     si, ax
        add     si, OFFSET irq_handler_table

        ; Update call count
        inc     dword ptr [si + IRQ_CONTEXT.call_count]

        ; Update error count if needed
        test    bl, bl
        jz      .no_error
        inc     word ptr [si + IRQ_CONTEXT.error_count]

.no_error:
.handler_not_found:
        mov     ax, HW_SUCCESS

        pop     si
        pop     cx
        pop     bp
        ret
irq_update_statistics ENDP

;=============================================================================
; IRQ UTILITY FUNCTIONS
;=============================================================================

;-----------------------------------------------------------------------------
; irq_common_handler - Common IRQ entry point for all IRQs
;
; This is the actual interrupt service routine that gets called
; by the hardware. It determines which IRQ occurred and dispatches
; to the appropriate device handlers.
;-----------------------------------------------------------------------------
irq_common_handler PROC
        ; Save all registers
        pushf
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    ds
        push    es

        ; Set up our data segment
        mov     ax, SEG _DATA
        mov     ds, ax
        assume  ds:_DATA

        ; Read IRQ number from PIC
        call    irq_get_current_irq
        mov     bl, al                      ; Save IRQ number

        ; Dispatch to device handlers
        call    irq_dispatch

        ; Send End of Interrupt (EOI) to PIC
        mov     al, bl                      ; Restore IRQ number
        call    irq_send_eoi

        ; Restore all registers
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        popf

        iret
irq_common_handler ENDP

;-----------------------------------------------------------------------------
; irq_find_handler - Find handler for given IRQ
;
; Input:  AL = IRQ number
; Output: AX = handler index, or 0FFFFh if not found
; Uses:   AX, CX, SI
;-----------------------------------------------------------------------------
irq_find_handler PROC
        push    cx
        push    si

        mov     cx, MAX_HW_INSTANCES
        mov     si, OFFSET irq_handler_table
        mov     ah, al                      ; Save IRQ number

.search_loop:
        cmp     [si + IRQ_CONTEXT.irq_number], ah
        je      .handler_found
        add     si, SIZE IRQ_CONTEXT
        dec     cx
        jnz     .search_loop

        ; Handler not found
        mov     ax, 0FFFFh
        jmp     .exit

.handler_found:
        ; Calculate handler index
        sub     si, OFFSET irq_handler_table
        mov     ax, si
        mov     cx, SIZE IRQ_CONTEXT
        div     cx                          ; AX = index

.exit:
        pop     si
        pop     cx
        ret
irq_find_handler ENDP

;-----------------------------------------------------------------------------
; irq_get_current_irq - Determine which IRQ is currently active
;
; Input:  None
; Output: AL = IRQ number
; Uses:   AL, DX
;-----------------------------------------------------------------------------
irq_get_current_irq PROC
        push    dx

        ; Read In-Service Register from primary PIC
        mov     dx, 20h                     ; Primary PIC command port
        mov     al, 0Bh                     ; Read ISR command
        out     dx, al
        
        mov     dx, 20h                     ; Primary PIC data port
        in      al, dx                      ; Read ISR

        ; Find first set bit
        mov     cl, 0
.find_bit:
        test    al, 1
        jnz     .found_irq
        shr     al, 1
        inc     cl
        cmp     cl, 8
        jb      .find_bit

        ; Check secondary PIC if no IRQ found in primary
        mov     dx, 0A0h                    ; Secondary PIC command port
        mov     al, 0Bh                     ; Read ISR command
        out     dx, al
        
        mov     dx, 0A0h                    ; Secondary PIC data port
        in      al, dx                      ; Read ISR

        mov     cl, 8                       ; Start from IRQ 8
.find_bit_secondary:
        test    al, 1
        jnz     .found_irq
        shr     al, 1
        inc     cl
        cmp     cl, 16
        jb      .find_bit_secondary

        ; Default to IRQ 0 if nothing found
        mov     cl, 0

.found_irq:
        mov     al, cl                      ; Return IRQ number

        pop     dx
        ret
irq_get_current_irq ENDP

;-----------------------------------------------------------------------------
; irq_send_eoi - Send End of Interrupt to PIC
;
; Input:  AL = IRQ number
; Output: None
; Uses:   AL, DX
;-----------------------------------------------------------------------------
irq_send_eoi PROC
        push    dx

        cmp     al, 8
        jae     .secondary_pic

        ; Primary PIC EOI
        mov     dx, 20h                     ; Primary PIC command port
        mov     al, 20h                     ; Non-specific EOI
        out     dx, al
        jmp     .eoi_sent

.secondary_pic:
        ; Secondary PIC EOI (also need to send to primary)
        mov     dx, 0A0h                    ; Secondary PIC command port
        mov     al, 20h                     ; Non-specific EOI
        out     dx, al
        
        mov     dx, 20h                     ; Primary PIC command port
        mov     al, 20h                     ; Non-specific EOI for cascade
        out     dx, al

.eoi_sent:
        pop     dx
        ret
irq_send_eoi ENDP

;-----------------------------------------------------------------------------
; irq_save_vector - Save current interrupt vector
;
; Input:  AL = IRQ number
; Output: None
; Uses:   AX, BX, ES
;-----------------------------------------------------------------------------
irq_save_vector PROC
        push    bx
        push    es

        ; Convert IRQ to interrupt number
        mov     bl, al
        add     al, 8                       ; IRQ to interrupt number
        cmp     al, 16
        jb      .primary_pic
        add     al, 70h - 16                ; Secondary PIC adjustment

.primary_pic:
        ; Get current vector using DOS
        mov     ah, 35h                     ; Get interrupt vector
        int     21h                         ; ES:BX = current vector

        ; Save in our table
        mov     al, bl                      ; Restore IRQ number
        mov     ah, 0
        shl     ax, 2                       ; Convert to dword offset
        mov     bx, ax
        mov     [saved_irq_vectors + bx], es   ; Save segment
        mov     [saved_irq_vectors + bx + 2], bx ; Save offset

        pop     es
        pop     bx
        ret
irq_save_vector ENDP

;-----------------------------------------------------------------------------
; irq_restore_vector - Restore original interrupt vector
;
; Input:  AL = IRQ number
; Output: None
; Uses:   AX, BX, DX, DS
;-----------------------------------------------------------------------------
irq_restore_vector PROC
        push    bx
        push    dx
        push    ds

        ; Get saved vector from our table
        mov     bl, al                      ; Save IRQ number
        mov     ah, 0
        shl     ax, 2                       ; Convert to dword offset
        mov     bx, ax
        mov     dx, [saved_irq_vectors + bx + 2] ; Offset
        mov     ds, [saved_irq_vectors + bx]     ; Segment

        ; Convert IRQ to interrupt number
        mov     al, bl
        add     al, 8
        cmp     al, 16
        jb      .restore_primary
        add     al, 70h - 16

.restore_primary:
        ; Restore vector using DOS
        mov     ah, 25h                     ; Set interrupt vector
        int     21h

        pop     ds
        pop     dx
        pop     bx
        ret
irq_restore_vector ENDP

;-----------------------------------------------------------------------------
; check_3c509b_interrupt_status - Check if 3C509B caused interrupt
;
; Input:  DX = I/O base address
; Output: AX = 1 if interrupt detected, 0 otherwise
; Uses:   AX, DX
;-----------------------------------------------------------------------------
check_3c509b_interrupt_status PROC
        push    dx

        ; Select window 1 for status access
        add     dx, 0Eh                     ; Command register
        mov     ax, (1 << 11) | 1           ; Select window 1
        out     dx, ax

        ; Read status register
        in      ax, dx
        test    ax, 0001h                   ; Check interrupt latch
        jz      .no_interrupt

        mov     ax, 1                       ; Interrupt detected
        jmp     .exit

.no_interrupt:
        mov     ax, 0                       ; No interrupt

.exit:
        pop     dx
        ret
check_3c509b_interrupt_status ENDP

;-----------------------------------------------------------------------------
; check_3c515_interrupt_status - Check if 3C515-TX caused interrupt
;
; Input:  DX = I/O base address  
; Output: AX = 1 if interrupt detected, 0 otherwise
; Uses:   AX, DX
;-----------------------------------------------------------------------------
check_3c515_interrupt_status PROC
        push    dx

        ; Select window 1 for status access
        add     dx, 0Eh                     ; Command register
        mov     ax, (1 << 11) | 1           ; Select window 1
        out     dx, ax

        ; Read status register
        in      ax, dx
        test    ax, 0001h                   ; Check interrupt latch
        jz      .no_interrupt

        mov     ax, 1                       ; Interrupt detected
        jmp     .exit

.no_interrupt:
        mov     ax, 0                       ; No interrupt

.exit:
        pop     dx
        ret
check_3c515_interrupt_status ENDP

; Export public functions
PUBLIC irq_setup_shared_handler
PUBLIC irq_install_handler
PUBLIC irq_remove_handler
PUBLIC irq_enable_line
PUBLIC irq_disable_line
PUBLIC irq_dispatch
PUBLIC irq_get_handler_info
PUBLIC irq_update_statistics
PUBLIC irq_common_handler

_TEXT ENDS

END