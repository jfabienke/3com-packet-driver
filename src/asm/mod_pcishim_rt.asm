; mod_pcishim_rt.asm - INT 1Ah PCI BIOS shim JIT module
; Created: 2026-02-01 20:15:00 (timestamp)
;
; Intercepts INT 1Ah PCI BIOS calls and redirects broken BIOS functions
; to Mechanism #1 port I/O (0xCF8/0xCFC). Designed to work around buggy
; BIOS implementations that fail PCI configuration space access.
;
; PCI Mechanism #1 requires 32-bit I/O to 0xCF8, so this module requires
; a 386+ CPU.

bits 16
%include "patch_macros.inc"

section .text class=MODULE

global _mod_pcishim_rt_header
_mod_pcishim_rt_header:
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
    db 2          ; cpu_req: 386 (PCI config space needs 32-bit I/O)
    db 0          ; nic_type: any
    dw 0          ; cap_flags: core functionality, no special caps
    times (64 - ($ - header)) db 0

hot_start:

;------------------------------------------------------------------------------
; pci_shim_get_stats_ - Stub for C linkage (no leading underscore)
;------------------------------------------------------------------------------
global pci_shim_get_stats_
pci_shim_get_stats_:
    xor ax, ax
    retf

;------------------------------------------------------------------------------
; pci_shim_handler_ - Stub for C linkage (no leading underscore)
;------------------------------------------------------------------------------
global pci_shim_handler_
pci_shim_handler_:
    xor ax, ax
    retf

;------------------------------------------------------------------------------
; pci_shim_handler_ - INT 1Ah ISR handler
;
; Entry: AH = BIOS function (0xB1 for PCI BIOS)
;        AL = PCI subfunction (if AH=0xB1)
;        BH = bus number
;        BL = device/function (bits 7:3=dev, 2:0=func)
;        DI = register number
;        Other regs depend on subfunction
; Exit:  Depends on function; CF clear on success, set on error
;
; Watcom far call convention with trailing underscore, uses iret/jmp
;------------------------------------------------------------------------------
global _pci_shim_handler_
_pci_shim_handler_:
    ; Check if this is a PCI BIOS call (AH == 0xB1)
    cmp ah, 0xB1
    jne .chain_to_old

    ; Increment total call counter
    push ds
    push bx
    push cs
    pop ds
    mov bx, shim_calls
    inc word [bx]
    jnz .no_carry1
    inc word [bx+2]
.no_carry1:
    pop bx
    pop ds

    ; Check if this subfunction is broken (AL should be 08h-0Dh for config space)
    ; Broken functions: 08=read_byte, 09=read_word, 0A=read_dword,
    ;                   0B=write_byte, 0C=write_word, 0D=write_dword
    ; Map AL to bit number: bit = AL - 8
    push ax
    push cx
    sub al, 8
    cmp al, 5           ; Valid range is 0-5 (6 functions)
    ja .not_broken      ; If > 5, not a broken function we handle

    ; Test if this bit is set in broken_mask
    mov cl, al          ; CL = bit number (0-5)
    mov ax, 1
    shl ax, cl          ; AX = bitmask for this function
    push ds
    push bx
    push cs
    pop ds
    mov bx, broken_mask
    test [bx], ax       ; Test if this function is marked broken
    pop bx
    pop ds
    pop cx
    pop ax
    jz .chain_to_old    ; If not broken, chain to BIOS

    ; This function is broken - handle via Mechanism #1
    jmp .handle_mechanism1

.not_broken:
    pop cx
    pop ax

.chain_to_old:
    ; Chain to original INT 1Ah handler
    jmp far [cs:old_int1a]

;------------------------------------------------------------------------------
; Handle PCI config space access via Mechanism #1 (0xCF8/0xCFC)
;
; Entry: AL = PCI subfunction (08h-0Dh)
;        BH = bus number
;        BL = device/function
;        DI = register number
;        CX/ECX = data to write (for write functions)
; Exit:  AH = 0 on success
;        CF clear on success
;        CX/ECX = data read (for read functions)
;------------------------------------------------------------------------------
.handle_mechanism1:
    ; Increment fallback counter
    push ds
    push bx
    push cs
    pop ds
    mov bx, fallback_calls
    inc word [bx]
    jnz .no_carry2
    inc word [bx+2]
.no_carry2:
    pop bx
    pop ds

    ; Build config address in DX:AX
    ; Config address = 0x80000000 | (bus<<16) | (dev<<11) | (func<<8) | (reg & 0xFC)
    ; DX:AX will hold the 32-bit config address

    push bx
    push cx
    push di

    ; Start with 0x80000000 in DX:AX
    mov dx, 0x8000      ; High word
    xor ax, ax          ; Low word = 0

    ; OR in (bus << 16) - bus goes into DX bits 7:0
    mov cl, bh          ; CL = bus number
    or dh, cl           ; DX = 0x8000 | (bus << 8)

    ; OR in (dev << 11) - dev is bits 7:3 of BL
    mov cl, bl
    and cl, 0xF8        ; Mask off function bits, keep device (bits 7:3)
    shl cl, 1
    shl cl, 1
    shl cl, 1           ; CL = dev << 3
    or al, cl           ; AX low byte |= (dev << 3)

    ; OR in (func << 8) - func is bits 2:0 of BL
    mov cl, bl
    and cl, 0x07        ; Mask to get function bits 2:0
    or ah, cl           ; AX high byte |= func

    ; OR in (reg & 0xFC) - register number with low 2 bits cleared
    mov cx, di
    and cl, 0xFC        ; Clear low 2 bits
    or al, cl           ; AX low byte |= (reg & 0xFC)

    ; Now DX:AX contains the 32-bit config address
    ; Write it to port 0xCF8 using two 16-bit writes
    ; Must use .386 opcodes for 32-bit out
    push dx
    push ax

    ; Write low word to 0xCF8, high word to 0xCFA
    mov dx, 0x0CF8
    pop ax              ; AX = low word of config address
    out dx, ax

    add dx, 2           ; DX = 0x0CFA
    pop ax              ; AX = high word of config address
    out dx, ax

    ; Now access the data at 0xCFC + (reg & 3)
    mov dx, 0x0CFC
    mov ax, di
    and ax, 3           ; AX = offset within dword (0-3)
    add dx, ax          ; DX = 0xCFC + (reg & 3)

    ; Restore original subfunction code
    pop di
    pop cx
    pop bx
    push ax             ; Save calculated port for later
    mov ax, [esp+6]     ; Get original AX from stack (beyond our pushes)

    ; Dispatch based on subfunction
    cmp al, 0x08
    je .read_byte
    cmp al, 0x09
    je .read_word
    cmp al, 0x0A
    je .read_dword
    cmp al, 0x0B
    je .write_byte
    cmp al, 0x0C
    je .write_word
    cmp al, 0x0D
    je .write_dword

    ; Should never get here
    pop ax
    jmp .error

.read_byte:
    pop ax              ; Discard saved port (DX already set)
    in al, dx           ; Read byte from config space
    mov cl, al          ; Return in CL per PCI BIOS spec
    jmp .success

.read_word:
    pop ax              ; Discard saved port
    in ax, dx           ; Read word from config space
    mov cx, ax          ; Return in CX per PCI BIOS spec
    jmp .success

.read_dword:
    pop ax              ; Discard saved port
    ; Need to read 32-bit value - use .386 opcode
    db 0x66             ; Operand size override
    in ax, dx           ; in eax, dx - Read dword from config space
    ; Return in ECX per PCI BIOS spec
    db 0x66             ; Operand size override
    mov cx, ax          ; mov ecx, eax
    jmp .success

.write_byte:
    pop ax              ; Discard saved port
    mov al, cl          ; Get byte to write from CL per PCI BIOS spec
    out dx, al          ; Write byte to config space
    jmp .success

.write_word:
    pop ax              ; Discard saved port
    mov ax, cx          ; Get word to write from CX per PCI BIOS spec
    out dx, ax          ; Write word to config space
    jmp .success

.write_dword:
    pop ax              ; Discard saved port
    ; Need to write 32-bit value - use .386 opcode
    db 0x66             ; Operand size override
    mov ax, cx          ; mov eax, ecx - Get dword from ECX per PCI BIOS spec
    db 0x66             ; Operand size override
    out dx, ax          ; out dx, eax - Write dword to config space
    jmp .success

.success:
    ; Set return values: AH=0 (success), CF clear
    mov ah, 0
    clc
    iret

.error:
    ; Set return values: AH=error code, CF set
    mov ah, 0x87        ; Invalid parameter error
    stc
    iret

;------------------------------------------------------------------------------
; pci_shim_get_stats_ - Get statistics
;
; Entry: DX:AX = far pointer to calls_out (dword)
;        CX:BX = far pointer to fallbacks_out (dword)
; Exit:  none (writes stats to buffers)
;
; Watcom far call convention, uses retf
;------------------------------------------------------------------------------
global _pci_shim_get_stats_
_pci_shim_get_stats_:
    push ds
    push es
    push si
    push di

    ; Write shim_calls to DX:AX buffer
    mov es, dx
    mov di, ax
    push cs
    pop ds
    mov si, shim_calls
    movsb
    movsb
    movsb
    movsb

    ; Write fallback_calls to CX:BX buffer
    mov es, cx
    mov di, bx
    mov si, fallback_calls
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
old_int1a:      dd 0        ; saved INT 1Ah vector (4 bytes)
broken_mask:    dw 0        ; bitmask of broken subfunctions (patched by init)
                            ; bit 0=read_byte(08h), bit 1=read_word(09h),
                            ; bit 2=read_dword(0Ah), bit 3=write_byte(0Bh),
                            ; bit 4=write_word(0Ch), bit 5=write_dword(0Dh)
shim_calls:     dd 0        ; total calls intercepted
fallback_calls: dd 0        ; calls handled via Mechanism #1
mechanism:      db 1        ; PCI access mechanism (1=standard)
shim_installed: db 0        ; installation flag

hot_end:

;------------------------------------------------------------------------------
; Patch table (empty for this module)
;------------------------------------------------------------------------------
PATCH_COUNT equ 0

patch_table:
patch_table_end:
