;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file hardware_smc.asm
;; @brief Hardware Access Layer with Self-Modifying Code patch points
;;
;; Provides optimized hardware access routines for 3C509B and 3C515 NICs
;; with CPU-specific optimizations applied via SMC patching.
;;
;; This module focuses on:
;; - Port I/O operations
;; - EEPROM access
;; - PHY management
;; - Hardware reset/initialization
;;
;; Converted to NASM syntax - 2026-01-23
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        bits 16
        cpu 386                         ; Enable 386+ instructions (REP INSB/OUTSB)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; CPU Optimization Constants and Macros
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; External CPU optimization level from packet_ops.asm
extern current_cpu_opt

; CPU optimization level constants (must match packet_ops.asm)
OPT_8086            EQU 0       ; 8086/8088 baseline (no 186+ instructions)
OPT_16BIT           EQU 1       ; 186+ optimizations (INS/OUTS available)

;------------------------------------------------------------------------------
; REP_INSB_SAFE - Input byte array from port (8086-compatible)
; Input: ES:DI = dest buffer, DX = port, CX = byte count
; Clobbers: AL, CX, DI
;------------------------------------------------------------------------------
%macro REP_INSB_SAFE 0
        push bx
        mov bl, [current_cpu_opt]
        test bl, OPT_16BIT
        pop bx
        jnz %%use_insb
        ;; 8086 path: manual loop
        jcxz %%done
%%insb_loop:
        in al, dx
        stosb
        loop %%insb_loop
        jmp short %%done
%%use_insb:
        rep insb
%%done:
%endmacro

;------------------------------------------------------------------------------
; REP_OUTSB_SAFE - Output byte array to port (8086-compatible)
; Input: DS:SI = source buffer, DX = port, CX = byte count
; Clobbers: AL, CX, SI
;------------------------------------------------------------------------------
%macro REP_OUTSB_SAFE 0
        push bx
        mov bl, [current_cpu_opt]
        test bl, OPT_16BIT
        pop bx
        jnz %%use_outsb
        ;; 8086 path: manual loop
        jcxz %%done
%%outsb_loop:
        lodsb
        out dx, al
        loop %%outsb_loop
        jmp short %%done
%%use_outsb:
        rep outsb
%%done:
%endmacro

        segment _TEXT class=CODE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Module Header (64 bytes)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        align 16
module_header:
hardware_module_header:                  ; Export for C code
        global  hardware_module_header   ; Make visible to patch_apply.c
        db      'PKTDRV',0              ; 7+1 bytes: Signature
        db      1, 0                    ; 2 bytes: Version 1.0
        dw      hot_section_start       ; 2 bytes: Hot start
        dw      hot_section_end         ; 2 bytes: Hot end
        dw      cold_section_start      ; 2 bytes: Cold start
        dw      cold_section_end        ; 2 bytes: Cold end
        dw      patch_table             ; 2 bytes: Patch table
        dw      patch_count             ; 2 bytes: Number of patches
        dw      module_size             ; 2 bytes: Total size
        dw      1024                    ; 2 bytes: Required memory (1KB)
        db      2                       ; 1 byte: Min CPU (286)
        db      0                       ; 1 byte: NIC type (any)
        times 37 db 0                   ; 37 bytes: Reserved

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; HOT SECTION - Resident hardware access routines
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        align 16
hot_section_start:

        ; Public exports
        global  hw_read_register
        global  hw_write_register
        global  hw_read_eeprom
        global  hw_reset_nic
        global  hw_read_block
        global  hw_write_block

        ; Constants
        EEPROM_BUSY     equ     0x8000
        EEPROM_CMD_READ equ     0x0180

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Read NIC Register (Optimized)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
hw_read_register:
        ; Input: DX = base port, BX = register offset
        ; Output: AX = value read
        push    dx
        add     dx, bx

        ; PATCH POINT: Optimized register read
PATCH_reg_read:
        in      al, dx                  ; 1 byte: 8-bit read
        xor     ah, ah                  ; 2 bytes: clear high
        nop                             ; 2 bytes padding
        nop
        ; Will be patched to:
        ; 286+: IN AX, DX (1 byte, 16-bit)
        ; 386+: Could use 32-bit but usually not needed

        pop     dx
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Write NIC Register (Optimized)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
hw_write_register:
        ; Input: DX = base port, BX = register offset, AX = value
        push    dx
        add     dx, bx

        ; PATCH POINT: Optimized register write
PATCH_reg_write:
        out     dx, al                  ; 1 byte: 8-bit write
        nop                             ; 4 bytes padding
        nop
        nop
        nop
        ; Will be patched to:
        ; 286+: OUT DX, AX (1 byte, 16-bit)

        pop     dx
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Read Block of Data (Optimized String I/O)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
hw_read_block:
        ; Input: DX = port, ES:DI = buffer, CX = byte count
        ; Preserves: BX, SI
        push    si
        push    ax

        ; Align to word boundary for 286+
        test    di, 1
        jz      rb_aligned

        ; Read unaligned byte
        in      al, dx
        stosb
        dec     cx
        jz      rb_done

rb_aligned:
        ; PATCH POINT: Optimized block read
        ; On 186+: May be patched to REP INSW or REP INSD
        ; On 8086: Uses loop-based byte transfer (SMC disabled)
PATCH_block_read:
        REP_INSB_SAFE                   ; CPU-adaptive: REP INSB (186+) or loop (8086)
        nop                             ; Padding for patches
        nop
        nop
        ; Will be patched to:
        ; 286: REP INSW with count adjustment
        ; 386: REP INSD with prefix
        ; 486+: Cache-optimized version

rb_done:
        pop     ax
        pop     si
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Write Block of Data (Optimized String I/O)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
hw_write_block:
        ; Input: DX = port, DS:SI = buffer, CX = byte count
        ; Preserves: BX, DI
        push    di
        push    ax

        ; Check alignment
        test    si, 1
        jz      wb_aligned

        ; Write unaligned byte
        lodsb
        out     dx, al
        dec     cx
        jz      wb_done

wb_aligned:
        ; PATCH POINT: Optimized block write
        ; On 186+: May be patched to REP OUTSW or REP OUTSD
        ; On 8086: Uses loop-based byte transfer (SMC disabled)
PATCH_block_write:
        REP_OUTSB_SAFE                  ; CPU-adaptive: REP OUTSB (186+) or loop (8086)
        nop                             ; Padding for patches
        nop
        nop
        ; Will be patched to:
        ; 286: REP OUTSW
        ; 386: REP OUTSD with prefix
        ; 486+: Optimized for write combining

wb_done:
        pop     ax
        pop     di
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Read EEPROM (NIC-specific, CPU-optimized wait)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
hw_read_eeprom:
        ; Input: BX = EEPROM address
        ; Output: AX = EEPROM data
        push    cx
        push    dx

        ; Select EEPROM register window
        mov     dx, [nic_io_base]
        add     dx, 0x0E                ; COMMAND register
        mov     ax, 0x0800              ; SELECT_WINDOW(0)
        out     dx, ax

        ; Issue EEPROM read command
        mov     dx, [nic_io_base]
        add     dx, 0x0A                ; EEPROM_CMD
        mov     ax, bx
        or      ax, EEPROM_CMD_READ
        out     dx, ax

        ; PATCH POINT: Optimized EEPROM wait
PATCH_eeprom_wait:
        ; Default: Simple loop
        mov     cx, 1000
eeprom_wait_loop:
        in      ax, dx                  ; 1 byte
        test    ax, EEPROM_BUSY         ; 3 bytes
        jz      eeprom_ready            ; 2 bytes
        loop    eeprom_wait_loop        ; 2 bytes
        ; Total: 8 bytes (padding needed)
        nop
        nop
        ; Will be patched to:
        ; 386+: Use PAUSE instruction in loop
        ; 486+: Optimized timing loop

eeprom_ready:
        ; Read EEPROM data
        mov     dx, [nic_io_base]
        add     dx, 0x0C                ; EEPROM_DATA
        in      ax, dx

        pop     dx
        pop     cx
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Reset NIC Hardware
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
hw_reset_nic:
        push    ax
        push    cx
        push    dx

        ; Issue global reset
        mov     dx, [nic_io_base]
        add     dx, 0x0E                ; COMMAND register
        mov     ax, 0x0000              ; GLOBAL_RESET
        out     dx, ax

        ; PATCH POINT: Optimized reset delay
PATCH_reset_delay:
        ; Default: Simple delay loop
        mov     cx, 1000
reset_delay_loop:
        nop
        loop    reset_delay_loop
        ; Total: 5 bytes
        ; Will be patched to:
        ; 286+: Better calibrated delay
        ; 386+: Use timestamp counter if available

        ; Wait for reset complete
        mov     cx, 1000
reset_wait_loop:
        in      ax, dx
        test    ax, 0x1000              ; RESET_COMPLETE
        jnz     reset_done

        ; Small delay between checks
        push    cx
        mov     cx, 10
reset_inner_delay:
        nop
        loop    reset_inner_delay
        pop     cx

        loop    reset_wait_loop

reset_done:
        ; Re-enable NIC
        mov     ax, 0x0001              ; ENABLE
        out     dx, ax

        pop     dx
        pop     cx
        pop     ax
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Fast Port I/O Macros (Inlined via patches)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
fast_io_read_word:
        ; PATCH POINT: Fast 16-bit read
PATCH_fast_read16:
        in      al, dx                  ; Read low byte
        inc     dx
        xchg    al, ah
        in      al, dx                  ; Read high byte
        dec     dx
        xchg    al, ah
        ; Total: 8 bytes
        nop
        nop
        ; Will be patched to:
        ; 286+: IN AX, DX (1 byte!)

        ret

fast_io_write_word:
        ; PATCH POINT: Fast 16-bit write
PATCH_fast_write16:
        out     dx, al                  ; Write low byte
        inc     dx
        xchg    al, ah
        out     dx, al                  ; Write high byte
        dec     dx
        xchg    al, ah
        ; Total: 8 bytes
        nop
        nop
        ; Will be patched to:
        ; 286+: OUT DX, AX (1 byte!)

        ret

hot_section_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; COLD SECTION - Initialization only
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        align 16
cold_section_start:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Patch Table
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
patch_table:
        ; Patch 1: Register read
        dw      PATCH_reg_read
        db      2                       ; Type: IO
        db      5                       ; Size
        ; 8086: IN AL, DX + XOR AH, AH
        db      0ECh, 32h, 0E4h, 90h, 90h
        ; 286+: IN AX, DX
        db      0EDh, 90h, 90h, 90h, 90h
        db      0EDh, 90h, 90h, 90h, 90h
        db      0EDh, 90h, 90h, 90h, 90h
        db      0EDh, 90h, 90h, 90h, 90h

        ; Patch 2: Register write
        dw      PATCH_reg_write
        db      2                       ; Type: IO
        db      5                       ; Size
        ; 8086: OUT DX, AL
        db      0EEh, 90h, 90h, 90h, 90h
        ; 286+: OUT DX, AX
        db      0EFh, 90h, 90h, 90h, 90h
        db      0EFh, 90h, 90h, 90h, 90h
        db      0EFh, 90h, 90h, 90h, 90h
        db      0EFh, 90h, 90h, 90h, 90h

        ; Patch 3: Block read
        dw      PATCH_block_read
        db      2                       ; Type: IO
        db      5                       ; Size
        ; 8086: REP INSB
        db      0F3h, 6Ch, 90h, 90h, 90h
        ; 286: REP INSW
        db      0F3h, 6Dh, 90h, 90h, 90h
        ; 386: 32-bit REP INSD
        db      66h, 0F3h, 6Dh, 90h, 90h
        db      66h, 0F3h, 6Dh, 90h, 90h
        db      66h, 0F3h, 6Dh, 90h, 90h

        ; Patch 4: Block write
        dw      PATCH_block_write
        db      2                       ; Type: IO
        db      5                       ; Size
        ; 8086: REP OUTSB
        db      0F3h, 6Eh, 90h, 90h, 90h
        ; 286: REP OUTSW
        db      0F3h, 6Fh, 90h, 90h, 90h
        ; 386: 32-bit REP OUTSD
        db      66h, 0F3h, 6Fh, 90h, 90h
        db      66h, 0F3h, 6Fh, 90h, 90h
        db      66h, 0F3h, 6Fh, 90h, 90h

        ; Patch 5: EEPROM wait (space for 10 bytes)
        dw      PATCH_eeprom_wait
        db      5                       ; Type: BRANCH
        db      10                      ; Size (larger for loop)
        ; All CPUs use similar wait, just optimized timing
        db      0B9h, 0E8h, 03h        ; MOV CX, 1000
        db      0ECh                    ; IN AL, DX
        db      0A9h, 00h, 80h          ; TEST AX, 8000h
        db      74h, 02h                ; JZ +2
        db      0E2h, 0F7h              ; LOOP -9
        ; (Same for all CPUs in this case)
        db      0B9h, 0E8h, 03h, 0ECh, 0A9h, 00h, 80h, 74h, 02h, 0E2h
        db      0B9h, 0E8h, 03h, 0ECh, 0A9h, 00h, 80h, 74h, 02h, 0E2h
        db      0B9h, 0E8h, 03h, 0ECh, 0A9h, 00h, 80h, 74h, 02h, 0E2h
        db      0B9h, 0E8h, 03h, 0ECh, 0A9h, 00h, 80h, 74h, 02h, 0E2h
        db      0B9h, 0E8h, 03h, 0ECh, 0A9h, 00h, 80h, 74h, 02h, 0E2h

        ; Patch 6: Reset delay
        dw      PATCH_reset_delay
        db      5                       ; Type: BRANCH
        db      5                       ; Size
        ; Simple delay loop for all CPUs
        db      0B9h, 0E8h, 03h        ; MOV CX, 1000
        db      0E2h, 0FEh              ; LOOP -2
        ; (Repeated for all CPUs)
        db      0B9h, 0E8h, 03h, 0E2h, 0FEh
        db      0B9h, 0E8h, 03h, 0E2h, 0FEh
        db      0B9h, 0E8h, 03h, 0E2h, 0FEh
        db      0B9h, 0E8h, 03h, 0E2h, 0FEh
        db      0B9h, 0E8h, 03h, 0E2h, 0FEh

        ; Patch 7: Fast 16-bit read
        dw      PATCH_fast_read16
        db      2                       ; Type: IO
        db      10                      ; Size
        ; 8086: Byte-by-byte
        db      0ECh, 42h, 86h, 0E0h, 0ECh, 4Ah, 86h, 0E0h, 90h, 90h
        ; 286+: Single instruction
        db      0EDh, 90h, 90h, 90h, 90h, 90h, 90h, 90h, 90h, 90h
        db      0EDh, 90h, 90h, 90h, 90h, 90h, 90h, 90h, 90h, 90h
        db      0EDh, 90h, 90h, 90h, 90h, 90h, 90h, 90h, 90h, 90h
        db      0EDh, 90h, 90h, 90h, 90h, 90h, 90h, 90h, 90h, 90h

        ; Patch 8: Fast 16-bit write
        dw      PATCH_fast_write16
        db      2                       ; Type: IO
        db      10                      ; Size
        ; 8086: Byte-by-byte
        db      0EEh, 42h, 86h, 0E0h, 0EEh, 4Ah, 86h, 0E0h, 90h, 90h
        ; 286+: Single instruction
        db      0EFh, 90h, 90h, 90h, 90h, 90h, 90h, 90h, 90h, 90h
        db      0EFh, 90h, 90h, 90h, 90h, 90h, 90h, 90h, 90h, 90h
        db      0EFh, 90h, 90h, 90h, 90h, 90h, 90h, 90h, 90h, 90h
        db      0EFh, 90h, 90h, 90h, 90h, 90h, 90h, 90h, 90h, 90h

patch_count     equ     8

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Hardware Initialization (Cold)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
hw_init:
        push    ax
        push    dx

        ; Display hardware init message
        mov     dx, init_msg
        mov     ah, 9
        int     21h

        ; Initialize base I/O address
        mov     ax, [detected_io_base]
        mov     [nic_io_base], ax

        ; Perform initial hardware reset
        call    hw_reset_nic

        pop     dx
        pop     ax
        ret

init_msg        db      'Hardware access layer initialized', 13, 10, '$'

cold_section_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Data Section
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        segment _DATA class=DATA

        ; Hardware configuration
nic_io_base         dw      0300h           ; Default I/O base
detected_io_base    dw      0               ; From detection

        ; Module size
module_size         equ     cold_section_end - module_header
