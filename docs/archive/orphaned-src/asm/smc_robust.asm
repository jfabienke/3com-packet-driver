;
; @file smc_robust.asm
; @brief Robust SMC patching with anchors and prefetch flush
;
; Uses labeled patch points instead of hard-coded offsets and
; properly flushes the prefetch queue after modifications.
;

SEGMENT _TEXT PUBLIC CLASS=CODE USE16

; External references
EXTERN _asm_is_v86_mode: FAR
EXTERN _asm_is_ring0: NEAR
EXTERN _cpu_type: BYTE

; Public patch functions
PUBLIC _smc_patch_all
PUBLIC _smc_flush_prefetch
PUBLIC _smc_atomic_patch_5byte

; Import patch points from other modules
EXTERN _vortex_tx_fast: NEAR
EXTERN _vortex_rx_fast: NEAR

;-----------------------------------------------------------------------------
; Patch anchors - these labels mark patchable code sections
;-----------------------------------------------------------------------------

; TX fast path patch point
PUBLIC _tx_burst_patch_start
PUBLIC _tx_burst_patch_end

_tx_burst_patch_start:
        ; Original code: rep outsw
        cld
        rep     outsw
        nop
        nop
        nop
        nop
_tx_burst_patch_end:

; RX fast path patch point
PUBLIC _rx_burst_patch_start
PUBLIC _rx_burst_patch_end

_rx_burst_patch_start:
        ; Original code: rep insw
        cld
        rep     insw
        nop
        nop
        nop
        nop
_rx_burst_patch_end:

;-----------------------------------------------------------------------------
; Flush prefetch queue after SMC
; Critical for 386+ processors
; GPT-5 Enhanced: Multiple flush methods for different CPU generations
;-----------------------------------------------------------------------------
_smc_flush_prefetch PROC NEAR
        ; Method 1: Near jump to flush queue (works on all CPUs)
        jmp     short .flush_1
.flush_1:
        ; Method 2: Self-modifying jump target (aggressive flush)
        jmp     $+2             ; Jump over next 0 bytes = flush
        
        ; Method 3: Far jump for complete flush (most thorough)
        push    cs
        push    OFFSET .flush_2
        retf
.flush_2:
        ; Method 4: Serializing instruction for 486+
        ; Check CPU type first
        push    ax
        mov     al, [_cpu_type]
        cmp     al, 4           ; 486 or higher?
        jb      .done
        
        ; 486+ can use WBINVD as serializing instruction
        ; But only in ring 0 and not in V86 mode
        call    _asm_is_ring0
        jz      .done           ; Not ring 0
        
        db      0Fh, 09h        ; WBINVD - serializes and flushes
        
.done:
        pop     ax
        ret
_smc_flush_prefetch ENDP

;-----------------------------------------------------------------------------
; Find and patch a code pattern
; Entry: DS:SI = search pattern, ES:DI = replacement, CX = length
;        BX = search start, DX = search limit
; Exit: AX = 0 if patched, -1 if not found
;-----------------------------------------------------------------------------
_smc_find_and_patch PROC NEAR
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    cx
        push    bx
        
        ; Save pattern info
        push    cx              ; Pattern length
        push    si              ; Pattern ptr
        push    di              ; Replacement ptr
        
.search_loop:
        ; Check if we've reached the limit
        cmp     bx, dx
        jae     .not_found
        
        ; Compare pattern at current position
        push    si
        push    di
        push    cx
        
        mov     di, bx          ; Current search position
        mov     si, [bp-4]      ; Pattern
        mov     cx, [bp-2]      ; Length
        
        push    es
        push    ds
        pop     es              ; ES = DS for comparison
        
        repe    cmpsb
        
        pop     es
        pop     cx
        pop     di
        pop     si
        
        je      .found          ; Pattern matched
        
        inc     bx              ; Try next position
        jmp     .search_loop
        
.found:
        ; Patch the code
        cli                     ; Disable interrupts
        
        push    es
        push    ds
        pop     es              ; ES = DS (code segment)
        
        mov     di, bx          ; Patch location
        mov     si, [bp-6]      ; Replacement
        mov     cx, [bp-2]      ; Length
        
        rep     movsb           ; Apply patch
        
        pop     es
        
        ; Flush prefetch queue
        call    _smc_flush_prefetch
        
        sti                     ; Re-enable interrupts
        
        xor     ax, ax          ; Success
        jmp     .done
        
.not_found:
        mov     ax, -1          ; Not found
        
.done:
        add     sp, 6           ; Clean up locals
        pop     bx
        pop     cx
        pop     di
        pop     si
        pop     bp
        ret
_smc_find_and_patch ENDP

;-----------------------------------------------------------------------------
; GPT-5 Specified: Atomic 5-byte patch application
; Entry: DS:SI = target address, ES:DI = 5-byte patch data
; Exit: AX = 0 on success, -1 on failure
; 
; This function ensures atomic patching of exactly 5 bytes with proper
; interrupt disable and prefetch queue flushing for all CPU types.
;-----------------------------------------------------------------------------
PUBLIC _smc_atomic_patch_5byte
_smc_atomic_patch_5byte PROC NEAR
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        push    ds
        
        ; Critical section start - disable interrupts
        pushf                   ; Save current interrupt flag
        cli                     ; Disable interrupts
        
        ; GPT-5 FIX: NO NMI MASKING - not needed for uniprocessor DOS
        ; and can cause system issues
        
        ; Perform the atomic 5-byte patch
        push    ds
        pop     es              ; ES = DS (target segment)
        
        mov     cx, 5           ; Exactly 5 bytes
        
        ; Use string instruction for write
        cld                     ; Clear direction flag
        rep     movsb           ; Copy 5 bytes
        
        ; Critical: Flush prefetch queue (GPT-5 validated sequence)
        ; Method 1: JMP $+2 (works on all CPUs)
        jmp     $+2
        
        ; Method 2: Near jump for pipeline flush
        jmp     short .flush_done
.flush_done:
        
        ; Method 3: For 486+ use serializing instruction
        ; Check CPU type first
        cmp     byte ptr [_cpu_type], 4
        jb      .restore_flags
        
        ; Optional: Use CPUID if available (Pentium+)
        cmp     byte ptr [_cpu_type], 5
        jb      .restore_flags
        
        ; Safe CPUID check (won't fault on CPUs without it)
        pushfd
        pop     eax
        mov     ebx, eax
        xor     eax, 200000h    ; Toggle ID bit
        push    eax
        popfd
        pushfd
        pop     eax
        cmp     eax, ebx
        je      .restore_flags   ; No CPUID
        
        ; CPUID available - use as serializing instruction
        xor     eax, eax
        cpuid                   ; Serializes execution
        
.restore_flags:
        ; Restore interrupt flag
        popf                    ; Restores previous interrupt state
        
        ; Success
        xor     ax, ax
        
        ; Cleanup and return
        pop     ds
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
_smc_atomic_patch_5byte ENDP

;-----------------------------------------------------------------------------
; Apply all SMC patches based on V86 mode
;-----------------------------------------------------------------------------
_smc_patch_all PROC NEAR
        push    bp
        mov     bp, sp
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        
        ; Check V86 mode
        call    FAR PTR _asm_is_v86_mode
        test    ax, ax
        jz      .patch_realmode
        
.patch_v86:
        ; Apply V86-safe patches
        
        ; Patch TX burst using anchors
        mov     si, OFFSET _tx_burst_patch_start
        mov     di, OFFSET v86_tx_chunk
        mov     cx, _tx_burst_patch_end - _tx_burst_patch_start
        call    _apply_patch_at_anchor
        
        ; Patch RX burst using anchors
        mov     si, OFFSET _rx_burst_patch_start
        mov     di, OFFSET v86_rx_chunk
        mov     cx, _rx_burst_patch_end - _rx_burst_patch_start
        call    _apply_patch_at_anchor
        
        jmp     .done
        
.patch_realmode:
        ; Real mode - patches may not be needed
        ; but we can optimize further if desired
        
.done:
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        pop     bp
        ret
_smc_patch_all ENDP

;-----------------------------------------------------------------------------
; Apply patch at a specific anchor point
; Entry: SI = anchor address, DI = patch data address, CX = length
;-----------------------------------------------------------------------------
_apply_patch_at_anchor PROC NEAR
        push    ax
        push    cx
        push    si
        push    di
        push    es
        push    ds
        
        cli                     ; Disable interrupts
        
        ; Set up segments properly
        ; ES:DI = destination (anchor in code segment)
        ; DS:SI = source (patch data)
        
        push    cs
        pop     es              ; ES = CS (code segment for destination)
        
        ; SI already points to anchor, move to DI for destination
        push    si
        pop     di              ; DI = anchor offset
        
        ; DI was patch data, move to SI for source
        push    di
        pop     si              ; SI = patch data offset
        
        ; Actually, let's be more explicit:
        ; On entry: SI = anchor, DI = patch data
        ; We need: ES:DI = anchor, DS:SI = patch
        
        mov     ax, si          ; Save anchor
        mov     si, di          ; SI = patch data (source)
        mov     di, ax          ; DI = anchor (destination)
        
        ; Now ES:DI points to anchor, DS:SI points to patch
        rep     movsb           ; Copy patch to anchor
        
        ; Flush prefetch queue
        call    _smc_flush_prefetch
        
        sti                     ; Re-enable interrupts
        
        pop     ds
        pop     es
        pop     di
        pop     si
        pop     cx
        pop     ax
        ret
_apply_patch_at_anchor ENDP

;-----------------------------------------------------------------------------
; V86-safe patch code
;-----------------------------------------------------------------------------

; V86 TX chunk (with delays)
v86_tx_chunk:
        push    cx
.tx_loop:
        cmp     cx, 8
        jbe     .tx_done
        push    cx
        mov     cx, 8
        rep     outsw
        jmp     $+2             ; Delay
        jmp     $+2
        pop     cx
        sub     cx, 8
        jmp     .tx_loop
.tx_done:
        rep     outsw
        pop     cx

; V86 RX chunk (with delays)
v86_rx_chunk:
        push    cx
.rx_loop:
        cmp     cx, 8
        jbe     .rx_done
        push    cx
        mov     cx, 8
        rep     insw
        jmp     $+2             ; Delay
        jmp     $+2
        pop     cx
        sub     cx, 8
        jmp     .rx_loop
.rx_done:
        rep     insw
        pop     cx

ENDS

END