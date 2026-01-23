; TSR Defensive Programming Macros and Constants (WASM/MASM Compatible)
; Provides reusable defensive patterns for DOS TSR development
; Converted from NASM syntax for Open Watcom WASM assembler
;
; Last Updated: 2026-01-23 19:50:00 CET

ifndef TSR_DEFENSIVE_WASM_INC
TSR_DEFENSIVE_WASM_INC equ 1

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
CANARY_PATTERN_LO   equ 0BEEFh
CANARY_PATTERN_HI   equ 0DEADh
SIGNATURE_MAGIC     equ 5A5Ah

;=============================================================================
; DATA STRUCTURES (MASM/WASM Syntax)
;=============================================================================

; IISP header structure for interrupt sharing
IISP_HEADER struc
    entry_jump      db 3 dup(?)     ; Short jump to handler (EB xx 90)
    signature       db 2 dup(?)     ; 'K', 'B'
    flags           db ?            ; IISP_FLAG_SHAREABLE
    chain_offset    dw ?            ; Offset of next handler
    chain_segment   dw ?            ; Segment of next handler
    reserved_field  dw ?            ; Reserved field
IISP_HEADER ends

; Size constant for IISP_HEADER
IISP_HEADER_SIZE    equ 11

; Protected data structure template
PROTECTED_DATA struc
    pd_signature    dw ?            ; SIGNATURE_MAGIC
    pd_length       dw ?            ; Total structure size
    pd_checksum     dw ?            ; XOR checksum of data
    ; pd_data starts here (offset 6)
PROTECTED_DATA ends

; Offset of data within PROTECTED_DATA
PROTECTED_DATA_OFFSET equ 6

; Access type entry structure
ACCESS_TYPE struc
    at_in_use       db ?            ; 0=free, 1=active
    at_packet_type  dw ?            ; Ethernet type (0x0800, etc.)
    at_handler_off  dw ?            ; Receiver function offset
    at_handler_seg  dw ?            ; Receiver function segment
    at_type_length  db ?            ; Length for non-standard types
    at_reserved     db 11 dup(?)    ; Reserved/additional data
ACCESS_TYPE ends

;=============================================================================
; SAFE STACK SWITCHING MACROS
;=============================================================================

; Switch to driver's private stack
; Assumes: CS = data segment, stack space available
; Modifies: SS, SP (obviously)
; Preserves: All other registers
SAFE_STACK_SWITCH macro
    cli                         ; Critical section start
    mov cs:caller_ss, ss        ; Save caller's stack
    mov cs:caller_sp, sp
    mov ax, cs                  ; Use CS as stack segment
    mov ss, ax
    mov sp, offset stack_top    ; Private stack
    sti                         ; Critical section end
endm

; Restore caller's original stack
; Assumes: caller_ss/caller_sp are valid
; Modifies: SS, SP
; Preserves: All other registers
RESTORE_CALLER_STACK macro
    cli                         ; Critical section start
    mov ss, cs:caller_ss        ; Restore caller's stack
    mov sp, cs:caller_sp
    sti                         ; Critical section end
endm

;=============================================================================
; CRITICAL SECTION MACROS
;=============================================================================

; Simple critical section (disables interrupts)
; Use for short, non-reentrant code sections
ENTER_CRITICAL macro
    pushf                       ; Save interrupt flag state
    cli                         ; Disable interrupts
endm

EXIT_CRITICAL macro
    popf                        ; Restore interrupt flag state
endm

; Reentrant critical section with nesting support
; Use when critical sections may be nested
; Note: Uses unique labels for each invocation
ENTER_CRITICAL_REENTRANT macro
    pushf                       ; Save interrupt flag state
    cli                         ; Disable interrupts
    inc byte ptr cs:critical_nesting  ; Increment nesting counter
endm

;=============================================================================
; DOS SAFETY MACROS
;=============================================================================

; Check if DOS is safe to call (InDOS flag = 0)
; Sets ZF if safe, clears ZF if busy
; Preserves: All registers except flags
CHECK_DOS_SAFE macro
    push es
    push bx
    mov es, cs:indos_segment
    mov bx, cs:indos_offset
    cmp byte ptr es:[bx], 0     ; 0 = DOS available
    pop bx
    pop es
endm

; Complete DOS safety check (InDOS + critical error flags)
; Sets ZF if completely safe, clears ZF if any flag set
; Preserves: All registers except flags
; Note: Uses unique labels for each invocation
CHECK_DOS_COMPLETELY_SAFE macro
    LOCAL cdcs_safe, cdcs_not_safe, cdcs_done
    push es
    push bx
    push ds
    push si
    push ax

    ; Check InDOS flag
    mov es, cs:indos_segment
    mov bx, cs:indos_offset
    cmp byte ptr es:[bx], 0
    jnz cdcs_not_safe

    ; Check critical error flag (if available)
    cmp word ptr cs:criterr_segment, 0
    jz cdcs_safe                ; No critical error support

    ; Load offset BEFORE changing DS
    mov si, cs:criterr_offset
    mov ds, cs:criterr_segment
    cmp byte ptr ds:[si], 0     ; Check critical error flag
    jnz cdcs_not_safe

cdcs_safe:
    xor ax, ax                  ; ZF = 1 (safe)
    jmp cdcs_done

cdcs_not_safe:
    mov ax, 1                   ; ZF = 0 (not safe)

cdcs_done:
    or ax, ax                   ; Set flags based on AX
    pop ax
    pop si
    pop ds
    pop bx
    pop es
endm

;=============================================================================
; ERROR HANDLING MACROS
;=============================================================================

; Set packet driver error and return
; Parameters: error_code - Error code constant
; Sets DH = error code, CF = 1, and returns
PACKET_ERROR macro error_code
    mov dh, error_code
    stc
    ret
endm

; Return success from packet driver function
; Clears CF and returns
PACKET_SUCCESS macro
    clc
    ret
endm

;=============================================================================
; REQUIRED DATA DECLARATIONS
;=============================================================================

; The following data must be declared in the TSR's data segment:
;
; caller_ss         dw  ?    ; Saved caller stack segment
; caller_sp         dw  ?    ; Saved caller stack pointer
; critical_nesting  db  0    ; Critical section nesting level
; indos_segment     dw  0    ; InDOS flag segment
; indos_offset      dw  0    ; InDOS flag offset
; criterr_segment   dw  0    ; Critical error flag segment (0 if N/A)
; criterr_offset    dw  0    ; Critical error flag offset
;
; Stack space:
; driver_stack      db  1024 dup(?)  ; Private stack space
; stack_top         equ $ - 2        ; Top of stack

endif ; TSR_DEFENSIVE_WASM_INC
