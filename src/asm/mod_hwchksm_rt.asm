;==============================================================================
; mod_hwchksm_rt.asm - Hardware Checksum Runtime Module (Software Implementation)
;==============================================================================
; NASM 16-bit JIT module for internet checksum calculation
; Implements ones-complement 16-bit checksumming for IP packets
;
; Created: 2026-02-01
; CPU: 8086 compatible
; Calling Convention: Watcom far call (trailing underscore, retf)
;==============================================================================

[CPU 8086]
[BITS 16]

%include "patch_macros.inc"

section .text class=MODULE

;==============================================================================
; MODULE HEADER (64 bytes, mod_pio.asm style)
;==============================================================================
global _mod_hwchksm_rt_header
_mod_hwchksm_rt_header:
    db 'MOD'                    ; +0: Magic signature
    db 1                        ; +3: Version
    dd mod_end - _mod_hwchksm_rt_header  ; +4: Module size
    dd 0                        ; +8: Capabilities (MOD_CAP_CORE would be defined)
    dw 0                        ; +12: CPU requirement (0 = 8086)
    dw 0                        ; +14: NIC type (0 = generic)
    times 48 db 0               ; +16-63: Reserved/padding

;==============================================================================
; GLOBAL DATA SECTION (Hot Data)
;==============================================================================
section .data

global global_checksum_stats
global_checksum_stats:
    times 56 db 0               ; checksum_stats_t structure (56 bytes)

checksum_mode:
    dw 0                        ; 0 = SOFTWARE mode

str_software:
    db "Software", 0

;==============================================================================
; CODE SECTION
;==============================================================================
section .text

;------------------------------------------------------------------------------
; sw_checksum_internet_ - Ones-complement 16-bit internet checksum
;------------------------------------------------------------------------------
; PARAMS:
;   DX:AX = far pointer to data buffer
;   BX = length in bytes
;   CX = initial value (16-bit sum)
; RETURNS:
;   AX = ones-complement checksum (16-bit)
; NOTES:
;   - Optimized for 8086 with unrolled loop
;   - Handles odd-length buffers
;   - Folds carries into final result
;------------------------------------------------------------------------------
global sw_checksum_internet_
sw_checksum_internet_:
    push ds
    push si
    push di
    push bp

    ; Load DS:SI from DX:AX
    mov ds, dx
    mov si, ax

    ; Initialize sum in DI with initial value
    mov di, cx

    ; Check if length is zero
    test bx, bx
    jz .done

    ; Check if SI is odd-aligned
    test si, 1
    jz .check_length

    ; Handle odd starting address - load one byte
    xor ax, ax
    lodsb                       ; AL = [DS:SI], SI++
    xchg ah, al                 ; Move byte to high position
    add di, ax                  ; Add to sum
    adc di, 0                   ; Fold carry
    dec bx                      ; Decrement length
    jz .done                    ; Exit if no more bytes

.check_length:
    ; BX = remaining bytes
    ; Check if we have at least 8 bytes for unrolled loop
    cmp bx, 8
    jb .word_loop_setup

.unrolled_loop:
    ; Unroll 4 words (8 bytes) per iteration
    lodsw                       ; AX = [DS:SI], SI+=2
    add di, ax
    adc di, 0

    lodsw
    add di, ax
    adc di, 0

    lodsw
    add di, ax
    adc di, 0

    lodsw
    add di, ax
    adc di, 0

    sub bx, 8
    cmp bx, 8
    jae .unrolled_loop

.word_loop_setup:
    ; Process remaining words
    test bx, bx
    jz .done

.word_loop:
    cmp bx, 1
    jbe .last_byte              ; Only one byte left

    lodsw                       ; AX = [DS:SI], SI+=2
    add di, ax
    adc di, 0
    sub bx, 2
    jnz .word_loop
    jmp .done

.last_byte:
    ; Handle trailing odd byte
    xor ax, ax
    lodsb                       ; AL = [DS:SI]
    xchg ah, al                 ; Move to high byte
    add di, ax
    adc di, 0

.done:
    ; DI contains 16-bit accumulated sum
    ; Fold any carries from accumulation
    mov ax, di
    xor ax, 0xFFFF              ; Ones-complement

    pop bp
    pop di
    pop si
    pop ds
    retf

;------------------------------------------------------------------------------
; hw_checksum_tx_calculate_ - Calculate checksum for TX packet
;------------------------------------------------------------------------------
; PARAMS:
;   AX = nic_index
;   DX:BX = far pointer to packet
;   CX = length
; RETURNS:
;   AX = checksum value
; NOTES:
;   Software mode: calculates IP header checksum at offset 14
;------------------------------------------------------------------------------
global hw_checksum_tx_calculate_
hw_checksum_tx_calculate_:
    push ds
    push si
    push di
    push bp

    ; Increment stats: tx_packets
    push ax
    mov ax, seg global_checksum_stats
    mov ds, ax
    inc word [global_checksum_stats + 0]  ; tx_packets (low word)
    pop ax

    ; Load packet pointer DS:SI = DX:BX + 14 (IP header offset)
    mov ds, dx
    mov si, bx
    add si, 14                  ; Skip Ethernet header

    ; Read IHL field (IP header byte 0, low nibble)
    mov al, [si]
    and al, 0x0F                ; Extract IHL
    mov ah, 0
    shl ax, 1                   ; IHL * 4 (shift left 2, but we'll do it in steps)
    shl ax, 1
    mov bp, ax                  ; BP = IP header length

    ; Prepare for sw_checksum_internet_ call
    ; DX:AX = far pointer (already DS:SI)
    mov ax, si
    mov dx, ds
    mov bx, bp                  ; BX = IP header length
    xor cx, cx                  ; CX = initial value (0)

    pop bp
    pop di
    pop si
    pop ds

    ; Tail call to sw_checksum_internet_
    jmp sw_checksum_internet_

;------------------------------------------------------------------------------
; hw_checksum_rx_validate_ - Validate checksum for RX packet
;------------------------------------------------------------------------------
; PARAMS:
;   AX = nic_index
;   DX:BX = far pointer to packet
;   CX = length
; RETURNS:
;   AX = 0 if valid, -1 if invalid
;------------------------------------------------------------------------------
global hw_checksum_rx_validate_
hw_checksum_rx_validate_:
    push ds
    push si
    push di
    push bp

    ; Increment stats: rx_packets
    push ax
    mov ax, seg global_checksum_stats
    mov ds, ax
    inc word [global_checksum_stats + 4]  ; rx_packets (low word)
    pop ax

    ; Load packet pointer DS:SI = DX:BX + 14
    mov ds, dx
    mov si, bx
    add si, 14

    ; Read IHL field
    mov al, [si]
    and al, 0x0F
    mov ah, 0
    shl ax, 1
    shl ax, 1
    mov bp, ax                  ; BP = IP header length

    ; Calculate checksum
    mov ax, si
    mov dx, ds
    mov bx, bp
    xor cx, cx

    ; Save return address info
    push cs
    call near .after_checksum

.after_checksum:
    ; AX contains checksum result
    ; Valid checksum should be 0xFFFF or 0x0000
    test ax, ax
    jz .valid
    cmp ax, 0xFFFF
    jz .valid

    ; Invalid - increment error stats
    push ds
    mov bx, seg global_checksum_stats
    mov ds, bx
    inc word [global_checksum_stats + 16]  ; errors (low word)
    pop ds

    mov ax, -1                  ; Return -1 (invalid)
    jmp .done

.valid:
    ; Increment valid stats
    push ds
    mov bx, seg global_checksum_stats
    mov ds, bx
    inc word [global_checksum_stats + 8]   ; valid (low word)
    pop ds

    xor ax, ax                  ; Return 0 (valid)

.done:
    pop bp
    pop di
    pop si
    pop ds
    retf

;------------------------------------------------------------------------------
; hw_checksum_calculate_ip_ - Calculate and store IP header checksum
;------------------------------------------------------------------------------
; PARAMS:
;   DX:AX = far pointer to IP header
;   BX = header length
; RETURNS:
;   AX = calculated checksum
;------------------------------------------------------------------------------
global hw_checksum_calculate_ip_
hw_checksum_calculate_ip_:
    push ds
    push si
    push di
    push bp

    ; Save pointer and length
    mov si, ax                  ; SI = offset
    mov ds, dx                  ; DS = segment
    mov bp, bx                  ; BP = length

    ; Zero the checksum field (bytes 10-11 of IP header)
    mov word [si + 10], 0

    ; Prepare parameters for sw_checksum_internet_
    mov ax, si                  ; DX:AX = far pointer
    ; DX already set
    mov bx, bp                  ; BX = length
    xor cx, cx                  ; CX = initial value (0)

    ; Calculate checksum
    push cs
    call near .after_checksum

.after_checksum:
    ; Store checksum back into header
    mov di, si
    mov [di + 10], ax           ; Store at offset 10

    ; AX already contains the checksum to return

    pop bp
    pop di
    pop si
    pop ds
    retf

;------------------------------------------------------------------------------
; hw_checksum_is_supported_ - Check if hardware checksum is supported
;------------------------------------------------------------------------------
; RETURNS:
;   AX = 0 (software only, no hardware support)
;------------------------------------------------------------------------------
global hw_checksum_is_supported_
hw_checksum_is_supported_:
    xor ax, ax                  ; Return 0
    retf

;------------------------------------------------------------------------------
; hw_checksum_get_optimal_mode_ - Get optimal checksum mode
;------------------------------------------------------------------------------
; RETURNS:
;   AX = 0 (SOFTWARE mode)
;------------------------------------------------------------------------------
global hw_checksum_get_optimal_mode_
hw_checksum_get_optimal_mode_:
    xor ax, ax                  ; Return 0 (SOFTWARE)
    retf

;------------------------------------------------------------------------------
; hw_checksum_get_stats_ - Get checksum statistics
;------------------------------------------------------------------------------
; PARAMS:
;   DX:AX = far pointer to output buffer (56 bytes)
; RETURNS:
;   AX = 0 (success)
;------------------------------------------------------------------------------
global hw_checksum_get_stats_
hw_checksum_get_stats_:
    push ds
    push si
    push di
    push es
    push cx

    ; ES:DI = destination (output buffer)
    mov es, dx
    mov di, ax

    ; DS:SI = source (global_checksum_stats)
    mov ax, seg global_checksum_stats
    mov ds, ax
    mov si, global_checksum_stats

    ; Copy 56 bytes
    mov cx, 56
    rep movsb

    xor ax, ax                  ; Return 0 (success)

    pop cx
    pop es
    pop di
    pop si
    pop ds
    retf

;------------------------------------------------------------------------------
; hw_checksum_clear_stats_ - Clear checksum statistics
;------------------------------------------------------------------------------
; RETURNS:
;   AX = 0 (success)
;------------------------------------------------------------------------------
global hw_checksum_clear_stats_
hw_checksum_clear_stats_:
    push ds
    push di
    push cx
    push ax

    ; DS:DI = global_checksum_stats
    mov ax, seg global_checksum_stats
    mov ds, ax
    mov di, global_checksum_stats

    ; Zero 56 bytes
    mov cx, 56
    xor ax, ax
    rep stosb

    xor ax, ax                  ; Return 0 (success)

    pop ax
    pop cx
    pop di
    pop ds
    retf

;------------------------------------------------------------------------------
; hw_checksum_mode_to_string_ - Get string representation of checksum mode
;------------------------------------------------------------------------------
; RETURNS:
;   DX:AX = far pointer to mode string
;------------------------------------------------------------------------------
global hw_checksum_mode_to_string_
hw_checksum_mode_to_string_:
    mov dx, seg str_software
    mov ax, str_software
    retf

;==============================================================================
; MODULE END MARKER
;==============================================================================
section .text
mod_end:

; Patch count for this module
PATCH_COUNT equ 0

;==============================================================================
; END OF mod_hwchksm_rt.asm
;==============================================================================
