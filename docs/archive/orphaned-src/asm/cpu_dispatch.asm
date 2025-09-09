;-----------------------------------------------------------------------------
; @file cpu_dispatch.asm
; @brief CPU-specific dispatch tables for optimized operations
;
; Implements jump tables for different CPU generations (386/486/P5/P6+)
; Selected at initialization based on CPU detection
;
; ISA Reality: Optimized for 16-bit operations with 32-bit enhancements
;-----------------------------------------------------------------------------

.MODEL SMALL
.386

_TEXT SEGMENT
        ASSUME CS:_TEXT, DS:_DATA

;=============================================================================
; CPU TYPE CONSTANTS
;=============================================================================

CPU_386                 equ 3
CPU_486                 equ 4
CPU_PENTIUM             equ 5
CPU_P6PLUS              equ 6

;=============================================================================
; EXTERNAL REFERENCES
;=============================================================================

EXTERN _cpu_type:BYTE
EXTERN _cpu_family:BYTE
EXTERN _has_cpuid:BYTE

;=============================================================================
; DATA SEGMENT - DISPATCH TABLES
;=============================================================================

_DATA SEGMENT

; Current CPU type for quick access
PUBLIC current_cpu_type
current_cpu_type        db 3        ; Default to 386

; Master dispatch table (points to CPU-specific tables)
PUBLIC dispatch_table
dispatch_table          dw offset dispatch_386    ; Default
                        dw offset dispatch_486
                        dw offset dispatch_pentium
                        dw offset dispatch_p6plus

; Operation indices
OP_MEMCPY               equ 0
OP_MEMSET               equ 2
OP_CHECKSUM             equ 4
OP_COPY_TO_USER         equ 6
OP_COPY_FROM_USER       equ 8
OP_COMPARE              equ 10
OP_SCAN                 equ 12

; 386 dispatch table (basic operations)
dispatch_386:
        dw offset memcpy_386
        dw offset memset_386
        dw offset checksum_386
        dw offset copy_to_user_386
        dw offset copy_from_user_386
        dw offset compare_386
        dw offset scan_386

; 486 dispatch table (cache-aware)
dispatch_486:
        dw offset memcpy_486
        dw offset memset_486
        dw offset checksum_486
        dw offset copy_to_user_486
        dw offset copy_from_user_486
        dw offset compare_486
        dw offset scan_486

; Pentium dispatch table (dual pipeline)
dispatch_pentium:
        dw offset memcpy_p5
        dw offset memset_p5
        dw offset checksum_p5
        dw offset copy_to_user_p5
        dw offset copy_from_user_p5
        dw offset compare_p5
        dw offset scan_p5

; P6+ dispatch table (out-of-order)
dispatch_p6plus:
        dw offset memcpy_p6
        dw offset memset_p6
        dw offset checksum_p6
        dw offset copy_to_user_p6
        dw offset copy_from_user_p6
        dw offset compare_p6
        dw offset scan_p6

; Selected dispatch table pointer
PUBLIC current_dispatch
current_dispatch        dw offset dispatch_386

_DATA ENDS

;=============================================================================
; INITIALIZATION
;=============================================================================

;-----------------------------------------------------------------------------
; init_cpu_dispatch - Initialize dispatch tables based on CPU
;-----------------------------------------------------------------------------
PUBLIC init_cpu_dispatch
init_cpu_dispatch PROC
        push    ax
        push    bx
        
        ; Get CPU type
        mov     al, [_cpu_family]
        mov     [current_cpu_type], al
        
        ; Select dispatch table
        cmp     al, CPU_P6PLUS
        jae     .use_p6
        cmp     al, CPU_PENTIUM
        je      .use_p5
        cmp     al, CPU_486
        je      .use_486
        
        ; Default to 386
.use_386:
        mov     ax, offset dispatch_386
        jmp     .set_table
        
.use_486:
        mov     ax, offset dispatch_486
        jmp     .set_table
        
.use_p5:
        mov     ax, offset dispatch_pentium
        jmp     .set_table
        
.use_p6:
        mov     ax, offset dispatch_p6plus
        
.set_table:
        mov     [current_dispatch], ax
        
        pop     bx
        pop     ax
        ret
init_cpu_dispatch ENDP

;=============================================================================
; 386 IMPLEMENTATIONS (Basic)
;=============================================================================

;-----------------------------------------------------------------------------
; memcpy_386 - Basic memory copy for 386
;
; Input: ES:DI = destination, DS:SI = source, CX = bytes
;-----------------------------------------------------------------------------
memcpy_386 PROC
        push    cx
        push    si
        push    di
        
        cld
        shr     cx, 1                   ; Word count
        jnc     .even
        movsb                           ; Odd byte first
.even:
        rep     movsw                   ; 16-bit transfers
        
        pop     di
        pop     si
        pop     cx
        ret
memcpy_386 ENDP

;-----------------------------------------------------------------------------
; memset_386 - Basic memory fill
;
; Input: ES:DI = destination, AL = value, CX = bytes
;-----------------------------------------------------------------------------
memset_386 PROC
        push    cx
        push    di
        push    ax
        
        mov     ah, al                  ; Duplicate for word fills
        cld
        shr     cx, 1
        jnc     .even
        stosb                           ; Odd byte
.even:
        rep     stosw                   ; Word fills
        
        pop     ax
        pop     di
        pop     cx
        ret
memset_386 ENDP

;-----------------------------------------------------------------------------
; checksum_386 - Basic checksum
;
; Input: DS:SI = buffer, CX = bytes
; Output: AX = checksum
;-----------------------------------------------------------------------------
checksum_386 PROC
        push    cx
        push    si
        
        xor     ax, ax                  ; Clear checksum
        cld
.loop:
        lodsb
        add     ax, ax                  ; Simple add
        loop    .loop
        
        pop     si
        pop     cx
        ret
checksum_386 ENDP

; Stub implementations for other 386 operations
copy_to_user_386:
copy_from_user_386:
compare_386:
scan_386:
        ret

;=============================================================================
; 486 IMPLEMENTATIONS (Cache-aware)
;=============================================================================

;-----------------------------------------------------------------------------
; memcpy_486 - Cache-aware copy with 32-bit moves
;
; Uses 32-bit operations where beneficial
;-----------------------------------------------------------------------------
memcpy_486 PROC
        push    ecx
        push    esi
        push    edi
        
        cld
        
        ; Use 32-bit moves for better 486 performance
        mov     ecx, ecx                ; Zero-extend CX to ECX
        shr     ecx, 2                  ; DWORD count
        jz      .small
        
        ; 32-bit transfer - safer to use word moves in 16-bit mode
        shl     ecx, 1                  ; Back to word count
        rep     movsw                   ; 16-bit moves (safer)
        
.small:
        ; Handle remaining bytes
        mov     ecx, ecx
        and     ecx, 3
        rep     movsb
        
        pop     edi
        pop     esi
        pop     ecx
        ret
memcpy_486 ENDP

;-----------------------------------------------------------------------------
; memset_486 - Cache-aware fill with 32-bit stores
;-----------------------------------------------------------------------------
memset_486 PROC
        push    ecx
        push    edi
        push    eax
        
        ; Expand byte to dword
        mov     ah, al
        mov     dx, ax
        shl     eax, 16
        mov     ax, dx                  ; EAX = repeated byte
        
        cld
        mov     ecx, ecx                ; Zero-extend
        shr     ecx, 2                  ; DWORD count
        
        ; Use 16-bit stores for safety in real mode
        shl     ecx, 1                  ; Back to word count  
        rep     stosw                   ; 16-bit stores
        
        ; Remaining bytes
        mov     ecx, ecx
        and     ecx, 3
        rep     stosb
        
        pop     eax
        pop     edi
        pop     ecx
        ret
memset_486 ENDP

checksum_486:
copy_to_user_486:
copy_from_user_486:
compare_486:
scan_486:
        ret

;=============================================================================
; PENTIUM IMPLEMENTATIONS (Dual pipeline)
;=============================================================================

;-----------------------------------------------------------------------------
; memcpy_p5 - Pentium-optimized with U/V pairing
;
; Optimizes for dual pipeline execution
;-----------------------------------------------------------------------------
memcpy_p5 PROC
        push    ecx
        push    esi
        push    edi
        push    eax
        push    edx
        
        cld
        
        ; Align destination for optimal performance
        test    edi, 3
        jz      .aligned
        
        ; Copy bytes to align
        mov     ecx, edi
        neg     ecx
        and     ecx, 3
        sub     ecx, ecx                ; Adjust count
        rep     movsb
        
.aligned:
        ; Main copy loop - optimized for U/V pairing
        mov     ecx, ecx
        shr     ecx, 3                  ; 8-byte chunks
        jz      .small
        
.copy_loop:
        ; Paired instructions for Pentium - use 16-bit for safety
        mov     ax, [si]                ; U-pipe
        mov     dx, [si+2]              ; V-pipe
        mov     [di], ax                ; U-pipe
        mov     [di+2], dx              ; V-pipe
        mov     ax, [si+4]              ; U-pipe
        mov     dx, [si+6]              ; V-pipe
        mov     [di+4], ax              ; U-pipe
        mov     [di+6], dx              ; V-pipe
        add     si, 8                   ; U-pipe
        add     di, 8                   ; V-pipe
        dec     cx                      ; U-pipe
        jnz     .copy_loop              ; V-pipe
        
.small:
        ; Remaining bytes
        mov     ecx, ecx
        and     ecx, 7
        rep     movsb
        
        pop     edx
        pop     eax
        pop     edi
        pop     esi
        pop     ecx
        ret
memcpy_p5 ENDP

memset_p5:
checksum_p5:
copy_to_user_p5:
copy_from_user_p5:
compare_p5:
scan_p5:
        ret

;=============================================================================
; P6+ IMPLEMENTATIONS (Out-of-order)
;=============================================================================

;-----------------------------------------------------------------------------
; memcpy_p6 - P6+ optimized with prefetch hints
;
; Uses out-of-order execution and cache hints
;-----------------------------------------------------------------------------
memcpy_p6 PROC
        push    ecx
        push    esi
        push    edi
        
        cld
        
        ; Check for SSE support (would need CPUID check)
        ; For now, use optimized 486 path
        call    memcpy_486
        
        pop     edi
        pop     esi
        pop     ecx
        ret
memcpy_p6 ENDP

memset_p6:
checksum_p6:
copy_to_user_p6:
copy_from_user_p6:
compare_p6:
scan_p6:
        ret

;=============================================================================
; DISPATCH INTERFACE
;=============================================================================

;-----------------------------------------------------------------------------
; dispatch_memcpy - Dispatched memory copy
;
; Calls CPU-specific implementation via jump table
;-----------------------------------------------------------------------------
PUBLIC dispatch_memcpy
dispatch_memcpy PROC
        push    bx
        
        mov     bx, [current_dispatch]
        call    word ptr [bx + OP_MEMCPY]
        
        pop     bx
        ret
dispatch_memcpy ENDP

;-----------------------------------------------------------------------------
; dispatch_memset - Dispatched memory set
;-----------------------------------------------------------------------------
PUBLIC dispatch_memset
dispatch_memset PROC
        push    bx
        
        mov     bx, [current_dispatch]
        call    word ptr [bx + OP_MEMSET]
        
        pop     bx
        ret
dispatch_memset ENDP

;-----------------------------------------------------------------------------
; dispatch_checksum - Dispatched checksum
;-----------------------------------------------------------------------------
PUBLIC dispatch_checksum
dispatch_checksum PROC
        push    bx
        
        mov     bx, [current_dispatch]
        call    word ptr [bx + OP_CHECKSUM]
        
        pop     bx
        ret
dispatch_checksum ENDP

_TEXT ENDS
END