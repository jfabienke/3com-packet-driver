; @file hweep.asm
; @brief EEPROM operations for 3Com NICs - HOT section module (stays resident)
;
; 3Com Packet Driver - EEPROM read operations for 3C509B and 3C515-TX NICs
; This module contains resident code for runtime MAC address queries
;
; Created: 2026-01-25 from hardware.asm modularization
; Last modified: 2026-01-25 11:32:59
;
; This file is part of the 3Com Packet Driver project.

bits 16
cpu 386

; Include assembly interface definitions
%define HARDWARE_MODULE_DEFINING
%include "asm_interfaces.inc"

;=============================================================================
; CONSTANTS
;=============================================================================

; Window select register (relative to base)
REG_WINDOW          EQU 0Eh     ; Window select register

;=============================================================================
; EXTERNAL DATA REFERENCES
;=============================================================================

extern hw_mac_addresses         ; MAC address storage (MAX_HW_INSTANCES * 6 bytes)
extern hw_type_table            ; NIC types (1=3C509B, 2=3C515)
extern hw_iobase_table          ; I/O base addresses
extern io_error_count           ; I/O error counter

;=============================================================================
; GLOBAL EXPORTS
;=============================================================================

global hardware_get_address
global read_mac_from_eeprom_3c509b
global read_3c509b_eeprom
global read_3c515_eeprom
global log_hardware_error

;=============================================================================
; JIT MODULE HEADER
;=============================================================================
segment MODULE class=MODULE align=16

global _mod_hweep_header
_mod_hweep_header:
    db  'PKTDRV', 0             ; +00  7 bytes: module signature
    db  1                       ; +07  1 byte:  major version
    db  0                       ; +08  1 byte:  minor version
    db  0                       ; +09  1 byte:  cpu_req
    db  0                       ; +0A  1 byte:  nic_type (0 = generic)
    db  1                       ; +0B  1 byte:  cap_flags (1 = MOD_CAP_CORE)
    dw  hot_end - hot_start     ; +0C  2 bytes: hot code size
    dw  patch_table             ; +0E  2 bytes: offset to patch table
    dw  patch_table_end - patch_table  ; +10  2 bytes: patch table size
    dw  hot_start               ; +12  2 bytes: offset to hot_start
    dw  hot_end                 ; +14  2 bytes: offset to hot_end
    times 64 - ($ - _mod_hweep_header) db 0  ; Pad to 64 bytes

;=============================================================================
; CODE SECTION - HOT (stays resident for runtime MAC queries)
;=============================================================================

section .text
hot_start:

;-----------------------------------------------------------------------------
; hardware_get_address - Get MAC address from hardware
;
; Input:  AL = NIC index, ES:DI = buffer (6 bytes)
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, SI, DI
;
; HOT PATH: Called at runtime to retrieve MAC addresses
;-----------------------------------------------------------------------------
hardware_get_address:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si

        ; Read MAC address from hardware EEPROM

        ; Validate NIC index
        cmp     al, MAX_HW_INSTANCES
        jae     .invalid_nic

        push    ax                      ; Save instance

        ; Get NIC type and I/O base
        mov     bl, al
        xor     bh, bh
        mov     si, hw_type_table
        mov     cl, [si+bx]             ; CL = NIC type

        shl     bx, 1                   ; Word offset
        mov     si, hw_iobase_table
        mov     dx, [si+bx]             ; DX = I/O base

        ; Branch based on NIC type
        cmp     cl, 1                   ; 3C509B
        je      .read_mac_3c509b
        cmp     cl, 2                   ; 3C515
        je      .read_mac_3c515

        ; Use cached MAC if type unknown
        jmp     .use_cached_mac

.read_mac_3c509b:
        ; Select Window 0 for EEPROM
        add     dx, REG_WINDOW
        mov     ax, 0800h               ; CMD_SELECT_WINDOW | 0
        out     dx, ax
        sub     dx, REG_WINDOW          ; Back to base

        ; Read 3 words of MAC from EEPROM addresses 0-2
        xor     si, si                  ; EEPROM address
        mov     cx, 3                   ; 3 words

.read_eeprom_3c509b:
        push    cx
        ; Send EEPROM read command
        mov     ax, dx                  ; Save base
        add     dx, 0Ah                 ; EEPROM command register
        mov     cx, 80h                 ; EEPROM_READ_CMD
        or      cx, si                  ; Add address
        mov     ax, cx
        out     dx, ax

        ; Wait for EEPROM ready (162us typical)
        mov     cx, 50                  ; Retry count
.wait_eeprom_3c509b:
        push    cx
        mov     cx, 6                   ; ~20us delay
.delay_eeprom:
        in      al, 80h                 ; I/O delay
        loop    .delay_eeprom
        pop     cx

        in      ax, dx                  ; Check status
        test    ah, 80h                 ; EEPROM_BUSY bit
        jz      .eeprom_ready_3c509b
        loop    .wait_eeprom_3c509b

        ; Timeout - use cached
        pop     cx
        jmp     .use_cached_mac

.eeprom_ready_3c509b:
        ; Read data word
        mov     ax, dx
        sub     ax, 0Ah                 ; Back to base
        mov     dx, ax
        add     dx, 0Ch                 ; EEPROM data register
        in      ax, dx

        ; Store in buffer (convert endianness)
        xchg    ah, al
        stosw                           ; Store to ES:DI

        mov     dx, ax                  ; Restore base
        sub     dx, 0Ch
        inc     si                      ; Next EEPROM address
        pop     cx
        loop    .read_eeprom_3c509b

        jmp     .mac_read_done

.read_mac_3c515:
        ; Similar process for 3C515
        add     dx, REG_WINDOW
        mov     ax, 0800h
        out     dx, ax
        sub     dx, REG_WINDOW

        mov     si, 0Ah                 ; MAC starts at 0x0A in 3C515 EEPROM
        mov     cx, 3

.read_eeprom_3c515:
        push    cx
        add     dx, 0Ah
        mov     ax, 200h                ; EEPROM_READ_3C515
        or      ax, si
        out     dx, ax

        mov     cx, 100
.wait_3c515:
        push    cx
        mov     cx, 10
.delay_3c515:
        in      al, 80h
        loop    .delay_3c515
        pop     cx

        in      ax, dx
        test    ah, 80h
        jz      .ready_3c515
        loop    .wait_3c515

        pop     cx
        jmp     .use_cached_mac

.ready_3c515:
        add     dx, 2                   ; Data register
        in      ax, dx
        xchg    ah, al
        stosw
        sub     dx, 2
        sub     dx, 0Ah
        inc     si
        pop     cx
        loop    .read_eeprom_3c515

        jmp     .mac_read_done

.use_cached_mac:
        ; Fall back to cached MAC address
        pop     ax                      ; Restore instance
        push    ax
        mov     bl, al
        xor     bh, bh
        mov     cl, 6
        mov     al, bl
        mul     cl                      ; AX = index * 6
        mov     si, hw_mac_addresses
        add     si, ax
        mov     cx, 6
        rep     movsb

.mac_read_done:
        pop     ax                      ; Restore instance

        ; Success
        mov     ax, 0
        jmp     .exit

.invalid_nic:
        mov     ax, 1
        jmp     .exit

.exit:
        pop     si
        pop     cx
        pop     bx
        pop     bp
        ret
;; end hardware_get_address

;-----------------------------------------------------------------------------
; read_mac_from_eeprom_3c509b - Read MAC address from 3C509B EEPROM
;
; Input:  AL = instance index, SI = I/O base address
; Output: MAC address stored in hw_mac_addresses for instance
; Uses:   All registers
;-----------------------------------------------------------------------------
read_mac_from_eeprom_3c509b:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    di
        push    si

        ; Calculate MAC storage offset
        mov     bl, al
        xor     bh, bh
        mov     cl, 6
        mov     al, bl
        mul     cl                      ; AX = index * 6
        mov     di, hw_mac_addresses
        add     di, ax                  ; DI = MAC storage location

        ; Select Window 0 for EEPROM access
        mov     dx, si
        add     dx, REG_WINDOW
        mov     ax, 0800h               ; CMD_SELECT_WINDOW | 0
        out     dx, ax

        ; Read 3 words of MAC address from EEPROM
        xor     bx, bx                  ; EEPROM address counter
        mov     cx, 3                   ; 3 words to read

.read_mac_loop:
        push    cx

        ; Send EEPROM read command
        mov     dx, si
        add     dx, 0Ah                 ; EEPROM command register
        mov     ax, 80h                 ; EEPROM_READ_CMD
        or      ax, bx                  ; Add address (0-2)
        out     dx, ax

        ; Wait for EEPROM ready (162us typical, 1ms max)
        mov     cx, 50                  ; Retry count
.wait_eeprom:
        push    cx
        ; Delay ~20us
        mov     cx, 6
.delay_loop:
        in      al, 80h                 ; I/O delay
        loop    .delay_loop
        pop     cx

        ; Check EEPROM busy bit
        in      ax, dx
        test    ah, 80h                 ; EEPROM_BUSY bit
        jz      .eeprom_ready
        loop    .wait_eeprom

        ; Timeout - use default MAC
        pop     cx
        jmp     .use_default_mac

.eeprom_ready:
        ; Read the data word
        mov     dx, si
        add     dx, 0Ch                 ; EEPROM data register
        in      ax, dx

        ; Store in MAC buffer (convert endianness)
        xchg    ah, al
        mov     [di], ax
        add     di, 2
        inc     bx                      ; Next EEPROM address
        pop     cx
        loop    .read_mac_loop

        jmp     .mac_read_complete

.use_default_mac:
        ; Set default MAC if EEPROM read fails
        mov     word [di], 0201h        ; 00:01:02
        mov     word [di+2], 0403h      ; 03:04:05
        mov     word [di+4], 0605h

.mac_read_complete:
        pop     si
        pop     di
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end read_mac_from_eeprom_3c509b

;-----------------------------------------------------------------------------
; read_3c509b_eeprom - Read 3C509B EEPROM with defensive programming
;
; Input:  DX = I/O base address
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
read_3c509b_eeprom:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Select window 0 for EEPROM access
        SELECT_3C509B_WINDOW dx, 0

        ; Read EEPROM with timeout protection
        mov     cx, 16                      ; Read 16 words
        mov     bx, 0                       ; Starting address

.eeprom_read_loop:
        ; Set EEPROM address
        push    dx
        add     dx, 0Ah                     ; EEPROM command register
        mov     ax, 80h                     ; Read command
        or      ax, bx                      ; OR with address
        out     dx, ax
        pop     dx

        ; Wait for EEPROM ready with timeout
        WAIT_FOR_CONDITION dx+0Ah, 8000h, TIMEOUT_MEDIUM
        jc      .eeprom_timeout

        ; Read EEPROM data
        push    dx
        add     dx, 0Ch                     ; EEPROM data register
        in      ax, dx
        pop     dx

        ; Store data (simplified - would store in MAC address array)
        ; For now just validate it's not 0
        test    ax, ax
        jz      .invalid_eeprom

        inc     bx
        dec     cx
        jnz     .eeprom_read_loop

        mov     ax, HW_SUCCESS
        jmp     .exit

.eeprom_timeout:
        mov     ax, HW_ERROR_TIMEOUT
        jmp     .exit

.invalid_eeprom:
        mov     ax, HW_ERROR_EEPROM_READ

.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end read_3c509b_eeprom

;-----------------------------------------------------------------------------
; read_3c515_eeprom - Read 3C515-TX EEPROM
;
; Input:  DX = I/O base address
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
read_3c515_eeprom:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Select window 0 for EEPROM access
        SELECT_3C515_WINDOW dx, 0

        ; Read EEPROM with timeout protection (similar to 3C509B but different registers)
        mov     cx, 16                      ; Read 16 words
        mov     bx, 0                       ; Starting address

.eeprom_read_loop:
        ; Set EEPROM address
        push    dx
        add     dx, 200Ah                   ; EEPROM command register for 3C515
        mov     ax, 80h                     ; Read command
        or      ax, bx                      ; OR with address
        out     dx, ax
        pop     dx

        ; Wait for EEPROM ready with timeout
        WAIT_FOR_CONDITION dx+200Ah, 8000h, TIMEOUT_MEDIUM
        jc      .eeprom_timeout

        ; Read EEPROM data
        push    dx
        add     dx, 200Ch                   ; EEPROM data register
        in      ax, dx
        pop     dx

        ; Validate data
        test    ax, ax
        jz      .invalid_eeprom

        inc     bx
        dec     cx
        jnz     .eeprom_read_loop

        mov     ax, HW_SUCCESS
        jmp     .exit

.eeprom_timeout:
        mov     ax, HW_ERROR_TIMEOUT
        jmp     .exit

.invalid_eeprom:
        mov     ax, HW_ERROR_EEPROM_READ

.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end read_3c515_eeprom

;-----------------------------------------------------------------------------
; log_hardware_error - Log hardware error
;
; Input:  AX = error code
; Output: None
; Uses:   None (preserves all registers)
;-----------------------------------------------------------------------------
log_hardware_error:
        push    ax
        push    bx
        push    cx
        push    dx

        ; Increment error counter
        inc     word [io_error_count]

        ; Could add more sophisticated logging here
        ; For now, just count errors

        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
;; end log_hardware_error

hot_end:

patch_table:
patch_table_end:

;=============================================================================
; END OF MODULE
;=============================================================================
