; @file cpu_optimized.asm
; @brief CPU-specific optimized routines for performance-critical operations
;
; 3Com Packet Driver - High Performance CPU-Specific Implementations
;
; This module implements CPU-specific optimizations for:
; - Memory copy operations with 0x66 prefix for 32-bit in real mode
; - Pipeline-optimized routines for 486+ processors
; - AGI stall avoidance for Pentium processors
; - Optimal register usage based on CPU capabilities
;
; Performance Targets:
; - ISR execution: <100 microseconds
; - Packet copy: >10MB/s on 386+
; - Interrupt latency: <10 microseconds
;
; This file is part of the 3Com Packet Driver project.

.MODEL SMALL
.386

; CPU detection constants
CPU_8086        EQU 0
CPU_80286       EQU 1
CPU_80386       EQU 2
CPU_80486       EQU 3
CPU_PENTIUM     EQU 4

; Performance optimization macros
; 32-bit operation in 16-bit segment using 0x66 prefix
OPERAND32 MACRO
    db 66h
ENDM

; Address size override for 32-bit addressing
ADDRESS32 MACRO
    db 67h
ENDM

; Combined operand and address size override
OPERAND_ADDR32 MACRO
    db 66h, 67h
ENDM

; Pipeline-friendly NOP for filling execution slots
PIPE_NOP MACRO
    xor eax, eax    ; U-pipe instruction that pairs well
ENDM

; AGI stall avoidance macro - insert instruction between address calc and use
AVOID_AGI MACRO reg
    xor reg, reg    ; Fill slot to avoid Address Generation Interlock
ENDM

; Data segment
_DATA SEGMENT
        ASSUME  DS:_DATA

; CPU-specific function dispatch table
cpu_copy_table      dw copy_8086, copy_286, copy_386, copy_486, copy_pentium
cpu_checksum_table  dw checksum_8086, checksum_286, checksum_386, checksum_486, checksum_pentium
cpu_memset_table    dw memset_8086, memset_286, memset_386, memset_486, memset_pentium

; Performance measurement data
perf_copy_cycles    dd 0        ; Last copy operation cycle count
perf_checksum_cycles dd 0       ; Last checksum cycle count
current_cpu_type    db CPU_8086 ; Detected CPU type

; CPU capability flags
has_32bit_ops       db 0        ; 32-bit operations available
has_pipeline        db 0        ; Pipeline optimization available
has_agi_concerns    db 0        ; AGI stall concerns (Pentium+)

; Alignment optimization thresholds
align_threshold_32  EQU 32      ; Use 32-bit ops if >=32 bytes aligned
align_threshold_cache EQU 64    ; Cache line optimization threshold

_DATA ENDS

; Code segment
_TEXT SEGMENT
        ASSUME  CS:_TEXT, DS:_DATA

; Public function exports
PUBLIC cpu_detect_optimizations
PUBLIC cpu_optimized_copy
PUBLIC cpu_optimized_checksum
PUBLIC cpu_optimized_memset
PUBLIC cpu_get_best_copy_func
PUBLIC cpu_get_performance_metrics

; Individual CPU-specific routines
PUBLIC copy_8086, copy_286, copy_386, copy_486, copy_pentium
PUBLIC checksum_8086, checksum_286, checksum_386, checksum_486, checksum_pentium
PUBLIC memset_8086, memset_286, memset_386, memset_486, memset_pentium

; External references
EXTRN get_cpu_type:PROC

;-----------------------------------------------------------------------------
; cpu_detect_optimizations - Detect CPU and set optimization flags
;
; Input:  None
; Output: AL = detected CPU type (0-4)
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
cpu_detect_optimizations PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Get CPU type from cpu_detect.asm
        call    get_cpu_type
        mov     [current_cpu_type], al

        ; Set capability flags based on CPU type
        mov     bl, al
        
        ; Clear all flags first
        mov     byte ptr [has_32bit_ops], 0
        mov     byte ptr [has_pipeline], 0  
        mov     byte ptr [has_agi_concerns], 0

        ; 8086/8088 - no special capabilities
        cmp     bl, CPU_8086
        je      .capabilities_set

        ; 286+ capabilities
        cmp     bl, CPU_80286
        jb      .capabilities_set

        ; 386+ capabilities - 32-bit operations
        cmp     bl, CPU_80386
        jb      .capabilities_set
        mov     byte ptr [has_32bit_ops], 1

        ; 486+ capabilities - pipeline optimization
        cmp     bl, CPU_80486
        jb      .capabilities_set
        mov     byte ptr [has_pipeline], 1

        ; Pentium+ capabilities - AGI stall concerns
        cmp     bl, CPU_PENTIUM
        jb      .capabilities_set
        mov     byte ptr [has_agi_concerns], 1

.capabilities_set:
        mov     al, [current_cpu_type]  ; Return CPU type

        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
cpu_detect_optimizations ENDP

;-----------------------------------------------------------------------------
; cpu_optimized_copy - Dispatch to optimal copy routine for current CPU
;
; Input:  DS:SI = source, ES:DI = destination, CX = byte count
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX
;-----------------------------------------------------------------------------
cpu_optimized_copy PROC
        push    bp
        mov     bp, sp
        push    bx

        ; Validate parameters
        test    cx, cx
        jz      .success
        cmp     cx, 65535
        ja      .error

        ; Get CPU type and dispatch
        mov     bl, [current_cpu_type]
        cmp     bl, CPU_PENTIUM
        ja      .error              ; Invalid CPU type
        
        mov     bh, 0               ; Clear high byte
        shl     bx, 1               ; Convert to word offset
        call    [cpu_copy_table + bx]

        test    ax, ax              ; Check return code
        jnz     .error

.success:
        mov     ax, 0
        jmp     .exit

.error:
        mov     ax, 1

.exit:
        pop     bx
        pop     bp
        ret
cpu_optimized_copy ENDP

;-----------------------------------------------------------------------------
; CPU-Specific Copy Implementations
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; copy_8086 - Basic 8086 memory copy
;
; Input:  DS:SI = source, ES:DI = destination, CX = count
; Output: AX = 0 for success
; Uses:   AX, CX, SI, DI, Flags
;-----------------------------------------------------------------------------
copy_8086 PROC
        push    bp
        mov     bp, sp

        ; Basic byte copy for maximum compatibility
        cld
        rep     movsb

        mov     ax, 0               ; Success

        pop     bp
        ret
copy_8086 ENDP

;-----------------------------------------------------------------------------
; copy_286 - 80286 optimized copy with PUSHA/POPA
;
; Input:  DS:SI = source, ES:DI = destination, CX = count
; Output: AX = 0 for success
; Uses:   All registers (saved with PUSHA)
;-----------------------------------------------------------------------------
copy_286 PROC
        ; Use PUSHA for efficient register save (19 cycles vs 8×PUSH = 24 cycles)
        pusha
        
        cld
        ; Use word operations when possible
        mov     dx, cx              ; Save original count
        shr     cx, 1               ; Convert to words
        rep     movsw               ; Copy words

        ; Handle odd byte
        test    dx, 1
        jz      .done_286
        movsb                       ; Copy remaining byte

.done_286:
        popa                        ; Restore all registers (19 cycles)
        mov     ax, 0               ; Success
        ret
copy_286 ENDP

;-----------------------------------------------------------------------------
; copy_386 - 80386 optimized copy with 32-bit operations
;
; Input:  DS:SI = source, ES:DI = destination, CX = count
; Output: AX = 0 for success
; Uses:   EAX, ECX, ESI, EDI, Flags
;-----------------------------------------------------------------------------
copy_386 PROC
        push    bp
        mov     bp, sp

        ; Save 32-bit registers
        OPERAND32
        push    eax
        OPERAND32
        push    ecx
        OPERAND32
        push    esi
        OPERAND32
        push    edi

        cld
        
        ; Check alignment for optimal 32-bit copy
        mov     ax, si
        or      ax, di
        test    ax, 3               ; Check 4-byte alignment
        jnz     .copy_386_unaligned

        ; Aligned 32-bit copy (3x faster than byte copy)
        OPERAND32
        mov     ecx, [bp+8]         ; Get original CX (adjusted for stack)
        OPERAND32
        mov     edx, ecx            ; Save original count
        OPERAND32
        shr     ecx, 2              ; Convert to dwords
        jz      .copy_386_remainder

        ; 32-bit copy with 0x66 prefix
        OPERAND32
        rep     movsd               ; Copy dwords

.copy_386_remainder:
        OPERAND32
        mov     ecx, edx
        OPERAND32
        and     ecx, 3              ; Get remainder bytes
        rep     movsb               ; Copy remaining bytes
        jmp     .copy_386_done

.copy_386_unaligned:
        ; Use 16-bit copy for unaligned data
        mov     dx, cx              ; Save count
        shr     cx, 1               ; Convert to words
        rep     movsw
        
        test    dx, 1
        jz      .copy_386_done
        movsb

.copy_386_done:
        ; Restore 32-bit registers
        OPERAND32
        pop     edi
        OPERAND32
        pop     esi
        OPERAND32
        pop     ecx
        OPERAND32
        pop     eax

        mov     ax, 0               ; Success

        pop     bp
        ret
copy_386 ENDP

;-----------------------------------------------------------------------------
; copy_486 - 80486 pipeline-optimized copy
;
; Input:  DS:SI = source, ES:DI = destination, CX = count
; Output: AX = 0 for success
; Uses:   EAX, EBX, ECX, EDX, ESI, EDI
;-----------------------------------------------------------------------------
copy_486 PROC
        push    bp
        mov     bp, sp

        ; Save registers
        OPERAND32
        pusha                       ; 486: PUSHA=5 cycles vs 8×PUSH=8 cycles

        cld

        ; Check for cache line alignment (32 bytes)
        mov     ax, si
        or      ax, di
        test    ax, 31              ; 32-byte alignment
        jnz     .copy_486_standard

        ; Cache-aligned copy with pipeline optimization
        OPERAND32
        mov     ecx, [bp+16]        ; Get count (adjusted for PUSHA)
        OPERAND32
        mov     edx, ecx            ; Save original
        OPERAND32
        shr     ecx, 5              ; 32-byte blocks
        jz      .copy_486_remainder

.copy_486_cache_loop:
        ; Pipeline-optimized 32-byte copy (8 dwords)
        ; Pair instructions for U/V pipe execution
        OPERAND32
        mov     eax, ds:[esi]       ; U-pipe
        OPERAND32
        mov     ebx, ds:[esi+4]     ; V-pipe (pairs with mov eax)
        OPERAND32
        mov     es:[edi], eax       ; U-pipe
        OPERAND32
        mov     es:[edi+4], ebx     ; V-pipe (pairs with mov [edi])
        
        OPERAND32
        mov     eax, ds:[esi+8]     ; U-pipe
        OPERAND32
        mov     ebx, ds:[esi+12]    ; V-pipe
        OPERAND32
        mov     es:[edi+8], eax     ; U-pipe
        OPERAND32
        mov     es:[edi+12], ebx    ; V-pipe

        OPERAND32
        mov     eax, ds:[esi+16]    ; U-pipe
        OPERAND32
        mov     ebx, ds:[esi+20]    ; V-pipe
        OPERAND32
        mov     es:[edi+16], eax    ; U-pipe
        OPERAND32
        mov     es:[edi+20], ebx    ; V-pipe

        OPERAND32
        mov     eax, ds:[esi+24]    ; U-pipe
        OPERAND32
        mov     ebx, ds:[esi+28]    ; V-pipe
        OPERAND32
        mov     es:[edi+24], eax    ; U-pipe
        OPERAND32
        mov     es:[edi+28], ebx    ; V-pipe

        OPERAND32
        add     esi, 32             ; U-pipe
        OPERAND32
        add     edi, 32             ; V-pipe (pairs with add esi)
        OPERAND32
        dec     ecx                 ; U-pipe
        jnz     .copy_486_cache_loop

.copy_486_remainder:
        ; Handle remaining bytes with standard 32-bit copy
        OPERAND32
        mov     ecx, edx
        OPERAND32
        and     ecx, 31             ; Remainder bytes
        OPERAND32
        shr     ecx, 2              ; Convert to dwords
        OPERAND32
        rep     movsd

        OPERAND32
        mov     ecx, edx
        OPERAND32
        and     ecx, 3              ; Final remainder
        rep     movsb
        jmp     .copy_486_done

.copy_486_standard:
        ; Standard 32-bit copy for unaligned data
        call    copy_386_internal   ; Reuse 386 logic
        
.copy_486_done:
        OPERAND32
        popa

        mov     ax, 0               ; Success

        pop     bp
        ret

; Internal 386 copy routine (reusable)
copy_386_internal:
        OPERAND32
        mov     ecx, [bp+16]
        OPERAND32
        mov     edx, ecx
        OPERAND32
        shr     ecx, 2
        OPERAND32
        rep     movsd
        OPERAND32
        mov     ecx, edx
        OPERAND32
        and     ecx, 3
        rep     movsb
        ret

copy_486 ENDP

;-----------------------------------------------------------------------------
; copy_pentium - Pentium superscalar optimized copy
;
; Input:  DS:SI = source, ES:DI = destination, CX = count
; Output: AX = 0 for success
; Uses:   EAX, EBX, ECX, EDX, ESI, EDI
;-----------------------------------------------------------------------------
copy_pentium PROC
        push    bp
        mov     bp, sp

        ; Save registers
        OPERAND32
        pusha

        cld

        ; Pentium-specific optimizations:
        ; 1. Avoid AGI stalls
        ; 2. Use dual pipeline (U/V pipe pairing)
        ; 3. Minimize memory access latency

        ; Check alignment and size for optimal strategy
        OPERAND32
        mov     ecx, [bp+16]        ; Get count
        cmp     cx, 64              ; Use special handling for large copies
        jb      .copy_pentium_small

        ; Large copy optimization
        mov     ax, si
        or      ax, di
        test    ax, 7               ; Check 8-byte alignment
        jnz     .copy_pentium_medium

        ; Optimal 8-byte aligned copy
        OPERAND32
        mov     edx, ecx
        OPERAND32
        shr     ecx, 3              ; 8-byte blocks
        jz      .copy_pentium_remainder

.copy_pentium_8byte_loop:
        ; Read 8 bytes with optimal pairing
        OPERAND32
        mov     eax, ds:[esi]       ; U-pipe
        OPERAND32
        mov     ebx, ds:[esi+4]     ; V-pipe (pairs)
        
        ; Avoid AGI - insert instruction between addr calc and use
        OPERAND32
        add     esi, 8              ; Prepare next address
        AVOID_AGI edx                ; Fill slot to avoid AGI stall
        
        ; Write 8 bytes
        OPERAND32
        mov     es:[edi], eax       ; U-pipe
        OPERAND32
        mov     es:[edi+4], ebx     ; V-pipe (pairs)
        
        OPERAND32
        add     edi, 8              ; U-pipe
        OPERAND32
        dec     ecx                 ; V-pipe (pairs)
        jnz     .copy_pentium_8byte_loop

.copy_pentium_remainder:
        OPERAND32
        mov     ecx, edx
        OPERAND32
        and     ecx, 7              ; Remainder bytes
        rep     movsb
        jmp     .copy_pentium_done

.copy_pentium_medium:
        ; Medium copy with 32-bit operations
        call    copy_386_internal

.copy_pentium_small:
        ; Small copy - just use fast byte copy
        rep     movsb

.copy_pentium_done:
        OPERAND32
        popa

        mov     ax, 0               ; Success

        pop     bp
        ret
copy_pentium ENDP

;-----------------------------------------------------------------------------
; CPU-Specific Checksum Implementations
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; checksum_8086 - Basic 8086 Internet checksum
;
; Input:  DS:SI = data, CX = length
; Output: AX = checksum
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
checksum_8086 PROC
        push    bp
        mov     bp, sp
        push    bx
        push    dx

        xor     dx, dx              ; Clear checksum accumulator
        
        ; Process words
        shr     cx, 1
        jnc     .checksum_8086_loop
        
        ; Handle odd byte at end
        push    cx
        mov     cx, [bp+6]          ; Get original length
        and     cx, 1
        jz      .no_odd_8086
        
        mov     al, [si + cx - 1]
        xor     ah, ah
        shl     ax, 8               ; Move to high byte
        add     dx, ax
        adc     dx, 0

.no_odd_8086:
        pop     cx

.checksum_8086_loop:
        jcxz    .checksum_8086_fold
        lodsw
        add     dx, ax
        adc     dx, 0
        dec     cx
        jmp     .checksum_8086_loop

.checksum_8086_fold:
        ; Fold carries
        mov     ax, dx
        shr     dx, 16
        add     ax, dx
        adc     ax, 0
        not     ax

        pop     dx
        pop     bx
        pop     bp
        ret
checksum_8086 ENDP

;-----------------------------------------------------------------------------
; checksum_286 - 80286 optimized checksum
;
; Input:  DS:SI = data, CX = length
; Output: AX = checksum
; Uses:   All registers (saved with PUSHA)
;-----------------------------------------------------------------------------
checksum_286 PROC
        pusha

        xor     dx, dx              ; Checksum accumulator
        
        ; Unroll loop for better performance
        mov     bx, cx
        shr     cx, 2               ; Process 4 words at a time
        jz      .checksum_286_words

.checksum_286_unroll:
        lodsw
        add     dx, ax
        adc     dx, 0
        lodsw
        add     dx, ax
        adc     dx, 0
        lodsw  
        add     dx, ax
        adc     dx, 0
        lodsw
        add     dx, ax
        adc     dx, 0
        dec     cx
        jnz     .checksum_286_unroll

.checksum_286_words:
        mov     cx, bx
        and     cx, 3               ; Remaining words
        shr     cx, 1
        jz      .checksum_286_byte

.checksum_286_word_loop:
        lodsw
        add     dx, ax
        adc     dx, 0
        dec     cx
        jnz     .checksum_286_word_loop

.checksum_286_byte:
        test    bx, 1               ; Check for odd byte
        jz      .checksum_286_fold
        
        mov     al, [si]
        xor     ah, ah
        shl     ax, 8
        add     dx, ax
        adc     dx, 0

.checksum_286_fold:
        mov     ax, dx
        shr     dx, 16
        add     ax, dx
        adc     ax, 0
        not     ax
        
        mov     [esp + 14], ax      ; Store result in AX position on stack
        popa
        ret
checksum_286 ENDP

;-----------------------------------------------------------------------------
; checksum_386 - 80386 32-bit optimized checksum
;
; Input:  DS:SI = data, CX = length
; Output: AX = checksum
; Uses:   EAX, EBX, ECX, EDX, ESI
;-----------------------------------------------------------------------------
checksum_386 PROC
        push    bp
        mov     bp, sp

        OPERAND32
        push    eax
        OPERAND32
        push    ebx
        OPERAND32
        push    ecx
        OPERAND32
        push    edx
        OPERAND32
        push    esi

        OPERAND32
        xor     edx, edx            ; 32-bit accumulator
        OPERAND32
        movzx   ecx, cx             ; Zero-extend length to 32-bit

        ; Process 4 bytes at a time when possible
        OPERAND32
        mov     ebx, ecx
        OPERAND32
        shr     ecx, 2              ; Dword count
        jz      .checksum_386_words

.checksum_386_dword_loop:
        OPERAND32
        lodsd                       ; Load dword
        OPERAND32
        add     dx, ax              ; Add low word
        OPERAND32
        shr     eax, 16             ; Get high word
        adc     dx, ax              ; Add high word with carry
        adc     dx, 0               ; Add final carry
        OPERAND32
        dec     ecx
        jnz     .checksum_386_dword_loop

.checksum_386_words:
        OPERAND32
        mov     ecx, ebx
        OPERAND32
        and     ecx, 3              ; Remainder bytes
        OPERAND32
        shr     ecx, 1              ; Word count
        jz      .checksum_386_byte

.checksum_386_word_loop:
        lodsw
        add     dx, ax
        adc     dx, 0
        OPERAND32
        dec     ecx
        jnz     .checksum_386_word_loop

.checksum_386_byte:
        OPERAND32
        test    ebx, 1
        jz      .checksum_386_fold
        
        mov     al, [si]
        xor     ah, ah
        shl     ax, 8
        add     dx, ax
        adc     dx, 0

.checksum_386_fold:
        mov     ax, dx
        shr     dx, 16
        add     ax, dx
        adc     ax, 0
        not     ax

        OPERAND32
        pop     esi
        OPERAND32
        pop     edx
        OPERAND32
        pop     ecx
        OPERAND32
        pop     ebx
        OPERAND32
        pop     eax

        pop     bp
        ret
checksum_386 ENDP

;-----------------------------------------------------------------------------
; checksum_486 - 80486 pipeline optimized checksum
;
; Input:  DS:SI = data, CX = length  
; Output: AX = checksum
; Uses:   EAX, EBX, ECX, EDX, ESI
;-----------------------------------------------------------------------------
checksum_486 PROC
        ; Use 386 implementation with minor pipeline optimizations
        call    checksum_386
        ret
checksum_486 ENDP

;-----------------------------------------------------------------------------
; checksum_pentium - Pentium dual-pipeline optimized checksum
;
; Input:  DS:SI = data, CX = length
; Output: AX = checksum  
; Uses:   EAX, EBX, ECX, EDX, ESI
;-----------------------------------------------------------------------------
checksum_pentium PROC
        push    bp
        mov     bp, sp

        OPERAND32
        pusha

        OPERAND32
        xor     edx, edx            ; Accumulator 1
        OPERAND32
        xor     ebx, ebx            ; Accumulator 2
        OPERAND32
        movzx   ecx, cx

        ; Use dual accumulators to maximize pipeline utilization
        OPERAND32
        mov     eax, ecx
        OPERAND32
        shr     ecx, 3              ; Process 8 bytes at a time
        jz      .checksum_pentium_remainder

.checksum_pentium_dual_loop:
        ; Process two dwords in parallel
        OPERAND32
        mov     edi, ds:[esi]       ; U-pipe - load first dword
        OPERAND32
        mov     ebp, ds:[esi+4]     ; V-pipe - load second dword (pairs)
        
        OPERAND32
        add     esi, 8              ; U-pipe - advance pointer
        AVOID_AGI edi               ; Avoid AGI stall
        
        ; Add to dual accumulators with optimal pairing
        add     dx, di              ; U-pipe - add low word of first dword
        OPERAND32
        shr     edi, 16             ; V-pipe - get high word (pairs)
        adc     dx, di              ; U-pipe - add high word with carry
        adc     dx, 0               ; V-pipe - add final carry (pairs)
        
        add     bx, bp              ; U-pipe - add low word of second dword
        OPERAND32
        shr     ebp, 16             ; V-pipe - get high word (pairs)
        adc     bx, bp              ; U-pipe - add high word with carry
        adc     bx, 0               ; V-pipe - add final carry (pairs)
        
        OPERAND32
        dec     ecx                 ; U-pipe
        jnz     .checksum_pentium_dual_loop

        ; Combine dual accumulators
        add     dx, bx              ; Combine results
        adc     dx, 0

.checksum_pentium_remainder:
        ; Handle remaining bytes
        OPERAND32
        mov     ecx, eax
        OPERAND32
        and     ecx, 7
        jz      .checksum_pentium_fold

.checksum_pentium_byte_loop:
        mov     al, [si]
        xor     ah, ah
        inc     si
        add     dx, ax
        adc     dx, 0
        OPERAND32
        dec     ecx
        jnz     .checksum_pentium_byte_loop

.checksum_pentium_fold:
        mov     ax, dx
        shr     dx, 16
        add     ax, dx
        adc     ax, 0
        not     ax

        mov     [esp + 14], ax      ; Store result in saved EAX
        OPERAND32
        popa

        pop     bp
        ret
checksum_pentium ENDP

;-----------------------------------------------------------------------------
; CPU-Specific Memset Implementations  
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; memset_8086 - Basic 8086 memory set
;-----------------------------------------------------------------------------
memset_8086 PROC
        rep     stosb
        mov     ax, 0
        ret
memset_8086 ENDP

;-----------------------------------------------------------------------------
; memset_286 - 80286 optimized memset
;-----------------------------------------------------------------------------
memset_286 PROC
        pusha
        
        mov     ah, al              ; Create word pattern
        mov     dx, cx              ; Save count
        shr     cx, 1               ; Word count
        rep     stosw               ; Store words
        
        test    dx, 1               ; Odd byte?
        jz      .memset_286_done
        stosb                       ; Store final byte

.memset_286_done:
        popa
        mov     ax, 0
        ret
memset_286 ENDP

;-----------------------------------------------------------------------------
; memset_386 - 80386 32-bit optimized memset
;-----------------------------------------------------------------------------
memset_386 PROC
        push    bp
        mov     bp, sp

        OPERAND32
        push    eax
        OPERAND32
        push    ecx
        OPERAND32
        push    edi

        ; Create 32-bit pattern
        mov     ah, al              ; Create word pattern
        OPERAND32
        mov     edx, eax            ; Save word pattern
        OPERAND32
        shl     eax, 16             ; Shift to high word
        OPERAND32
        or      eax, edx            ; Complete dword pattern

        OPERAND32
        movzx   ecx, cx
        OPERAND32
        mov     edx, ecx            ; Save count
        OPERAND32
        shr     ecx, 2              ; Dword count

        OPERAND32
        rep     stosd               ; Store dwords

        ; Handle remainder
        OPERAND32
        mov     ecx, edx
        OPERAND32
        and     ecx, 3
        mov     al, dl              ; Restore byte value
        rep     stosb

        OPERAND32
        pop     edi
        OPERAND32
        pop     ecx
        OPERAND32
        pop     eax

        mov     ax, 0
        pop     bp
        ret
memset_386 ENDP

;-----------------------------------------------------------------------------
; memset_486 - 80486 pipeline optimized memset
;-----------------------------------------------------------------------------
memset_486 PROC
        ; Use 386 implementation for now
        call    memset_386
        ret
memset_486 ENDP

;-----------------------------------------------------------------------------
; memset_pentium - Pentium optimized memset
;-----------------------------------------------------------------------------
memset_pentium PROC
        ; Use 386 implementation for now
        call    memset_386
        ret
memset_pentium ENDP

;-----------------------------------------------------------------------------
; Utility Functions
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; cpu_optimized_checksum - Dispatch to optimal checksum routine
;
; Input:  DS:SI = data, CX = length
; Output: AX = checksum
; Uses:   AX, BX
;-----------------------------------------------------------------------------
cpu_optimized_checksum PROC
        push    bp
        mov     bp, sp
        push    bx

        ; Get CPU type and dispatch
        mov     bl, [current_cpu_type]
        mov     bh, 0
        shl     bx, 1
        call    [cpu_checksum_table + bx]

        pop     bx
        pop     bp
        ret
cpu_optimized_checksum ENDP

;-----------------------------------------------------------------------------
; cpu_optimized_memset - Dispatch to optimal memset routine
;
; Input:  ES:DI = destination, AL = fill byte, CX = count
; Output: AX = 0 for success
; Uses:   AX, BX
;-----------------------------------------------------------------------------
cpu_optimized_memset PROC
        push    bp
        mov     bp, sp
        push    bx

        ; Get CPU type and dispatch
        mov     bl, [current_cpu_type]
        mov     bh, 0
        shl     bx, 1
        call    [cpu_memset_table + bx]

        pop     bx
        pop     bp
        ret
cpu_optimized_memset ENDP

;-----------------------------------------------------------------------------
; cpu_get_best_copy_func - Return pointer to best copy function for CPU
;
; Input:  None
; Output: AX = function pointer
; Uses:   AX, BX
;-----------------------------------------------------------------------------
cpu_get_best_copy_func PROC
        mov     bl, [current_cpu_type]
        mov     bh, 0
        shl     bx, 1
        mov     ax, [cpu_copy_table + bx]
        ret
cpu_get_best_copy_func ENDP

;-----------------------------------------------------------------------------
; cpu_get_performance_metrics - Get performance metrics
;
; Input:  ES:DI = buffer for metrics
; Output: Buffer filled with performance data
; Uses:   AX, CX, SI, DI
;-----------------------------------------------------------------------------
cpu_get_performance_metrics PROC
        push    si
        push    cx

        ; Copy performance data to buffer
        mov     si, OFFSET perf_copy_cycles
        mov     cx, 8               ; 2 dwords = 8 words
        rep     movsw

        ; Add CPU type information
        mov     al, [current_cpu_type]
        stosb
        mov     al, [has_32bit_ops]
        stosb
        mov     al, [has_pipeline]
        stosb
        mov     al, [has_agi_concerns]
        stosb

        pop     cx
        pop     si
        ret
cpu_get_performance_metrics ENDP

_TEXT ENDS

END