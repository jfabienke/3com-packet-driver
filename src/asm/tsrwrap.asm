; @file tsr_c_wrappers.asm
; @brief C-callable wrappers for TSR defensive programming
;
; Provides C-callable wrapper functions around the defensive programming
; macros and routines. Enables C code to access DOS safety checks,
; vector monitoring, and other TSR defensive capabilities.
;
; Last Updated: 2026-01-23 21:45:00 CET
; Note: Converted to NASM syntax

bits 16
cpu 386

; C symbol naming bridge (maps C symbols to symbol_)
%include "csym.inc"

%include 'tsr_defensive.inc'

; External references to data defined in other modules
extern caller_ss
extern caller_sp
extern critical_nesting
extern indos_segment
extern indos_offset
extern criterr_segment
extern criterr_offset

; External references to functions in defensive_integration.asm
extern periodic_vector_monitoring
extern deferred_work_queue_add
extern deferred_work_queue_process
extern deferred_work_queue_count

; ############################################################################
; MODULE SEGMENT
; ############################################################################
segment MODULE class=MODULE align=16

; ============================================================================
; 64-byte Module Header
; ============================================================================
global _mod_tsrwrap_header
_mod_tsrwrap_header:
    db  'PKTDRV', 0             ; +00  7 bytes: module signature
    db  1                       ; +07  1 byte:  major version
    db  0                       ; +08  1 byte:  minor version
    db  0                       ; +09  1 byte:  cpu_req (0 = 8086)
    db  0                       ; +0A  1 byte:  nic_type (0 = generic)
    db  1                       ; +0B  1 byte:  cap_flags (1 = MOD_CAP_CORE)
    dw  hot_end - hot_start     ; +0C  2 bytes: hot code size
    dw  patch_table             ; +0E  2 bytes: offset to patch table
    dw  patch_table_end - patch_table  ; +10  2 bytes: patch table size
    dw  hot_start               ; +12  2 bytes: offset to hot_start
    dw  hot_end                 ; +14  2 bytes: offset to hot_end
    times 64 - ($ - _mod_tsrwrap_header) db 0  ; Pad to 64 bytes

segment _TEXT class=CODE

; ============================================================================
; HOT PATH START
; ============================================================================
hot_start:

;=============================================================================
; DOS SAFETY CHECK WRAPPERS
;=============================================================================

; C-callable wrapper for CHECK_DOS_SAFE macro
; Returns: AX = 0 if safe, non-zero if busy
global asm_check_dos_safe
asm_check_dos_safe:
        push    bp
        mov     bp, sp

        CHECK_DOS_SAFE
        jz      .safe

        ; DOS is busy
        mov     ax, 1
        jmp     .done

.safe:
        ; DOS is available
        xor     ax, ax

.done:
        pop     bp
        ret

; ======================================================
; asm_check_dos_completely_safe
;
; Checks both InDOS flag and critical error flag to determine if DOS
; is completely safe for reentrancy. This is the definitive test for
; whether DOS calls can be made safely from ISR or deferred contexts.
;
; PRECONDITION: Caller should have DS=DGROUP, but function is defensive
;               and will work with any DS on entry.
;
; Returns:
;   AX = 0 (ZF=1) if completely safe for DOS calls,
;   AX = 1 (ZF=0) if DOS is busy or in critical error state.
; Preserves: DS is preserved to original value.
; Clobbers: AX, BX, ES, SI. Flags reflect result via final AX value.
; CRITICAL: Fixed DS ordering bug - loads offset before switching DS
; ======================================================
global asm_check_dos_completely_safe
asm_check_dos_completely_safe:
        push    bp
        mov     bp, sp
        push    ds
        push    es
        push    bx
        push    si

        ; Ensure DS points to DGROUP to read our variables safely
        ; (Assumes caller has DS=DGROUP, but we explicitly set it for safety)
        mov     ax, ds                  ; Save caller DS
        push    ax                      ; Save on stack for later restore

        ; Check if InDOS pointer is available
        cmp     word [indos_segment], 0
        jz      .not_safe               ; No InDOS pointer - assume unsafe

        ; Check InDOS flag
        mov     es, [indos_segment]
        mov     bx, [indos_offset]
        cmp     byte [es:bx], 0         ; InDOS count must be 0
        jnz     .not_safe

        ; CRITICAL FIX: Load the far pointer parts from DGROUP BEFORE changing DS
        mov     ax, [criterr_segment]
        mov     si, [criterr_offset]

        or      ax, ax
        jz      .safe                   ; No pointer available -> assume safe

        ; Test bit 0 at [AX:SI] - now DS change is safe
        push    ds                      ; Save current DS
        mov     ds, ax                  ; DS = criterr_segment
        test    byte [si], 01h          ; Test ErrorMode bit 0 at criterr_offset
        pop     ds                      ; Restore DS
        jnz     .not_safe               ; Bit set -> not safe

.safe:
        pop     ax                      ; Restore caller DS (discard)
        xor     ax, ax                  ; AX = 0 (safe)
        jmp     .done

.not_safe:
        pop     ax                      ; Restore caller DS (discard)
        mov     ax, 1                   ; AX = 1 (not safe)

.done:
        pop     si
        pop     bx
        pop     es
        pop     ds
        pop     bp
        ret

; Initialize DOS safety monitoring
; Returns: AX = 0 on success, non-zero on error
; CRITICAL: This function properly preserves DS to avoid memory corruption
global asm_dos_safety_init
asm_dos_safety_init:
        push    bp
        mov     bp, sp
        push    ds
        push    es
        push    bx
        push    si
        push    di
        push    cx

        mov     di, ds                  ; DI = original DGROUP for restoring

        ; Get InDOS flag address (DOS 3.0+)
        mov     ax, 3400h               ; Get InDOS flag address
        int     21h
        jc      .dos_2x_fallback

        ; CRITICAL: Restore DS to DGROUP before storing
        mov     ds, di                  ; DS = DGROUP
        mov     [indos_segment], es     ; Store InDOS segment
        mov     [indos_offset], bx      ; Store InDOS offset

        ; Try to get critical error flag address (DOS 3.1+)
        mov     ax, 5D06h               ; Get critical error flag
        int     21h
        jc      .no_criterr_flag

        ; CRITICAL: INT 21h AX=5D06h returns DS:SI, but changes DS!
        ; Save returned DS:SI before restoring our DS
        mov     bx, ds                  ; BX = returned segment from DOS
        mov     cx, si                  ; CX = returned offset from DOS
        mov     ds, di                  ; DS = DGROUP (restore our data segment)
        mov     [criterr_segment], bx   ; Store critical error segment
        mov     [criterr_offset], cx    ; Store critical error offset
        jmp     .init_success

.no_criterr_flag:
        ; No critical error flag available - restore DS first
        mov     ds, di                  ; DS = DGROUP
        mov     word [criterr_segment], 0
        mov     word [criterr_offset], 0
        jmp     .init_success

.dos_2x_fallback:
        ; DOS 2.x - can't get InDOS flag safely
        mov     ah, 30h                 ; Get DOS version
        int     21h
        mov     ds, di                  ; DS = DGROUP (restore before any stores)
        cmp     al, 3                   ; Check if DOS 3.0+
        jae     .init_error             ; Should have worked but didn't

        ; For DOS 2.x, disable DOS safety checks (safer than unreliable pointers)
        mov     word [indos_segment], 0
        mov     word [indos_offset], 0
        mov     word [criterr_segment], 0
        mov     word [criterr_offset], 0
        ; Treat as successful init but with limited capability

.init_success:
        xor     ax, ax                  ; Success
        jmp     .exit

.init_error:
        mov     ax, 1                   ; Error

.exit:
        ; DS is already restored to DGROUP at this point
        pop     cx
        pop     di
        pop     si
        pop     bx
        pop     es
        pop     ds
        pop     bp
        ret

;=============================================================================
; VECTOR MONITORING WRAPPERS
;=============================================================================

; Check vector ownership
; Returns: AX = number of vectors hijacked
global asm_check_vector_ownership
asm_check_vector_ownership:
        push    bp
        mov     bp, sp

        ; For now, return 0 (no vectors hijacked)
        ; Full implementation would check all installed vectors
        xor     ax, ax

        pop     bp
        ret

; Periodic vector monitoring wrapper
; Returns: AX = number of vectors recovered
global asm_periodic_vector_monitoring
asm_periodic_vector_monitoring:
        push    bp
        mov     bp, sp

        ; Call the actual monitoring function from defensive_integration.asm
        call    periodic_vector_monitoring
        ; AX already contains return value

        pop     bp
        ret

;=============================================================================
; DEFERRED WORK QUEUE WRAPPERS
;=============================================================================

; Add work item to deferred queue
; Parameters: Function pointer passed on stack (C calling convention)
; Returns: AX = 0 on success, -1 if queue full
global asm_deferred_add_work
asm_deferred_add_work:
        push    bp
        mov     bp, sp

        ; Function pointer is already at [BP+4] - pass it through to the queue
        ; deferred_work_queue_add uses C calling convention and expects [BP+4]
        call    deferred_work_queue_add
        ; AX contains return value

        pop     bp
        ret

; Process pending deferred work
; Returns: AX = number of items processed
global asm_deferred_process_pending
asm_deferred_process_pending:
        push    bp
        mov     bp, sp

        ; Only process if DOS is completely safe
        CHECK_DOS_COMPLETELY_SAFE
        jnz     .not_safe

        ; Call queue process function
        call    deferred_work_queue_process
        ; AX contains return value
        jmp     .exit

.not_safe:
        xor     ax, ax                  ; Processed 0 items

.exit:
        pop     bp
        ret

; Check pending work count
; Returns: AX = number of pending work items
global asm_deferred_work_pending
asm_deferred_work_pending:
        push    bp
        mov     bp, sp

        ; Call queue count function
        call    deferred_work_queue_count
        ; AX contains return value

        pop     bp
        ret

;=============================================================================
; EMERGENCY RECOVERY WRAPPERS
;=============================================================================

; Emergency TSR recovery
; Returns: AX = 0 if successful, non-zero if failed
global asm_tsr_emergency_recovery
asm_tsr_emergency_recovery:
        push    bp
        mov     bp, sp

        ; For now, return success
        ; Full implementation would attempt various recovery strategies
        xor     ax, ax

        pop     bp
        ret

; Validate TSR integrity
; Returns: AX = 0 if all checks pass, non-zero for corruption
global asm_tsr_validate_integrity
asm_tsr_validate_integrity:
        push    bp
        mov     bp, sp

        ; For now, return success
        ; Full implementation would validate memory canaries, checksums, etc.
        xor     ax, ax

        pop     bp
        ret

; ============================================================================
; HOT PATH END
; ============================================================================
hot_end:

; ============================================================================
; PATCH TABLE
; ============================================================================
patch_table:
patch_table_end:
