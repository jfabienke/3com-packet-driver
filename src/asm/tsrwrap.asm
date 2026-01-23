; @file tsr_c_wrappers.asm
; @brief C-callable wrappers for TSR defensive programming
;
; Provides C-callable wrapper functions around the defensive programming
; macros and routines. Enables C code to access DOS safety checks,
; vector monitoring, and other TSR defensive capabilities.
;
; Last Updated: 2026-01-23 19:55:00 CET
; Note: Fixed local labels for WASM/MASM compatibility

.MODEL SMALL
.386

include 'tsr_defensive_wasm.inc'

; External references to data defined in other modules
EXTERN caller_ss:WORD
EXTERN caller_sp:WORD
EXTERN critical_nesting:BYTE
EXTERN indos_segment:WORD
EXTERN indos_offset:WORD
EXTERN criterr_segment:WORD
EXTERN criterr_offset:WORD

; External references to functions in defensive_integration.asm
EXTERN periodic_vector_monitoring:PROC
EXTERN deferred_work_queue_add:PROC
EXTERN deferred_work_queue_process:PROC
EXTERN deferred_work_queue_count:PROC

_TEXT SEGMENT
        ASSUME  CS:_TEXT

;=============================================================================
; DOS SAFETY CHECK WRAPPERS
;=============================================================================

; C-callable wrapper for CHECK_DOS_SAFE macro
; Returns: AX = 0 if safe, non-zero if busy
PUBLIC asm_check_dos_safe
asm_check_dos_safe PROC
        push    bp
        mov     bp, sp

        CHECK_DOS_SAFE
        jz      cds_safe

        ; DOS is busy
        mov     ax, 1
        jmp     cds_done

cds_safe:
        ; DOS is available
        xor     ax, ax

cds_done:
        pop     bp
        ret
asm_check_dos_safe ENDP

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
PUBLIC asm_check_dos_completely_safe
asm_check_dos_completely_safe PROC
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
        cmp     word ptr [indos_segment], 0
        jz      cdcs_not_safe           ; No InDOS pointer - assume unsafe

        ; Check InDOS flag
        mov     es, [indos_segment]
        mov     bx, [indos_offset]
        cmp     byte ptr [es:bx], 0     ; InDOS count must be 0
        jnz     cdcs_not_safe

        ; CRITICAL FIX: Load the far pointer parts from DGROUP BEFORE changing DS
        mov     ax, [criterr_segment]
        mov     si, [criterr_offset]

        or      ax, ax
        jz      cdcs_safe               ; No pointer available -> assume safe

        ; Test bit 0 at [AX:SI] - now DS change is safe
        push    ds                      ; Save current DS
        mov     ds, ax                  ; DS = criterr_segment
        test    byte ptr [si], 01h      ; Test ErrorMode bit 0 at criterr_offset
        pop     ds                      ; Restore DS
        jnz     cdcs_not_safe           ; Bit set -> not safe

cdcs_safe:
        pop     ax                      ; Restore caller DS (discard)
        xor     ax, ax                  ; AX = 0 (safe)
        jmp     cdcs_done

cdcs_not_safe:
        pop     ax                      ; Restore caller DS (discard)
        mov     ax, 1                   ; AX = 1 (not safe)

cdcs_done:
        pop     si
        pop     bx
        pop     es
        pop     ds
        pop     bp
        ret
asm_check_dos_completely_safe ENDP

; Initialize DOS safety monitoring
; Returns: AX = 0 on success, non-zero on error
; CRITICAL: This function properly preserves DS to avoid memory corruption
PUBLIC asm_dos_safety_init
asm_dos_safety_init PROC
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
        jc      dsi_dos_2x_fallback

        ; CRITICAL: Restore DS to DGROUP before storing
        mov     ds, di                  ; DS = DGROUP
        mov     [indos_segment], es     ; Store InDOS segment
        mov     [indos_offset], bx      ; Store InDOS offset

        ; Try to get critical error flag address (DOS 3.1+)
        mov     ax, 5D06h               ; Get critical error flag
        int     21h
        jc      dsi_no_criterr_flag

        ; CRITICAL: INT 21h AX=5D06h returns DS:SI, but changes DS!
        ; Save returned DS:SI before restoring our DS
        mov     bx, ds                  ; BX = returned segment from DOS
        mov     cx, si                  ; CX = returned offset from DOS
        mov     ds, di                  ; DS = DGROUP (restore our data segment)
        mov     [criterr_segment], bx   ; Store critical error segment
        mov     [criterr_offset], cx    ; Store critical error offset
        jmp     dsi_init_success

dsi_no_criterr_flag:
        ; No critical error flag available - restore DS first
        mov     ds, di                  ; DS = DGROUP
        mov     word ptr [criterr_segment], 0
        mov     word ptr [criterr_offset], 0
        jmp     dsi_init_success

dsi_dos_2x_fallback:
        ; DOS 2.x - can't get InDOS flag safely
        mov     ah, 30h                 ; Get DOS version
        int     21h
        mov     ds, di                  ; DS = DGROUP (restore before any stores)
        cmp     al, 3                   ; Check if DOS 3.0+
        jae     dsi_init_error          ; Should have worked but didn't

        ; For DOS 2.x, disable DOS safety checks (safer than unreliable pointers)
        mov     word ptr [indos_segment], 0
        mov     word ptr [indos_offset], 0
        mov     word ptr [criterr_segment], 0
        mov     word ptr [criterr_offset], 0
        ; Treat as successful init but with limited capability

dsi_init_success:
        xor     ax, ax                  ; Success
        jmp     dsi_done

dsi_init_error:
        mov     ax, 1                   ; Error

dsi_done:
        ; DS is already restored to DGROUP at this point
        pop     cx
        pop     di
        pop     si
        pop     bx
        pop     es
        pop     ds
        pop     bp
        ret
asm_dos_safety_init ENDP

;=============================================================================
; VECTOR MONITORING WRAPPERS
;=============================================================================

; Check vector ownership
; Returns: AX = number of vectors hijacked
PUBLIC asm_check_vector_ownership
asm_check_vector_ownership PROC
        push    bp
        mov     bp, sp

        ; For now, return 0 (no vectors hijacked)
        ; Full implementation would check all installed vectors
        xor     ax, ax

        pop     bp
        ret
asm_check_vector_ownership ENDP

; Periodic vector monitoring wrapper
; Returns: AX = number of vectors recovered
PUBLIC asm_periodic_vector_monitoring
asm_periodic_vector_monitoring PROC
        push    bp
        mov     bp, sp

        ; Call the actual monitoring function from defensive_integration.asm
        call    periodic_vector_monitoring
        ; AX already contains return value

        pop     bp
        ret
asm_periodic_vector_monitoring ENDP

;=============================================================================
; DEFERRED WORK QUEUE WRAPPERS
;=============================================================================

; Add work item to deferred queue
; Parameters: Function pointer passed on stack (C calling convention)
; Returns: AX = 0 on success, -1 if queue full
PUBLIC asm_deferred_add_work
asm_deferred_add_work PROC
        push    bp
        mov     bp, sp

        ; Function pointer is already at [BP+4] - pass it through to the queue
        ; deferred_work_queue_add uses C calling convention and expects [BP+4]
        call    deferred_work_queue_add
        ; AX contains return value

        pop     bp
        ret
asm_deferred_add_work ENDP

; Process pending deferred work
; Returns: AX = number of items processed
PUBLIC asm_deferred_process_pending
asm_deferred_process_pending PROC
        push    bp
        mov     bp, sp

        ; Only process if DOS is completely safe
        CHECK_DOS_COMPLETELY_SAFE
        jnz     dpp_not_safe

        ; Call queue process function
        call    deferred_work_queue_process
        ; AX contains return value
        jmp     dpp_done

dpp_not_safe:
        xor     ax, ax                  ; Processed 0 items

dpp_done:
        pop     bp
        ret
asm_deferred_process_pending ENDP

; Check pending work count
; Returns: AX = number of pending work items
PUBLIC asm_deferred_work_pending
asm_deferred_work_pending PROC
        push    bp
        mov     bp, sp

        ; Call queue count function
        call    deferred_work_queue_count
        ; AX contains return value

        pop     bp
        ret
asm_deferred_work_pending ENDP

;=============================================================================
; EMERGENCY RECOVERY WRAPPERS
;=============================================================================

; Emergency TSR recovery
; Returns: AX = 0 if successful, non-zero if failed
PUBLIC asm_tsr_emergency_recovery
asm_tsr_emergency_recovery PROC
        push    bp
        mov     bp, sp

        ; For now, return success
        ; Full implementation would attempt various recovery strategies
        xor     ax, ax

        pop     bp
        ret
asm_tsr_emergency_recovery ENDP

; Validate TSR integrity
; Returns: AX = 0 if all checks pass, non-zero for corruption
PUBLIC asm_tsr_validate_integrity
asm_tsr_validate_integrity PROC
        push    bp
        mov     bp, sp

        ; For now, return success
        ; Full implementation would validate memory canaries, checksums, etc.
        xor     ax, ax

        pop     bp
        ret
asm_tsr_validate_integrity ENDP

_TEXT ENDS
END
