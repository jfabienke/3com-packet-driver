;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file pci_io.asm
;; @brief 32-bit I/O helpers for PCI configuration access
;;
;; Provides 32-bit I/O port access functions for PCI Mechanism #1
;; configuration cycles. Uses 386+ instructions in 16-bit real mode.
;;
;; Calling convention: Watcom C 16-bit cdecl
;; - Arguments pushed right-to-left
;; - 32-bit values passed as two 16-bit words (low, high)
;; - 32-bit return in DX:AX (high:low)
;; - Caller cleans stack
;;
;; 3Com Packet Driver - PCI I/O Assembly Module
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16
cpu 386                         ; Enable 386 instructions

segment _TEXT class=CODE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; unsigned long cdecl inportd(unsigned short port);
;;
;; Read 32-bit value from I/O port
;;
;; @param port I/O port address (stack [bp+4])
;; @return 32-bit value in DX:AX (high:low)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _inportd
_inportd:
    push    bp
    mov     bp, sp

    mov     dx, [bp+4]          ; Get port number

    ; Perform 32-bit input
    db      66h                 ; Operand size prefix for 32-bit
    in      ax, dx              ; IN EAX, DX (32-bit read)

    ; Result is now in EAX
    ; Need to return in DX:AX format for Watcom C
    push    ax                  ; Save low word
    db      66h                 ; Operand size prefix
    shr     ax, 16              ; Shift EAX right by 16
    mov     dx, ax              ; DX = high word
    pop     ax                  ; AX = low word

    pop     bp
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; void cdecl outportd(unsigned short port, unsigned long value);
;;
;; Write 32-bit value to I/O port
;;
;; @param port I/O port address (stack [bp+4])
;; @param value 32-bit value to write (stack [bp+6] low, [bp+8] high)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _outportd
_outportd:
    push    bp
    mov     bp, sp
    push    cx                  ; Save CX

    mov     dx, [bp+4]          ; Get port number
    mov     ax, [bp+6]          ; Get value low word
    mov     cx, [bp+8]          ; Get value high word

    ; Combine into EAX
    db      66h                 ; Operand size prefix
    shl     cx, 16              ; Shift ECX left by 16
    db      66h                 ; Operand size prefix
    or      ax, cx              ; OR EAX with ECX (EAX = complete value)

    ; Perform 32-bit output
    db      66h                 ; Operand size prefix
    out     dx, ax              ; OUT DX, EAX (32-bit write)

    pop     cx                  ; Restore CX
    pop     bp
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; unsigned short cdecl inportw(unsigned short port);
;;
;; Read 16-bit value from I/O port
;; Standard function but included for completeness
;;
;; @param port I/O port address (stack [bp+4])
;; @return 16-bit value in AX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _inportw
_inportw:
    push    bp
    mov     bp, sp

    mov     dx, [bp+4]          ; Get port number
    in      ax, dx              ; 16-bit read

    pop     bp
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; void cdecl outportw(unsigned short port, unsigned short value);
;;
;; Write 16-bit value to I/O port
;; Standard function but included for completeness
;;
;; @param port I/O port address (stack [bp+4])
;; @param value 16-bit value to write (stack [bp+6])
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _outportw
_outportw:
    push    bp
    mov     bp, sp

    mov     dx, [bp+4]          ; Get port number
    mov     ax, [bp+6]          ; Get value
    out     dx, ax              ; 16-bit write

    pop     bp
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; unsigned char cdecl inportb(unsigned short port);
;;
;; Read 8-bit value from I/O port
;; Standard function but included for completeness
;;
;; @param port I/O port address (stack [bp+4])
;; @return 8-bit value in AL
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _inportb
_inportb:
    push    bp
    mov     bp, sp

    mov     dx, [bp+4]          ; Get port number
    in      al, dx              ; 8-bit read
    xor     ah, ah              ; Clear high byte

    pop     bp
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; void cdecl outportb(unsigned short port, unsigned char value);
;;
;; Write 8-bit value to I/O port
;; Standard function but included for completeness
;;
;; @param port I/O port address (stack [bp+4])
;; @param value 8-bit value to write (stack [bp+6])
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _outportb
_outportb:
    push    bp
    mov     bp, sp

    mov     dx, [bp+4]          ; Get port number
    mov     al, [bp+6]          ; Get value (low byte)
    out     dx, al              ; 8-bit write

    pop     bp
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; void cdecl cli_safe(void);
;;
;; Disable interrupts safely
;; Can be called from C
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _cli_safe
_cli_safe:
    cli
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; void cdecl sti_safe(void);
;;
;; Enable interrupts safely
;; Can be called from C
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _sti_safe
_sti_safe:
    sti
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; unsigned short cdecl save_flags(void);
;;
;; Save current flags register
;;
;; @return Flags register value in AX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _save_flags
_save_flags:
    pushf
    pop     ax
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; void cdecl restore_flags(unsigned short flags);
;;
;; Restore flags register
;;
;; @param flags Flags value to restore (stack [bp+4])
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _restore_flags
_restore_flags:
    push    bp
    mov     bp, sp

    mov     ax, [bp+4]          ; Get flags value
    push    ax
    popf                        ; Restore flags

    pop     bp
    ret
