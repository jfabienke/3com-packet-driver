; TSR Defensive Programming Macros and Constants
; Provides reusable defensive patterns for DOS TSR development
; Based on proven techniques from 30+ years of DOS networking
; Converted to NASM syntax: 2026-01-23

%ifndef TSR_DEFENSIVE_INC
%define TSR_DEFENSIVE_INC 1

;=============================================================================
; CONSTANTS
;=============================================================================

; Error codes (compatible with packet driver specification)
BAD_HANDLE          equ 1       ; Invalid handle
NO_CLASS            equ 2       ; No such class
NO_TYPE             equ 3       ; No such type
NO_NUMBER           equ 4       ; No such number
BAD_TYPE            equ 5       ; Bad type specified
NO_MULTICAST        equ 6       ; No multicast available
CANT_TERMINATE      equ 7       ; Can't terminate
BAD_MODE            equ 8       ; Bad mode specified
NO_SPACE            equ 9       ; No space available
TYPE_INUSE          equ 10      ; Type already in use
BAD_COMMAND         equ 11      ; Bad command
CANT_SEND           equ 12      ; Can't send
CANT_SET            equ 13      ; Can't set hardware address
BAD_ADDRESS         equ 14      ; Bad address
CANT_RESET          equ 15      ; Can't reset interface

; AMIS constants
AMIS_INSTALLATION_CHECK     equ 00h
AMIS_GET_ENTRY_POINT       equ 01h
AMIS_UNINSTALL_CHECK       equ 02h
AMIS_POPUP_REQUEST         equ 03h
AMIS_DETERMINE_CHAINING    equ 04h

; IISP constants
IISP_SIGNATURE_1    equ 'K'
IISP_SIGNATURE_2    equ 'B'
IISP_FLAG_SHAREABLE equ 0

; Hardware timeout constants
TIMEOUT_SHORT       equ 1000    ; For quick operations
TIMEOUT_MEDIUM      equ 5000    ; For medium operations
TIMEOUT_LONG        equ 10000   ; For reset, initialization

; Memory protection constants
CANARY_PATTERN      equ 0xDEADBEEF
SIGNATURE_MAGIC     equ 0x5A5A

;=============================================================================
; DATA STRUCTURES
;=============================================================================

; IISP header structure for interrupt sharing
struc IISP_HEADER
    .entry_jump:    resb 3      ; Short jump to handler (EB xx 90)
    .signature:     resb 2      ; Signature bytes (set at runtime)
    .flags:         resb 1      ; Flags byte (set at runtime)
    .chain_offset:  resw 1      ; Offset of next handler
    .chain_segment: resw 1      ; Segment of next handler
    .reserved:      resw 1      ; Reserved field
endstruc

; Protected data structure template
struc PROTECTED_DATA
    .signature:     resw 1      ; SIGNATURE_MAGIC (set at runtime)
    .length:        resw 1      ; Total structure size
    .checksum:      resw 1      ; XOR checksum of data
    .data:          resb 0      ; Start of actual data
endstruc

; Access type entry structure
struc ACCESS_TYPE
    .in_use:        resb 1      ; 0=free, 1=active
    .packet_type:   resw 1      ; Ethernet type (0x0800, etc.)
    .handler_offset: resw 1     ; Receiver function offset
    .handler_segment: resw 1    ; Receiver function segment
    .type_length:   resb 1      ; Length for non-standard types
    .reserved:      resb 11     ; Reserved/additional data
endstruc

;=============================================================================
; SAFE STACK SWITCHING MACROS
;=============================================================================

; Switch to driver's private stack
; Assumes: CS = data segment, stack space available
; Modifies: SS, SP (obviously)
; Preserves: All other registers
%macro SAFE_STACK_SWITCH 0
    cli                         ; Critical section start
    mov [cs:caller_ss], ss     ; Save caller's stack
    mov [cs:caller_sp], sp
    mov ax, cs                 ; Use CS as stack segment
    mov ss, ax
    mov sp, stack_top          ; Private stack
    sti                        ; Critical section end
%endmacro

; Restore caller's original stack
; Assumes: caller_ss/caller_sp are valid
; Modifies: SS, SP
; Preserves: All other registers
%macro RESTORE_CALLER_STACK 0
    cli                        ; Critical section start
    mov ss, [cs:caller_ss]     ; Restore caller's stack
    mov sp, [cs:caller_sp]
    sti                        ; Critical section end
%endmacro

;=============================================================================
; CRITICAL SECTION MACROS
;=============================================================================

; Simple critical section (disables interrupts)
; Use for short, non-reentrant code sections
%macro ENTER_CRITICAL 0
    pushf                      ; Save interrupt flag state
    cli                        ; Disable interrupts
%endmacro

%macro EXIT_CRITICAL 0
    popf                       ; Restore interrupt flag state
%endmacro

; Reentrant critical section with nesting support
; Use when critical sections may be nested
%macro ENTER_CRITICAL_REENTRANT 0
    pushf                      ; Save interrupt flag state
    cli                        ; Disable interrupts
    inc byte [cs:critical_nesting]  ; Increment nesting counter
%endmacro

%macro EXIT_CRITICAL_REENTRANT 0
    dec byte [cs:critical_nesting]  ; Decrement nesting counter
    jnz %%skip_restore         ; If not zero, stay disabled
    popf                       ; Restore interrupt flag state
    jmp short %%done
%%skip_restore:
    add sp, 2                  ; Discard saved flags
%%done:
%endmacro

;=============================================================================
; DOS SAFETY MACROS
;=============================================================================

; Check if DOS is safe to call (InDOS flag = 0)
; Sets ZF if safe, clears ZF if busy
; Preserves: All registers except flags
%macro CHECK_DOS_SAFE 0
    push es
    push bx
    mov es, [cs:indos_segment]
    mov bx, [cs:indos_offset]
    cmp byte [es:bx], 0        ; 0 = DOS available
    pop bx
    pop es
%endmacro

; Complete DOS safety check (InDOS + critical error flags)
; Sets ZF if completely safe, clears ZF if any flag set
; Preserves: All registers except flags
%macro CHECK_DOS_COMPLETELY_SAFE 0
    push es
    push bx
    push ds
    push si

    ; Check InDOS flag
    mov es, [cs:indos_segment]
    mov bx, [cs:indos_offset]
    cmp byte [es:bx], 0
    jnz %%not_safe

    ; Check critical error flag (if available)
    cmp word [cs:criterr_segment], 0
    jz %%safe                  ; No critical error support

    ; GPT-5 Critical Fix: Load offset BEFORE changing DS
    mov si, [cs:criterr_offset]  ; Load offset while CS is still accessible
    mov ds, [cs:criterr_segment] ; Now change DS to criterr segment
    cmp byte [ds:si], 0          ; Check critical error flag

%%not_safe:
%%safe:
    pop si
    pop ds
    pop bx
    pop es
%endmacro

;=============================================================================
; HARDWARE TIMEOUT MACROS
;=============================================================================

; Wait for hardware condition with timeout
; Parameters:
;   %1 - I/O port to check (DX should be set)
;   %2 - Bit mask for condition
;   %3 - Timeout counter value
; Sets CY if timeout, clears CY if condition met
; Preserves: All registers except AX, CX, flags
%macro WAIT_FOR_CONDITION 3
    mov cx, %3
%%wait_loop:
    in ax, dx                  ; Read 16-bit from port (DX = port)
    test ax, %2                ; Check condition bits (supports 16-bit masks)
    jnz %%condition_met        ; Condition satisfied
    loop %%wait_loop           ; Continue waiting

    stc                        ; Timeout - set carry
    jmp short %%done

%%condition_met:
    clc                        ; Success - clear carry
%%done:
%endmacro

;=============================================================================
; STRUCTURE VALIDATION MACROS
;=============================================================================

; Validate protected data structure
; Parameters: BX = pointer to structure
; Sets CY if invalid, clears CY if valid
; Preserves: All registers except AX, flags
%macro VALIDATE_STRUCTURE 0
    push cx
    push si

    ; Check signature
    cmp word [bx + PROTECTED_DATA.signature], SIGNATURE_MAGIC
    jne %%invalid

    ; Calculate checksum
    mov si, bx
    add si, PROTECTED_DATA.data    ; Start of data
    mov cx, [bx + PROTECTED_DATA.length]
    sub cx, PROTECTED_DATA.data    ; Data length
    xor ax, ax

%%checksum_loop:
    xor al, [si]
    inc si
    loop %%checksum_loop

    ; Compare with stored checksum
    cmp ax, [bx + PROTECTED_DATA.checksum]
    jne %%invalid

    clc                        ; Valid structure
    jmp short %%done

%%invalid:
    stc                        ; Invalid structure
%%done:
    pop si
    pop cx
%endmacro

; Update structure checksum after modification
; Parameters: BX = pointer to structure
; Modifies: Checksum field in structure
; Preserves: All registers except AX, CX, SI
%macro UPDATE_STRUCTURE_CHECKSUM 0
    push cx
    push si

    mov si, bx
    add si, PROTECTED_DATA.data
    mov cx, [bx + PROTECTED_DATA.length]
    sub cx, PROTECTED_DATA.data
    xor ax, ax

%%checksum_loop:
    xor al, [si]
    inc si
    loop %%checksum_loop

    mov [bx + PROTECTED_DATA.checksum], ax

    pop si
    pop cx
%endmacro

;=============================================================================
; VECTOR MANAGEMENT MACROS
;=============================================================================

; Check if we still own an interrupt vector
; Parameters: %1 - Interrupt number (e.g., 60h)
;            %2 - Expected handler address
; Sets CY if vector stolen, clears CY if we own it
; Preserves: All registers except flags
%macro CHECK_VECTOR_OWNERSHIP 2
    push es
    push bx
    push ax

    xor ax, ax
    mov es, ax
    mov bx, %1
    shl bx, 2                  ; Vector table offset

    ; Check offset
    cmp word [es:bx], %2
    jne %%stolen

    ; Check segment (can't cmp directly with cs, use ax)
    push cs
    pop ax                     ; AX = CS
    cmp word [es:bx + 2], ax
    jne %%stolen

    pop ax
    clc                        ; We own it
    jmp short %%done

%%stolen:
    pop ax
    stc                        ; Vector stolen
%%done:
    pop bx
    pop es
%endmacro

; Safely install interrupt vector with chaining
; Parameters: %1 - Interrupt number
;            %2 - New handler address
;            %3 - Storage for old vector (4 bytes)
; Preserves: All registers
%macro INSTALL_VECTOR 3
    push ax
    push bx
    push es

    ; Get current vector
    mov ax, 3500h + %1
    int 21h
    mov word [%3], bx          ; Save offset
    mov word [%3 + 2], es      ; Save segment

    ; Install new vector
    push ds
    mov ax, cs
    mov ds, ax
    mov dx, %2
    mov ax, 2500h + %1
    int 21h
    pop ds

    pop es
    pop bx
    pop ax
%endmacro

;=============================================================================
; AMIS COMPLIANCE MACROS
;=============================================================================

; Standard AMIS installation check response
; Parameters: %1 - Pointer to identification string
;            %2 - Version number
; Use in INT 2Fh handler for AL=00h
%macro AMIS_INSTALL_CHECK_RESPONSE 2
    mov al, 0FFh               ; Installed indicator
    mov cx, %2                 ; Version number
    mov dx, cs                 ; Our segment
    mov di, %1                 ; ID string pointer
    iret
%endmacro

; AMIS get entry point response
; Parameters: %1 - Private API entry point
;            %2 - Version number
; Use in INT 2Fh handler for AL=01h
%macro AMIS_GET_ENTRY_POINT_RESPONSE 2
    mov dx, cs                 ; Entry point segment
    mov bx, %1                 ; Entry point offset
    mov cx, %2                 ; Version number
    iret
%endmacro

;=============================================================================
; DEBUGGING AND DIAGNOSTICS MACROS
;=============================================================================

; Debug output macro (conditional compilation)
; Parameters: %1 - Pointer to debug message string
; Only active if DEBUG_BUILD is defined
%macro DEBUG_OUTPUT 1
%ifdef DEBUG_BUILD
    push ax
    push dx
    push si

    mov si, %1
    call debug_print_string

    pop si
    pop dx
    pop ax
%endif
%endmacro

; Assert macro for runtime validation
; Parameters: %1 - Condition to test (sets flags)
;            %2 - Error code if assertion fails
; Only active if DEBUG_BUILD is defined
%macro ASSERT 2
%ifdef DEBUG_BUILD
    %1                         ; Execute condition test
    jz %%assert_ok             ; Jump if condition true (ZF set)

    ; Assertion failed
    mov al, %2
    call assertion_failed

%%assert_ok:
%endif
%endmacro

;=============================================================================
; PERFORMANCE MONITORING MACROS
;=============================================================================

; Start performance timing
; Uses system timer for basic profiling
%macro START_TIMER 0
%ifdef PROFILE_BUILD
    push ax
    push dx

    ; Read system timer
    xor ax, ax
    int 1Ah                    ; Get time in CX:DX
    mov [cs:timer_start_high], cx
    mov [cs:timer_start_low], dx

    pop dx
    pop ax
%endif
%endmacro

; End performance timing and update statistics
; Parameters: %1 - Performance counter to update
%macro END_TIMER 1
%ifdef PROFILE_BUILD
    push ax
    push bx
    push cx
    push dx

    ; Read current time
    xor ax, ax
    int 1Ah

    ; Calculate elapsed time
    sub dx, [cs:timer_start_low]
    sbb cx, [cs:timer_start_high]

    ; Update counter (assuming DX is sufficient for most measurements)
    add [cs:%1], dx
    inc word [cs:%1 + 2]       ; Count field

    pop dx
    pop cx
    pop bx
    pop ax
%endif
%endmacro

;=============================================================================
; MEMORY PROTECTION MACROS
;=============================================================================

; Place memory canary before allocation
; Parameters: %1 - Memory pointer (BX recommended)
; Modifies: Memory at [%1-4], uses SI temporarily
; Note: %1 must be a register for 16-bit addressing compatibility
%macro PLACE_FRONT_CANARY 1
    push si
    mov si, %1
    sub si, 4
    mov dword [si], CANARY_PATTERN
    pop si
%endmacro

; Place memory canary after allocation
; Parameters: %1 - Memory pointer (register)
;            %2 - Allocation size (register or immediate)
; Modifies: Memory at [%1 + %2], uses SI temporarily
%macro PLACE_REAR_CANARY 2
    push si
    mov si, %1
    add si, %2
    mov dword [si], CANARY_PATTERN
    pop si
%endmacro

; Check memory canaries for corruption
; Parameters: %1 - Memory pointer (register)
;            %2 - Allocation size (register or immediate)
; Sets CY if corruption detected
; Uses SI temporarily
%macro CHECK_CANARIES 2
    push si

    ; Check front canary
    mov si, %1
    sub si, 4
    cmp dword [si], CANARY_PATTERN
    jne %%corruption

    ; Check rear canary
    mov si, %1
    add si, %2
    cmp dword [si], CANARY_PATTERN
    jne %%corruption

    pop si
    clc                        ; No corruption
    jmp short %%done

%%corruption:
    pop si
    stc                        ; Corruption detected
%%done:
%endmacro

;=============================================================================
; ERROR HANDLING MACROS
;=============================================================================

; Set packet driver error and return
; Parameters: %1 - Error code constant
; Sets DH = error code, CF = 1, and returns
%macro PACKET_ERROR 1
    mov dh, %1
    stc
    ret
%endmacro

; Return success from packet driver function
; Clears CF and returns
%macro PACKET_SUCCESS 0
    clc
    ret
%endmacro

; Generic error recovery attempt
; Parameters: %1 - Recovery procedure to call
;            %2 - Maximum retry attempts
; Returns: CY set if all attempts failed
%macro RETRY_ON_ERROR 2
    push cx
    mov cx, %2

%%retry_loop:
    call %1
    jnc %%success              ; Recovery succeeded
    loop %%retry_loop          ; Try again

    stc                        ; All attempts failed
    jmp short %%done

%%success:
    clc                        ; Success
%%done:
    pop cx
%endmacro

;=============================================================================
; REQUIRED DATA DECLARATIONS
;=============================================================================

; The following data must be declared in the TSR's data segment:
;
; caller_ss         dw  0    ; Saved caller stack segment
; caller_sp         dw  0    ; Saved caller stack pointer
; critical_nesting  db  0    ; Critical section nesting level
; indos_segment     dw  0    ; InDOS flag segment
; indos_offset      dw  0    ; InDOS flag offset
; criterr_segment   dw  0    ; Critical error flag segment (0 if N/A)
; criterr_offset    dw  0    ; Critical error flag offset
;
; For profiling (if PROFILE_BUILD defined):
; timer_start_high  dw  0    ; Timer start value (high word)
; timer_start_low   dw  0    ; Timer start value (low word)
;
; Stack space:
; driver_stack      resb 1024      ; Private stack space
; stack_top         equ $ - 2      ; Top of stack

;=============================================================================
; INTERRUPT FLAG (IF) PRESERVATION PATTERNS
;=============================================================================
;
; Pattern 1: Testing IF state (when you need to branch on IF value)
; -----------------------------------------------------------------
; Use this when you need to check if interrupts are enabled and take
; different actions based on the state:
;
;   pushf
;   pop     ax
;   test    ax, 0200h           ; Test IF bit (bit 9)
;   jnz     interrupts_enabled
;   ; interrupts are disabled
;   jmp     interrupts_disabled
;
; This is the only reliable way on 8086/286 to test the IF state.
;
; Pattern 2: Preserving IF during critical section (RECOMMENDED)
; ---------------------------------------------------------------
; Use this when you need to disable interrupts for a critical section
; but want to restore the original IF state afterward:
;
;   pushf                       ; Save all flags including IF
;   cli                         ; Disable interrupts
;   ; ... critical section code ...
;   popf                        ; Restore original IF (and other flags)
;
; This is FASTER and SAFER than the test-and-branch pattern when you
; only need to preserve/restore IF, not branch on it.
;
; Pattern 3: Atomic operations with IF preservation
; --------------------------------------------------
; For atomic increment/decrement of 32-bit counters in ISR context:
;
;   pushf                       ; Save current IF state
;   cli                         ; Ensure atomicity
;   inc     word [counter]      ; Low word
;   adc     word [counter+2], 0 ; High word with carry
;   popf                        ; Restore original IF
;
; IMPORTANT: Never use unconditional _enable() or STI after CLI in
; library code, as it may incorrectly enable interrupts when the
; caller had them disabled for a reason.
;
; Pattern 4: C code atomic operations (Watcom C specific)
; --------------------------------------------------------
; In C code, preserve IF when doing atomic operations:
;
;   unsigned short flags;
;   _asm { pushf; pop flags }   /* Save IF state */
;   _disable();                 /* CLI for atomic update */
;   counter++;                  /* Atomic operation */
;   if (flags & 0x0200) _enable(); /* Restore IF if it was set */
;
; This ensures correct IF state preservation in mixed C/ASM code.

%endif ; TSR_DEFENSIVE_INC
