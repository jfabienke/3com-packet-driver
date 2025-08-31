; code.asm - Main Code Section for 3c5x9pd.asm
;
; This file contains the primary logic of the packet driver.
; It includes functions for initialization, packet transmission/reception,
; interrupt handling, and command-line argument parsing.


;Hardware Definitions:
_3C509B_COMMAND_REG_ADDR    equ 0x29c ; Temporary definition, replace with proper label in 3c509b.h
_3C509B_STATUS_CONTROL_REG  equ 0x26d ; Temporary definition, replace with proper label in 3c509b.h
_3C509B_INTERRUPT_MASK      equ 0x270 ; Temporary definition, replace with proper label in 3c509b.h
_3C509B_ID_PORT				equ 0x25e
_3C509B_MIN_PACKET_SIZE     equ 0x3c  ; Load with minimium packet

;Constants


; Initialize something (likely a status flag)
mov byte [bp+0x5], 0x1

; Call a subroutine (probably for initialization)
call sub_8e7  ; (Will be defined later)
jc init_fail    ; Jump if carry flag is set (indicating an error)

; Get a value (likely a parameter or status)
mov al, [bx+0xe]
mov [bp+0x5], al    ; Store it

jmp short arg_parse_done

init_fail:
; Set up stack frame variables (likely for argument parsing)
mov word [bp+0x2], 0xc
mov word [bp+0x6], 0x5e
mov byte [bp+0x4], 0x0
mov byte [bp+0x0], 0x6
mov ax, cs
mov [bp+0xe], ax
mov word [bp+0xa], 0x105
jmp sub_9cf  ; Jump to a subroutine (likely for argument handling)

; --- Argument Parsing and Driver Setup ---
arg_parse_done:
mov byte [bp+0x7], 0x3  ; Set a flag/variable
jmp short set_flag_done

set_flag_option2:
mov byte [bp+0x7], 0x2
jmp short set_flag_done

set_flag_option4:
mov byte [bp+0x7], 0x4
jmp short set_flag_done

set_flag_option9:
mov byte [bp+0x7], 0x9
or word [bp+0x16], byte +0x1 ; Set a bit in a flag
jmp sub_9cf
; --- Check Command Line Arguments ---
check_args:
    mov ax, cs
    mov es, ax          ; Set ES to the code segment (for PSP access)
    mov si, 0x175       ; Offset to a string (possibly a command name)

check_arg_loop:
    lodsb               ; Load a byte from [cs:si] into AL
    test al, al         ; Check if it's the end of the string (null terminator)
    jz no_args          ; If end of string, no arguments were found

    cmp al, [bp+0x0]    ; Compare with expected argument character
    jnz check_next_arg  ; If not a match, check the next argument

    cmp word [bp+0x2], byte -0x1  ; Check special case
    jz process_arg

    cmp word [bp+0x2], byte +0x5e  ; Check if argument is 0x5E
    jnz check_arg_loop_inner           ;

    cmp byte [bp+0x6], 0x0       ; Check a flag
    jz process_arg_5e_0

    cmp byte [bp+0x6], 0x0      ;
    jnz check_arg_loop_inner

; --- (Code continues in the next chunk) ---
check_arg_loop_inner:   ;Added label since none existed and was jumped to.
check_cx:
    mov cx, [bp+0x4]
    cmp cx, byte +0x6      ; Check if CX is greater than 6
    ja check_arg_loop_inner

    mov bx, 0x2f7          ; Base address of a table/structure
    add bx, byte +0x13    ; Offset into the table
    cmp bx, 0x3c8          ; Check table bounds
    jnc check_arg_loop_inner

check_entry:
    cmp word [bx], byte +0x1  ; Check if the entry is 'active'
    jnz compare_pkt_int_invalid  ;

compare_pkt_int:
    mov al, [bp+0x0]
    cmp al, [bx+0xe]        ; compare al with byte ptr [bx + 0Eh]
    jnz invalid_pkt_int ;

    mov es, [bp+0xe]
    mov di, [bp+0xa]     ; Destination index
    lea si, [bx+0x2]      ; source index
    mov cx, [bx+0x8]
    repe cmpsb          ; Compare strings

    mov es, [0x178]      ; Restore ES
    jnz invalid_pkt_int

    mov byte [bp+0x7], 0xa ; Set flag indicating valid argument
    jmp short set_flag_done

compare_pkt_int_invalid: ;Renamed label
invalid_pkt_int:
; --- (Code continues in the next chunk) ---
process_arg:
    mov word [bx], 0x1     ; Mark entry as 'used'
    mov al, [bp+0x0]
    mov [bx+0xe], al       ; Store the packet interrupt number
    mov ax, [bp+0xc]
    mov [bx+0xc], ax       ; store segment
    mov si, [bp+0x8]
    mov [bx+0xa], si       ; store offset

    test word [0x248], 0x4 ; Check bit 2 (flag)  <-- Could be a #define
    jz skip_copy_params  ;

copy_params:
    mov ds, ax
    mov es, [cs:0x178] ; Load ES from a global variable
    lea di, [bx+0xf]
    mov cx, 0x4
    rep movsb       ; Copy 4 bytes of parameters

    mov ds, [cs:0x178]
    mov cx, [bp+0x4]  ;
    mov [bx+0x8], cx  ; store length of string
    mov si, [bp+0xa]
    mov ds, [bp+0xe]  ; load ds
    lea di, [bx+0x2]    ; Destination index
    rep movsb          ; copy string

    mov [bp+0x0], bx    ;
    mov ds, [cs:0x178]

skip_copy_params:

call_sub_917:
    call sub_917      ; Call a subroutine (likely for initialization)

init_packet_int_table:
    xor bx, bx
    mov bl, [0x25d]  ; Load packet interrupt number
    shl bx, 1       ; Multiply by 2 (size of a word)
    mov ax, [bx+0x24f] ; Get function pointer (word)

   ;Use hardware defition
    mov dx, [0x29c]   ; Load I/O port address  <-- Should use _3C509B_COMMAND_REG
    ;Correct statement once hardware definitions are included.
    ;mov dx, _3C509B_COMMAND_REG

    out dx, ax        ; Output the function pointer (init the card)
    jmp sub_9cf      ; Jump to common exit point

no_args:
    mov bx, [bp+0x2]
    call sub_8e7        ; Call a subroutine
    jc cleanup         ; Jump on carry (error)

    mov word [bx], 0x0
    call sub_900     ; Call another subroutine.
    jnz cleanup2      ; jump if not zero

    mov bx, 0x2
    mov ax, [bx+0x24f] ;
   ; Use hardware defition
    mov dx, [0x29c] ;  <-- Should use _3C509B_COMMAND_REG
    ;Correct statement once hardware definitions are included.
    ;mov dx, _3C509B_COMMAND_REG

    out dx, ax        ; output dx, ax

    jmp short cleanup2
    set_flag_done:
    mov byte [bp+0x7], 0x1   ; Set status flag to 1.
    or word [bp+0x16], byte +0x1
    jmp sub_9cf

cleanup:
    mov bx, [bp+0x2]
    call sub_8e7        ; Call a helper function.
    jc set_flag_and_exit_1a     ; Jump on carry (error)

    mov word [bx], 0x0 ;
    call sub_900     ;
    jnz set_flag_and_exit_2a ;

disable_interrupts_and_setup_vectors:
    cli ; Disable Interrupts

    ;Use hardware definitions for registers
    mov dx, [0x26d] ;  <-- Likely a status/control register.
    in al, dx
    or  al, [0x270]  ;  <-- Likely a mask for enabling interrupts.

    jmp short initial_set

initial_set: ; Added colon
    ;Use hardware definition for I/O registers:
    mov dx, [0x26d];
    out dx, al  ; Output to I/O port

    mov dx, [0x29c] ;    <-- Should use _3C509B_COMMAND_REG

    mov ax, 0x0  ; Command: Select window 0
    out dx, ax

    xor ax, ax
    mov es, ax          ; ES = 0 (for IVT access)
    mov di, 0x20        ; Offset for interrupt 8 vector

    mov ax, [0x290]      ; original int 8 vector
    mov [es:di], ax   ; store original offset
    mov ax, [0x292]   ;
    mov [es:di+0x2], ax   ; original segment

    ; Set the interrupt vectors
    xor ax, ax
    mov al, [0x24a]     ; Packet interrupt number
    mov di, ax
    shl di, byte 0x2    ; Multiply by 4 (size of an interrupt vector entry)
    mov ax, [0x24b]    ;
    mov [es:di], ax   ; new offset
    mov ax, [0x24d]
    mov [es:di+0x2], ax ; new segment

    ; also save original packet driver interrupt vector.
    xor ax, ax
    mov al, [0x26a]  ;
    mov di, ax
    shl di, byte 0x2  ; di = pkt_int * 4
    mov ax, [0x28c]  ;
    mov [es:di], ax    ; original offset
    mov ax, [0x28e]
    mov [es:di+0x2], ax ; original segment


    sti                 ; Enable interrupts

    ; Deallocate memory block
    mov ah, 0x49
    mov es, [0x178]     ; ES = segment of block to deallocate
    int 0x21            ; Call DOS service

    jmp sub_9cf         ; Jump to common exit
    set_flag_and_exit_1a:
    mov byte [bp+0x7], 0x7
    or word [bp+0x16], byte +0x1
    jmp sub_9cf

set_flag_and_exit_2a:
	mov byte [bp+0x7], 0x8
	or word [bp+0x16], byte +0x1
	jmp sub_9cf

check_len_args_main: ;Renamed label
    cmp cx, byte +0x6
    jc check_len_done_main

    mov cx, 0x6
    mov word [bp+0x4], 0x6
    mov es, [bp+0xc]
    mov si, 0x282 ;
    rep movsb

check_len_done_main:   ;Renamed label
	jmp sub_9cf

set_flag_and_exit_9a:  ;Renamed label
	mov byte [bp+0x7], 0x9
	or word [bp+0x16],byte +0x1
	jmp sub_9cf

set_flag_and_exit_fa:   ;Renamed label
	mov byte [bp+0x7], 0xf
	or word [bp+0x16], byte +0x1
	jmp sub_9cf

check_cx_2_main:       ;Renamed label
    call sub_900
    cmp cx, byte +0x2
    jnc check_cx_2_done_main
    jmp sub_9cf

check_cx_2_done_main:  ;Renamed label
	mov [bp+0xc], ds
	mov word [bp+0x8], 0x116
	jmp sub_9cf

set_flag_and_exit_ba:   ;Renamed label
	mov byte [bp+0x7], 0xb
	or word [bp+0x16], byte +0x1
	jmp sub_9cf
; --- (Code continues in the next chunk) ---
; Subroutine at 0x7b8
sub_7b8:
    mov bx, [bp+0x2]    ; Get parameter (likely a pointer)
    call sub_8e7         ; Call a subroutine
    jc sub_7b8_exit     ; Jump on carry (error)

    call sub_900         ;
    cmp cx, byte +0x1    ; Check return value
    mov bx, [bp+0x4]
    jna sub_7d7          ;
    cmp bl, [0x25d]
    jnz check_bx_nonzero  ;

sub_7d7:
    jmp sub_9cf

check_bx_nonzero:
    test bx, bx          ; Check if BX is zero
    jz check_bx_nonzero_exit     ; Jump if zero

    cmp bx, byte +0x6
    ja check_bx_nonzero_exit  ;

    mov [0x25d], bl ; store bl
    shl bx, 1
    ; Use hardware definition
    mov dx, [0x29c] ; command register   <-- Should use _3C509B_COMMAND_REG
	 ; Correct statement once hardware definitions are included.
    ;mov dx, _3C509B_COMMAND_REG
    mov ax, [bx+0x24f]    ;
    test ax, ax
    jz check_bx_nonzero_exit
    out dx, ax ;

check_bx_nonzero_exit:
    jmp sub_9cf ; Return

sub_7b8_exit:
    mov byte [bp+0x7], 0x1
    or word [bp+0x16], byte +0x1 ; Set flag
    jmp sub_9cf ;
    set_flag_8_and_exit:
    mov byte [bp+0x7], 0x8
    or word [bp+0x16], byte +0x1
    jmp sub_9cf

; Subroutine to get/clear the packet interrupt number.
sub_813:
    mov bx, [bp+0x2]
    call sub_8e7        ; Call a helper function
    jc sub_813_exit    ; Jump on carry (error)

    xor ax, ax
    mov al, [0x25d]     ; Load the packet interrupt number
    mov [bp+0x0], ax    ;

sub_813_exit:
    mov byte [bp+0x7], 0x1  ;
    or  word [bp+0x16], byte +0x1 ;
    jmp sub_9cf ;
check_divisible_by_6:
    mov cx, [bp+0x4]    ; Load parameter
    mov ax, cx
    mov bl, 0x6
    div bl              ; Divide AX by 6
    test ah, ah          ; Check remainder
    jnz check_divisible_by_6_exit    ; Jump if not divisible by 6

    cmp cx, byte +0x30   ;
    ja check_length  ;

    mov [0x174], al
    mov ds, [bp+0xc]
    mov si, [bp+0x8] ; si points to data
    mov di, 0x144  ;
    rep movsb        ; Copy data

    jmp sub_9cf ; Return

check_divisible_by_6_exit:
check_length:
    mov byte [bp+0x7], 0xe
    or  word [bp+0x16], byte +0x1
    jmp sub_9cf

set_flag_9_again:
    mov byte [bp+0x7], 0x9  ;
    or  word [bp+0x16], byte +0x1 ;
    jmp sub_9cf

set_data_segment:
    mov [bp+0xc], ds
    mov word [bp+0x8], 0x144

    mov al, [0x174]  ; al has the length now
    mov bl, 0x6
    mul bl  ; length * 6
    mov [bp+0x4], ax

    jmp sub_9cf

; Subroutine to get Ethernet address
sub_874:
    call sub_bf5            ; Call a subroutine (likely for EEPROM access)
    mov [bp+0xe], ds         ;
    mov word [bp+0xa], 0x124 ;

    jmp sub_9cf

set_flag_d_and_exit:
    mov byte [bp+0x7], 0xd ;
    or  word [bp+0x16], byte +0x1
    jmp sub_9cf ;

check_arg_len:
    cmp word [bp+0x4], byte +0x6  ; Check argument length
    jnz sub_899              ; Jump if not equal to 6

    call sub_900         ;
    cmp cx, byte +0x2    ;
    jnc set_media       ; Jump if CX >= 2

    ; Use hardware definition.
    mov dx, [0x29c]
	;Replace with correct label
    ;mov dx, _3C509B_COMMAND_REG

    mov ax, 0x802  ; Command: Select window 2   #TODO. Select window 2
    out dx, ax

    mov ds, [bp+0xc]  ; Restore DS
    mov si, [bp+0x8]   ; Source index
    add dx, byte -0xe    ; Adjust DX to point to station address registers
    mov cx, 0x6          ; 6 bytes (MAC address)

write_mac_loop:
    lodsb              ; Load a byte from [DS:SI]
    out dx, al         ; Write to the station address register
    inc dx             ; Increment I/O port
    loop write_mac_loop  ; Loop until CX is zero

   ; Use hardware definition for I/O register
    mov dx, [0x29c]   ; Command register   <-- Should use _3C509B_COMMAND_REG
	;Replace with correct label
    ;mov dx, _3C509B_COMMAND_REG
    mov ax, 0x801; Command: Select window 1   #TODO. Select window 1
    out dx, ax

    ; Copy MAC Address for use later.
    mov es, [cs:0x178]
    mov si, [bp+0x8]  ;
    mov di, 0x282 ; destination
    mov cx, 0x6
    rep movsb  ; move 6 bytes

    jmp sub_9cf

set_media:
    mov byte [bp+0x7], 0xe
    or word [bp+0x16], byte +0x1 ;
    jmp sub_9cf

sub_899:
	mov byte [bp+0x7],0xb
	or word [bp+0x16],byte +0x1
	jmp sub_9cf

; --- Subroutine at 0x8ee ---
; Checks if a given address is within the valid range of the packet driver's internal
; linked list of active buffers.
sub_8ee:
    mov si, 0x30a ; Start of the linked list

check_address_range:
    cmp bx, si     ; Compare BX (address to check) with current entry
    jc address_out_of_range  ; Jump if BX < SI (out of range)

    cmp bx, si     ; Check for exact match
    jz address_in_range     ; Jump if BX == SI (in range)

    add si, byte +0x13 ; Move to the next entry in the list (0x13 bytes per entry)
    cmp si, 0x3c8      ; Check if we've reached the end of the list
    jc check_address_range  ; Loop if SI < 0x3c8

address_out_of_range:
    stc               ; Set carry flag (indicating out of range)
    jmp short sub_8ff_exit

address_in_range:
    clc               ; Clear carry flag (indicating in range)

sub_8ff_exit:
    ret               ; Return

; --- Subroutine at 0x900 ---
; Scans a linked list of packet buffers (probably used for transmit buffers).
; Returns the number of 'active' buffers in CX.
sub_900:
    xor cx, cx            ; Initialize count to 0
    mov bx, 0x30a         ; Start of the linked list

scan_buffer_list:
    cmp word [bx], byte +0x1 ; Check if the buffer is 'active'
    jnz next_buffer         ; If not active, skip to the next buffer

    inc cx               ; Increment the count of active buffers

next_buffer:
    add bx, byte +0x13    ; Move to the next entry in the linked list (0x13 bytes per entry)
    cmp bx, 0x3c8          ; Check if we've reached the end of the list
    jc scan_buffer_list   ; Loop if BX < 0x3C8

    test cx, cx          ; Check if any active buffers were found
    ret                  ; Return (CX contains the count)

; --- Subroutine at 0x917 ---
; This subroutine likely manages a linked list of buffers, possibly
; used for transmit buffers. It appears to move entries around in the list
; and potentially copies data between them.

sub_917:
    mov bx, 0x30a       ; Start of the linked list
    xor si, si

check_buffer:
    cmp word [bx], byte +0x1  ; Check if the buffer is 'active'
    jnz try_next_buffer

    cmp word [bx+0x8], byte +0x0  ; Check if the length is 0
    jz try_next_buffer_inner

    test si, si
    jz move_to_end

shift_buffer:
    mov si, 0x3b5  ;
    cmp bx, si
    jnc do_copy

copy_to_end:
; copies current element to 0x3b5
    mov di, 0x3c8 ; destination
    mov cx, 0x13  ; length of entry
    push si
    rep movsb  ;
    pop di

    mov si, bx  ;
    mov cx, 0x13
    rep movsb ; copy from current to destination

    mov di, bx  ; source
    mov si, 0x3c8  ;
    mov cx, 0x13
    rep movsb ; copy from 0x3c8 to source.

    jmp short check_buffer

try_next_buffer:
    test si, si
    jnz try_next_buffer_inner
    mov si, bx ; store current location.
    add bx, byte +0x13  ;
    cmp bx, 0x3c8   ;
    jnc do_copy  ;
    jmp short check_buffer

try_next_buffer_inner:
move_to_end:
    test si, si
    jnz move_to_end_inner ;
    mov si, bx

move_to_end_inner:
    add bx, byte +0x13
    cmp bx, 0x3c8
    jnc do_copy
    jmp short check_buffer

do_copy:

check_tail:
    mov si, 0x3b5 ;
    cmp word [si], byte +0x1 ; check if active.
    jnz copy_done   ;

    mov di, 0x2f7 ; destination
    add di, byte +0x13  ;
    cmp di, si ; si = 0x3b5, di = 0x30a.  Check if reached end.
    jz copy_done ; if so, copy is done

    cmp word [di], byte +0x1 ;
    jz shift_tail ;

    mov cx, 0x13 ; copy length
    push si  ;
    rep movsb      ; Copy from SI to DI
    pop si  ;

    mov word [si], 0x0 ; reset to 0

copy_done:
    ret

shift_tail:
    mov cx, 0x13 ;
    push si
    rep movsb    ;
    pop si

    mov word [si], 0x0

    ret

; This section sets up the interrupt handlers.

; --- Interrupt Handler Dispatcher (at 0x98A) ---
;
; This is the main entry point for interrupts.  It saves registers,
; determines the interrupt source, and calls the appropriate handler.

interrupt_handler:
    add [bx+si], al          ; Placeholder instruction (likely NOP)
    add [bp+si+0x2d9a], bh  ; Placeholder instruction (likely NOP)
    call sub_1bb3          ; (Placeholder for now, will define later)
    mov bx, 0x27b5          ; (Placeholder for now)
    cmp byte [bx], 0x0
    jz handler_done

    mov dx, [bx+0x3]
    call sub_1bbc          ;
    add bx, byte +0x5
    jmp short handler_loop_start

handler_done:
    mov dx, 0x2f2b         ; (Placeholder for now)
    call sub_1bbc
    mov bx, 0x28b8        ; (Placeholder)
    cmp word [bx], 0x0    ;
    jz handler_loop_end

    mov dx, [bx+0x4]
    call sub_1bbc          ;
    add bx, byte +0x6     ;

handler_loop_start:
    jmp short handler_done

handler_loop_end:
    mov dx, 0x2fcc           ; (Placeholder)
    call sub_1bbc
    jmp short handler_exit

handler_exit:
    ; Placeholder - this should never be reached in the original code.
    ; The real exit is handled by the individual interrupt routines via IRET.

    iret  ; Should not reach here.

; --- Common Interrupt Handler Prologue ---
;
; This code is executed at the beginning of each interrupt handler.

interrupt_prologue:
    push bp                 ; Save BP
    mov  bp, sp             ; Set up stack frame
    and  word [bp+0x16], byte -0x2  ; Clear a bit in a flag

    mov  ax, cs             ; Get code segment
    mov  ds, ax             ; Set DS to code segment

    cmp  byte [0x104], 0x1   ; Check a flag (likely initialization status)
    jnz  interrupt_common_exit     ; Jump if not initialized

    xor  bx, bx             ; Clear BX
    mov  bl, [bp+0x1]       ; Load interrupt type (from stack)
    cmp  bx, byte +0x1c     ; Check if interrupt type is within range
    ja   interrupt_common_exit      ; Jump if out of range

    shl  bx, 1              ; Multiply by 2 (size of a word)
    jmp  [bx+jump_table]      ; Jump to the appropriate handler

interrupt_common_exit:
    pop  ax                 ; Restore registers
    pop  bx
    pop  cx
    pop  dx
    pop  di
    pop  si
    pop  es
    pop  ds
    pop  bp
    iret                    ; Return from interrupt


; --- Interrupt Handler Jump Table ---
;
; This table contains the addresses of the individual interrupt handlers.

jump_table:
    dw handler_0    ; 0x9FA
    dw handler_1    ; 0x9FC
    dw handler_2    ; 0x9FE
    dw handler_3    ; 0xA00
    dw handler_4    ; 0xA02
    dw handler_5    ; 0xA04
    dw handler_6    ; 0xA06
    dw handler_7    ; 0xA08
    dw handler_8    ; 0xA0A
    dw handler_9    ; 0xA0C
    dw handler_A    ; 0xA0E
    dw handler_B    ; 0xA10
    dw handler_C    ; 0xA12
    dw handler_D    ; 0xA14
    dw handler_E    ; 0xA16
    dw handler_F    ; 0xA18
    dw handler_10   ; 0xA1A
    dw handler_11   ; 0xA1C
    dw handler_12   ; 0xA1E
    dw handler_13   ; 0xA20
    dw handler_14   ; 0xA22
    dw handler_15   ; 0xA24
    dw handler_16   ; 0xA26
    dw handler_17   ; 0xA28
    dw handler_18   ; 0xA2A
    dw handler_19   ; 0xA2C
    dw handler_1A   ; 0xA2E
    dw handler_1B   ; 0xA30


; --- Placeholder Interrupt Handlers ---
;
; These are temporary placeholders.  The real interrupt handlers are
; defined later.
handler_0:  jmp handler_exit
handler_1:  jmp handler_exit
handler_2:  jmp handler_exit
handler_3:  jmp handler_exit
handler_4:  jmp handler_exit
handler_5:  jmp handler_exit
handler_6:  jmp handler_exit
handler_7:  jmp handler_exit
handler_8:  jmp handler_exit
handler_9:  jmp handler_exit
handler_A:  jmp handler_exit
handler_B:  jmp handler_exit
handler_C:  jmp handler_exit
handler_D:  jmp handler_exit
handler_E:  jmp handler_exit
handler_F:  jmp handler_exit
handler_10: jmp handler_exit
handler_11: jmp handler_exit
handler_12: jmp handler_exit
handler_13: jmp handler_exit
handler_14: jmp handler_exit
handler_15: jmp handler_exit
handler_16: jmp handler_exit
handler_17: jmp handler_exit
handler_18: jmp handler_exit
handler_19: jmp handler_exit
handler_1A: jmp handler_exit
handler_1B: jmp handler_exit

; --- Common Subroutine at 0x9CF ---
; Called with arguments in BP.
; It sets up a dispatcher
sub_9cf:
    push bp
    mov bp, sp
    and word [bp+0x16], byte -0x2  ;
    mov ax, cs
    mov ds, ax ;
    cmp byte [0x104], 0x1 ; Check driver initialized.
    jnz common_exit ;

    xor bx, bx ;
    mov bl, [bp+0x1] ; first argument
    cmp bx, byte +0x1c
    ja common_exit
    shl bx, 1 ;
    jmp [bx + jump_table] ; Call based on arguments

common_exit:
    pop bp
    ret

; --- Interrupt Handler for Timer (Interrupt 8) ---
;
; This handler is installed to the original timer interrupt vector (INT 8).
; It checks if the 3C509 card has generated an interrupt and, if so,
; calls the 3C509's interrupt handler.  It also handles some timing-related
; tasks.

timer_interrupt_handler:  ; 0xb04

    pusha                   ; Save all general-purpose registers
    push ds                  ; Save DS
    push es                  ; Save ES

    mov  ds, [cs:0x178]       ; Load DS with the driver's data segment

    cld                    ; Clear direction flag (for string instructions)

    mov  ax, [0x142]        ; Load a value (likely a counter)
    cmp  ax, 0x3e8          ; Compare with 1000
    jc   check_tx_fifo      ; Jump if less than 1000

    ; --- (Code continues in the next chunk) ---
        xor  bx, bx             ; Clear BX
    shl  ax, 1              ; Multiply AX by 2
    rcl  bx, 1              ; Rotate carry into BX (effectively multiplying by 2^16)
    shl  ax, 1              ; Multiply AX by 2 again
    rcl  bx, 1              ; Rotate carry into BX
    cmp  bx, [0x12a]      ;
    ja   clear_tx_window     ;

    cmp  ax, [0x128]      ;
    jc   check_tx_fifo ;
    ; Correct Hardware Definitions:
    mov  dx, [0x29c]  ; Load I/O port for command register <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG

    mov  ax, 0x804   ; Select window 4    #TODO. Select window 4
    out  dx, ax             ; Send command

    add  dx, byte -0x4      ; Point DX to W4 config register
    in   ax, dx             ; Read configuration
    and  ax, 0xfff7         ; Clear bit 3 (disable something)
    out  dx, ax             ; Write back modified configuration

    add  dx, byte +0x4     ; back to command register
    mov  ax, 0x801 ; Select window 1  #TODO. Select window 1
    out  dx, ax            ;

    ; timing related calculations
    mov  dx, [0x2e8]
    mov  ax, [0x2e6] ;
    div  word [0x2e2]    ; Divide AX by [0x2e2]
    mov  cx, ax            ;
    mov  ax, [0x2e4]     ;
    div  byte [0x2e2]     ;
    xor  ah, ah            ; Clear AH (remainder)
    mov  si, ax            ;
    mul  al                ;
    sub  cx, ax           ;
    mov  bx, cx            ;
    shr  bx, byte 0xc      ; Divide BX by 4096 (right shift by 12)
    mov  al, [bx+0x2ea]   ;
    xor  ah, ah            ; Clear AH
    test ax, ax            ; Check if AX is zero
    jnz  calc_fifo_addr   ;

    mov  ax, cx            ;
    shr  ax, byte 0x5     ; Divide AX by 32 (right shift by 5)
    add  ax, 0x2          ; Add 2
    mov  bx, ax             ;
    mov  ax, cx            ;
    div  bl                ; Divide AX by BL
    xor  ah, ah            ; Clear AH (remainder)
    add  bx, ax             ;
    add  bx, ax             ;
    xor  ah, ah            ; Clear AH
    add  ax, bx            ;
    add  ax, si            ;
    shl  ax, byte 0x2    ; multiply by 4
    mov  [0x2e0], ax     ;

    calc_fifo_addr:
    ; --- Decrement a counter, and check another one
    ;Correct Hardware Definitions:
    mov  dx, [0x29c] ; I/O port for the command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG

    dec  word [0x306]     ; Decrement a counter
    jnz  check_another_counter  ; Jump if not zero

    mov  ax, [0x302]        ; Load another counter
    mov  [0x306], ax        ; Reset the first counter
    cmp  word [0x140], byte +0x64 ; Compare counter with 100
    mov word [0x140], 0x0 ; reset
    jc   check_another_counter ; Jump if less than 100

    ; --- (Code continues in the next chunk) ---
    check_another_counter:

    add word [0x2db], byte +0x8; increment TX start threshold counter

    ; Call subroutine to adjust TX start threshold
    call sub_c93  ; (Will be defined later)

    cmp byte [0x46b], 0x1 ; Check if transmitting
    jz check_another_counter_exit  ; Jump if transmitting

    ; If not transmitting, initiate a transmission if there's a packet to send.
    mov ax, [0x2d9]    ; Load the TX start address
     ;Correct Hardware Definitions:
    mov  dx, [0x29c]   ; Load I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG

    out dx, ax          ; Send the address to the command register (starts TX)

    call sub_120a ; Call a subroutine (likely related to TX completion). Returns a value in AX.
    mov bx, ax ; store result
    mov si, 0x2af ; rx status address
    add si, byte +0xa  ; RX status register address + 10
    mov ax, bx  ; restore result
    sub ax, [si+0x4] ;
    cmp ax, [0x2b7]  ; Compare with a threshold/value.
    jc tx_complete ; Jump if below threshold (transmit complete?)

    call sub_1194 ;

    cmp si, 0x2cd  ;
    jnz check_another_counter_inner

check_another_counter_inner:
    cmp byte [0x44b], 0x0 ; Check if packet reception is enabled
    jz timer_interrupt_epilogue ; Jump if reception is disabled

check_another_counter_exit:
tx_complete:
    ; --- This jump target was inside the check_another_counter block
    ;     in the disassembled code, which is unusual.  It suggests the
    ;     transmit completion check might be part of a larger loop.
    ;     We'll keep the structure, but it's worth investigating further
    ;     with dynamic analysis if possible.

    ; --- (Continuing from previous chunk) ---

    ; Transmit Reset
     ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG

    mov ax, [0x2cd]  ; Command: TX Reset
    out dx, ax        ; Send command

timer_interrupt_epilogue:
    pop  es                  ; Restore ES
    pop  ds                  ; Restore DS
    popa                    ; Restore all general-purpose registers
    iret                    ; Return from interrupt

check_tx_fifo:
; --- Interrupt Handler for Packet Reception (INT 8 handler, continued)
;
; This section handles the actual reception of packets from the network card.

; Check if there's space in the FIFO.  First, switch to Window 6 (Statistics)
     ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; Load I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG

    mov  ax, 0x806  ; Select window 6 #TODO
    out  dx, ax             ; Send command

    ; The following block reads various statistic counters.  These are likely
    ; *not* directly related to the FIFO check, but are being read here
    ; opportunistically while Window 6 is selected. The original code might
    ; have done this for efficiency, to avoid switching windows repeatedly.

    mov  ah, 0xb0           ; Possibly a command related to statistics?
    out  dx, ah             ; Send command
    xor  ah, ah             ; Clear AH
    add  dx, byte -0xd     ; Point DX to a register in window 6. (TX Carrier Errors)
    in   al, dx             ; Read TX Carrier Errors
    add  dx, byte +0x1     ; (TX Heartbeat Errors)
    in   al, dx             ; Read TX Heartbeat Errors
    add  dx, byte +0x1     ; Point DX to another register in window 6. (TX Multiple Collisions)
    in   al, dx             ; Read TX Multiple Collisions
    add  dx, byte +0x2      ; (TX Window Errors)
    in   al, dx             ; Read TX Window Errors
    add  [0x13c], ax      ;
    adc  word [0x13e], byte +0x0;
    add  dx, byte +0x1     ; (RX FIFO Errors)
    in   al, dx             ;
    add  [0x128], ax      ; Add to a counter
    adc  word [0x12a], byte +0x0;
    add  dx, byte +0x1     ; (TX Packets)
    in   al, dx             ;
    add  [0x124], ax      ; Add to another counter
    adc  word [0x126], byte +0x0 ;
    add  dx, byte +0x3     ; (TX Octets, low byte)
    in   ax, dx             ;
    add  [0x12c], ax      ; Add to another counter
    adc  word [0x12e], byte +0x0;
    add  dx, byte +0x2      ; (RX Octets, low byte)
    in   ax, dx             ; Read from I/O port
    add  [0x130], ax       ; Add to a counter
    adc  word [0x132], byte +0x0 ; Add carry to the high word of the counter

    ; --- Go back to window 1 to check for received packets.
    ;Correct Hardware Definitions:
    mov  dx, [0x29c]   ; I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG

    mov  ax, 0x801  ; Select window 1 #TODO
    out  dx, ax              ; Send command

    ; --- Check TX Status (this seems out of place in the RX handler)
    add  dx, byte -0x3       ; Point DX to the TX status register (_3C509B_TX_STATUS)
    in   al, dx             ; Read TX status
    add  dx, byte +0x3       ; Point DX to status register
    test al, al             ; Test AL (check for zero/sign flag)
    js   read_rx_status      ; Jump if sign flag is set (negative) - meaning TX error?

; --- Check if we have received any packets (finally!)
    mov  ax, 0x801  ; Select window 1  #TODO: Select window 1 Redundant
    out  dx, ax             ; Switch back to Window 1 (if not already there)

    mov  ax, 0x5800   ; Enable Statistics (0x5800)
    out  dx, ax              ; Send command

    ; Check RX Status
    add  dx, byte -0x8       ; Point DX to RX status register (_3C509B_RX_STATUS)
    in   ax, dx             ; Read RX status
    test ax, 0x200          ; Test bit 9 (RX_EARLY?)  <-- Could be a named constant.
    jnz  rx_early_or_error   ; Jump if bit is set

    add  dx, byte +0x8      ; Restore DX to command register
    mov  ax, 0x801  ; Select window 1 #TODO
    out  dx, ax            ;

    ; --- Check for pending interrupts (even if no packet ready)
check_pending_interrupts:
    pop  ax                   ; Restore AX (from the RX status read)
    test al, 0x1   ; Test bit 0 (Interrupt Latch)  #TODO
    jz   no_interrupt_pending      ; Jump if no interrupt

; handle the interrupts

    ; Increment a counter (likely related to interrupt count)
    add  word [0x138], byte +0x1
    adc  word [0x13a], byte +0x0 ;

    ; --- Call a subroutine (likely for additional interrupt processing)
    call sub_122e    ; (Will be defined later)

    ; Check if the source and destination are equal.
    mov ax, cs ;
    cmp ax, [cs:0x2af] ;
    jnz process_queued_packet ;

    cmp word [cs:0x2ad], 0x2ad  ; Check if destination address is 0x2ad:cs
    jz send_tx_reset          ; If so, jump.

process_queued_packet:

    les di, [0x2ad]  ;
    mov ax, [es:di]  ; ax = word ptr es:[di]
    mov [es:di+0x14], ax ; word ptr es:[di+14h] = ax

    ; --- Return to main interrupt handler
    ret ; will return to the interrupt_prologue ret.

send_tx_reset:
    ; --- Send TX reset
    mov  ax, [0x2c3]   ;  (Likely a TX control register value)
     ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    out  dx, ax         ; Send command to reset TX
    and  word [0x298], 0xfb  ; clear bit.
    mov  ax, [0x298]   ;  (Likely a status register)
    out  dx, ax         ; Send command
    les  di, [0x2ad]
    mov  ax, [es:di]
    mov [es:di+0x14], ax

    add  dx, byte -0x2      ; Adjust DX
    push word return_after_tx_reset ; Push return address
    jmp dispatch_table_rx_handler_part_2 ; Jump to another part of the RX handler

return_after_tx_reset:

check_rx_status_again:
    cmp byte [0x265], 0x3 ; check media
    jz rx_handler_part2_dispatch

    ; Call a subroutine (possibly related to media type)
    call sub_166b    ; (Will be defined later)
    jmp short return_after_tx_reset ;

rx_handler_part2_dispatch:
dispatch_table_rx_handler_part_2:

    ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    mov  ax, 0x801  ; Select window 1    #TODO
    out  dx, ax

    ; Set TX Start
    mov  ah, 0x50    ; TX start #TODO
    out  dx, ax      ;

    mov ah, 0x18   ; set rx filter. #TODO
    out dx, ax

    add dx, byte +0x0  ; added #TODO
    mov ax, 0x7800  ; #TODO
    out dx, ax  ;
    mov [0x298], ax ; store
    mov byte [0x2ac], 0x1  ;

    jmp short return_after_tx_reset

rx_early_or_error:
    call sub_bf5            ; Call a subroutine (likely related to EEPROM or diagnostics)

no_interrupt_pending:
    jmp return_after_tx_reset

; --- Subroutine at 0xA5C (Part of Packet Reception Handling) ---
; This section appears to be a continuation of the packet reception
; logic, reached via a jump from within the interrupt handler.

sub_a5c:
    jmp sub_a5c  ; Infinite loop? (Will be patched later)

; --- (More code related to the jump table will go here later) ---

; Original Address: 0xAD1
; --- Subroutine: Process Received Packet ---
;
; This routine handles a packet that has been received by the network card.
; It checks for errors, updates status information, and potentially
; passes the packet to a higher-level protocol handler.

process_received_packet:
    pushf                   ; Push flags
    call far [cs:0x290]    ; Call the original interrupt 8 handler

    pusha                   ; Save all general-purpose registers
    push ds                  ; Save DS
    push es                  ; Save ES

    mov  ds, [cs:0x178]     ; Load DS with the driver's data segment
    cld                     ; Clear direction flag

    ; --- Check if we have enough free buffers ---
    mov  ax, [0x142]     ; Load a value (likely a free buffer counter or pointer)
    cmp  ax, 0x3e8    ;
    jc   receive_packet_copy     ; Jump if less than 0x3E8 (enough buffers)

    ; --- (Code to handle buffer exhaustion will likely go here) ---
    ; --- Calculate buffer indices
	xor bx,bx ;
	shl ax,1 ;
	rcl bx,1 ;
	shl ax,1 ;
	rcl bx,1
	cmp bx,[0x12a] ;
	ja free_packet_buffer

	cmp ax,[0x128]
	jc receive_packet_copy

    ; --- Select Window 4
       ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG

    mov  ax, 0x804   ; Select window 4 #TODO
    out  dx, ax             ; Send command

    ; --- Modify network configuration (related to window 4)
    add  dx, byte -0x4     ; Point DX to W4 config register (_3C509B_W4_NETDIAG)
    in   ax, dx             ; Read config
    and  ax, 0xfff7       ; Clear bit 3
    out  dx, ax            ; Write back modified config

	add dx,byte +0x4 ; Select Window 1.
     ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG

	mov ax,0x801   ; Select window 1    #TODO
	out dx,ax

    ; --- Calculate packet length and address ---
    mov  dx, [0x2e8]  ;
    mov  ax, [0x2e6]    ;
    div  word [0x2e2]   ;
    mov  cx, ax        ;
    mov  ax, [0x2e4]   ;
    div  byte [0x2e2]   ;
    xor  ah, ah          ; Clear AH (remainder)
    mov  si, ax         ;
    mul  al
    sub  cx, ax            ;
    mov  bx, cx            ;
    shr  bx, byte 0xc      ; Divide BX by 4096 (right shift by 12)
    mov  al, [bx+0x2ea]  ;
    xor  ah, ah            ; Clear AH
    test ax, ax            ; Check if AX is zero
    jnz  continue_calc     ; Jump if not zero

    ; --- (Code continues in the next chunk) ---
    continue_calc:
    mov  ax, cx               ;
    shr  ax, byte 0x5          ; Divide AX by 32 (right shift by 5)
    add  ax, 0x2              ;
    mov  bx, ax              ;
    mov  ax, cx              ;
    div  bl                  ; Divide AX by BL
    xor  ah, ah              ; Clear AH (remainder)
    add  bx, ax               ;
    add  bx, ax              ;
    xor  ah, ah                ; Clear AH
    add  ax, bx             ;
    add  ax, si             ;
    shl  ax, byte 0x2        ; Multiply AX by 4
    mov  [0x2e0], ax          ; Store result

    ; --- Decrement and check counters ---
     ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; Command register I/O port  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    dec  word [0x306]          ; Decrement a counter
    jnz  check_rx_filter       ; Jump if not zero

    mov  ax, [0x302]          ; Load another counter
    mov  [0x306], ax          ; Reset the first counter
    cmp  word [0x140], byte +0x64   ; Compare with 100
    mov word [0x140], 0x0 ; reset.
    jc   check_rx_filter        ; Jump if less than 100

check_counters_done:
	add word [0x2db],byte +0x8

    ; --- Call a subroutine (likely to adjust TX start threshold)
    call sub_c93           ; (Will be defined later)

    cmp byte [0x46b], 0x1  ;
    jz  check_rx_filter ;

; discard
    mov ax,[0x2d9]
     ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; Command register I/O port  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    out dx,ax

    call sub_120a ;
    mov bx,ax ;

    mov si,0x2af
    add si, byte + 0xa ;
    mov ax,bx
    sub ax,[si+0x4]
    cmp ax,[0x2b7] ;
    jc discard_packet ;

    call sub_1194

    cmp si, 0x2cd
    jnz skip_discard

    skip_discard:

check_rx_filter:
    cmp byte [0x44b], 0x0 ; Check packet reception
    jz rx_complete_epilogue ;

;discard packet
discard_packet:
	;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; Command register I/O port  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
	mov ax,[0x2cd]
	out dx,ax

rx_complete_epilogue:
    pop  es                  ; Restore ES
    pop  ds                  ; Restore DS
    popa                    ; Restore all general-purpose registers
    iret                    ; Return from interrupt

free_packet_buffer:
receive_packet_copy:

; --- (Code continues in the next chunk) ---
; receive packet and copy to buffer

; --- Select Window 6 for statistics
       ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; Load I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG

    mov  ax, 0x806 ; Select window 6 #TODO
    out  dx, ax            ; Send command

    ; --- Read statistics registers (opportunistically)
    mov  ah, 0xb0     ;
    out  dx, ax            ; Send command
    xor  ax, ax            ; Clear AX
    add  dx, byte -0x8    ; Point DX to W6 TX Heartbeat Errors register
    in   al, dx             ; Read TX Heartbeat Errors
    add  [0x128], ax      ; Add to a counter
    adc  word [0x12a], byte +0x0;
    add  dx, byte +0x8 ; command register
    mov  ah, 0xa8          ;
    out  dx, ax            ; Send command
      ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; Load I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    mov  ax, 0x801  ; Select window 1  #TODO
    out  dx, ax            ; Send command

    ; --- Check if there are pending interrupts again
    add  dx, byte -0x3    ; Point DX to the TX Status register
    in   al, dx          ; read tx status
    add  dx, byte +0x3     ;
    test al, al          ;
    js   check_rx_status_again2   ; Jump if sign flag set (TX error?)

    ; --- Check for received packets again
        ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; Load I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    mov  ax, 0x806  ; Select window 6 #TODO
    out  dx, ax            ; Send command
    add  dx, byte -0xe    ;
    xor  ah, ah          ; Clear AH
    in   al, dx             ; Read RX status (low byte)
    add  dx, byte +0x4     ;
    in   al, dx            ; Read RX status (part of high byte)
    add  dx, byte +0x4     ; Point DX to another register in window 6.
    in   al, dx             ; Read another register
    add  dx, byte +0x6     ;
    mov  ax, [0x298]      ;
    or   al, 0x80          ; Set bit 7 (likely enabling something)
    out  dx, ax            ; Write back modified value
    add  dx, byte +0x0   ;
    mov  ah, 0xa8       ;
    out  dx, ax           ; Send command
     ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; Load I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    mov  ax, 0x801  ; Select window 1 #TODO
    out  dx, ax             ; Send command
    ret                     ; Return (to interrupt prologue's IRET)

    check_rx_status_again2:
; --- Check RX Status (again, after reading statistics)
	add dx,byte + 0x0
	mov ax,[0x298]
	and al,0x7f
	out dx,ax
	jmp short check_rx_status_again2_done

check_rx_status_again2_done:
; --- (Code continues in the next chunk) ---

; --- Subroutine at 0xC93 ---
; Adjusts the TX start threshold based on the amount of free space in the
; transmit buffer.

sub_c93:
    push dx             ; Save DX

    ; calculate free tx buffer
    mov  ax, [0x43f]      ; Load a value (likely related to buffer size)
    shr  ax, 1              ; Divide AX by 2
    mul  word [0x2fe]       ; Multiply by a value (likely a scaling factor)
    shr  ax, byte 0x5        ; Divide AX by 32
    sub  ax, [0x2e0]         ; Subtract a value (likely the current buffer usage)
    sub  ax, 0x10         ; Subtract 16

    cwd                     ; Convert word to doubleword (sign extend AX into DX)
    not  dx                  ; Invert DX (bitwise NOT)
    and  ax, dx              ; Bitwise AND (effectively taking the absolute value)
    sub  ax, [0x2db]        ; subtract from tx start threshold
    cmc                     ; Complement carry flag
    sbb  dx, dx              ;
    and  ax, dx              ;
    add  ax, [0x2db]        ; Add back
    add  ax, 0x8800        ; Add a constant value
     ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; Command register I/O port  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    mov  [0x2d9], ax       ; store new tx start address

    pop  dx                 ; Restore DX
    ret                     ; Return

    ; --- Subroutine at 0xCF0 ---
; Appears to be part of the packet reception process.  It handles
; checking the received packet's status and potentially forwarding it
; to a higher-level handler.  It also manages buffer pointers.

sub_cf0:
    ; This label was originally inside an instruction (add [bx+si], al).
    ; This is highly unusual and likely indicates some kind of
    ; self-modifying code or a calculated jump target.  We'll preserve
    ; the structure, but it warrants further investigation during testing.
    ; add [bx+si], al          ; Placeholder (likely NOP or part of self-modifying code)
    ; add [bp+si+0x2d9a], bh  ; Placeholder instruction (likely NOP)

sub_dcc:                    ; Another entry point (likely for retries/errors)

    push es
    dec bx

get_rx_status_and_length:
    ; --- Read RX status and packet length ---
    ; The following instructions seem to manipulate some values.
    ; It's unclear what they are without further context.
    ; They might be part of some buffer management or checksum calculation.
    ; Preserving the original instructions for now.
    add al, 0x75    ;
    repne mov di, 0x452  ;

    ; --- Save information about the received packet
    mov ax, cs ;
    mov es, ax
    mov [di+0x4], cx  ; likely length
    mov ax, [bp+0xe]
    mov [di+0x2], ax ; segment
    mov [di], si     ; likely offset.

    dec byte [0x44b] ; Decrement

    push word sub_9cf  ; Jump to the packet driver dispatcher
    jmp sub_ddb ;

; --- Subroutine ---
;
;
sub_ddb:
    lds si, [es:di]  ; load ds:si

    cmp word [si+0xc], 0x3781 ; compare with magic number
    jnz check_next_packet

    ; --- Handle the received packet if the magic number matches
    mov ax, [si+0x10]  ; packet
    xchg al, ah     ;
    inc ax       ;
    and ax, 0xfffe  ;
    xchg ah, al ;
    mov [si+0xc], ax ; word ptr [si + 0Ch] = ax
    mov ds, [cs:0x178]  ; set to packet driver data segment
    jmp dispatch_table_rx_handler_part_2

    check_next_packet:
; calculate where the packet is
    mov ax, [es:di+0x4]
    add ax, 0x4
    add ax, 0x3 ; add 7 total
    and ax, 0xfffc

    mov ds, [cs:0x178] ;
    les di, [0x44c] ; load buffer
    mov [es:di+0x14], si  ; original offset
    lds si, [cs:0x2ad] ;
    mov [es:di+0x12], ds ; store segment
    mov [es:di+0x10], si ; store offset
    mov [cs:0x2af], es ; store buffer es
    mov [cs:0x2ad], di ; store buffer di

    cmp si, [cs:0x2b1]
    jnz check_next_packet_done

    mov si, ds
    cmp si, [cs:0x2b3]
    jz check_next_packet_inner_done ;

    jmp short check_next_packet_done

check_next_packet_inner_done:
    mov [cs:0x2b3], es
    mov [cs:0x2b1], di

check_next_packet_done:
    ; --- Send a command to the network card (likely to start RX process)
     ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG

    add ax, 0x9000 ;  #TODO
    out dx, ax

    mov ax, [0x2b9] ;
    out dx, ax
    or byte [0x298], 0x4 ;
    mov ax, [0x298]  ;
    out dx, ax

    ret ; return to interrupt_prologue iret

    ; --- Subroutine ---
; Decrement a counter and potentially call a far routine.

sub_dcc_inner:
    nop ; place holder
    dec byte [0x44b] ; decrement counter

    mov byte [bp+0x7], 0xc
    or word [bp+0x16], byte + 0x1 ;
    ret

; --- Subroutine at 0xD74 ---
;
; This routine appears to be involved in managing a linked list of packet
; buffers, potentially used for receive buffers. It handles 'releasing'
; a buffer and updating pointers.

sub_d74:
    ; Check a flag to determine whether to process the buffer.
    cmp byte [es:di+0x16], 0x1   ; Check a flag in the buffer descriptor
    jnz sub_d84                  ; Jump if not equal to 1

    ; --- 'Release' the current buffer and update pointers

    mov [es:di+0x14], si        ; Store SI (likely source offset)
    lds si, [cs:0x2b1]      ; Load DS:SI from a global variable (likely a pointer)
    mov ax, [si+0x12]     ;
    mov [es:di+0x12], ax   ; store segment
    mov word [es:di+0x10], 0x2b1   ; Store offset
    mov [si+0x12], es   ; store current es
    mov [si+0x10], di   ; store current di
    mov [cs:0x2b3], es  ; store current es
    mov [cs:0x2ad], di   ; store current di
    ret                     ; Return

sub_d84:
; these are unused functions in the code, probably.
    jmp sub_f10
    jmp sub_f1c
    jmp sub_cf0  ; Jump back to the previous subroutine (part of the receive logic)

; --- Subroutine at 0xDB2 ---
;
; This subroutine handles the actual reception of a packet from the network
; card's FIFO and copies it into a buffer.

sub_db2:
; --- Check if packet reception is in progress
    cmp byte [es:di+0x16], 0x1 ; Check if a packet is being received
    jnz sub_eb6                 ; Jump if no packet reception in progress

; --- Check if enough data is available in the RX FIFO ---
    cmp ax, 0x4                 ; Compare with 4 (minimum number of bytes to read?)
    jc  sub_ef3                 ; Jump if less than 4

    ; --- Prepare for reading data from the FIFO ---
    mov cx, ax                 ; Move the number of bytes to read into CX
    add dx, byte +0x2          ; Point DX to data port
    and byte [cs:0x298], 0xfb; clear bit.
    mov ax, [cs:0x298]    ;
    out dx, ax                ; Send command
    mov ax, [cs:0x2c3]       ;
    out dx, ax                ; Send command

    add dx, byte -0xe          ; Point DX to the RX FIFO (_3C509B_RX_FIFO)
    mov ax, [es:di+0x4]     ;
    mov di, ax
    add di, byte +0x3 ;
    and di, byte -0x4

    out dx, ax           ;
    out dx, ax

; --- Calculate how much data is actually available in the RX FIFO ---
    sub cx, byte +0x4  ; subtract 4.
    sub cx, di
    sbb ax, ax
    and cx, ax
    add cx, di
    sub di, cx  ; di = amount in fifo

    sub bx, cx
    jc no_room_in_buffer

check_fifo_data_ready:
    ; --- Check if there's data in the FIFO
    test di, di          ; Check if DI (amount in FIFO) is zero
    jnz read_from_fifo  ; Jump if there's data in the FIFO

; no data in fifo, read the data word by word
read_fifo_slow:
    shr cx, 1   ; divide by 2
    rep outsw  ; read 2 bytes at a time.
    mov bx, bx          ; Placeholder (likely NOP)

    ; --- Return to a higher-level function/interrupt handler
    mov ds, [cs:0x178]  ;
    mov di, [0x44c]
    add dx, byte +0xe   ;

    ; --- (Code continues in the next chunk) ---
    no_room_in_buffer:

read_from_fifo:

; --- (Continuing from the previous chunk) ---
; --- Finish FIFO reading and return to interrupt handler

; Use hardware definition to select window.
    mov  dx, [0x29c]  ; Command register I/O port  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    mov ax, 0x804 ; Select window 4 #TODO
    out dx, ax

    add dx, byte -0x6  ; Point DX to network config
    in ax, dx  ; read
    test ah, 0x20 ; test for errors
    jnz rx_error

    add dx, byte +0x6 ; command register
    ; Use hardware definition to select window.
    mov  dx, [0x29c]  ; Command register I/O port  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG

    mov ax, 0x801
    out dx, ax

    add dx, byte -0x3 ; tx status
    in al, dx
    test al, 0x10  ; check tx available
    jnz check_rx_status_after_fifo_read

    ; --- Set the 'packet received' flag and call the packet handler
    or byte [es:di+0x6], 0x1
    test byte [es:di+0x6], 0x2
    jz finish_fifo_read

call_packet_handler:
    call far [es:di+0x8]  ; Call the packet handler (far call)
    mov byte [0x44b], 0xff  ;
    ret ; return to dispatcher.

finish_fifo_read:
    ; This appears to be unused code, probably a result of the
    ; disassembly or earlier compiler optimizations.
    ; We keep it to preserve structure, but it doesn't affect functionality.
    add dx, byte +0x3
    mov ax, cs ;
    cmp ax, [cs:0x2af]
    jnz process_packet_finish

    cmp word [cs:0x2ad], 0x2ad
    jnz continue_packet_processing

process_packet_finish:
     ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; Command register I/O port  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG

    mov ax, [0x2cd]
    out dx, ax   ; Send TX reset command
    or byte [0x298], 0x4  ; Set a bit (likely enabling something)
    mov ax, [0x298]
    out dx, ax

    mov byte [0x44b], 0xff ; set to -1
    ret ;

; --- (Code continues in the next chunk) ---
; --- Subroutine at 0x1038 ---
;
; This subroutine appears to be related to transmitting packets. It checks
; various status flags and conditions and might initiate a transmission.

sub_1038:
    and ax, 0x3800          ; Mask out bits (likely checking TX status)
    add word [0x134], byte +0x1; increment a counter
    adc word [0x136], byte +0x0
    cmp ax, 0x1800           ; Compare with a threshold/value
    jz sub_1038_done            ; Jump if equal

    ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG

    call sub_1628           ; Call a subroutine (likely related to TX status)
    mov word [0x469], 0x0 ; set to 0
    mov byte [0x46b], 0x0 ; set to 0.

    jmp sub_a5c             ; Jump back to the packet processing routine

sub_1038_done:

; --- Subroutine at 0x1047
sub_1047:
    ; This code copies data from the network card's FIFO into a buffer.
    ; It is another part of packet reception and runs on the timer interrupt.
    push cx  ; store cx

    les di, [0x441]
    mov cx, [0x43f]

    add [0x469], cx
    add dx, byte -0x8  ; Point DX to the RX FIFO (_3C509B_RX_FIFO)
    shr cx, 1   ;
    rep insw   ; receive
    mov bx, bx  ; place holder

    call sub_15b3  ;
    pop cx ;
    jc check_packet_length ; jump if we are out of memory.

    jmp short copy_packet_done

; --- Subroutine at 0x1390 ---
;
;

sub_1390:
; add incoming packet to buffer.
	add cx,bp
	neg bp
	shr cx,1
	rep insw ;
	mov bx,bx

	sti ;
	mov cx,bp
	mov bp,[0x2fa]
	cli

	jmp short sub_13de_done

; --- Subroutine at 0x1342 ---
sub_1342:
; --- (Code continues in the next chunk) ---

; --- Subroutine at 0x139F/0x1390 ---
;
; This routine reads data from the network card's FIFO and stores it in
; a buffer.  It appears to be part of the interrupt-driven packet
; reception process.

sub_139f:
; read packet data from fifo
    add  dx, byte -0x6       ; Point DX to RX status register
    in   ax, dx                ; Read RX status
    test ah, 0x40            ; Test bit 6 of the high byte (RX error?)
    jnz  packet_reception_failed  ; Jump if error

    and  ax, 0x7ff          ; Mask out the high byte (keep only the length)
    mov  cx, ax             ; Move the length into CX
    add  cx, [0x469]       ;

    cmp byte [0x46b], 0x1 ;
    jnz check_tx_status_2    ;

    mov  bx, [0x46c]    ;
    xor  ax, ax          ; Clear AX
    mov  dx, [0x469]    ;
    mov  si, 0x441    ;
    mov  [0x46e], cx  ;
    call far [bx+0xa]   ; Call a far routine (likely a callback to the protocol stack)
    cli

    mov  ax, es
    or   ax, di              ; Check if ES:DI is zero (indicates an error/no buffer?)
    jz   check_packet_length       ; Jump if zero

    mov  [0x472], es ;
    mov  [0x470], di       ; store packet data offset

    sti; enable interrupt
    mov bp, [0x2fa] ;
    cli

    mov cx, [0x46e]
    mov dx, [0x2a6]
    sub cx, [0x469]  ; calculate packet length to read
    add di, [0x469]
    sub bp, cx  ; check buffer space.
    jc copy_data_to_buffer ; Jump if carry (enough space)

read_packet_data:
    ; --- Read data from the FIFO (word by word) ---
    shr  cx, 1              ; Divide CX by 2 (since we're reading words)
    rep insw               ; Read data from I/O port into memory (ES:DI)
    adc  cx, cx              ; Adjust CX for odd byte count
    rep insb                ; Read any remaining byte

    ; --- (Likely some placeholder instructions)
    ds mov bx, bx  ; ds prefix should have no effect here.
    ds mov bx, bx  ;

    add  dx, byte +0xe      ; Point DX to the command register

; --- (Code continues in the next chunk) ---
; --- Call a subroutine (likely to update statistics)
    call sub_1628     ; (Will be defined later)

    ; --- Copy the received packet data to another buffer
    mov cx, [0x469]  ; load byte count.
    mov di, [0x470]    ; offset
    mov si, 0x3db    ; source
    shr cx, 1 ; word count
    rep movsw           ; Copy packet data (word by word)
    mov bx, bx           ; Placeholder

    ; --- Call a far routine (likely a callback to indicate packet reception)
    mov  bx, [0x46c]       ; Load a value (likely a function pointer)
    mov  ax, 0x1             ; Set AX to 1 (likely a status code or parameter)
    mov  cx, [0x46e]      ; Load a value (likely packet length)
    lds  si, [0x470]      ; Load DS:SI with the buffer address
    call far [cs:bx+0xa]   ; Call the far routine (indirect call)
    cli

    ; Restore things
    mov  ds, [cs:0x178]      ;
    mov word [0x469], 0x0 ; set to 0
    mov byte [0x46b], 0x0 ;

    jmp  sub_a5c           ; Jump back to the main packet processing routine

copy_data_to_buffer:
check_tx_status_2:
packet_reception_failed:
sub_13de_done:
check_packet_length:
; --- (Code continues in the next chunk) ---
; --- Subroutine at 0x142E ---
;
; This subroutine appears to be involved in handling received packets and
; potentially triggering further processing.

sub_142e:
    add  dx, byte +0x0       ; Point DX to status register
    sti                       ; Enable interrupts
    in   ax, dx                ; Read status
    cli                       ; Disable interrupts
    test ah, ah              ; Check the high byte of AX
    js   check_rx_status_3   ; Jump if sign flag is set (error/special condition?)

    jmp sub_1390              ; Jump back to the FIFO reading routine

check_rx_status_3:
; check for available space.
    and  ax, 0x7fc          ; Mask out bits (likely keeping only status flags)
    jz   return_from_142e    ; Jump if zero (no status?)

    jmp short check_rx_status_3_done ;

return_from_142e:
copy_packet_done:

; --- Subroutine at 0x1489 ---
;
sub_1489:
; add to buffer
    add cx, bp ;
    neg bp
    shr cx, 1 ;
    rep insw
    mov bx, bx

    ; --- Set up for return to interrupt handler
    mov cx, bp ;
    sti
    mov bp, [0x2fa] ;
    cli ;
    jmp short check_rx_status_3_done

; --- Subroutine at 0x1422 ---
;
sub_1422:
    add dx,byte +0x8 ;
    in ax, dx ;
    test ah,ah
    jns check_rx_status_3_done ;

    and ax,0x7fc
    jnz check_rx_status_3_done

    jmp short sub_142e ;

; --- Subroutine ---
;
sub_1442:
; --- Send TX start command and set up for packet reception
      ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG

    mov  ax, [0x2d9]   ; Load TX start address
    out  dx, ax            ; Send command

    mov  si, [0x445]     ;
    les  di, [0x447]   ;
    mov  bp, [0x2fa]  ;
    add  dx, byte -0x6 ; Point DX to a register
    in   ax, dx            ; Read status
    and  ax, 0x7fc       ;
    add  dx, byte +0x0 ; command register
    mov  cx, ax          ;
    sub  cx, si           ; Calculate difference
    sbb  bx, bx          ;
    and  cx, bx         ;
    add  cx, si           ;
    sub  si, cx           ;
    add  [0x469], cx     ;
    add  dx, byte -0x8   ; Point DX to RX FIFO

    sub  bp, cx           ;
    jc   read_packet_from_fifo   ; Jump if carry (enough space in buffer)

; --- Copy packet data from memory to the FIFO (word by word)
read_packet_from_fifo_loop:
    shr  cx, 1               ; Divide CX by 2 (for word reads)
    rep insw               ; Read data from FIFO
    mov  bx, bx               ; Placeholder

    ; --- Check if there's more data to read ---
    test si, si  ;
    jz check_rx_status_4

    add dx, byte +0x8 ; command register
    in ax, dx ;
    test ah, ah ;
    jns check_rx_status_3_done

    and ax, 0x7fc ;
    jnz check_rx_status_3_done ;

    jmp short sub_142e

check_rx_status_4:
check_rx_status_3_done:
; --- (Code continues in the next chunk) ---

; --- Subroutine at 0x14A6 ---
;
; This subroutine handles enabling reception and setting up related parameters.

sub_14a6:
     ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; Load I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG

    mov  ax, 0x6820        ; Command: Enable reception?
    out  dx, ax             ; Send command

    cmp  byte [0x46b], 0x1  ; Check flag
    jz   set_rx_filter_done   ; Jump if flag is set

; --- Set the 'receiving' flag and initialize parameters ---
enable_reception:  ;Renamed for clarity as that was the action being done at the label.
    mov  byte [0x46b], 0x1  ; Set 'receiving' flag
    mov  si, [0x43f]    ;
    les  di, [0x441]     ;
    add  dx, byte -0x6    ; Point DX to status register
    in   ax, dx          ; Read status
    and  ax, 0x7fc       ; Mask out bits
    add  dx, byte +0x0    ; Point DX to command register
    mov  cx, ax            ;
    sub  cx, si          ; Calculate difference
    sbb  bx, bx             ;
    and  cx, bx             ;
    add  cx, si           ;
    sub  si, cx        ;
    add  [0x469], cx ;
    add  dx, byte -0x8      ; Point DX to RX FIFO (_3C509B_RX_FIFO)

    ; --- Copy data from the RX FIFO (word by word) ---
    shr  cx, 1                ; Divide CX by 2 (for word reads)
    rep insw                 ; Read data from FIFO
    mov  bx, bx               ; Placeholder

    ; --- Check for remaining data in the FIFO
    test si, si
    jz set_rx_filter_done

continue_reading_fifo:

; --- Subroutine at 0x14D3 ---
;
sub_14d3:
    add dx,byte + 0x8 ; point to command register.
    in ax,dx
    and ax,0x7fc ; check status
    jnz sub_14d3  ;

    in ax,dx
    test ax,0x4000  ; check
    jz check_for_jabber

    jmp sub_1330 ; error

check_for_jabber:
; --- (Code continues in the next chunk) ---
; --- Subroutine at 0x1508 ---
;
;

set_rx_filter_done:
sub_1508:

; --- Subroutine at 0x154D ---
;
;

sub_154d:
; --- Subroutine at 0x15B3 ---
;
;

sub_15b3_continue:
    call sub_15b3
    jc return_from_sub_15b3_continue

    mov [0x46c], bx
     ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    add dx,byte +0x8 ; point to command
    in ax,dx
    test ah,ah
    js sub_1390

    jmp sub_1342 ;

return_from_sub_15b3_continue:
    jmp sub_1330

; --- Subroutine at 0x15EA ---
;
;

sub_15ea:
    mov di, 0x3db;
    mov ax, [es:di+0xc] ; load
    xchg al, ah
    cmp ax, 0x5dc  ;
    jna sub_15ea_continue

    cmp ax,0x800
    jz prepare_packet

    cmp ax, 0x600
    jz prepare_packet

    cmp ax,0x8137
    jz prepare_packet

set_min_packet_size:
    mov ax, 0x3c  ; 60 changed to hex
    jmp short prepare_packet_size

prepare_packet:
    mov ax, [es:di+0x10]  ; get packet
    xchg al, ah
    add ax, 0xe
    cmp ax, 0x5ea
    ja set_min_packet_size

    sub ax, [0x469]
    jc set_min_packet_size

set_tx_start:
    mov si, ax
    shr ax, 1 ; word count
    mul word [0x2fe] ; free
    shr ax, byte 0x5
    sub ax, si ;
    neg ax ;
    sub ax, [0x2e0]
    sub ax, 0x10
    cwd ;
    not dx;
    and ax,dx ;

       ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG

    or ah, 0x88 ; set tx start command
    out dx, ax
    mov byte [0x46b], 0x1
    add dx, byte + 0x0 ; point to command register
    in ax, dx
    sti
    xor bx, bx ; clear bx
    mov bl, al  ; set packet int
    cli

    jmp [bx+0x4b0]

prepare_packet_size:
sub_15ea_continue:
; --- (Code continues in the next chunk) ---
; --- Subroutine at 0x15C4 ---
;
; This subroutine appears to handle some specific packet types or conditions,
; potentially related to different network protocols (like IPX).

sub_15c4:
    cmp word [es:di+0xc], 0xec5  ; Compare with a magic number (0xEC5)
    jnz check_another_packet_type   ; Jump if not equal

    ; --- Handle packet type 0xEC5
    mov word [es:di+0xc], 0xf29 ;
    jmp short check_another_packet_type ;

check_another_packet_type_inner:
    cmp ax, 0x5dc ;
    ja check_ethernet_header

    cmp word [es:di+0xe], byte -0x1 ; check broadcast
    jnz check_another_packet_type ;

    ; --- Handle a specific packet condition (likely broadcast)
    mov ax, 0x3781 ;
    mov [es:di+0xc], ax ; store magic word
    jmp short check_another_packet_type ;

; --- Subroutine at 0x15B3 ---
;
;

sub_15b3_complete:
    mov di, 0x3db ;
    mov ax, [es:di+0xc] ; get packet.
    xchg al, ah ;
check_ethernet_header:
    ; --- Check if testing is enabled (bit 1 in 0x248)
    test word [0x248], 0x2 ;
    jnz check_packet_buffer_status  ; Jump if testing is enabled

    ; --- Check if the buffer is 'active'
    mov bx, 0x30a            ; Start of the buffer list
check_packet_buffer_loop:
    cmp word [bx], byte +0x1  ; Check if the buffer is active
    jnz check_next_buffer_entry ; Jump if not active

    ; --- Check if the buffer matches the current packet
    mov cx, [bx+0x8]          ; Load buffer length
    cmp byte [bx+0xe], 0xb     ; Compare a byte in the buffer (likely packet type)
    jnz check_next_packet_type_inner ; Jump if not equal

    cmp ax, 0x5dc            ; Compare AX with 1500
    ja check_next_buffer_entry ; Jump if greater than

    jcxz compare_mac_addresses  ; Jump if CX is zero

; --- Compare MAC addresses (part of packet filtering)
compare_mac_addresses_loop:
    jcxz compare_mac_addresses  ; Jump if CX is zero

    cmp ax, 0x5dc  ;
    jna check_next_buffer_entry ; jump if not above

    add di, byte +0xc ;
    lea si, [bx+0x2]  ; Source index
    repe cmpsb  ; compare strings

    jnz check_next_buffer_entry  ;

    mov ax, [bx+0xa] ; check packet int number.
    or ax, [bx+0xc] ; or offset and segment.
    jz check_next_buffer_entry ; jump

check_for_multicast:
    test word [0x248], 0x4 ;
    jz check_multicast_done ;

    mov di, [bx+0xa]  ; offset
    mov es, [bx+0xc]    ; segment
    mov cx, 0x2 ;
    lea si, [bx+0xf] ;
    repe cmpsw
    jnz check_multicast_done ; equal

valid_mac_address:
    mov [0x46c], bx    ; Store the buffer address (likely for later use)
    clc                ; Clear carry flag (indicating success)
    ret                ; Return

check_multicast_done:
invalid_mac_address:
    stc                ; Set carry flag (indicating failure)
    ret ;

compare_mac_addresses:
check_next_buffer_entry:
check_next_packet_type_inner:
check_packet_type:
check_next_packet_type:
check_packet_buffer_status:
    ; --- (Code continues in the next chunk) ---
; --- Move to the next entry in the buffer list
    add bx, byte +0x13 ;
    cmp bx, 0x3c8 ;
    jnc invalid_mac_address ;

    mov di, 0x3db  ;
    jmp short sub_15b3_complete

; --- Subroutine at 0x1628 ---
;
;

sub_1628:
; --- Subroutine at 0x166B ---
;
;
sub_166b_continue:
; --- Enable/disable TX based on available buffer space
      ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG

    mov  ah, 0x40           ; TX enable command? #TODO
    out  dx, ax             ; Send command

    ; --- Check for pending interrupts (again)
    in   ax, dx             ; Read status
    test ah, 0x10           ; Test bit 4 of high byte
    jnz  sub_1628_done      ; Jump if bit is set

    add  dx, byte -0x6      ; Point DX to a register
    in   ax, dx             ; Read status
    add  dx, byte +0x6      ; Point DX to command register
; --- Start of Chunk 50 ---
sub_1628_done:

; --- Subroutine at 0x163A ---
;
;

sub_163a:
    push bx
    mov bx, ax
    mov  dx, [0x29c] ;   <-- Should use _3C509B_COMMAND_REG   #TODO
    mov ax, 0x803  ;
    out dx, ax

    add dx, byte - 0x4
    in ax, dx ;
    add dx, byte +0x4
    and bx, 0x7ff
    add ax, bx ;
    pop bx ;
    add ax, 0x64
    cmp ax, [0x288]
    jc sub_1663

    ; --- Select window 1
    mov  dx, [0x29c] ;   <-- Should use _3C509B_COMMAND_REG #TODO
    mov  ax, 0x801  ;
    out  dx, ax            ; Send command
    ret  ;

sub_1663:
    ; --- Select window 1
	 mov  dx, [0x29c] ;   <-- Should use _3C509B_COMMAND_REG   #TODO
    mov  ax, 0x801
    out dx, ax
    call sub_166b_continue ;
    ret

; --- Subroutine at 0x166B ---
;
; This subroutine seems to be a central point for various initialization
; and configuration tasks. It interacts with the network card, sets up
; interrupt handlers, and prepares the driver for operation.

sub_166b:
       ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    mov  ax, 0x2800   ; Command: Disable RX?   #TODO
    out  dx, ax        ; Send command

    in   ax, dx           ; Read status
    test ah, 0x10       ;
    jnz  sub_166b_done    ; Jump if bit is set

    add  dx, byte +0x0 ; command register
    xor  bx, bx       ; Clear BX
    mov  bl, [0x25d]  ; Load packet interrupt number
    shl  bx, 1         ; Multiply by 2
    mov  ax, [bx+0x24f] ; Load a value (likely a function pointer)
    out  dx, ax           ; Send the value to the I/O port

    ; --- Send more commands to the network card ---
    mov  ah, 0x20       ;   #TODO
    out  dx, ax        ; Send command

    add  dx, byte +0x0  ; Point DX to command register
    mov  ax, [0x2d9]    ; Load TX start address
    out  dx, ax            ; Send command

sub_166b_done:
    ret ;
; --- Subroutine at 0x1690 ---
;
; This subroutine seems to be a central point for various initialization
; and configuration tasks. It interacts with the network card, sets up
; interrupt handlers, and prepares the driver for operation.

sub_1690:
    cli                     ; Disable interrupts

    ; --- Check a flag to determine whether to proceed with initialization ---
    cmp  byte [0x264], 0xff ; Check a flag
    jnz  configure_irq      ; Jump if not equal to 0xFF

    ; --- Perform initialization steps (if flag is 0xFF) ---
    mov  dx, [0x25e]       ; Load I/O port
    call sub_180e           ; Call a subroutine (likely for I/O port setup)

    mov  al, [0x263]       ; Load a value
    out  dx, al             ; Write to I/O port
    mov  al, 0xff           ; Load 0xFF
    out  dx, al             ; Write to I/O port

    ; --- Configure interrupt-related registers
    mov  dx, [0x26d]      ; Load I/O port
    in   al, dx             ; Read from I/O port
    or   al, [0x270]     ;
    jmp short configure_irq_done  ; Jump

configure_irq:

config_irq_and_enable_cmd:
    ; --- Configure interrupt-related registers (continued)
    mov  dx, [0x26d]              ; Write to I/O port  #TODO Check Status/IO Register
    out  dx, al
    mov  dx, [0x260]        ; Load I/O port
    add  dl, 0x4         ;
    mov  ax, 0x1            ;
    out  dx, ax              ; Write to I/O port

    ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; Load I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG

    mov  ax, 0x5800           ; Command (likely enable interrupts)   #TODO
    out  dx, ax              ; Send command
    in   ax, dx                ; Read status
    test ah, 0x10             ;
    jnz  sub_16c5             ; Jump if bit is set

    mov  ax, 0x2800           ; Command (likely disable RX)   #TODO
    out  dx, ax              ; Send command
    in   ax, dx                ; Read status
    test ah, 0x10           ;
    jnz  sub_16cf              ; Jump if bit is set

    add  dx, byte +0x0 ; point to command register
    mov  ax, [0x298]        ;
    out  dx, ax               ; Send command

    mov  ax, 0x70fe          ; Command (likely related to interrupts) #TODO
    out  dx, ax                ; Send command

	  ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; Load I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    mov  ax, 0x801 ; Select window 1.  #TODO
    out  dx, ax              ; Send command

    ; --- Call subroutines to perform additional configuration
    call sub_1821           ; (Will be defined later)
    call sub_18c0           ; (Will be defined later)
;--- End of Chunk 51 ---
; --- (Continuing from the previous chunk) ---

; --- (Continuing from 0x1690)
   ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; Load I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    mov  ax, 0x5800        ; Command (likely enable interrupts) #TODO
    out  dx, ax              ; Send command
    in   ax, dx            ; Read status
    test ah, 0x10            ; Test bit 4 (interrupt status?)
    jnz  sub_16f2              ; Jump if bit is set

    mov  ax, 0x2800      ; disable rx
    out  dx, ax           ; Send command
    in   ax, dx             ; Read status
    test ah, 0x10           ;
    jnz sub_16fc ;

; --- select window 2
	;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    mov ax, 0x802; Select window 2  #TODO
    out dx, ax

    ; --- Copy the MAC address (from 0x27C) to the NIC
    mov si, 0x27c ; source
    add dx, byte -0xe ; point to mac address.

    mov cx, 6  ; 6 bytes
write_mac_address_loop:
    lodsb ; load
    out dx, al ; output
    inc dx
    loop write_mac_address_loop

    ; --- Prepare to save the original interrupt vectors
    pusha ;

    mov dx, [0x260] ; base port.
    add dx, byte +0xe
    mov ax, 0x803
    out dx, ax

    add dx, byte -0x6
    in ax, dx
    or ax, 0x8c00 ;
    jmp short write_config_to_nic

write_config_to_nic:
    out dx, ax ; write to network config

    popa ;

    ; --- Select Window 4 and modify configuration
     ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    mov  ax, 0x804 ; Select window 4 #TODO
    out  dx, ax          ; Send command

    add  dl, 0xfc   ; point to config
    in   ax, dx             ; Read configuration
    or   ax, [0x278]      ; Combine with a value
    out  dx, ax              ; Write modified configuration

    add  dl, 0x4    ; Point to command register.
    mov  ax, 0x803   ; Select window 3?  #TODO
    out  dx, ax             ; Send command
    add  dx, byte -0x4 ; point to config.
    in   ax, dx              ; Read a register value
    mov  [0x288], ax       ; Store the value
    add  dx, byte +0x4   ;
     ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    mov  ax, 0x801 ; Select window 1 #TODO
    out  dx, ax              ; Send command

    ; --- Read and store another register value
    add  dx, byte -0x2    ; Point DX to another register
    in   ax, dx              ; Read the register value
    mov  [0x28a], ax       ; Store the value

    add  dx, byte +0x2    ; Point to command register
    add  dx, byte +0x0    ;
    mov  ah, 0xa8       ;
    out  dx, ax            ; Send command

    ; --- Calculate and set the TX start threshold
    mov  ax, [0x2d9]    ; Load TX start address
    and  ax, 0x7ff    ; Mask out bits
    sub  ax, [0x2db] ;
    cmc                 ; Complement carry flag
    sbb  cx, cx           ;
    and  ax, cx           ;
    add  ax, [0x2db]     ;
    add  ax, 0x8800      ; Add a constant
     ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    mov  [0x2d9], ax      ; Store the calculated TX start address
    out  dx, ax             ; Send command

    mov  ax, [0x2cd]     ; Load a value (TX reset command?)
    out  dx, ax             ; Send command
    mov  ax, [0x2d7]  ;
    out  dx, ax ;

    add  dx, byte +0x0    ;
    mov  ax, 0x6841      ; Command (likely related to interrupts) #TODO
    out  dx, ax              ; Send command
    mov  ah, 0x60       ;   #TODO
    out  dx, ax             ; Send command
    mov  ah, 0x10          ;   #TODO
    cmp  word [0x267], 0xc000; Check a flag
    jz   sub_1799 ; Jump if equal

    mov  ah, 0xb8          ;  #TODO
    out  dx, ax              ; Send command

    add  dx, byte -0x4      ;

    sti                       ; Enable interrupts
    in   al, dx                ; Read from I/O port
    inc  al                   ; Increment AL
    jnz  sub_179e             ; Jump if not zero
    cli                       ; Disable interrupts

sub_1799:
; --- (Code continues in the next chunk) ---
; --- (Continuing from the previous chunk) ---

; --- (Continuing from 0x1690, initialization)
    ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; Load I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    mov  ax, 0x6841        ; Command (likely related to interrupts) #TODO
    out  dx, ax              ; Send command
    mov  ah, 0x35        ; DOS function: Get interrupt vector
    mov  al, [0x26a]    ; Load packet interrupt number
    int  0x21             ; Call DOS service (get original interrupt vector)
    mov  [0x28e], es    ; Save original vector's segment
    mov  [0x28c], bx    ; Save original vector's offset

    ; --- Set the packet driver's interrupt handler ---
    mov  ah, 0x25          ; DOS function: Set interrupt vector
    mov  al, [0x26a]      ; Load packet interrupt number
    push cs                  ; Push code segment onto stack
    pop  ds                  ; Pop into DS (DS = CS)
    mov  dx, interrupt_handler ; Load offset of interrupt handler
    int  0x21                ; Call DOS service (set new interrupt vector)

    ; --- Save and set the original timer interrupt (INT 8) vector ---
    mov  ds, [cs:0x178]    ; Load DS with the driver's data segment
    mov  ax, 0x3508       ; DOS function: Get interrupt vector (INT 8)
    int  0x21                ; Call DOS service (get original vector)
    mov  [0x292], es       ; Save original vector's segment
    mov  [0x290], bx       ; Save original vector's offset

    mov  ax, 0x2508        ; DOS function: Set interrupt vector (INT 8)
    mov  dx, timer_interrupt_handler ; Load offset of timer interrupt handler
    int  0x21                ; Call DOS service (set new vector)

    ; --- Enable interrupts on the network card ---
     ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; Load I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    mov  ah, 0x48           ; Command (likely enable interrupts)  #TODO
    out  dx, ax             ; Send command

    ; --- Disable specific interrupts on the 8259 PIC
    mov  ah, 0x20       ;
    out  dx, ax       ;

    mov  dx, [0x26d]        ; Load I/O port (likely interrupt mask register)
    in   al, dx             ; Read interrupt mask
    and  al, [0x26f]       ; Mask out specific bits (disable certain interrupts)
    jmp  short output_interrupt_mask ; Jump

configure_irq_done:
output_interrupt_mask:
set_and_restore_configuration:
    ;Use harware defintions for this subroutine
     ;Correct Hardware Definitions:
    mov  dx, [0x26d]  ; Load I/O port (likely interrupt mask register)  <-- Should use _3C509B_STATUS_CONTROL_REG or _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    out  dx, al                ; Write the modified interrupt mask

    ; --- Store segment registers for later use ---
    mov  [0x2af], ds       ; Save DS
    mov  [0x2b3], ds        ; Save DS again
    mov  [0x443], ds   ;
    mov  [0x449], ds      ;

    sti                       ; Enable interrupts
    mov  byte [0x104], 0x1  ; Set initialization flag
    clc                       ; Clear carry flag (indicating success)
    ret                       ; Return

sub_16c5:
sub_16cf:
sub_16f2:
sub_1799b:
sub_179e:
; --- (Code continues in the next chunk) ---
; --- Subroutine at 0x180E ---
;
; This subroutine appears to be responsible for reading data from the
; network card's EEPROM. It uses a specific sequence of I/O writes
; and reads to access the EEPROM.

sub_180e:
    mov  dx, [0x25e]  ; ID port
    xor  al, al             ; Clear AL
    out  dx, al             ; Write 0 to I/O port
    out  dx, al             ; Write 0 again

    ; --- Send the "activate" command sequence to the ID port ---
    mov  cx, 0xff           ; Load 0xFF into CX
    mov  al, 0xff   ; Load the "activate" command    <-- TODO: Replace direct values with descriptive defines.
eeProm_activate_loop:
    out  dx, al             ; Write the command to the ID port
    shl  al, 1              ; Shift AL left (part of the command sequence)
    jnc  eeProm_activate_next ; Jump if carry flag is not set

    ; --- Part of the activate command sequence
    xor  al, 0xcf          ; XOR AL with 0xCF      <-- TODO: Replace direct values with descriptive defines.

eeProm_activate_next:
    loop eeProm_activate_loop   ; Loop until CX is zero

    ret  ; return
; --- Subroutine at 0x1821 ---
;
;

sub_1821:
    sti
    xor si, si  ; clear
    cli

    ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; Load I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    mov ax, 0x6841 ;  #TODO
    out dx, ax ;
    mov ah, 0x60 ; #TODO
    out dx, ax ;

    add dx, byte -0xe ;

    mov cx, 0x100
    shr cx, 1 ;
    rep outsw ; send dummy data

    mov bx, bx  ;

; --- (Code continues in the next chunk) ---
    add dx, byte +0xa ; point to eeprom data
    in al, dx
    sti
    xor ah, ah
    mov bx, ax  ; bx now has config word

; --- Calculate a value based on configuration
    xor dx, dx
    mov ah, [0x277]
    xor al, al
    div bx  ; ax = 0x???? / config.
    and ax, 0xfffc

    cmp word [0x2fc], byte +0x0 ;
    jnz skip_buffer_init

    mov [0x2fc], ax ;

    cli  ; disable interrupts

    ; --- Select Window 6 for statistics
       ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    mov ax, 0x806 ; select window 6 #TODO
    out dx, ax

    mov bl, [0x277]
    mov es, [0x178]  ;
    mov di, 0x3db ;
    xor si, si  ; clear si

    add dx, byte +0x0
    mov ah, 0x60  ; #TODO
    out dx, ax ;

    add dx, byte -0xe
    cld
    inc si

; --- Read data from the RX FIFO (word by word)
    mov cx, 0x40  ; 64
    shr cx, 1  ; cx = 32
    rep insw
    mov bx, bx

    ; --- (Likely related to buffer management or statistics)
    sub di, byte +0x40 ;

    add dx, byte +0xa    ; Point DX to EEPROM data register
    in   al, dx            ; Read from EEPROM data register
    add  dx, byte -0xa ;
    cmp  al, bl           ; Compare AL with BL
    jc   check_buffer_size      ; Jump if carry (AL < BL)

    sti ; enable interrupts

check_buffer_size_done:
    ; --- (Code continues in the next chunk) ---
    shl si, byte 0x6
    cmp word [0x2fa], byte +0x0 ;
    jnz skip_buffer_calc

    mov [0x2fa], si ;

    cli ; disable

    ; --- Select Window 6
       ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG

    mov  ax, 0x806  ; Select window 6 #TODO
    out  dx, ax             ; Send command

    add word [0x2fc], byte + 0x3;
    and word [0x2fc], byte -0x4;
    add word [0x2fa], byte + 0x3;
    and word [0x2fa], byte -0x4;

    ret  ;

skip_buffer_init:
skip_buffer_calc:

; --- Subroutine at 0x18C0 ---
;
;

sub_18c0:
    ; --- Calculate and store the buffer size ---
    mov  ax, [0x300]       ; Load a value (likely related to total buffer size)
    mov  cx, 0x40         ; Load 64
    mul  cx                  ; Multiply AX by 64
    div  word [0x2fa] ; Divide
    inc  ax                  ; Increment AX
    mov  [0x2fe], ax      ; Store the calculated buffer size

    ret                      ; Return

check_buffer_size:
    jmp check_buffer_size_done
; --- Placeholder/Padding ---
; The following instructions are likely placeholders or remnants of
; compiler optimizations. They don't have a clear functional purpose in
; the current context. We preserve them for structural fidelity.
    add [bx+si], al
    add [bp+si+0x2d9a], bh

; "Hidden" Subroutine at 0x18D4 ---
;
; This subroutine is not explicitly called by name in the original code.
; It's reached via a jump from within another subroutine, or possibly
; through a calculated jump target. We'll define it here for completeness,
; but its exact role will become clearer as we analyze the rest of the code.
; It appears to be the command-line parameter parsing section.

handle_switches_and_params:
    call sub_1bb3 ; (Will be defined later)
    ; (The rest of this subroutine will be filled in later, as we encounter
    ; the code that jumps to it.  It's likely part of the argument parsing
    ; logic.)
; --- (Continuing from 0x18D4 - Command-Line Argument Parsing) ---
; This code parses command-line arguments passed to the driver.  It checks
; for specific switches and parameters, and configures the driver accordingly.
; --- Check for the '-' character (indicating a switch)
    mov bx, 0x27b5   ;
    cmp byte [bx], 0x0   ; Check for null terminator (end of arguments)
    jz no_more_args    ; Jump if end of arguments

    mov dx, [bx+0x3]  ;
    call sub_1bbc      ; Call a subroutine (likely for processing a switch)
    add bx, byte +0x5 ;
    jmp short check_next_arg_2  ; Jump back to check the next argument

no_more_args:
    mov dx, 0x2f2b ;
    call sub_1bbc      ;
    mov bx, 0x28b8  ;
    cmp word [bx], byte +0x0  ;
    jz no_more_args_done ;

    mov dx, [bx+0x4]
    call sub_1bbc
    add bx, byte + 0x6
    jmp short no_more_args

no_more_args_done:
    mov dx,0x2fcc
    call sub_1bbc ;

    jmp short main_driver_logic_start ; jump
; --- Subroutine at 0x1910
;
;

sub_1910:
    cld                  ; Clear direction flag
    mov ax, cs           ; Load code segment into AX
    mov ds, ax           ; Set DS to the code segment
    mov es, ax           ; Set ES to the code segment
    mov [0x178], ax      ; Store the code segment (likely for later use)
    mov byte [0x291b], 0x24  ; set $ character
    mov dx, 0x28f3  ;
    call sub_1bb3  ;

    mov dx, 0x291e
    call sub_1bb3

    push sp ; save stack pointer
    pop ax
    mov dx, 0x295f
    cmp ax, sp ; compare stacks
    jz stack_check_ok

    jmp main_driver_logic_start ;

stack_check_ok:
    ; --- Disable interrupts, call a routine, then re-enable ---
    cli                ; Disable interrupts
    call sub_202f    ; (Will be defined later)
    mov [0x265], al ; store media
    sti                 ; Enable interrupts

    ; --- Process command-line arguments ---
    mov si, 0x81   ; Load SI with 0x81 (offset to command-line parameters in PSP)
    call sub_1ba5     ; Call a subroutine (likely for argument parsing)

parse_argument_loop:
    mov al, [si]        ; Load a character from the command line
    cmp al, 0xd         ; Check for carriage return (end of arguments)
    jnz check_for_dash  ; Jump if not a carriage return

    jmp short sub_18d4      ; Jump back to the argument parsing routine
check_for_dash:
    ; --- Check for '-' character (start of a switch)
    cmp al, 0x2d         ; Compare with '-' character
    jnz check_for_switch   ; Jump if not a '-'

    inc si              ; Increment SI to point to the next character
    lodsb               ; Load the next character into AL
    mov bx, 0x27b5     ;
check_switch_loop:
    cmp al, [bx]     ; compare al with byte ptr [bx]
    jz switch_found

    add bx, byte +0x5
    cmp byte [bx], 0x0  ;
    jnz check_switch_loop ;

    jmp sub_18d4; error.

switch_found:
    ; --- Call the appropriate routine to handle the switch
    call [bx+0x1]       ; Call the subroutine associated with the switch
    call sub_1ba5         ; Call a subroutine (likely for argument parsing)
    mov al, [si] ;
    jmp short parse_argument_loop      ; Jump back to check for more arguments

check_for_switch:
; --- (Code continues in the next chunk) ---
; --- Check and Parse interrupt number.
    call sub_1b56 ;
    mov dx, 0x2987
    cmp ax, 0x20  ; space
    jnc check_valid_pkt_int_2

    jmp main_driver_logic_start ;

check_valid_pkt_int_2:
    cmp ax, 0xff ;
    jna set_pkt_int_num

    jmp main_driver_logic_start ; invalid

set_pkt_int_num:
    mov [0x24a], al  ; store packet interrupt number.

    ; --- Check if testing is enabled
    test word [0x248], 0x1 ; Check bit 0 (testing flag?)
    jz check_pkt_int_handler  ; Jump if testing is not enabled

    ; --- Call subroutines if testing is enabled
    call sub_1a2b       ; (Will be defined later)
    xor bx, bx ; clear
    mov bl, [0x24a]
    call sub_1b28 ;
    mov dx, 0x29d3
    jnc main_driver_logic_start ; if ok, continue.

    ; error
    call sub_1af9 ;
    call sub_1c4d ;
    cmp ax,0x286
    jz setup_irq_handler

    ; --- Display error message
    mov di, 0x985  ;
    mov cx, 0x23cc
    sub cx, di
    mov bx, 0x3265
    call sub_1c69 ;

    call sub_1d88  ;
    jc main_driver_logic_start ;
setup_irq_handler:
    test word [0x248], 0x8  ; check bit.
    jnz free_memory

    call sub_1690
    jc main_driver_logic_start

    mov ah, 0x35  ; dos interrupt
    mov al, [0x24a] ;
    int 0x21

    mov [0x24d], es ; store
    mov [0x24b], bx ; store

    mov ah, 0x25
    mov al, [0x24a]
    mov dx, 0x98a
    int 0x21

    call sub_1d0c

    mov ah, 0x49
    mov es, [0x2c]
    int 0x21

free_memory:

; restore the registers.
    mov dx, 0x169f  ;
    test word [0x248], 0x8
    jz check_memory_freed

    mov dx, 0x18e3
    mov ax, 0x3100 ; terminate and stay resident.
    shr dx, byte 0x4
    add dx, byte +0x10
    int 0x21

check_memory_freed:

    ; print messages
    call sub_1bb3 ;
    mov dx,0x2952
    call sub_1bb3 ;
    int 0x20 ; terminate

main_driver_logic_start:
; --- (Code continues in the next chunk) ---
; --- Subroutine at 0x19CB ---
;
; This subroutine sets a bit in a flag (likely related to testing or
; a specific operating mode).

sub_19cb:
    or word [0x248], byte +0x1  ; Set bit 0 in the flag at 0x248
    ret                         ; Return

; --- Subroutine at 0x19D0 ---
;
;
sub_19d0:
    or word [0x248], byte +0x2  ; Set bit 1
    ret

; --- Subroutine at 0x19D5 ---
;
;
sub_19d5:
    or word [0x248], byte +0x4 ; set bit 2
    ret

; --- Subroutine at 0x19DA ---
;
;
sub_19da:
    or word [0x248], byte +0x8 ; set bit 3.
    ret

; --- Subroutine at 0x19DF ---
;
;

sub_19df:
    xor  bx, bx           ; Clear BX
    mov  bl, [0x24a]     ; Load packet interrupt number
    call sub_1b28       ; Call a subroutine (likely to get interrupt vector)
    jc   restore_interrupts ; Jump if carry flag is set (error)

    mov  [0x23cc], bx   ;
    mov  [0x23ce], es  ;
    mov  ah, 0x1         ;
    mov  al, 0xff       ;
    pushf                 ; Push flags onto stack
    cli                  ; Disable interrupts
    call far [0x23cc]  ; Call the original interrupt handler (far call)
    mov  ds, [cs:0x178]    ; Load DS with the driver's data segment
    jc   restore_interrupts_error       ; Jump if carry flag is set (error)

    ; --- Prepare to call the original interrupt handler again
    mov  ah, 0x2         ;
    mov  al, ch          ;
    mov  bx, dx          ;
    mov  dl, cl           ;
    mov  cx, 0x2          ;
    mov  si, 0x23d0      ;
    mov  di, 0x23d2     ;
    pushf                ; Push flags onto stack
    cli                 ; Disable interrupts
    call far [0x23cc]    ; Call the original interrupt handler (far call)
    jc   restore_interrupts_error     ; Jump if carry flag is set (error)

    ; --- Save the return value from the interrupt handler
    mov  [0x23d7], ax    ;
    mov  bx, ax            ;
    mov  ah, 0x5         ;
    pushf               ; Push flags onto stack
    cli                ; Disable interrupts
    call far [0x23cc]    ; Call the original interrupt handler (far call)
    jc   restore_interrupts_error    ; Jump if carry flag is set (error)

    mov dx, 0x2a8d
    call sub_1bb3 ;

    jmp short sub_19df_done ;

restore_interrupts_error:
    mov ah, 0x3
    mov bx, [0x23d7]
    pushf
    cli
    call far [0x23cc]
    push dx
    mov dx, 0x2a4d
    call sub_1bb3
    pop dx

    call sub_1b48
    jmp sub_19df_done ;

restore_interrupts:
    mov dx, 0x2a1e ; error message
    call sub_1bb3
    xor bx, bx
    mov bl, [0x24a]  ;
    call sub_1bc1

sub_19df_done:
; --- (Code continues in the next chunk) ---
; --- Subroutine at 0x1A99 ---
;
; This subroutine appears to be related to processing command-line
; arguments, specifically handling errors or invalid input.

sub_1a99:
    ; This label was originally inside an instruction in the disassembled code
    ; (likely a result of compiler optimization or obfuscation).
    ; add [bx+si], al          ; Placeholder instruction (likely NOP or part of self-modifying code)
    ; add [bx+si], al          ; Placeholder instruction (likely NOP)
    ; add [bx+si], al          ; Placeholder instruction (likely NOP)
    ; add [bx+si], al          ; Placeholder instruction (likely NOP)

; --- Subroutine at 0x1AF9 ---
;
; This subroutine prepares to parse and process command-line parameters.
; It likely sets up pointers and initializes variables used in the
; parameter parsing loop.

sub_1af9:
    push bx                 ; Save BX
    push di                 ; Save DI

    mov  ax, ds             ; Load DS into AX
    mov  es, ax             ; Set ES to DS (for string operations)
    mov  al, 0x24         ; Load '$' character (string terminator)
    mov  cx, 0xffff       ; Load a large value into CX (for searching)
    repne scasb           ; Search for '$' in the string (ES:DI)
    not  cx               ; Invert CX (get the length of the string)
    dec  cx                ; Decrement CX (adjust for the terminator)
    pop  di                ; Restore DI
    mov  bx, cx            ; Move the string length into BX
    push di                ; Save DI again
    mov  di, si            ; Set DI to SI (source index)
    mov  al, 0xd          ; Load carriage return character
    mov  cx, 0xffff        ; Load a large value into CX
    repne scasb           ; Search for carriage return in the string
    pop  di                 ; Restore DI
    not  cx                ; Invert CX
    dec  cx                ; Decrement CX
    cmp  cx, bx            ; Compare the lengths
    jc   check_string_length ; Jump if CX < BX

    sub  cx, bx            ; Calculate the difference in lengths
    inc  cx                ; Increment CX

    ; --- Prepare to compare strings
    push cx                ; Save CX
    push si                ; Save SI
    push di                ; Save DI
    mov  cx, bx           ; Move the string length into CX
    repe cmpsb          ; Compare the strings (DS:SI and ES:DI)
    pop  di                ; Restore DI
    pop  si                 ; Restore SI
    pop  cx                ; Restore CX

    jz   string_match_found ; Jump if strings are equal

string_match_not_found:
    ; --- (Code continues in the next chunk) ---
; --- Handle the case where the strings don't match
    inc  si                ; Increment SI (move to the next character)
    loop string_match_not_found ; Loop until CX is zero

    jmp  short check_string_length ;

string_match_found:
    ; --- Check for specific characters after a match
    cmp byte [si-0x1], 0xd  ; Check for carriage return
    jz  string_match_confirmed ; Jump if carriage return

    cmp byte [si-0x1], 0x3d  ; Check for '=' character
    jz string_match_equals  ; Jump if '='

    inc si
    jmp short string_match_found;

string_match_equals:
string_match_confirmed:
    clc                    ; Clear carry flag (indicating success)
    pop  bx                  ; Restore BX
    ret                    ; Return

check_string_length:
invalid_parameter:
    stc                     ; Set carry flag (indicating error)
    pop  bx                  ; Restore BX
    ret                    ; Return
; --- (Code continues in the next chunk) ---
; --- Subroutine at 0x1AAB ---
;
; This subroutine appears to handle character processing, potentially
; converting lowercase letters to uppercase.

sub_1aab:
    push si                 ; Save SI

char_processing_loop:
    lodsb                  ; Load a byte from [DS:SI] into AL
    cmp  al, 0xd            ; Compare with carriage return (end of string)
    jz   char_processing_done   ; Jump if carriage return

    cmp  al, 0x61           ; Compare with 'a' (lowercase)
    jc   next_char           ; Jump if less than 'a'
    cmp  al, 0x7a           ; Compare with 'z' (lowercase)
    ja   next_char           ; Jump if greater than 'z'

    ; --- Convert lowercase to uppercase
    and  word [si-0x1], 0xffdf; Convert to uppercase (clear bit 5)

next_char:
    jmp  short char_processing_loop ; Loop back to process the next character

char_processing_done:
    pop  si                  ; Restore SI
    ret                      ; Return

; --- Subroutine at 0x1B0E ---
;
; This subroutine appears to locate and call a specific function based
; on a command-line switch (likely related to the "switches" mentioned
; in the usage string).

sub_1b0e:
    mov  bx, 0x28b8         ; Load the base address of a table (likely a switch table)
    mov  di, [bx]        ;
    test di, di          ; Check if DI is zero
    jz   switch_not_found     ; Jump if zero (switch not found)

parse_switch_loop:
    push si                 ; Save SI
    call sub_1aab        ; uppercase
    jc   switch_not_found   ; Jump if carry (error/invalid switch)

    ; --- Call the function associated with the switch
    call [bx+0x2]           ; Indirect call (using the jump table)
    pop  si                  ; Restore SI
    add  bx, byte +0x6      ; Move to the next entry in the table
    jmp  short sub_1b0e      ;

switch_not_found:
    ret                     ; Return
; --- (Code continues in the next chunk) ---
; --- Subroutine at 0x1B28 ---
;
; This subroutine gets the interrupt vector for a given interrupt number.
; It sets the carry flag (CF) if the interrupt number is invalid.

sub_1b28:
    push si                ; Save SI
    xor  ax, ax            ; Clear AX
    mov  es, ax            ; Set ES to 0 (for accessing the IVT)
    shl  bx, byte 0x2     ; Multiply BX by 4 (size of an interrupt vector entry)
    les  bx, [es:bx]     ; Load ES:BX with the interrupt vector
    mov  di, bx            ; Copy BX to DI
    add  di, byte +0x3     ; Add 3 to DI (likely pointing to a specific byte within the vector)
    mov  cx, 0x9           ; Load 9 into CX
    mov  si, 0x98d       ; Load offset of a string (likely for comparison)
    repe cmpsb          ; Compare the bytes at ES:DI with the string at DS:SI
    jnz  invalid_interrupt  ; Jump if not equal (invalid interrupt number)

    clc                    ; Clear carry flag (valid interrupt number)
    pop  si                ; Restore SI
    ret                    ; Return

invalid_interrupt:
    stc                    ; Set carry flag (invalid interrupt number)
    jmp  short sub_1b43      ;

sub_1b43:
    pop si
    ret
; --- Subroutine at 0x1B48 ---
;
; This subroutine seems to call the interrupt handler.
sub_1b48:
    xor  bx, bx            ; Clear BX
    mov  bl, dh            ; Move DH (likely containing the interrupt number) into BL
    shl  bx, 1            ; Multiply BX by 2
    mov  dx, [bx+0x23d9]  ; Load DX from a table (likely a table of interrupt handler addresses)
    call sub_1bbc           ; Call a subroutine (likely to actually execute the interrupt handler)
    ret                    ; Return
; --- Subroutine at 0x1B56 ---
; This function converts the command line numerical argument into
; a binary value.

sub_1b56:
    push si                ; Save SI
    push di                ; Save DI
    push bx                ; Save BX

    xor  cx, cx            ; Clear CX
    mov  bx, 0xa           ; Initialize BX to 10 (for decimal conversion)

parse_number_loop:
    call sub_1ba5         ; uppercase command line characters
    cmp  byte [si], 0x30   ; Compare with '0'
    jnz  check_hex_prefix ; Jump if not a digit

    ; --- Handle decimal/hexadecimal conversion ---
    mov  bx, 0x8            ; Set BX to 8 (for octal - not used as it turns out)
    inc  si                 ; Increment SI (point to next character)

    mov  al, [si]        ; Load the next character
    or   al, 0x20         ; Convert to lowercase
    cmp  al, 0x78          ; Compare with 'x' (for hexadecimal)
    jnz  check_digit      ; Jump if not 'x'

    mov  bx, 0x10          ; Set BX to 16 (for hexadecimal)
    inc  si                 ; Increment SI (skip the 'x')

check_digit:
    xor  ax, ax              ; Clear AX
    mov  al, [si]        ; Load the next character
    cmp  al, 0x30          ; Compare with '0'
    jc   number_parse_done ; Jump if less than '0' (not a digit)
    cmp  al, 0x39         ; Compare with '9'
    jna  process_digit     ; Jump if less than or equal to '9' (decimal digit)

    or   al, 0x20          ; Convert to lowercase
    cmp  al, 0x61          ; Compare with 'a'
    jc   number_parse_done    ; Jump if less than 'a' (not a hex digit)
    cmp  al, 0x66          ; Compare with 'f'
    ja   number_parse_done    ; Jump if greater than 'f' (not a hex digit)
    sub  al, 0x27          ; Adjust for hexadecimal (a=10, b=11, etc.)

process_digit:
    sub  al, 0x30         ; Convert ASCII digit to binary value
    cmp  al, bl            ; Compare with the base (BX)
    ja   number_parse_done    ; Jump if greater than the base (invalid digit)

    push ax                ; Save AX
    mov  ax, cx            ; Move CX to AX
    mul  bl               ; Multiply AX by BX (base)
    pop  cx                ; Restore the digit value
    add  cx, ax            ; Add the result to CX
    inc  si                ; Increment SI (move to the next character)
    jmp  short parse_number_loop   ; Loop back to process the next digit

number_parse_done:
    ; --- Return the converted value in AX ---
    mov  ax, cx            ; Move the result (CX) into AX
    pop  bx                ; Restore BX
    pop  di                ; Restore DI
    pop  si                ; Restore SI
    ret                     ; Return
    check_hex_prefix:
; --- Subroutine at
; --- (Continuing from the previous chunk)
; --- Subroutine at 0x1BA5 ---
; Skips whitespace (space and tab) in the input string.

sub_1ba5:
skip_whitespace_loop:
    cmp byte [si], 0x20  ; Compare with space character
    jz skip_whitespace ; Jump if equal (space)
    cmp byte [si], 0x9  ; Compare with tab character
    jz skip_whitespace   ; Jump if equal (tab)

    ret ; return

skip_whitespace:
    inc si
    jmp short skip_whitespace_loop

; --- Subroutine at 0x1BBC ---
; Prints a message to standard output (likely an error message).

sub_1bbc:
    push dx               ; Save DX
    mov  dx, 0x2952       ; Load offset of message string
    mov  ah, 0x9          ; DOS function: Display string
    int  0x21              ; Call DOS service
    pop  dx               ; Restore DX
    mov  ah, 0x9          ; DOS function: Display string
    int  0x21              ; Call DOS service
    ret                    ; Return

; --- Subroutine at 0x1BB3 ---
;
;

sub_1bb3:
    mov es, [0x178]
    mov ax, bx ;
    xor dx, dx ;

    ; convert number to string.
    push ax  ;
    mov si, 0xa  ;
    mov di, 0x1bfc
    call sub_1c08 ;

    mov dx, 0x1bfc
    mov ah, 0x9
    int 0x21

    mov dx, 0x2955 ;
    mov ah, 0x9
    int 0x21 ;

    pop ax
    xor dx, dx
    mov si, 0x10
    mov di, 0x1bfc
    call sub_1c08

    mov dx, 0x1bfc
    mov ah, 0x9  ; display
    int 0x21 ;

    mov dx, 0x295d
    mov ah, 0x9  ;
    int 0x21 ;

    ret
; --- Subroutine at 0x1BC1 ---
; Converts a number (in BX) to its hexadecimal string representation
; and then displays it along with additional messages.

sub_1bc1:
    mov  es, [0x178]      ; Load the driver's data segment into ES
    mov  ax, bx            ; Move the number (BX) into AX
    xor  dx, dx            ; Clear DX (for division)

    ; --- Convert the number to a hexadecimal string (similar to itoa)

convert_to_hex_string:
    push ax ; save number
    mov si, 0xa
    mov di, 0x1bfc ; offset
    call sub_1c08

    ; Display
    mov dx, 0x1bfc  ;
    mov ah, 0x9 ; dos interrupt.
    int 0x21

    mov dx, 0x2955
    mov ah, 0x9 ;
    int 0x21 ;

    pop ax

    xor dx, dx; clear
    mov si, 0x10
    mov di, 0x1bfc ;
    call sub_1c08 ;

    mov dx, 0x1bfc ; load offset
    mov ah, 0x9 ; display string
    int 0x21;

    mov dx, 0x295d ;
    mov ah, 0x9 ;
    int 0x21;
    ret
; --- Subroutine at 0x1c08
; Convert a number to an ASCII string representation.
sub_1c08:
    push bx
    push si
    push di
    push di ;

    mov bx, dx ; store dx
    mov cx, ax ; store ax
    mov ax, bx ;
    or ax, cx ;
    jz convert_done ; jump if zero.

convert_loop:
; Convert to ASCII
    xor dx, dx
    mov ax, bx  ;
    div si ; divide by base (10 or 16)
    mov bx, ax ; store the result back in bx.
    mov ax, cx
    div si  ;
    mov cx, ax
    mov ax, dx
    call sub_1cae ;
    stosb  ; store

    mov ax, bx ; check
    or ax, cx
    jnz convert_loop

    mov byte [es:di], 0x24 ; '$'

convert_done:
    pop si
    dec di

reverse_string_loop:
    cmp si, di  ;
    jnc reverse_string_done

    ;swap
    mov ah, [es:si]
    mov al, [es:di]
    mov [es:di], ah
    mov [es:si], al

    inc si
    jmp short reverse_string_loop

reverse_string_done:
    pop di
    pop si
    pop bx
    ret
; --- Subroutine at 0x1C4D ---
;
;

sub_1c4d:
    push bp ;
    sub sp, byte +0x6
    mov bp, sp ;
    sgdt [bp+0x0] ; store global descriptor table.
    add sp, byte +0x4
    pop ax
    xor al, al
    inc ah
    jz sub_1c4d_done

    mov ax, 0x100
    add ax, 0x286
sub_1c4d_done:
    pop bp
    ret
; --- Subroutine at 0x1C69 ---
; Copies bytes between two strings
sub_1c69:
    pusha ; save
    push cx
    push di

    mov si, [bx]
    mov al, [si]

sub_1c69_loop:
    repne scasb ; scan string
    jnz sub_1c69_done

    push cx ; save cx
    push di

    dec di
    mov cx, [bx+0x4]  ; length
    mov si, [bx] ;
    repe cmpsb ; compare strings
    pop di  ;
    pop cx
    jnz next_string_check ;

    push cx
    push di
    dec di
    mov cx, [bx+0x4]
    mov si, [bx+0x2]
    rep movsb
    pop di
    pop cx ;
    jmp short next_string_check ;

sub_1c69_done:

next_string_check:
    pop di
    pop cx
    add bx, byte +0x6 ; move to next element
    cmp word [bx], byte + 0x0 ; check if null
    jnz sub_1c69_loop ; if no, continue.

    popa
    sti
    ret

; --- Subroutine at 0x1C9E ---
; Reads a single character from standard input.

sub_1c9e:
    mov ah, 0x8  ; dos interrupt
    int 0x21 ;
    ret  ;
; --- Subroutine at 0x1CA3 ---
; Converts a character to uppercase if it is a lowercase letter.
sub_1ca3:
    cmp al, 0x61  ; Compare with 'a'
    jc sub_1ca3_done  ; Jump if less than 'a'
    cmp al, 0x7a  ; Compare with 'z'
    ja sub_1ca3_done  ; Jump if greater than 'z'
    sub al, 0x20  ; Convert to uppercase by subtracting 32
    ret  ;return

sub_1ca3_done:
    ret ;

; --- Subroutine at 0x1CAE ---
; Converts a character to its hexadecimal representation.

sub_1cae:
    add al, 0x30          ; Add '0' to convert to ASCII
    cmp al, 0x39         ; Compare with '9'
    jna sub_1cae_done      ; Jump if less than or equal to '9'
    add al, 0x7         ; Add 7 to handle A-F

sub_1cae_done:
    ret

; --- Subroutine at 0x1CC2 ---
;
;

sub_1cc2:
    cmp al, 0x30
    jc sub_1cd0

    cmp al,0x39
    ja sub_1cd0_inner

    sub al, 0x30
    ret

sub_1cd0_inner:
sub_1cd0:
    stc ; set carry
    ret
; --- Subroutine at 0x1CD2 ---
;
; This subroutine displays the MAC address of the network card.

sub_1cd2:
    mov si, 0x282 ; source
    mov cx, 0x6   ; length 6 bytes
output_mac_loop:
    lodsb          ; Load a byte from [DS:SI] into AL
    mov bl, al     ; Copy AL to BL
    shr al, byte 0x4 ; Shift AL right by 4 bits (get high nibble)
    and al, 0xf     ; Mask out the high nibble
    cmp al, 0x9     ; Compare with 9
    jna output_high_nibble ; Jump if less than or equal to 9

    add al, 0x7    ; Adjust for A-F

output_high_nibble:
    add al, 0x30 ; add '0'
    mov dl, al     ; Move the high nibble (in ASCII) to DL
    mov ah, 0x2    ; DOS function: Display character
    int 0x21        ; Call DOS service

    ; --- Output the low nibble of the MAC address byte
    mov al, bl       ; Restore the original byte from BL
    and al, 0xf     ; Mask out the high nibble
    cmp al, 0x9     ; Compare with 9
    jna output_low_nibble ; Jump if less than or equal to 9

    add al, 0x7     ; Adjust for A-F

output_low_nibble:
    add al, 0x30          ; Add '0' to convert to ASCII
    mov dl, al            ; Move the low nibble (in ASCII) to DL
    mov ah, 0x2           ; DOS function: Display character
    int 0x21             ; Call DOS service

    dec cx                ; Decrement the byte counter
    jz output_mac_done   ; Jump if all bytes have been displayed

    mov dl, 0x3a          ; Load ':' character (separator)
    mov ah, 0x2           ; DOS function: Display character
    int 0x21              ; Call DOS service
    jmp short output_mac_loop  ; Loop back to process the next byte

output_mac_done:
    ret                   ; Return
; --- Subroutine at 0x1D0C ---
;
; This subroutine displays configuration information about the network card,
; including the I/O base address, interrupt number, and MAC address.

sub_1d0c:
    xor  bx, bx            ; Clear BX
    cmp  byte [0x264], 0xff  ; Check if I/O base is set
    jz   display_config    ; Jump if I/O base is not set

    ; --- Display I/O base address ---
    mov  dx, 0x3087     ;
    call sub_1bb3      ;
    mov  bl, [0x264]       ; Load I/O base address
    cmp byte [0x265],0x3
    jnz display_io_done

    inc bx
display_io_done:
    call sub_1bc1      ; Call subroutine to display the I/O base address
    mov  dx, 0x30a1       ;
    call sub_1bb3     ;

    ; --- Display the packet interrupt number
    mov  bx, [0x260]      ; Load packet interrupt number
    call sub_1bc1      ;
    xor  bx, bx            ; Clear BX
    mov  dx, 0x30bb       ;
    call sub_1bb3    ;
    mov  bl, [0x269]    ; Load a value (likely related to interrupt type)
    call sub_1bc1      ;
    mov  dx, 0x30d5       ;
    call sub_1bb3     ;

    ; --- Display the MAC address ---
    mov  dx, 0x3109       ;
    cmp  word [0x267], 0xc000; check for 10Base-2
    jz   display_mac     ; Jump if 10Base-2

    mov  dx, 0x3117       ;
    cmp  word [0x267], byte +0x0; Check for 10Base-T
    jz   display_mac       ; Jump if 10Base-T

    mov  dx, 0x312e       ; message offset
    mov  ah, 0x9            ; DOS function: Display string
    int  0x21               ; Call DOS service

display_mac:
    mov  dx, 0x30ef       ; Load offset of a string (likely a label)
    call sub_1bb3         ; Call a subroutine (likely to display the string)
    call sub_1cd2          ; Call subroutine to display the MAC address
    mov  dx, 0x306d       ;
    call sub_1bb3     ;
    xor  bx, bx            ; Clear BX
    mov  bl, [0x24a]     ; Load packet interrupt number
    call sub_1bc1      ;
    mov  dx, 0x2952      ;
    call sub_1bbc    ;
    ret                 ;
; --- Subroutine at 0x1d88 ---
;
; This subroutine appears to be an error handler. It displays an error
; message and potentially terminates the program. It's likely called
; when an unrecoverable error occurs during initialization or operation.

sub_1d88:
    ; This is likely the main error handling, when all parameters are invalid
    add [bx+di+0xc8], bh  ; Placeholder
    call sub_2062      ; (Will be defined later, probably related to cleanup)
    mov  [0x26b], ax   ;

    ; --- Check media type and jump to specific error handlers
    cmp  byte [0x265], 0x1  ; check for 10BaseT
    jz   error_handler_tp   ; Jump if 10BaseT

    cmp  byte [0x265], 0x3  ; Check for 10Base2
    jz   error_handler_bnc   ; Jump if 10Base2

    cmp  word [0x260], byte -0x1; Check if iobase is not set
    jnz  error_handler_tp ; Jump if iobase is set.

    jmp  short exit_program      ; Jump to program termination

error_handler_bnc:
error_handler_tp:
    ; --- (Code for specific error handlers might go here) ---
    jmp sub_1e9c  ;

exit_program:
    ; --- Terminate the program (likely using a DOS function)
    ; The exact termination method might depend on whether the driver
    ; was loaded as a TSR (terminate and stay resident) or as a regular
    ; program.
    ; Since this driver *can* function as a TSR (it intercepts an interrupt),
    ; a TSR termination is most likely.
    mov ax, 0x3c ;
    call sub_22bd ;

    cmp byte [0x264], 0xff  ;
    jz check_irq_2

    mov cl, [0x264]
    dec cl
    call sub_20ab
    jc check_io_base ;

    mov dx, 0x2aa2
    jmp sub_202c

check_io_base:
    mov dx,0x2aef
    jmp sub_202c

check_irq_2:
    mov cl, 0xff
    inc cl  ; cl=0
    cmp cl, 0x7 ;
    ja sub_1da8
    call sub_20ab
    jnc check_irq_done
    mov [0x264], cl
    cmp byte [0x266], 0x1
    jnz terminate_program

    ; --- Restore the original interrupt vector (if necessary) ---
    cli ; disable interrupts
    mov dx, [0x260]
    mov dl, 0xe ; point to command register
    in ax, dx ; read
    shr ax, byte 0xd  ; check
    push ax
    mov ax, 0x800
    out dx, ax

    ; --- Restore the original interrupt vector ---
    mov  dx, [0x260]       ; Load I/O port
    mov  dl, 0x8       ; Point DX to a register
    in   ax, dx             ; Read from I/O port
    or   ax, 0x40           ; Set bit 6
    out  dx, ax             ; Write to I/O port
    in   ax, dx                ;
    pop ax
    add ax, 0x800 ;
    mov dl, 0xe ;
    out dx, ax ;
    sti ;

    jmp short terminate_program ;
check_irq_done:
; --- Subroutine at 0x1E0E ---
;
;

sub_1e0e:
    cmp byte [0x264], 0xff
    jz sub_1e24

    mov cl, [0x264]
    call sub_2100
    jc sub_1e32

    mov dx,0x2aa2
    jmp sub_202c

sub_1e24:
    xor cl, cl
    inc cl ; cl = 1
    cmp cl, 0xf
    ja sub_1e9c ; if invalid,

    call sub_2100 ; try to set.
    jnc sub_1e26
    mov [0x264], cl ; store the irq
    mov ch, cl ; store high byte.
    xor cl, cl  ; low byte
    shl cx, byte 0x4 ; multiply by 16.
    mov [0x260], cx ; store iobase

    cli
    mov dx, [0x260] ; get io base
    mov dl, 0xe ; command register
    in ax, dx
    shr ax, byte 0xd
    push ax  ; save.
     ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; Load I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    mov ax,0x800 ; select window 0 #TODO
    out dx,ax ; send command
    mov dl, 0x6 ;
    in ax, dx ;
    call sub_2125 ;
    mov dx, [0x260]
    mov dl, 0x8  ;
    in ax, dx
    call sub_2175 ;
    mov al, 0xd ; select window 5
    call sub_2230 ;
    call sub_21dc
    mov al, 0xe  ; select window 6
    call sub_2230  ;
    mov [0x27a], ax ; store
    mov al, 0xa
    call sub_2230 ;
    xchg al, ah  ; swap
    mov [0x27c], ax
    mov al, 0xb  ;
    call sub_2230  ;
    xchg al, ah ;
    mov [0x27e], ax
    mov al, 0xc  ;
    call sub_2230  ;
    xchg al, ah ;
    mov [0x280], ax
    pop ax
    add ax, 0x800 ;
     ;Correct Hardware Definitions:
    mov  dx, [0x29c]  ; Load I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    out dx,ax ;
    sti

    jmp terminate_program
sub_1e9c:
sub_1e32:

; --- Subroutine at 0x1EA2 ---
;
;

sub_1ea2:

sub_1ead:
    mov dx, [0x25e] ; id port
    xor bx, bx
    cli
    call sub_180e ; send reset sequence
    test bx,bx ;
    jnz sub_1ead_done ; if we have value, good

    mov al, 0xd0
    out dx, al

    mov al, 0x7
    call sub_226d
    cmp ax, 0x6d50 ;
    jnz check_tag

    ; --- Check for EEPROM presence and configuration ---
    mov al, 0xa          ; EEPROM read command (part of address)
    call sub_226d      ;
    mov al, 0xb    ;
    call sub_226d
    mov al, 0xc
    call sub_226d ;
    inc bx

    lea ax, [bx+0xd0]  ; set tag register to 1.
    out dx, al
    xor al, al
    out dx, al

    sti
    jmp short check_tag_done

check_tag:
    xor al, al ; clear
    out dx, al ; write
    test bx, bx ; check.
    jnz check_tag_set

    sti
    mov dx, 0x2aef  ;
    jmp sub_202c  ;

check_tag_set:
sub_1ead_done:
    xor al, al
    out dx, al
    sti

    mov dx, 0x2b1a
    jmp sub_202c

; --- Subroutine at 0x1EE0 ---
;
;

sub_1ee0:
    sti
    xor bx, bx  ; clear
    cli
    call sub_180e ; send activation sequence
    inc bx

    lea ax, [bx+0xd8]  ; set tag register to 1.
    out dx,al ; write
    mov al, 0xd0  ; set tag register command
    out dx,al ;

    mov al,0x7
    call sub_226d ;
    cmp ax,0x6d50
    jnz activate_vulcan

    cmp word [0x260], byte -0x1 ; FF FF
    jz set_io_address

    mov al,0x8 ; window 0
    call sub_226d
    and ax,0x1f ; clear
    shl ax, byte 0x4 ; multiply by 16
    add ax,0x200 ; add 0x200
    cmp ax,[0x260] ;
    jnz set_io_address_done

    cmp byte [0x262],0x0 ; check
    jnz sub_1ee0_done ; if not 0, exit

    mov [0x262], bl  ; store 1.
    lea ax, [bx+0xd0] ; 0xd1
    out dx, al  ;
    xor al, al ;clear
    out dx, al ; write
    sti
    jmp short sub_1ee0_done ; return.

set_io_address:
activate_vulcan:
set_io_address_done:
sub_1ee0_done:
; --- (Code continues in the next chunk) ---
; --- Subroutine at 0x1F2A ---
;
;

sub_1f2a:
    sti ;
    cmp byte [0x262], 0x0  ;
    jnz sub_1f43 ; if set,
    mov dx, 0x2b58
    jmp sub_202c ; error.

sub_1f43:
    mov al,0xd8  ;
    add al, [0x262] ; add
    mov [0x263], al
    cli ;
    ;Correct Hardware Definitions:
    mov  dx, [0x25e]  ; Load I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    call sub_180e
    mov al, [0x263]
    ;Correct Hardware Definitions:
    mov  dx, [0x25e]  ; Load I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    out dx, al ; write to output.
    mov al, 0xd0 ; write to port again
     ;Correct Hardware Definitions:
    mov  dx, [0x25e]  ; Load I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    out dx, al

    mov al, 0xa ;
    call sub_226d ;
    xchg al, ah ;
    mov [0x27c], ax

    mov al, 0xb  ;
    call sub_226d ;
    xchg al, ah;
    mov [0x27e], ax

    mov al, 0xc
    call sub_226d ;
    xchg al, ah ;
    mov [0x280], ax ;

    mov al, 0x8 ; window 0
    call sub_226d ;
    call sub_2125
    mov al, 0x9
    call sub_226d
    call sub_2175

    mov al,0xd ; select window 5
    call sub_226d
    call sub_21dc
    mov al,0xe ; select window 6
    call sub_226d ;
    mov [0x27a], ax
    mov al,0xd0
    add al,[0x262]
    ;Correct Hardware Definitions:
    mov  dx, [0x25e]  ; Load I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    out dx,al
    xor al,al
    ;Correct Hardware Definitions:
    mov  dx, [0x25e]  ; Load I/O port for command register  <-- Should use _3C509B_COMMAND_REG
	; Correct statement once hardware definitions are included.
    ;mov  dx, _3C509B_COMMAND_REG
    out dx, al

    sti ; enable interrupts
    cmp word [0x260], 0x3f0
    jnz sub_1fae

    mov dx,0x2b9b
    jmp short sub_202c

sub_1fae:
terminate_program:
; --- Subroutine at 0x1FBA ---
;
;

sub_1fba:
    cmp byte [0x27b], 0x0
    jna sub_1fba_done ; jump if not set

    mov dx, 0x2bc7 ;
    jmp short sub_202c

sub_1fba_done:

; --- Subroutine at 0x1FC7 ---
;
;

sub_1fc7:
    cmp byte [0x27a], 0x0 ;
    jna sub_1fc7_done

    mov dx,0x2c08
    call sub_1bb3 ;

    mov ax,[0x2cf]
    sub ax,0x10
    cmc
    sbb dx,dx
    and ax,dx
    add ax,0x10
    add ax,0x9800
    mov [0x2cd],ax

    mov ax, [0x2c5]
    sub ax, 0x10
    cmc
    sbb dx,dx
    and ax,dx
    add ax,0x10
    add ax,0x9800
    mov [0x2c3],ax

    mov ax,[0x2bb]
    add ax,0x9800
    mov [0x2b9], ax ; store to global

    ; --- Calculate initial buffer sizes ---
    mov  ax, 0x12       ; Load 18 (decimal)
    mul  word [0x302]   ; Multiply by a value (likely related to buffer count)
    mov  [0x302], ax   ; Store result (total buffer size?)

    mov  ax, 0x12
    mul  word [0x304]   ;
    mov  [0x304], ax   ;

    mov  ax, [0x302]       ;
    mov  [0x306], ax       ;
    mov  ax, [0x304]       ;
    mov  [0x308], ax       ;

    mov  ax, [0x27c]      ;
    mov  [0x282], ax       ;
    mov  ax, [0x27e]      ;
    mov  [0x284], ax       ;
    mov  ax, [0x280]      ; Load a value (likely a configuration parameter)
    mov  [0x286], ax       ; Store the value

    clc                    ; Clear carry flag (indicating success)
    ret                    ; Return

sub_1fc7_done:
; --- Subroutine at 0x2021 ---
;
;

sub_2021:
    sti  ; enable
    stc  ;set carry
    ret

; --- Subroutine at 0x2025 ---
;
;

sub_2025:
    ; --- Detect and identify the 3Com 5C509 card ---
    mov  ax, 0xf000       ; Load segment for BIOS ROM area
    mov  es, ax            ; Set ES to point to BIOS ROM
    mov  di, 0xffd9       ; Offset within BIOS ROM (signature location?)
    cmp  word [es:di], 0x4945  ; Compare with 'IE' signature
    jnz  not_3c509       ; Jump if not 'IE'

    cmp  word [es:di+0x2], 0x4153  ; Compare with 'SA' signature
    jnz  not_3c509     ; Jump if not 'SA'

    ; --- 3C509 card detected (signature found)
    mov  al, 0x2          ; Set AL to 2 (indicating detection method)
    jmp  short detection_done   ; Jump

not_3c509:
    ; --- Attempt to detect using another method (INT 15h, function C0h)
    mov  ah, 0xc0          ; INT 15h, function C0h (Get System Configuration)
    int  0x15             ; Call system BIOS
    jc   detection_failed    ; Jump if carry flag is set (error)

    test ah, ah            ; Check if function is supported
    jnz  detection_failed   ; Jump if function is not supported

    ; --- Check for 3C509-specific signature in returned data
    test byte [es:bx+0x5], 0x2 ;
    jz   detection_failed   ; Jump if bit is not set

    mov  al, 0x3          ; Set AL to 3 (indicating detection method)
    jmp  short detection_done    ; Jump

detection_failed:
    mov  al, 0x1          ; Set AL to 1 (indicating no card found)

detection_done:
    ret  ;
; --- Subroutine at 0x2062 ---
; Set interrupt mask.

sub_2062:
    in al, 0x21 ; read interrupt mask
    push ax ; save
    jmp short sub_2067_continue

sub_2067:
    mov al,0xfe
    out 0x21, al  ; mask interrupt

    sti ; enable interrupts.

    push ds
    mov ax,0x40
    mov ds,ax  ; point to bios area
    mov di,[0x6c] ; get tick count offset
    cmp di,[0x6c] ; compare
    jz sub_2076_done

    xor ax,ax ; clear ax
    xor dx,dx ;
    add di,byte +0x6
    nop

sub_2084:
    add ax,0x1 ;increment
    adc dx,byte +0x0 ; add with carry
    cmp di,[0x6c]  ; compare
    ja sub_2084 ; jump

    mov bx, 0x3e8  ; 1000
    div bx ; ax = num/1000
    mul cx ; ax *= cx
    mov bx, 0x113 ; 275
    div bx
    test ax,ax ; check ax
    jnz sub_20a1_done
    inc ax ; increment

sub_20a1_done:
    pop ds
    cli ; disable interrupts.
    mov bx,ax ; store result
    pop ax ;restore mask
    out 0x21,al ; restore interrupt mask
    mov ax,bx
    ret

sub_2076_done:
    xor ax,ax
    xor dx,dx
    add di,byte +0x6
    nop
    jmp sub_2084
; --- Subroutine at 0x20AB ---
; Set I/O base address
; Input: CL - IO Base index.

sub_20ab:
    pusha    ; Save all general-purpose registers
    mov  dx, 0x94  ;
    mov  al, 0xff   ;
    out  dx, al     ;

    mov  dx, 0x96  ;
    mov  al, 0x8      ;
    or   al, cl    ; Combine with the I/O base index
    out  dx, al       ;

    mov  dx, 0x101   ;
    in   al, dx        ; Read from I/O port
    mov  ah, al        ;
    mov  dx, 0x100    ;
    in   al, dx        ; Read from I/O port
    cmp  ax, 0x627c ; compare
    jz   sub_20ab_done   ; Jump if equal

    cmp  ax, 0x627d   ;
    jz   sub_20ab_done     ; Jump if equal
    cmp  ax, 0x61db  ;
    jz   sub_20ab_done     ; Jump if equal
    cmp  ax, 0x62f6  ;
    jz   sub_20ab_done     ; Jump if equal
    cmp  ax, 0x62f7  ;
    jz   sub_20ab_done      ; Jump if equal

    clc                 ; Clear carry flag (success)
    jmp  short sub_20f8_done      ; Jump

sub_20ab_done:
    mov dx, 0x102  ;
    in al, dx ; read
    test al, 0x1 ; check
    jz sub_20dd_done

    mov dx, 0x104 ;
    in al, dx ;
    mov ah, al ;
    and ax, 0xfc00
    add ax, 0x200
    mov [0x260], ax  ; store iobase
    stc                 ; Set carry flag (error)

sub_20dd_done:
sub_20f8_done:
    mov  dx, 0x96   ;
    mov  al, 0x0     ;
    out  dx, al      ;
    popa             ; Restore all general-purpose registers
    ret              ; Return
; --- Subroutine at 0x2100 ---
; Set IRQ
; input cl: irq number.

sub_2100:
    pusha

    xor dx, dx
    mov dh, cl  ;
    shl dx, byte 0x4 ;
    add dx, 0xc80 ;
    in ax, dx
    cmp ax, 0x6d50  ;
    jnz sub_2122_done

    add dx, byte +0x2
    in ax, dx  ;
    and ax, 0xf0ff ; mask.
    cmp ax, [0x29a]  ; compare.
    jnz sub_2122_done

    stc
    jmp short sub_2123_done

sub_2122_done:
    clc

sub_2123_done:
    popa
    ret
; --- Subroutine at 0x2175 ---
; Get IRQ and other configuration settings

sub_2175:
    mov  bx, ax            ; Move AX to BX
    mov  ax, bx            ; Copy BX back to AX
    and  ax, 0xf000        ; Mask out low bits (keep high nibble)
    shr  ax, byte 0xc    ; Shift right by 12 bits (get IRQ value)
    cmp  al, 0x7          ; Compare with 7
    jna  check_irq_value  ; Jump if less than or equal to 7

    ; --- Disable a specific interrupt if IRQ is greater than 7
    push ax                ; Save AX
    in   al, 0x21           ; Read interrupt mask register
    and  al, 0xfb          ; Clear bit 2 (disable IRQ 2/9)
    out  0x21, al           ; Write modified mask back
    pop  ax                ; Restore AX

check_irq_value:
    cmp  al, 0x9          ; Compare with 9
    jnz  set_irq_value   ; Jump if not equal to 9
    mov  al, 0x2          ;
    mov  [0x269], al   ; store irq value.

set_irq_value:
    mov al, [0x269]
    mov cl, 0x8  ;
    cmp al, 0x7 ;
    jna sub_21a1_done

    mov cl, 0x68 ;
    add al, cl
sub_21a1_done:
    mov [0x26a], al ; store al.

    mov word [0x26d], 0x21 ; default int.
    cmp byte [0x269], 0x7 ; check irq
    jna sub_21b9_done

    mov word [0x26d], 0xa1
sub_21b9_done:
    cmp byte [0x269], 0x7 ; check irq.
    jna sub_21c6_done ;

    mov word [0x271], 0xa0

sub_21c6_done:
    mov cl, [0x269]
    and cl, 0x7 ;
    mov ah, 0x1 ; set bit
    shl ah, cl
    mov [0x270], ah ; store mask
    not ah
    mov [0x26f], ah
    ret
; --- Subroutine at 0x21DC ---
; Calculates and stores several timing-related values.  These values are
; likely used for managing delays and timeouts when interacting with the
; network card.

sub_21dc:
    push dx             ; Save DX
    mov [0x273], ax     ; save ax
    mov bx, ax          ; Copy AX to BX
    mov ax, bx          ; Copy BX back to AX
    and ax, 0x3f00      ; Mask out bits (likely getting a specific field)
    add ah, 0x1       ; increment high byte
    mov al, 0x19       ;
    mul ah              ; Multiply AX by AH
    mov [0x275], ax     ; Store the result

    mov ax, [0x275]    ; Load 1000
    mov cx, 0xa       ; Load 10
    mul cx             ; Multiply AX by 10
    shr ax, byte 0x5    ; Divide AX by 32 (right shift by 5 bits)
    sub ax, 0xff        ; Subtract 255
    sbb cx, cx            ; Subtract with borrow (CX is 0 here)
    and ax, cx            ; Bitwise AND (effectively taking absolute value if negative)
    add ax, 0xff          ; add 255
    mov [0x277], al        ; Store result
    shl ax, byte 0x2     ; Multiply ax by 4.
    mov [0x300], ax

    mov word [0x278], 0x8 ;
    cmp word [0x267], byte + 0x0 ; check
    jnz sub_21dc_done

    or word [0x278], byte + 0x40 ; set
    mov ax, bx
    and ax, 0x4000 ;
    jnz sub_21dc_done ; jump

    or word [0x278], 0x80

sub_21dc_done:
    pop dx
    ret
; --- Subroutine at 0x2230 ---
; Wrapper to write to I/O port and introduce a delay
sub_2230:
    mov  dx, [0x260]       ; Load I/O port base address
    add  dl, al            ; Add AL (register offset) to DL
    out  dx, al            ; Write AL to the I/O port
    in   ax, dx            ; Read from I/O port (likely for timing delay)
    test ax, ax           ; Test AX (likely for status check)
    js   sub_2230_done     ; Jump if sign flag is set (error/special condition)

    add  dl, 0x2          ; Add 2 to DL (point to another register)
    in   ax, dx             ; Read from the I/O port
sub_2230_done:
    ret ; return
; --- Subroutine at 0x2244
sub_2244:
    mov ax, [0x26b]  ; load.
    neg ax  ; negate
    ; nop - Removed, no functional impact and likely an assembler artifact

    ; The following block appears to be duplicated in the disassembly,
    ; likely due to a compiler optimization or obfuscation technique.
    ; We include it only once for clarity and to avoid unnecessary
    ; code duplication.
    ; Original disassembled code (duplicated):
    ; adc  bx, byte +0x0
    ; cmp  bx, [0x26b]
    ; add  ax, 0x1
    ; jnz  short sub_224c_continue
    ; mov  ax, [0x26b]
    ; neg  ax
    ; adc  bx, byte +0x0
    ; cmp  bx, [0x26b]
    ; add  ax, 0x1
    ; jnz  short sub_2260_continue

    ; Simplified, functionally equivalent code:
    adc  bx, byte +0x0       ; Add carry to BX
    cmp  bx, [0x26b]     ; Compare BX with a value
    add  ax, 0x1             ; Increment AX
    jnz short sub_224c_continue ; Jump if not zero.

    mov ax, [0x26b] ; load again
    neg ax  ; negate
    adc bx, byte + 0x0 ; add carry
    cmp bx, [0x26b]
    add ax, 0x1
    jnz short sub_2260_continue ; jump

sub_224c_continue:
sub_2260_continue:
    ret
; --- Subroutine at 0x226D ---
; Writes a value to an I/O port and then reads data in a loop.

sub_226d:
    push bx                 ; Save BX
    push ax                ; Save AX

    mov  dx, [0x25e]      ; Load I/O port address <--- This is the ID PORT
    call sub_1bb3      ; Call a subroutine

    mov  dx, [bx]     ;
    call sub_1bbc    ;

    mov  dx, 0x2cb1    ; Load I/O port address  #TODO Remove hardcoded value and replace with define
    call sub_1bbc     ; Call a subroutine
    pop  bx                ; Restore BX
    call sub_1bc1    ;
    pop  bx                 ; Restore BX
    ret                    ; Return
; --- Subroutine at 0x2287 ---
;
;

sub_2287:
    push bx
    mov dx, 0x2cb6
    call sub_1bb3
    mov dx, [bx] ;
    call sub_1bbc

    mov dx, 0x2d3b
    call sub_1bb3
    mov dx,0x2d61
    call sub_1bb3 ;
    call sub_1c9e ; get char
    pop bx
    ret
; --- Subroutine at 0x230F ---
;
;
sub_230f:
    cmp byte [0x265], 0x3 ;
    jnz sub_2332

    mov byte [0x266], 0x1 ;
    mov dx, 0x2d80  ; message
    call sub_1bb3 ;
    ret
; --- Subroutine at 0x231C ---
;
;

sub_231c:
    call sub_1ba5 ; skip spaces
    call sub_1b56 ; convert to number
    cmp byte [0x265], 0x3 ; check media type
    jz sub_2332

    cmp ax, 0x200
    jc sub_2333
    cmp ax, 0x3f0
    ja sub_2333
    mov [0x260],ax ; store
    call sub_2287
    ret

sub_2332:
sub_2333:
    ret
; --- Subroutine at 0x2345 ---
;
;

sub_2345:
    call sub_1ba5 ; skip spaces
    call sub_1b56 ;
    cmp ax, 0x0 ;
    jc sub_23a1  ; jump if carry (error)
    cmp ax, 0x7f8
    ja sub_23a1

    ; --- Store values related to packet size limits
    mov [0x2bb], ax ;
    mov [0x2cf], ax ;
    mov [0x2c5], ax ;
    call sub_2287
    ret

sub_23a0:
sub_23a1:
    ret
; --- Subroutine at 0x235E ---
;
;
sub_235e:
    call sub_1ba5 ; skip spaces
    call sub_1b56
    cmp ax, 0x7f0
    ja sub_23b8

    mov [0x2db], ax
    call sub_2287  ;
    ret ;

sub_23b7:
sub_23b8:
    ret

; --- Subroutine at 0x2371 ---
;
;

sub_2371:
    call sub_1ba5 ;
    call sub_1b56 ;
    mov [0x450], ax ; store result
    call sub_2287
    ret
; --- Placeholder/Padding ---
; The following instructions are likely placeholders or remnants of
; compiler optimizations. They don't have a clear functional purpose in
; the current context. We preserve them for structural fidelity.
    add [bx+si], al
    add [bp+si+0x2d9a], bh
