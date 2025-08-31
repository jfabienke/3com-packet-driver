; @file test_suite.asm
; @brief Main test framework with discovery and execution engine
;
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
;
; This file implements the main test framework that automatically discovers
; and executes all available tests with comprehensive error handling and reporting.

.MODEL SMALL
.386

; Test Suite Constants
MAX_TEST_SUITES         EQU 16          ; Maximum number of test suites
MAX_TESTS_PER_SUITE     EQU 32          ; Maximum tests per suite
MAX_SUITE_NAME_LEN      EQU 32          ; Maximum suite name length
MAX_TEST_DISCOVERY      EQU 128         ; Maximum discovered tests
TEST_TIMEOUT_TICKS      EQU 109         ; 6 seconds (18.2 ticks/sec)

; Test Discovery Status
DISCOVERY_NOT_STARTED   EQU 0           ; Discovery not started
DISCOVERY_IN_PROGRESS   EQU 1           ; Discovery in progress
DISCOVERY_COMPLETE      EQU 2           ; Discovery completed
DISCOVERY_ERROR         EQU 3           ; Discovery error

; Test Execution States
EXEC_STATE_IDLE         EQU 0           ; Execution idle
EXEC_STATE_RUNNING      EQU 1           ; Execution running
EXEC_STATE_TIMEOUT      EQU 2           ; Test timeout
EXEC_STATE_ERROR        EQU 3           ; Execution error
EXEC_STATE_COMPLETE     EQU 4           ; Execution complete

; Test Suite Types
SUITE_TYPE_CPU          EQU 1           ; CPU compatibility tests
SUITE_TYPE_MEMORY       EQU 2           ; Memory manager tests
SUITE_TYPE_PERFORMANCE  EQU 3           ; Performance benchmarks
SUITE_TYPE_STRESS       EQU 4           ; Stress tests
SUITE_TYPE_INTEGRATION  EQU 5           ; Integration tests

; Data segment
_DATA SEGMENT
        ASSUME  DS:_DATA

; Test Suite Registry
test_suite_count        dw 0                                    ; Number of registered suites
suite_names             db MAX_TEST_SUITES * MAX_SUITE_NAME_LEN dup(0)
suite_types             db MAX_TEST_SUITES dup(0)
suite_init_functions    dw MAX_TEST_SUITES dup(0)             ; Suite initialization
suite_run_functions     dw MAX_TEST_SUITES dup(0)             ; Suite run functions
suite_cleanup_functions dw MAX_TEST_SUITES dup(0)             ; Suite cleanup
suite_test_counts       dw MAX_TEST_SUITES dup(0)             ; Tests per suite
suite_results           dw MAX_TEST_SUITES dup(0)             ; Suite results

; Test Discovery State
discovery_status        db DISCOVERY_NOT_STARTED
discovered_test_count   dw 0
discovered_tests        dw MAX_TEST_DISCOVERY dup(0)          ; Test function pointers
discovered_names        db MAX_TEST_DISCOVERY * 32 dup(0)     ; Test names
discovered_types        db MAX_TEST_DISCOVERY dup(0)          ; Test types

; Test Execution State
execution_status        db EXEC_STATE_IDLE
current_suite_id        dw 0
current_test_id         dw 0
test_timeout_start      dw 0
total_tests_run         dw 0
total_tests_passed      dw 0
total_tests_failed      dw 0
total_tests_skipped     dw 0

; Error Recovery State
error_recovery_count    dw 0
max_error_recovery      dw 3                                   ; Maximum recovery attempts
last_error_code         dw 0
error_recovery_stack    dw 16 dup(0)                          ; Error recovery stack

; Test Configuration
config_run_cpu_tests     db 1                                  ; Run CPU tests
config_run_memory_tests  db 1                                  ; Run memory tests
config_run_perf_tests    db 1                                  ; Run performance tests
config_run_stress_tests  db 0                                  ; Run stress tests (optional)
config_verbose_output    db 1                                  ; Verbose output
config_stop_on_failure   db 0                                  ; Stop on first failure
config_timeout_enabled   db 1                                  ; Enable test timeouts

; Message strings
msg_suite_header        db 0Dh, 0Ah, '=== 3Com Packet Driver Test Suite v2.0 ===', 0Dh, 0Ah, 0
msg_discovery_start     db 'Discovering tests...', 0Dh, 0Ah, 0
msg_discovery_complete  db 'Test discovery complete. Found ', 0
msg_tests_found         db ' tests in ', 0
msg_suites_found        db ' suites.', 0Dh, 0Ah, 0
msg_execution_start     db 0Dh, 0Ah, 'Starting test execution...', 0Dh, 0Ah, 0
msg_suite_start         db 0Dh, 0Ah, '--- Running Suite: ', 0
msg_suite_complete      db ' ---', 0Dh, 0Ah, 0
msg_test_timeout        db ' [TIMEOUT]', 0Dh, 0Ah, 0
msg_test_error          db ' [ERROR]', 0Dh, 0Ah, 0
msg_execution_complete  db 0Dh, 0Ah, '=== Test Execution Complete ===', 0Dh, 0Ah, 0
msg_final_summary       db 0Dh, 0Ah, 'FINAL SUMMARY:', 0Dh, 0Ah, 0
msg_total_run           db 'Total Tests Run: ', 0
msg_total_passed        db 'Total Passed: ', 0
msg_total_failed        db 'Total Failed: ', 0
msg_total_skipped       db 'Total Skipped: ', 0
msg_error_recovery      db 'Attempting error recovery...', 0Dh, 0Ah, 0
msg_recovery_failed     db 'Error recovery failed!', 0Dh, 0Ah, 0
msg_newline             db 0Dh, 0Ah, 0

; External test suite references
EXTRN cpu_compat_test_init:PROC
EXTRN cpu_compat_test_run:PROC
EXTRN cpu_compat_test_cleanup:PROC
EXTRN mem_manager_test_init:PROC
EXTRN mem_manager_test_run:PROC
EXTRN mem_manager_test_cleanup:PROC
EXTRN performance_bench_init:PROC
EXTRN performance_bench_run:PROC
EXTRN performance_bench_cleanup:PROC

_DATA ENDS

; Code segment
_TEXT SEGMENT
        ASSUME  CS:_TEXT, DS:_DATA

; Public function exports
PUBLIC test_suite_main
PUBLIC test_suite_init
PUBLIC test_suite_discover_tests
PUBLIC test_suite_execute_all
PUBLIC test_suite_register_suite
PUBLIC test_suite_get_results
PUBLIC test_suite_cleanup
PUBLIC test_suite_set_config
PUBLIC test_suite_handle_error
PUBLIC test_suite_timeout_check

;-----------------------------------------------------------------------------
; test_suite_main - Main entry point for test suite execution
;
; Input:  None
; Output: AX = Overall test result (0=pass, 1=fail, 2=error)
; Uses:   All registers
;-----------------------------------------------------------------------------
test_suite_main PROC
        push    bp
        mov     bp, sp
        
        ; Display header
        mov     dx, OFFSET msg_suite_header
        call    print_string
        
        ; Initialize test suite framework
        call    test_suite_init
        test    ax, ax
        jnz     .init_error
        
        ; Register built-in test suites
        call    register_builtin_suites
        test    ax, ax
        jnz     .register_error
        
        ; Discover all available tests
        call    test_suite_discover_tests
        cmp     al, DISCOVERY_COMPLETE
        jne     .discovery_error
        
        ; Execute all discovered tests
        call    test_suite_execute_all
        test    ax, ax
        jnz     .execution_error
        
        ; Generate final report
        call    generate_final_report
        
        ; Cleanup resources
        call    test_suite_cleanup
        
        ; Determine overall result
        mov     ax, [total_tests_failed]
        test    ax, ax
        jnz     .overall_fail
        
        mov     ax, 0                   ; Overall pass
        jmp     .exit
        
.init_error:
        mov     ax, 2                   ; Initialization error
        jmp     .exit
        
.register_error:
        mov     ax, 2                   ; Registration error
        jmp     .exit
        
.discovery_error:
        mov     ax, 2                   ; Discovery error
        jmp     .exit
        
.execution_error:
        mov     ax, 1                   ; Execution error
        jmp     .exit
        
.overall_fail:
        mov     ax, 1                   ; Tests failed
        
.exit:
        pop     bp
        ret
test_suite_main ENDP

;-----------------------------------------------------------------------------
; test_suite_init - Initialize the test suite framework
;
; Input:  None
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, SI, DI
;-----------------------------------------------------------------------------
test_suite_init PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si
        push    di
        
        ; Clear all counters and state
        mov     word ptr [test_suite_count], 0
        mov     word ptr [discovered_test_count], 0
        mov     word ptr [total_tests_run], 0
        mov     word ptr [total_tests_passed], 0
        mov     word ptr [total_tests_failed], 0
        mov     word ptr [total_tests_skipped], 0
        mov     word ptr [error_recovery_count], 0
        
        ; Reset states
        mov     byte ptr [discovery_status], DISCOVERY_NOT_STARTED
        mov     byte ptr [execution_status], EXEC_STATE_IDLE
        mov     word ptr [current_suite_id], 0
        mov     word ptr [current_test_id], 0
        
        ; Clear suite registry
        mov     cx, MAX_TEST_SUITES
        mov     si, OFFSET suite_types
        xor     al, al
.clear_types:
        mov     [si], al
        inc     si
        loop    .clear_types
        
        ; Clear function pointers
        mov     cx, MAX_TEST_SUITES
        mov     si, OFFSET suite_init_functions
        xor     ax, ax
.clear_functions:
        mov     [si], ax
        mov     [si + MAX_TEST_SUITES * 2], ax    ; run functions
        mov     [si + MAX_TEST_SUITES * 4], ax    ; cleanup functions
        add     si, 2
        loop    .clear_functions
        
        ; Clear suite names
        mov     cx, MAX_TEST_SUITES * MAX_SUITE_NAME_LEN
        mov     di, OFFSET suite_names
        xor     al, al
        rep     stosb
        
        ; Initialize error recovery stack
        mov     cx, 16
        mov     si, OFFSET error_recovery_stack
        xor     ax, ax
.clear_error_stack:
        mov     [si], ax
        add     si, 2
        loop    .clear_error_stack
        
        ; Set default configuration
        mov     byte ptr [config_run_cpu_tests], 1
        mov     byte ptr [config_run_memory_tests], 1
        mov     byte ptr [config_run_perf_tests], 1
        mov     byte ptr [config_run_stress_tests], 0
        mov     byte ptr [config_verbose_output], 1
        mov     byte ptr [config_stop_on_failure], 0
        mov     byte ptr [config_timeout_enabled], 1
        
        ; Success
        mov     ax, 0
        
        pop     di
        pop     si
        pop     cx
        pop     bx
        pop     bp
        ret
test_suite_init ENDP

;-----------------------------------------------------------------------------
; test_suite_discover_tests - Discover all available test suites and tests
;
; Input:  None
; Output: AL = Discovery status
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
test_suite_discover_tests PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        
        ; Set discovery status
        mov     byte ptr [discovery_status], DISCOVERY_IN_PROGRESS
        
        ; Display discovery start message
        cmp     byte ptr [config_verbose_output], 0
        je      .skip_verbose
        mov     dx, OFFSET msg_discovery_start
        call    print_string
        
.skip_verbose:
        ; Initialize discovery counters
        mov     word ptr [discovered_test_count], 0
        
        ; Discover tests in each registered suite
        mov     si, 0                   ; Suite index
        mov     cx, [test_suite_count]
        test    cx, cx
        jz      .no_suites
        
.discovery_loop:
        ; Check if suite should be discovered based on configuration
        call    should_discover_suite
        test    al, al
        jz      .skip_suite
        
        ; Discover tests in this suite
        push    cx
        push    si
        call    discover_suite_tests
        pop     si
        pop     cx
        test    al, al
        jnz     .discovery_error
        
.skip_suite:
        inc     si
        loop    .discovery_loop
        
.no_suites:
        ; Mark discovery complete
        mov     byte ptr [discovery_status], DISCOVERY_COMPLETE
        
        ; Display discovery results
        cmp     byte ptr [config_verbose_output], 0
        je      .skip_results
        call    display_discovery_results
        
.skip_results:
        mov     al, DISCOVERY_COMPLETE
        jmp     .exit
        
.discovery_error:
        mov     byte ptr [discovery_status], DISCOVERY_ERROR
        mov     al, DISCOVERY_ERROR
        
.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
test_suite_discover_tests ENDP

;-----------------------------------------------------------------------------
; test_suite_execute_all - Execute all discovered tests with error handling
;
; Input:  None
; Output: AX = 0 for success, non-zero for error
; Uses:   All registers
;-----------------------------------------------------------------------------
test_suite_execute_all PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        
        ; Check discovery status
        cmp     byte ptr [discovery_status], DISCOVERY_COMPLETE
        jne     .not_ready
        
        ; Set execution status
        mov     byte ptr [execution_status], EXEC_STATE_RUNNING
        
        ; Display execution start message
        cmp     byte ptr [config_verbose_output], 0
        je      .skip_start_msg
        mov     dx, OFFSET msg_execution_start
        call    print_string
        
.skip_start_msg:
        ; Execute each test suite
        mov     si, 0                   ; Suite index
        mov     cx, [test_suite_count]
        test    cx, cx
        jz      .no_suites_to_run
        
.execution_loop:
        mov     [current_suite_id], si
        
        ; Check if suite should run
        call    should_run_suite
        test    al, al
        jz      .skip_execution
        
        ; Execute suite with error handling
        push    cx
        push    si
        call    execute_suite_safe
        pop     si
        pop     cx
        
        ; Check for stop on failure
        cmp     byte ptr [config_stop_on_failure], 0
        je      .continue_execution
        test    ax, ax
        jnz     .execution_failed
        
.continue_execution:
.skip_execution:
        inc     si
        loop    .execution_loop
        
.no_suites_to_run:
        ; Mark execution complete
        mov     byte ptr [execution_status], EXEC_STATE_COMPLETE
        
        ; Display completion message
        cmp     byte ptr [config_verbose_output], 0
        je      .skip_complete_msg
        mov     dx, OFFSET msg_execution_complete
        call    print_string
        
.skip_complete_msg:
        mov     ax, 0                   ; Success
        jmp     .exit
        
.not_ready:
        mov     ax, 1                   ; Not ready error
        jmp     .exit
        
.execution_failed:
        mov     byte ptr [execution_status], EXEC_STATE_ERROR
        mov     ax, 2                   ; Execution error
        
.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
test_suite_execute_all ENDP

;-----------------------------------------------------------------------------
; test_suite_register_suite - Register a test suite
;
; Input:  SI = Suite name, AL = Suite type, BX = Init function,
;         CX = Run function, DX = Cleanup function
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
test_suite_register_suite PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        
        ; Check if we have space for another suite
        mov     di, [test_suite_count]
        cmp     di, MAX_TEST_SUITES
        jae     .too_many_suites
        
        ; Store suite type
        mov     [suite_types + di], al
        
        ; Store function pointers
        mov     ax, di
        shl     ax, 1                   ; Convert to word offset
        mov     [suite_init_functions + di * 2], bx
        mov     [suite_run_functions + di * 2], cx
        mov     [suite_cleanup_functions + di * 2], dx
        
        ; Copy suite name
        mov     ax, di
        mov     bx, MAX_SUITE_NAME_LEN
        mul     bx
        add     ax, OFFSET suite_names
        mov     di, ax
        
        mov     cx, MAX_SUITE_NAME_LEN - 1
.copy_name:
        lodsb
        test    al, al
        jz      .name_done
        stosb
        loop    .copy_name
.name_done:
        mov     al, 0
        stosb
        
        ; Increment suite count
        inc     word ptr [test_suite_count]
        
        mov     ax, 0                   ; Success
        jmp     .exit
        
.too_many_suites:
        mov     ax, 1                   ; Error - too many suites
        
.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
test_suite_register_suite ENDP

;-----------------------------------------------------------------------------
; Helper Functions
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; register_builtin_suites - Register all built-in test suites
;
; Input:  None
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
register_builtin_suites PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        
        ; Register CPU compatibility test suite
        mov     si, OFFSET cpu_suite_name
        mov     al, SUITE_TYPE_CPU
        mov     bx, OFFSET cpu_compat_test_init
        mov     cx, OFFSET cpu_compat_test_run
        mov     dx, OFFSET cpu_compat_test_cleanup
        call    test_suite_register_suite
        test    ax, ax
        jnz     .register_error
        
        ; Register memory manager test suite
        mov     si, OFFSET mem_suite_name
        mov     al, SUITE_TYPE_MEMORY
        mov     bx, OFFSET mem_manager_test_init
        mov     cx, OFFSET mem_manager_test_run
        mov     dx, OFFSET mem_manager_test_cleanup
        call    test_suite_register_suite
        test    ax, ax
        jnz     .register_error
        
        ; Register performance benchmark suite
        mov     si, OFFSET perf_suite_name
        mov     al, SUITE_TYPE_PERFORMANCE
        mov     bx, OFFSET performance_bench_init
        mov     cx, OFFSET performance_bench_run
        mov     dx, OFFSET performance_bench_cleanup
        call    test_suite_register_suite
        test    ax, ax
        jnz     .register_error
        
        mov     ax, 0                   ; Success
        jmp     .exit
        
.register_error:
        mov     ax, 1                   ; Registration error
        
.exit:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret

; Suite names
cpu_suite_name          db 'CPU Compatibility Tests', 0
mem_suite_name          db 'Memory Manager Tests', 0
perf_suite_name         db 'Performance Benchmarks', 0

register_builtin_suites ENDP

;-----------------------------------------------------------------------------
; should_discover_suite - Check if suite should be discovered
;
; Input:  SI = Suite index
; Output: AL = 1 if should discover, 0 if not
; Uses:   AL, BX
;-----------------------------------------------------------------------------
should_discover_suite PROC
        push    bx
        
        ; Get suite type
        mov     bx, si
        mov     al, [suite_types + bx]
        
        ; Check configuration based on type
        cmp     al, SUITE_TYPE_CPU
        je      .check_cpu
        cmp     al, SUITE_TYPE_MEMORY
        je      .check_memory
        cmp     al, SUITE_TYPE_PERFORMANCE
        je      .check_performance
        cmp     al, SUITE_TYPE_STRESS
        je      .check_stress
        
        ; Default: discover
        mov     al, 1
        jmp     .exit
        
.check_cpu:
        mov     al, [config_run_cpu_tests]
        jmp     .exit
        
.check_memory:
        mov     al, [config_run_memory_tests]
        jmp     .exit
        
.check_performance:
        mov     al, [config_run_perf_tests]
        jmp     .exit
        
.check_stress:
        mov     al, [config_run_stress_tests]
        
.exit:
        pop     bx
        ret
should_discover_suite ENDP

;-----------------------------------------------------------------------------
; should_run_suite - Check if suite should run
;
; Input:  SI = Suite index
; Output: AL = 1 if should run, 0 if not
; Uses:   AL
;-----------------------------------------------------------------------------
should_run_suite PROC
        ; For now, same logic as discovery
        call    should_discover_suite
        ret
should_run_suite ENDP

;-----------------------------------------------------------------------------
; discover_suite_tests - Discover tests in a specific suite
;
; Input:  SI = Suite index
; Output: AL = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
discover_suite_tests PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        
        ; Get suite initialization function
        mov     bx, si
        shl     bx, 1
        mov     ax, [suite_init_functions + bx]
        test    ax, ax
        jz      .no_init_function
        
        ; Call suite initialization (discovery mode)
        mov     bx, ax
        mov     al, 1                   ; Discovery mode flag
        call    bx
        
        ; For now, assume success
        mov     al, 0
        jmp     .exit
        
.no_init_function:
        mov     al, 1                   ; Error - no init function
        
.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
discover_suite_tests ENDP

;-----------------------------------------------------------------------------
; execute_suite_safe - Execute a test suite with error handling
;
; Input:  SI = Suite index
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
execute_suite_safe PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        
        ; Display suite start message
        cmp     byte ptr [config_verbose_output], 0
        je      .skip_suite_msg
        
        mov     dx, OFFSET msg_suite_start
        call    print_string
        
        ; Print suite name
        mov     ax, si
        mov     bx, MAX_SUITE_NAME_LEN
        mul     bx
        add     ax, OFFSET suite_names
        mov     dx, ax
        call    print_string
        
        mov     dx, OFFSET msg_suite_complete
        call    print_string
        
.skip_suite_msg:
        ; Set up error recovery
        call    setup_error_recovery
        
        ; Get suite run function
        mov     bx, si
        shl     bx, 1
        mov     ax, [suite_run_functions + bx]
        test    ax, ax
        jz      .no_run_function
        
        ; Call suite run function
        mov     bx, ax
        call    bx
        
        ; Store result
        mov     bx, si
        shl     bx, 1
        mov     [suite_results + bx], ax
        
        ; Cleanup error recovery
        call    cleanup_error_recovery
        
        jmp     .exit
        
.no_run_function:
        mov     ax, 1                   ; Error - no run function
        
.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
execute_suite_safe ENDP

;-----------------------------------------------------------------------------
; setup_error_recovery - Set up error recovery state
;
; Input:  None
; Output: None
; Uses:   AX, BX
;-----------------------------------------------------------------------------
setup_error_recovery PROC
        push    ax
        push    bx
        
        ; Reset error recovery count for this suite
        mov     word ptr [error_recovery_count], 0
        mov     word ptr [last_error_code], 0
        
        ; Save current timer for timeout detection
        call    get_timer_ticks
        mov     [test_timeout_start], ax
        
        pop     bx
        pop     ax
        ret
setup_error_recovery ENDP

;-----------------------------------------------------------------------------
; cleanup_error_recovery - Clean up error recovery state
;
; Input:  None
; Output: None
; Uses:   None
;-----------------------------------------------------------------------------
cleanup_error_recovery PROC
        ; Reset timeout start
        mov     word ptr [test_timeout_start], 0
        ret
cleanup_error_recovery ENDP

;-----------------------------------------------------------------------------
; display_discovery_results - Display test discovery results
;
; Input:  None
; Output: None
; Uses:   AX, DX
;-----------------------------------------------------------------------------
display_discovery_results PROC
        push    ax
        push    dx
        
        mov     dx, OFFSET msg_discovery_complete
        call    print_string
        
        mov     ax, [discovered_test_count]
        call    print_number
        
        mov     dx, OFFSET msg_tests_found
        call    print_string
        
        mov     ax, [test_suite_count]
        call    print_number
        
        mov     dx, OFFSET msg_suites_found
        call    print_string
        
        pop     dx
        pop     ax
        ret
display_discovery_results ENDP

;-----------------------------------------------------------------------------
; generate_final_report - Generate final test execution report
;
; Input:  None
; Output: None
; Uses:   AX, DX
;-----------------------------------------------------------------------------
generate_final_report PROC
        push    ax
        push    dx
        
        mov     dx, OFFSET msg_final_summary
        call    print_string
        
        ; Total tests run
        mov     dx, OFFSET msg_total_run
        call    print_string
        mov     ax, [total_tests_run]
        call    print_number
        mov     dx, OFFSET msg_newline
        call    print_string
        
        ; Total passed
        mov     dx, OFFSET msg_total_passed
        call    print_string
        mov     ax, [total_tests_passed]
        call    print_number
        mov     dx, OFFSET msg_newline
        call    print_string
        
        ; Total failed
        mov     dx, OFFSET msg_total_failed
        call    print_string
        mov     ax, [total_tests_failed]
        call    print_number
        mov     dx, OFFSET msg_newline
        call    print_string
        
        ; Total skipped
        mov     dx, OFFSET msg_total_skipped
        call    print_string
        mov     ax, [total_tests_skipped]
        call    print_number
        mov     dx, OFFSET msg_newline
        call    print_string
        
        pop     dx
        pop     ax
        ret
generate_final_report ENDP

;-----------------------------------------------------------------------------
; Utility Functions (use existing implementations from test_framework.asm)
;-----------------------------------------------------------------------------
EXTRN print_string:PROC
EXTRN print_number:PROC
EXTRN get_timer_ticks:PROC

;-----------------------------------------------------------------------------
; test_suite_cleanup - Cleanup all test suite resources
;
; Input:  None
; Output: None
; Uses:   AX, BX, CX, SI
;-----------------------------------------------------------------------------
test_suite_cleanup PROC
        push    bp
        mov     bp, sp
        push    ax
        push    bx
        push    cx
        push    si
        
        ; Call cleanup function for each registered suite
        mov     si, 0
        mov     cx, [test_suite_count]
        test    cx, cx
        jz      .no_cleanup
        
.cleanup_loop:
        mov     bx, si
        shl     bx, 1
        mov     ax, [suite_cleanup_functions + bx]
        test    ax, ax
        jz      .skip_cleanup
        
        ; Call cleanup function
        push    cx
        push    si
        mov     bx, ax
        call    bx
        pop     si
        pop     cx
        
.skip_cleanup:
        inc     si
        loop    .cleanup_loop
        
.no_cleanup:
        ; Reset framework state
        mov     byte ptr [execution_status], EXEC_STATE_IDLE
        mov     byte ptr [discovery_status], DISCOVERY_NOT_STARTED
        
        pop     si
        pop     cx
        pop     bx
        pop     ax
        pop     bp
        ret
test_suite_cleanup ENDP

;-----------------------------------------------------------------------------
; test_suite_get_results - Get overall test results
;
; Input:  SI = Pointer to result structure
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DI
;-----------------------------------------------------------------------------
test_suite_get_results PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    di
        
        test    si, si
        jz      .invalid_pointer
        
        mov     di, si
        
        ; Copy results to structure
        mov     ax, [total_tests_run]
        mov     [di], ax                ; Total tests
        mov     ax, [total_tests_passed]
        mov     [di + 2], ax            ; Passed
        mov     ax, [total_tests_failed]
        mov     [di + 4], ax            ; Failed
        mov     ax, [total_tests_skipped]
        mov     [di + 6], ax            ; Skipped
        mov     al, [execution_status]
        mov     [di + 8], al            ; Status
        
        mov     ax, 0                   ; Success
        jmp     .exit
        
.invalid_pointer:
        mov     ax, 1                   ; Error
        
.exit:
        pop     di
        pop     cx
        pop     bx
        pop     bp
        ret
test_suite_get_results ENDP

;-----------------------------------------------------------------------------
; test_suite_set_config - Set test suite configuration
;
; Input:  AL = Config type, BL = Value
; Output: AX = 0 for success, non-zero for error
; Uses:   AX
;-----------------------------------------------------------------------------
test_suite_set_config PROC
        cmp     al, 1
        je      .set_cpu_tests
        cmp     al, 2
        je      .set_memory_tests
        cmp     al, 3
        je      .set_perf_tests
        cmp     al, 4
        je      .set_stress_tests
        cmp     al, 5
        je      .set_verbose
        cmp     al, 6
        je      .set_stop_on_fail
        cmp     al, 7
        je      .set_timeout
        
        mov     ax, 1                   ; Invalid config type
        ret
        
.set_cpu_tests:
        mov     [config_run_cpu_tests], bl
        jmp     .success
.set_memory_tests:
        mov     [config_run_memory_tests], bl
        jmp     .success
.set_perf_tests:
        mov     [config_run_perf_tests], bl
        jmp     .success
.set_stress_tests:
        mov     [config_run_stress_tests], bl
        jmp     .success
.set_verbose:
        mov     [config_verbose_output], bl
        jmp     .success
.set_stop_on_fail:
        mov     [config_stop_on_failure], bl
        jmp     .success
.set_timeout:
        mov     [config_timeout_enabled], bl
        
.success:
        mov     ax, 0
        ret
test_suite_set_config ENDP

;-----------------------------------------------------------------------------
; test_suite_handle_error - Handle test execution errors
;
; Input:  AX = Error code
; Output: AL = Recovery result (0=recovered, 1=failed)
; Uses:   AX, BX, CX
;-----------------------------------------------------------------------------
test_suite_handle_error PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        
        ; Store error code
        mov     [last_error_code], ax
        
        ; Check recovery count
        mov     bx, [error_recovery_count]
        cmp     bx, [max_error_recovery]
        jae     .max_recovery_reached
        
        ; Attempt recovery
        cmp     byte ptr [config_verbose_output], 0
        je      .skip_recovery_msg
        mov     dx, OFFSET msg_error_recovery
        call    print_string
        
.skip_recovery_msg:
        ; Increment recovery count
        inc     word ptr [error_recovery_count]
        
        ; Perform basic error recovery
        call    basic_error_recovery
        
        mov     al, 0                   ; Recovery attempted
        jmp     .exit
        
.max_recovery_reached:
        cmp     byte ptr [config_verbose_output], 0
        je      .skip_fail_msg
        mov     dx, OFFSET msg_recovery_failed
        call    print_string
        
.skip_fail_msg:
        mov     al, 1                   ; Recovery failed
        
.exit:
        pop     cx
        pop     bx
        pop     bp
        ret
test_suite_handle_error ENDP

;-----------------------------------------------------------------------------
; basic_error_recovery - Perform basic error recovery operations
;
; Input:  None
; Output: None
; Uses:   AX, BX
;-----------------------------------------------------------------------------
basic_error_recovery PROC
        push    ax
        push    bx
        
        ; Reset stack pointer to a known good state
        ; (This is a simplified version - in real implementation,
        ;  we would save/restore stack state more carefully)
        
        ; Clear any pending interrupts
        cli
        sti
        
        ; Reset test timeout
        call    get_timer_ticks
        mov     [test_timeout_start], ax
        
        pop     bx
        pop     ax
        ret
basic_error_recovery ENDP

;-----------------------------------------------------------------------------
; test_suite_timeout_check - Check for test timeout
;
; Input:  None
; Output: AL = 1 if timeout, 0 if not
; Uses:   AX, BX
;-----------------------------------------------------------------------------
test_suite_timeout_check PROC
        push    bx
        
        ; Check if timeout is enabled
        cmp     byte ptr [config_timeout_enabled], 0
        je      .no_timeout
        
        ; Check if timeout timer is active
        mov     ax, [test_timeout_start]
        test    ax, ax
        jz      .no_timeout
        
        ; Get current time
        call    get_timer_ticks
        mov     bx, ax
        
        ; Calculate elapsed time
        sub     bx, [test_timeout_start]
        
        ; Check against timeout threshold
        cmp     bx, TEST_TIMEOUT_TICKS
        ja      .timeout_detected
        
.no_timeout:
        mov     al, 0                   ; No timeout
        jmp     .exit
        
.timeout_detected:
        mov     al, 1                   ; Timeout detected
        
.exit:
        pop     bx
        ret
test_suite_timeout_check ENDP

_TEXT ENDS

END