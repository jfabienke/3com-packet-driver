;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file mod_isr.asm
;; @brief INT 60h Packet Driver API dispatch module (JIT-assembled)
;;
;; Implements the standard FTP Software Packet Driver Specification INT 60h
;; entry point. All function dispatch is via AH register. NIC-specific send
;; is invoked through a patchable CALL that the SMC fills at load time.
;;
;; Hot path size target: ~1.5KB
;;
;; Last Updated: 2026-02-01 11:02:20 CET
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16
cpu 286

%include "patch_macros.inc"

;; ============================================================================
;; Patch type constants (local aliases)
;; ============================================================================
PATCH_TYPE_IMM16        equ 6
PATCH_TYPE_IMM8         equ 7
PATCH_TYPE_RELOC_NEAR   equ 8

;; ============================================================================
;; Packet driver error codes
;; ============================================================================
PKTDRV_ERR_NONE         equ 0
PKTDRV_ERR_BAD_HANDLE   equ 1
PKTDRV_ERR_NO_CLASS     equ 2
PKTDRV_ERR_NO_TYPE      equ 3
PKTDRV_ERR_NO_NUMBER    equ 4
PKTDRV_ERR_BAD_TYPE     equ 5
PKTDRV_ERR_NO_SPACE     equ 7
PKTDRV_ERR_TYPE_INUSE   equ 8
PKTDRV_ERR_CANT_TERM    equ 9
PKTDRV_ERR_BAD_MODE     equ 10
PKTDRV_ERR_CANT_RESET   equ 12

;; ============================================================================
;; Driver constants
;; ============================================================================
DRIVER_CLASS_ETHERNET   equ 1
DRIVER_TYPE_3C509       equ 0           ; Patched at load time
DRIVER_VERSION          equ 10          ; Version 1.0

MAX_HANDLES             equ 8
HANDLE_ENTRY_SIZE       equ 8           ; per-handle: flags(1)+type(2)+recv_ptr(4)+pad(1)

;; ############################################################################
;; MODULE SEGMENT
;; ############################################################################
segment MODULE class=MODULE align=16

;; ============================================================================
;; 64-byte Module Header
;; ============================================================================
global _mod_isr_header
_mod_isr_header:
    db  'PKTDRV', 0             ; +00  7 bytes: module signature
    db  1                       ; +07  1 byte:  major version
    db  0                       ; +08  1 byte:  minor version
    db  0                       ; +09  1 byte:  cpu_req (0 = any 286+)
    db  0                       ; +0A  1 byte:  nic_type (0 = generic)
    db  0                       ; +0B  1 byte:  cap_flags
    dw  hot_end - hot_start     ; +0C  2 bytes: hot code size
    dw  patch_table             ; +0E  2 bytes: offset to patch table
    dw  patch_table_end - patch_table  ; +10  2 bytes: patch table size
    dw  hot_start               ; +12  2 bytes: offset to hot_start
    dw  hot_end                 ; +14  2 bytes: offset to hot_end
    times 64 - ($ - _mod_isr_header) db 0  ; Pad to 64 bytes

;; ============================================================================
;; HOT PATH START
;; ============================================================================
hot_start:

;; ----------------------------------------------------------------------------
;; isr_entry - INT 60h handler entry point
;; ----------------------------------------------------------------------------
global isr_entry
isr_entry:
    push    ax
    push    bx
    push    cx
    push    dx
    push    si
    push    di
    push    ds
    push    es
    push    bp

    ;; Set DS to our data segment (patched by linker/loader)
    push    cs
    pop     ds

    ;; Dispatch on AH
    mov     al, ah
    cmp     al, 01h
    je      isr_fn_driver_info
    cmp     al, 02h
    je      isr_fn_access_type
    cmp     al, 03h
    je      isr_fn_release_type
    cmp     al, 04h
    je      isr_fn_send_pkt
    cmp     al, 05h
    je      isr_fn_terminate
    cmp     al, 06h
    je      isr_fn_get_address
    cmp     al, 07h
    je      isr_fn_reset_interface
    cmp     al, 08h
    je      isr_fn_get_parameters
    cmp     al, 0Bh
    je      isr_fn_get_statistics

    ;; Unknown function - set carry and error
    mov     dh, PKTDRV_ERR_BAD_TYPE
    jmp     isr_exit_error

;; ----------------------------------------------------------------------------
;; AH=01h: driver_info - return driver class, type, version
;; ----------------------------------------------------------------------------
isr_fn_driver_info:
    mov     bx, DRIVER_CLASS_ETHERNET       ; BX = driver class
    mov     ch, DRIVER_TYPE_3C509           ; CH = driver type
    mov     cl, 0                           ; CL = driver number
    mov     dx, DRIVER_VERSION              ; DX = version
    lea     si, [driver_name]               ; DS:SI -> name string
    jmp     isr_exit_ok

;; ----------------------------------------------------------------------------
;; AH=02h: access_type - register protocol handler
;;   BX = interface class, DL = interface number
;;   DS:SI -> type string, CX = type length
;;   ES:DI -> receiver callback
;; ----------------------------------------------------------------------------
isr_fn_access_type:
    ;; Find free handle slot
    xor     bx, bx
    lea     si, [handle_table]
isr_access_scan:
    cmp     bx, MAX_HANDLES
    jge     isr_access_no_space
    test    byte [si], 1                    ; Check in-use flag
    jz      isr_access_found
    add     si, HANDLE_ENTRY_SIZE
    inc     bx
    jmp     isr_access_scan
isr_access_no_space:
    mov     dh, PKTDRV_ERR_NO_SPACE
    jmp     isr_exit_error
isr_access_found:
    mov     byte [si], 1                    ; Mark in-use
    ;; Store receiver callback (ES:DI from caller saved on stack)
    mov     [si+3], di                      ; Offset
    mov     [si+5], es                      ; Segment
    mov     ax, bx                          ; Return handle in AX
    jmp     isr_exit_ok

;; ----------------------------------------------------------------------------
;; AH=03h: release_type - unregister protocol handler
;;   BX = handle
;; ----------------------------------------------------------------------------
isr_fn_release_type:
    cmp     bx, MAX_HANDLES
    jge     isr_bad_handle
    mov     ax, HANDLE_ENTRY_SIZE
    mul     bx
    lea     si, [handle_table]
    add     si, ax
    test    byte [si], 1
    jz      isr_bad_handle
    mov     byte [si], 0                    ; Clear in-use flag
    jmp     isr_exit_ok
isr_bad_handle:
    mov     dh, PKTDRV_ERR_BAD_HANDLE
    jmp     isr_exit_error

;; ----------------------------------------------------------------------------
;; AH=04h: send_pkt - transmit packet
;;   DS:SI -> packet data, CX = packet length
;; ----------------------------------------------------------------------------
isr_fn_send_pkt:
    ;; Call NIC-specific send routine (patched by SMC)
    PATCH_POINT_CALL pp_send_call, isr_send_fallback
    jmp     isr_exit_ok

isr_send_fallback:
    ;; Fallback: no NIC send patched, return error
    mov     dh, PKTDRV_ERR_CANT_RESET
    ret

;; ----------------------------------------------------------------------------
;; AH=05h: terminate - unload driver
;; ----------------------------------------------------------------------------
isr_fn_terminate:
    ;; Clear all handles
    lea     si, [handle_table]
    mov     cx, MAX_HANDLES * HANDLE_ENTRY_SIZE
    xor     al, al
isr_term_clear:
    mov     [si], al
    inc     si
    loop    isr_term_clear

    ;; DOS idle - yield to DOS while unloading
    int     28h                             ; DOS idle interrupt
    nop                                     ; Padding
    nop                                     ; (4 lines inline)

    jmp     isr_exit_ok

;; ----------------------------------------------------------------------------
;; AH=06h: get_address - return MAC address
;;   BX = handle, ES:DI -> buffer, CX = buffer length
;; ----------------------------------------------------------------------------
isr_fn_get_address:
    cmp     cx, 6
    jb      isr_exit_ok                     ; Buffer too small, return what fits
    ;; Copy MAC address to caller buffer (assumes mac_addr is accessible)
    lea     si, [mac_addr_local]
    mov     cx, 6
    rep movsb
    jmp     isr_exit_ok

;; ----------------------------------------------------------------------------
;; AH=07h: reset_interface
;; ----------------------------------------------------------------------------
isr_fn_reset_interface:
    ;; Stub - reset is NIC-specific, done via patched call if needed
    jmp     isr_exit_ok

;; ----------------------------------------------------------------------------
;; AH=08h: get_parameters
;;   ES:BX -> parameter block
;; ----------------------------------------------------------------------------
isr_fn_get_parameters:
    ;; Return pointer to our parameter block
    push    cs
    pop     es
    lea     bx, [param_block]
    jmp     isr_exit_ok

;; ----------------------------------------------------------------------------
;; AH=0Bh: get_statistics
;;   BX = handle
;; ----------------------------------------------------------------------------
isr_fn_get_statistics:
    ;; Return pointer to statistics block in DS:SI
    lea     si, [stats_block]
    jmp     isr_exit_ok

;; ----------------------------------------------------------------------------
;; Common exit paths
;; ----------------------------------------------------------------------------
isr_exit_ok:
    clc                                     ; Clear carry = success
    jmp     isr_exit_common
isr_exit_error:
    stc                                     ; Set carry = error
isr_exit_common:
    pop     bp
    pop     es
    pop     ds
    pop     di
    pop     si
    pop     dx
    pop     cx
    pop     bx
    pop     ax
    iret

;; ============================================================================
;; Local data (within hot path)
;; ============================================================================
driver_name:
    db  '3Com EtherLink III Packet Driver', 0

mac_addr_local:
    db  0, 0, 0, 0, 0, 0                   ; Copied from data module at init

handle_table:
    times MAX_HANDLES * HANDLE_ENTRY_SIZE db 0

param_block:
    db  1                                   ; Major version
    db  9                                   ; Minor version (1.09 spec)
    db  14                                  ; Length of param block
    db  DRIVER_CLASS_ETHERNET               ; Class
    dw  1514                                ; Max packet size (Ethernet)
    dw  0                                   ; Min packet size
    dw  1                                   ; Number of multicast addresses
    dw  0                                   ; Receive mode
    db  0, 0                                ; Padding

stats_block:
    dd  0                                   ; Packets in
    dd  0                                   ; Packets out
    dd  0                                   ; Bytes in
    dd  0                                   ; Bytes out
    dd  0                                   ; Errors in
    dd  0                                   ; Errors out
    dd  0                                   ; Packets dropped

hot_end:

;; ============================================================================
;; PATCH TABLE
;; ============================================================================
patch_table:
    PATCH_TABLE_ENTRY  pp_send_call, PATCH_TYPE_RELOC_NEAR
patch_table_end:
