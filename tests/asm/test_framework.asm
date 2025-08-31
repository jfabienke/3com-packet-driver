; @file asm_test_framework.asm
; @brief Assembly test harness and runner for comprehensive testing
;
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
;
; This file implements a complete assembly language testing framework
; for validating assembly code modules, register state, memory operations,
; and performance optimizations.

.MODEL SMALL
.386

; Test Framework Constants
TEST_MAX_TESTS          EQU 64          ; Maximum number of tests
TEST_MAX_NAME_LEN       EQU 32          ; Maximum test name length
TEST_MAX_MSG_LEN        EQU 128         ; Maximum message length

; Test Results
TEST_RESULT_PASS        EQU 0           ; Test passed
TEST_RESULT_FAIL        EQU 1           ; Test failed
TEST_RESULT_SKIP        EQU 2           ; Test skipped
TEST_RESULT_ERROR       EQU 3           ; Test error

; Test Categories
TEST_CAT_REGISTERS      EQU 1           ; Register validation tests
TEST_CAT_MEMORY         EQU 2           ; Memory operation tests
TEST_CAT_PERFORMANCE    EQU 3           ; Performance tests
TEST_CAT_INTEGRATION    EQU 4           ; Integration tests
TEST_CAT_CPU_DETECT     EQU 5           ; CPU detection tests

; Test Framework Status
TEST_STATUS_INIT        EQU 0           ; Initializing
TEST_STATUS_READY       EQU 1           ; Ready to run
TEST_STATUS_RUNNING     EQU 2           ; Running tests
TEST_STATUS_COMPLETE    EQU 3           ; Tests complete

; Memory Test Constants
MEM_TEST_PATTERN_A      EQU 0AAh        ; Test pattern A
MEM_TEST_PATTERN_5      EQU 055h        ; Test pattern 5
MEM_TEST_PATTERN_0      EQU 000h        ; Test pattern 0
MEM_TEST_PATTERN_F      EQU 0FFh        ; Test pattern F

; Performance Test Constants
PERF_TEST_ITERATIONS    EQU 10000       ; Default performance test iterations
PERF_MIN_EXPECTED_SPEED EQU 1000        ; Minimum expected operations/sec

; Data segment
_DATA SEGMENT
        ASSUME  DS:_DATA

; Test framework state
test_framework_status   db TEST_STATUS_INIT
test_count             dw 0                    ; Number of tests run
test_passed            dw 0                    ; Number of tests passed
test_failed            dw 0                    ; Number of tests failed
test_skipped           dw 0                    ; Number of tests skipped
current_test_id        dw 0                    ; Current test ID

; Test result tracking
test_names             db TEST_MAX_TESTS * TEST_MAX_NAME_LEN dup(0)
test_results           db TEST_MAX_TESTS dup(0)
test_categories        db TEST_MAX_TESTS dup(0)
test_durations         dw TEST_MAX_TESTS dup(0)

; Register state validation
saved_registers        dw 8 dup(0)             ; AX, BX, CX, DX, SI, DI, BP, SP
expected_registers     dw 8 dup(0)             ; Expected register values
register_mask          dw 8 dup(0FFFFh)        ; Which bits to check

; Memory test areas
memory_test_buffer     db 4096 dup(0)          ; Test buffer for memory operations
memory_backup_buffer   db 4096 dup(0)          ; Backup for restore
memory_pattern_buffer  db 256 dup(0)           ; Pattern generation buffer

; Performance measurement
perf_start_time        dw 0                    ; Performance test start time
perf_end_time          dw 0                    ; Performance test end time
perf_iterations        dw 0                    ; Performance test iterations

; Test messages and reporting
test_header_msg        db 'Assembly Test Framework v1.0', 0Dh, 0Ah, 0
test_separator_msg     db '----------------------------------------', 0Dh, 0Ah, 0
test_start_msg         db 'Starting test: ', 0
test_pass_msg          db ' [PASS]', 0Dh, 0Ah, 0
test_fail_msg          db ' [FAIL]', 0Dh, 0Ah, 0
test_skip_msg          db ' [SKIP]', 0Dh, 0Ah, 0
test_error_msg         db ' [ERROR]', 0Dh, 0Ah, 0

; Summary messages
summary_header_msg     db 0Dh, 0Ah, 'TEST SUMMARY:', 0Dh, 0Ah, 0
summary_total_msg      db 'Total tests: ', 0
summary_passed_msg     db 'Passed: ', 0
summary_failed_msg     db 'Failed: ', 0
summary_skipped_msg    db 'Skipped: ', 0
summary_newline        db 0Dh, 0Ah, 0

; Error messages
error_framework_msg    db 'Test framework error: ', 0
error_memory_msg       db 'Memory test error', 0Dh, 0Ah, 0
error_register_msg     db 'Register validation error', 0Dh, 0Ah, 0
error_performance_msg  db 'Performance test error', 0Dh, 0Ah, 0

_DATA ENDS

; Code segment
_TEXT SEGMENT
        ASSUME  CS:_TEXT, DS:_DATA

; Public function exports
PUBLIC test_framework_init
PUBLIC test_framework_run_test
PUBLIC test_framework_validate_registers
PUBLIC test_framework_memory_test
PUBLIC test_framework_performance_test
PUBLIC test_framework_report_results
PUBLIC test_framework_cleanup
PUBLIC test_start_test
PUBLIC test_end_test
PUBLIC test_assert_equal
PUBLIC test_assert_memory_equal
PUBLIC test_measure_performance

; External references for integration testing
EXTRN cpu_detect_main:PROC
EXTRN get_cpu_type:PROC
EXTRN get_cpu_features:PROC

;-----------------------------------------------------------------------------
; test_framework_init - Initialize the assembly test framework
;
; Input:  None
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
test_framework_init PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Display framework header
        mov     dx, OFFSET test_header_msg
        call    print_string
        mov     dx, OFFSET test_separator_msg
        call    print_string

        ; Initialize test counters
        mov     word ptr [test_count], 0
        mov     word ptr [test_passed], 0
        mov     word ptr [test_failed], 0
        mov     word ptr [test_skipped], 0
        mov     word ptr [current_test_id], 0

        ; Clear test result arrays
        mov     cx, TEST_MAX_TESTS
        mov     si, OFFSET test_results
        xor     al, al
.clear_results:
        mov     [si], al
        inc     si
        loop    .clear_results

        ; Clear test names
        mov     cx, TEST_MAX_TESTS * TEST_MAX_NAME_LEN
        mov     si, OFFSET test_names
        xor     al, al
        rep     stosb

        ; Initialize register validation arrays
        mov     cx, 8
        mov     si, OFFSET saved_registers
        xor     ax, ax
.clear_regs:
        mov     [si], ax
        add     si, 2
        loop    .clear_regs

        ; Initialize memory test buffers
        mov     cx, 4096
        mov     si, OFFSET memory_test_buffer
        mov     al, 0
        rep     stosb

        ; Set framework status to ready
        mov     byte ptr [test_framework_status], TEST_STATUS_READY

        ; Success
        mov     ax, 0

        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
test_framework_init ENDP

;-----------------------------------------------------------------------------
; test_framework_run_test - Run a single test with validation
;
; Input:  SI = Pointer to test name, BX = Test function address
;         CL = Test category, CH = Expected result
; Output: AL = Test result
; Uses:   All registers (test function dependent)
;-----------------------------------------------------------------------------
test_framework_run_test PROC
        push    bp
        mov     bp, sp
        push    dx
        push    di

        ; Check framework status
        cmp     byte ptr [test_framework_status], TEST_STATUS_READY
        jb      .framework_error

        ; Set status to running if not already
        cmp     byte ptr [test_framework_status], TEST_STATUS_READY
        jne     .status_ok
        mov     byte ptr [test_framework_status], TEST_STATUS_RUNNING

.status_ok:
        ; Save test information
        mov     dx, [current_test_id]
        cmp     dx, TEST_MAX_TESTS
        jae     .too_many_tests

        ; Store test name
        mov     di, dx
        mov     ax, TEST_MAX_NAME_LEN
        mul     di
        add     ax, OFFSET test_names
        mov     di, ax
        
        push    cx
        mov     cx, TEST_MAX_NAME_LEN - 1
.copy_name:
        lodsb
        or      al, al
        jz      .name_done
        stosb
        loop    .copy_name
.name_done:
        mov     al, 0
        stosb
        pop     cx

        ; Store test category
        mov     di, [current_test_id]
        mov     [test_categories + di], cl

        ; Display test start message
        push    si
        mov     dx, OFFSET test_start_msg
        call    print_string
        
        ; Calculate test name address
        mov     ax, [current_test_id]
        mov     dx, TEST_MAX_NAME_LEN
        mul     dx
        add     ax, OFFSET test_names
        mov     dx, ax
        call    print_string
        pop     si

        ; Save current register state for validation
        call    save_register_state

        ; Get start time for performance measurement
        call    get_timer_ticks
        mov     [perf_start_time], ax

        ; Call the test function
        call    bx

        ; Get end time
        call    get_timer_ticks
        mov     [perf_end_time], ax

        ; Store test result
        mov     di, [current_test_id]
        mov     [test_results + di], al

        ; Update statistics
        cmp     al, TEST_RESULT_PASS
        je      .test_passed
        cmp     al, TEST_RESULT_FAIL
        je      .test_failed
        cmp     al, TEST_RESULT_SKIP
        je      .test_skipped
        jmp     .test_failed        ; Treat unknown as failure

.test_passed:
        inc     word ptr [test_passed]
        mov     dx, OFFSET test_pass_msg
        jmp     .display_result

.test_failed:
        inc     word ptr [test_failed]
        mov     dx, OFFSET test_fail_msg
        jmp     .display_result

.test_skipped:
        inc     word ptr [test_skipped]
        mov     dx, OFFSET test_skip_msg

.display_result:
        call    print_string

        ; Increment test counters
        inc     word ptr [test_count]
        inc     word ptr [current_test_id]

        ; Return test result
        mov     al, [test_results + di]
        jmp     .exit

.framework_error:
        mov     dx, OFFSET error_framework_msg
        call    print_string
        mov     al, TEST_RESULT_ERROR
        jmp     .exit

.too_many_tests:
        mov     al, TEST_RESULT_ERROR

.exit:
        pop     di
        pop     dx
        pop     bp
        ret
test_framework_run_test ENDP

;-----------------------------------------------------------------------------
; test_framework_validate_registers - Validate register state after test
;
; Input:  None (uses saved_registers and expected_registers)
; Output: AL = 0 if valid, non-zero if invalid
; Uses:   AX, BX, CX, SI
;-----------------------------------------------------------------------------
test_framework_validate_registers PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si

        ; Compare current registers with expected values
        mov     si, OFFSET saved_registers
        mov     bx, OFFSET expected_registers
        mov     cx, 8                   ; 8 registers to check

.check_register:
        mov     ax, [si]                ; Current value
        xor     ax, [bx]                ; XOR with expected
        and     ax, [register_mask + si] ; Apply mask
        jnz     .register_mismatch       ; Non-zero means mismatch

        add     si, 2
        add     bx, 2
        loop    .check_register

        ; All registers match
        mov     al, 0
        jmp     .exit

.register_mismatch:
        mov     dx, OFFSET error_register_msg
        call    print_string
        mov     al, 1

.exit:
        pop     si
        pop     cx
        pop     bx
        pop     bp
        ret
test_framework_validate_registers ENDP

;-----------------------------------------------------------------------------
; test_framework_memory_test - Comprehensive memory testing
;
; Input:  SI = Memory area to test, CX = Size in bytes
; Output: AL = Test result
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
test_framework_memory_test PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Validate parameters
        test    cx, cx
        jz      .invalid_size
        cmp     cx, 4096
        ja      .invalid_size

        ; Backup original memory content
        mov     di, OFFSET memory_backup_buffer
        rep     movsb

        ; Restore parameters
        mov     si, [bp + 4]            ; Restore SI from stack frame
        mov     cx, [bp + 6]            ; Restore CX from stack frame

        ; Test 1: Walking 1s pattern
        call    memory_test_walking_ones
        cmp     al, 0
        jne     .test_failed

        ; Test 2: Walking 0s pattern  
        call    memory_test_walking_zeros
        cmp     al, 0
        jne     .test_failed

        ; Test 3: Checkerboard pattern
        call    memory_test_checkerboard
        cmp     al, 0
        jne     .test_failed

        ; Test 4: Address-in-address test
        call    memory_test_address_in_address
        cmp     al, 0
        jne     .test_failed

        ; Restore original memory content
        mov     si, OFFSET memory_backup_buffer
        mov     di, [bp + 4]            ; Original memory area
        mov     cx, [bp + 6]            ; Original size
        rep     movsb

        ; All tests passed
        mov     al, TEST_RESULT_PASS
        jmp     .exit

.test_failed:
        ; Restore memory before returning failure
        mov     si, OFFSET memory_backup_buffer
        mov     di, [bp + 4]
        mov     cx, [bp + 6]
        rep     movsb
        
        mov     dx, OFFSET error_memory_msg
        call    print_string
        mov     al, TEST_RESULT_FAIL
        jmp     .exit

.invalid_size:
        mov     al, TEST_RESULT_ERROR

.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
test_framework_memory_test ENDP

;-----------------------------------------------------------------------------
; test_framework_performance_test - Measure and validate performance
;
; Input:  BX = Function to test, CX = Expected min operations/sec
; Output: AL = Test result, DX = Actual operations/sec
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
test_framework_performance_test PROC
        push    bp
        mov     bp, sp
        push    si
        push    di

        ; Record start time
        call    get_timer_ticks
        mov     [perf_start_time], ax

        ; Run performance test iterations
        mov     si, PERF_TEST_ITERATIONS
        mov     [perf_iterations], si

.perf_loop:
        call    bx                      ; Call test function
        dec     si
        jnz     .perf_loop

        ; Record end time
        call    get_timer_ticks
        mov     [perf_end_time], ax

        ; Calculate elapsed time (in timer ticks)
        mov     ax, [perf_end_time]
        sub     ax, [perf_start_time]
        jz      .divide_by_zero         ; Avoid division by zero

        ; Calculate operations per second
        ; ops_per_sec = (iterations * 18.2) / elapsed_ticks
        mov     dx, 0
        mov     si, [perf_iterations]
        mov     bx, 18                  ; Approximate timer frequency
        mul     bx
        div     si                      ; AX = ops/sec (approximate)
        mov     dx, ax

        ; Compare with expected minimum
        cmp     ax, cx
        jae     .perf_pass

        ; Performance below expectations
        mov     dx, OFFSET error_performance_msg
        call    print_string
        mov     al, TEST_RESULT_FAIL
        jmp     .exit

.perf_pass:
        mov     al, TEST_RESULT_PASS
        jmp     .exit

.divide_by_zero:
        mov     al, TEST_RESULT_ERROR
        mov     dx, 0

.exit:
        pop     di
        pop     si
        pop     bp
        ret
test_framework_performance_test ENDP

;-----------------------------------------------------------------------------
; test_framework_report_results - Generate comprehensive test report
;
; Input:  None
; Output: None
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
test_framework_report_results PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Set framework status to complete
        mov     byte ptr [test_framework_status], TEST_STATUS_COMPLETE

        ; Display summary header
        mov     dx, OFFSET summary_header_msg
        call    print_string
        mov     dx, OFFSET test_separator_msg
        call    print_string

        ; Display total tests
        mov     dx, OFFSET summary_total_msg
        call    print_string
        mov     ax, [test_count]
        call    print_number
        mov     dx, OFFSET summary_newline
        call    print_string

        ; Display passed tests
        mov     dx, OFFSET summary_passed_msg
        call    print_string
        mov     ax, [test_passed]
        call    print_number
        mov     dx, OFFSET summary_newline
        call    print_string

        ; Display failed tests
        mov     dx, OFFSET summary_failed_msg
        call    print_string
        mov     ax, [test_failed]
        call    print_number
        mov     dx, OFFSET summary_newline
        call    print_string

        ; Display skipped tests
        mov     dx, OFFSET summary_skipped_msg
        call    print_string
        mov     ax, [test_skipped]
        call    print_number
        mov     dx, OFFSET summary_newline
        call    print_string

        ; Display detailed results
        mov     dx, OFFSET test_separator_msg
        call    print_string

        mov     si, 0                   ; Test index
        mov     cx, [test_count]        ; Number of tests
        test    cx, cx
        jz      .no_tests

.detail_loop:
        ; Display test name
        mov     ax, si
        mov     bx, TEST_MAX_NAME_LEN
        mul     bx
        add     ax, OFFSET test_names
        mov     dx, ax
        call    print_string

        ; Display result
        mov     al, [test_results + si]
        cmp     al, TEST_RESULT_PASS
        je      .show_pass
        cmp     al, TEST_RESULT_FAIL
        je      .show_fail
        cmp     al, TEST_RESULT_SKIP
        je      .show_skip
        mov     dx, OFFSET test_error_msg
        jmp     .show_result

.show_pass:
        mov     dx, OFFSET test_pass_msg
        jmp     .show_result
.show_fail:
        mov     dx, OFFSET test_fail_msg
        jmp     .show_result
.show_skip:
        mov     dx, OFFSET test_skip_msg

.show_result:
        call    print_string
        inc     si
        loop    .detail_loop

.no_tests:
        mov     dx, OFFSET test_separator_msg
        call    print_string

        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
test_framework_report_results ENDP

;-----------------------------------------------------------------------------
; test_framework_cleanup - Cleanup framework resources
;
; Input:  None
; Output: None
; Uses:   AX
;-----------------------------------------------------------------------------
test_framework_cleanup PROC
        push    bp
        mov     bp, sp

        ; Reset framework status
        mov     byte ptr [test_framework_status], TEST_STATUS_INIT

        ; Generate final report if tests were run
        cmp     word ptr [test_count], 0
        jz      .no_report
        call    test_framework_report_results

.no_report:
        pop     bp
        ret
test_framework_cleanup ENDP

;-----------------------------------------------------------------------------
; Helper Functions for Test Framework
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; save_register_state - Save current register state for validation
;
; Input:  None
; Output: None (saves to saved_registers array)
; Uses:   None (all registers preserved)
;-----------------------------------------------------------------------------
save_register_state PROC
        push    si
        mov     si, OFFSET saved_registers
        
        mov     [si], ax                ; Save AX
        mov     [si + 2], bx            ; Save BX
        mov     [si + 4], cx            ; Save CX
        mov     [si + 6], dx            ; Save DX
        mov     [si + 8], si            ; Save SI (adjusted)
        mov     [si + 10], di           ; Save DI
        mov     [si + 12], bp           ; Save BP
        mov     [si + 14], sp           ; Save SP
        
        pop     si
        ret
save_register_state ENDP

;-----------------------------------------------------------------------------
; get_timer_ticks - Get current timer tick count
;
; Input:  None
; Output: AX = Current timer ticks
; Uses:   AX
;-----------------------------------------------------------------------------
get_timer_ticks PROC
        push    ds
        
        ; Read BIOS timer tick counter at 0040:006C
        mov     ax, 0040h
        mov     ds, ax
        mov     ax, ds:[006Ch]          ; Low word of tick counter
        
        pop     ds
        ret
get_timer_ticks ENDP

;-----------------------------------------------------------------------------
; print_string - Print null-terminated string
;
; Input:  DS:DX = Pointer to string
; Output: None
; Uses:   AX, DX
;-----------------------------------------------------------------------------
print_string PROC
        push    ax
        push    si
        
        mov     si, dx
.loop:
        lodsb
        or      al, al
        jz      .done
        mov     dl, al
        mov     ah, 02h                 ; DOS write character
        int     21h
        jmp     .loop
.done:
        pop     si
        pop     ax
        ret
print_string ENDP

;-----------------------------------------------------------------------------
; print_number - Print 16-bit number in decimal
;
; Input:  AX = Number to print
; Output: None
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
print_number PROC
        push    ax
        push    bx
        push    cx
        push    dx
        
        mov     bx, 10
        mov     cx, 0
        
        ; Convert to decimal digits
.divide_loop:
        mov     dx, 0
        div     bx
        add     dl, '0'
        push    dx
        inc     cx
        test    ax, ax
        jnz     .divide_loop
        
        ; Print digits
.print_loop:
        pop     dx
        mov     ah, 02h
        int     21h
        loop    .print_loop
        
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
print_number ENDP

;-----------------------------------------------------------------------------
; Memory Test Helper Functions
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; memory_test_walking_ones - Walking ones memory test
;
; Input:  SI = Memory area, CX = Size
; Output: AL = 0 if pass, non-zero if fail
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
memory_test_walking_ones PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        mov     di, si                  ; DI = memory area
        mov     bx, cx                  ; BX = size

        ; Test walking 1 pattern through each byte
        mov     dx, 8                   ; 8 bits per byte

.bit_loop:
        mov     al, 1
        mov     cl, dl
        dec     cl                      ; CL = bit position
        shl     al, cl                  ; AL = walking 1 pattern

        ; Write pattern to entire area
        mov     si, di
        mov     cx, bx
        rep     stosb

        ; Verify pattern
        mov     si, di
        mov     cx, bx
.verify_loop:
        lodsb
        cmp     al, [si - 1]
        jne     .test_fail
        loop    .verify_loop

        dec     dx
        jnz     .bit_loop

        ; Test passed
        mov     al, 0
        jmp     .exit

.test_fail:
        mov     al, 1

.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
memory_test_walking_ones ENDP

;-----------------------------------------------------------------------------
; memory_test_walking_zeros - Walking zeros memory test
;
; Input:  SI = Memory area, CX = Size
; Output: AL = 0 if pass, non-zero if fail
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
memory_test_walking_zeros PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        mov     di, si
        mov     bx, cx

        ; Test walking 0 pattern
        mov     dx, 8

.bit_loop:
        mov     al, 0FEh                ; 11111110b
        mov     cl, dl
        dec     cl
        rol     al, cl                  ; Rotate to create walking 0

        ; Write pattern
        mov     si, di
        mov     cx, bx
        rep     stosb

        ; Verify pattern
        mov     si, di
        mov     cx, bx
.verify_loop:
        lodsb
        cmp     al, [si - 1]
        jne     .test_fail
        loop    .verify_loop

        dec     dx
        jnz     .bit_loop

        mov     al, 0
        jmp     .exit

.test_fail:
        mov     al, 1

.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
memory_test_walking_zeros ENDP

;-----------------------------------------------------------------------------
; memory_test_checkerboard - Checkerboard pattern memory test
;
; Input:  SI = Memory area, CX = Size
; Output: AL = 0 if pass, non-zero if fail
; Uses:   AX, BX, CX, SI, DI
;-----------------------------------------------------------------------------
memory_test_checkerboard PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si
        push    di

        mov     di, si
        mov     bx, cx

        ; Test 0xAA pattern
        mov     al, MEM_TEST_PATTERN_A
        mov     cx, bx
        rep     stosb

        ; Verify 0xAA pattern
        mov     si, di
        mov     cx, bx
.verify_aa:
        lodsb
        cmp     al, MEM_TEST_PATTERN_A
        jne     .test_fail
        loop    .verify_aa

        ; Test 0x55 pattern
        mov     si, di
        mov     al, MEM_TEST_PATTERN_5
        mov     cx, bx
        rep     stosb

        ; Verify 0x55 pattern
        mov     si, di
        mov     cx, bx
.verify_55:
        lodsb
        cmp     al, MEM_TEST_PATTERN_5
        jne     .test_fail
        loop    .verify_55

        mov     al, 0
        jmp     .exit

.test_fail:
        mov     al, 1

.exit:
        pop     di
        pop     si
        pop     cx
        pop     bx
        pop     bp
        ret
memory_test_checkerboard ENDP

;-----------------------------------------------------------------------------
; memory_test_address_in_address - Address-in-address memory test
;
; Input:  SI = Memory area, CX = Size (must be even)
; Output: AL = 0 if pass, non-zero if fail
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
memory_test_address_in_address PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Ensure size is even for word operations
        and     cx, 0FFFEh
        jz      .invalid_size

        mov     di, si
        mov     bx, cx
        shr     cx, 1                   ; Convert to word count

        ; Write address to each word location
        mov     dx, di                  ; Start address
.write_loop:
        mov     [di], dx                ; Store address at location
        add     di, 2                   ; Next word
        add     dx, 2                   ; Next address
        loop    .write_loop

        ; Verify addresses
        mov     di, si                  ; Reset to start
        mov     cx, bx
        shr     cx, 1
        mov     dx, di                  ; Expected address
.verify_loop:
        cmp     [di], dx                ; Compare stored vs expected
        jne     .test_fail
        add     di, 2
        add     dx, 2
        loop    .verify_loop

        mov     al, 0
        jmp     .exit

.test_fail:
        mov     al, 1
        jmp     .exit

.invalid_size:
        mov     al, 1

.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
memory_test_address_in_address ENDP

;-----------------------------------------------------------------------------
; Test Convenience Macros and Functions
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; test_start_test - Start a test with timing
;
; Input:  SI = Test name pointer
; Output: None
; Uses:   AX, DX
;-----------------------------------------------------------------------------
test_start_test PROC
        push    ax
        push    dx
        
        ; Print test start message
        mov     dx, OFFSET test_start_msg
        call    print_string
        mov     dx, si
        call    print_string
        
        ; Record start time
        call    get_timer_ticks
        mov     [perf_start_time], ax
        
        pop     dx
        pop     ax
        ret
test_start_test ENDP

;-----------------------------------------------------------------------------
; test_end_test - End a test and record duration
;
; Input:  AL = Test result
; Output: None
; Uses:   AX, DX
;-----------------------------------------------------------------------------
test_end_test PROC
        push    ax
        push    dx
        
        ; Record end time
        call    get_timer_ticks
        mov     [perf_end_time], ax
        
        ; Calculate and store duration
        sub     ax, [perf_start_time]
        mov     dx, [current_test_id]
        mov     [test_durations + dx * 2], ax
        
        pop     dx
        pop     ax
        ret
test_end_test ENDP

;-----------------------------------------------------------------------------
; test_assert_equal - Assert two values are equal
;
; Input:  AX = Actual value, BX = Expected value
; Output: AL = TEST_RESULT_PASS or TEST_RESULT_FAIL
; Uses:   AX
;-----------------------------------------------------------------------------
test_assert_equal PROC
        cmp     ax, bx
        je      .equal
        mov     al, TEST_RESULT_FAIL
        ret
.equal:
        mov     al, TEST_RESULT_PASS
        ret
test_assert_equal ENDP

;-----------------------------------------------------------------------------
; test_assert_memory_equal - Assert memory blocks are equal
;
; Input:  SI = Memory block 1, DI = Memory block 2, CX = Size
; Output: AL = TEST_RESULT_PASS or TEST_RESULT_FAIL
; Uses:   AX, CX, SI, DI
;-----------------------------------------------------------------------------
test_assert_memory_equal PROC
        push    cx
        push    si
        push    di
        
        repe    cmpsb
        jne     .not_equal
        
        mov     al, TEST_RESULT_PASS
        jmp     .exit
        
.not_equal:
        mov     al, TEST_RESULT_FAIL
        
.exit:
        pop     di
        pop     si
        pop     cx
        ret
test_assert_memory_equal ENDP

;-----------------------------------------------------------------------------
; test_measure_performance - Measure performance of a function
;
; Input:  BX = Function address, CX = Number of iterations
; Output: AX = Operations per second, DX = Total time in ticks
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
test_measure_performance PROC
        push    si
        
        ; Record start time
        call    get_timer_ticks
        push    ax                      ; Save start time
        
        mov     si, cx                  ; Save iteration count
        
.perf_loop:
        call    bx                      ; Call test function
        loop    .perf_loop
        
        ; Record end time
        call    get_timer_ticks
        pop     dx                      ; Restore start time
        sub     ax, dx                  ; AX = elapsed ticks
        mov     dx, ax                  ; DX = total time
        
        ; Calculate operations per second
        ; ops/sec = (iterations * 18.2) / elapsed_ticks
        test    ax, ax
        jz      .divide_by_zero
        
        mov     ax, si                  ; Get iteration count
        mov     bx, 18                  ; Timer frequency approximation
        mul     bx                      ; AX = iterations * 18
        div     dx                      ; AX = ops/sec
        
        jmp     .exit
        
.divide_by_zero:
        mov     ax, 0                   ; Return 0 ops/sec
        
.exit:
        pop     si
        ret
test_measure_performance ENDP

_TEXT ENDS

END