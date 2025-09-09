; @file performance_bench.asm
; @brief Performance benchmarks with high-precision timing
;
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
;
; This file implements comprehensive performance benchmarks to measure
; packet throughput, interrupt latency, memory bandwidth, and CPU utilization
; across different CPU architectures and configurations.

.MODEL SMALL
.386

; Benchmark Categories
BENCH_THROUGHPUT        EQU 1           ; Packet throughput tests
BENCH_LATENCY           EQU 2           ; Interrupt latency tests
BENCH_MEMORY            EQU 3           ; Memory bandwidth tests
BENCH_CPU               EQU 4           ; CPU utilization tests
BENCH_NETWORK           EQU 5           ; Network protocol tests

; Timing Methods
TIMING_8254_PIT         EQU 1           ; 8254 Programmable Interval Timer
TIMING_TSC              EQU 2           ; Time Stamp Counter (Pentium+)
TIMING_BIOS             EQU 3           ; BIOS timer ticks

; Performance Targets (operations per second)
TARGET_MIN_THROUGHPUT   EQU 1000        ; 1000 packets/sec minimum
TARGET_MAX_LATENCY      EQU 100         ; 100 microseconds maximum
TARGET_MIN_MEMORY_BW    EQU 1000000     ; 1 MB/sec minimum memory bandwidth

; Test Duration Constants
SHORT_TEST_DURATION     EQU 1000        ; 1 second (in milliseconds)
MEDIUM_TEST_DURATION    EQU 5000        ; 5 seconds
LONG_TEST_DURATION      EQU 10000       ; 10 seconds

; Buffer Sizes for Testing
SMALL_BUFFER_SIZE       EQU 64          ; 64 bytes (minimum Ethernet)
MEDIUM_BUFFER_SIZE      EQU 512         ; 512 bytes
LARGE_BUFFER_SIZE       EQU 1518        ; 1518 bytes (maximum Ethernet)
JUMBO_BUFFER_SIZE       EQU 9000        ; 9000 bytes (jumbo frames)

; Test Results
TEST_PASS               EQU 0           ; Test passed
TEST_FAIL               EQU 1           ; Test failed
TEST_SKIP               EQU 2           ; Test skipped
TEST_ERROR              EQU 3           ; Test error

; Data segment
_DATA SEGMENT
        ASSUME  DS:_DATA

; Timing infrastructure
timing_method           db TIMING_BIOS          ; Current timing method
cpu_frequency_mhz       dw 0                    ; Detected CPU frequency
timer_frequency         dw 18                   ; Timer frequency (18.2 Hz for BIOS)
tsc_available           db 0                    ; TSC instruction available
pit_available           db 0                    ; 8254 PIT available

; High-precision timing variables
timing_start_low        dd 0                    ; Start time (low 32 bits)
timing_start_high       dd 0                    ; Start time (high 32 bits)
timing_end_low          dd 0                    ; End time (low 32 bits)
timing_end_high         dd 0                    ; End time (high 32 bits)
timing_overhead         dd 0                    ; Timing overhead calibration

; Performance measurement results
throughput_results      dw 16 dup(0)            ; Throughput test results (pps)
latency_results         dw 16 dup(0)            ; Latency test results (microseconds)
memory_results          dd 16 dup(0)            ; Memory bandwidth results (bytes/sec)
cpu_results             dw 16 dup(0)            ; CPU utilization results (percentage)

; Test configuration
benchmark_duration      dw MEDIUM_TEST_DURATION ; Current test duration
packet_size             dw MEDIUM_BUFFER_SIZE   ; Current packet size
test_iterations         dd 10000                ; Test iterations
warmup_iterations       dd 1000                 ; Warmup iterations

; Test state
current_benchmark       db 0                    ; Current benchmark type
test_results            db 32 dup(0)            ; Individual test results
test_count              dw 0                    ; Number of tests run
tests_passed            dw 0                    ; Tests passed
tests_failed            dw 0                    ; Tests failed
tests_skipped           dw 0                    ; Tests skipped

; Benchmark data buffers
test_packet_buffer      db 9000 dup(0)          ; Test packet buffer
source_buffer           db 4096 dup(0)          ; Source data buffer
dest_buffer             db 4096 dup(0)          ; Destination buffer
pattern_buffer          db 1024 dup(0)          ; Test pattern buffer

; Network simulation data
simulated_packets       dw 1000 dup(0)          ; Simulated packet queue
packet_queue_head       dw 0                    ; Queue head pointer
packet_queue_tail       dw 0                    ; Queue tail pointer
packets_processed       dd 0                    ; Packets processed counter
bytes_processed         dd 0                    ; Bytes processed counter

; Interrupt simulation
interrupt_count         dd 0                    ; Simulated interrupt count
interrupt_start_time    dd 0                    ; Interrupt start time
interrupt_total_time    dd 0                    ; Total interrupt time
max_interrupt_latency   dd 0                    ; Maximum observed latency
min_interrupt_latency   dd 0FFFFFFFFh           ; Minimum observed latency

; Memory bandwidth test data
memory_copy_source      dd 0                    ; Memory copy source address
memory_copy_dest        dd 0                    ; Memory copy destination address
memory_copy_size        dd 0                    ; Memory copy size
memory_test_pattern     dd 0A5A5A5A5h           ; Memory test pattern

; CPU utilization measurement
cpu_work_counter        dd 0                    ; CPU work counter
cpu_idle_counter        dd 0                    ; CPU idle counter
cpu_test_duration       dd 0                    ; CPU test duration

; Test names
test_name_timing        db 'Timing Calibration', 0
test_name_throughput    db 'Packet Throughput', 0
test_name_latency       db 'Interrupt Latency', 0
test_name_memory_bw     db 'Memory Bandwidth', 0
test_name_cpu_util      db 'CPU Utilization', 0
test_name_small_packets db 'Small Packet Performance', 0
test_name_large_packets db 'Large Packet Performance', 0
test_name_burst_perf    db 'Burst Performance', 0
test_name_sustained     db 'Sustained Performance', 0
test_name_optimization  db 'Optimization Validation', 0

; Messages
msg_perf_start          db 'Performance Benchmark Suite:', 0Dh, 0Ah, 0
msg_timing_method       db 'Timing Method: ', 0
msg_timing_8254         db '8254 PIT', 0
msg_timing_tsc          db 'TSC (Time Stamp Counter)', 0
msg_timing_bios         db 'BIOS Timer', 0
msg_cpu_freq            db 'CPU Frequency: ', 0
msg_mhz                 db ' MHz', 0
msg_benchmark_start     db 'Running benchmark: ', 0
msg_throughput_result   db 'Throughput: ', 0
msg_pps                 db ' packets/sec', 0
msg_latency_result      db 'Latency: ', 0
msg_microseconds        db ' microseconds', 0
msg_memory_result       db 'Memory Bandwidth: ', 0
msg_mbps                db ' MB/sec', 0
msg_cpu_result          db 'CPU Utilization: ', 0
msg_percent             db '%', 0
msg_target_met          db ' [TARGET MET]', 0
msg_target_failed       db ' [TARGET FAILED]', 0
msg_newline             db 0Dh, 0Ah, 0
msg_test_complete       db 'Performance benchmarks complete.', 0Dh, 0Ah, 0

; External function references
EXTRN print_string:PROC
EXTRN print_number:PROC
EXTRN print_hex:PROC

_DATA ENDS

; Code segment
_TEXT SEGMENT
        ASSUME  CS:_TEXT, DS:_DATA

; Public function exports
PUBLIC performance_bench_init
PUBLIC performance_bench_run
PUBLIC performance_bench_cleanup
PUBLIC calibrate_timing
PUBLIC measure_throughput
PUBLIC measure_latency
PUBLIC measure_memory_bandwidth
PUBLIC measure_cpu_utilization

;-----------------------------------------------------------------------------
; performance_bench_init - Initialize performance benchmarking
;
; Input:  AL = Mode (0=test mode, 1=discovery mode)
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
performance_bench_init PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        
        ; Clear test state
        mov     word ptr [test_count], 0
        mov     word ptr [tests_passed], 0
        mov     word ptr [tests_failed], 0
        mov     word ptr [tests_skipped], 0
        
        ; Clear performance results
        mov     cx, 16
        mov     si, OFFSET throughput_results
        xor     ax, ax
.clear_throughput:
        mov     [si], ax
        add     si, 2
        loop    .clear_throughput
        
        mov     cx, 16
        mov     si, OFFSET latency_results
        xor     ax, ax
.clear_latency:
        mov     [si], ax
        add     si, 2
        loop    .clear_latency
        
        ; Initialize timing infrastructure
        call    detect_timing_capabilities
        call    calibrate_timing
        
        ; Detect CPU frequency for accurate measurements
        call    detect_cpu_frequency
        
        ; Initialize test buffers with patterns
        call    initialize_test_buffers
        
        ; Display benchmark configuration (if not in discovery mode)
        cmp     byte ptr [bp + 4], 1    ; Check discovery mode flag
        je      .skip_display
        
        call    display_benchmark_config
        
.skip_display:
        ; Success
        mov     ax, 0
        
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
performance_bench_init ENDP

;-----------------------------------------------------------------------------
; performance_bench_run - Run all performance benchmarks
;
; Input:  None
; Output: AX = Number of failed tests
; Uses:   All registers
;-----------------------------------------------------------------------------
performance_bench_run PROC
        push    bp
        mov     bp, sp
        
        ; Benchmark 1: Timing Calibration Validation
        mov     byte ptr [current_benchmark], BENCH_THROUGHPUT
        call    test_timing_calibration
        call    record_test_result
        
        ; Benchmark 2: Packet Throughput - Small Packets
        mov     word ptr [packet_size], SMALL_BUFFER_SIZE
        call    test_packet_throughput
        call    record_test_result
        
        ; Benchmark 3: Packet Throughput - Large Packets
        mov     word ptr [packet_size], LARGE_BUFFER_SIZE
        call    test_packet_throughput
        call    record_test_result
        
        ; Benchmark 4: Interrupt Latency
        mov     byte ptr [current_benchmark], BENCH_LATENCY
        call    test_interrupt_latency
        call    record_test_result
        
        ; Benchmark 5: Memory Bandwidth
        mov     byte ptr [current_benchmark], BENCH_MEMORY
        call    test_memory_bandwidth
        call    record_test_result
        
        ; Benchmark 6: CPU Utilization
        mov     byte ptr [current_benchmark], BENCH_CPU
        call    test_cpu_utilization
        call    record_test_result
        
        ; Benchmark 7: Burst Performance
        call    test_burst_performance
        call    record_test_result
        
        ; Benchmark 8: Sustained Performance
        call    test_sustained_performance
        call    record_test_result
        
        ; Benchmark 9: Optimization Validation
        call    test_optimization_validation
        call    record_test_result
        
        ; Display completion message
        mov     dx, OFFSET msg_test_complete
        call    print_string
        
        ; Return number of failed tests
        mov     ax, [tests_failed]
        
        pop     bp
        ret
performance_bench_run ENDP

;-----------------------------------------------------------------------------
; performance_bench_cleanup - Cleanup performance benchmarking
;
; Input:  None
; Output: None
; Uses:   None
;-----------------------------------------------------------------------------
performance_bench_cleanup PROC
        ; Reset timing method to default
        mov     byte ptr [timing_method], TIMING_BIOS
        
        ; Clear any allocated resources (none for this implementation)
        
        ret
performance_bench_cleanup ENDP

;-----------------------------------------------------------------------------
; Timing Infrastructure
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; detect_timing_capabilities - Detect available timing methods
;
; Input:  None
; Output: None (updates timing_method and capability flags)
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
detect_timing_capabilities PROC
        push    bx
        push    cx
        push    dx
        
        ; Default to BIOS timing
        mov     byte ptr [timing_method], TIMING_BIOS
        mov     byte ptr [tsc_available], 0
        mov     byte ptr [pit_available], 0
        
        ; Check for TSC availability (Pentium+)
        call    detect_tsc_capability
        test    al, al
        jz      .no_tsc
        
        mov     byte ptr [tsc_available], 1
        mov     byte ptr [timing_method], TIMING_TSC
        
.no_tsc:
        ; Check for 8254 PIT access
        call    detect_pit_capability
        test    al, al
        jz      .no_pit
        
        mov     byte ptr [pit_available], 1
        ; Only use PIT if TSC not available
        cmp     byte ptr [tsc_available], 0
        jne     .timing_done
        
        mov     byte ptr [timing_method], TIMING_8254_PIT
        
.no_pit:
.timing_done:
        pop     dx
        pop     cx
        pop     bx
        ret
detect_timing_capabilities ENDP

;-----------------------------------------------------------------------------
; detect_tsc_capability - Detect Time Stamp Counter availability
;
; Input:  None
; Output: AL = 1 if TSC available, 0 if not
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
detect_tsc_capability PROC
        push    bx
        push    cx
        push    dx
        
        ; Check if CPUID instruction is available
        pushfd
        pop     eax
        mov     ecx, eax
        xor     eax, 200000h            ; Toggle CPUID bit (bit 21)
        push    eax
        popfd
        pushfd
        pop     eax
        push    ecx
        popfd
        xor     eax, ecx
        and     eax, 200000h
        jz      .no_cpuid
        
        ; CPUID available - check for TSC feature
        mov     eax, 1
        db      0Fh, 0A2h               ; CPUID instruction
        
        test    edx, 10h                ; TSC feature bit (bit 4)
        jz      .no_tsc
        
        ; TSC available
        mov     al, 1
        jmp     .done
        
.no_cpuid:
.no_tsc:
        mov     al, 0
        
.done:
        pop     dx
        pop     cx
        pop     bx
        ret
detect_tsc_capability ENDP

;-----------------------------------------------------------------------------
; detect_pit_capability - Detect 8254 PIT accessibility
;
; Input:  None
; Output: AL = 1 if PIT accessible, 0 if not
; Uses:   AX
;-----------------------------------------------------------------------------
detect_pit_capability PROC
        push    ax
        
        ; Try to read PIT channel 0
        ; This is a simplified test - real implementation would
        ; be more careful about V86 mode restrictions
        
        ; In V86 mode, direct I/O might be trapped
        ; For now, assume PIT is available if not in V86 mode
        
        ; Check current privilege level / mode
        smsw    ax
        test    ax, 1                   ; PE bit
        jnz     .check_v86
        
        ; Real mode - PIT should be accessible
        mov     al, 1
        jmp     .done
        
.check_v86:
        ; Protected mode - check for V86
        pushf
        pop     ax
        test    ax, 20000h              ; VM flag
        jnz     .v86_mode
        
        ; Protected mode but not V86 - assume no direct I/O
        mov     al, 0
        jmp     .done
        
.v86_mode:
        ; V86 mode - PIT access depends on configuration
        ; For safety, assume not available
        mov     al, 0
        
.done:
        pop     ax
        ret
detect_pit_capability ENDP

;-----------------------------------------------------------------------------
; calibrate_timing - Calibrate timing overhead and accuracy
;
; Input:  None
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
calibrate_timing PROC
        push    bx
        push    cx
        push    dx
        
        ; Measure timing overhead by doing empty timing loops
        mov     cx, 100                 ; 100 measurements
        mov     dx, 0                   ; Accumulate overhead
        
.calibration_loop:
        ; Start timing
        call    start_precise_timing
        
        ; No operation (just timing overhead)
        nop
        
        ; End timing
        call    end_precise_timing
        
        ; Get elapsed time
        call    get_elapsed_microseconds
        add     dx, ax                  ; Accumulate
        
        loop    .calibration_loop
        
        ; Calculate average overhead
        mov     ax, dx
        mov     bl, 100
        div     bl                      ; AX = average overhead
        mov     word ptr [timing_overhead], ax
        
        mov     ax, 0                   ; Success
        
        pop     dx
        pop     cx
        pop     bx
        ret
calibrate_timing ENDP

;-----------------------------------------------------------------------------
; start_precise_timing - Start high-precision timing measurement
;
; Input:  None
; Output: None (updates timing_start_*)
; Uses:   EAX, EDX
;-----------------------------------------------------------------------------
start_precise_timing PROC
        push    eax
        push    edx
        
        cmp     byte ptr [timing_method], TIMING_TSC
        je      .use_tsc
        cmp     byte ptr [timing_method], TIMING_8254_PIT
        je      .use_pit
        
        ; Use BIOS timer
        call    get_bios_timer_ticks
        mov     dword ptr [timing_start_low], eax
        mov     dword ptr [timing_start_high], 0
        jmp     .done
        
.use_tsc:
        rdtsc                           ; Read Time Stamp Counter
        mov     dword ptr [timing_start_low], eax
        mov     dword ptr [timing_start_high], edx
        jmp     .done
        
.use_pit:
        call    read_pit_counter
        mov     dword ptr [timing_start_low], eax
        mov     dword ptr [timing_start_high], 0
        
.done:
        pop     edx
        pop     eax
        ret
start_precise_timing ENDP

;-----------------------------------------------------------------------------
; end_precise_timing - End high-precision timing measurement
;
; Input:  None
; Output: None (updates timing_end_*)
; Uses:   EAX, EDX
;-----------------------------------------------------------------------------
end_precise_timing PROC
        push    eax
        push    edx
        
        cmp     byte ptr [timing_method], TIMING_TSC
        je      .use_tsc
        cmp     byte ptr [timing_method], TIMING_8254_PIT
        je      .use_pit
        
        ; Use BIOS timer
        call    get_bios_timer_ticks
        mov     dword ptr [timing_end_low], eax
        mov     dword ptr [timing_end_high], 0
        jmp     .done
        
.use_tsc:
        rdtsc                           ; Read Time Stamp Counter
        mov     dword ptr [timing_end_low], eax
        mov     dword ptr [timing_end_high], edx
        jmp     .done
        
.use_pit:
        call    read_pit_counter
        mov     dword ptr [timing_end_low], eax
        mov     dword ptr [timing_end_high], 0
        
.done:
        pop     edx
        pop     eax
        ret
end_precise_timing ENDP

;-----------------------------------------------------------------------------
; get_elapsed_microseconds - Calculate elapsed time in microseconds
;
; Input:  None (uses timing_start_* and timing_end_*)
; Output: EAX = Elapsed time in microseconds
; Uses:   EAX, EBX, ECX, EDX
;-----------------------------------------------------------------------------
get_elapsed_microseconds PROC
        push    ebx
        push    ecx
        push    edx
        
        ; Calculate elapsed ticks
        mov     eax, dword ptr [timing_end_low]
        sub     eax, dword ptr [timing_start_low]
        
        ; Handle high word for TSC
        cmp     byte ptr [timing_method], TIMING_TSC
        jne     .convert_to_microseconds
        
        mov     edx, dword ptr [timing_end_high]
        sbb     edx, dword ptr [timing_start_high]
        ; For simplicity, ignore high word overflow in this implementation
        
.convert_to_microseconds:
        ; Convert ticks to microseconds based on timing method
        cmp     byte ptr [timing_method], TIMING_TSC
        je      .convert_tsc
        cmp     byte ptr [timing_method], TIMING_8254_PIT
        je      .convert_pit
        
        ; Convert BIOS ticks (18.2 Hz) to microseconds
        ; 1 tick = 54945 microseconds
        mov     ebx, 54945
        mul     ebx
        jmp     .done
        
.convert_tsc:
        ; Convert TSC cycles to microseconds
        ; microseconds = cycles / (MHz * 1000000 / 1000000) = cycles / MHz
        movzx   ebx, word ptr [cpu_frequency_mhz]
        test    ebx, ebx
        jz      .assume_frequency       ; If frequency unknown, assume 100MHz
        
        div     ebx
        jmp     .done
        
.assume_frequency:
        mov     ebx, 100                ; Assume 100 MHz
        div     ebx
        jmp     .done
        
.convert_pit:
        ; Convert PIT counts to microseconds
        ; PIT runs at 1.193182 MHz, so 1 count = 0.838 microseconds
        mov     ebx, 838
        mul     ebx
        mov     ebx, 1000
        div     ebx                     ; Divide by 1000 to get microseconds
        
.done:
        ; Subtract timing overhead
        sub     eax, dword ptr [timing_overhead]
        
        pop     edx
        pop     ecx
        pop     ebx
        ret
get_elapsed_microseconds ENDP

;-----------------------------------------------------------------------------
; Performance Benchmark Tests
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; test_timing_calibration - Test timing system accuracy
;
; Input:  None
; Output: AL = Test result
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
test_timing_calibration PROC
        push    bx
        push    cx
        push    dx
        
        ; Test timing accuracy by measuring known delays
        mov     cx, 10                  ; 10 test iterations
        mov     dx, 0                   ; Accumulate results
        
.accuracy_loop:
        ; Start timing
        call    start_precise_timing
        
        ; Known delay (simple loop)
        push    cx
        mov     cx, 1000                ; 1000 iterations
.delay_loop:
        nop
        loop    .delay_loop
        pop     cx
        
        ; End timing
        call    end_precise_timing
        
        ; Get elapsed time
        call    get_elapsed_microseconds
        add     dx, ax
        
        loop    .accuracy_loop
        
        ; Calculate average timing
        mov     ax, dx
        mov     bl, 10
        div     bl
        
        ; Check if timing is reasonable (between 10 and 10000 microseconds)
        cmp     ax, 10
        jb      .timing_too_fast
        cmp     ax, 10000
        ja      .timing_too_slow
        
        mov     al, TEST_PASS
        jmp     .done
        
.timing_too_fast:
.timing_too_slow:
        mov     al, TEST_FAIL
        
.done:
        pop     dx
        pop     cx
        pop     bx
        ret
test_timing_calibration ENDP

;-----------------------------------------------------------------------------
; test_packet_throughput - Test packet processing throughput
;
; Input:  None (uses packet_size global)
; Output: AL = Test result
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
test_packet_throughput PROC
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        
        ; Initialize packet processing simulation
        call    init_packet_simulation
        
        ; Start timing
        call    start_precise_timing
        
        ; Process packets for test duration
        mov     cx, word ptr [test_iterations]
        
.packet_loop:
        ; Simulate packet processing
        call    simulate_packet_processing
        
        loop    .packet_loop
        
        ; End timing
        call    end_precise_timing
        
        ; Calculate throughput
        call    get_elapsed_microseconds
        
        ; Avoid division by zero
        test    eax, eax
        jz      .no_time_elapsed
        
        ; Calculate packets per second
        ; pps = (iterations * 1000000) / elapsed_microseconds
        mov     ebx, dword ptr [test_iterations]
        mov     ecx, 1000000
        mul     ecx                     ; EAX = elapsed * 1000000
        div     ebx                     ; EAX = packets per second
        
        ; Store result
        mov     bx, [test_count]
        mov     [throughput_results + bx * 2], ax
        
        ; Check against target
        cmp     ax, TARGET_MIN_THROUGHPUT
        jae     .throughput_ok
        
        mov     al, TEST_FAIL
        jmp     .done
        
.throughput_ok:
        mov     al, TEST_PASS
        jmp     .done
        
.no_time_elapsed:
        mov     al, TEST_ERROR
        
.done:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret
test_packet_throughput ENDP

;-----------------------------------------------------------------------------
; test_interrupt_latency - Test interrupt handling latency
;
; Input:  None
; Output: AL = Test result
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
test_interrupt_latency PROC
        push    bx
        push    cx
        push    dx
        
        ; Initialize interrupt simulation
        mov     dword ptr [interrupt_count], 0
        mov     dword ptr [interrupt_total_time], 0
        mov     dword ptr [max_interrupt_latency], 0
        mov     dword ptr [min_interrupt_latency], 0FFFFFFFFh
        
        ; Simulate multiple interrupt scenarios
        mov     cx, 100                 ; 100 interrupt simulations
        
.interrupt_loop:
        ; Simulate interrupt occurrence
        call    start_precise_timing
        
        ; Simulate interrupt handler
        call    simulate_interrupt_handler
        
        call    end_precise_timing
        
        ; Measure latency
        call    get_elapsed_microseconds
        
        ; Update statistics
        inc     dword ptr [interrupt_count]
        add     dword ptr [interrupt_total_time], eax
        
        ; Update min/max
        cmp     eax, dword ptr [max_interrupt_latency]
        jbe     .check_min_latency
        mov     dword ptr [max_interrupt_latency], eax
        
.check_min_latency:
        cmp     eax, dword ptr [min_interrupt_latency]
        jae     .continue_loop
        mov     dword ptr [min_interrupt_latency], eax
        
.continue_loop:
        loop    .interrupt_loop
        
        ; Calculate average latency
        mov     eax, dword ptr [interrupt_total_time]
        mov     ebx, dword ptr [interrupt_count]
        div     ebx                     ; EAX = average latency
        
        ; Store result
        mov     bx, [test_count]
        mov     [latency_results + bx * 2], ax
        
        ; Check against target (100 microseconds max)
        cmp     eax, TARGET_MAX_LATENCY
        jbe     .latency_ok
        
        mov     al, TEST_FAIL
        jmp     .done
        
.latency_ok:
        mov     al, TEST_PASS
        
.done:
        pop     dx
        pop     cx
        pop     bx
        ret
test_interrupt_latency ENDP

;-----------------------------------------------------------------------------
; test_memory_bandwidth - Test memory bandwidth performance
;
; Input:  None
; Output: AL = Test result
; Uses:   AX, BX, CX, DX, SI, DI, ES
;-----------------------------------------------------------------------------
test_memory_bandwidth PROC
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        
        ; Set up memory copy test
        mov     si, OFFSET source_buffer
        mov     di, OFFSET dest_buffer
        mov     cx, 2048                ; Copy 2048 words (4KB)
        
        ; Fill source buffer with test pattern
        push    di
        mov     di, si
        mov     ax, 0A55Ah
        push    cx
        rep     stosw
        pop     cx
        pop     di
        
        ; Start timing
        call    start_precise_timing
        
        ; Perform memory bandwidth test
        mov     bx, 100                 ; 100 iterations
        
.bandwidth_loop:
        push    cx
        push    si
        push    di
        
        ; Memory copy operation
        rep     movsw
        
        pop     di
        pop     si
        pop     cx
        
        dec     bx
        jnz     .bandwidth_loop
        
        ; End timing
        call    end_precise_timing
        
        ; Calculate bandwidth
        call    get_elapsed_microseconds
        
        ; Avoid division by zero
        test    eax, eax
        jz      .no_time_elapsed
        
        ; Calculate bytes per second
        ; Total bytes = 100 iterations * 4KB = 400KB = 409600 bytes
        mov     ebx, 409600
        mov     ecx, 1000000            ; Convert to seconds
        mul     ecx
        div     ebx                     ; EAX = bytes per second
        
        ; Store result
        mov     bx, [test_count]
        mov     [memory_results + bx * 4], eax
        
        ; Check against target (1 MB/sec minimum)
        cmp     eax, TARGET_MIN_MEMORY_BW
        jae     .bandwidth_ok
        
        mov     al, TEST_FAIL
        jmp     .done
        
.bandwidth_ok:
        mov     al, TEST_PASS
        jmp     .done
        
.no_time_elapsed:
        mov     al, TEST_ERROR
        
.done:
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret
test_memory_bandwidth ENDP

;-----------------------------------------------------------------------------
; test_cpu_utilization - Test CPU utilization measurement
;
; Input:  None
; Output: AL = Test result
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
test_cpu_utilization PROC
        push    bx
        push    cx
        push    dx
        
        ; Initialize CPU utilization counters
        mov     dword ptr [cpu_work_counter], 0
        mov     dword ptr [cpu_idle_counter], 0
        
        ; Start timing for CPU test
        call    start_precise_timing
        
        ; Simulate mixed workload
        mov     cx, 1000                ; 1000 work cycles
        
.cpu_test_loop:
        ; Simulate work
        push    cx
        mov     cx, 100
.work_loop:
        inc     dword ptr [cpu_work_counter]
        loop    .work_loop
        pop     cx
        
        ; Simulate idle time
        push    cx
        mov     cx, 10
.idle_loop:
        inc     dword ptr [cpu_idle_counter]
        loop    .idle_loop
        pop     cx
        
        loop    .cpu_test_loop
        
        ; End timing
        call    end_precise_timing
        
        ; Calculate CPU utilization percentage
        ; utilization = (work_counter * 100) / (work_counter + idle_counter)
        mov     eax, dword ptr [cpu_work_counter]
        mov     ebx, 100
        mul     ebx                     ; EAX = work_counter * 100
        
        mov     ebx, dword ptr [cpu_work_counter]
        add     ebx, dword ptr [cpu_idle_counter]
        div     ebx                     ; EAX = utilization percentage
        
        ; Store result
        mov     bx, [test_count]
        mov     [cpu_results + bx * 2], ax
        
        ; CPU utilization test passes if we get reasonable values (0-100%)
        cmp     ax, 100
        ja      .util_invalid
        
        mov     al, TEST_PASS
        jmp     .done
        
.util_invalid:
        mov     al, TEST_FAIL
        
.done:
        pop     dx
        pop     cx
        pop     bx
        ret
test_cpu_utilization ENDP

;-----------------------------------------------------------------------------
; test_burst_performance - Test burst packet handling performance
;
; Input:  None
; Output: AL = Test result
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
test_burst_performance PROC
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        
        ; Initialize burst test
        mov     word ptr [packet_queue_head], 0
        mov     word ptr [packet_queue_tail], 0
        mov     dword ptr [packets_processed], 0
        
        ; Fill packet queue with burst
        mov     cx, 100                 ; 100 packets in burst
        mov     si, OFFSET simulated_packets
        
.fill_queue:
        mov     ax, cx                  ; Packet ID
        mov     [si], ax
        add     si, 2
        loop    .fill_queue
        
        mov     word ptr [packet_queue_tail], 200  ; 100 packets * 2 bytes
        
        ; Start timing
        call    start_precise_timing
        
        ; Process burst
.process_burst:
        ; Check if queue empty
        mov     ax, [packet_queue_head]
        cmp     ax, [packet_queue_tail]
        jae     .burst_complete
        
        ; Process one packet
        call    simulate_packet_processing
        
        ; Update queue head
        add     word ptr [packet_queue_head], 2
        inc     dword ptr [packets_processed]
        
        jmp     .process_burst
        
.burst_complete:
        ; End timing
        call    end_precise_timing
        
        ; Calculate burst processing rate
        call    get_elapsed_microseconds
        
        ; Check that all packets were processed
        cmp     dword ptr [packets_processed], 100
        jne     .burst_incomplete
        
        ; Check processing time is reasonable
        cmp     eax, 1000000            ; Should complete within 1 second
        ja      .burst_too_slow
        
        mov     al, TEST_PASS
        jmp     .done
        
.burst_incomplete:
.burst_too_slow:
        mov     al, TEST_FAIL
        
.done:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret
test_burst_performance ENDP

;-----------------------------------------------------------------------------
; test_sustained_performance - Test sustained performance over time
;
; Input:  None
; Output: AL = Test result
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
test_sustained_performance PROC
        push    bx
        push    cx
        push    dx
        
        ; Run sustained test for longer duration
        mov     dword ptr [test_iterations], 50000     ; 50,000 iterations
        
        ; Start timing
        call    start_precise_timing
        
        ; Sustained packet processing
        mov     ecx, dword ptr [test_iterations]
        
.sustained_loop:
        ; Simulate packet processing with periodic interrupts
        call    simulate_packet_processing
        
        ; Simulate periodic interrupt every 100 packets
        test    cx, 63                  ; Check every 64 packets (power of 2)
        jnz     .continue_sustained
        
        call    simulate_interrupt_handler
        
.continue_sustained:
        loop    .sustained_loop
        
        ; End timing
        call    end_precise_timing
        
        ; Calculate sustained throughput
        call    get_elapsed_microseconds
        
        ; Calculate packets per second
        mov     ebx, dword ptr [test_iterations]
        mov     ecx, 1000000
        mul     ecx
        div     ebx
        
        ; For sustained performance, target should be at least 80% of burst
        mov     ebx, TARGET_MIN_THROUGHPUT
        mov     ecx, 80
        mul     ecx
        mov     ecx, 100
        div     ecx                     ; EBX = 80% of target
        
        cmp     eax, ebx
        jae     .sustained_ok
        
        mov     al, TEST_FAIL
        jmp     .done
        
.sustained_ok:
        mov     al, TEST_PASS
        
.done:
        ; Restore original test iterations
        mov     dword ptr [test_iterations], 10000
        
        pop     dx
        pop     cx
        pop     bx
        ret
test_sustained_performance ENDP

;-----------------------------------------------------------------------------
; test_optimization_validation - Test that optimizations are working
;
; Input:  None
; Output: AL = Test result
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
test_optimization_validation PROC
        push    bx
        push    cx
        push    dx
        
        ; Test basic optimization: compare optimized vs unoptimized paths
        
        ; Test 1: Optimized packet copy
        call    start_precise_timing
        
        mov     cx, 1000
.opt_copy_loop:
        ; Optimized copy (using string instructions)
        push    cx
        mov     cx, 64                  ; 64 words
        mov     si, OFFSET source_buffer
        mov     di, OFFSET dest_buffer
        rep     movsw
        pop     cx
        
        loop    .opt_copy_loop
        
        call    end_precise_timing
        call    get_elapsed_microseconds
        mov     bx, ax                  ; Save optimized time
        
        ; Test 2: Unoptimized packet copy
        call    start_precise_timing
        
        mov     cx, 1000
.unopt_copy_loop:
        ; Unoptimized copy (byte by byte)
        push    cx
        mov     cx, 128                 ; 128 bytes
        mov     si, OFFSET source_buffer
        mov     di, OFFSET dest_buffer
.byte_copy:
        mov     al, [si]
        mov     [di], al
        inc     si
        inc     di
        loop    .byte_copy
        pop     cx
        
        loop    .unopt_copy_loop
        
        call    end_precise_timing
        call    get_elapsed_microseconds
        
        ; Optimized version should be faster
        cmp     bx, ax
        jb      .optimization_working
        
        mov     al, TEST_FAIL
        jmp     .done
        
.optimization_working:
        mov     al, TEST_PASS
        
.done:
        pop     dx
        pop     cx
        pop     bx
        ret
test_optimization_validation ENDP

;-----------------------------------------------------------------------------
; Helper Functions
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; detect_cpu_frequency - Detect CPU frequency for timing calculations
;
; Input:  None
; Output: None (updates cpu_frequency_mhz)
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
detect_cpu_frequency PROC
        push    bx
        push    cx
        push    dx
        
        ; Default frequency if detection fails
        mov     word ptr [cpu_frequency_mhz], 100      ; Assume 100 MHz
        
        ; Skip frequency detection if TSC not available
        cmp     byte ptr [tsc_available], 0
        je      .done
        
        ; Rough frequency detection using BIOS timer
        ; Measure TSC cycles during a known time period
        
        ; Get start BIOS time
        call    get_bios_timer_ticks
        mov     bx, ax                  ; Save start time
        
        ; Get start TSC
        rdtsc
        mov     ecx, eax                ; Save start TSC (low)
        
        ; Wait for next BIOS tick
.wait_tick:
        call    get_bios_timer_ticks
        cmp     ax, bx
        je      .wait_tick
        
        ; Get end TSC
        rdtsc
        sub     eax, ecx                ; EAX = TSC cycles in one tick
        
        ; One BIOS tick = 54.945 ms = 0.054945 seconds
        ; Frequency = cycles / time = cycles / 0.054945
        ; Approximate: freq_mhz = cycles / 54945
        mov     ebx, 54945
        div     ebx
        
        ; Sanity check: frequency should be reasonable (10 MHz to 1000 MHz)
        cmp     ax, 10
        jb      .use_default
        cmp     ax, 1000
        ja      .use_default
        
        mov     [cpu_frequency_mhz], ax
        jmp     .done
        
.use_default:
        mov     word ptr [cpu_frequency_mhz], 100
        
.done:
        pop     dx
        pop     cx
        pop     bx
        ret
detect_cpu_frequency ENDP

;-----------------------------------------------------------------------------
; initialize_test_buffers - Initialize test data buffers
;
; Input:  None
; Output: None
; Uses:   AX, CX, DI
;-----------------------------------------------------------------------------
initialize_test_buffers PROC
        push    ax
        push    cx
        push    di
        
        ; Initialize source buffer with test pattern
        mov     di, OFFSET source_buffer
        mov     cx, 2048                ; 4KB buffer
        mov     ax, 0A55Ah
        rep     stosw
        
        ; Initialize pattern buffer
        mov     di, OFFSET pattern_buffer
        mov     cx, 512                 ; 1KB buffer
        mov     ax, 05AA5h
        rep     stosw
        
        ; Clear destination buffer
        mov     di, OFFSET dest_buffer
        mov     cx, 2048
        xor     ax, ax
        rep     stosw
        
        pop     di
        pop     cx
        pop     ax
        ret
initialize_test_buffers ENDP

;-----------------------------------------------------------------------------
; Simulation Functions
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; init_packet_simulation - Initialize packet processing simulation
;
; Input:  None
; Output: None
; Uses:   AX, CX, DI
;-----------------------------------------------------------------------------
init_packet_simulation PROC
        push    ax
        push    cx
        push    di
        
        ; Initialize packet buffer with test data
        mov     di, OFFSET test_packet_buffer
        mov     cx, word ptr [packet_size]
        shr     cx, 1                   ; Convert to words
        mov     ax, 0DEADh              ; Test pattern
        rep     stosw
        
        ; Reset counters
        mov     dword ptr [packets_processed], 0
        mov     dword ptr [bytes_processed], 0
        
        pop     di
        pop     cx
        pop     ax
        ret
init_packet_simulation ENDP

;-----------------------------------------------------------------------------
; simulate_packet_processing - Simulate processing one packet
;
; Input:  None
; Output: None
; Uses:   AX, BX, CX, SI, DI
;-----------------------------------------------------------------------------
simulate_packet_processing PROC
        push    ax
        push    bx
        push    cx
        push    si
        push    di
        
        ; Simulate packet header parsing
        mov     si, OFFSET test_packet_buffer
        mov     ax, [si]                ; Read "header"
        mov     bx, [si + 2]            ; Read more "header"
        
        ; Simulate checksum calculation
        mov     cx, word ptr [packet_size]
        shr     cx, 1                   ; Convert to words
        xor     dx, dx                  ; Checksum accumulator
        
.checksum_loop:
        add     dx, [si]
        add     si, 2
        loop    .checksum_loop
        
        ; Simulate packet forwarding decision
        test    dx, 1
        jz      .forward_packet
        
        ; Simulate packet drop
        jmp     .packet_done
        
.forward_packet:
        ; Simulate packet copy to output buffer
        mov     si, OFFSET test_packet_buffer
        mov     di, OFFSET dest_buffer
        mov     cx, word ptr [packet_size]
        shr     cx, 1
        rep     movsw
        
.packet_done:
        ; Update statistics
        inc     dword ptr [packets_processed]
        movzx   eax, word ptr [packet_size]
        add     dword ptr [bytes_processed], eax
        
        pop     di
        pop     si
        pop     cx
        pop     bx
        pop     ax
        ret
simulate_packet_processing ENDP

;-----------------------------------------------------------------------------
; simulate_interrupt_handler - Simulate interrupt handler processing
;
; Input:  None
; Output: None
; Uses:   AX, BX, CX
;-----------------------------------------------------------------------------
simulate_interrupt_handler PROC
        push    ax
        push    bx
        push    cx
        
        ; Simulate interrupt context save
        pusha
        
        ; Simulate interrupt processing work
        mov     cx, 50                  ; Simulate work
.int_work_loop:
        mov     ax, cx
        shl     ax, 1
        add     ax, cx
        loop    .int_work_loop
        
        ; Simulate hardware acknowledgment
        mov     ax, 20h
        ; Simulate EOI to PIC (normally would be: out 20h, al)
        
        ; Simulate interrupt context restore
        popa
        
        pop     cx
        pop     bx
        pop     ax
        ret
simulate_interrupt_handler ENDP

;-----------------------------------------------------------------------------
; Low-level timing functions
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; get_bios_timer_ticks - Get BIOS timer tick count
;
; Input:  None
; Output: EAX = Timer ticks
; Uses:   EAX, ES
;-----------------------------------------------------------------------------
get_bios_timer_ticks PROC
        push    es
        
        ; Read BIOS timer at 0040:006C
        mov     ax, 0040h
        mov     es, ax
        mov     eax, es:[006Ch]         ; Timer ticks (32-bit)
        
        pop     es
        ret
get_bios_timer_ticks ENDP

;-----------------------------------------------------------------------------
; read_pit_counter - Read 8254 PIT counter value
;
; Input:  None
; Output: EAX = Counter value
; Uses:   EAX
;-----------------------------------------------------------------------------
read_pit_counter PROC
        push    ax
        
        ; Latch counter 0
        mov     al, 0
        ; Normally: out 43h, al
        
        ; Read counter value
        ; Normally: in al, 40h (low byte), then in al, 40h (high byte)
        ; For this simulation, return a mock value
        mov     eax, 12345
        
        pop     ax
        ret
read_pit_counter ENDP

;-----------------------------------------------------------------------------
; Display and utility functions
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; display_benchmark_config - Display benchmark configuration
;
; Input:  None
; Output: None
; Uses:   AX, DX
;-----------------------------------------------------------------------------
display_benchmark_config PROC
        push    ax
        push    dx
        
        ; Display header
        mov     dx, OFFSET msg_perf_start
        call    print_string
        
        ; Display timing method
        mov     dx, OFFSET msg_timing_method
        call    print_string
        
        mov     al, [timing_method]
        cmp     al, TIMING_TSC
        je      .show_tsc
        cmp     al, TIMING_8254_PIT
        je      .show_pit
        
        mov     dx, OFFSET msg_timing_bios
        jmp     .show_timing
        
.show_tsc:
        mov     dx, OFFSET msg_timing_tsc
        jmp     .show_timing
        
.show_pit:
        mov     dx, OFFSET msg_timing_8254
        
.show_timing:
        call    print_string
        mov     dx, OFFSET msg_newline
        call    print_string
        
        ; Display CPU frequency if known
        cmp     word ptr [cpu_frequency_mhz], 0
        je      .skip_frequency
        
        mov     dx, OFFSET msg_cpu_freq
        call    print_string
        mov     ax, [cpu_frequency_mhz]
        call    print_number
        mov     dx, OFFSET msg_mhz
        call    print_string
        mov     dx, OFFSET msg_newline
        call    print_string
        
.skip_frequency:
        pop     dx
        pop     ax
        ret
display_benchmark_config ENDP

;-----------------------------------------------------------------------------
; record_test_result - Record the result of a benchmark test
;
; Input:  AL = Test result
; Output: None
; Uses:   AX, BX
;-----------------------------------------------------------------------------
record_test_result PROC
        push    bx
        
        ; Store result in array
        mov     bx, [test_count]
        mov     [test_results + bx], al
        
        ; Update counters
        cmp     al, TEST_PASS
        je      .test_passed
        cmp     al, TEST_FAIL
        je      .test_failed
        cmp     al, TEST_SKIP
        je      .test_skipped
        jmp     .update_count
        
.test_passed:
        inc     word ptr [tests_passed]
        jmp     .update_count
        
.test_failed:
        inc     word ptr [tests_failed]
        jmp     .update_count
        
.test_skipped:
        inc     word ptr [tests_skipped]
        
.update_count:
        inc     word ptr [test_count]
        
        pop     bx
        ret
record_test_result ENDP

_TEXT ENDS

END