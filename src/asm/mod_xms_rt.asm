;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file mod_xms_rt.asm
;; @brief XMS runtime operations module for extended memory management
;;
;; JIT-assembled runtime module for 16-bit DOS packet driver TSR.
;; Provides XMS (Extended Memory Specification) runtime operations including
;; lock/unlock, memory copy, A20 line control, and capability queries.
;;
;; Last Updated: 2026-02-01 16:45:00 CET
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16

%include "patch_macros.inc"

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Constants
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
CPU_REQ         equ 0               ; 8086 baseline
CAP_FLAGS       equ MOD_CAP_XMS     ; XMS capability required
PATCH_COUNT     equ 0               ; No patch points

;; XMS function codes
XMS_QUERY_FREE      equ 08h
XMS_ALLOC_EMB       equ 09h
XMS_FREE_EMB        equ 0Ah
XMS_MOVE_EMB        equ 0Bh
XMS_LOCK_EMB        equ 0Ch
XMS_UNLOCK_EMB      equ 0Dh
XMS_ENABLE_A20      equ 05h
XMS_DISABLE_A20     equ 06h
XMS_QUERY_A20       equ 07h

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Module segment
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
section .text class=MODULE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 64-byte module header
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _mod_xms_rt_header
_mod_xms_rt_header:
header:
    db 'PKTDRV',0                   ; 7 bytes - module signature
    db 1, 0                         ; 2 bytes - version 1.0
    dw hot_start                    ; hot section start offset
    dw hot_end                      ; hot section end offset
    dw 0, 0                         ; cold_start, cold_end (unused)
    dw patch_table                  ; patch table offset
    dw PATCH_COUNT                  ; number of patch entries
    dw (hot_end - header)           ; module_size
    dw (hot_end - hot_start)        ; required_memory
    db CPU_REQ                      ; cpu_requirements (8086)
    db 0                            ; nic_type (0 = any)
    dw CAP_FLAGS                    ; capability flags
    times (64 - ($ - header)) db 0  ; pad header to 64 bytes

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Hot section - XMS runtime operations
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
hot_start:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; xms_lock_ - Lock XMS extended memory block and get linear address
;;
;; Watcom far call convention:
;;   Input:  AX = XMS handle
;;   Output: DX:AX = 32-bit linear address (on success)
;;           AX = 0FFFFh (on failure)
;; Clobbers: BX, DX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _xms_lock_
_xms_lock_:
    push    bx
    mov     dx, ax                  ; DX = handle for XMS call
    mov     ah, XMS_LOCK_EMB        ; AH = 0Ch (Lock EMB)
    call    far [cs:xms_entry]     ; Call XMS driver

    cmp     ax, 1                   ; AX = 1 on success
    jne     .fail

    ;; Success: DX:BX contains 32-bit linear address
    mov     ax, bx                  ; Return low word in AX
    ;; DX already has high word
    pop     bx
    retf

.fail:
    mov     ax, 0FFFFh              ; Return -1 on failure
    mov     dx, 0FFFFh
    pop     bx
    retf

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; xms_unlock_ - Unlock XMS extended memory block
;;
;; Watcom far call convention:
;;   Input:  AX = XMS handle
;;   Output: AX = 0 (success) or -1 (failure)
;; Clobbers: DX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _xms_unlock_
_xms_unlock_:
    mov     dx, ax                  ; DX = handle for XMS call
    mov     ah, XMS_UNLOCK_EMB      ; AH = 0Dh (Unlock EMB)
    call    far [cs:xms_entry]     ; Call XMS driver

    cmp     ax, 1                   ; AX = 1 on success
    jne     .fail

    xor     ax, ax                  ; Return 0 on success
    retf

.fail:
    mov     ax, 0FFFFh              ; Return -1 on failure
    retf

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; xms_copy_ - Copy memory block using XMS move
;;
;; Watcom far call convention:
;;   Input:  DX:AX = far pointer to xms_move_struct (10 bytes)
;;   Output: AX = 0 (success) or -1 (failure)
;; Clobbers: DS, SI
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _xms_copy_
_xms_copy_:
    push    ds
    push    si

    ;; DX:AX = far pointer to struct
    mov     ds, dx                  ; DS:SI = pointer to move struct
    mov     si, ax

    mov     ah, XMS_MOVE_EMB        ; AH = 0Bh (Move EMB)
    call    far [cs:xms_entry]     ; Call XMS driver (DS:SI = struct)

    pop     si
    pop     ds

    cmp     ax, 1                   ; AX = 1 on success
    jne     .fail

    xor     ax, ax                  ; Return 0 on success
    retf

.fail:
    mov     ax, 0FFFFh              ; Return -1 on failure
    retf

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; xms_enable_a20_ - Enable A20 address line
;;
;; Watcom far call convention:
;;   Output: AX = 1 (success) or 0 (failure)
;; Clobbers: None
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _xms_enable_a20_
_xms_enable_a20_:
    mov     ah, XMS_ENABLE_A20      ; AH = 05h (Enable A20)
    call    far [cs:xms_entry]     ; Call XMS driver
    retf                            ; Return AX as-is

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; xms_disable_a20_ - Disable A20 address line
;;
;; Watcom far call convention:
;;   Output: AX = 1 (success) or 0 (failure)
;; Clobbers: None
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _xms_disable_a20_
_xms_disable_a20_:
    mov     ah, XMS_DISABLE_A20     ; AH = 06h (Disable A20)
    call    far [cs:xms_entry]     ; Call XMS driver
    retf                            ; Return AX as-is

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; xms_query_a20_ - Query A20 address line status
;;
;; Watcom far call convention:
;;   Output: AX = 1 (enabled) or 0 (disabled)
;; Clobbers: None
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _xms_query_a20_
_xms_query_a20_:
    mov     ah, XMS_QUERY_A20       ; AH = 07h (Query A20)
    call    far [cs:xms_entry]     ; Call XMS driver
    retf                            ; Return AX as-is

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; xms_query_free_ - Query free XMS memory
;;
;; Watcom far call convention:
;;   Input:  DX:AX = far pointer to free_kb output (uint32_t)
;;           CX:BX = far pointer to largest_kb output (uint32_t)
;;   Output: AX = 0 (success) or -1 (failure)
;; Clobbers: DS, SI, ES, DI
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _xms_query_free_
_xms_query_free_:
    push    ds
    push    si
    push    es
    push    di
    push    bx
    push    cx
    push    dx
    push    ax

    ;; Call XMS to query free memory
    mov     ah, XMS_QUERY_FREE      ; AH = 08h (Query Free EMB)
    call    far [cs:xms_entry]     ; Call XMS driver
    ;; Returns: AX = largest free block (KB), DX = total free (KB)

    ;; Save results
    push    ax                      ; Save largest block size
    push    dx                      ; Save total free size

    ;; Retrieve output pointers from stack
    mov     si, sp
    add     si, 12                  ; Skip saved regs to get to params

    ;; Get free_kb pointer (DX:AX param)
    mov     ax, [ss:si]             ; AX offset
    mov     dx, [ss:si+2]           ; DX segment
    mov     es, dx
    mov     di, ax

    ;; Write total free (from DX on stack)
    pop     dx                      ; Restore total free (high word = 0)
    xor     ax, ax
    mov     [es:di], dx             ; Low word of uint32_t
    mov     [es:di+2], ax           ; High word = 0

    ;; Get largest_kb pointer (CX:BX param)
    mov     ax, [ss:si+4]           ; BX offset
    mov     cx, [ss:si+6]           ; CX segment
    mov     es, cx
    mov     di, ax

    ;; Write largest block (from AX on stack)
    pop     dx                      ; Restore largest block (high word = 0)
    xor     ax, ax
    mov     [es:di], dx             ; Low word of uint32_t
    mov     [es:di+2], ax           ; High word = 0

    ;; Clean up stack and return success
    add     sp, 8                   ; Remove saved params
    pop     di
    pop     es
    pop     si
    pop     ds

    xor     ax, ax                  ; Return 0 (success)
    retf

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; xms_promisc_available_ - Check if XMS promiscuous mode is available
;;
;; Watcom far call convention:
;;   Output: AX = flag value (0 = unavailable, non-zero = available)
;; Clobbers: None
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _xms_promisc_available_
_xms_promisc_available_:
    mov     ax, [cs:xms_promisc_flag]
    retf

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; xms_routing_available_ - Check if XMS routing mode is available
;;
;; Watcom far call convention:
;;   Output: AX = flag value (0 = unavailable, non-zero = available)
;; Clobbers: None
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _xms_routing_available_
_xms_routing_available_:
    mov     ax, [cs:xms_routing_flag]
    retf

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; xms_unavailable_reason_ - Stub for C linkage (no leading underscore)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global xms_unavailable_reason_
xms_unavailable_reason_:
    xor ax, ax
    retf

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; xms_unavailable_reason_ - Get pointer to XMS unavailable reason string
;;
;; Watcom far call convention:
;;   Output: DX:AX = far pointer to reason string (in BSS)
;; Clobbers: None
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _xms_unavailable_reason_
_xms_unavailable_reason_:
    mov     ax, _g_xms_unavail_reason
    mov     dx, seg _g_xms_unavail_reason
    retf

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Module data (in hot section)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
xms_entry:
    dd      0                       ; XMS driver entry point (far ptr)
                                     ; Set by initialization code

xms_promisc_flag:
    dw      0                       ; Promiscuous mode availability flag

xms_routing_flag:
    dw      0                       ; Routing mode availability flag

hot_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; External references
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
extern _g_xms_unavail_reason        ; BSS: XMS unavailable reason string

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Patch table (empty - no patch points in this module)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
patch_table:
    ;; No patches required
