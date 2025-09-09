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

.model small
.386                        ; Enable 386 instructions
.code

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; unsigned long cdecl inportd(unsigned short port);
;; 
;; Read 32-bit value from I/O port
;;
;; @param port I/O port address (stack [bp+4])
;; @return 32-bit value in DX:AX (high:low)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
public _inportd
_inportd proc near
    push    bp
    mov     bp, sp
    
    mov     dx, [bp+4]      ; Get port number
    
    ; Perform 32-bit input
    db      66h             ; Operand size prefix for 32-bit
    in      ax, dx          ; IN EAX, DX (32-bit read)
    
    ; Result is now in EAX
    ; Need to return in DX:AX format for Watcom C
    push    ax              ; Save low word
    db      66h             ; Operand size prefix
    shr     ax, 16          ; Shift EAX right by 16
    mov     dx, ax          ; DX = high word
    pop     ax              ; AX = low word
    
    pop     bp
    ret
_inportd endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; void cdecl outportd(unsigned short port, unsigned long value);
;;
;; Write 32-bit value to I/O port
;;
;; @param port I/O port address (stack [bp+4])
;; @param value 32-bit value to write (stack [bp+6] low, [bp+8] high)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
public _outportd
_outportd proc near
    push    bp
    mov     bp, sp
    push    cx              ; Save CX
    
    mov     dx, [bp+4]      ; Get port number
    mov     ax, [bp+6]      ; Get value low word
    mov     cx, [bp+8]      ; Get value high word
    
    ; Combine into EAX
    db      66h             ; Operand size prefix
    shl     cx, 16          ; Shift ECX left by 16
    db      66h             ; Operand size prefix
    or      ax, cx          ; OR EAX with ECX (EAX = complete value)
    
    ; Perform 32-bit output
    db      66h             ; Operand size prefix
    out     dx, ax          ; OUT DX, EAX (32-bit write)
    
    pop     cx              ; Restore CX
    pop     bp
    ret
_outportd endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; unsigned short cdecl inportw(unsigned short port);
;;
;; Read 16-bit value from I/O port
;; Standard function but included for completeness
;;
;; @param port I/O port address (stack [bp+4])
;; @return 16-bit value in AX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
public _inportw
_inportw proc near
    push    bp
    mov     bp, sp
    
    mov     dx, [bp+4]      ; Get port number
    in      ax, dx          ; 16-bit read
    
    pop     bp
    ret
_inportw endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; void cdecl outportw(unsigned short port, unsigned short value);
;;
;; Write 16-bit value to I/O port
;; Standard function but included for completeness
;;
;; @param port I/O port address (stack [bp+4])
;; @param value 16-bit value to write (stack [bp+6])
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
public _outportw
_outportw proc near
    push    bp
    mov     bp, sp
    
    mov     dx, [bp+4]      ; Get port number
    mov     ax, [bp+6]      ; Get value
    out     dx, ax          ; 16-bit write
    
    pop     bp
    ret
_outportw endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; unsigned char cdecl inportb(unsigned short port);
;;
;; Read 8-bit value from I/O port
;; Standard function but included for completeness
;;
;; @param port I/O port address (stack [bp+4])
;; @return 8-bit value in AL
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
public _inportb
_inportb proc near
    push    bp
    mov     bp, sp
    
    mov     dx, [bp+4]      ; Get port number
    in      al, dx          ; 8-bit read
    xor     ah, ah          ; Clear high byte
    
    pop     bp
    ret
_inportb endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; void cdecl outportb(unsigned short port, unsigned char value);
;;
;; Write 8-bit value to I/O port
;; Standard function but included for completeness
;;
;; @param port I/O port address (stack [bp+4])
;; @param value 8-bit value to write (stack [bp+6])
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
public _outportb
_outportb proc near
    push    bp
    mov     bp, sp
    
    mov     dx, [bp+4]      ; Get port number
    mov     al, [bp+6]      ; Get value (low byte)
    out     dx, al          ; 8-bit write
    
    pop     bp
    ret
_outportb endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; void cdecl cli_safe(void);
;;
;; Disable interrupts safely
;; Can be called from C
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
public _cli_safe
_cli_safe proc near
    cli
    ret
_cli_safe endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; void cdecl sti_safe(void);
;;
;; Enable interrupts safely
;; Can be called from C
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
public _sti_safe
_sti_safe proc near
    sti
    ret
_sti_safe endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; unsigned short cdecl save_flags(void);
;;
;; Save current flags register
;;
;; @return Flags register value in AX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
public _save_flags
_save_flags proc near
    pushf
    pop     ax
    ret
_save_flags endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; void cdecl restore_flags(unsigned short flags);
;;
;; Restore flags register
;;
;; @param flags Flags value to restore (stack [bp+4])
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
public _restore_flags
_restore_flags proc near
    push    bp
    mov     bp, sp
    
    mov     ax, [bp+4]      ; Get flags value
    push    ax
    popf                    ; Restore flags
    
    pop     bp
    ret
_restore_flags endp

end