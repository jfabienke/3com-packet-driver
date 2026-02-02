; mod_pcimux_rt.asm - INT 2Fh multiplex handler JIT module
; Created: 2026-02-01 (timestamp)
;
; Provides DOS multiplex interrupt handling for packet driver shim control.
; Implements function 0xB1 for install check, enable/disable, stats, etc.

bits 16
%include "patch_macros.inc"

; Symbol definitions (must precede header usage)
MOD_CAP_CORE    equ 8000h       ; Core module capability flag (matches modhdr.h)
PATCH_COUNT     equ 0           ; Number of patches in this module

section .text class=MODULE

global _mod_pcimux_rt_header
_mod_pcimux_rt_header:
header:
    db 'PKTDRV',0
    db 1, 0
    dw hot_start
    dw hot_end
    dw 0, 0
    dw patch_table
    dw PATCH_COUNT
    dw (hot_end - header)
    dw (hot_end - hot_start)
    db 0          ; cpu_req: 8086
    db 0          ; nic_type: any
    dw MOD_CAP_CORE
    times (64 - ($ - header)) db 0

hot_start:

;------------------------------------------------------------------------------
; multiplex_handler_ - INT 2Fh ISR handler
;
; Entry: AX = function (AH=0xB1 for our multiplex ID)
;        Other regs depend on subfunction
; Exit:  AX = result (depends on subfunction)
;        Other regs depend on subfunction
;
; Watcom far call convention with trailing underscore, uses iret/far jmp
;------------------------------------------------------------------------------
global _multiplex_handler_
_multiplex_handler_:
    ; Increment call counter
    push ds
    push bx
    push cs
    pop ds
    mov bx, mplex_calls
    inc word [bx]
    jnz .no_carry
    inc word [bx+2]
.no_carry:
    pop bx
    pop ds

    ; Check if this is our multiplex ID (AH == 0xB1)
    cmp ah, 0xB1
    je .our_function

    ; Not our function - chain to old handler
    jmp far [cs:old_int2f]

.our_function:
    ; Dispatch based on AL (subfunction)
    cmp al, 0x00
    je .install_check
    cmp al, 0x01
    je .enable_shim
    cmp al, 0x02
    je .disable_shim
    cmp al, 0x03
    je .get_stats
    cmp al, 0xFF
    je .uninstall_check

    ; Unknown subfunction - chain to old handler
    jmp far [cs:old_int2f]

.install_check:
    ; AL=0x00: Install check
    ; Return AX=0x00FF (installed)
    mov ax, 0x00FF
    iret

.enable_shim:
    ; AL=0x01: Enable shim
    push ds
    push bx
    push cs
    pop ds
    mov byte [shim_enabled], 1
    pop bx
    pop ds
    xor ax, ax
    iret

.disable_shim:
    ; AL=0x02: Disable shim
    push ds
    push bx
    push cs
    pop ds
    mov byte [shim_enabled], 0
    pop bx
    pop ds
    xor ax, ax
    iret

.get_stats:
    ; AL=0x03: Get stats
    ; ES:BX points to buffer to receive mplex_calls (4 bytes)
    push ds
    push si
    push di
    push cs
    pop ds
    mov si, mplex_calls
    mov di, bx
    ; Copy 4 bytes from DS:SI to ES:DI
    movsb
    movsb
    movsb
    movsb
    pop di
    pop si
    pop ds
    xor ax, ax
    iret

.uninstall_check:
    ; AL=0xFF: Uninstall check
    ; Return AX=0 (deny uninstall)
    xor ax, ax
    iret

;------------------------------------------------------------------------------
; multiplex_is_shim_enabled_ - Check if shim is enabled
;
; Entry: none
; Exit:  AX = shim_enabled flag (0 or 1)
;
; Watcom far call convention, uses retf
;------------------------------------------------------------------------------
global _multiplex_is_shim_enabled_
_multiplex_is_shim_enabled_:
    push ds
    push bx
    push cs
    pop ds
    xor ax, ax
    mov al, [shim_enabled]
    pop bx
    pop ds
    retf

;------------------------------------------------------------------------------
; multiplex_set_shim_enabled_ - Set shim enable flag
;
; Entry: AX = value to set (0 or 1)
; Exit:  none
;
; Watcom far call convention, uses retf
;------------------------------------------------------------------------------
global _multiplex_set_shim_enabled_
_multiplex_set_shim_enabled_:
    push ds
    push bx
    push cs
    pop ds
    mov byte [shim_enabled], al
    pop bx
    pop ds
    retf

;------------------------------------------------------------------------------
; multiplex_get_stats_ - Get pointer to stats and write to output buffer
;
; Entry: DX:AX = far pointer to output uint32_t buffer
; Exit:  none (writes mplex_calls to buffer)
;
; Watcom far call convention, uses retf
;------------------------------------------------------------------------------
global _multiplex_get_stats_
_multiplex_get_stats_:
    push ds
    push es
    push si
    push di

    ; Set up ES:DI to point to output buffer (DX:AX)
    mov es, dx
    mov di, ax

    ; Set up DS:SI to point to mplex_calls
    push cs
    pop ds
    mov si, mplex_calls

    ; Copy 4 bytes
    movsb
    movsb
    movsb
    movsb

    pop di
    pop si
    pop es
    pop ds
    retf

;------------------------------------------------------------------------------
; Data section (hot data, part of runtime image)
;------------------------------------------------------------------------------
align 2
old_int2f:    dd 0        ; saved INT 2Fh vector (4 bytes)
installed:    db 0        ; installation flag
shim_enabled: db 0        ; shim enable flag
mplex_calls:  dd 0        ; call counter (uint32_t)

hot_end:

;------------------------------------------------------------------------------
; Patch table (empty for this module)
;------------------------------------------------------------------------------
patch_table:
patch_table_end:
