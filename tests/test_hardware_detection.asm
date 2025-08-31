; test_hardware_detection.asm
; Assembly test harness for hardware detection functions
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs

BITS 16
ORG 0x100  ; COM file format for DOS testing

%include "../include/tsr_defensive.inc"

; Test framework constants
MAX_TEST_RESULTS    equ 64
LFSR_TEST_CYCLES    equ 1000
DETECTION_TIMEOUT   equ 10000       ; 10 second timeout

; Test result codes
TEST_PASS           equ 0
TEST_FAIL           equ 1
TEST_SKIP           equ 2
TEST_ERROR          equ 3

; Hardware detection test types
DETECT_3C509B       equ 1
DETECT_3C515        equ 2
DETECT_LFSR_SEQ     equ 3
DETECT_ID_PORT      equ 4
DETECT_IRQ_PROBE    equ 5

SECTION .data

; Test configuration
test_config:
    db "HARDWARE DETECTION TEST SUITE", 0
    db "============================", 0

; Test result storage
test_results:
    times MAX_TEST_RESULTS db 0

test_count          dw 0
passed_count        dw 0
failed_count        dw 0
skip_count          dw 0

; LFSR sequence validation data
lfsr_seed           dw 0x1357       ; Initial seed for testing
lfsr_expected       dw 0            ; Expected sequence value
lfsr_actual         dw 0            ; Actual sequence value

; Hardware mock configuration
mock_3c509b_config:
    dw 0x0200       ; I/O base address
    db 10           ; IRQ
    db 0x00, 0x60, 0x8C, 0x12, 0x34, 0x56  ; MAC address
    dw 0x5090       ; Product ID (3C509B)
    db 1            ; Link status (up)
    
mock_3c515_config:
    dw 0x0220       ; I/O base address  
    db 11           ; IRQ
    db 0x00, 0x60, 0x8C, 0x12, 0x34, 0x57  ; MAC address
    dw 0x5150       ; Product ID (3C515)
    db 1            ; Link status (up)

; Test messages
msg_start:          db "Starting hardware detection tests...", 13, 10, 0
msg_lfsr_test:      db "Testing LFSR sequence generation...", 13, 10, 0
msg_3c509b_test:    db "Testing 3C509B detection...", 13, 10, 0
msg_3c515_test:     db "Testing 3C515 detection...", 13, 10, 0
msg_irq_test:       db "Testing IRQ probe simulation...", 13, 10, 0
msg_pass:           db " [PASS]", 13, 10, 0
msg_fail:           db " [FAIL]", 13, 10, 0
msg_skip:           db " [SKIP]", 13, 10, 0
msg_summary:        db "Test Summary:", 13, 10, 0
msg_total:          db "Total: ", 0
msg_passed:         db " Passed: ", 0
msg_failed:         db " Failed: ", 0
msg_skipped:        db " Skipped: ", 0
msg_newline:        db 13, 10, 0

SECTION .bss

; Test context storage
test_context:
    .current_test   resb 1
    .timeout_count  resw 1
    .error_code     resw 1
    .scratch_buffer resb 256

; Hardware simulation state
hw_sim_state:
    .nic_count      resb 1
    .current_nic    resb 1
    .detection_step resb 1
    .lfsr_state     resw 1
    .irq_pending    resb 8      ; IRQ flags for 8 possible NICs

SECTION .text

; Entry point for standalone test execution
main:
    ; Initialize test framework
    call init_test_framework
    
    ; Display test banner
    mov si, msg_start
    call print_string
    
    ; Run hardware detection test suite
    call run_hardware_detection_tests
    
    ; Display test summary
    call display_test_summary
    
    ; Exit with appropriate code
    mov al, [failed_count]
    cmp al, 0
    je .exit_success
    mov ax, 4C01h       ; Exit with error code 1
    int 21h
.exit_success:
    mov ax, 4C00h       ; Exit with success code 0
    int 21h

; Initialize test framework
init_test_framework:
    push ax
    push cx
    push di
    
    ; Clear test results
    mov di, test_results
    mov cx, MAX_TEST_RESULTS
    xor al, al
    rep stosb
    
    ; Reset counters
    mov word [test_count], 0
    mov word [passed_count], 0
    mov word [failed_count], 0
    mov word [skip_count], 0
    
    ; Initialize hardware simulation state
    mov byte [hw_sim_state.nic_count], 2    ; Simulate 2 NICs
    mov byte [hw_sim_state.current_nic], 0
    mov byte [hw_sim_state.detection_step], 0
    mov word [hw_sim_state.lfsr_state], 0x1357
    
    ; Clear IRQ pending flags
    mov di, hw_sim_state.irq_pending
    mov cx, 8
    xor al, al
    rep stosb
    
    pop di
    pop cx
    pop ax
    ret

; Main hardware detection test suite
run_hardware_detection_tests:
    push ax
    
    ; Test 1: LFSR sequence generation
    mov si, msg_lfsr_test
    call print_string
    call test_lfsr_sequence_generation
    call record_test_result
    
    ; Test 2: 3C509B detection simulation
    mov si, msg_3c509b_test
    call print_string
    call test_3c509b_detection
    call record_test_result
    
    ; Test 3: 3C515 detection simulation
    mov si, msg_3c515_test
    call print_string
    call test_3c515_detection
    call record_test_result
    
    ; Test 4: IRQ probe simulation
    mov si, msg_irq_test
    call print_string
    call test_irq_probe_simulation
    call record_test_result
    
    pop ax
    ret

; Test LFSR sequence generation for hardware detection
test_lfsr_sequence_generation:
    push ax
    push bx
    push cx
    push dx
    
    ; Initialize LFSR with known seed
    mov ax, [lfsr_seed]
    mov [hw_sim_state.lfsr_state], ax
    mov [lfsr_expected], ax
    
    ; Generate and validate LFSR sequence
    mov cx, LFSR_TEST_CYCLES
.lfsr_loop:
    ; Generate next LFSR value
    call lfsr_next_value
    mov [lfsr_actual], ax
    
    ; Validate sequence properties
    call validate_lfsr_sequence
    cmp ax, TEST_PASS
    jne .lfsr_fail
    
    loop .lfsr_loop
    
    ; All sequence validations passed
    mov ax, TEST_PASS
    jmp .lfsr_done
    
.lfsr_fail:
    mov ax, TEST_FAIL
    
.lfsr_done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Generate next LFSR value (16-bit LFSR with taps at positions 16, 14, 13, 11)
lfsr_next_value:
    push bx
    push cx
    
    mov ax, [hw_sim_state.lfsr_state]
    
    ; Extract feedback bits (positions 16, 14, 13, 11 = bits 15, 13, 12, 10)
    mov bx, ax
    shr bx, 15          ; bit 15
    mov cx, ax
    shr cx, 13
    and cx, 1           ; bit 13
    xor bx, cx
    
    mov cx, ax
    shr cx, 12
    and cx, 1           ; bit 12
    xor bx, cx
    
    mov cx, ax
    shr cx, 10
    and cx, 1           ; bit 10
    xor bx, cx
    
    ; Shift left and insert feedback bit
    shl ax, 1
    or ax, bx
    
    ; Update LFSR state
    mov [hw_sim_state.lfsr_state], ax
    
    pop cx
    pop bx
    ret

; Validate LFSR sequence properties
validate_lfsr_sequence:
    push bx
    push cx
    
    mov ax, [lfsr_actual]
    
    ; Test 1: Value should not be zero (LFSR should never generate 0)
    cmp ax, 0
    je .validate_fail
    
    ; Test 2: Check for reasonable distribution (not all bits same)
    mov bx, ax
    and bx, 0x5555      ; Odd bits
    cmp bx, 0
    je .check_even_bits
    cmp bx, 0x5555
    je .validate_fail
    
.check_even_bits:
    mov bx, ax
    and bx, 0xAAAA      ; Even bits
    cmp bx, 0
    je .validate_pass
    cmp bx, 0xAAAA
    je .validate_fail
    
.validate_pass:
    mov ax, TEST_PASS
    jmp .validate_done
    
.validate_fail:
    mov ax, TEST_FAIL
    
.validate_done:
    pop cx
    pop bx
    ret

; Test 3C509B hardware detection simulation
test_3c509b_detection:
    push ax
    push bx
    push dx
    
    ; Simulate 3C509B activation sequence
    mov dx, [mock_3c509b_config]    ; I/O base address
    
    ; Step 1: Send ID sequence to activate card
    call simulate_3c509b_id_sequence
    cmp ax, TEST_PASS
    jne .detection_3c509b_fail
    
    ; Step 2: Read product ID from activated card
    call simulate_3c509b_id_read
    cmp ax, TEST_PASS
    jne .detection_3c509b_fail
    
    ; Step 3: Read MAC address
    call simulate_3c509b_mac_read
    cmp ax, TEST_PASS
    jne .detection_3c509b_fail
    
    ; Step 4: Verify link status
    call simulate_3c509b_link_check
    cmp ax, TEST_PASS
    jne .detection_3c509b_fail
    
    ; All 3C509B detection steps passed
    mov ax, TEST_PASS
    jmp .detection_3c509b_done
    
.detection_3c509b_fail:
    mov ax, TEST_FAIL
    
.detection_3c509b_done:
    pop dx
    pop bx
    pop ax
    ret

; Simulate 3C509B ID sequence transmission
simulate_3c509b_id_sequence:
    push bx
    push cx
    push dx
    
    ; 3C509B requires specific bit pattern sent to 0x100-0x1F0 range
    mov dx, 0x100       ; ID port start
    mov cx, 255         ; Pattern length
    
.id_sequence_loop:
    ; Generate ID pattern bit
    call lfsr_next_value
    and ax, 1           ; Use LSB as pattern bit
    
    ; Simulate sending bit to ID port
    out dx, al
    
    ; Small delay simulation
    call short_delay
    
    ; Move to next ID port
    inc dx
    cmp dx, 0x110       ; ID port range end
    jle .continue_sequence
    mov dx, 0x100       ; Wrap around
    
.continue_sequence:
    loop .id_sequence_loop
    
    ; ID sequence completed successfully
    mov ax, TEST_PASS
    
    pop dx
    pop cx
    pop bx
    ret

; Simulate 3C509B product ID read
simulate_3c509b_id_read:
    push bx
    push dx
    
    ; Simulate reading from EEPROM location 0
    mov dx, [mock_3c509b_config]
    add dx, 8           ; EEPROM command register offset
    
    ; Send read command for address 0 (product ID)
    mov ax, 0x80        ; Read command + address 0
    out dx, ax
    
    ; Wait for EEPROM read completion
    call simulate_eeprom_wait
    
    ; Read product ID value
    mov dx, [mock_3c509b_config]
    add dx, 12          ; EEPROM data register offset
    in ax, dx
    
    ; Verify product ID matches 3C509B
    cmp ax, [mock_3c509b_config + 8]    ; Expected product ID
    je .id_read_pass
    
    mov ax, TEST_FAIL
    jmp .id_read_done
    
.id_read_pass:
    mov ax, TEST_PASS
    
.id_read_done:
    pop dx
    pop bx
    ret

; Simulate 3C509B MAC address read
simulate_3c509b_mac_read:
    push ax
    push bx
    push cx
    push dx
    push si
    
    ; Read 6-byte MAC address from EEPROM locations 0-2
    mov si, mock_3c509b_config + 2  ; MAC address in config
    mov cx, 3           ; 3 words (6 bytes)
    mov bx, 0           ; EEPROM address counter
    
.mac_read_loop:
    ; Simulate EEPROM read for MAC address word
    mov dx, [mock_3c509b_config]
    add dx, 8           ; EEPROM command register
    mov ax, 0x80        ; Read command
    or ax, bx           ; Add EEPROM address
    out dx, ax
    
    call simulate_eeprom_wait
    
    ; Read MAC word
    mov dx, [mock_3c509b_config]
    add dx, 12          ; EEPROM data register
    in ax, dx
    
    ; Compare with expected MAC bytes (little-endian)
    cmp al, [si]
    jne .mac_read_fail
    cmp ah, [si+1]
    jne .mac_read_fail
    
    add si, 2           ; Next MAC word
    inc bx              ; Next EEPROM address
    loop .mac_read_loop
    
    ; All MAC bytes matched
    mov ax, TEST_PASS
    jmp .mac_read_done
    
.mac_read_fail:
    mov ax, TEST_FAIL
    
.mac_read_done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Simulate 3C509B link status check
simulate_3c509b_link_check:
    push bx
    push dx
    
    ; Read link status from media status register
    mov dx, [mock_3c509b_config]
    add dx, 14          ; Media status register offset
    in ax, dx
    
    ; Check link beat bit (bit 11)
    test ax, 0x0800
    jz .link_check_fail
    
    ; Link is up
    mov ax, TEST_PASS
    jmp .link_check_done
    
.link_check_fail:
    mov ax, TEST_FAIL
    
.link_check_done:
    pop dx
    pop bx
    ret

; Test 3C515 hardware detection simulation  
test_3c515_detection:
    push ax
    push bx
    push dx
    
    ; 3C515 uses PCI-like detection - simpler than 3C509B
    mov dx, [mock_3c515_config]    ; I/O base address
    
    ; Step 1: Check if I/O ports respond
    call simulate_3c515_io_check
    cmp ax, TEST_PASS
    jne .detection_3c515_fail
    
    ; Step 2: Read vendor/device ID
    call simulate_3c515_id_read
    cmp ax, TEST_PASS  
    jne .detection_3c515_fail
    
    ; Step 3: Verify DMA capabilities
    call simulate_3c515_dma_check
    cmp ax, TEST_PASS
    jne .detection_3c515_fail
    
    ; Step 4: Check link status
    call simulate_3c515_link_check
    cmp ax, TEST_PASS
    jne .detection_3c515_fail
    
    ; All 3C515 detection steps passed
    mov ax, TEST_PASS
    jmp .detection_3c515_done
    
.detection_3c515_fail:
    mov ax, TEST_FAIL
    
.detection_3c515_done:
    pop dx
    pop bx
    pop ax
    ret

; Simulate 3C515 I/O port response check
simulate_3c515_io_check:
    push bx
    push cx
    push dx
    
    mov dx, [mock_3c515_config]    ; Base I/O address
    
    ; Try to read from status register
    add dx, 14          ; Status register offset
    in ax, dx
    
    ; Check if we get a reasonable response (not 0xFFFF)
    cmp ax, 0xFFFF
    je .io_check_fail
    
    ; Write test pattern to command register
    sub dx, 14          ; Back to base
    add dx, 12          ; Command register offset
    mov ax, 0x0000      ; Null command
    out dx, ax
    
    ; Small delay
    call short_delay
    
    ; Read back - should not be 0xFFFF
    in ax, dx
    cmp ax, 0xFFFF
    je .io_check_fail
    
    mov ax, TEST_PASS
    jmp .io_check_done
    
.io_check_fail:
    mov ax, TEST_FAIL
    
.io_check_done:
    pop dx
    pop cx
    pop bx
    ret

; Simulate 3C515 vendor/device ID read
simulate_3c515_id_read:
    push bx
    push dx
    
    ; Read device ID from internal ID register
    mov dx, [mock_3c515_config]
    add dx, 16          ; Device ID register offset  
    in ax, dx
    
    ; Verify device ID matches 3C515
    cmp ax, [mock_3c515_config + 8]    ; Expected device ID
    je .id_read_3c515_pass
    
    mov ax, TEST_FAIL
    jmp .id_read_3c515_done
    
.id_read_3c515_pass:
    mov ax, TEST_PASS
    
.id_read_3c515_done:
    pop dx
    pop bx
    ret

; Simulate 3C515 DMA capability check
simulate_3c515_dma_check:
    push bx
    push dx
    
    ; Check DMA command register exists and responds
    mov dx, [mock_3c515_config]
    add dx, 32          ; DMA control register offset
    
    ; Write test pattern
    mov ax, 0x0001      ; DMA enable bit
    out dx, ax
    
    call short_delay
    
    ; Read back
    in ax, dx
    and ax, 0x0001      ; Check DMA enable bit
    cmp ax, 0x0001
    je .dma_check_pass
    
    mov ax, TEST_FAIL
    jmp .dma_check_done
    
.dma_check_pass:
    ; Clear DMA enable for safety
    mov ax, 0x0000
    out dx, ax
    mov ax, TEST_PASS
    
.dma_check_done:
    pop dx
    pop bx
    ret

; Simulate 3C515 link status check
simulate_3c515_link_check:
    push bx
    push dx
    
    ; Read link status from network diagnostic register
    mov dx, [mock_3c515_config]
    add dx, 18          ; Network diagnostic register
    in ax, dx
    
    ; Check link status bit (bit 13)
    test ax, 0x2000
    jz .link_check_3c515_fail
    
    ; Link is up
    mov ax, TEST_PASS
    jmp .link_check_3c515_done
    
.link_check_3c515_fail:
    mov ax, TEST_FAIL
    
.link_check_3c515_done:
    pop dx
    pop bx
    ret

; Test IRQ probe simulation
test_irq_probe_simulation:
    push ax
    push bx
    push cx
    
    ; Test IRQ detection for both mock NICs
    mov bx, 0           ; Start with NIC 0
    mov cx, 2           ; Test 2 NICs
    
.irq_test_loop:
    push bx
    push cx
    
    ; Simulate IRQ probe for current NIC
    call simulate_irq_probe
    cmp ax, TEST_PASS
    jne .irq_test_fail
    
    pop cx
    pop bx
    inc bx              ; Next NIC
    loop .irq_test_loop
    
    ; All IRQ probes successful
    mov ax, TEST_PASS
    jmp .irq_test_done
    
.irq_test_fail:
    pop cx
    pop bx
    mov ax, TEST_FAIL
    
.irq_test_done:
    pop cx
    pop bx
    pop ax
    ret

; Simulate IRQ probe for specific NIC
simulate_irq_probe:
    push ax
    push bx
    push cx
    push dx
    
    ; Determine which NIC configuration to use
    cmp bx, 0
    je .use_3c509b_irq
    ; Use 3C515 configuration
    mov al, [mock_3c515_config + 2] ; IRQ from 3C515 config
    mov dx, [mock_3c515_config]     ; I/O base
    jmp .start_irq_probe
    
.use_3c509b_irq:
    mov al, [mock_3c509b_config + 2] ; IRQ from 3C509B config  
    mov dx, [mock_3c509b_config]     ; I/O base
    
.start_irq_probe:
    ; Store expected IRQ
    mov [test_context.error_code], ax
    
    ; Simulate enabling interrupts on the card
    add dx, 10          ; Interrupt enable register offset
    mov ax, 0x8000      ; Enable all interrupts
    out dx, ax
    
    ; Generate test interrupt
    call simulate_test_interrupt
    
    ; Check if interrupt was detected
    call check_simulated_interrupt
    cmp ax, TEST_PASS
    jne .irq_probe_fail
    
    ; Clear interrupt enable
    mov ax, 0x0000
    out dx, ax
    
    mov ax, TEST_PASS
    jmp .irq_probe_done
    
.irq_probe_fail:
    mov ax, TEST_FAIL
    
.irq_probe_done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Simulate test interrupt generation
simulate_test_interrupt:
    push ax
    push bx
    
    ; Set IRQ pending flag for current NIC
    mov bx, [hw_sim_state.current_nic]
    mov byte [hw_sim_state.irq_pending + bx], 1
    
    ; Simulate small delay for interrupt processing
    call short_delay
    
    pop bx
    pop ax
    ret

; Check if simulated interrupt was detected
check_simulated_interrupt:
    push bx
    
    ; Check IRQ pending flag for current NIC
    mov bx, [hw_sim_state.current_nic]
    cmp byte [hw_sim_state.irq_pending + bx], 1
    je .interrupt_detected
    
    mov ax, TEST_FAIL
    jmp .check_interrupt_done
    
.interrupt_detected:
    ; Clear the flag
    mov byte [hw_sim_state.irq_pending + bx], 0
    mov ax, TEST_PASS
    
.check_interrupt_done:
    pop bx
    ret

; Record test result and update counters
record_test_result:
    push bx
    push si
    
    mov bx, [test_count]
    cmp bx, MAX_TEST_RESULTS
    jge .record_done        ; Skip if too many tests
    
    ; Store result
    mov [test_results + bx], al
    
    ; Update counters based on result
    cmp al, TEST_PASS
    je .record_pass
    cmp al, TEST_FAIL  
    je .record_fail
    cmp al, TEST_SKIP
    je .record_skip
    jmp .record_error
    
.record_pass:
    inc word [passed_count]
    mov si, msg_pass
    jmp .record_print
    
.record_fail:
    inc word [failed_count]
    mov si, msg_fail
    jmp .record_print
    
.record_skip:
    inc word [skip_count]
    mov si, msg_skip
    jmp .record_print
    
.record_error:
    inc word [failed_count]  ; Count errors as failures
    mov si, msg_fail
    
.record_print:
    call print_string
    inc word [test_count]
    
.record_done:
    pop si
    pop bx
    ret

; Display comprehensive test summary
display_test_summary:
    push ax
    push dx
    
    ; Print summary header
    mov si, msg_summary
    call print_string
    
    ; Print total tests
    mov si, msg_total
    call print_string
    mov ax, [test_count]
    call print_decimal
    
    ; Print passed tests
    mov si, msg_passed
    call print_string
    mov ax, [passed_count]
    call print_decimal
    
    ; Print failed tests
    mov si, msg_failed
    call print_string
    mov ax, [failed_count]
    call print_decimal
    
    ; Print skipped tests
    mov si, msg_skipped
    call print_string
    mov ax, [skip_count]
    call print_decimal
    
    mov si, msg_newline
    call print_string
    
    pop dx
    pop ax
    ret

; Utility functions

; Simulate EEPROM wait delay
simulate_eeprom_wait:
    push cx
    mov cx, 100         ; Short delay loop
.eeprom_wait_loop:
    loop .eeprom_wait_loop
    pop cx
    ret

; Short delay for simulation
short_delay:
    push cx
    mov cx, 10
.delay_loop:
    loop .delay_loop
    pop cx
    ret

; Print string pointed to by SI
print_string:
    push ax
    push si
.print_loop:
    lodsb
    cmp al, 0
    je .print_done
    mov ah, 0x0E        ; BIOS teletype function
    int 0x10
    jmp .print_loop
.print_done:
    pop si
    pop ax
    ret

; Print decimal number in AX
print_decimal:
    push ax
    push bx  
    push cx
    push dx
    
    mov cx, 0           ; Digit counter
    mov bx, 10          ; Base 10
    
    ; Handle zero case
    cmp ax, 0
    jne .convert_digits
    push 48             ; ASCII '0'
    inc cx
    jmp .print_digits
    
.convert_digits:
    cmp ax, 0
    je .print_digits
    xor dx, dx
    div bx              ; AX = AX/10, DX = remainder
    add dl, 48          ; Convert to ASCII
    push dx             ; Push digit onto stack
    inc cx
    jmp .convert_digits
    
.print_digits:
    cmp cx, 0
    je .decimal_done
    pop ax              ; Get digit from stack
    mov ah, 0x0E        ; BIOS teletype
    int 0x10
    dec cx
    jmp .print_digits
    
.decimal_done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret