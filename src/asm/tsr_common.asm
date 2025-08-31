; TSR Common Routines
; Implements battle-tested TSR defensive patterns for DOS packet driver
; Based on analysis of 3C5X9PD and 30+ years of DOS networking survival techniques

.model small
.386

include 'tsr_defensive.inc'

.data

; TSR identification and status
driver_signature    db 'PKT DRVR v2.0 - 3Com Advanced',0
driver_installed    db 0                ; Installation status flag
multiplex_id        db 0xC9             ; AMIS multiplex ID

; Saved interrupt vectors
old_int2f_offset    dw ?
old_int2f_segment   dw ?
old_int28_offset    dw ?  
old_int28_segment   dw ?
old_int60_offset    dw ?
old_int60_segment   dw ?

; DOS safety management
indos_segment       dw 0
indos_offset        dw 0
criterr_segment     dw 0                ; 0 if not available
criterr_offset      dw 0

; Stack management
caller_ss           dw ?
caller_sp           dw ?
driver_stack        db 2048 dup(?)      ; Private stack (2KB)
stack_top           equ $ - 2           ; Top of stack

; Critical section management  
critical_nesting    db 0

; Vector validation
vector_check_timer  dw 0                ; Periodic validation timer

; Work queue for deferred operations
work_queue          dw 32 dup(?)        ; Deferred work queue
queue_head          dw 0
queue_tail          dw 0
work_pending        db 0

; UMB management (DOS 5.0+ support)
public umb_segment, umb_available
umb_available       db 0                ; UMB support available flag
umb_linked          db 0                ; UMBs linked into memory chain
umb_segment         dw 0                ; Our UMB segment (if allocated)
umb_size_paragraphs dw 0                ; Size of our UMB allocation
original_alloc_strategy db 0            ; Original DOS allocation strategy
memory_manager_type db 0                ; 0=none, 1=HIMEM, 2=EMM386, 3=QEMM

; Memory manager detection strings
himem_signature     db 'HIMEM   ', 0
emm386_signature    db 'EMM386  ', 0
qemm_signature      db 'QEMM386 ', 0

; UMB allocation status
umb_allocation_attempted db 0           ; Flag to prevent retry loops
conventional_fallback   db 0           ; Using conventional memory flag

; Performance profiling (if enabled)
ifdef PROFILE_BUILD
timer_start_high    dw ?
timer_start_low     dw ?
endif

; Error statistics
error_count         dw 0
recovery_count      dw 0
last_error_code     db 0

.code

;=============================================================================
; INITIALIZATION ROUTINES
;=============================================================================

public initialize_tsr_defense
initialize_tsr_defense proc
    push ax
    push bx
    push cx
    push dx
    push es
    
    ; Get InDOS flag address
    call get_indos_address
    
    ; Get critical error flag (DOS 3.0+)
    call get_critical_error_flag
    
    ; Install AMIS multiplex handler
    call install_multiplex_handler
    
    ; Install DOS idle hook  
    call install_idle_hook
    
    ; Initialize vector validation
    mov word [vector_check_timer], 1000  ; Check every ~1000 calls
    
    ; Mark as initialized
    mov byte [driver_installed], 1
    
    pop es
    pop dx
    pop cx  
    pop bx
    pop ax
    clc                                  ; Success
    ret
initialize_tsr_defense endp

get_indos_address proc
    push es
    push bx
    
    mov ah, 34h
    int 21h                              ; Returns ES:BX -> InDOS flag
    mov [indos_segment], es
    mov [indos_offset], bx
    
    pop bx
    pop es
    ret
get_indos_address endp

get_critical_error_flag proc
    push ds
    push si
    
    ; Try to get critical error flag (DOS 3.0+)
    mov ax, 5D06h
    int 21h                              ; Returns DS:SI -> critical error flag
    jc .not_available                    ; Not supported
    
    mov [criterr_segment], ds
    mov [criterr_offset], si
    jmp short .done
    
.not_available:
    mov word [criterr_segment], 0        ; Mark as unavailable
    
.done:
    pop si
    pop ds
    ret
get_critical_error_flag endp

;=============================================================================
; INTERRUPT VECTOR MANAGEMENT
;=============================================================================

install_multiplex_handler proc
    ; Save original INT 2Fh vector
    mov ax, 352Fh
    int 21h
    mov [old_int2f_offset], bx
    mov [old_int2f_segment], es
    
    ; Install our handler
    push ds
    mov ax, cs
    mov ds, ax
    mov dx, offset int2f_handler
    mov ax, 252Fh
    int 21h
    pop ds
    
    ret
install_multiplex_handler endp

install_idle_hook proc
    ; Save original INT 28h vector
    mov ax, 3528h
    int 21h
    mov [old_int28_offset], bx
    mov [old_int28_segment], es
    
    ; Install our handler
    push ds
    mov ax, cs
    mov ds, ax
    mov dx, offset int28_handler
    mov ax, 2528h
    int 21h
    pop ds
    
    ret
install_idle_hook endp

public install_packet_api_vector
install_packet_api_vector proc
    ; Save original INT 60h vector
    mov ax, 3560h
    int 21h
    mov [old_int60_offset], bx
    mov [old_int60_segment], es
    
    ; Install packet driver API vector (will be set by main driver)
    ; This is just the infrastructure - actual handler set elsewhere
    
    ret
install_packet_api_vector endp

;=============================================================================
; AMIS-COMPLIANT MULTIPLEX HANDLER
;=============================================================================

int2f_handler proc far
    cmp ah, [cs:multiplex_id]
    jne .chain
    
    ; Handle AMIS standard functions
    cmp al, AMIS_INSTALLATION_CHECK
    je .installation_check
    cmp al, AMIS_GET_ENTRY_POINT
    je .get_entry_point
    cmp al, AMIS_UNINSTALL_CHECK
    je .uninstall_check
    
.chain:
    ; Chain to original handler
    jmp dword ptr [cs:old_int2f_offset]

.installation_check:
    AMIS_INSTALLATION_CHECK driver_signature, 0x0200

.get_entry_point:
    AMIS_GET_ENTRY_POINT private_api_entry, 0x0200
    
.uninstall_check:
    ; Check if we can uninstall
    call check_uninstall_safety
    jc .cannot_uninstall
    
    mov al, 0FFh                         ; Can uninstall
    iret
    
.cannot_uninstall:
    mov al, 07h                          ; Cannot uninstall now
    iret
int2f_handler endp

;=============================================================================
; DOS IDLE HOOK FOR DEFERRED OPERATIONS
;=============================================================================

int28_handler proc far
    ; Check if we have deferred work
    cmp byte [cs:work_pending], 0
    jz .chain
    
    ; Check if DOS is safe
    CHECK_DOS_COMPLETELY_SAFE
    jnz .chain                           ; DOS still busy
    
    ; Process deferred work safely
    SAFE_STACK_SWITCH
    
    pusha
    push ds
    push es
    
    mov ax, cs
    mov ds, ax
    
    call process_work_queue
    
    pop es
    pop ds
    popa
    
    RESTORE_CALLER_STACK
    
.chain:
    ; Chain to original idle handler
    jmp dword ptr [cs:old_int28_offset]
int28_handler endp

;=============================================================================
; DEFERRED WORK QUEUE MANAGEMENT
;=============================================================================

public queue_deferred_work
queue_deferred_work proc
    ; AX = function address to call later
    ; Returns: CY set if queue full
    
    ENTER_CRITICAL
    
    ; Check for queue space
    mov bx, [queue_tail]
    inc bx
    and bx, 31                           ; Wrap at 32 entries
    cmp bx, [queue_head]
    je .queue_full
    
    ; Add to queue
    mov bx, [queue_tail]
    mov [work_queue + bx*2], ax
    inc word [queue_tail]
    and word [queue_tail], 31
    mov byte [work_pending], 1
    
    EXIT_CRITICAL
    clc
    ret
    
.queue_full:
    EXIT_CRITICAL
    stc
    ret
queue_deferred_work endp

process_work_queue proc
    ; Process all queued work
    
.process_loop:
    ENTER_CRITICAL
    
    ; Check if queue empty
    mov ax, [queue_head]
    cmp ax, [queue_tail]
    je .queue_empty
    
    ; Get next work item
    mov bx, ax
    mov ax, [work_queue + bx*2]
    inc word [queue_head]
    and word [queue_head], 31
    
    EXIT_CRITICAL
    
    ; Call the deferred function
    push ax
    call ax                              ; Call deferred function
    add sp, 2
    
    ; Continue processing
    jmp short .process_loop
    
.queue_empty:
    mov byte [work_pending], 0           ; Mark queue empty
    EXIT_CRITICAL
    ret
process_work_queue endp

;=============================================================================
; VECTOR OWNERSHIP VALIDATION
;=============================================================================

public validate_interrupt_vectors
validate_interrupt_vectors proc
    ; Periodic validation of owned vectors
    ; Should be called from timer interrupt or main loop
    
    dec word [vector_check_timer]
    jnz .skip_check                      ; Not time to check yet
    
    ; Reset timer
    mov word [vector_check_timer], 1000
    
    ; Check INT 60h (packet driver API)
    CHECK_VECTOR_OWNERSHIP 60h, packet_api_entry
    jnc .vectors_ok
    
    ; Vector stolen - attempt recovery
    call emergency_vector_recovery
    
.vectors_ok:
.skip_check:
    ret
validate_interrupt_vectors endp

emergency_vector_recovery proc
    ; Attempt to reclaim stolen vectors
    push ax
    push dx
    
    inc word [recovery_count]
    
    ; Log the event
    mov al, 0FFh                         ; Recovery event code
    call log_error_event
    
    ; Try to reinstall packet API vector
    push ds
    mov ax, cs
    mov ds, ax
    mov dx, offset packet_api_entry      ; Will be set by main driver
    mov ax, 2560h                        ; Set INT 60h
    int 21h
    pop ds
    
    pop dx
    pop ax
    ret
emergency_vector_recovery endp

;=============================================================================
; SAFE MEMORY ALLOCATION WITH PROTECTION
;=============================================================================

public allocate_protected_memory
allocate_protected_memory proc
    ; CX = requested size
    ; Returns: BX = protected pointer, CY = error
    
    push ax
    push cx
    
    ; Add space for canaries
    add cx, 8                            ; 4 bytes front + 4 bytes rear
    
    ; Allocate memory (using DOS or XMS - implementation specific)
    call allocate_raw_memory
    jc .allocation_failed
    
    ; Place protection canaries
    PLACE_FRONT_CANARY bx
    PLACE_REAR_CANARY bx, cx
    
    ; Adjust pointer past front canary
    add bx, 4
    
    pop cx
    pop ax
    clc
    ret
    
.allocation_failed:
    pop cx
    pop ax
    stc
    ret
allocate_protected_memory endp

public validate_protected_memory
validate_protected_memory proc
    ; BX = protected pointer, CX = original size
    ; Returns: CY set if corruption detected
    
    push ax
    
    ; Check canaries
    CHECK_CANARIES bx, cx
    jc .corruption_detected
    
    pop ax
    clc
    ret
    
.corruption_detected:
    ; Log corruption event
    mov al, 0xFE                         ; Memory corruption code
    call log_error_event
    
    pop ax
    stc
    ret
validate_protected_memory endp

;=============================================================================
; HARDWARE I/O WITH TIMEOUT PROTECTION
;=============================================================================

public safe_port_read
safe_port_read proc
    ; DX = port, CX = timeout
    ; Returns: AL = value, CY = timeout
    
    push cx
    
.wait_loop:
    in al, dx
    test al, al                          ; Simple responsiveness check
    jnz .port_ready
    loop .wait_loop
    
    ; Timeout occurred
    pop cx
    mov al, 0xFD                         ; Hardware timeout code  
    call log_error_event
    stc
    ret
    
.port_ready:
    pop cx
    clc
    ret
safe_port_read endp

public safe_port_write
safe_port_write proc
    ; DX = port, AL = value, CX = timeout for readback verification
    ; Returns: CY = error
    
    push ax
    push cx
    
    ; Write to port
    out dx, al
    
    ; Verify write (if port supports readback)
    mov cx, TIMEOUT_SHORT
    call verify_port_write
    
    pop cx
    pop ax
    ret
safe_port_write endp

verify_port_write proc
    ; Simple verification - implementation depends on hardware
    ; For now, just a short delay
    push cx
    mov cx, 100
.delay_loop:
    nop
    loop .delay_loop
    pop cx
    
    clc                                  ; Assume success
    ret
verify_port_write endp

;=============================================================================
; ERROR HANDLING AND LOGGING
;=============================================================================

log_error_event proc
    ; AL = error code
    ; Simple error logging - stores last error for diagnostics
    
    push ax
    
    mov [last_error_code], al
    inc word [error_count]
    
    ; In a full implementation, this would write to a circular buffer
    ; For now, just update counters
    
    pop ax
    ret
log_error_event endp

;=============================================================================
; UNINSTALL SAFETY CHECKS
;=============================================================================

check_uninstall_safety proc
    ; Returns: CY set if cannot uninstall safely
    
    ; Check if any applications still have access types registered
    ; This is driver-specific - placeholder for now
    
    ; Check if hardware is still active
    ; This is hardware-specific - placeholder for now
    
    ; For now, assume we can always uninstall
    clc
    ret
check_uninstall_safety endp

;=============================================================================
; PRIVATE API ENTRY POINT
;=============================================================================

private_api_entry proc far
    ; Private API for advanced applications
    ; AH = function code, other registers = parameters
    
    SAFE_STACK_SWITCH
    
    pusha
    push ds
    push es
    
    mov ax, cs
    mov ds, ax
    
    ; Validate function code
    cmp ah, 80h
    jb .invalid_function
    cmp ah, 8Fh
    ja .invalid_function
    
    ; Dispatch to private function handlers
    mov al, ah
    sub al, 80h                          ; Convert to 0-based
    xor ah, ah
    mov bx, ax
    shl bx, 1
    call word ptr [private_function_table + bx]
    
    pop es
    pop ds
    popa
    
    RESTORE_CALLER_STACK
    retf
    
.invalid_function:
    mov ah, BAD_COMMAND
    stc
    
    pop es
    pop ds
    popa
    
    RESTORE_CALLER_STACK
    retf
private_api_entry endp

; Private function dispatch table
private_function_table:
    dw get_driver_statistics             ; Function 80h
    dw reset_driver_statistics           ; Function 81h
    dw get_hardware_status               ; Function 82h
    dw set_debug_level                   ; Function 83h
    dw get_memory_usage                  ; Function 84h
    dw validate_driver_integrity         ; Function 85h
    ; ... additional private functions

;=============================================================================
; PRIVATE API FUNCTION IMPLEMENTATIONS
;=============================================================================

get_driver_statistics proc
    ; Return driver statistics
    ; ES:DI -> statistics buffer (provided by caller)
    
    ; Copy statistics to caller's buffer
    push si
    push cx
    
    mov si, offset error_count
    mov cx, 8                            ; Copy 8 bytes of statistics
    rep movsb
    
    pop cx
    pop si
    
    clc
    ret
get_driver_statistics endp

reset_driver_statistics proc
    ; Reset error counters
    
    mov word [error_count], 0
    mov word [recovery_count], 0
    mov byte [last_error_code], 0
    
    clc
    ret
reset_driver_statistics endp

get_hardware_status proc
    ; Return hardware status
    ; Implementation specific to actual hardware
    
    ; Placeholder - return generic "OK" status
    mov ax, 0x0100                       ; Status: OK, version 1.0
    clc
    ret
get_hardware_status endp

set_debug_level proc
    ; Set debug output level
    ; AL = debug level (0=off, 1=errors, 2=warnings, 3=info, 4=verbose)
    
    ; Store debug level (implementation specific)
    ; For now, just acknowledge the command
    
    clc
    ret
set_debug_level endp

get_memory_usage proc
    ; Return memory usage statistics  
    ; ES:DI -> memory usage buffer
    
    ; Return basic memory information
    ; This would need to integrate with actual memory manager
    
    clc
    ret
get_memory_usage endp

public validate_driver_integrity
validate_driver_integrity proc
    ; Comprehensive driver integrity check
    ; Returns: CY set if corruption detected
    
    ; Check our data structures
    ; Validate vector ownership
    call validate_interrupt_vectors
    
    ; Check memory canaries (if any are active)
    ; This would iterate through protected allocations
    
    ; Validate critical data structures
    ; Implementation specific to driver data
    
    ; For now, assume integrity is OK
    clc
    ret
validate_driver_integrity endp

;=============================================================================
; UTILITY ROUTINES
;=============================================================================

allocate_raw_memory proc
    ; CX = size to allocate
    ; Returns: BX = pointer, CY = error
    ; 
    ; This is a placeholder - actual implementation would:
    ; 1. Try UMB allocation first
    ; 2. Fall back to XMS if available  
    ; 3. Fall back to conventional memory as last resort
    
    ; For now, return error (not implemented)
    stc
    ret
allocate_raw_memory endp

;=============================================================================
; UMB SUPPORT FUNCTIONS (DOS 5.0+)
;=============================================================================

public detect_memory_managers
detect_memory_managers proc
    ; Detect available memory managers for UMB support
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    
    ; Reset memory manager type
    mov byte [memory_manager_type], 0
    
    ; Check for HIMEM.SYS
    call detect_himem
    jnc .himem_found
    
    ; Check for EMM386
    call detect_emm386
    jnc .emm386_found
    
    ; Check for QEMM386
    call detect_qemm
    jnc .qemm_found
    
    ; No memory manager found
    jmp short .no_manager
    
.himem_found:
    mov byte [memory_manager_type], 1
    jmp short .manager_detected
    
.emm386_found:
    mov byte [memory_manager_type], 2
    jmp short .manager_detected
    
.qemm_found:
    mov byte [memory_manager_type], 3
    
.manager_detected:
    ; Check DOS version for UMB support (need DOS 5.0+)
    mov ah, 30h
    int 21h
    cmp al, 5                       ; DOS version 5.0 or higher
    jae .dos_ok
    
    mov byte [memory_manager_type], 0   ; Disable if DOS too old
    
.dos_ok:
.no_manager:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
detect_memory_managers endp

detect_himem proc
    ; Check for HIMEM.SYS presence
    push ds
    push si
    push di
    push es
    
    ; Search for HIMEM signature in device chain
    mov ax, 3800h                   ; Get country info
    int 21h
    jc .not_found
    
    ; Alternative: Check XMS driver
    mov ax, 4300h                   ; XMS installation check
    int 2Fh
    cmp al, 80h
    jne .not_found
    
    clc                             ; HIMEM found
    jmp short .exit
    
.not_found:
    stc                             ; Not found
    
.exit:
    pop es
    pop di
    pop si
    pop ds
    ret
detect_himem endp

detect_emm386 proc
    ; Check for EMM386.EXE
    push ax
    push bx
    push dx
    
    ; Try to access EMM386 VxD
    mov ax, 1600h                   ; Enhanced mode check
    int 2Fh
    cmp al, 0
    je .not_found
    cmp al, 80h
    je .not_found
    
    ; EMM386 likely present
    clc
    jmp short .exit
    
.not_found:
    stc
    
.exit:
    pop dx
    pop bx
    pop ax
    ret
detect_emm386 endp

detect_qemm proc
    ; Check for QEMM386
    push ax
    push bx
    push dx
    
    ; QEMM specific detection
    mov ax, 5945h                   ; QEMM API check
    int 2Fh
    cmp ax, 5945h
    je .not_found
    
    ; QEMM found
    clc
    jmp short .exit
    
.not_found:
    stc
    
.exit:
    pop dx
    pop bx
    pop ax
    ret
detect_qemm endp

public check_umb_availability
check_umb_availability proc
    ; Check if UMBs are available and can be used
    push ax
    push bx
    
    ; First detect memory managers
    call detect_memory_managers
    
    ; Check if any memory manager found
    cmp byte [memory_manager_type], 0
    je .not_available
    
    ; Try to get current allocation strategy
    mov ax, 5800h                   ; Get allocation strategy  
    int 21h
    jc .not_available
    
    mov [original_alloc_strategy], al
    
    ; Try to get UMB link state
    mov ax, 5802h                   ; Get UMB link state
    int 21h
    jc .not_available
    
    ; UMBs are available
    mov byte [umb_available], 1
    clc
    jmp short .exit
    
.not_available:
    mov byte [umb_available], 0
    stc
    
.exit:
    pop bx
    pop ax
    ret
check_umb_availability endp

public attempt_umb_allocation
attempt_umb_allocation proc
    ; Try to allocate UMB for TSR
    ; CX = required size in paragraphs
    ; Returns: AX = allocated segment (0 if failed), CY = status
    
    push bx
    push cx
    push dx
    
    ; Check if already attempted
    cmp byte [umb_allocation_attempted], 1
    je .already_attempted
    
    ; Mark as attempted
    mov byte [umb_allocation_attempted], 1
    
    ; Check UMB availability
    call check_umb_availability
    jc .allocation_failed
    
    ; Link UMBs into memory chain
    call link_umbs
    jc .allocation_failed
    
    ; Set allocation strategy to try UMBs first
    mov ax, 5801h                   ; Set allocation strategy
    mov bx, 0081h                   ; Best fit high memory first
    int 21h
    jc .allocation_failed
    
    ; Try to allocate UMB
    mov ah, 48h                     ; Allocate memory
    mov bx, cx                      ; Size in paragraphs
    int 21h
    jc .allocation_failed
    
    ; Check if allocated segment is in UMB range (A000h-FFFFh)
    cmp ax, 0A000h
    jb .conventional_allocated
    
    ; Successfully allocated UMB
    mov [umb_segment], ax
    mov [umb_size_paragraphs], cx
    clc                             ; Success
    jmp short .exit
    
.conventional_allocated:
    ; Got conventional memory instead - still usable but not optimal
    mov [umb_segment], ax
    mov [umb_size_paragraphs], cx
    mov byte [conventional_fallback], 1
    clc                             ; Success (but conventional)
    jmp short .exit
    
.already_attempted:
    ; Return previously allocated segment
    mov ax, [umb_segment]
    cmp ax, 0
    jz .allocation_failed
    clc
    jmp short .exit
    
.allocation_failed:
    ; Restore original allocation strategy
    call restore_allocation_strategy
    xor ax, ax                      ; No segment allocated
    stc                             ; Failure
    
.exit:
    pop dx
    pop cx
    pop bx
    ret
attempt_umb_allocation endp

link_umbs proc
    ; Link UMBs into the DOS memory chain
    push ax
    push bx
    
    ; Check if already linked
    cmp byte [umb_linked], 1
    je .already_linked
    
    ; Link UMBs 
    mov ax, 5803h                   ; Link/unlink UMB
    mov bx, 1                       ; Link UMBs
    int 21h
    jc .link_failed
    
    mov byte [umb_linked], 1
    clc
    jmp short .exit
    
.already_linked:
    clc                             ; Success
    jmp short .exit
    
.link_failed:
    stc
    
.exit:
    pop bx
    pop ax
    ret
link_umbs endp

public restore_allocation_strategy
restore_allocation_strategy proc
    ; Restore original DOS allocation strategy
    push ax
    push bx
    
    ; Restore allocation strategy
    mov ax, 5801h                   ; Set allocation strategy
    mov bl, [original_alloc_strategy]
    xor bh, bh
    int 21h
    
    pop bx
    pop ax
    ret
restore_allocation_strategy endp

public relocate_tsr_to_umb
relocate_tsr_to_umb proc
    ; Relocate TSR code/data to allocated UMB
    ; BX = source segment, CX = size in paragraphs
    ; Returns: AX = destination segment, CY = status
    
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    
    ; Check if we have a UMB
    mov ax, [umb_segment]
    cmp ax, 0
    jz .no_umb
    
    ; Set up for memory copy
    mov ds, bx                      ; Source segment
    mov es, ax                      ; Destination segment (UMB)
    xor si, si                      ; Source offset
    xor di, di                      ; Destination offset
    
    ; Calculate number of bytes to copy
    mov ax, cx                      ; Paragraphs
    mov cl, 4                       ; Shift count
    shl ax, cl                      ; Convert to bytes
    mov cx, ax                      ; Bytes to copy
    
    ; Copy TSR to UMB
    cld
    rep movsb                       ; Copy byte by byte
    
    ; Success
    mov ax, [umb_segment]           ; Return UMB segment
    clc
    jmp short .exit
    
.no_umb:
    ; No UMB allocated
    xor ax, ax
    stc
    
.exit:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
relocate_tsr_to_umb endp

public get_memory_usage_info
get_memory_usage_info proc
    ; Get information about memory usage
    ; ES:DI -> buffer for memory usage structure
    ; Structure: umb_available(1), umb_segment(2), umb_size(2), 
    ;           conventional_fallback(1), memory_manager_type(1)
    
    push ax
    
    ; UMB available flag
    mov al, [umb_available]
    mov es:[di], al
    
    ; UMB segment
    mov ax, [umb_segment]
    mov es:[di+1], ax
    
    ; UMB size in paragraphs
    mov ax, [umb_size_paragraphs]
    mov es:[di+3], ax
    
    ; Conventional fallback flag
    mov al, [conventional_fallback]
    mov es:[di+5], al
    
    ; Memory manager type
    mov al, [memory_manager_type]
    mov es:[di+6], al
    
    pop ax
    ret
get_memory_usage_info endp

public cleanup_umb_allocation
cleanup_umb_allocation proc
    ; Clean up UMB resources during uninstall
    push ax
    push bx
    push es
    
    ; Free UMB if allocated
    mov ax, [umb_segment]
    cmp ax, 0
    jz .no_umb_to_free
    
    mov es, ax                      ; Segment to free
    mov ah, 49h                     ; Free memory
    int 21h
    
    ; Clear UMB info
    mov word [umb_segment], 0
    mov word [umb_size_paragraphs], 0
    
.no_umb_to_free:
    ; Restore original allocation strategy
    call restore_allocation_strategy
    
    ; Clear flags
    mov byte [umb_available], 0
    mov byte [umb_linked], 0
    mov byte [umb_allocation_attempted], 0
    mov byte [conventional_fallback], 0
    mov byte [memory_manager_type], 0
    
    pop es
    pop bx
    pop ax
    ret
cleanup_umb_allocation endp

; External references that will be provided by main driver
extern packet_api_entry:far             ; Main packet API entry point

end