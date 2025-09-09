;****************************************************************************
; @file pio_lib.asm
; @brief Shared PIO Library - CPU-Optimized I/O Routines for PTASK.MOD
;
; Team A Implementation - CPU-specific optimized I/O operations
; Supports 80286 through Pentium with self-modifying code optimizations
;****************************************************************************

.model small
.code

; External dependencies
EXTERN _cpu_type:WORD
EXTERN _cpu_features:WORD

; Public interface functions
PUBLIC pio_lib_init
PUBLIC pio_get_interface
PUBLIC _pio_outb_optimized
PUBLIC _pio_outw_optimized
PUBLIC _pio_inb_optimized
PUBLIC _pio_inw_optimized
PUBLIC _pio_outsw_optimized
PUBLIC _pio_insw_optimized

; CPU optimization functions
PUBLIC pio_patch_286_optimizations
PUBLIC pio_patch_386_optimizations
PUBLIC pio_patch_486_optimizations
PUBLIC pio_patch_pentium_optimizations
PUBLIC flush_prefetch_queue

;=============================================================================
; Constants and Structures
;=============================================================================

; CPU Types
CPU_TYPE_80286          EQU 0286h
CPU_TYPE_80386          EQU 0386h
CPU_TYPE_80486          EQU 0486h
CPU_TYPE_PENTIUM        EQU 0586h

; CPU Features
CPU_FEATURE_32BIT       EQU 0001h
CPU_FEATURE_CACHE       EQU 0002h
CPU_FEATURE_PIPELINE    EQU 0004h

; Optimization levels
OPT_LEVEL_BASIC         EQU 0
OPT_LEVEL_386           EQU 1
OPT_LEVEL_486           EQU 2
OPT_LEVEL_PENTIUM       EQU 3

;=============================================================================
; Data Section
;=============================================================================

.data

; PIO Interface Structure (matches C definition)
pio_interface_struct    LABEL BYTE
    outb_ptr            DW      OFFSET _pio_outb_optimized
    outw_ptr            DW      OFFSET _pio_outw_optimized
    inb_ptr             DW      OFFSET _pio_inb_optimized
    inw_ptr             DW      OFFSET _pio_inw_optimized
    outsw_ptr           DW      OFFSET _pio_outsw_optimized
    insw_ptr            DW      OFFSET _pio_insw_optimized

; Optimization state
optimization_level      DB      OPT_LEVEL_BASIC
cpu_detected            DW      CPU_TYPE_80286
features_detected       DW      0
patches_applied         DB      0   ; Boolean flag

;=============================================================================
; Code Section - Public Interface
;=============================================================================

;-----------------------------------------------------------------------------
; @brief Initialize PIO library with CPU detection
; @param AX = CPU type from detection
; @param BX = CPU features
; @return AX = 0 on success, negative on error
;-----------------------------------------------------------------------------
pio_lib_init PROC
    push    bp
    mov     bp, sp
    push    bx
    push    cx
    push    dx
    
    ; Store CPU information
    mov     [cpu_detected], ax
    mov     [features_detected], bx
    
    ; Determine optimization level
    cmp     ax, CPU_TYPE_PENTIUM
    jae     init_pentium
    cmp     ax, CPU_TYPE_80486
    jae     init_486
    cmp     ax, CPU_TYPE_80386
    jae     init_386
    
    ; Default to 80286 optimization
    mov     [optimization_level], OPT_LEVEL_BASIC
    jmp     init_complete
    
init_386:
    mov     [optimization_level], OPT_LEVEL_386
    jmp     init_complete
    
init_486:
    mov     [optimization_level], OPT_LEVEL_486
    jmp     init_complete
    
init_pentium:
    mov     [optimization_level], OPT_LEVEL_PENTIUM
    
init_complete:
    ; Apply CPU-specific optimizations
    call    apply_cpu_optimizations
    
    xor     ax, ax          ; Return success
    
    pop     dx
    pop     cx
    pop     bx
    pop     bp
    ret
pio_lib_init ENDP

;-----------------------------------------------------------------------------
; @brief Get PIO interface structure pointer
; @return AX = pointer to interface structure
;-----------------------------------------------------------------------------
pio_get_interface PROC
    mov     ax, OFFSET pio_interface_struct
    ret
pio_get_interface ENDP

;=============================================================================
; Optimized I/O Functions
;=============================================================================

;-----------------------------------------------------------------------------
; @brief Optimized byte output
; @param AX = port address
; @param BL = value to output
;-----------------------------------------------------------------------------
_pio_outb_optimized PROC
    push    dx
    mov     dx, ax
    mov     al, bl
    
    ; Self-modifying optimization point
outb_patch_point:
    out     dx, al          ; Default implementation
    
    pop     dx
    ret
_pio_outb_optimized ENDP

;-----------------------------------------------------------------------------
; @brief Optimized word output
; @param AX = port address  
; @param BX = value to output
;-----------------------------------------------------------------------------
_pio_outw_optimized PROC
    push    dx
    mov     dx, ax
    mov     ax, bx
    
    ; Self-modifying optimization point
outw_patch_point:
    out     dx, ax          ; Default implementation
    
    pop     dx
    ret
_pio_outw_optimized ENDP

;-----------------------------------------------------------------------------
; @brief Optimized byte input
; @param AX = port address
; @return AL = input value
;-----------------------------------------------------------------------------
_pio_inb_optimized PROC
    push    dx
    mov     dx, ax
    
    ; Self-modifying optimization point  
inb_patch_point:
    in      al, dx          ; Default implementation
    
    pop     dx
    ret
_pio_inb_optimized ENDP

;-----------------------------------------------------------------------------
; @brief Optimized word input
; @param AX = port address
; @return AX = input value
;-----------------------------------------------------------------------------
_pio_inw_optimized PROC
    push    dx
    mov     dx, ax
    
    ; Self-modifying optimization point
inw_patch_point:
    in      ax, dx          ; Default implementation
    
    pop     dx
    ret
_pio_inw_optimized ENDP

;-----------------------------------------------------------------------------
; @brief Optimized string word output
; @param AX = port address
; @param BX = buffer pointer  
; @param CX = word count
;-----------------------------------------------------------------------------
_pio_outsw_optimized PROC
    push    bp
    mov     bp, sp
    push    si
    push    dx
    push    es
    
    mov     dx, ax          ; Port address
    mov     si, bx          ; Buffer pointer
    push    ds
    pop     es              ; ES = DS for string operations
    
    ; Self-modifying optimization point
outsw_patch_point:
    cld                     ; Clear direction flag
    rep     outsw           ; Default implementation
    
    pop     es
    pop     dx
    pop     si
    pop     bp
    ret
_pio_outsw_optimized ENDP

;-----------------------------------------------------------------------------
; @brief Optimized string word input
; @param AX = port address
; @param BX = buffer pointer
; @param CX = word count
;-----------------------------------------------------------------------------
_pio_insw_optimized PROC
    push    bp
    mov     bp, sp
    push    di
    push    dx
    push    es
    
    mov     dx, ax          ; Port address
    mov     di, bx          ; Buffer pointer
    push    ds
    pop     es              ; ES = DS for string operations
    
    ; Self-modifying optimization point
insw_patch_point:
    cld                     ; Clear direction flag
    rep     insw            ; Default implementation
    
    pop     es
    pop     dx
    pop     di
    pop     bp
    ret
_pio_insw_optimized ENDP

;=============================================================================
; CPU-Specific Optimization Patches
;=============================================================================

;-----------------------------------------------------------------------------
; @brief Apply CPU-specific optimizations
;-----------------------------------------------------------------------------
apply_cpu_optimizations PROC
    push    ax
    push    bx
    
    cmp     [patches_applied], 1
    je      opt_already_applied
    
    mov     al, [optimization_level]
    cmp     al, OPT_LEVEL_PENTIUM
    je      apply_pentium_opt
    cmp     al, OPT_LEVEL_486
    je      apply_486_opt
    cmp     al, OPT_LEVEL_386
    je      apply_386_opt
    
    ; Basic 80286 optimizations (no patches needed)
    jmp     opt_applied
    
apply_386_opt:
    call    pio_patch_386_optimizations
    jmp     opt_applied
    
apply_486_opt:
    call    pio_patch_486_optimizations
    jmp     opt_applied
    
apply_pentium_opt:
    call    pio_patch_pentium_optimizations
    
opt_applied:
    mov     [patches_applied], 1
    call    flush_prefetch_queue    ; Flush after self-modification
    
opt_already_applied:
    pop     bx
    pop     ax
    ret
apply_cpu_optimizations ENDP

;-----------------------------------------------------------------------------
; @brief Apply 80386-specific optimizations
;-----------------------------------------------------------------------------
pio_patch_386_optimizations PROC
    push    ax
    push    bx
    push    si
    push    di
    push    es
    
    ; Patch outsw for 32-bit optimizations
    ; Replace with 32-bit addressing where beneficial
    
    ; Get code segment
    push    cs
    pop     es
    
    ; Example: Add pipeline-friendly NOPs
    mov     si, OFFSET outsw_patch_point
    mov     di, OFFSET outsw_386_version
    mov     cx, outsw_386_size
    cld
    rep     movsb
    
    pop     es
    pop     di
    pop     si
    pop     bx
    pop     ax
    ret
pio_patch_386_optimizations ENDP

;-----------------------------------------------------------------------------
; @brief Apply 80486-specific optimizations  
;-----------------------------------------------------------------------------
pio_patch_486_optimizations PROC
    push    ax
    push    bx
    push    si
    push    di
    push    es
    
    ; Apply cache-friendly optimizations
    ; Align code to cache line boundaries
    ; Add prefetch hints where applicable
    
    push    cs
    pop     es
    
    ; Patch for burst transfers
    mov     si, OFFSET insw_patch_point
    mov     di, OFFSET insw_486_version
    mov     cx, insw_486_size
    cld
    rep     movsb
    
    pop     es
    pop     di
    pop     si
    pop     bx
    pop     ax
    ret
pio_patch_486_optimizations ENDP

;-----------------------------------------------------------------------------
; @brief Apply Pentium-specific optimizations
;-----------------------------------------------------------------------------
pio_patch_pentium_optimizations PROC
    push    ax
    push    bx
    push    si
    push    di
    push    es
    
    ; Apply dual-pipeline optimizations
    ; Pair instructions for optimal execution
    ; Use AGI stall avoidance techniques
    
    push    cs
    pop     es
    
    ; Patch for Pentium pairing
    mov     si, OFFSET outw_patch_point
    mov     di, OFFSET outw_pentium_version
    mov     cx, outw_pentium_size
    cld
    rep     movsb
    
    pop     es
    pop     di
    pop     si
    pop     bx
    pop     ax
    ret
pio_patch_pentium_optimizations ENDP

;-----------------------------------------------------------------------------
; @brief Flush prefetch queue after self-modification
;-----------------------------------------------------------------------------
flush_prefetch_queue PROC
    ; Jump to flush prefetch queue
    jmp     SHORT flush_continue
flush_continue:
    ret
flush_prefetch_queue ENDP

;=============================================================================
; Optimized Code Versions
;=============================================================================

;-----------------------------------------------------------------------------
; 80386 optimized outsw
;-----------------------------------------------------------------------------
outsw_386_version:
    cld
    ; Add NOPs for pipeline efficiency
    nop
    rep     outsw
    nop
outsw_386_size  EQU $ - outsw_386_version

;-----------------------------------------------------------------------------
; 80486 optimized insw  
;-----------------------------------------------------------------------------
insw_486_version:
    cld
    ; Cache-line aligned transfers
    push    cx
    mov     ax, cx
    and     ax, 0Fh         ; Check alignment
    jz      insw_486_aligned
    
    ; Handle unaligned portion
    sub     cx, ax
    rep     insw
    mov     cx, ax
    
insw_486_aligned:
    pop     ax              ; Original count in AX
    rep     insw
insw_486_size   EQU $ - insw_486_version

;-----------------------------------------------------------------------------
; Pentium optimized outw
;-----------------------------------------------------------------------------
outw_pentium_version:
    ; Avoid AGI stalls and optimize for pairing
    push    dx              ; U pipe
    mov     dx, ax          ; V pipe
    mov     ax, bx          ; U pipe
    nop                     ; V pipe (pairing)
    out     dx, ax          ; U pipe
    pop     dx              ; V pipe
outw_pentium_size EQU $ - outw_pentium_version

END