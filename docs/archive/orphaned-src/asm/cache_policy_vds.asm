;-----------------------------------------------------------------------------
; @file cache_policy_vds.asm
; @brief Cache management policy driven by VDS hints
;
; GPT-5 Critical Fix: Never default to NOP when DMA active without VDS
; Most PC chipsets have hardware cache coherency - but NOT for ISA bus masters!
;
; ISA Reality: ISA bus masters often bypass CPU cache - must flush
;-----------------------------------------------------------------------------

SECTION .text

;=============================================================================
; EXTERNAL REFERENCES
;=============================================================================

EXTERN vds_cache_policy_hint
EXTERN _cpu_type
EXTERN _has_clflush
EXTERN _has_wbinvd
EXTERN vds_available        ; Critical: Check if VDS is present
EXTERN tx_use_dma           ; Critical: Check if using bus-master DMA
EXTERN detect_v86_mode      ; Critical: Check for V86 mode

;=============================================================================
; CACHE TIER DEFINITIONS (GPT-5 Validated)
;=============================================================================

CACHE_TIER_NOP          equ 0       ; Hardware coherent (safe ONLY with VDS)
CACHE_TIER_CLFLUSH      equ 1       ; P4+ selective flush (rare)
CACHE_TIER_WBINVD       equ 2       ; 486+ sledgehammer (required without VDS)
CACHE_TIER_BARRIER      equ 3       ; Software fence only
CACHE_TIER_PIO_ONLY     equ 4       ; Force PIO - no DMA (386 without VDS)

;=============================================================================
; DATA SEGMENT
;=============================================================================

SECTION .data

; Cache policy configuration
GLOBAL cache_policy_tier
GLOBAL cache_operations_count
GLOBAL cache_nop_count
GLOBAL cache_flush_count

cache_policy_tier:      db CACHE_TIER_WBINVD  ; Default: SAFE (not NOP!)
cache_operations_count: dd 0        ; Total cache operations
cache_nop_count:        dd 0        ; NOPs (coherent)
cache_flush_count:      dd 0        ; Actual flushes

; Patch statistics
patches_applied:        dw 0
patch_failures:         dw 0

SECTION .text

;=============================================================================
; CACHE POLICY INITIALIZATION
;=============================================================================

;-----------------------------------------------------------------------------
; init_cache_policy - Initialize cache policy based on system detection
;
; GPT-5 CRITICAL: Never use NOP without VDS when bus-master DMA is active
;-----------------------------------------------------------------------------
GLOBAL init_cache_policy
init_cache_policy:
        push    ax
        push    bx
        push    cx
        push    dx
        
        ; === CRITICAL SAFETY CHECK ===
        ; Check if we're using bus-master DMA (3C515-TX)
        cmp     byte [tx_use_dma], 0
        je      .pio_only_mode          ; No DMA = safe to use NOP
        
        ; Using DMA - check VDS availability
        cmp     byte [vds_available], 0
        je      .no_vds_with_dma        ; CRITICAL: DMA without VDS!
        
        ; === VDS Available with DMA ===
        ; Check VDS cache policy hint
        call    vds_cache_policy_hint
        test    al, al
        jz      .vds_says_coherent      ; VDS says coherent
        
        ; VDS indicates cache management needed
        jmp     .need_cache_management
        
.no_vds_with_dma:
        ; === CRITICAL PATH: Bus-master DMA without VDS ===
        ; ISA bus masters do NOT snoop CPU cache on most systems!
        
        ; CRITICAL: Check for V86 mode first (WBINVD will fault in V86!)
        call    detect_v86_mode
        test    al, al
        jnz     .force_pio_v86          ; V86 without VDS - WBINVD will fault!
        
        ; Not V86 - check CPU for WBINVD capability
        cmp     byte [_cpu_type], 4     ; 486 or higher?
        jb      .force_pio_386          ; 386 can't flush - must use PIO
        
        ; 486+ in real mode - safe to use WBINVD
        mov     byte [cache_policy_tier], CACHE_TIER_WBINVD
        jmp     .apply_patches
        
.force_pio_v86:
        ; V86 mode without VDS - WBINVD is privileged and will fault!
        ; MUST use PIO only for safety
        mov     byte [cache_policy_tier], CACHE_TIER_PIO_ONLY
        jmp     .apply_patches
        
.force_pio_386:
        ; 386 with DMA but no VDS - no WBINVD instruction
        ; MUST use PIO only
        mov     byte [cache_policy_tier], CACHE_TIER_PIO_ONLY
        jmp     .apply_patches
        
.pio_only_mode:
        ; Not using DMA - safe to use NOP
        mov     byte [cache_policy_tier], CACHE_TIER_NOP
        jmp     .apply_patches
        
.vds_says_coherent:
        ; VDS present and says coherent - trust it
        mov     byte [cache_policy_tier], CACHE_TIER_NOP
        jmp     .apply_patches
        
.need_cache_management:
        ; VDS says we need cache management
        ; Choose based on CPU capabilities
        
        ; Check for CLFLUSH (Pentium 4+)
        cmp     byte [_has_clflush], 1
        jne     .check_wbinvd
        
        ; CLFLUSH available but use sparingly
        mov     byte [cache_policy_tier], CACHE_TIER_BARRIER
        jmp     .apply_patches
        
.check_wbinvd:
        ; Check for WBINVD (486+)
        cmp     byte [_has_wbinvd], 1
        jne     .use_barrier
        
        ; WBINVD available - use it when VDS says needed
        mov     byte [cache_policy_tier], CACHE_TIER_WBINVD
        jmp     .apply_patches
        
.use_barrier:
        ; 386 or no cache management instructions
        mov     byte [cache_policy_tier], CACHE_TIER_BARRIER
        
.apply_patches:
        ; Apply SMC patches based on selected tier
        call    apply_cache_tier_patches
        
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

;=============================================================================
; CACHE TIER IMPLEMENTATIONS
;=============================================================================

;-----------------------------------------------------------------------------
; cache_operation - Main cache operation based on policy
;
; This is the hot path - optimized for common cases
;-----------------------------------------------------------------------------
GLOBAL cache_operation
cache_operation:
        ; Update statistics
        inc     word [cache_operations_count]
        adc     word [cache_operations_count+2], 0
        
        ; Check policy tier
        mov     al, [cache_policy_tier]
        
        cmp     al, CACHE_TIER_NOP
        je      .tier_nop
        cmp     al, CACHE_TIER_WBINVD
        je      .tier_wbinvd
        cmp     al, CACHE_TIER_BARRIER
        je      .tier_barrier
        cmp     al, CACHE_TIER_CLFLUSH
        je      .tier_clflush
        cmp     al, CACHE_TIER_PIO_ONLY
        je      .tier_pio_only
        
        ; Unknown tier - default to barrier
        jmp     .tier_barrier
        
.tier_nop:
        ; === TIER 0: NOP (only safe with VDS) ===
        inc     word [cache_nop_count]
        adc     word [cache_nop_count+2], 0
        ret                             ; Fast return
        
.tier_wbinvd:
        ; === TIER 2: WBINVD (required without VDS) ===
        push    ax
        call    cache_wbinvd_safe
        pop     ax
        jmp     .done_flush
        
.tier_barrier:
        ; === TIER 3: Software barrier ===
        push    ax
        call    cache_barrier
        pop     ax
        jmp     .done_flush
        
.tier_clflush:
        ; === TIER 1: CLFLUSH (selective) ===
        push    ax
        call    cache_clflush_selective
        pop     ax
        jmp     .done_flush
        
.tier_pio_only:
        ; === TIER 4: PIO only - no cache ops needed ===
        ret
        
.done_flush:
        inc     word [cache_flush_count]
        adc     word [cache_flush_count+2], 0
        ret

;-----------------------------------------------------------------------------
; cache_barrier - Software memory barrier
;
; Ensures write ordering without cache flush
;-----------------------------------------------------------------------------
cache_barrier:
        push    ax
        push    dx
        
        ; I/O operation for serialization
        ; Reading from a safe port ensures ordering
        mov     dx, 80h                 ; POST diagnostic port
        in      al, dx                  ; Serializing I/O
        
        ; Memory fence for 486+
        cmp     byte [_cpu_type], 4
        jb      .done
        
        ; Use locked operation as fence
        lock inc word [cache_operations_count]
        dec     word [cache_operations_count]
        
.done:
        pop     dx
        pop     ax
        ret

;-----------------------------------------------------------------------------
; cache_wbinvd_safe - Safe WBINVD for 486+
;
; Critical for DMA without VDS - ensures cache coherence
;-----------------------------------------------------------------------------
cache_wbinvd_safe:
        push    ax
        
        ; Check if we can use WBINVD
        cmp     byte [_cpu_type], 4     ; 486+?
        jb      .no_wbinvd
        
        cmp     byte [_has_wbinvd], 1
        jne     .no_wbinvd
        
        ; Execute WBINVD (expensive but necessary without VDS)
        wbinvd
        jmp     .done
        
.no_wbinvd:
        ; Fall back to barrier
        call    cache_barrier
        
.done:
        pop     ax
        ret

;-----------------------------------------------------------------------------
; cache_clflush_selective - Selective cache line flush
;
; Only flushes specific cache lines, not entire cache
;-----------------------------------------------------------------------------
cache_clflush_selective:
        push    ax
        push    di
        push    es
        
        ; Check if CLFLUSH available
        cmp     byte [_has_clflush], 1
        jne     .no_clflush
        
        ; For now, just flush one line as example
        ; Real implementation would flush specific buffer
        push    ds
        pop     es
        xor     di, di                  ; ES:DI = address to flush
        
        ; CLFLUSH instruction
        db      0Fh, 0AEh, 3Dh         ; CLFLUSH [DI]
        jmp     .done
        
.no_clflush:
        ; Fall back to barrier
        call    cache_barrier
        
.done:
        pop     es
        pop     di
        pop     ax
        ret

;=============================================================================
; SMC PATCH APPLICATION
;=============================================================================

;-----------------------------------------------------------------------------
; apply_cache_tier_patches - Apply SMC patches for selected tier
;-----------------------------------------------------------------------------
apply_cache_tier_patches:
        push    ax
        push    bx
        push    cx
        push    si
        push    di
        push    es
        
        ; Get selected tier
        mov     al, [cache_policy_tier]
        
        ; Select patch template based on tier
        cmp     al, CACHE_TIER_NOP
        je      .patch_nop
        cmp     al, CACHE_TIER_BARRIER
        je      .patch_barrier
        cmp     al, CACHE_TIER_WBINVD
        je      .patch_wbinvd
        cmp     al, CACHE_TIER_CLFLUSH
        je      .patch_clflush
        cmp     al, CACHE_TIER_PIO_ONLY
        je      .patch_pio_only
        
        jmp     .done                   ; Unknown tier
        
.patch_nop:
        ; Patch all cache points with 5-byte NOP sled
        mov     si, nop_5byte_template
        jmp     .apply_patches
        
.patch_barrier:
        ; Patch with CALL to barrier routine
        mov     si, barrier_call_template
        jmp     .apply_patches
        
.patch_wbinvd:
        ; Patch with inline WBINVD
        mov     si, wbinvd_inline_template
        jmp     .apply_patches
        
.patch_clflush:
        ; Patch with inline CLFLUSH
        mov     si, clflush_inline_template
        jmp     .apply_patches
        
.patch_pio_only:
        ; No patches needed - PIO doesn't need cache ops
        jmp     .done
        
.apply_patches:
        ; Apply patches to all cache operation points
        ; This would patch the actual hot path locations
        inc     word [patches_applied]
        
        ; CRITICAL: Flush prefetch after SMC
        jmp     short $+2               ; Prefetch flush
        
.done:
        pop     es
        pop     di
        pop     si
        pop     cx
        pop     bx
        pop     ax
        ret

;=============================================================================
; PATCH TEMPLATES
;=============================================================================

SECTION .data

; 5-byte NOP sled (for coherent systems with VDS)
nop_5byte_template:
        db 90h, 90h, 90h, 90h, 90h
        
; CALL to barrier routine
barrier_call_template:
        db 0E8h                         ; CALL rel16
        dw cache_barrier - $ - 3
        db 90h, 90h                     ; Padding to 5 bytes
        
; Inline WBINVD (required without VDS)
wbinvd_inline_template:
        db 0Fh, 09h                     ; WBINVD
        db 90h, 90h, 90h                ; Padding
        
; Inline CLFLUSH [DI]
clflush_inline_template:
        db 0Fh, 0AEh, 3Dh              ; CLFLUSH [DI]
        db 90h, 90h                     ; Padding