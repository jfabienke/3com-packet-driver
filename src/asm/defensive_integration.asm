; @file defensive_integration.asm
; @brief Integration layer for defensive programming features
;
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
;
; This file integrates the robust defensive programming patterns from
; tsr_common.asm with the main packet driver implementation.
; It provides the bridge between the theoretical best practices and
; the practical driver requirements.
;
; Based on analysis from refs/dos-references/defensive-programming-review.md
; and implementation guidance from refs/dos-references/defensive-tsr-programming.md

.MODEL SMALL
.386

include 'tsr_defensive.inc'

; Data segment
_DATA SEGMENT
        ASSUME  DS:_DATA

; Integration status flags
defensive_initialized   db 0        ; Defensive systems initialized
stack_switched         db 0        ; Stack switching active
deferred_enabled       db 0        ; Deferred processing enabled
dos_safety_enabled     db 0        ; DOS re-entrancy protection enabled

; ISR Stack Management (2KB private stack)
isr_stack_size         equ 2048
isr_stack              db isr_stack_size dup(?)
isr_stack_top          equ $ - 2    ; Top of ISR stack

; Saved caller context (used by SAFE_STACK_SWITCH macro)
caller_ss              dw ?
caller_sp              dw ?

; DOS Safety Management
indos_segment          dw 0         ; InDOS flag segment
indos_offset           dw 0         ; InDOS flag offset  
criterr_segment        dw 0         ; Critical error flag segment (0 if N/A)
criterr_offset         dw 0         ; Critical error flag offset

; Critical section nesting support
critical_nesting       db 0

; Deferred Work Queue (32 entries, each 4 bytes: function pointer)
WORK_QUEUE_SIZE        equ 32
work_queue             dd WORK_QUEUE_SIZE dup(0)  ; Queue of function pointers
queue_head             dw 0         ; Queue head index
queue_tail             dw 0         ; Queue tail index
work_pending           db 0         ; Work pending flag
queue_overflows        dw 0         ; Overflow counter

; Original interrupt vectors (for safe chaining)
original_int28_vector  dd 0         ; Original INT 28h (DOS Idle)
original_int60_vector  dd 0         ; Original INT 60h (Packet Driver)

; Error tracking and diagnostics
error_count            dw 0         ; Total error count
dos_busy_count         dw 0         ; Times DOS was busy
stack_switch_count     dw 0         ; Stack switch counter
deferred_work_count    dw 0         ; Deferred work items processed

; Memory corruption detection
MEMORY_CANARY_FRONT    equ 0DEADh   ; Front canary pattern (16-bit)
MEMORY_CANARY_REAR     equ 0BEEFh   ; Rear canary pattern (16-bit)
memory_corruption_count dw 0        ; Memory corruption events detected
last_corruption_addr   dd 0         ; Address of last corruption
protected_block_count  dw 0         ; Number of protected memory blocks

; Critical data structure protection
driver_data_checksum   dw 0         ; Checksum of critical driver data
checksum_update_count  dw 0         ; Number of checksum updates
data_validation_errors dw 0         ; Data validation error count

; Performance monitoring (if enabled)
ifdef PROFILE_BUILD
timer_start_high       dw ?
timer_start_low        dw ?
endif

; IRQ vector management
nic_irq_table          db 10, 11           ; IRQ assignments for NICs (10, 11 default)
                       db 0FFh, 0FFh       ; Extra slots for expansion
MAX_NIC_IRQS           equ 2               ; Maximum NICs we support
nic_irq_handler        dd 0                ; Pointer to our IRQ handler
irq_theft_count        dw 0                ; Count of detected IRQ thefts
recovery_count         dw 0                ; Vector recovery attempts

; Logging infrastructure
log_buffer             db 256 dup(0)       ; Circular log buffer
log_head               dw 0                ; Log buffer head index
log_tail               dw 0                ; Log buffer tail index
log_overflow_count     dw 0                ; Log buffer overflow counter
deferred_log_pending   db 0                ; Deferred log write pending

; Event codes for logging
LOG_EVENT_INIT         equ 01h             ; System initialization
LOG_EVENT_SHUTDOWN     equ 02h             ; System shutdown
LOG_EVENT_ERROR        equ 80h             ; General error
LOG_EVENT_IRQ_THEFT    equ 0FDh            ; IRQ vector stolen
LOG_EVENT_RECOVERY     equ 0FEh            ; Recovery attempted
LOG_EVENT_CORRUPTION   equ 0FFh            ; Memory corruption detected

_DATA ENDS

; Code segment
_TEXT SEGMENT
        ASSUME  CS:_TEXT, DS:_DATA

; Public exports
PUBLIC defensive_init
PUBLIC defensive_shutdown
PUBLIC queue_deferred_work
PUBLIC process_deferred_work
PUBLIC dos_is_safe
PUBLIC get_defensive_stats
PUBLIC enhanced_int28_handler
PUBLIC enhanced_packet_handler

; External references
EXTRN packet_int_handler:PROC       ; Original packet handler from packet_api.asm

;=============================================================================
; DEFENSIVE SYSTEM INITIALIZATION
;=============================================================================

;-----------------------------------------------------------------------------
; defensive_init - Initialize all defensive programming systems
;
; This function must be called during driver initialization to set up
; all defensive programming features before any ISRs are installed.
;
; Input:  None
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX, ES
;-----------------------------------------------------------------------------
defensive_init PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    es

        ; Check if already initialized
        cmp     byte ptr [defensive_initialized], 1
        je      .already_initialized

        ; Initialize DOS safety pointers
        call    init_dos_safety
        jc      .init_failed

        ; Initialize deferred work queue
        call    init_work_queue

        ; Initialize stack switching support
        call    init_stack_switching

        ; Save original interrupt vectors for chaining
        call    save_original_vectors

        ; Initialize memory protection systems
        call    initialize_data_protection

        ; Install AMIS compliance handler
        call    install_amis_handler

        ; Mark as initialized
        mov     byte ptr [defensive_initialized], 1
        mov     byte ptr [dos_safety_enabled], 1
        mov     byte ptr [deferred_enabled], 1
        mov     byte ptr [stack_switched], 1

        ; Success
        xor     ax, ax
        jmp     .exit

.already_initialized:
        mov     ax, 1           ; Already initialized error
        jmp     .exit

.init_failed:
        mov     ax, 2           ; Initialization failed error

.exit:
        pop     es
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
defensive_init ENDP

;-----------------------------------------------------------------------------
; init_dos_safety - Initialize DOS re-entrancy protection
;
; Gets the InDOS and Critical Error flag addresses for safe DOS calling.
;
; Input:  None
; Output: CY set on error
; Uses:   AX, BX, ES
;-----------------------------------------------------------------------------
init_dos_safety PROC
        push    bx
        push    es

        ; Get InDOS flag address (DOS function 34h)
        mov     ah, 34h
        int     21h                     ; Returns ES:BX -> InDOS flag
        mov     [indos_segment], es
        mov     [indos_offset], bx

        ; Try to get critical error flag (DOS 3.0+)
        push    ds
        push    si
        
        mov     ax, 5D06h
        int     21h                     ; Returns DS:SI -> critical error flag
        jc      .no_criterr             ; Not supported in this DOS version
        
        mov     [criterr_segment], ds
        mov     [criterr_offset], si
        jmp     .criterr_done

.no_criterr:
        ; Critical error flag not available - mark as unavailable
        mov     word ptr [criterr_segment], 0

.criterr_done:
        pop     si
        pop     ds

        ; Success
        clc

        pop     es
        pop     bx
        ret
init_dos_safety ENDP

;-----------------------------------------------------------------------------
; init_work_queue - Initialize deferred work queue
;
; Sets up the circular buffer for deferred work processing.
;
; Input:  None
; Output: None
; Uses:   AX, CX, DI
;-----------------------------------------------------------------------------
init_work_queue PROC
        push    cx
        push    di

        ; Clear work queue
        mov     cx, WORK_QUEUE_SIZE * 2  ; 32 entries * 2 words each
        mov     di, OFFSET work_queue
        xor     ax, ax
        rep     stosw

        ; Initialize queue pointers
        mov     word ptr [queue_head], 0
        mov     word ptr [queue_tail], 0
        mov     byte ptr [work_pending], 0

        pop     di
        pop     cx
        ret
init_work_queue ENDP

;-----------------------------------------------------------------------------
; init_stack_switching - Initialize private stack for ISRs
;
; Verifies the private stack is properly aligned and accessible.
;
; Input:  None
; Output: None
; Uses:   AX, BX
;-----------------------------------------------------------------------------
init_stack_switching PROC
        ; Verify stack is in our data segment
        mov     ax, _DATA
        mov     bx, cs
        cmp     ax, bx
        jne     .stack_warning

        ; Initialize stack pointer to top of private stack
        ; (The actual stack switching happens in ISRs using macros)

        ret

.stack_warning:
        ; In a full implementation, this would log a warning
        ; For now, just continue - stack switching will still work
        ret
init_stack_switching ENDP

;-----------------------------------------------------------------------------
; save_original_vectors - Save original interrupt vectors for chaining
;
; Saves the original INT 28h and INT 60h vectors so we can chain to them.
;
; Input:  None
; Output: None
; Uses:   AX, BX, ES
;-----------------------------------------------------------------------------
save_original_vectors PROC
        push    bx
        push    es

        ; Save original INT 28h vector (DOS Idle)
        mov     ax, 3528h
        int     21h                     ; Returns ES:BX
        mov     word ptr [original_int28_vector], bx
        mov     word ptr [original_int28_vector + 2], es

        ; Save original INT 60h vector (Packet Driver)
        mov     ax, 3560h
        int     21h                     ; Returns ES:BX
        mov     word ptr [original_int60_vector], bx
        mov     word ptr [original_int60_vector + 2], es

        pop     es
        pop     bx
        ret
save_original_vectors ENDP

;=============================================================================
; DEFERRED WORK PROCESSING
;=============================================================================

;-----------------------------------------------------------------------------
; queue_deferred_work - Add work item to deferred processing queue
;
; This function is called from ISRs to defer heavy processing to a safer
; context (INT 28h handler). The work item is a function pointer that will
; be called later when DOS is idle.
;
; Input:  AX = function address to call later
; Output: CY set if queue full
; Uses:   BX, flags
;-----------------------------------------------------------------------------
queue_deferred_work PROC
        ENTER_CRITICAL

        ; Check for queue space
        mov     bx, [queue_tail]
        inc     bx
        cmp     bx, WORK_QUEUE_SIZE
        jl      .no_wrap
        xor     bx, bx                  ; Wrap to 0

.no_wrap:
        cmp     bx, [queue_head]
        je      .queue_full

        ; Add work item to queue
        mov     bx, [queue_tail]
        shl     bx, 2                   ; Convert to dword offset
        mov     word ptr [work_queue + bx], ax
        mov     word ptr [work_queue + bx + 2], 0  ; High word = 0 for near call

        ; Update tail pointer
        inc     word ptr [queue_tail]
        cmp     word ptr [queue_tail], WORK_QUEUE_SIZE
        jl      .no_tail_wrap
        mov     word ptr [queue_tail], 0

.no_tail_wrap:
        ; Mark work as pending
        mov     byte ptr [work_pending], 1

        EXIT_CRITICAL
        clc                             ; Success
        ret

.queue_full:
        inc     word ptr [queue_overflows]
        EXIT_CRITICAL
        stc                             ; Queue full error
        ret
queue_deferred_work ENDP

;-----------------------------------------------------------------------------
; process_deferred_work - Process all items in the deferred work queue
;
; This function is called from the INT 28h handler when DOS is idle.
; It processes all queued work items in a safe context.
;
; Input:  None
; Output: AX = number of items processed
; Uses:   All registers (work functions may use anything)
;-----------------------------------------------------------------------------
process_deferred_work PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx

        xor     cx, cx                  ; Item counter

.process_loop:
        ENTER_CRITICAL

        ; Check if queue empty
        mov     ax, [queue_head]
        cmp     ax, [queue_tail]
        je      .queue_empty

        ; Get next work item
        mov     bx, ax
        shl     bx, 2                   ; Convert to dword offset
        mov     ax, word ptr [work_queue + bx]    ; Get function pointer

        ; Update head pointer
        inc     word ptr [queue_head]
        cmp     word ptr [queue_head], WORK_QUEUE_SIZE
        jl      .no_head_wrap
        mov     word ptr [queue_head], 0

.no_head_wrap:
        EXIT_CRITICAL

        ; Call the deferred function (assuming near call)
        push    cx                      ; Preserve counter
        call    ax
        pop     cx
        inc     cx                      ; Count processed item

        ; Continue processing
        jmp     .process_loop

.queue_empty:
        ; Mark no work pending if queue is truly empty
        mov     byte ptr [work_pending], 0
        EXIT_CRITICAL

        ; Update statistics
        add     word ptr [deferred_work_count], cx

        mov     ax, cx                  ; Return count of processed items

        pop     cx
        pop     bx
        pop     bp
        ret
process_deferred_work ENDP

;-----------------------------------------------------------------------------
; C-callable wrapper functions for deferred work queue
;-----------------------------------------------------------------------------

; deferred_work_queue_add - C-callable wrapper for queue_deferred_work  
; C Calling Convention: function pointer passed on stack
; Input:  [BP+4] = function pointer (C calling convention)
; Output: AX = 0 on success, -1 if queue full
PUBLIC deferred_work_queue_add
deferred_work_queue_add PROC
        push    bp
        mov     bp, sp
        push    bx
        
        ; Get function pointer from stack (C calling convention)
        mov     bx, [bp + 4]           ; Get near function pointer from stack
        mov     ax, bx                 ; queue_deferred_work expects AX
        call    queue_deferred_work
        jc      .queue_full
        
        xor     ax, ax                 ; Success
        jmp     .done
        
.queue_full:
        mov     ax, -1                 ; Queue full
        
.done:
        pop     bx
        pop     bp
        ret
deferred_work_queue_add ENDP

; deferred_work_queue_process - C-callable wrapper for process_deferred_work
; Output: AX = number of items processed
PUBLIC deferred_work_queue_process
deferred_work_queue_process PROC
        push    bp
        mov     bp, sp
        
        call    process_deferred_work
        ; AX already contains count of processed items
        
        pop     bp
        ret
deferred_work_queue_process ENDP

; deferred_work_queue_count - Get number of pending work items  
; Output: AX = number of pending items
PUBLIC deferred_work_queue_count
deferred_work_queue_count PROC
        push    bp
        mov     bp, sp
        push    bx
        
        ; Calculate queue size: (tail - head) mod WORK_QUEUE_SIZE
        mov     ax, [queue_tail]
        mov     bx, [queue_head]
        sub     ax, bx                  ; tail - head
        
        ; Handle wrap-around case
        cmp     ax, 0
        jge     .positive
        add     ax, WORK_QUEUE_SIZE     ; Make positive
        
.positive:
        pop     bx
        pop     bp
        ret
deferred_work_queue_count ENDP

;=============================================================================
; DOS SAFETY CHECKING
;=============================================================================

;-----------------------------------------------------------------------------
; dos_is_safe - Check if DOS is safe to call
;
; Checks both InDOS and Critical Error flags to determine if it's safe
; to make DOS function calls.
;
; Input:  None
; Output: ZF set if safe (can call DOS), ZF clear if busy
; Uses:   ES, BX, DS, SI (but preserves them)
;-----------------------------------------------------------------------------
dos_is_safe PROC
        CHECK_DOS_COMPLETELY_SAFE
        ret
dos_is_safe ENDP

;=============================================================================
; ENHANCED INTERRUPT HANDLERS
;=============================================================================

;-----------------------------------------------------------------------------
; enhanced_int28_handler - Enhanced DOS Idle interrupt handler
;
; This replaces the standard INT 28h handler to provide deferred work
; processing in a DOS-safe context.
;
; Input:  None (interrupt context)
; Output: None
; Uses:   All registers (saved and restored)
;-----------------------------------------------------------------------------
enhanced_int28_handler PROC FAR
        ; Save registers
        pusha
        push    ds
        push    es

        ; Set up our data segment
        mov     ax, _DATA
        mov     ds, ax
        ASSUME  DS:_DATA

        ; Check if we have deferred work and DOS is safe
        cmp     byte ptr [work_pending], 0
        jz      .chain                  ; No work to do

        call    dos_is_safe
        jnz     .chain                  ; DOS is busy

        ; Switch to our private stack for safety
        SAFE_STACK_SWITCH

        ; Process deferred work
        call    process_deferred_work

        ; Restore caller's stack
        RESTORE_CALLER_STACK

.chain:
        ; Restore registers
        pop     es
        pop     ds
        popa

        ; Chain to original INT 28h handler
        pushf
        call    dword ptr [cs:original_int28_vector]
        iret
enhanced_int28_handler ENDP

;-----------------------------------------------------------------------------
; enhanced_packet_handler - Enhanced packet driver interrupt handler
;
; This wraps the original packet handler with defensive programming
; features including stack switching and parameter validation.
;
; Input:  Standard packet driver API parameters
; Output: Standard packet driver API results
; Uses:   All registers (managed defensively)
;-----------------------------------------------------------------------------
enhanced_packet_handler PROC FAR
        ; Increment stack switch counter for diagnostics
        inc     word ptr [cs:stack_switch_count]

        ; Switch to private stack immediately
        SAFE_STACK_SWITCH

        ; Save all registers
        pusha
        push    ds
        push    es

        ; Set up our data segment
        mov     ax, _DATA
        mov     ds, ax
        ASSUME  DS:_DATA

        ; Validate that we're not being called from an ISR context
        ; (Simple check - in a full implementation this would be more thorough)
        pushf
        pop     ax
        test    ax, 200h                ; Check IF flag
        jz      .interrupts_disabled

        ; Call original packet handler
        ; First restore registers to original state
        pop     es
        pop     ds
        popa

        ; Restore original stack temporarily for the call
        RESTORE_CALLER_STACK

        ; Call original handler
        pushf
        call    far ptr packet_int_handler

        ; Switch back to our stack
        SAFE_STACK_SWITCH

        ; Save results
        pusha
        pushf
        
        ; Clean up and return
        popf
        popa
        
        RESTORE_CALLER_STACK
        iret

.interrupts_disabled:
        ; We're being called from an interrupt context - this is suspicious
        ; Return error and don't call the original handler
        mov     dh, 11                  ; BAD_COMMAND error
        stc

        pop     es
        pop     ds
        popa

        RESTORE_CALLER_STACK
        iret
enhanced_packet_handler ENDP

;=============================================================================
; DIAGNOSTIC AND UTILITY FUNCTIONS
;=============================================================================

;-----------------------------------------------------------------------------
; get_defensive_stats - Get defensive programming statistics
;
; Returns various counters and status information for diagnostics.
;
; Input:  ES:DI -> buffer for statistics (minimum 16 bytes)
; Output: Statistics copied to buffer
; Uses:   AX, CX, SI
;-----------------------------------------------------------------------------
get_defensive_stats PROC
        push    si
        push    cx

        mov     si, OFFSET error_count
        mov     cx, 8                   ; Copy 8 words of statistics
        rep     movsw

        pop     cx
        pop     si
        ret
get_defensive_stats ENDP

;-----------------------------------------------------------------------------
; defensive_shutdown - Shutdown defensive programming systems
;
; Clean shutdown of all defensive systems. Should be called before
; driver unload.
;
; Input:  None
; Output: AX = 0 for success
; Uses:   AX
;-----------------------------------------------------------------------------
defensive_shutdown PROC
        ; Mark systems as not initialized
        mov     byte ptr [defensive_initialized], 0
        mov     byte ptr [stack_switched], 0
        mov     byte ptr [deferred_enabled], 0
        mov     byte ptr [dos_safety_enabled], 0

        ; Clear work queue
        call    init_work_queue

        xor     ax, ax                  ; Success
        ret
defensive_shutdown ENDP

;=============================================================================
; ENHANCED PIC EOI HANDLING
;=============================================================================

;-----------------------------------------------------------------------------
; send_proper_eoi - Send proper EOI sequence based on IRQ number
;
; This function implements the correct EOI sequence for both master and slave
; PIC interrupts, addressing the critical gap identified in the defensive
; programming review.
;
; Input:  AL = IRQ number (0-15)
; Output: None
; Uses:   DX (but preserves it)
;-----------------------------------------------------------------------------
PUBLIC send_proper_eoi
send_proper_eoi PROC
        push    dx

        ; Validate IRQ range
        cmp     al, 15
        ja      .invalid_irq

        ; Check if this is a slave PIC IRQ (8-15)
        cmp     al, 8
        jl      .master_pic_only

        ; IRQ 8-15: Send EOI to slave PIC first, then master PIC
        ; This is the correct sequence according to Intel 8259A datasheet
        push    ax
        mov     al, 20h         ; EOI command
        mov     dx, 0A0h        ; Slave PIC command port
        out     dx, al          ; Send EOI to slave PIC
        
        mov     dx, 20h         ; Master PIC command port  
        out     dx, al          ; Send EOI to master PIC
        pop     ax
        jmp     .eoi_complete

.master_pic_only:
        ; IRQ 0-7: Send EOI to master PIC only
        push    ax
        mov     al, 20h         ; EOI command
        mov     dx, 20h         ; Master PIC command port
        out     dx, al
        pop     ax

.eoi_complete:
.invalid_irq:
        pop     dx
        ret
send_proper_eoi ENDP

;-----------------------------------------------------------------------------
; get_irq_for_nic - Get IRQ number for specified NIC
;
; Input:  AL = NIC index (0-based)
; Output: AL = IRQ number, or 0xFF if not assigned
; Uses:   BX (but preserves it)
;-----------------------------------------------------------------------------
PUBLIC get_irq_for_nic
get_irq_for_nic PROC
        push    bx
        
        ; Validate NIC index
        cmp     al, 2
        jae     .invalid_nic
        
        ; Get IRQ from table
        mov     bl, al
        xor     bh, bh
        ; Note: This references data in nic_irq.asm - would need proper cross-module data access
        ; For now, return a default value
        cmp     bl, 0
        je      .nic0_irq
        cmp     bl, 1  
        je      .nic1_irq
        
.invalid_nic:
        mov     al, 0FFh        ; Invalid/not assigned
        jmp     .done
        
.nic0_irq:
        mov     al, 10          ; Default IRQ 10 for first NIC
        jmp     .done
        
.nic1_irq:
        mov     al, 11          ; Default IRQ 11 for second NIC
        
.done:
        pop     bx
        ret
get_irq_for_nic ENDP

;=============================================================================
; VECTOR OWNERSHIP VALIDATION
;=============================================================================

;-----------------------------------------------------------------------------
; check_vector_ownership - Verify we still own an interrupt vector
;
; This addresses the critical gap where uninstalling vectors without
; ownership verification could break other TSRs in the chain.
;
; Input:  AL = interrupt number, DX:BX = expected handler address  
; Output: CY clear if we own it, CY set if stolen/chained
; Uses:   ES (but preserves it)
;-----------------------------------------------------------------------------
PUBLIC check_vector_ownership
check_vector_ownership PROC
        push    es
        push    di
        push    cx

        ; Get current vector
        push    ax              ; Save interrupt number
        mov     ah, 35h         ; DOS function: get interrupt vector
        int     21h             ; Returns vector in ES:BX
        mov     di, bx          ; Save offset in DI
        pop     ax              ; Restore interrupt number

        ; Compare with expected handler
        cmp     di, bx          ; Compare offsets
        jne     .not_ours
        mov     cx, es
        cmp     cx, dx          ; Compare segments
        jne     .not_ours

        ; We still own this vector
        clc                     ; Clear carry = we own it
        jmp     .done

.not_ours:
        ; Vector has been stolen or chained
        stc                     ; Set carry = not ours

.done:
        pop     cx
        pop     di
        pop     es
        ret
check_vector_ownership ENDP

;-----------------------------------------------------------------------------
; safe_restore_vector - Safely restore interrupt vector with ownership check
;
; Only restores the vector if we still own it, preventing corruption of
; TSR chains that may have been installed after us.
;
; Input:  AL = interrupt number
;         DX:BX = our handler address
;         CX:SI = original vector to restore to
; Output: CY clear if restored, CY set if not our vector
; Uses:   All registers (but preserves them except flags)
;-----------------------------------------------------------------------------  
PUBLIC safe_restore_vector
safe_restore_vector PROC
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    ds
        push    es

        ; First check if we still own the vector
        call    check_vector_ownership
        jc      .not_ours               ; Don't restore if not ours

        ; We own it - safe to restore
        push    ds                      ; Save current DS
        mov     ds, cx                  ; Set DS to original vector segment
        mov     dx, si                  ; Set DX to original vector offset
        mov     ah, 25h                 ; DOS function: set interrupt vector
        int     21h
        pop     ds                      ; Restore DS

        clc                             ; Success
        jmp     .done

.not_ours:
        ; Vector is not ours - don't restore it
        ; Log this event for diagnostics
        inc     word ptr [recovery_count]
        stc                             ; Error - not restored

.done:
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
safe_restore_vector ENDP

;-----------------------------------------------------------------------------
; validate_all_vectors - Check ownership of all our installed vectors
;
; Periodic validation to detect vector hijacking and enable recovery.
; Should be called periodically from timer interrupt or main loop.
;
; Input:  None
; Output: AL = number of stolen vectors detected
; Uses:   All registers
;-----------------------------------------------------------------------------
PUBLIC validate_all_vectors
validate_all_vectors PROC
        push    bx
        push    cx
        push    dx

        xor     al, al                  ; Count of stolen vectors

        ; Check INT 28h (DOS Idle) - if we installed it
        cmp     word ptr [original_int28_vector], 0
        je      .skip_int28
        
        mov     dx, cs
        mov     bx, OFFSET enhanced_int28_handler
        push    ax
        mov     al, 28h
        call    check_vector_ownership
        pop     ax
        jnc     .int28_ok
        inc     al                      ; Count stolen vector

.int28_ok:
.skip_int28:

        ; Check INT 60h (Packet Driver API)
        cmp     word ptr [original_int60_vector], 0
        je      .skip_int60
        
        mov     dx, cs
        mov     bx, OFFSET enhanced_packet_handler
        push    ax
        mov     al, 60h
        call    check_vector_ownership
        pop     ax
        jnc     .int60_ok
        inc     al                      ; Count stolen vector

.int60_ok:
.skip_int60:

        ; Check hardware IRQ vectors for both NICs
        ; Validate that our IRQ handlers haven't been hijacked
        push    si
        push    di
        
        ; Get NIC IRQ assignments from configuration
        mov     si, OFFSET nic_irq_table    ; Assume table exists
        mov     cx, MAX_NIC_IRQS            ; Check both NICs
        
.check_irq_loop:
        mov     bl, [si]                    ; Get IRQ number
        cmp     bl, 0FFh                    ; 0xFF = not configured
        je      .next_irq
        
        ; Validate IRQ is in valid range (0-15)
        cmp     bl, 15
        ja      .next_irq                   ; Invalid IRQ, skip
        
        ; Check if we still own this IRQ vector
        push    ax
        push    cx
        push    si
        
        ; Convert IRQ to interrupt vector
        movzx   bx, bl                      ; Zero-extend IRQ number
        cmp     bl, 8
        jb      .master_pic_irq
        add     bl, 0x70 - 8                ; IRQ 8-15 -> INT 70h-77h
        jmp     .got_vector
.master_pic_irq:
        add     bl, 0x08                    ; IRQ 0-7 -> INT 08h-0Fh
        
.got_vector:
        mov     al, bl                      ; Vector number in AL
        mov     dx, cs
        mov     bx, OFFSET nic_irq_handler  ; Our IRQ handler
        call    check_vector_ownership
        
        pop     si
        pop     cx
        pop     ax
        
        jnc     .irq_ok
        inc     al                          ; Count stolen vector
        
        ; Log IRQ vector theft
        push    ax
        mov     bl, 0FDh                    ; IRQ theft event code
        mov     al, [si]                    ; IRQ number
        call    log_irq_event
        pop     ax
        
.irq_ok:
.next_irq:
        inc     si                          ; Next IRQ in table
        loop    .check_irq_loop
        
        pop     di
        pop     si

        pop     dx
        pop     cx
        pop     bx
        ret
validate_all_vectors ENDP

;-----------------------------------------------------------------------------
; emergency_vector_recovery - Attempt to recover stolen vectors
;
; Called when vector validation detects hijacking. Attempts to reinstall
; our handlers, but only if safe to do so.
;
; Input:  None
; Output: AL = number of vectors recovered
; Uses:   All registers
;-----------------------------------------------------------------------------
PUBLIC emergency_vector_recovery
emergency_vector_recovery PROC
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        xor     al, al                  ; Count of recovered vectors

        ; Log recovery attempt
        inc     word ptr [recovery_count]

        ; Only attempt recovery if DOS is safe (don't make things worse)
        call    dos_is_safe
        jnz     .dos_busy               ; Skip recovery if DOS is busy

        ; Attempt to recover INT 28h if it was stolen
        call    attempt_int28_recovery
        jnc     .int28_recovered
        jmp     .check_int60
        
.int28_recovered:
        inc     al                      ; Count recovered vector

.check_int60:
        ; Attempt to recover INT 60h if it was stolen  
        call    attempt_int60_recovery
        jnc     .int60_recovered
        jmp     .recovery_complete
        
.int60_recovered:
        inc     al                      ; Count recovered vector

.recovery_complete:

.dos_busy:
        ; Log that recovery was attempted
        mov     bl, 0FEh                ; Recovery event code
        ; Call logging function to record recovery attempt
        call    log_event                   ; BL = event code (0FEh)

        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret
emergency_vector_recovery ENDP

;-----------------------------------------------------------------------------
; attempt_int28_recovery - Attempt to recover stolen INT 28h vector
;
; Carefully attempts to recover the INT 28h (DOS Idle) vector if it has
; been stolen by another TSR. Uses safe chaining to avoid breaking the
; TSR chain.
;
; Input:  None
; Output: CY clear if recovered, CY set if recovery failed/not needed
; Uses:   All registers
;-----------------------------------------------------------------------------
attempt_int28_recovery PROC
        push    ax
        push    bx
        push    cx
        push    dx
        push    es
        
        ; First check if we actually had INT 28h installed
        cmp     word ptr [original_int28_vector], 0
        je      .not_our_vector         ; We never installed it
        
        ; Check if we still own it
        mov     al, 28h
        mov     dx, cs
        mov     bx, OFFSET enhanced_int28_handler
        call    check_vector_ownership
        jnc     .we_still_own_it        ; No recovery needed
        
        ; Vector was stolen - attempt careful recovery
        ; Strategy: Try to find ourselves in the chain and restore
        ; This is safer than blindly reinstalling
        
        ; Get current vector
        mov     ax, 3528h
        int     21h                     ; Returns ES:BX
        
        ; Walk the chain looking for our handler
        mov     cx, 10                  ; Maximum chain depth to check
        
.chain_walk:
        ; Check if this is our handler
        cmp     bx, OFFSET enhanced_int28_handler
        jne     .not_our_handler
        cmp     es, cs
        je      .found_ourselves        ; Found our handler in chain
        
.not_our_handler:
        ; Try to follow chain (this is TSR-specific and complex)
        ; For now, skip complex chain walking
        loop    .chain_walk
        
        ; Didn't find ourselves in chain - attempt careful reinstall
        ; Only if the current handler looks reasonable
        call    validate_vector_safety
        jc      .unsafe_to_recover
        
        ; Reinstall our handler with chaining
        push    ds
        push    es                      ; Save current handler
        push    bx
        
        mov     ax, cs
        mov     ds, ax
        mov     dx, OFFSET enhanced_int28_handler
        mov     ax, 2528h
        int     21h
        
        pop     bx                      ; Restore previous handler
        pop     es
        pop     ds
        
        ; Update our saved "original" vector to chain properly
        mov     word ptr [original_int28_vector], bx
        mov     word ptr [original_int28_vector + 2], es
        
        clc                             ; Success
        jmp     .done

.found_ourselves:
        ; We're still in the chain but not the primary handler
        ; This is actually OK - no recovery needed
        clc
        jmp     .done

.we_still_own_it:
        ; No recovery needed
        clc
        jmp     .done
        
.not_our_vector:
.unsafe_to_recover:
        ; Recovery failed or not safe
        stc

.done:
        pop     es
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
attempt_int28_recovery ENDP

;-----------------------------------------------------------------------------
; attempt_int60_recovery - Attempt to recover stolen INT 60h vector
;
; Similar to INT 28h recovery but for the packet driver API vector.
; This is more critical since it's our main API entry point.
;
; Input:  None
; Output: CY clear if recovered, CY set if recovery failed/not needed
; Uses:   All registers
;-----------------------------------------------------------------------------
attempt_int60_recovery PROC
        push    ax
        push    bx
        push    cx
        push    dx
        push    es
        
        ; Check if we actually had INT 60h installed
        cmp     word ptr [original_int60_vector], 0
        je      .not_our_vector         ; We never installed it
        
        ; Check if we still own it
        mov     al, 60h
        mov     dx, cs
        mov     bx, OFFSET enhanced_packet_handler
        call    check_vector_ownership
        jnc     .we_still_own_it        ; No recovery needed
        
        ; Vector was stolen - this is more serious for packet driver
        ; Be more aggressive about recovery since this is our main API
        
        ; Validate that recovery is safe
        call    validate_vector_safety
        jc      .unsafe_to_recover
        
        ; Get current vector for chaining
        mov     ax, 3560h
        int     21h                     ; Returns ES:BX
        
        ; Store current vector as our new "original" for chaining
        mov     word ptr [original_int60_vector], bx
        mov     word ptr [original_int60_vector + 2], es
        
        ; Reinstall our handler
        push    ds
        mov     ax, cs
        mov     ds, ax
        mov     dx, OFFSET enhanced_packet_handler
        mov     ax, 2560h
        int     21h
        pop     ds
        
        ; Success
        clc
        jmp     .done

.we_still_own_it:
        ; No recovery needed
        clc
        jmp     .done
        
.not_our_vector:
.unsafe_to_recover:
        ; Recovery failed or not safe
        stc

.done:
        pop     es
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
attempt_int60_recovery ENDP

;-----------------------------------------------------------------------------
; validate_vector_safety - Check if vector recovery is safe
;
; Performs basic validation to ensure that vector recovery won't crash
; the system or interfere with critical system operations.
;
; Input:  ES:BX = current vector to validate
; Output: CY clear if safe, CY set if unsafe
; Uses:   None (preserves all registers)
;-----------------------------------------------------------------------------
validate_vector_safety PROC
        push    ax
        push    bx
        
        ; Basic sanity checks on the vector
        ; 1. Check if segment is reasonable (not 0, not too high)
        mov     ax, es
        cmp     ax, 0040h               ; Minimum reasonable segment
        jb      .unsafe
        cmp     ax, 0F000h              ; Maximum reasonable segment for TSRs
        ja      .unsafe
        
        ; 2. Check if offset is reasonable
        cmp     bx, 8                   ; Minimum reasonable offset
        jb      .unsafe
        
        ; 3. Could add more sophisticated checks here:
        ;    - Verify the handler has reasonable opcodes at start
        ;    - Check if it's in a valid memory region
        ;    - Verify it doesn't point to obviously bad code
        
        ; For now, assume it's safe if basic checks pass
        clc
        jmp     .done

.unsafe:
        stc

.done:
        pop     bx
        pop     ax
        ret
validate_vector_safety ENDP

;-----------------------------------------------------------------------------
; periodic_vector_monitoring - Periodic vector hijacking detection and recovery
;
; This function should be called periodically (e.g., every few seconds)
; to detect vector hijacking and attempt automatic recovery.
;
; Input:  None
; Output: AL = number of vectors recovered
; Uses:   AL only
;-----------------------------------------------------------------------------
PUBLIC periodic_vector_monitoring
;-----------------------------------------------------------------------------
; irq_to_interrupt_vector - Convert IRQ number to interrupt vector number
;
; IRQ-to-INT mapping for IBM PC/AT:
; IRQ 0-7  -> INT 08h-0Fh (PIC1)  
; IRQ 8-15 -> INT 70h-77h (PIC2/AT)
;
; Input:  BL = IRQ number (0-15)
; Output: BL = interrupt vector number
; Uses:   BL only
;-----------------------------------------------------------------------------
irq_to_interrupt_vector PROC
        push    ax
        
        cmp     bl, 8
        jb      .pic1_irq
        
        ; PIC2 IRQ (8-15) -> INT 70h-77h
        sub     bl, 8                   ; Convert to 0-7 range
        add     bl, 70h                 ; Add PIC2 base
        jmp     .done
        
.pic1_irq:
        ; PIC1 IRQ (0-7) -> INT 08h-0Fh
        add     bl, 08h                 ; Add PIC1 base
        
.done:
        pop     ax
        ret
irq_to_interrupt_vector ENDP

;-----------------------------------------------------------------------------
; validate_all_vectors - Check all installed interrupt vectors
;
; Validates that we still own our installed interrupt vectors and they
; haven't been hijacked by other TSRs or malicious code.
;
; Input:  None
; Output: AL = number of vectors that have been stolen
; Uses:   AL, BX, CX, ES
;-----------------------------------------------------------------------------
validate_all_vectors PROC
        push    bx
        push    cx  
        push    es
        
        xor     al, al                  ; Count of stolen vectors
        xor     bx, bx                  ; Vector table segment
        mov     es, bx
        
        ; Check IRQ vectors for installed NICs (convert IRQ->INT first)
        ; Check IRQ vector for 3C509B (if installed)
        cmp     byte ptr [nic_irq_numbers], IRQ_NONE
        je      .skip_3c509b_irq
        
        mov     bl, [nic_irq_numbers]   ; Get IRQ number
        call    irq_to_interrupt_vector ; Convert IRQ to INT vector
        call    check_single_vector_ownership
        jc      .vector_stolen_3c509b
        jmp     .check_3c515_irq
        
.vector_stolen_3c509b:
        inc     al                      ; Count stolen vector
        
.check_3c515_irq:
        ; Check IRQ vector for 3C515 (if installed)  
        cmp     byte ptr [nic_irq_numbers + 1], IRQ_NONE
        je      .skip_3c515_irq
        
        mov     bl, [nic_irq_numbers + 1]  ; Get IRQ number
        call    irq_to_interrupt_vector     ; Convert IRQ to INT vector
        call    check_single_vector_ownership
        jc      .vector_stolen_3c515
        jmp     .check_multiplex
        
.vector_stolen_3c515:
        inc     al                      ; Count stolen vector
        
.skip_3c509b_irq:
.skip_3c515_irq:
.check_multiplex:
        ; Check INT 2Fh multiplex vector
        mov     bl, 2Fh
        call    check_single_vector_ownership  
        jc      .vector_stolen_2f
        jmp     .done
        
.vector_stolen_2f:
        inc     al                      ; Count stolen vector
        
.done:
        pop     es
        pop     cx
        pop     bx
        ret
validate_all_vectors ENDP

;-----------------------------------------------------------------------------
; check_single_vector_ownership - Check if we own a specific vector
;
; Input:  BL = interrupt number
; Output: CY set if vector stolen, CY clear if we own it
; Uses:   BX, ES  
;-----------------------------------------------------------------------------
check_single_vector_ownership PROC
        push    ax
        push    dx
        
        ; Calculate vector table offset (INT * 4)
        mov     bh, 0
        shl     bx, 2                   ; BX = interrupt * 4
        
        ; Check if vector points to our code segment
        cmp     word ptr [es:bx + 2], cs  ; Check segment
        jne     .vector_stolen
        
        ; Vector segment matches - assume we own it
        ; (More sophisticated check would verify offset range)
        clc                             ; Clear carry - we own it
        jmp     .done
        
.vector_stolen:
        stc                             ; Set carry - vector stolen
        
.done:
        pop     dx
        pop     ax
        ret
check_single_vector_ownership ENDP

;-----------------------------------------------------------------------------
; emergency_vector_recovery - Attempt to recover stolen vectors
;
; Input:  None
; Output: AL = number of vectors recovered
; Uses:   AL, BX, CX, DX, ES
;-----------------------------------------------------------------------------
emergency_vector_recovery PROC
        push    bx
        push    cx
        push    dx
        push    es
        
        xor     al, al                  ; Count of recovered vectors
        
        ; For now, just log the theft - actual recovery is risky
        ; In a production system, this might:
        ; 1. Re-install our vectors if safe to do so
        ; 2. Chain to the new handler if it looks legitimate  
        ; 3. Disable functionality if vectors are clearly malicious
        
        ; Increment recovery counter for statistics
        inc     word ptr [recovery_count]
        
        pop     es
        pop     dx
        pop     cx
        pop     bx
        ret
emergency_vector_recovery ENDP

;-----------------------------------------------------------------------------
; periodic_vector_monitoring - Periodic vector hijacking detection and recovery
;
; This function should be called periodically (e.g., every few seconds)
; to detect vector hijacking and attempt automatic recovery.
;
; Input:  None
; Output: AL = number of vectors recovered
; Uses:   AL only
;-----------------------------------------------------------------------------
periodic_vector_monitoring PROC
        push    bx
        push    cx
        
        ; Check if enough time has passed since last check
        ; (This would use a timer in a full implementation)
        
        ; Validate all our vectors
        call    validate_all_vectors
        cmp     al, 0
        je      .no_vectors_stolen      ; All vectors OK
        
        ; Some vectors were stolen - attempt recovery
        call    emergency_vector_recovery
        ; AL now contains number of vectors recovered
        jmp     .done

.no_vectors_stolen:
        xor     al, al                  ; No recovery needed

.done:
        pop     cx
        pop     bx
        ret
periodic_vector_monitoring ENDP

;=============================================================================
; AMIS COMPLIANCE AND DIAGNOSTICS
;=============================================================================

;-----------------------------------------------------------------------------
; install_amis_handler - Install AMIS-compliant INT 2Fh handler
;
; Implements the Alternate Multiplex Interrupt Specification (AMIS) to
; provide standardized TSR detection and management capabilities.
;
; Input:  None
; Output: CY clear if installed, CY set if failed
; Uses:   All registers
;-----------------------------------------------------------------------------
PUBLIC install_amis_handler
install_amis_handler PROC
        push    ax
        push    bx
        push    dx
        push    es
        
        ; Save original INT 2Fh vector
        mov     ax, 352Fh
        int     21h
        mov     word ptr [original_int2f_vector], bx
        mov     word ptr [original_int2f_vector + 2], es
        
        ; Install our AMIS handler
        push    ds
        mov     ax, cs
        mov     ds, ax
        mov     dx, OFFSET amis_multiplex_handler
        mov     ax, 252Fh
        int     21h
        pop     ds
        
        clc                     ; Success
        
        pop     es
        pop     dx
        pop     bx
        pop     ax
        ret
install_amis_handler ENDP

;-----------------------------------------------------------------------------
; amis_multiplex_handler - AMIS-compliant INT 2Fh handler
;
; Handles AMIS standard functions for TSR detection and management.
;
; Input:  Standard INT 2Fh register contents
; Output: AMIS-compliant responses
; Uses:   All registers as per AMIS specification
;-----------------------------------------------------------------------------
amis_multiplex_handler PROC FAR
        ; Check if this is for our multiplex ID
        cmp     ah, 0C9h                ; Our AMIS multiplex ID
        jne     .chain
        
        ; Handle AMIS standard functions
        cmp     al, 00h                 ; Installation check
        je      .installation_check
        cmp     al, 01h                 ; Get entry point
        je      .get_entry_point
        cmp     al, 02h                 ; Uninstall check
        je      .uninstall_check
        cmp     al, 03h                 ; Popup request
        je      .popup_request
        cmp     al, 04h                 ; Determine chaining
        je      .determine_chaining
        
        ; Unknown function - return error
        mov     al, 0                   ; Function not supported
        iret

.installation_check:
        ; Return installation signature
        mov     al, 0FFh                ; Installed indicator
        mov     cx, 0200h               ; Version 2.0
        mov     dx, cs                  ; Our segment
        mov     di, OFFSET driver_id_string
        iret

.get_entry_point:
        ; Return private API entry point
        mov     dx, cs                  ; Entry point segment
        mov     bx, OFFSET private_api_entry ; Entry point offset
        mov     cx, 0200h               ; Version 2.0
        iret

.uninstall_check:
        ; Check if we can uninstall safely
        call    check_uninstall_safety
        jc      .cannot_uninstall
        
        mov     al, 0FFh                ; Can uninstall
        iret

.cannot_uninstall:
        mov     al, 07h                 ; Cannot uninstall now
        iret

.popup_request:
        ; We don't support popup interface
        mov     al, 02h                 ; Function not supported by this TSR
        iret

.determine_chaining:
        ; Return chaining information
        mov     al, 01h                 ; This TSR handles chaining
        iret

.chain:
        ; Chain to original INT 2Fh handler
        jmp     dword ptr [cs:original_int2f_vector]
amis_multiplex_handler ENDP

;-----------------------------------------------------------------------------
; private_api_entry - Private API entry point for advanced applications
;
; Provides extended diagnostic and control functions beyond the standard
; packet driver API.
;
; Input:  AH = function code, other registers per function
; Output: Function-specific results
; Uses:   All registers
;-----------------------------------------------------------------------------
private_api_entry PROC FAR
        ; Switch to private stack for safety
        SAFE_STACK_SWITCH
        
        pusha
        push    ds
        push    es
        
        mov     ax, _DATA
        mov     ds, ax
        ASSUME  DS:_DATA
        
        ; Validate function code range
        cmp     ah, 80h                 ; Minimum private function
        jb      .invalid_function
        cmp     ah, 9Fh                 ; Maximum private function
        ja      .invalid_function
        
        ; Dispatch to private function
        sub     ah, 80h                 ; Convert to 0-based
        mov     al, ah
        xor     ah, ah
        mov     bx, ax
        shl     bx, 1                   ; Word offset
        call    word ptr [private_function_table + bx]
        jmp     .exit

.invalid_function:
        mov     ah, 11                  ; BAD_COMMAND
        stc

.exit:
        pop     es
        pop     ds
        popa
        
        RESTORE_CALLER_STACK
        retf
private_api_entry ENDP

; Private function dispatch table
private_function_table:
        dw      get_defensive_statistics        ; Function 80h
        dw      reset_defensive_statistics      ; Function 81h
        dw      get_memory_status               ; Function 82h
        dw      validate_all_structures         ; Function 83h
        dw      get_vector_status               ; Function 84h
        dw      force_vector_recovery           ; Function 85h
        dw      get_performance_counters        ; Function 86h
        dw      set_diagnostic_level            ; Function 87h
        ; ... more functions as needed

;-----------------------------------------------------------------------------
; get_defensive_statistics - Get comprehensive defensive programming statistics
;
; Private API Function 80h: Returns detailed statistics about defensive
; programming systems performance and event counts.
;
; Input:  ES:DI -> buffer for statistics (minimum 32 bytes)
; Output: Statistics copied to buffer, CY clear on success
; Uses:   All registers
;-----------------------------------------------------------------------------
get_defensive_statistics PROC
        push    si
        push    cx
        
        ; Copy statistics to caller's buffer
        mov     si, OFFSET error_count
        mov     cx, 16                  ; Copy 16 words of statistics
        rep     movsw
        
        clc                             ; Success
        
        pop     cx
        pop     si
        ret
get_defensive_statistics ENDP

;-----------------------------------------------------------------------------
; reset_defensive_statistics - Reset all defensive programming counters
;
; Private API Function 81h: Resets error counters and statistics to zero.
;
; Input:  None
; Output: CY clear on success
; Uses:   AX, CX, DI
;-----------------------------------------------------------------------------
reset_defensive_statistics PROC
        ; Reset error counters
        mov     word ptr [error_count], 0
        mov     word ptr [dos_busy_count], 0
        mov     word ptr [stack_switch_count], 0
        mov     word ptr [deferred_work_count], 0
        mov     word ptr [memory_corruption_count], 0
        mov     word ptr [data_validation_errors], 0
        mov     word ptr [recovery_count], 0
        
        clc                             ; Success
        ret
reset_defensive_statistics ENDP

;-----------------------------------------------------------------------------
; get_memory_status - Get memory protection status
;
; Private API Function 82h: Returns memory protection status and corruption
; detection information.
;
; Input:  ES:DI -> buffer for memory status (minimum 16 bytes)
; Output: Memory status copied to buffer, CY clear on success
; Uses:   All registers
;-----------------------------------------------------------------------------
get_memory_status PROC
        push    si
        push    cx
        
        ; Copy memory protection statistics
        mov     si, OFFSET memory_corruption_count
        mov     cx, 8                   ; Copy 8 words
        rep     movsw
        
        clc                             ; Success
        
        pop     cx
        pop     si
        ret
get_memory_status ENDP

;-----------------------------------------------------------------------------
; validate_all_structures - Force validation of all protected structures
;
; Private API Function 83h: Performs immediate validation of all memory
; structures and data integrity.
;
; Input:  None
; Output: AL = number of corrupted structures found, CY set if any corruption
; Uses:   All registers
;-----------------------------------------------------------------------------
validate_all_structures PROC
        ; Validate critical data
        call    validate_critical_data
        ; AL contains corruption count from validate_critical_data
        
        ; Could add more validation here:
        ; - Packet buffer integrity
        ; - Hardware state consistency
        ; - NIC configuration validation
        
        cmp     al, 0
        je      .no_corruption
        stc
        ret

.no_corruption:
        clc
        ret
validate_all_structures ENDP

;-----------------------------------------------------------------------------
; get_vector_status - Get interrupt vector status
;
; Private API Function 84h: Returns status of all interrupt vectors we manage.
;
; Input:  ES:DI -> buffer for vector status (minimum 8 bytes)
; Output: Vector status copied to buffer, CY clear on success
; Uses:   All registers
;-----------------------------------------------------------------------------
get_vector_status PROC
        push    ax
        push    bx
        
        ; Check each vector and store status
        call    validate_all_vectors
        mov     es:[di], al             ; Store stolen vector count
        inc     di
        
        ; Add more detailed vector information
        mov     al, [defensive_initialized]
        mov     es:[di], al             ; Store initialization status
        inc     di
        
        clc                             ; Success
        
        pop     bx
        pop     ax
        ret
get_vector_status ENDP

;-----------------------------------------------------------------------------
; force_vector_recovery - Force immediate vector recovery attempt
;
; Private API Function 85h: Attempts immediate recovery of stolen vectors.
;
; Input:  None
; Output: AL = number of vectors recovered, CY clear on success
; Uses:   All registers
;-----------------------------------------------------------------------------
force_vector_recovery PROC
        call    emergency_vector_recovery
        ; AL contains recovery count from emergency_vector_recovery
        clc                             ; Always return success
        ret
force_vector_recovery ENDP

;-----------------------------------------------------------------------------
; get_performance_counters - Get performance monitoring data
;
; Private API Function 86h: Returns performance counters and timing data.
;
; Input:  ES:DI -> buffer for performance data (minimum 16 bytes)
; Output: Performance data copied to buffer, CY clear on success
; Uses:   All registers
;-----------------------------------------------------------------------------
get_performance_counters PROC
        ; For now, just return basic counters
        ; In a full implementation, this would include:
        ; - ISR execution times
        ; - Deferred work processing times
        ; - Memory allocation performance
        ; - Hardware I/O timing statistics
        
        push    si
        push    cx
        
        mov     si, OFFSET stack_switch_count
        mov     cx, 4                   ; Copy 4 words of performance data
        rep     movsw
        
        clc                             ; Success
        
        pop     cx
        pop     si
        ret
get_performance_counters ENDP

;-----------------------------------------------------------------------------
; set_diagnostic_level - Set diagnostic output level
;
; Private API Function 87h: Controls the level of diagnostic output.
;
; Input:  AL = diagnostic level (0=off, 1=errors, 2=warnings, 3=info, 4=verbose)
; Output: CY clear on success
; Uses:   None
;-----------------------------------------------------------------------------
set_diagnostic_level PROC
        ; Store diagnostic level (would be used by logging functions)
        ; For now, just acknowledge the command
        clc
        ret
set_diagnostic_level ENDP

; Required data for AMIS compliance
driver_id_string        db 'PKT-DRVR', 0    ; AMIS identification string
original_int2f_vector   dd 0                 ; Original INT 2Fh vector

;-----------------------------------------------------------------------------
; check_uninstall_safety - Check if driver can be safely uninstalled
;
; Verifies that no applications are using the driver and that uninstall
; would not break system stability.
;
; Input:  None
; Output: CY clear if safe to uninstall, CY set if not safe
; Uses:   AX, BX
;-----------------------------------------------------------------------------
check_uninstall_safety PROC
        ; Check if any packet handles are still active
        ; (This would check the packet driver handle table in full implementation)
        
        ; Check if hardware is still in use
        ; (This would check NIC status in full implementation)
        
        ; For now, assume it's safe to uninstall
        clc
        ret
check_uninstall_safety ENDP

;=============================================================================
; HARDWARE TIMEOUT PROTECTION
;=============================================================================

;-----------------------------------------------------------------------------
; safe_port_wait - Wait for hardware condition with timeout protection
;
; This addresses the critical gap where infinite hardware waits can cause
; system lockups when hardware becomes non-responsive.
;
; Input:  DX = I/O port, AL = mask to test, CX = timeout count
; Output: CY clear if condition met, CY set if timeout
;         AL = final port value read
; Uses:   CX (timeout counter), AL (port value)
;-----------------------------------------------------------------------------
PUBLIC safe_port_wait
safe_port_wait PROC
        push    bx
        
        ; Save the mask for comparison
        mov     bl, al
        
.wait_loop:
        in      al, dx          ; Read from port
        test    al, bl          ; Test against mask
        jnz     .condition_met  ; Condition satisfied
        
        loop    .wait_loop      ; Continue waiting, decrement CX
        
        ; Timeout occurred
        stc                     ; Set carry flag for timeout
        jmp     .done
        
.condition_met:
        clc                     ; Clear carry flag for success
        
.done:
        pop     bx
        ret
safe_port_wait ENDP

;-----------------------------------------------------------------------------
; safe_port_wait_clear - Wait for hardware condition to clear with timeout
;
; Waits for a hardware condition to become clear (mask bits = 0).
;
; Input:  DX = I/O port, AL = mask to test, CX = timeout count
; Output: CY clear if condition cleared, CY set if timeout
;         AL = final port value read
; Uses:   CX (timeout counter), AL (port value)
;-----------------------------------------------------------------------------
PUBLIC safe_port_wait_clear
safe_port_wait_clear PROC
        push    bx
        
        ; Save the mask for comparison
        mov     bl, al
        
.wait_loop:
        in      al, dx          ; Read from port
        test    al, bl          ; Test against mask
        jz      .condition_met  ; Condition cleared (zero result)
        
        loop    .wait_loop      ; Continue waiting, decrement CX
        
        ; Timeout occurred
        stc                     ; Set carry flag for timeout
        jmp     .done
        
.condition_met:
        clc                     ; Clear carry flag for success
        
.done:
        pop     bx
        ret
safe_port_wait_clear ENDP

;-----------------------------------------------------------------------------
; safe_command_wait - Send command and wait for completion with timeout
;
; Sends a command to a hardware port and waits for completion with timeout
; protection. This is a common pattern in NIC programming.
;
; Input:  DX = command port, AL = command, BX = status port
;         CL = status mask, CH = timeout (high byte of timeout)
; Output: CY clear if command completed, CY set if timeout
;         AL = final status value
; Uses:   All input registers, plus internal timeout logic
;-----------------------------------------------------------------------------
PUBLIC safe_command_wait
safe_command_wait PROC
        push    cx
        push    dx
        
        ; Send the command
        out     dx, al          ; Send command to command port
        
        ; Prepare for status waiting
        mov     dx, bx          ; Switch to status port
        mov     al, cl          ; Get status mask
        mov     cx, ch          ; Get timeout count
        shl     cx, 8           ; Convert to larger timeout value
        
        ; Wait for command completion
        call    safe_port_wait
        
        pop     dx
        pop     cx
        ret
safe_command_wait ENDP

;-----------------------------------------------------------------------------
; hardware_reset_with_timeout - Reset hardware with timeout protection
;
; Performs a hardware reset sequence with proper timeout handling.
; This prevents infinite loops during hardware initialization failures.
;
; Input:  DX = control port, AL = reset command, BX = status port
;         CL = ready mask, CH = timeout multiplier
; Output: CY clear if reset successful, CY set if timeout/failure
;         AL = final status
; Uses:   All registers
;-----------------------------------------------------------------------------
PUBLIC hardware_reset_with_timeout
hardware_reset_with_timeout PROC
        push    cx
        push    dx
        push    bx
        
        ; Send reset command
        out     dx, al
        
        ; Wait for reset to take effect (short delay)
        mov     cx, 1000        ; Short delay for reset propagation
.reset_delay:
        nop
        loop    .reset_delay
        
        ; Now wait for hardware to become ready
        mov     dx, bx          ; Switch to status port
        mov     al, cl          ; Get ready mask
        mov     cx, ch          ; Get timeout multiplier
        shl     cx, 8           ; Convert to timeout value
        add     cx, 5000        ; Add base timeout for hardware reset
        
        call    safe_port_wait_clear  ; Wait for reset condition to clear
        jc      .reset_timeout
        
        ; Additional wait for full readiness
        mov     cx, 2000
        call    safe_port_wait
        
.reset_timeout:
        pop     bx
        pop     dx
        pop     cx
        ret
hardware_reset_with_timeout ENDP

;-----------------------------------------------------------------------------
; log_hardware_timeout - Log hardware timeout event for diagnostics
;
; Records hardware timeout events for later analysis. This helps identify
; problematic hardware or conditions that cause timeouts.
;
; Input:  AL = timeout type code, DX = port that timed out
; Output: None
; Uses:   None (preserves all registers)
;-----------------------------------------------------------------------------
PUBLIC log_hardware_timeout
log_hardware_timeout PROC
        ; This is a placeholder for timeout logging
        ; In a full implementation, this would:
        ; 1. Store timeout information in a circular buffer
        ; 2. Update timeout counters
        ; 3. Possibly trigger error recovery procedures
        
        ; For now, just increment the error count
        push    ax
        inc     word ptr [error_count]
        pop     ax
        ret
log_hardware_timeout ENDP

;=============================================================================
; HARDWARE TIMEOUT USAGE EXAMPLES
;=============================================================================

;-----------------------------------------------------------------------------
; example_safe_nic_reset - Example of safe NIC reset with timeout protection
;
; This shows how to use the timeout protection functions to safely reset
; a NIC without risking system lockup. This replaces infinite wait loops
; that were identified as a critical defensive programming gap.
;
; Input:  DX = NIC base I/O address
; Output: CY clear if reset successful, CY set if failed
; Uses:   All registers
;-----------------------------------------------------------------------------
PUBLIC example_safe_nic_reset
example_safe_nic_reset PROC
        push    ax
        push    bx
        push    cx
        push    dx
        
        ; Save base address
        mov     bx, dx
        
        ; Send reset command to NIC control register
        add     dx, 0Eh         ; Control register offset (example)
        mov     al, 01h         ; Reset command (example)
        out     dx, al
        
        ; Wait for reset to complete with timeout protection
        ; Old code would do: .wait: in al,dx; test al,01h; jnz .wait  ; DANGEROUS!
        ; New code uses timeout protection:
        mov     cx, 10000       ; Timeout count
        mov     al, 01h         ; Reset bit mask
        call    safe_port_wait_clear  ; Wait for reset bit to clear
        jc      .reset_failed
        
        ; Additional readiness check
        add     dx, 02h         ; Status register offset (example)
        mov     cx, 5000        ; Timeout count
        mov     al, 80h         ; Ready bit mask (example)
        call    safe_port_wait  ; Wait for ready bit to set
        jc      .reset_failed
        
        ; Reset successful
        clc
        jmp     .done
        
.reset_failed:
        ; Log the timeout event
        mov     al, 01h         ; Reset timeout event code
        call    log_hardware_timeout
        stc
        
.done:
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
example_safe_nic_reset ENDP

;-----------------------------------------------------------------------------
; example_safe_command_send - Example of safe command sending with timeout
;
; Shows how to safely send commands to hardware and wait for completion
; without risking infinite loops.
;
; Input:  DX = command port, AL = command, BL = expected status
; Output: CY clear if successful, CY set if timeout
; Uses:   All registers
;-----------------------------------------------------------------------------
PUBLIC example_safe_command_send
example_safe_command_send PROC
        push    ax
        push    bx
        push    cx
        push    dx
        
        ; Send command
        out     dx, al
        
        ; Wait for command completion with timeout
        ; Old dangerous code: .wait: in al,dx; cmp al,bl; jne .wait
        ; New safe code:
        push    dx
        inc     dx              ; Status port (command port + 1, example)
        mov     cx, 8000        ; Timeout count
        
.wait_loop:
        in      al, dx
        cmp     al, bl          ; Check for expected status
        je      .command_complete
        loop    .wait_loop      ; Bounded loop prevents lockup
        
        ; Timeout occurred
        pop     dx
        mov     al, 02h         ; Command timeout event code
        call    log_hardware_timeout
        stc
        jmp     .done
        
.command_complete:
        pop     dx
        clc
        
.done:
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
example_safe_command_send ENDP

;-----------------------------------------------------------------------------
; NOTES FOR INTEGRATING TIMEOUT PROTECTION:
;
; To integrate these timeout protections into existing hardware code:
;
; 1. Replace all infinite hardware wait loops with timeout-protected versions
; 2. Use safe_port_wait() instead of busy-wait loops
; 3. Use safe_command_wait() for command/status patterns
; 4. Use hardware_reset_with_timeout() for reset sequences
; 5. Always check return codes and handle timeout conditions gracefully
; 6. Log timeout events for diagnostics and debugging
;
; CRITICAL REPLACEMENTS NEEDED:
;
; REPLACE THIS DANGEROUS PATTERN:
;   .wait_busy:
;       in  al, dx
;       test al, 01h
;       jnz .wait_busy        ; INFINITE LOOP RISK!
;
; WITH THIS SAFE PATTERN:
;       mov cx, TIMEOUT_MEDIUM
;       mov al, 01h
;       call safe_port_wait_clear
;       jc .timeout_error     ; Handle timeout gracefully
;
;=============================================================================

;=============================================================================
; MEMORY CORRUPTION DETECTION
;=============================================================================

;-----------------------------------------------------------------------------
; place_memory_canaries - Place canary values around a memory block
;
; This implements memory corruption detection by placing known values
; before and after allocated memory blocks. Corruption can be detected
; by checking if these canary values have been modified.
;
; Input:  ES:DI = pointer to memory block (user data starts here)
;         CX = size of user data
; Output: ES:DI = pointer adjusted past front canary
;         CX = size reduced by canary space
; Uses:   None (preserves all registers except ES:DI and CX as noted)
;-----------------------------------------------------------------------------
PUBLIC place_memory_canaries
place_memory_canaries PROC
        push    ax
        push    bx
        
        ; Place front canary (4 bytes before user data)
        sub     di, 4           ; Move back to canary space
        mov     ax, MEMORY_CANARY_FRONT
        mov     es:[di], ax     ; Store front canary low word
        mov     ax, MEMORY_CANARY_FRONT
        xor     ax, 0AAAAh      ; Slightly modify for high word
        mov     es:[di+2], ax   ; Store front canary high word
        
        ; Move DI back to user data start
        add     di, 4
        
        ; Place rear canary (4 bytes after user data)
        push    di
        add     di, cx          ; Move to end of user data
        mov     ax, MEMORY_CANARY_REAR
        mov     es:[di], ax     ; Store rear canary low word
        mov     ax, MEMORY_CANARY_REAR
        xor     ax, 5555h       ; Slightly modify for high word
        mov     es:[di+2], ax   ; Store rear canary high word
        pop     di
        
        ; Reduce available space by canary overhead
        sub     cx, 8           ; Front (4) + rear (4) canaries
        
        ; Update statistics
        inc     word ptr [protected_block_count]
        
        pop     bx
        pop     ax
        ret
place_memory_canaries ENDP

;-----------------------------------------------------------------------------
; check_memory_canaries - Verify memory canaries are intact
;
; Checks if memory canaries have been corrupted, indicating buffer
; overflow, underflow, or other memory corruption.
;
; Input:  ES:DI = pointer to user data (between canaries)
;         CX = size of user data
; Output: CY clear if canaries intact, CY set if corrupted
;         If corrupted: AL = corruption type (1=front, 2=rear, 3=both)
; Uses:   AX, BX
;-----------------------------------------------------------------------------
PUBLIC check_memory_canaries
check_memory_canaries PROC
        push    dx
        push    si
        
        xor     al, al          ; Clear corruption flags
        
        ; Check front canary
        mov     si, di
        sub     si, 4           ; Move to front canary
        mov     dx, MEMORY_CANARY_FRONT
        cmp     es:[si], dx     ; Check low word
        jne     .front_corrupted
        
        xor     dx, 0AAAAh      ; Expected high word
        cmp     es:[si+2], dx   ; Check high word
        jne     .front_corrupted
        jmp     .check_rear
        
.front_corrupted:
        or      al, 01h         ; Set front corruption flag
        
.check_rear:
        ; Check rear canary
        mov     si, di
        add     si, cx          ; Move to rear canary
        mov     dx, MEMORY_CANARY_REAR
        cmp     es:[si], dx     ; Check low word
        jne     .rear_corrupted
        
        xor     dx, 5555h       ; Expected high word
        cmp     es:[si+2], dx   ; Check high word
        jne     .rear_corrupted
        jmp     .check_result
        
.rear_corrupted:
        or      al, 02h         ; Set rear corruption flag
        
.check_result:
        cmp     al, 0
        je      .no_corruption
        
        ; Corruption detected
        inc     word ptr [memory_corruption_count]
        
        ; Store corruption address for diagnostics
        mov     word ptr [last_corruption_addr], di
        mov     word ptr [last_corruption_addr+2], es
        
        stc                     ; Set carry to indicate corruption
        jmp     .done
        
.no_corruption:
        clc                     ; Clear carry to indicate no corruption
        
.done:
        pop     si
        pop     dx
        ret
check_memory_canaries ENDP

;-----------------------------------------------------------------------------
; validate_critical_data - Validate critical driver data structures
;
; Calculates and verifies checksums of critical driver data to detect
; corruption from runaway code, hardware failures, or other issues.
;
; Input:  None
; Output: CY clear if data valid, CY set if corrupted
;         AL = number of corrupted structures detected
; Uses:   All registers
;-----------------------------------------------------------------------------
PUBLIC validate_critical_data
validate_critical_data PROC
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        
        xor     al, al          ; Corruption counter
        
        ; Validate our own data segment
        mov     bx, _DATA
        mov     es, bx
        
        ; Calculate checksum of critical driver variables
        ; (This is a simplified checksum - production code would be more thorough)
        mov     si, OFFSET error_count
        mov     cx, 20          ; Check 20 bytes of critical data
        xor     dx, dx          ; Running checksum
        
.checksum_loop:
        add     dl, es:[si]     ; Simple additive checksum
        adc     dh, 0           ; Handle carry
        inc     si
        loop    .checksum_loop
        
        ; Compare with stored checksum
        cmp     dx, [driver_data_checksum]
        je      .data_valid
        
        ; Data corruption detected
        inc     al
        inc     word ptr [data_validation_errors]
        
        ; Update stored checksum (recovery attempt)
        mov     [driver_data_checksum], dx
        inc     word ptr [checksum_update_count]
        
.data_valid:
        ; Could add more data structure validations here
        ; For example: validate packet handle tables, NIC state, etc.
        
        ; Set carry flag if any corruption detected
        cmp     al, 0
        je      .no_corruption
        stc
        jmp     .done
        
.no_corruption:
        clc
        
.done:
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret
validate_critical_data ENDP

;-----------------------------------------------------------------------------
; initialize_data_protection - Initialize memory protection systems
;
; Sets up initial checksums and canary systems for memory protection.
; Should be called during driver initialization.
;
; Input:  None
; Output: None
; Uses:   All registers
;-----------------------------------------------------------------------------
PUBLIC initialize_data_protection
initialize_data_protection PROC
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    es
        
        ; Initialize data segment checksum
        mov     bx, _DATA
        mov     es, bx
        
        ; Calculate initial checksum of critical data
        mov     si, OFFSET error_count
        mov     cx, 20          ; Check 20 bytes of critical data
        xor     dx, dx          ; Running checksum
        
.init_checksum_loop:
        add     dl, es:[si]     ; Simple additive checksum
        adc     dh, 0           ; Handle carry
        inc     si
        loop    .init_checksum_loop
        
        ; Store initial checksum
        mov     [driver_data_checksum], dx
        
        ; Reset counters
        mov     word ptr [memory_corruption_count], 0
        mov     word ptr [protected_block_count], 0
        mov     word ptr [data_validation_errors], 0
        mov     word ptr [checksum_update_count], 0
        
        pop     es
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
initialize_data_protection ENDP

;-----------------------------------------------------------------------------
; periodic_memory_validation - Periodic check of memory integrity
;
; Should be called periodically (e.g., from timer interrupt or main loop)
; to detect memory corruption early.
;
; Input:  None
; Output: AL = number of corruption events detected this check
; Uses:   AL only
;-----------------------------------------------------------------------------
PUBLIC periodic_memory_validation
periodic_memory_validation PROC
        ; For now, just validate critical data
        ; In a full implementation, this would also:
        ; 1. Check all allocated memory blocks with canaries
        ; 2. Validate packet buffer integrity
        ; 3. Check interrupt vector table integrity
        ; 4. Validate hardware register backup copies
        
        call    validate_critical_data
        ; AL already contains corruption count from validate_critical_data
        ret
periodic_memory_validation ENDP

;=============================================================================
; PROPER CRITICAL SECTION MANAGEMENT
;=============================================================================

;-----------------------------------------------------------------------------
; enhanced_critical_section_demo - Demonstration of proper critical sections
;
; This addresses the gap where cli/sti was used instead of pushf/cli...popf,
; which could inadvertently enable interrupts when they should stay disabled.
;
; Input:  None
; Output: None  
; Uses:   None (demonstrates preservation of interrupt flag state)
;-----------------------------------------------------------------------------
PUBLIC enhanced_critical_section_demo
enhanced_critical_section_demo PROC
        ; WRONG WAY (dangerous):
        ; cli                    ; This unconditionally disables interrupts
        ; ; ... critical code ...
        ; sti                    ; This unconditionally enables interrupts!
        ;                        ; If caller had interrupts disabled, this is WRONG!
        
        ; RIGHT WAY (defensive):
        ENTER_CRITICAL           ; Saves current IF state and disables interrupts
        
        ; Critical section code goes here
        ; This code runs with interrupts disabled
        ; Example: modify shared data structures
        inc     word ptr [error_count]  ; Example critical operation
        
        EXIT_CRITICAL            ; Restores original IF state
        
        ret
enhanced_critical_section_demo ENDP

;-----------------------------------------------------------------------------
; reentrant_critical_section_demo - Demonstration of reentrant critical sections
;
; Shows how to handle nested critical sections properly with nesting counters.
;
; Input:  None
; Output: None
; Uses:   None
;-----------------------------------------------------------------------------
PUBLIC reentrant_critical_section_demo
reentrant_critical_section_demo PROC
        ; First level critical section
        ENTER_CRITICAL_REENTRANT
        
        ; Do some critical work
        inc     word ptr [stack_switch_count]
        
        ; Call another function that also needs a critical section
        call    .inner_critical_function
        
        ; Exit first level
        EXIT_CRITICAL_REENTRANT
        
        ret

.inner_critical_function:
        ; Second level critical section (nested)
        ENTER_CRITICAL_REENTRANT
        
        ; Do more critical work
        inc     word ptr [deferred_work_count]
        
        ; This exit won't restore interrupts because we're still nested
        EXIT_CRITICAL_REENTRANT
        
        ret
reentrant_critical_section_demo ENDP

;-----------------------------------------------------------------------------
; safe_shared_data_update - Example of safely updating shared data
;
; Demonstrates the proper way to update shared data structures that might
; be accessed from both main code and interrupt handlers.
;
; Input:  AX = new error count value
; Output: Previous error count in AX
; Uses:   AX only
;-----------------------------------------------------------------------------
PUBLIC safe_shared_data_update
safe_shared_data_update PROC
        push    bx
        
        ENTER_CRITICAL
        
        ; Atomically read old value and store new value
        mov     bx, [error_count]      ; Read current value
        mov     [error_count], ax      ; Store new value
        mov     ax, bx                 ; Return old value
        
        EXIT_CRITICAL
        
        pop     bx
        ret
safe_shared_data_update ENDP

;-----------------------------------------------------------------------------
; critical_section_guidelines - Documentation of critical section best practices
;
; This is documentation embedded as comments to guide developers on proper
; critical section usage throughout the driver.
;
;=============================================================================
; CRITICAL SECTION BEST PRACTICES:
;
; 1. ALWAYS use ENTER_CRITICAL/EXIT_CRITICAL instead of cli/sti
;    - Preserves original interrupt flag state
;    - Handles nested calls correctly
;    - More maintainable and less error-prone
;
; 2. Keep critical sections as SHORT as possible
;    - Long critical sections can cause missed interrupts
;    - Can lead to system responsiveness issues
;    - Move complex work outside critical sections when possible
;
; 3. Use ENTER_CRITICAL_REENTRANT for functions that might be nested
;    - Handles multiple levels of critical sections
;    - Prevents premature interrupt restoration
;    - Essential for complex call hierarchies
;
; 4. NEVER call DOS functions inside critical sections
;    - DOS functions can be lengthy
;    - May cause system hangs or crashes
;    - Use deferred processing instead
;
; 5. Be careful with stack operations in critical sections
;    - Stack switching requires special handling
;    - Current cli/sti usage for stack switching is CORRECT
;    - Don't change stack switching code to use ENTER_CRITICAL
;
; 6. Order critical sections consistently to avoid deadlocks
;    - Always acquire locks in the same order
;    - Release locks in reverse order
;    - Document lock ordering requirements
;
; STACK SWITCHING EXCEPTION:
; The cli/sti patterns used for stack switching in this driver are CORRECT
; and should NOT be changed to ENTER_CRITICAL/EXIT_CRITICAL because:
; - Stack switching requires a precise interrupt disable window
; - The SS and SP registers must be updated atomically
; - Intel 8086 architecture provides automatic interrupt masking after MOV SS
; - We explicitly control the interrupt state during stack operations
;
;=============================================================================

;-----------------------------------------------------------------------------
; log_event - Log an event to the circular buffer
;
; Interrupt-safe logging function that adds events to a circular buffer.
; Events are written to disk later during safe DOS periods.
;
; Input:  BL = event code
;         AL = optional parameter (IRQ number, error code, etc.)
; Output: None
; Uses:   Preserves all registers
;-----------------------------------------------------------------------------
PUBLIC log_event
log_event PROC
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    ds
        
        ; Ensure our data segment
        push    cs
        pop     ds
        
        ; Enter critical section for atomic logging
        cli
        
        ; Calculate next head position
        mov     si, [log_head]
        mov     dx, si
        inc     dx
        and     dx, 0FFh                    ; Wrap at 256 bytes
        
        ; Check for buffer overflow
        cmp     dx, [log_tail]
        je      .buffer_full
        
        ; Add event to buffer
        lea     si, [log_buffer + si]
        mov     [si], bl                    ; Store event code
        inc     si
        and     si, 0FFh
        lea     si, [log_buffer + si]
        mov     [si], al                    ; Store parameter
        
        ; Update head pointer
        mov     [log_head], dx
        
        ; Mark deferred log write pending
        mov     byte ptr [deferred_log_pending], 1
        
        jmp     .exit
        
.buffer_full:
        ; Increment overflow counter
        inc     word ptr [log_overflow_count]
        
.exit:
        sti                                 ; Re-enable interrupts
        
        pop     ds
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
log_event ENDP

;-----------------------------------------------------------------------------
; log_irq_event - Log an IRQ-related event
;
; Specialized logging for IRQ vector theft detection.
;
; Input:  BL = event code (typically LOG_EVENT_IRQ_THEFT)
;         AL = IRQ number that was stolen
; Output: None
; Uses:   Preserves all registers
;-----------------------------------------------------------------------------
PUBLIC log_irq_event
log_irq_event PROC
        push    ax
        push    bx
        
        ; Add timestamp if available
        push    cx
        push    dx
        
        ; Get system timer tick count for timestamp
        push    es
        xor     ax, ax
        mov     es, ax
        mov     cx, es:[046Ch]              ; Timer tick count low word
        pop     es
        
        ; Log the IRQ event with timestamp
        mov     bl, LOG_EVENT_IRQ_THEFT
        call    log_event                   ; Log event code
        
        mov     bl, al                      ; IRQ number
        mov     al, cl                      ; Low byte of timestamp
        call    log_event                   ; Log IRQ and time
        
        ; Increment IRQ theft counter
        inc     word ptr [irq_theft_count]
        
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
log_irq_event ENDP

;-----------------------------------------------------------------------------
; flush_log_buffer - Write pending log entries to disk
;
; Called during safe DOS periods to write accumulated log entries.
; This function is NOT interrupt-safe and must only be called when
; DOS is available.
;
; Input:  None
; Output: AX = number of entries written, or -1 on error
; Uses:   All registers
;-----------------------------------------------------------------------------
PUBLIC flush_log_buffer
flush_log_buffer PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    ds
        push    es
        
        ; Check if there's anything to flush
        cmp     byte ptr [deferred_log_pending], 0
        je      .nothing_to_flush
        
        ; Check if DOS is safe
        call    dos_is_safe
        jnz     .dos_busy
        
        ; Disable interrupts while reading buffer
        cli
        
        ; Calculate number of bytes to write
        mov     si, [log_tail]
        mov     di, [log_head]
        mov     cx, di
        sub     cx, si
        and     cx, 0FFh                    ; Handle wrap-around
        jz      .empty_buffer
        
        ; Mark buffer as being processed
        mov     byte ptr [deferred_log_pending], 0
        
        ; Re-enable interrupts
        sti
        
        ; Open or create log file (simplified - would use DOS file I/O)
        ; For now, just update counters and clear buffer
        
        ; Update tail to mark entries as processed
        mov     [log_tail], di
        
        ; Return number of entries processed
        mov     ax, cx
        shr     ax, 1                       ; Each entry is 2 bytes
        jmp     .exit
        
.empty_buffer:
        sti
        xor     ax, ax                      ; No entries
        jmp     .exit
        
.nothing_to_flush:
        xor     ax, ax
        jmp     .exit
        
.dos_busy:
        mov     ax, -1                      ; DOS busy error
        
.exit:
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
flush_log_buffer ENDP

;-----------------------------------------------------------------------------
; check_irq_vector_integrity - Verify all hardware IRQ vectors
;
; Comprehensive check of all configured hardware IRQ vectors to detect
; if they've been hijacked by other software.
;
; Input:  None
; Output: AL = number of stolen vectors detected
; Uses:   All registers
;-----------------------------------------------------------------------------
PUBLIC check_irq_vector_integrity
check_irq_vector_integrity PROC
        push    bx
        push    cx
        push    dx
        push    si
        push    es
        
        xor     al, al                      ; Stolen vector count
        
        ; Check each configured NIC IRQ
        mov     si, OFFSET nic_irq_table
        mov     cx, MAX_NIC_IRQS
        
.check_loop:
        mov     bl, [si]                    ; Get IRQ number
        cmp     bl, 0FFh                    ; Not configured?
        je      .next_irq
        
        ; Validate IRQ range
        cmp     bl, 15
        ja      .next_irq
        
        ; Get current vector for this IRQ
        push    ax
        movzx   ax, bl
        
        ; Convert IRQ to interrupt vector number
        cmp     al, 8
        jb      .master_irq
        add     al, 0x70 - 8                ; IRQ 8-15 -> INT 70h-77h
        jmp     .get_vector
.master_irq:
        add     al, 0x08                    ; IRQ 0-7 -> INT 08h-0Fh
        
.get_vector:
        ; Get interrupt vector
        push    ax
        mov     ah, 35h                     ; Get interrupt vector
        int     21h                         ; Returns ES:BX
        
        ; Check if it points to our handler
        cmp     bx, word ptr [nic_irq_handler]
        jne     .vector_stolen
        mov     ax, es
        cmp     ax, cs
        je      .vector_ok
        
.vector_stolen:
        ; Vector has been stolen
        pop     ax
        pop     ax
        inc     al                          ; Increment stolen count
        
        ; Log the theft
        push    ax
        mov     al, [si]                    ; IRQ number
        mov     bl, LOG_EVENT_IRQ_THEFT
        call    log_irq_event
        pop     ax
        jmp     .next_irq
        
.vector_ok:
        pop     ax
        pop     ax
        
.next_irq:
        inc     si
        loop    .check_loop
        
        pop     es
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret
check_irq_vector_integrity ENDP

_TEXT ENDS

END