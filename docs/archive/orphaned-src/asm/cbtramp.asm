; cbtramp.asm - Application callback trampolines for DOS packet driver
; Provides safe far call mechanisms with DS fixup and alternate stack support

.286
.model SMALL
.code

; C prototype:
; void call_recv_reg_tramp(APP_CB *cb, AX, BX, CX, void far *pkt, int use_alt_stack);
; Near call, stack layout:
;  [bp+04] cb
;  [bp+06] axv
;  [bp+08] bxv
;  [bp+0A] cxv
;  [bp+0C] pkt_off
;  [bp+0E] pkt_seg
;  [bp+10] use_alt_stack
; APP_CB layout:
;  +0: entry (4 bytes)
;  +4: client_ds
;  +6: alt_ss
;  +8: alt_sp

PUBLIC _call_recv_reg_tramp
_call_recv_reg_tramp PROC NEAR
    push bp
    mov  bp, sp
    push ds
    push es
    push si
    push di

    ; Load cb ptr into si
    mov  si, [bp+4]        ; cb offset
    ; ds=ss to read from our stack, then read cb fields
    mov  ax, [si+4]        ; client_ds
    mov  dx, [si+6]        ; alt_ss
    mov  di, [si+8]        ; alt_sp

    ; Save old SS:SP
    push ss
    push sp

    ; Switch to alt stack if requested and provided
    mov  bx, [bp+16]       ; use_alt_stack
    or   bx, bx
    jz   short noswitch_rs
    or   dx, dx
    jz   short noswitch_rs
    cli
    mov  ss, dx
    mov  sp, di
    sti
noswitch_rs:

    ; Set DS to client's DS
    mov  ds, ax

    ; Load regs AX,BX,CX from params
    mov  ax, [bp+6]        ; axv (linktype)
    mov  bx, [bp+8]        ; bxv (handle)
    mov  cx, [bp+10]       ; cxv (length)

    ; Load ES:DI with packet pointer
    mov  di, [bp+12]       ; pkt_off
    mov  es, [bp+14]       ; pkt_seg

    ; Far call cb->entry
    ; Read CS:IP from original cb (need to access via our SS segment)
    push ds                ; save client DS
    mov  ds, ss            ; point to our stack segment
    mov  si, [bp+4]        ; cb offset
    mov  dx, [si+2]        ; entry.seg (CS)
    mov  si, [si+0]        ; entry.off (IP)
    pop  ds                ; restore client DS
    
    ; Push far return address and jump
    push cs
    push offset ret_from_client_rs
    push dx                ; push CS
    push si                ; push IP
    retf                   ; far jump to client

ret_from_client_rs:
    ; Restore original SS:SP if switched
    cli
    pop  sp
    pop  ss
    sti

    pop  di
    pop  si
    pop  es
    pop  ds
    pop  bp
    ret
_call_recv_reg_tramp ENDP

; C prototype:
; void call_cdecl_tramp(APP_CB *cb, void far *arg0, unsigned short arg1, 
;                       unsigned short arg2, int use_alt_stack);
; We'll push args right-to-left and do far call; callee is __cdecl, so caller cleans up.

PUBLIC _call_cdecl_tramp
_call_cdecl_tramp PROC NEAR
    push bp
    mov  bp, sp
    push ds
    push es

    mov  si, [bp+4]        ; cb
    mov  ax, [si+4]        ; client_ds
    mov  dx, [si+6]        ; alt_ss
    mov  di, [si+8]        ; alt_sp

    ; Save old SS:SP
    push ss
    push sp

    ; Switch stack if requested and provided
    mov  bx, [bp+14]       ; use_alt_stack
    or   bx, bx
    jz   short noswitch_cd
    or   dx, dx
    jz   short noswitch_cd
    cli
    mov  ss, dx
    mov  sp, di
    sti
noswitch_cd:

    ; Set DS to client's DS
    mov  ds, ax

    ; Push args right-to-left for __cdecl(void far *arg0, unsigned arg1, unsigned arg2)
    mov  ax, [bp+12]       ; arg2
    push ax
    mov  ax, [bp+10]       ; arg1
    push ax
    mov  ax, [bp+8]        ; arg0.seg
    push ax
    mov  ax, [bp+6]        ; arg0.off
    push ax

    ; Far call cb->entry
    push ds                ; save client DS
    mov  ds, ss            ; point to our segment
    mov  si, [bp+4]        ; cb
    mov  dx, [si+2]        ; entry.seg
    mov  si, [si+0]        ; entry.off
    pop  ds                ; restore client DS
    
    push cs
    push offset ret_from_client_cd
    push dx
    push si
    retf

ret_from_client_cd:
    ; Clean up stack for __cdecl (4 params: 2+2+2+2 bytes)
    add  sp, 8             ; pop far pointer (4 bytes)
    add  sp, 4             ; pop two words (4 bytes)

    ; Restore original SS:SP
    cli
    pop  sp
    pop  ss
    sti

    pop  es
    pop  ds
    pop  bp
    ret
_call_cdecl_tramp ENDP

END