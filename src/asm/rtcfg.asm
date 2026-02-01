; rtcfg.asm - Runtime configuration variables and functions
;
; 3Com Packet Driver - Replaces src/c/rtcfg.c
; Last Updated: 2026-02-01 16:00:00 CET
;
; Provides runtime-adjustable copy-break threshold, interrupt mitigation,
; and media mode control for the Extension API.
; Original C: 128 LOC, 1,988 bytes compiled. ASM: ~350 bytes.

; NIC type constants
NIC_TYPE_3C509B     equ 1
NIC_TYPE_3C515_TX   equ 2

; nic_info_t struct offsets (-zp1)
NIC_TYPE    equ 0
NIC_IO_BASE equ 15

; 3Com register offsets
EL3_CMD     equ 0x0E

; =============================================================================
; DATA SEGMENT - Initialized values
; =============================================================================
group DGROUP _DATA

segment _DATA class=DATA

global _g_copy_break_threshold
_g_copy_break_threshold:  dw 256       ; Default 256 bytes

global _g_mitigation_batch
_g_mitigation_batch:      db 10        ; Default 10 packets

global _g_mitigation_timeout
_g_mitigation_timeout:    db 2         ; Default 2 ticks

; =============================================================================
; CODE SEGMENT
; =============================================================================
segment rtcfg_TEXT class=CODE

extern hardware_get_primary_nic_

; int hardware_set_media_mode(uint8_t mode)
; Watcom: mode in AX
global hardware_set_media_mode_
hardware_set_media_mode_:
    push    bp
    mov     bp, sp
    push    si
    push    di
    push    es

    mov     si, ax              ; SI = mode

    ; Validate mode <= 3
    cmp     si, 3
    ja      .ret_err

    ; Get primary NIC -> DX:AX
    call far hardware_get_primary_nic_
    mov     bx, ax
    or      bx, dx
    jz      .ret_err

    ; ES:BX = nic pointer
    mov     es, dx
    mov     bx, ax

    ; Branch on NIC type
    cmp     word [es:bx+NIC_TYPE], NIC_TYPE_3C515_TX
    je      .nic_3c515
    cmp     word [es:bx+NIC_TYPE], NIC_TYPE_3C509B
    je      .nic_3c509b
    jmp     .ret_err

; --- 3C515 media mode ---
.nic_3c515:
    mov     di, [es:bx+NIC_IO_BASE]    ; DI = io_base

    ; Select window 3: outw(io_base + 0x0E, 0x0803)
    lea     dx, [di+EL3_CMD]
    mov     ax, 0x0803
    out     dx, ax

    ; Read media options: inw(io_base + 0x08)
    lea     dx, [di+0x08]
    in      ax, dx
    and     ax, 0xFF00              ; Clear low byte (media bits)

    ; Set media mode based on SI
    cmp     si, 0
    je      .c515_auto
    cmp     si, 1
    je      .c515_10bt
    cmp     si, 2
    je      .c515_10b2
    ; mode 3: 100baseTX
    or      ax, 0x0040
    jmp     .c515_write

.c515_auto:
    or      ax, 0x0080
    jmp     .c515_write
.c515_10bt:
    or      ax, 0x0020
    jmp     .c515_write
.c515_10b2:
    or      ax, 0x0010

.c515_write:
    ; Write media options: outw(io_base + 0x08, media_ctrl)
    lea     dx, [di+0x08]
    out     dx, ax

    ; Reset TX: outw(io_base + 0x0E, 0x2800)
    lea     dx, [di+EL3_CMD]
    mov     ax, 0x2800
    out     dx, ax

    ; Reset RX: 0x3000
    mov     ax, 0x3000
    out     dx, ax

    ; Enable TX: 0x4800
    mov     ax, 0x4800
    out     dx, ax

    ; Enable RX: 0x5000
    mov     ax, 0x5000
    out     dx, ax

    jmp     .ret_ok

; --- 3C509B media mode ---
.nic_3c509b:
    mov     di, [es:bx+NIC_IO_BASE]

    ; No 100baseTX on 3C509B
    cmp     si, 3
    je      .ret_err

    ; Select window 4: outw(io_base + 0x0E, 0x0804)
    lea     dx, [di+EL3_CMD]
    mov     ax, 0x0804
    out     dx, ax

    ; Read media register: inw(io_base + 0x0A)
    lea     dx, [di+0x0A]
    in      ax, dx

    ; Set media type
    cmp     si, 2
    je      .c509b_bnc

    ; mode 0 (auto) or 1 (10baseT): enable link beat, select TP
    or      ax, 0x8000          ; Enable link beat
    and     ax, ~0x4000         ; Select TP
    jmp     .c509b_write

.c509b_bnc:
    and     ax, ~0x8000         ; Disable link beat
    or      ax, 0x4000          ; Select BNC

.c509b_write:
    ; Write media register: outw(io_base + 0x0A, media_ctrl)
    lea     dx, [di+0x0A]
    out     dx, ax

.ret_ok:
    xor     ax, ax              ; return 0
    jmp     .done

.ret_err:
    mov     ax, -1              ; return -1
    cwd                         ; sign-extend to DX:AX

.done:
    pop     es
    pop     di
    pop     si
    pop     bp
    retf

; uint16_t get_copy_break_threshold(void)
global get_copy_break_threshold_
get_copy_break_threshold_:
    mov     ax, [_g_copy_break_threshold]
    retf

; void get_mitigation_params(uint8_t *batch, uint8_t *timeout)
; Watcom: batch in AX, timeout in DX (near pointers in large model data segment)
global get_mitigation_params_
get_mitigation_params_:
    test    ax, ax
    jz      .no_batch
    mov     bx, ax
    mov     cl, [_g_mitigation_batch]
    mov     [bx], cl
.no_batch:
    test    dx, dx
    jz      .no_timeout
    mov     bx, dx
    mov     cl, [_g_mitigation_timeout]
    mov     [bx], cl
.no_timeout:
    retf
