; ===========================================================================
; rtlib.inc - Runtime Library Macros for 16-bit DOS Real-Mode Assembly
; ===========================================================================
; Project: 3Com Packet Driver
; Created: 2026-02-01 (timestamp)
; Last Modified: 2026-02-01 00:00:00
;
; Description:
;   Shared NASM macros for common operations in 16-bit real-mode DOS
;   assembly programming. These macros provide memory operations and
;   32-bit arithmetic for 8086-compatible code.
;
; Usage:
;   %include "rtlib.inc"
;
; Requirements:
;   - NASM assembler
;   - [BITS 16] mode
;   - [CPU 8086] compatible
; ===========================================================================

[BITS 16]
[CPU 8086]

; ---------------------------------------------------------------------------
; MEMSET_ZERO - Zero a memory region
; ---------------------------------------------------------------------------
; Description:
;   Fills a memory region with zeros using REP STOSB
;
; Parameters:
;   ES:DI - Destination address
;   CX    - Number of bytes to zero
;
; Modifies:
;   AL, CX, DI, flags
;
; Example:
;   mov ax, ds
;   mov es, ax
;   mov di, buffer
;   mov cx, 256
;   MEMSET_ZERO
; ---------------------------------------------------------------------------
%macro MEMSET_ZERO 0
    xor al, al              ; AL = 0
    rep stosb               ; Fill ES:DI with zeros (CX bytes)
%endmacro

; ---------------------------------------------------------------------------
; MEMCPY_NEAR - Near memory copy
; ---------------------------------------------------------------------------
; Description:
;   Copies a memory block from DS:SI to ES:DI using REP MOVSB
;
; Parameters:
;   DS:SI - Source address
;   ES:DI - Destination address
;   CX    - Number of bytes to copy
;
; Modifies:
;   CX, SI, DI, flags
;
; Example:
;   mov si, source_buffer
;   mov di, dest_buffer
;   mov cx, 100
;   MEMCPY_NEAR
; ---------------------------------------------------------------------------
%macro MEMCPY_NEAR 0
    rep movsb               ; Copy DS:SI -> ES:DI (CX bytes)
%endmacro

; ---------------------------------------------------------------------------
; MEMCMP_NEAR - Near memory compare
; ---------------------------------------------------------------------------
; Description:
;   Compares two memory blocks and sets the Zero Flag (ZF)
;   ZF=1 if blocks are equal, ZF=0 if different
;
; Parameters:
;   DS:SI - First memory block
;   ES:DI - Second memory block
;   CX    - Number of bytes to compare
;
; Modifies:
;   CX, SI, DI, flags
;
; Returns:
;   ZF - Set if blocks are equal, cleared if different
;
; Example:
;   mov si, buffer1
;   mov di, buffer2
;   mov cx, 6
;   MEMCMP_NEAR
;   je blocks_equal
; ---------------------------------------------------------------------------
%macro MEMCMP_NEAR 0
    repe cmpsb              ; Compare DS:SI vs ES:DI (CX bytes)
%endmacro

; ---------------------------------------------------------------------------
; ADD32 - 32-bit addition
; ---------------------------------------------------------------------------
; Description:
;   Adds two 32-bit values stored in register pairs
;
; Parameters:
;   %1:%2 - Destination (high:low) - receives result
;   %3:%4 - Source (high:low) - value to add
;
; Modifies:
;   %1, %2, flags
;
; Example:
;   ADD32 dx, ax, cx, bx    ; DX:AX += CX:BX
; ---------------------------------------------------------------------------
%macro ADD32 4
    add %2, %4              ; Add low words
    adc %1, %3              ; Add high words with carry
%endmacro

; ---------------------------------------------------------------------------
; SUB32 - 32-bit subtraction
; ---------------------------------------------------------------------------
; Description:
;   Subtracts two 32-bit values stored in register pairs
;
; Parameters:
;   %1:%2 - Destination (high:low) - receives result
;   %3:%4 - Source (high:low) - value to subtract
;
; Modifies:
;   %1, %2, flags
;
; Example:
;   SUB32 dx, ax, cx, bx    ; DX:AX -= CX:BX
; ---------------------------------------------------------------------------
%macro SUB32 4
    sub %2, %4              ; Subtract low words
    sbb %1, %3              ; Subtract high words with borrow
%endmacro

; ---------------------------------------------------------------------------
; CMP32 - 32-bit comparison
; ---------------------------------------------------------------------------
; Description:
;   Compares two 32-bit values and sets flags accordingly
;   Sets carry, zero, and sign flags based on the comparison
;
; Parameters:
;   %1:%2 - First value (high:low)
;   %3:%4 - Second value (high:low)
;
; Modifies:
;   flags only
;
; Example:
;   CMP32 dx, ax, cx, bx    ; Compare DX:AX with CX:BX
;   jb first_is_less
; ---------------------------------------------------------------------------
%macro CMP32 4
    cmp %1, %3              ; Compare high words
    jne %%done              ; If different, we're done
    cmp %2, %4              ; Compare low words if high are equal
%%done:
%endmacro

; ---------------------------------------------------------------------------
; SHL32 - 32-bit shift left by 1
; ---------------------------------------------------------------------------
; Description:
;   Shifts a 32-bit value left by 1 bit position
;
; Parameters:
;   %1:%2 - Register pair (high:low) to shift
;
; Modifies:
;   %1, %2, flags (carry flag gets MSB)
;
; Example:
;   SHL32 dx, ax            ; DX:AX <<= 1
; ---------------------------------------------------------------------------
%macro SHL32 2
    shl %2, 1               ; Shift low word left
    rcl %1, 1               ; Rotate high word left with carry
%endmacro

; ---------------------------------------------------------------------------
; INC32 - 32-bit increment in memory
; ---------------------------------------------------------------------------
; Description:
;   Increments a 32-bit value stored in memory at seg:offset
;
; Parameters:
;   %1 - Segment register or segment value
;   %2 - Offset (memory location)
;
; Modifies:
;   flags, memory at %1:%2
;
; Example:
;   INC32 ds, [packet_count]    ; Increment dword at DS:[packet_count]
; ---------------------------------------------------------------------------
%macro INC32 2
    add word [%2], 1        ; Increment low word
    adc word [%2 + 2], 0    ; Add carry to high word
%endmacro

; ---------------------------------------------------------------------------
; LOAD_SEG_VAR - Load a far variable's segment into a register
; ---------------------------------------------------------------------------
; Description:
;   Loads the segment portion of a far pointer/variable into a register
;
; Parameters:
;   %1 - Destination register (typically ES, DS, etc.)
;   %2 - Far variable name
;
; Modifies:
;   %1
;
; Example:
;   LOAD_SEG_VAR es, far_buffer    ; ES = segment of far_buffer
; ---------------------------------------------------------------------------
%macro LOAD_SEG_VAR 2
    mov %1, [%2 + 2]        ; Load segment word (high word of far pointer)
%endmacro

; ===========================================================================
; End of rtlib.inc
; ===========================================================================
