; memory_ops.asm
; CPU-Optimized Memory Operations for Memory Pool Module
;
; Agent 11 - Memory Management - Day 3-4 Deliverable
;
; Provides CPU-optimized copy and move primitives with 64KB boundary
; handling and automatic CPU detection for optimal performance.
;
; This file is part of the 3Com Packet Driver project.

.MODEL SMALL
.CODE

; External symbols from CPU detection
EXTRN cpu_get_type:FAR
EXTRN cpu_supports_32bit:FAR

; Memory operation result codes
MEMOP_SUCCESS       EQU 0
MEMOP_ERROR_PARAM   EQU -1
MEMOP_ERROR_BOUNDARY EQU -2
MEMOP_ERROR_OVERFLOW EQU -3

; CPU type constants (matching cpu_detect.h)
CPU_TYPE_8086       EQU 0
CPU_TYPE_80286      EQU 1  
CPU_TYPE_80386      EQU 2
CPU_TYPE_80486      EQU 3
CPU_TYPE_PENTIUM    EQU 4

; Maximum safe copy size per operation
MAX_COPY_SIZE       EQU 32768  ; 32KB max per call

;==============================================================================
; PUBLIC FUNCTIONS
;==============================================================================

PUBLIC mempool_copy_optimized
PUBLIC mempool_move_optimized
PUBLIC mempool_set_optimized
PUBLIC mempool_compare_optimized
PUBLIC mempool_copy_64kb_safe
PUBLIC mempool_move_64kb_safe
PUBLIC mempool_copy_string_safe
PUBLIC mempool_validate_range

;==============================================================================
; CPU-Optimized Memory Copy
; 
; Entry: DS:SI = source, ES:DI = destination, CX = byte count
; Exit:  AX = result code, flags set
; Uses:  All registers except BP and segment registers
;==============================================================================
mempool_copy_optimized PROC FAR
    push    bp
    mov     bp, sp
    push    bx
    push    dx
    
    ; Get parameters from stack
    mov     si, [bp+6]      ; source offset
    mov     ax, [bp+8]      ; source segment
    mov     ds, ax
    mov     di, [bp+10]     ; dest offset
    mov     ax, [bp+12]     ; dest segment
    mov     es, ax
    mov     cx, [bp+14]     ; byte count
    
    ; Validate parameters
    test    cx, cx
    jz      copy_success    ; Zero bytes = success
    cmp     cx, MAX_COPY_SIZE
    ja      copy_param_error
    
    ; Check for null pointers (segment 0)
    mov     ax, ds
    test    ax, ax
    jz      copy_param_error
    mov     ax, es
    test    ax, ax
    jz      copy_param_error
    
    ; Detect CPU type and select optimal copy routine
    call    cpu_get_type
    cmp     al, CPU_TYPE_80386
    jae     copy_32bit      ; 386+ supports 32-bit operations
    cmp     al, CPU_TYPE_80286
    jae     copy_16bit      ; 286 supports 16-bit operations
    jmp     copy_8bit       ; 8086/8088 byte operations only

copy_32bit:
    ; Check if we can use 32-bit operations
    call    cpu_supports_32bit
    test    ax, ax
    jz      copy_16bit
    
    ; 32-bit optimized copy for 386+
    push    ecx             ; Save full count
    
    ; Check alignment for optimal 32-bit transfers
    mov     ax, si
    or      ax, di
    test    ax, 3           ; Check if both are 4-byte aligned
    jnz     copy_32_unaligned
    
    ; Both aligned to 4-byte boundary
    shr     cx, 2           ; Convert to dword count
    jz      copy_32_remainder
    
    ; Use 32-bit string operations
copy_32_loop:
    db      66h             ; 32-bit operand override
    movsw                   ; Move 32-bit dword
    loop    copy_32_loop
    
copy_32_remainder:
    pop     ecx
    and     cx, 3           ; Get remainder bytes
    jz      copy_success
    rep     movsb           ; Copy remaining bytes
    jmp     copy_success

copy_32_unaligned:
    pop     ecx             ; Restore count
    ; Fall through to 16-bit copy for unaligned data

copy_16bit:
    ; 16-bit optimized copy for 286+
    ; Check for word alignment
    mov     ax, si
    or      ax, di
    test    ax, 1           ; Check if both are word-aligned
    jnz     copy_8bit       ; Use byte copy if unaligned
    
    ; Both word-aligned
    push    cx              ; Save byte count
    shr     cx, 1           ; Convert to word count
    jz      copy_16_remainder
    
    ; Use 16-bit string operations
    rep     movsw           ; Copy words
    
copy_16_remainder:
    pop     cx
    test    cx, 1           ; Check for odd byte
    jz      copy_success
    movsb                   ; Copy final byte
    jmp     copy_success

copy_8bit:
    ; Basic byte copy for all CPUs
    rep     movsb
    
copy_success:
    xor     ax, ax          ; Return success
    jmp     copy_exit

copy_param_error:
    mov     ax, MEMOP_ERROR_PARAM
    
copy_exit:
    pop     dx
    pop     bx
    pop     bp
    retf
mempool_copy_optimized ENDP

;==============================================================================
; CPU-Optimized Memory Move (handles overlapping regions)
;
; Entry: DS:SI = source, ES:DI = destination, CX = byte count
; Exit:  AX = result code
;==============================================================================
mempool_move_optimized PROC FAR
    push    bp
    mov     bp, sp
    push    bx
    push    dx
    
    ; Get parameters
    mov     si, [bp+6]      ; source offset
    mov     ax, [bp+8]      ; source segment
    mov     ds, ax
    mov     di, [bp+10]     ; dest offset
    mov     ax, [bp+12]     ; dest segment
    mov     es, ax
    mov     cx, [bp+14]     ; byte count
    
    ; Validate parameters
    test    cx, cx
    jz      move_success
    cmp     cx, MAX_COPY_SIZE
    ja      move_param_error
    
    ; Check for overlap
    ; If source and dest are in same segment, check for overlap
    mov     ax, ds
    cmp     ax, es
    jne     move_no_overlap
    
    ; Same segment - check for overlap
    cmp     si, di
    je      move_success    ; Same address = no work needed
    ja      move_backward   ; Source > dest, copy backward
    
    ; Source < dest, check if ranges overlap
    mov     ax, si
    add     ax, cx
    cmp     ax, di
    jbe     move_no_overlap ; No overlap
    
move_backward:
    ; Copy backward to handle overlap
    add     si, cx          ; Point to end of source
    add     di, cx          ; Point to end of dest
    dec     si              ; Adjust for last byte
    dec     di
    std                     ; Set direction flag for backward copy
    rep     movsb
    cld                     ; Clear direction flag
    jmp     move_success

move_no_overlap:
    ; No overlap, use regular copy
    call    mempool_copy_optimized
    jmp     move_exit

move_success:
    xor     ax, ax
    jmp     move_exit

move_param_error:
    mov     ax, MEMOP_ERROR_PARAM
    
move_exit:
    pop     dx
    pop     bx
    pop     bp
    retf
mempool_move_optimized ENDP

;==============================================================================
; CPU-Optimized Memory Set
;
; Entry: ES:DI = destination, AL = fill value, CX = byte count
; Exit:  AX = result code
;==============================================================================
mempool_set_optimized PROC FAR
    push    bp
    mov     bp, sp
    push    bx
    push    dx
    
    ; Get parameters
    mov     di, [bp+6]      ; dest offset
    mov     ax, [bp+8]      ; dest segment
    mov     es, ax
    mov     al, [bp+10]     ; fill value (low byte)
    mov     cx, [bp+12]     ; byte count
    
    ; Validate parameters
    test    cx, cx
    jz      set_success
    cmp     cx, MAX_COPY_SIZE
    ja      set_param_error
    
    ; Detect CPU type for optimization
    push    ax              ; Save fill value
    call    cpu_get_type
    mov     bl, al          ; Save CPU type
    pop     ax              ; Restore fill value
    
    cmp     bl, CPU_TYPE_80386
    jae     set_32bit
    cmp     bl, CPU_TYPE_80286
    jae     set_16bit
    jmp     set_8bit

set_32bit:
    ; 32-bit optimized set for 386+
    call    cpu_supports_32bit
    test    ax, ax
    jz      set_16bit
    
    ; Check alignment
    test    di, 3
    jnz     set_16bit       ; Not aligned, use 16-bit
    
    ; Create 32-bit pattern
    mov     ah, al          ; AX = pattern
    push    ax
    shl     eax, 16         ; Move to high word
    pop     ax              ; EAX = 32-bit pattern
    
    push    ecx
    shr     cx, 2           ; Dword count
    jz      set_32_remainder
    
    ; Use 32-bit store
set_32_loop:
    db      66h             ; 32-bit operand override
    stosw                   ; Store 32-bit value
    loop    set_32_loop
    
set_32_remainder:
    pop     ecx
    and     cx, 3           ; Remainder bytes
    jz      set_success
    rep     stosb
    jmp     set_success

set_16bit:
    ; 16-bit optimized set
    test    di, 1
    jnz     set_8bit        ; Not word aligned
    
    ; Create word pattern
    mov     ah, al          ; AX = word pattern
    push    cx
    shr     cx, 1           ; Word count
    jz      set_16_remainder
    
    rep     stosw           ; Store words
    
set_16_remainder:
    pop     cx
    test    cx, 1
    jz      set_success
    stosb                   ; Store final byte
    jmp     set_success

set_8bit:
    ; Basic byte set
    rep     stosb

set_success:
    xor     ax, ax
    jmp     set_exit

set_param_error:
    mov     ax, MEMOP_ERROR_PARAM
    
set_exit:
    pop     dx
    pop     bx
    pop     bp
    retf
mempool_set_optimized ENDP

;==============================================================================
; CPU-Optimized Memory Compare
;
; Entry: DS:SI = buffer1, ES:DI = buffer2, CX = byte count
; Exit:  AX = result (-1, 0, 1), ZF set if equal
;==============================================================================
mempool_compare_optimized PROC FAR
    push    bp
    mov     bp, sp
    push    bx
    push    dx
    
    ; Get parameters
    mov     si, [bp+6]      ; buf1 offset
    mov     ax, [bp+8]      ; buf1 segment
    mov     ds, ax
    mov     di, [bp+10]     ; buf2 offset
    mov     ax, [bp+12]     ; buf2 segment
    mov     es, ax
    mov     cx, [bp+14]     ; byte count
    
    ; Validate parameters
    test    cx, cx
    jz      compare_equal   ; Zero bytes = equal
    
    ; Detect CPU and use optimal comparison
    call    cpu_get_type
    cmp     al, CPU_TYPE_80286
    jae     compare_16bit
    
compare_8bit:
    ; Byte-by-byte comparison
    repe    cmpsb
    jmp     compare_check_result

compare_16bit:
    ; Check word alignment
    mov     ax, si
    or      ax, di
    test    ax, 1
    jnz     compare_8bit    ; Not aligned, use byte compare
    
    ; Word-aligned comparison
    push    cx
    shr     cx, 1           ; Word count
    jz      compare_16_remainder
    
    repe    cmpsw
    jne     compare_word_diff
    
compare_16_remainder:
    pop     cx
    test    cx, 1           ; Check odd byte
    jz      compare_equal
    cmpsb                   ; Compare final byte
    jmp     compare_check_result

compare_word_diff:
    ; Words differ, need to determine which byte
    pop     cx              ; Restore original count
    sub     si, 2           ; Back up to differing word
    sub     di, 2
    cmpsb                   ; Compare first byte of word
    jne     compare_check_result
    cmpsb                   ; Compare second byte
    
compare_check_result:
    jc      compare_less    ; CF set = first < second
    jz      compare_equal   ; ZF set = equal
    
compare_greater:
    mov     ax, 1           ; First > second
    jmp     compare_exit

compare_less:
    mov     ax, -1          ; First < second
    jmp     compare_exit

compare_equal:
    xor     ax, ax          ; Equal
    
compare_exit:
    pop     dx
    pop     bx
    pop     bp
    retf
mempool_compare_optimized ENDP

;==============================================================================
; 64KB Boundary Safe Copy
;
; Handles copies that might cross 64KB boundaries by splitting them
; Entry: DS:SI = source, ES:DI = dest, CX = count
; Exit:  AX = result code
;==============================================================================
mempool_copy_64kb_safe PROC FAR
    push    bp
    mov     bp, sp
    push    bx
    push    dx
    
    ; Get parameters
    mov     si, [bp+6]
    mov     ax, [bp+8]
    mov     ds, ax
    mov     di, [bp+10]
    mov     ax, [bp+12]
    mov     es, ax
    mov     cx, [bp+14]
    
    test    cx, cx
    jz      safe_copy_success
    
safe_copy_loop:
    ; Calculate bytes to next 64KB boundary for source
    mov     ax, si
    neg     ax              ; AX = bytes to 64KB boundary
    and     ax, 0FFFFh      ; Ensure positive
    cmp     ax, cx
    jae     safe_copy_check_dest ; Source won't cross boundary
    mov     bx, ax          ; BX = max bytes before source boundary
    jmp     safe_copy_check_dest_with_limit

safe_copy_check_dest:
    ; Source won't cross, check destination
    mov     ax, di
    neg     ax
    and     ax, 0FFFFh
    cmp     ax, cx
    jae     safe_copy_direct ; Neither will cross boundary
    mov     bx, ax          ; BX = max bytes before dest boundary
    jmp     safe_copy_limited

safe_copy_check_dest_with_limit:
    ; Source will cross, check if dest also limits
    mov     ax, di
    neg     ax
    and     ax, 0FFFFh
    cmp     ax, bx
    jae     safe_copy_limited ; Source limit is smaller
    mov     bx, ax          ; Use dest limit

safe_copy_limited:
    ; Copy limited chunk
    push    cx              ; Save total count
    mov     cx, bx          ; Copy limited amount
    call    mempool_copy_optimized
    test    ax, ax
    jnz     safe_copy_error
    
    ; Update pointers and counts
    add     si, bx
    add     di, bx
    pop     cx              ; Restore total count
    sub     cx, bx          ; Reduce remaining count
    jnz     safe_copy_loop  ; Continue if more to copy
    jmp     safe_copy_success

safe_copy_direct:
    ; Can copy all at once
    call    mempool_copy_optimized
    jmp     safe_copy_exit

safe_copy_success:
    xor     ax, ax
    jmp     safe_copy_exit

safe_copy_error:
    ; Error occurred, clean up stack
    add     sp, 2           ; Remove saved CX
    
safe_copy_exit:
    pop     dx
    pop     bx
    pop     bp
    retf
mempool_copy_64kb_safe ENDP

;==============================================================================
; 64KB Boundary Safe Move
;
; Entry: DS:SI = source, ES:DI = dest, CX = count
; Exit:  AX = result code
;==============================================================================
mempool_move_64kb_safe PROC FAR
    push    bp
    mov     bp, sp
    
    ; For move operations, we need to be more careful about overlap
    ; For now, use the safe copy (could be optimized further)
    call    mempool_copy_64kb_safe
    
    pop     bp
    retf
mempool_move_64kb_safe ENDP

;==============================================================================
; String Copy with Length Limit (like strncpy but safer)
;
; Entry: DS:SI = source, ES:DI = dest, CX = max length
; Exit:  AX = actual bytes copied
;==============================================================================
mempool_copy_string_safe PROC FAR
    push    bp
    mov     bp, sp
    push    bx
    push    dx
    
    ; Get parameters
    mov     si, [bp+6]
    mov     ax, [bp+8]
    mov     ds, ax
    mov     di, [bp+10]
    mov     ax, [bp+12]
    mov     es, ax
    mov     cx, [bp+14]     ; Max length
    
    xor     bx, bx          ; BX = bytes copied
    test    cx, cx
    jz      string_copy_done
    
string_copy_loop:
    lodsb                   ; Load source byte
    stosb                   ; Store to dest
    inc     bx              ; Count copied byte
    test    al, al          ; Check for null terminator
    jz      string_copy_done
    loop    string_copy_loop
    
    ; Ensure null termination if we hit length limit
    mov     al, 0
    mov     es:[di-1], al
    
string_copy_done:
    mov     ax, bx          ; Return bytes copied
    
    pop     dx
    pop     bx
    pop     bp
    retf
mempool_copy_string_safe ENDP

;==============================================================================
; Validate Memory Range
;
; Checks if a memory range is valid and doesn't cross segments
; Entry: DS:SI = address, CX = length
; Exit:  AX = 0 if valid, error code if invalid
;==============================================================================
mempool_validate_range PROC FAR
    push    bp
    mov     bp, sp
    push    bx
    push    dx
    
    ; Get parameters
    mov     si, [bp+6]      ; offset
    mov     ax, [bp+8]      ; segment
    mov     ds, ax
    mov     cx, [bp+10]     ; length
    
    ; Check for null segment
    test    ax, ax
    jz      validate_error
    
    ; Check for zero length
    test    cx, cx
    jz      validate_success
    
    ; Check if range wraps around segment
    mov     ax, si
    add     ax, cx
    jc      validate_overflow ; Overflow in 16-bit addition
    
    ; Check if it exceeds segment limit (64KB)
    cmp     ax, si          ; Should be >= original offset
    jb      validate_overflow
    
validate_success:
    xor     ax, ax
    jmp     validate_exit

validate_error:
    mov     ax, MEMOP_ERROR_PARAM
    jmp     validate_exit

validate_overflow:
    mov     ax, MEMOP_ERROR_OVERFLOW
    
validate_exit:
    pop     dx
    pop     bx
    pop     bp
    retf
mempool_validate_range ENDP

;==============================================================================
; Data Section
;==============================================================================
.DATA

; Operation counters for statistics
copy_operations     DD  0
move_operations     DD  0
set_operations      DD  0
compare_operations  DD  0
boundary_splits     DD  0

END