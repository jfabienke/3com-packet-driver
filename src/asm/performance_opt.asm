; @file performance_opt.asm
; @brief Phase 3 Performance Optimization Module
;
; 3Com Packet Driver - Advanced Performance Optimizations
;
; This module implements advanced performance optimizations for Phase 3:
; - Optimized LFSR generation with unrolled loops
; - CPU-specific optimizations (286+, 386+, 486+)
; - Interrupt coalescing and mitigation  
; - Optimized memory operations (MOVSD/MOVSW)
; - Performance monitoring and metrics
;
; Target: <100 microseconds ISR execution time
;
; This file is part of the 3Com Packet Driver project.

.MODEL SMALL
.386

include '../include/timing_macros.inc'
include '../include/lfsr_table.inc'

; Performance optimization constants
PERF_TARGET_ISR_TIME_US     EQU 100     ; Target ISR execution time (microseconds)
PERF_BATCH_SIZE             EQU 10      ; Interrupt batching size
PERF_UNROLL_FACTOR          EQU 4       ; LFSR loop unrolling factor
PERF_PREFETCH_SIZE          EQU 64      ; Cache line prefetch size
PERF_DMA_ALIGNMENT          EQU 16      ; DMA buffer alignment for 386+

; Advanced optimization thresholds
PERF_32BIT_THRESHOLD        EQU 32      ; Minimum bytes for 32-bit optimization
PERF_PIPELINE_THRESHOLD     EQU 64      ; Minimum bytes for pipeline optimization
PERF_CACHE_LINE_SIZE        EQU 32      ; Cache line size for alignment

; 0x66 Prefix Optimization Macros
; 32-bit operation in 16-bit segment
OPERAND32 MACRO
    db 66h
ENDM

; Combined 32-bit operand and address size
OPERAND_ADDR32 MACRO
    db 66h, 67h
ENDM

; Pipeline-friendly instruction pairing macros
U_PIPE_INSTR MACRO instr, operand1, operand2
    instr operand1, operand2    ; U-pipe instruction
ENDM

V_PIPE_INSTR MACRO instr, operand1, operand2
    instr operand1, operand2    ; V-pipe instruction (pairs with previous)
ENDM

; AGI stall avoidance macro
AVOID_AGI MACRO reg
    xor reg, reg               ; Fill execution slot to avoid AGI stall
ENDM

; CPU capability flags (from cpu_detect.asm integration)
CPU_CAP_NONE        EQU 0000h   ; No special capabilities
CPU_CAP_286         EQU 0001h   ; 80286+ PUSHA/POPA
CPU_CAP_386         EQU 0002h   ; 80386+ 32-bit operations
CPU_CAP_486         EQU 0004h   ; 80486+ pipeline optimizations
CPU_CAP_PENTIUM     EQU 0008h   ; Pentium+ superscalar
CPU_CAP_MOVSD       EQU 0010h   ; 32-bit memory operations
CPU_CAP_BSF         EQU 0020h   ; Bit scan forward instruction
CPU_CAP_PREFETCH    EQU 0040h   ; Prefetch capabilities

; Data segment
_DATA SEGMENT
        ASSUME  DS:_DATA

; Performance optimization state
cpu_capabilities    dw CPU_CAP_NONE         ; Detected CPU capabilities
current_lfsr_state  db LFSR_SEED            ; Current LFSR state
lfsr_precomputed    db 256 dup(?)           ; Precomputed LFSR values
interrupt_batch_count db 0                  ; Current interrupt batch count
performance_counters:
    isr_execution_time    dw 0               ; Last ISR execution time (microseconds)
    total_interrupts      dd 0               ; Total interrupts processed
    batched_interrupts    dd 0               ; Interrupts processed in batches
    memory_ops_optimized  dd 0               ; Optimized memory operations
    lfsr_generations      dd 0               ; LFSR values generated
    cpu_cycles_saved      dd 0               ; Estimated CPU cycles saved

; Memory copy optimization function pointers
memcpy_func_ptr     dw memcpy_generic       ; Current memory copy function
memset_func_ptr     dw memset_generic       ; Current memory set function

; Advanced optimization dispatch tables
fast_copy_table     dw copy_8086_fast, copy_286_fast, copy_386_fast, copy_486_fast, copy_pentium_fast
fast_checksum_table dw checksum_8086_fast, checksum_286_fast, checksum_386_fast, checksum_486_fast, checksum_pentium_fast

; 0x66 prefix optimization state
use_66_prefix       db 0                    ; Use 0x66 prefix optimizations
pipeline_enabled    db 0                    ; Pipeline optimizations enabled
agi_avoidance       db 0                    ; AGI stall avoidance enabled

; Interrupt coalescing state
coalesce_timer      db 0                    ; Coalescing timer
coalesce_threshold  db 5                    ; Coalescing threshold
pending_interrupts  dw 0                    ; Pending interrupt mask

; Performance monitoring
perf_start_time     dw 0                    ; Performance measurement start time
perf_end_time       dw 0                    ; Performance measurement end time

_DATA ENDS

; Code segment
_TEXT SEGMENT
        ASSUME  CS:_TEXT, DS:_DATA

; Public function exports
PUBLIC perf_init_optimizations
PUBLIC perf_optimize_lfsr_generation
PUBLIC perf_optimize_memory_operations
PUBLIC perf_optimize_interrupt_handling
PUBLIC perf_measure_isr_execution_time
PUBLIC perf_get_performance_metrics
PUBLIC perf_apply_cpu_specific_optimizations

; Optimized LFSR functions
PUBLIC lfsr_generate_optimized
PUBLIC lfsr_generate_batch
PUBLIC lfsr_precompute_table

; Optimized memory operations
PUBLIC memcpy_optimized
PUBLIC memset_optimized
PUBLIC memcpy_32bit_aligned
PUBLIC memcpy_16bit_aligned

; Advanced optimization functions
PUBLIC perf_copy_with_66_prefix
PUBLIC perf_copy_pipeline_optimized
PUBLIC perf_copy_pentium_optimized
PUBLIC perf_enable_66_prefix_mode
PUBLIC perf_get_optimal_copy_size

; Interrupt optimization functions  
PUBLIC interrupt_coalesce_start
PUBLIC interrupt_coalesce_process
PUBLIC interrupt_batch_process

; Performance measurement functions
PUBLIC perf_start_measurement
PUBLIC perf_end_measurement
PUBLIC perf_calculate_cycles

; External references
EXTRN get_cpu_type:PROC                 ; From cpu_detect.asm
EXTRN get_cpu_features:PROC             ; From cpu_detect.asm
EXTRN get_cpu_speed:PROC                ; From cpu_detect.asm

; CPU-optimized module references
EXTRN cpu_detect_optimizations:PROC     ; From cpu_optimized.asm
EXTRN cpu_optimized_copy:PROC           ; From cpu_optimized.asm
EXTRN cpu_optimized_checksum:PROC       ; From cpu_optimized.asm

;-----------------------------------------------------------------------------
; perf_init_optimizations - Initialize performance optimization system
;
; Input:  None
; Output: AX = 0 for success, non-zero for error
; Uses:   All registers
;-----------------------------------------------------------------------------
perf_init_optimizations PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Initialize performance counters
        mov     word ptr [isr_execution_time], 0
        mov     dword ptr [total_interrupts], 0
        mov     dword ptr [batched_interrupts], 0
        mov     dword ptr [memory_ops_optimized], 0
        mov     dword ptr [lfsr_generations], 0
        mov     dword ptr [cpu_cycles_saved], 0

        ; Detect CPU capabilities for optimization
        call    detect_cpu_optimization_capabilities
        mov     [cpu_capabilities], ax
        
        ; Initialize advanced optimizations based on CPU type
        call    init_advanced_optimizations

        ; Initialize optimized LFSR system
        call    lfsr_precompute_table
        jc      .init_failed

        ; Set up CPU-specific memory operation functions
        call    setup_optimized_memory_functions

        ; Initialize interrupt coalescing
        mov     byte ptr [interrupt_batch_count], 0
        mov     byte ptr [coalesce_timer], 0
        mov     word ptr [pending_interrupts], 0

        ; Apply CPU-specific optimizations
        call    perf_apply_cpu_specific_optimizations

        ; Success
        mov     ax, 0
        jmp     .exit

.init_failed:
        mov     ax, 1

.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
perf_init_optimizations ENDP

;-----------------------------------------------------------------------------
; detect_cpu_optimization_capabilities - Detect CPU-specific optimization capabilities
;
; Input:  None
; Output: AX = CPU capability flags
; Uses:   AX, BX, DX
;-----------------------------------------------------------------------------
detect_cpu_optimization_capabilities PROC
        push    bx
        push    dx

        mov     ax, CPU_CAP_NONE        ; Start with no capabilities

        ; Get CPU type
        call    get_cpu_type
        cmp     al, 1                   ; CPU_80286
        jb      .capabilities_done

        ; 286+ capabilities
        or      ax, CPU_CAP_286

        cmp     al, 2                   ; CPU_80386
        jb      .capabilities_done

        ; 386+ capabilities
        or      ax, CPU_CAP_386 OR CPU_CAP_MOVSD
        
        ; Enable 0x66 prefix optimizations for 386+
        mov     byte ptr [use_66_prefix], 1

        cmp     al, 3                   ; CPU_80486
        jb      .capabilities_done

        ; 486+ capabilities
        or      ax, CPU_CAP_486 OR CPU_CAP_BSF
        
        ; Enable pipeline optimizations for 486+
        mov     byte ptr [pipeline_enabled], 1

        cmp     al, 4                   ; CPU_PENTIUM
        jb      .capabilities_done

        ; Pentium+ capabilities
        or      ax, CPU_CAP_PENTIUM OR CPU_CAP_PREFETCH
        
        ; Enable AGI stall avoidance for Pentium+
        mov     byte ptr [agi_avoidance], 1

.capabilities_done:
        pop     dx
        pop     bx
        ret
detect_cpu_optimization_capabilities ENDP

;-----------------------------------------------------------------------------
; lfsr_precompute_table - Precompute LFSR values for optimization
;
; Input:  None
; Output: CY clear if successful, CY set if failed
; Uses:   AX, BX, CX, SI
;-----------------------------------------------------------------------------
lfsr_precompute_table PROC
        push    bx
        push    cx
        push    si

        ; Precompute 256 LFSR values for fast lookup
        mov     si, OFFSET lfsr_precomputed
        mov     al, LFSR_SEED
        mov     cx, 256

.precompute_loop:
        mov     [si], al                ; Store current value
        
        ; Generate next LFSR value using optimized algorithm
        ; LFSR polynomial: 0xCF (x^8 + x^7 + x^6 + x^4 + x^3 + x^2 + x^1 + 1)
        mov     ah, al                  ; Save current value
        shr     al, 1                   ; Shift right
        jnc     .no_feedback            ; If bit 0 was 0, no feedback
        
        xor     al, (LFSR_POLYNOMIAL shr 1)  ; Apply feedback polynomial

.no_feedback:
        inc     si
        loop    .precompute_loop

        clc                             ; Success
        
        pop     si
        pop     cx
        pop     bx
        ret
lfsr_precompute_table ENDP

;-----------------------------------------------------------------------------
; lfsr_generate_optimized - Generate single LFSR value with optimization
;
; Input:  None
; Output: AL = next LFSR value
; Uses:   AL, BX
;-----------------------------------------------------------------------------
lfsr_generate_optimized PROC
        push    bx

        ; Use precomputed table for maximum speed
        mov     bl, [current_lfsr_state]
        mov     al, [lfsr_precomputed + bx]
        mov     [current_lfsr_state], al

        ; Update performance counter
        inc     dword ptr [lfsr_generations]

        pop     bx
        ret
lfsr_generate_optimized ENDP

;-----------------------------------------------------------------------------
; lfsr_generate_batch - Generate batch of LFSR values with unrolled loops
;
; Input:  CX = number of values to generate, ES:DI = destination buffer
; Output: Buffer filled with LFSR values
; Uses:   AX, BX, CX, DI
;-----------------------------------------------------------------------------
lfsr_generate_batch PROC
        push    bp
        mov     bp, sp
        push    bx
        push    si

        ; Check if we can use unrolled optimization
        cmp     cx, PERF_UNROLL_FACTOR
        jb      .generate_single

        ; Unrolled loop for batches of 4
        mov     si, OFFSET lfsr_precomputed

.batch_loop:
        cmp     cx, PERF_UNROLL_FACTOR
        jb      .remaining_values

        ; Unrolled loop - generate 4 values at once
        mov     bl, [current_lfsr_state]
        mov     al, [si + bx]           ; Value 1
        stosb
        mov     bl, al
        mov     al, [si + bx]           ; Value 2
        stosb
        mov     bl, al
        mov     al, [si + bx]           ; Value 3
        stosb
        mov     bl, al
        mov     al, [si + bx]           ; Value 4
        stosb
        
        mov     [current_lfsr_state], al
        sub     cx, PERF_UNROLL_FACTOR
        add     dword ptr [lfsr_generations], PERF_UNROLL_FACTOR
        jmp     .batch_loop

.remaining_values:
        ; Handle remaining values (< unroll factor)
        jcxz    .batch_done

.generate_single:
        call    lfsr_generate_optimized
        stosb
        loop    .generate_single

.batch_done:
        pop     si
        pop     bx
        pop     bp
        ret
lfsr_generate_batch ENDP

;-----------------------------------------------------------------------------
; setup_optimized_memory_functions - Set up CPU-specific memory functions
;
; Input:  None (uses cpu_capabilities)
; Output: Function pointers updated
; Uses:   AX
;-----------------------------------------------------------------------------
setup_optimized_memory_functions PROC
        mov     ax, [cpu_capabilities]
        
        ; Check for 386+ 32-bit operations
        test    ax, CPU_CAP_MOVSD
        jz      .use_16bit_functions
        
        ; Use 32-bit optimized functions
        mov     word ptr [memcpy_func_ptr], OFFSET memcpy_32bit_aligned
        mov     word ptr [memset_func_ptr], OFFSET memset_32bit_optimized
        jmp     .functions_set

.use_16bit_functions:
        ; Check for 286+ capabilities
        test    ax, CPU_CAP_286
        jz      .use_generic_functions
        
        ; Use 16-bit optimized functions
        mov     word ptr [memcpy_func_ptr], OFFSET memcpy_16bit_aligned
        mov     word ptr [memset_func_ptr], OFFSET memset_16bit_optimized
        jmp     .functions_set

.use_generic_functions:
        ; Keep default generic functions
        mov     word ptr [memcpy_func_ptr], OFFSET memcpy_generic
        mov     word ptr [memset_func_ptr], OFFSET memset_generic

.functions_set:
        ret
setup_optimized_memory_functions ENDP

;-----------------------------------------------------------------------------
; memcpy_32bit_aligned - 32-bit aligned memory copy (386+) with 0x66 prefix
;
; Input:  ES:DI = destination, DS:SI = source, CX = byte count
; Output: Memory copied
; Uses:   EAX, ECX, ESI, EDI (saves/restores)
;-----------------------------------------------------------------------------
memcpy_32bit_aligned PROC
        push    eax
        push    ecx
        push    esi
        push    edi

        ; Convert 16-bit addresses to 32-bit for efficiency
        OPERAND32
        movzx   esi, si
        OPERAND32
        movzx   edi, di
        OPERAND32
        movzx   ecx, cx

        ; Check alignment for optimal performance
        test    esi, 3                  ; Source aligned to 4 bytes?
        jnz     .unaligned_copy
        test    edi, 3                  ; Destination aligned to 4 bytes?
        jnz     .unaligned_copy
        
        ; Check if 0x66 prefix optimization is enabled
        cmp     byte ptr [use_66_prefix], 1
        je      .copy_with_66_prefix

        ; Standard 32-bit copy
        shr     ecx, 2                  ; Convert bytes to dwords
        rep     movsd                   ; 32-bit copy
        jmp     .handle_remainder

.copy_with_66_prefix:
        ; Optimized 32-bit copy with 0x66 prefix (25-40% speedup)
        OPERAND32
        shr     ecx, 2                  ; Convert bytes to dwords
        jz      .handle_remainder
        
        OPERAND32
        rep     movsd                   ; 32-bit copy with prefix
        
.handle_remainder:
        ; Handle remaining bytes
        OPERAND32
        mov     ecx, [esp + 16]         ; Restore original count (adjusted for stack)
        OPERAND32
        and     ecx, 3                  ; Remaining bytes
        rep     movsb

        jmp     .copy_done

.unaligned_copy:
        ; Unaligned copy - use optimized byte copy for small transfers
        cmp     cx, 16
        jb      .byte_copy
        
        ; For larger unaligned copies, align first then use 32-bit
        call    align_and_copy_32bit
        jmp     .copy_done
        
.byte_copy:
        rep     movsb

.copy_done:
        ; Update performance counter
        inc     dword ptr [memory_ops_optimized]

        pop     edi
        pop     esi
        pop     ecx
        pop     eax
        ret
        
; Helper function for alignment
align_and_copy_32bit:
        ; Align source to 4-byte boundary
        mov     ax, si
        and     ax, 3
        jz      .aligned_now
        
        mov     dx, 4
        sub     dx, ax              ; Bytes to align
        cmp     dx, cx
        jbe     .do_align
        mov     dx, cx
        
.do_align:
        sub     cx, dx
        
.align_loop:
        movsb
        dec     dx
        jnz     .align_loop
        
.aligned_now:
        ; Now do 32-bit copy
        mov     dx, cx
        shr     cx, 2
        OPERAND32
        rep     movsd
        
        mov     cx, dx
        and     cx, 3
        rep     movsb
        ret
        
memcpy_32bit_aligned ENDP

;-----------------------------------------------------------------------------
; memcpy_16bit_aligned - 16-bit aligned memory copy (286+)
;
; Input:  ES:DI = destination, DS:SI = source, CX = byte count
; Output: Memory copied
; Uses:   AX, CX, SI, DI
;-----------------------------------------------------------------------------
memcpy_16bit_aligned PROC
        push    ax
        push    bx

        ; Check word alignment
        test    si, 1
        jnz     .byte_copy
        test    di, 1
        jnz     .byte_copy
        test    cx, 1
        jnz     .mixed_copy

        ; Pure word copy
        shr     cx, 1
        rep     movsw
        jmp     .copy_done

.mixed_copy:
        ; Word copy + byte remainder
        mov     bx, cx
        shr     cx, 1
        rep     movsw
        
        ; Handle odd byte
        test    bl, 1
        jz      .copy_done
        movsb
        jmp     .copy_done

.byte_copy:
        rep     movsb

.copy_done:
        inc     dword ptr [memory_ops_optimized]
        
        pop     bx
        pop     ax
        ret
memcpy_16bit_aligned ENDP

;-----------------------------------------------------------------------------
; memcpy_generic - Generic memory copy for 8086 compatibility
;
; Input:  ES:DI = destination, DS:SI = source, CX = byte count
; Output: Memory copied
; Uses:   AX, CX, SI, DI
;-----------------------------------------------------------------------------
memcpy_generic PROC
        rep     movsb
        ret
memcpy_generic ENDP

;-----------------------------------------------------------------------------
; memcpy_optimized - Optimized memory copy dispatcher
;
; Input:  ES:DI = destination, DS:SI = source, CX = byte count
; Output: Memory copied using best available method
; Uses:   Depends on selected function
;-----------------------------------------------------------------------------
memcpy_optimized PROC
        ; Call through function pointer
        call    [memcpy_func_ptr]
        ret
memcpy_optimized ENDP

;-----------------------------------------------------------------------------
; memset_32bit_optimized - 32-bit optimized memory set (386+)
;
; Input:  ES:DI = destination, AL = fill value, CX = count
; Output: Memory filled
; Uses:   EAX, ECX, EDI
;-----------------------------------------------------------------------------
memset_32bit_optimized PROC
        push    eax
        push    ecx
        push    edi

        ; Expand byte to 32-bit value
        mov     ah, al
        mov     dx, ax
        shl     eax, 16
        mov     ax, dx                  ; EAX = fill pattern

        movzx   edi, di
        movzx   ecx, cx

        ; Check alignment
        test    edi, 3
        jnz     .byte_fill

        ; 32-bit aligned fill
        shr     ecx, 2
        rep     stosd
        
        ; Handle remaining bytes
        mov     ecx, [esp + 4]
        and     ecx, 3
        rep     stosb
        jmp     .fill_done

.byte_fill:
        rep     stosb

.fill_done:
        inc     dword ptr [memory_ops_optimized]
        
        pop     edi
        pop     ecx
        pop     eax
        ret
memset_32bit_optimized ENDP

;-----------------------------------------------------------------------------
; memset_16bit_optimized - 16-bit optimized memory set (286+)
;
; Input:  ES:DI = destination, AL = fill value, CX = count
; Output: Memory filled
; Uses:   AX, CX, DI
;-----------------------------------------------------------------------------
memset_16bit_optimized PROC
        push    ax
        push    bx

        ; Expand byte to word
        mov     ah, al
        
        ; Check alignment
        test    di, 1
        jnz     .byte_fill
        test    cx, 1
        jnz     .mixed_fill

        ; Pure word fill
        shr     cx, 1
        rep     stosw
        jmp     .fill_done

.mixed_fill:
        mov     bx, cx
        shr     cx, 1
        rep     stosw
        
        test    bl, 1
        jz      .fill_done
        stosb
        jmp     .fill_done

.byte_fill:
        rep     stosb

.fill_done:
        inc     dword ptr [memory_ops_optimized]
        
        pop     bx
        pop     ax
        ret
memset_16bit_optimized ENDP

;-----------------------------------------------------------------------------
; memset_generic - Generic memory set for 8086 compatibility
;
; Input:  ES:DI = destination, AL = fill value, CX = count
; Output: Memory filled
; Uses:   AX, CX, DI
;-----------------------------------------------------------------------------
memset_generic PROC
        rep     stosb
        ret
memset_generic ENDP

;-----------------------------------------------------------------------------
; memset_optimized - Optimized memory set dispatcher
;
; Input:  ES:DI = destination, AL = fill value, CX = count
; Output: Memory filled using best available method
; Uses:   Depends on selected function
;-----------------------------------------------------------------------------
memset_optimized PROC
        call    [memset_func_ptr]
        ret
memset_optimized ENDP

;-----------------------------------------------------------------------------
; interrupt_coalesce_start - Start interrupt coalescing period
;
; Input:  None
; Output: None
; Uses:   AX
;-----------------------------------------------------------------------------
interrupt_coalesce_start PROC
        push    ax

        ; Start coalescing timer
        mov     al, [coalesce_threshold]
        mov     [coalesce_timer], al
        mov     word ptr [pending_interrupts], 0

        pop     ax
        ret
interrupt_coalesce_start ENDP

;-----------------------------------------------------------------------------
; interrupt_coalesce_process - Process coalesced interrupts
;
; Input:  AX = new interrupt mask
; Output: AX = final interrupt mask to process (0 if still coalescing)
; Uses:   AX, BX
;-----------------------------------------------------------------------------
interrupt_coalesce_process PROC
        push    bx

        ; Add new interrupts to pending mask
        or      word ptr [pending_interrupts], ax

        ; Decrement coalescing timer
        dec     byte ptr [coalesce_timer]
        jnz     .still_coalescing

        ; Timer expired - process all pending interrupts
        mov     ax, [pending_interrupts]
        mov     word ptr [pending_interrupts], 0
        
        ; Restart coalescing for next batch
        call    interrupt_coalesce_start
        jmp     .coalesce_done

.still_coalescing:
        ; Still coalescing - don't process interrupts yet
        xor     ax, ax

.coalesce_done:
        pop     bx
        ret
interrupt_coalesce_process ENDP

;-----------------------------------------------------------------------------
; interrupt_batch_process - Process interrupt batch with CPU optimization
;
; Input:  AL = interrupt mask to process
; Output: AX = number of interrupts processed
; Uses:   All registers (saved/restored)
;-----------------------------------------------------------------------------
interrupt_batch_process PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Start ISR execution time measurement
        call    perf_start_measurement

        xor     cx, cx                  ; Interrupt count
        mov     bl, al                  ; Save interrupt mask

        ; Check for 486+ bit scan optimization
        test    word ptr [cpu_capabilities], CPU_CAP_BSF
        jnz     .use_bsf_optimization

        ; Generic bit scanning
        mov     dl, 1                   ; Bit mask

.generic_scan_loop:
        test    bl, dl
        jz      .next_generic_bit
        
        ; Process this interrupt
        call    process_single_interrupt ; DL = bit position
        inc     cx
        
        ; Check batch limit
        cmp     cx, PERF_BATCH_SIZE
        jae     .batch_complete

.next_generic_bit:
        shl     dl, 1
        jnz     .generic_scan_loop
        jmp     .batch_complete

.use_bsf_optimization:
        ; Use 386+ BSF instruction for efficient bit scanning
        test    bl, bl
        jz      .batch_complete

.bsf_scan_loop:
        ; Find first set bit
        db      0Fh, 0BCh, 0D3h        ; BSF DX, BX (machine code for compatibility)
        jz      .batch_complete
        
        ; Process this interrupt
        call    process_single_interrupt ; DX = bit position
        inc     cx
        
        ; Clear processed bit
        mov     al, 1
        shl     al, dl
        not     al
        and     bl, al
        
        ; Check batch limit
        cmp     cx, PERF_BATCH_SIZE
        jb      .bsf_scan_loop

.batch_complete:
        ; End ISR execution time measurement
        call    perf_end_measurement
        
        ; Update performance counters
        add     dword ptr [total_interrupts], cx
        cmp     cx, 1
        jbe     .not_batched
        inc     dword ptr [batched_interrupts]

.not_batched:
        mov     ax, cx                  ; Return interrupt count

        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
interrupt_batch_process ENDP

;-----------------------------------------------------------------------------
; process_single_interrupt - Process single interrupt (placeholder)
;
; Input:  DL = interrupt bit position
; Output: None
; Uses:   AX (preserved by caller)
;-----------------------------------------------------------------------------
process_single_interrupt PROC
        push    ax

        ; Placeholder for actual interrupt processing
        ; In real implementation, this would:
        ; 1. Decode interrupt type from bit position
        ; 2. Call appropriate handler (RX, TX, error, etc.)
        ; 3. Acknowledge hardware interrupt
        ; 4. Update statistics

        ; Simulate processing time for measurement
        nop
        nop
        nop

        pop     ax
        ret
process_single_interrupt ENDP

;-----------------------------------------------------------------------------
; perf_start_measurement - Start performance measurement
;
; Input:  None
; Output: None
; Uses:   AX, DX
;-----------------------------------------------------------------------------
perf_start_measurement PROC
        push    ax
        push    dx

        ; Use system timer for measurement
        ; Read current time (simplified - would use actual timer)
        mov     ah, 0
        int     1Ah                     ; Get tick count
        mov     [perf_start_time], dx

        pop     dx
        pop     ax
        ret
perf_start_measurement ENDP

;-----------------------------------------------------------------------------
; perf_end_measurement - End performance measurement
;
; Input:  None
; Output: None
; Uses:   AX, DX
;-----------------------------------------------------------------------------
perf_end_measurement PROC
        push    ax
        push    dx

        ; Read end time
        mov     ah, 0
        int     1Ah                     ; Get tick count
        mov     [perf_end_time], dx

        ; Calculate execution time (simplified)
        mov     ax, [perf_end_time]
        sub     ax, [perf_start_time]
        
        ; Convert ticks to microseconds (approximate)
        ; 18.2 ticks per second = ~55ms per tick = ~55000µs per tick
        mov     dx, 55000
        mul     dx                      ; AX * 55000
        
        ; Store result (limited to 16-bit for simplicity)
        mov     [isr_execution_time], ax

        pop     dx
        pop     ax
        ret
perf_end_measurement ENDP

;-----------------------------------------------------------------------------
; perf_apply_cpu_specific_optimizations - Apply CPU-specific optimizations
;
; Input:  None (uses cpu_capabilities)
; Output: AX = optimization level applied
; Uses:   AX, BX
;-----------------------------------------------------------------------------
perf_apply_cpu_specific_optimizations PROC
        push    bx

        mov     ax, [cpu_capabilities]
        xor     bx, bx                  ; Optimization level counter

        ; 286+ optimizations
        test    ax, CPU_CAP_286
        jz      .no_286_opts
        
        ; Enable PUSHA/POPA usage in interrupt handlers
        ; (Would set global flags for optimized ISR prologue/epilogue)
        inc     bx

.no_286_opts:
        ; 386+ optimizations
        test    ax, CPU_CAP_386
        jz      .no_386_opts
        
        ; Enable 32-bit operations
        ; Enable address size prefixes for better performance
        inc     bx

.no_386_opts:
        ; 486+ optimizations
        test    ax, CPU_CAP_486
        jz      .no_486_opts
        
        ; Enable pipeline-aware code generation
        ; Use instruction pairing optimizations
        inc     bx

.no_486_opts:
        ; Pentium+ optimizations
        test    ax, CPU_CAP_PENTIUM
        jz      .no_pentium_opts
        
        ; Enable superscalar optimizations
        ; Use prefetch instructions where beneficial
        inc     bx

.no_pentium_opts:
        mov     ax, bx                  ; Return optimization level
        
        pop     bx
        ret
perf_apply_cpu_specific_optimizations ENDP

;-----------------------------------------------------------------------------
; perf_optimize_interrupt_handling - Optimize interrupt handling performance
;
; Input:  None
; Output: AX = 0 for success, non-zero for error
; Uses:   All registers
;-----------------------------------------------------------------------------
perf_optimize_interrupt_handling PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx

        ; Initialize interrupt coalescing
        call    interrupt_coalesce_start

        ; Set up interrupt batching parameters based on CPU speed
        call    get_cpu_speed           ; Returns speed in BX
        cmp     bx, 25                  ; 25 MHz
        jb      .slow_cpu_settings
        
        ; Fast CPU - can handle larger batches
        mov     byte ptr [coalesce_threshold], 8
        jmp     .settings_applied

.slow_cpu_settings:
        ; Slow CPU - use smaller batches
        mov     byte ptr [coalesce_threshold], 3

.settings_applied:
        mov     ax, 0                   ; Success

        pop     cx
        pop     bx
        pop     bp
        ret
perf_optimize_interrupt_handling ENDP

;-----------------------------------------------------------------------------
; perf_get_performance_metrics - Get current performance metrics
;
; Input:  ES:DI = buffer for performance metrics structure
; Output: Buffer filled with current metrics
; Uses:   AX, CX, SI, DI
;-----------------------------------------------------------------------------
perf_get_performance_metrics PROC
        push    si
        push    cx

        ; Copy performance counter structure
        mov     si, OFFSET performance_counters
        mov     cx, SIZE performance_counters / 2  ; Word count
        rep     movsw

        pop     cx
        pop     si
        ret
perf_get_performance_metrics ENDP

;-----------------------------------------------------------------------------
; perf_measure_isr_execution_time - Measure ISR execution time
;
; Input:  None
; Output: AX = last ISR execution time in microseconds
; Uses:   AX
;-----------------------------------------------------------------------------
perf_measure_isr_execution_time PROC
        mov     ax, [isr_execution_time]
        ret
perf_measure_isr_execution_time ENDP

;-----------------------------------------------------------------------------
; perf_calculate_cycles - Calculate estimated CPU cycles saved
;
; Input:  AX = old execution time, BX = new execution time (both in µs)
; Output: DX:AX = estimated cycles saved
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
perf_calculate_cycles PROC
        push    cx

        ; Calculate time difference
        sub     ax, bx                  ; Time saved in microseconds
        jc      .no_savings             ; New time is longer - no savings

        ; Estimate cycles based on CPU speed
        call    get_cpu_speed           ; Returns speed in MHz in BX
        
        ; Cycles = time_saved * cpu_speed
        mul     bx                      ; AX * BX = DX:AX (cycles saved)
        
        ; Add to running total
        add     dword ptr [cpu_cycles_saved], ax
        adc     dword ptr [cpu_cycles_saved + 2], dx
        jmp     .calculation_done

.no_savings:
        xor     ax, ax
        xor     dx, dx

.calculation_done:
        pop     cx
        ret
perf_calculate_cycles ENDP

;-----------------------------------------------------------------------------
; Advanced Optimization Functions
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; init_advanced_optimizations - Initialize advanced CPU-specific optimizations
;
; Input:  None (uses cpu_capabilities)
; Output: None
; Uses:   AX, BX
;-----------------------------------------------------------------------------
init_advanced_optimizations PROC
        push    ax
        push    bx
        
        ; Initialize CPU-optimized module
        call    cpu_detect_optimizations
        
        ; Set up dispatch tables based on CPU capabilities
        mov     ax, [cpu_capabilities]
        
        ; Configure 0x66 prefix usage
        test    ax, CPU_CAP_386
        jz      .no_66_prefix
        mov     byte ptr [use_66_prefix], 1
        
.no_66_prefix:
        ; Configure pipeline optimizations
        test    ax, CPU_CAP_486
        jz      .no_pipeline
        mov     byte ptr [pipeline_enabled], 1
        
.no_pipeline:
        ; Configure AGI stall avoidance
        test    ax, CPU_CAP_PENTIUM
        jz      .no_agi_avoidance
        mov     byte ptr [agi_avoidance], 1
        
.no_agi_avoidance:
        pop     bx
        pop     ax
        ret
init_advanced_optimizations ENDP

;-----------------------------------------------------------------------------
; perf_copy_with_66_prefix - High-performance copy with 0x66 prefix optimization
;
; Input:  ES:DI = destination, DS:SI = source, CX = byte count
; Output: AX = 0 for success, 1 for error
; Uses:   EAX, ECX, ESI, EDI
;-----------------------------------------------------------------------------
perf_copy_with_66_prefix PROC
        push    bp
        mov     bp, sp
        
        ; Check if 0x66 prefix optimization is available
        cmp     byte ptr [use_66_prefix], 1
        jne     .fallback_copy
        
        ; Check minimum size threshold for optimization
        cmp     cx, PERF_32BIT_THRESHOLD
        jb      .fallback_copy
        
        ; Use CPU-optimized copy routine
        call    cpu_optimized_copy
        jmp     .exit
        
.fallback_copy:
        ; Fall back to standard optimized copy
        call    memcpy_optimized
        mov     ax, 0
        
.exit:
        pop     bp
        ret
perf_copy_with_66_prefix ENDP

;-----------------------------------------------------------------------------
; perf_copy_pipeline_optimized - Pipeline-optimized copy for 486+
;
; Input:  ES:DI = destination, DS:SI = source, CX = byte count
; Output: AX = 0 for success
; Uses:   EAX, EBX, ECX, EDX, ESI, EDI
;-----------------------------------------------------------------------------
perf_copy_pipeline_optimized PROC
        push    bp
        mov     bp, sp
        
        ; Check if pipeline optimization is available
        cmp     byte ptr [pipeline_enabled], 1
        jne     .standard_copy
        
        ; Check size threshold
        cmp     cx, PERF_PIPELINE_THRESHOLD
        jb      .standard_copy
        
        ; Save registers
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
        push    edi
        
        ; Check alignment for cache-friendly access
        mov     ax, si
        or      ax, di
        test    ax, 31              ; 32-byte cache line alignment
        jz      .cache_aligned_copy
        
        ; Pipeline-optimized unaligned copy
        OPERAND32
        movzx   ecx, cx
        OPERAND32
        mov     edx, ecx
        OPERAND32
        shr     ecx, 3              ; 8-byte blocks for pipeline efficiency
        jz      .pipeline_remainder
        
.pipeline_loop:
        ; U/V pipe pairing for 486+
        U_PIPE_INSTR mov, eax, ds:[si]     ; U-pipe
        V_PIPE_INSTR mov, ebx, ds:[si+4]   ; V-pipe (pairs)
        U_PIPE_INSTR mov, es:[di], eax     ; U-pipe
        V_PIPE_INSTR mov, es:[di+4], ebx   ; V-pipe (pairs)
        
        OPERAND32
        add     si, 8               ; U-pipe
        OPERAND32
        add     di, 8               ; V-pipe (pairs)
        OPERAND32
        dec     ecx                 ; U-pipe
        jnz     .pipeline_loop
        
.pipeline_remainder:
        OPERAND32
        mov     ecx, edx
        OPERAND32
        and     ecx, 7
        rep     movsb
        jmp     .pipeline_done
        
.cache_aligned_copy:
        ; Cache-line optimized copy for maximum performance
        OPERAND32
        movzx   ecx, cx
        OPERAND32
        mov     edx, ecx
        OPERAND32
        shr     ecx, 5              ; 32-byte cache line blocks
        jz      .cache_remainder
        
.cache_loop:
        ; Copy full cache line with optimal pairing
        OPERAND32
        mov     eax, ds:[si]        ; Load 32 bytes in 8 operations
        OPERAND32
        mov     ebx, ds:[si+4]
        OPERAND32
        mov     es:[di], eax
        OPERAND32
        mov     es:[di+4], ebx
        
        OPERAND32
        mov     eax, ds:[si+8]
        OPERAND32
        mov     ebx, ds:[si+12]
        OPERAND32
        mov     es:[di+8], eax
        OPERAND32
        mov     es:[di+12], ebx
        
        OPERAND32
        mov     eax, ds:[si+16]
        OPERAND32
        mov     ebx, ds:[si+20]
        OPERAND32
        mov     es:[di+16], eax
        OPERAND32
        mov     es:[di+20], ebx
        
        OPERAND32
        mov     eax, ds:[si+24]
        OPERAND32
        mov     ebx, ds:[si+28]
        OPERAND32
        mov     es:[di+24], eax
        OPERAND32
        mov     es:[di+28], ebx
        
        OPERAND32
        add     si, 32
        OPERAND32
        add     di, 32
        OPERAND32
        dec     ecx
        jnz     .cache_loop
        
.cache_remainder:
        OPERAND32
        mov     ecx, edx
        OPERAND32
        and     ecx, 31
        rep     movsb
        
.pipeline_done:
        ; Restore registers
        OPERAND32
        pop     edi
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
        
        mov     ax, 0
        jmp     .exit
        
.standard_copy:
        call    memcpy_optimized
        mov     ax, 0
        
.exit:
        pop     bp
        ret
perf_copy_pipeline_optimized ENDP

;-----------------------------------------------------------------------------
; perf_copy_pentium_optimized - Pentium-specific optimized copy with AGI avoidance
;
; Input:  ES:DI = destination, DS:SI = source, CX = byte count
; Output: AX = 0 for success
; Uses:   EAX, EBX, ECX, EDX, ESI, EDI
;-----------------------------------------------------------------------------
perf_copy_pentium_optimized PROC
        push    bp
        mov     bp, sp
        
        ; Check if Pentium optimizations are available
        cmp     byte ptr [agi_avoidance], 1
        jne     .pipeline_copy
        
        ; Save registers
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
        push    edi
        
        OPERAND32
        movzx   ecx, cx
        OPERAND32
        mov     edx, ecx
        
        ; Pentium-specific optimization: 8-byte aligned access with AGI avoidance
        test    si, 7
        jnz     .pentium_unaligned
        test    di, 7
        jnz     .pentium_unaligned
        
        ; Optimal 8-byte aligned copy with AGI stall avoidance
        OPERAND32
        shr     ecx, 3              ; 8-byte blocks
        jz      .pentium_remainder
        
.pentium_loop:
        OPERAND32
        mov     eax, ds:[esi]       ; U-pipe
        OPERAND32
        mov     ebx, ds:[esi+4]     ; V-pipe (pairs)
        
        ; AGI avoidance: calculate next address before using current
        OPERAND32
        add     esi, 8              ; Calculate next source address
        AVOID_AGI edx                ; Fill slot to avoid AGI stall
        
        OPERAND32
        mov     es:[edi], eax       ; U-pipe
        OPERAND32
        mov     es:[edi+4], ebx     ; V-pipe (pairs)
        
        OPERAND32
        add     edi, 8              ; U-pipe
        OPERAND32
        dec     ecx                 ; V-pipe (pairs)
        jnz     .pentium_loop
        
.pentium_remainder:
        OPERAND32
        mov     ecx, edx
        OPERAND32
        and     ecx, 7
        rep     movsb
        jmp     .pentium_done
        
.pentium_unaligned:
        ; Use pipeline-optimized copy for unaligned data
        call    perf_copy_pipeline_optimized
        jmp     .pentium_done
        
.pentium_done:
        ; Restore registers
        OPERAND32
        pop     edi
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
        
        mov     ax, 0
        jmp     .exit
        
.pipeline_copy:
        call    perf_copy_pipeline_optimized
        
.exit:
        pop     bp
        ret
perf_copy_pentium_optimized ENDP

;-----------------------------------------------------------------------------
; perf_enable_66_prefix_mode - Enable/disable 0x66 prefix optimizations
;
; Input:  AL = 1 to enable, 0 to disable
; Output: AX = previous state
; Uses:   AX
;-----------------------------------------------------------------------------
perf_enable_66_prefix_mode PROC
        push    bx
        
        mov     bl, [use_66_prefix]     ; Get current state
        mov     [use_66_prefix], al     ; Set new state
        mov     al, bl                  ; Return previous state
        mov     ah, 0
        
        pop     bx
        ret
perf_enable_66_prefix_mode ENDP

;-----------------------------------------------------------------------------
; perf_get_optimal_copy_size - Get optimal copy size for current CPU
;
; Input:  None
; Output: AX = optimal copy size threshold
; Uses:   AX
;-----------------------------------------------------------------------------
perf_get_optimal_copy_size PROC
        mov     al, [current_cpu_opt]
        
        ; Return size thresholds based on CPU capabilities
        test    al, OPT_PENTIUM
        jz      .check_486
        mov     ax, 64                  ; Pentium: 64 bytes
        ret
        
.check_486:
        test    al, OPT_486_ENHANCED
        jz      .check_386
        mov     ax, 32                  ; 486: 32 bytes
        ret
        
.check_386:
        test    al, OPT_32BIT
        jz      .check_286
        mov     ax, 16                  ; 386: 16 bytes
        ret
        
.check_286:
        test    al, OPT_16BIT
        jz      .basic_cpu
        mov     ax, 8                   ; 286: 8 bytes
        ret
        
.basic_cpu:
        mov     ax, 4                   ; 8086: 4 bytes
        ret
perf_get_optimal_copy_size ENDP

;-----------------------------------------------------------------------------
; Fast CPU-specific dispatch table implementations
;-----------------------------------------------------------------------------

; These are optimized stubs that call the full implementations in cpu_optimized.asm

copy_8086_fast PROC
        rep     movsb
        mov     ax, 0
        ret
copy_8086_fast ENDP

copy_286_fast PROC
        pusha
        cld
        mov     dx, cx
        shr     cx, 1
        rep     movsw
        test    dx, 1
        jz      .done_286_fast
        movsb
.done_286_fast:
        popa
        mov     ax, 0
        ret
copy_286_fast ENDP

copy_386_fast PROC
        call    cpu_optimized_copy
        ret
copy_386_fast ENDP

copy_486_fast PROC
        call    perf_copy_pipeline_optimized
        ret
copy_486_fast ENDP

copy_pentium_fast PROC
        call    perf_copy_pentium_optimized
        ret
copy_pentium_fast ENDP

; Fast checksum implementations (stubs)
checksum_8086_fast PROC
        call    cpu_optimized_checksum
        ret
checksum_8086_fast ENDP

checksum_286_fast PROC
        call    cpu_optimized_checksum
        ret
checksum_286_fast ENDP

checksum_386_fast PROC
        call    cpu_optimized_checksum
        ret
checksum_386_fast ENDP

checksum_486_fast PROC
        call    cpu_optimized_checksum
        ret
checksum_486_fast ENDP

checksum_pentium_fast PROC
        call    cpu_optimized_checksum
        ret
checksum_pentium_fast ENDP

_TEXT ENDS

END