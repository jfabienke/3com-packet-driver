; @file cpu_patches.asm
; @brief CPU-specific optimization patches
;
; 3Com Packet Driver - CPU Optimization Patch Library
;
; Provides optimized instruction sequences for different CPU types
; targeting 25-30% performance improvement on critical operations.
;
; Agent 04 - Performance Engineer - Week 1 Day 4-5 Critical Deliverable

.MODEL SMALL
.386

; Include shared definitions
INCLUDE ..\..\include\asm_interfaces.inc

; Patch template constants
MAX_PATCH_SIZE          EQU 32      ; Maximum patch size in bytes
PATCH_ALIGNMENT         EQU 1       ; Minimum alignment for patches

; CPU-specific feature flags (from cpu_detect.h)
FEATURE_PUSHA           EQU 0001h   ; PUSHA/POPA instructions (286+)
FEATURE_32BIT           EQU 0002h   ; 32-bit operations (386+)
FEATURE_CPUID           EQU 0004h   ; CPUID instruction (486+)

; Patch template IDs
PATCH_ID_REP_MOVSW      EQU 1       ; REP MOVSW optimization
PATCH_ID_REP_MOVSD      EQU 2       ; REP MOVSD optimization  
PATCH_ID_PUSHA_POPA     EQU 3       ; PUSHA/POPA optimization
PATCH_ID_UNROLLED_COPY  EQU 4       ; Unrolled copy loop
PATCH_ID_STRING_IO      EQU 5       ; String I/O optimization
PATCH_ID_BURST_IO       EQU 6       ; Burst I/O optimization

; Data segment
_DATA SEGMENT
        ASSUME  DS:_DATA

; Patch template storage
patch_template_rep_movsw    db MAX_PATCH_SIZE dup(0)
patch_template_rep_movsd    db MAX_PATCH_SIZE dup(0)
patch_template_pusha_popa   db MAX_PATCH_SIZE dup(0)
patch_template_unrolled     db MAX_PATCH_SIZE dup(0)
patch_template_string_io    db MAX_PATCH_SIZE dup(0)
patch_template_burst_io     db MAX_PATCH_SIZE dup(0)

; Patch template sizes
patch_template_sizes        db 0, 0, 0, 0, 0, 0, 0    ; Indexed by patch ID

; Current CPU type and features
current_cpu_type            db 0                        ; Detected CPU type
current_cpu_features        dd 0                        ; Detected CPU features

_DATA ENDS

; Code segment
_TEXT SEGMENT
        ASSUME  CS:_TEXT, DS:_DATA

; Public function exports
PUBLIC generate_rep_movsw_patch
PUBLIC generate_rep_movsd_patch
PUBLIC generate_pusha_popa_patch
PUBLIC generate_unrolled_copy_patch
PUBLIC generate_string_io_patch
PUBLIC generate_burst_io_patch
PUBLIC get_patch_template
PUBLIC get_patch_template_size
PUBLIC initialize_patch_templates
PUBLIC asm_atomic_patch_bytes
PUBLIC asm_flush_prefetch_near_jump

; External references
EXTRN get_cpu_type:PROC
EXTRN get_cpu_features:PROC

;-----------------------------------------------------------------------------
; initialize_patch_templates - Initialize all patch templates
;
; Input:  None
; Output: AX = 0 for success, non-zero for error
; Uses:   All registers
;-----------------------------------------------------------------------------
initialize_patch_templates PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Get current CPU type and features
        call    get_cpu_type
        mov     [current_cpu_type], al
        call    get_cpu_features
        mov     dword ptr [current_cpu_features], eax

        ; Generate all patch templates
        call    generate_rep_movsw_patch
        call    generate_rep_movsd_patch
        call    generate_pusha_popa_patch
        call    generate_unrolled_copy_patch
        call    generate_string_io_patch
        call    generate_burst_io_patch

        ; Success
        mov     ax, 0

        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
initialize_patch_templates ENDP

;-----------------------------------------------------------------------------
; generate_rep_movsw_patch - Generate REP MOVSW optimization patch (286+)
;
; Input:  None
; Output: Patch stored in patch_template_rep_movsw
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
generate_rep_movsw_patch PROC
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    cx

        ; Check if CPU supports 286+ features
        cmp     byte ptr [current_cpu_type], 1  ; CPU_TYPE_80286
        jb      .not_supported

        ; Generate REP MOVSW patch sequence
        mov     di, OFFSET patch_template_rep_movsw
        mov     cx, 0                   ; Patch size counter

        ; Clear direction flag
        mov     al, 0fch                ; CLD instruction
        stosb
        inc     cx

        ; Set up for word operations (assumes CX contains word count)
        ; shr cx, 1                     ; Convert byte count to word count
        mov     al, 0c1h                ; SHR CX, 1 (opcode)
        stosb
        mov     al, 0e9h                ; ModR/M byte: SHR CX, 1
        stosb  
        mov     al, 01h                 ; Immediate: shift by 1
        stosb
        add     cx, 3

        ; REP MOVSW instruction
        mov     al, 0f3h                ; REP prefix
        stosb
        mov     al, 0a5h                ; MOVSW instruction
        stosb
        add     cx, 2

        ; Handle odd byte if original count was odd
        ; jnc skip_odd_byte              ; If no carry from SHR, count was even
        mov     al, 73h                 ; JNC instruction
        stosb
        mov     al, 01h                 ; Skip 1 byte
        stosb
        add     cx, 2

        ; movsb                          ; Copy remaining odd byte
        mov     al, 0a4h                ; MOVSB instruction
        stosb
        inc     cx

        ; skip_odd_byte:
        ; NOP for alignment (target of JNC)
        mov     al, 90h                 ; NOP instruction
        stosb
        inc     cx

        ; Store patch size
        mov     [patch_template_sizes + PATCH_ID_REP_MOVSW], cl
        jmp     .done

.not_supported:
        ; CPU doesn't support this optimization
        mov     byte ptr [patch_template_sizes + PATCH_ID_REP_MOVSW], 0

.done:
        pop     cx
        pop     di
        pop     si
        pop     bp
        ret
generate_rep_movsw_patch ENDP

;-----------------------------------------------------------------------------
; generate_rep_movsd_patch - Generate REP MOVSD optimization patch (386+)
;
; Input:  None
; Output: Patch stored in patch_template_rep_movsd
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
generate_rep_movsd_patch PROC
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    cx

        ; Check if CPU supports 386+ features
        test    dword ptr [current_cpu_features], FEATURE_32BIT
        jz      .not_supported

        ; Generate REP MOVSD patch sequence
        mov     di, OFFSET patch_template_rep_movsd
        mov     cx, 0                   ; Patch size counter

        ; Clear direction flag
        mov     al, 0fch                ; CLD instruction
        stosb
        inc     cx

        ; Set up for dword operations (assumes CX contains byte count)
        ; shr cx, 2                     ; Convert byte count to dword count
        mov     al, 0c1h                ; SHR CX, immediate
        stosb
        mov     al, 0e9h                ; ModR/M byte: SHR CX
        stosb
        mov     al, 02h                 ; Immediate: shift by 2
        stosb
        add     cx, 3

        ; REP MOVSD instruction (386+ only)
        mov     al, 0f3h                ; REP prefix
        stosb
        mov     al, 0a5h                ; MOVSD instruction (32-bit operand size)
        stosb
        add     cx, 2

        ; Handle remaining bytes (0-3 bytes)
        ; mov ax, original_count         ; Get original byte count
        ; and ax, 3                     ; Keep only remainder bits
        ; jz skip_remainder             ; If zero, no remainder
        
        ; For simplicity, use a small loop for remainder
        ; test al, 2                    ; Check bit 1 (2 bytes remaining?)
        mov     al, 0a8h                ; TEST AL, immediate
        stosb
        mov     al, 02h                 ; Test bit 1
        stosb
        add     cx, 2

        ; jz skip_word                  ; No word to copy
        mov     al, 74h                 ; JZ instruction
        stosb
        mov     al, 01h                 ; Skip 1 byte (MOVSW)
        stosb
        add     cx, 2

        ; movsw                         ; Copy remaining word
        mov     al, 0a5h                ; MOVSW instruction (16-bit)
        stosb
        inc     cx

        ; skip_word:
        ; test al, 1                    ; Check bit 0 (1 byte remaining?)
        mov     al, 0a8h                ; TEST AL, immediate
        stosb
        mov     al, 01h                 ; Test bit 0
        stosb
        add     cx, 2

        ; jz skip_byte                  ; No byte to copy
        mov     al, 74h                 ; JZ instruction
        stosb
        mov     al, 01h                 ; Skip 1 byte (MOVSB)
        stosb
        add     cx, 2

        ; movsb                         ; Copy remaining byte
        mov     al, 0a4h                ; MOVSB instruction
        stosb
        inc     cx

        ; skip_byte:
        ; NOP for alignment
        mov     al, 90h                 ; NOP instruction
        stosb
        inc     cx

        ; Store patch size
        mov     [patch_template_sizes + PATCH_ID_REP_MOVSD], cl
        jmp     .done

.not_supported:
        ; CPU doesn't support this optimization
        mov     byte ptr [patch_template_sizes + PATCH_ID_REP_MOVSD], 0

.done:
        pop     cx
        pop     di
        pop     si
        pop     bp
        ret
generate_rep_movsd_patch ENDP

;-----------------------------------------------------------------------------
; generate_pusha_popa_patch - Generate PUSHA/POPA optimization patch (286+)
;
; Input:  None
; Output: Patch stored in patch_template_pusha_popa
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
generate_pusha_popa_patch PROC
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    cx

        ; Check if CPU supports PUSHA/POPA
        test    dword ptr [current_cpu_features], FEATURE_PUSHA
        jz      .not_supported

        ; Generate PUSHA/POPA patch sequence
        mov     di, OFFSET patch_template_pusha_popa
        mov     cx, 0                   ; Patch size counter

        ; PUSHA instruction (saves AX, CX, DX, BX, SP, BP, SI, DI)
        mov     al, 60h                 ; PUSHA instruction
        stosb
        inc     cx

        ; Function body would go here (NOPs for template)
        mov     al, 90h                 ; NOP
        stosb
        mov     al, 90h                 ; NOP
        stosb
        mov     al, 90h                 ; NOP
        stosb
        add     cx, 3

        ; POPA instruction (restores DI, SI, BP, SP, BX, DX, CX, AX)
        mov     al, 61h                 ; POPA instruction
        stosb
        inc     cx

        ; Store patch size
        mov     [patch_template_sizes + PATCH_ID_PUSHA_POPA], cl
        jmp     .done

.not_supported:
        ; CPU doesn't support this optimization
        mov     byte ptr [patch_template_sizes + PATCH_ID_PUSHA_POPA], 0

.done:
        pop     cx
        pop     di
        pop     si
        pop     bp
        ret
generate_pusha_popa_patch ENDP

;-----------------------------------------------------------------------------
; generate_unrolled_copy_patch - Generate unrolled copy loop patch
;
; Input:  None
; Output: Patch stored in patch_template_unrolled
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
generate_unrolled_copy_patch PROC
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    cx

        ; Generate unrolled copy for small, fixed sizes (e.g., 16 bytes)
        mov     di, OFFSET patch_template_unrolled
        mov     cx, 0                   ; Patch size counter

        ; Clear direction flag
        mov     al, 0fch                ; CLD instruction
        stosb
        inc     cx

        ; Unroll copy for 16 bytes (8 word moves)
        ; movsw (repeated 8 times)
        mov     bl, 8                   ; Loop count
.unroll_loop:
        mov     al, 0a5h                ; MOVSW instruction
        stosb
        inc     cx
        dec     bl
        jnz     .unroll_loop

        ; Store patch size
        mov     [patch_template_sizes + PATCH_ID_UNROLLED_COPY], cl

        pop     cx
        pop     di
        pop     si
        pop     bp
        ret
generate_unrolled_copy_patch ENDP

;-----------------------------------------------------------------------------
; generate_string_io_patch - Generate string I/O optimization patch
;
; Input:  None
; Output: Patch stored in patch_template_string_io
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
generate_string_io_patch PROC
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    cx

        ; Generate string I/O patch (REP INSW/OUTSW)
        mov     di, OFFSET patch_template_string_io
        mov     cx, 0                   ; Patch size counter

        ; Clear direction flag
        mov     al, 0fch                ; CLD instruction
        stosb
        inc     cx

        ; REP INSW instruction (assumes DX = port, CX = count, ES:DI = buffer)
        mov     al, 0f3h                ; REP prefix
        stosb
        mov     al, 6dh                 ; INSW instruction
        stosb
        add     cx, 2

        ; Store patch size
        mov     [patch_template_sizes + PATCH_ID_STRING_IO], cl

        pop     cx
        pop     di
        pop     si
        pop     bp
        ret
generate_string_io_patch ENDP

;-----------------------------------------------------------------------------
; generate_burst_io_patch - Generate burst I/O optimization patch
;
; Input:  None
; Output: Patch stored in patch_template_burst_io
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
generate_burst_io_patch PROC
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    cx

        ; Generate burst I/O patch (unrolled IN/OUT instructions)
        mov     di, OFFSET patch_template_burst_io
        mov     cx, 0                   ; Patch size counter

        ; Unroll IN instructions for burst read (4 reads)
        ; in ax, dx (repeated 4 times)
        mov     bl, 4                   ; Loop count
.burst_loop:
        mov     al, 0edh                ; IN AX, DX instruction
        stosb
        inc     cx
        ; Store to memory would go here - using NOP for template
        mov     al, 90h                 ; NOP placeholder
        stosb
        inc     cx
        dec     bl
        jnz     .burst_loop

        ; Store patch size
        mov     [patch_template_sizes + PATCH_ID_BURST_IO], cl

        pop     cx
        pop     di
        pop     si
        pop     bp
        ret
generate_burst_io_patch ENDP

;-----------------------------------------------------------------------------
; get_patch_template - Get pointer to patch template
;
; Input:  AL = patch template ID
; Output: BX = pointer to template, CX = template size
; Uses:   BX, CX
;-----------------------------------------------------------------------------
get_patch_template PROC
        push    bp
        mov     bp, sp
        push    ax

        ; Validate patch ID
        cmp     al, PATCH_ID_BURST_IO
        ja      .invalid_id

        ; Get template pointer based on ID
        cmp     al, PATCH_ID_REP_MOVSW
        je      .get_movsw
        cmp     al, PATCH_ID_REP_MOVSD
        je      .get_movsd
        cmp     al, PATCH_ID_PUSHA_POPA
        je      .get_pusha
        cmp     al, PATCH_ID_UNROLLED_COPY
        je      .get_unrolled
        cmp     al, PATCH_ID_STRING_IO
        je      .get_string_io
        cmp     al, PATCH_ID_BURST_IO
        je      .get_burst_io
        jmp     .invalid_id

.get_movsw:
        mov     bx, OFFSET patch_template_rep_movsw
        mov     cl, [patch_template_sizes + PATCH_ID_REP_MOVSW]
        jmp     .done

.get_movsd:
        mov     bx, OFFSET patch_template_rep_movsd
        mov     cl, [patch_template_sizes + PATCH_ID_REP_MOVSD]
        jmp     .done

.get_pusha:
        mov     bx, OFFSET patch_template_pusha_popa
        mov     cl, [patch_template_sizes + PATCH_ID_PUSHA_POPA]
        jmp     .done

.get_unrolled:
        mov     bx, OFFSET patch_template_unrolled
        mov     cl, [patch_template_sizes + PATCH_ID_UNROLLED_COPY]
        jmp     .done

.get_string_io:
        mov     bx, OFFSET patch_template_string_io
        mov     cl, [patch_template_sizes + PATCH_ID_STRING_IO]
        jmp     .done

.get_burst_io:
        mov     bx, OFFSET patch_template_burst_io
        mov     cl, [patch_template_sizes + PATCH_ID_BURST_IO]
        jmp     .done

.invalid_id:
        mov     bx, 0                   ; Null pointer
        mov     cl, 0                   ; Zero size

.done:
        mov     ch, 0                   ; Clear high byte of CX
        pop     ax
        pop     bp
        ret
get_patch_template ENDP

;-----------------------------------------------------------------------------
; get_patch_template_size - Get size of patch template
;
; Input:  AL = patch template ID
; Output: CL = template size (0 if invalid or not supported)
; Uses:   CL
;-----------------------------------------------------------------------------
get_patch_template_size PROC
        push    bp
        mov     bp, sp
        push    bx

        ; Validate patch ID
        cmp     al, PATCH_ID_BURST_IO
        ja      .invalid_id

        ; Get template size
        mov     bl, al
        mov     bh, 0
        mov     cl, [patch_template_sizes + bx]
        jmp     .done

.invalid_id:
        mov     cl, 0

.done:
        pop     bx
        pop     bp
        ret
get_patch_template_size ENDP

;-----------------------------------------------------------------------------
; asm_atomic_patch_bytes - Atomically patch bytes at target address
;
; Input:  [BP+4] = target address, [BP+6] = patch data, [BP+8] = size
; Output: None
; Uses:   All registers (atomic operation)
;-----------------------------------------------------------------------------
asm_atomic_patch_bytes PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        push    ds

        ; Get parameters
        les     di, dword ptr [bp+4]    ; Target address (ES:DI)
        lds     si, dword ptr [bp+6]    ; Patch data (DS:SI)
        mov     cx, word ptr [bp+8]     ; Size

        ; Disable interrupts for atomic operation
        cli

        ; Copy patch bytes
        cld
        rep     movsb

        ; Flush instruction prefetch with near jump
        call    asm_flush_prefetch_near_jump

        ; Re-enable interrupts
        sti

        pop     ds
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret     6                       ; Clean up parameters
asm_atomic_patch_bytes ENDP

;-----------------------------------------------------------------------------
; asm_flush_prefetch_near_jump - Flush instruction prefetch queue
;
; Input:  None
; Output: None
; Uses:   None (preserves all registers)
;-----------------------------------------------------------------------------
asm_flush_prefetch_near_jump PROC
        ; Execute a near jump to flush prefetch queue
        ; This ensures modified instructions are re-fetched from memory
        jmp     short .flush_complete
.flush_complete:
        ret
asm_flush_prefetch_near_jump ENDP

;-----------------------------------------------------------------------------
; Performance validation routines
;-----------------------------------------------------------------------------

; Test function for measuring copy performance
test_copy_performance PROC
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    cx

        ; Set up test parameters
        mov     si, OFFSET test_source_buffer
        mov     di, OFFSET test_dest_buffer
        mov     cx, 1024                ; Test size

        ; Measure baseline performance
        ; (Implementation would use PIT timing here)

        ; Apply optimization patch
        ; (Implementation would apply appropriate patch)

        ; Measure optimized performance
        ; (Implementation would use PIT timing here)

        ; Calculate improvement percentage
        ; (Implementation would calculate improvement)

        pop     cx
        pop     di
        pop     si
        pop     bp
        ret
test_copy_performance ENDP

; Test data buffers
test_source_buffer      db 1024 dup(0AAh)
test_dest_buffer        db 1024 dup(0)

_TEXT ENDS

END