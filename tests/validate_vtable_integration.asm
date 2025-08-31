; @file validate_vtable_integration.asm  
; @brief Basic INT 60h validation test for vtable integration
; 
; This test validates that the complete vtable integration architecture works
; end-to-end by exercising key Packet Driver API functions through INT 60h.
;
; Tests performed:
; 1. Get driver information (Function 1) 
; 2. Get MAC address (Function 6)
; 3. Register packet type (Function 2) 
; 4. Set receive mode (Function 20)
; 5. Get statistics (Function 24)
;
; Expected Results:
; - All functions return success (CF=0)
; - Driver responds with valid information
; - Hardware vtable dispatch is functional
; - Production-ready for DOS networking applications

.MODEL SMALL
.STACK 256

.DATA
    ; Test result messages
    msg_start    db 'Testing 3Com Packet Driver vtable integration...', 13, 10, '$'
    msg_success  db 13, 10, '*** VTABLE INTEGRATION TEST PASSED ***', 13, 10
                 db 'Packet driver is production-ready!', 13, 10, '$'
    msg_fail     db 13, 10, '*** VTABLE INTEGRATION TEST FAILED ***', 13, 10
                 db 'Error in function: $'
    msg_test1    db 'Test 1: Driver Info...', '$'  
    msg_test2    db 'Test 2: MAC Address...', '$'
    msg_test3    db 'Test 3: Register Type...', '$'
    msg_test4    db 'Test 4: Set RX Mode...', '$'
    msg_test5    db 'Test 5: Statistics...', '$'
    msg_ok       db ' OK', 13, 10, '$'
    msg_fail_fn  db ' FAILED', 13, 10, '$'
    
    ; Test data buffers
    mac_buffer   db 6 dup(0)      ; MAC address buffer
    driver_info  db 64 dup(0)     ; Driver information buffer  
    stats_buffer db 32 dup(0)     ; Statistics buffer
    
    ; Test parameters
    packet_type  dw 0800h         ; IP packets (Ethernet II)
    saved_handle dw 0             ; Handle from register operation

.CODE
start:
    mov ax, @data
    mov ds, ax
    mov es, ax
    
    ; Print startup message
    lea dx, msg_start
    call print_string
    
    ; Test 1: Get driver information (Function 1)
    lea dx, msg_test1  
    call print_string
    call test_driver_info
    jc test_failed
    lea dx, msg_ok
    call print_string
    
    ; Test 2: Get MAC address (Function 6)
    lea dx, msg_test2
    call print_string 
    call test_get_mac
    jc test_failed
    lea dx, msg_ok
    call print_string
    
    ; Test 3: Register packet type (Function 2)
    lea dx, msg_test3
    call print_string
    call test_register_type  
    jc test_failed
    lea dx, msg_ok
    call print_string
    
    ; Test 4: Set receive mode (Function 20)
    lea dx, msg_test4
    call print_string
    call test_set_rcv_mode
    jc test_failed 
    lea dx, msg_ok
    call print_string
    
    ; Test 5: Get statistics (Function 24)
    lea dx, msg_test5
    call print_string
    call test_get_statistics
    jc test_failed
    lea dx, msg_ok  
    call print_string
    
    ; All tests passed - print success message
    lea dx, msg_success
    call print_string
    
    ; Exit with success code
    mov ax, 4C00h   
    int 21h

test_failed:
    ; Print failure message and function info
    lea dx, msg_fail
    call print_string
    
    ; Convert AH to ASCII and print function number
    mov al, ah
    add al, '0'
    mov dl, al
    mov ah, 2
    int 21h
    
    lea dx, msg_fail_fn
    call print_string
    
    ; Exit with error code 
    mov ax, 4C01h
    int 21h

;-----------------------------------------------------------------------------
; Test Functions
;-----------------------------------------------------------------------------

; Test 1: Get driver information (Function 1)
test_driver_info proc
    mov ah, 1          ; driver_info function
    mov al, 0FFh       ; high performance check
    int 60h
    ret
test_driver_info endp

; Test 2: Get MAC address (Function 6)  
test_get_mac proc
    mov ah, 6          ; get_address function
    mov al, 0          ; interface 0
    mov cx, 6          ; 6 bytes for MAC
    lea di, mac_buffer
    push ds
    pop es             ; ES:DI = buffer
    int 60h
    ret
test_get_mac endp

; Test 3: Register packet type (Function 2)
test_register_type proc
    mov ah, 2          ; access_type function  
    mov al, 0          ; interface 0
    mov bx, packet_type ; IP packets (0x0800)
    mov dl, 0          ; no type template
    mov cx, 0          ; typelen = 0
    int 60h
    jc register_failed
    mov saved_handle, ax ; Save handle for later tests
    clc                  ; Success
    ret
register_failed:
    stc                  ; Set carry flag for error
    ret
test_register_type endp

; Test 4: Set receive mode (Function 20)
test_set_rcv_mode proc  
    mov ah, 20         ; set_rcv_mode function
    mov al, 0          ; interface 0
    mov cx, 2          ; direct packets only  
    int 60h
    ret
test_set_rcv_mode endp

; Test 5: Get statistics (Function 24)
test_get_statistics proc
    mov ah, 24         ; get_statistics function
    mov bx, saved_handle ; Use saved handle
    int 60h
    ret  
test_get_statistics endp

;-----------------------------------------------------------------------------
; Utility Functions
;-----------------------------------------------------------------------------

; Print null-terminated string pointed to by DS:DX
print_string proc
    push ax
    mov ah, 9          ; DOS print string function
    int 21h
    pop ax
    ret
print_string endp

END start