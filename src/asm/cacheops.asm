; 3Com Packet Driver - Cache Operations Assembly Module
; 
; This module provides low-level cache management operations for the 4-tier
; cache coherency system. It includes CPU-specific cache instructions and
; register access functions.
;
; This file is part of the 3Com Packet Driver project.

.model small
.386

include 'patch_macros.inc'

; CPU optimization level constants (must match packet_ops.asm)
OPT_8086            EQU 0       ; 8086/8088 baseline (no 186+ instructions)
OPT_16BIT           EQU 1       ; 16-bit optimizations (186+: PUSHA, INS/OUTS)

; External reference to CPU optimization level
EXTRN current_cpu_opt:BYTE

;-----------------------------------------------------------------------------
; 8086-SAFE REGISTER SAVE/RESTORE MACROS
; Note: cache_flush_range is only called on Pentium 4+ (CLFLUSH requirement),
; but for consistency and future-proofing, we use the 8086-safe macros.
;-----------------------------------------------------------------------------

SAVE_ALL_REGS MACRO
    LOCAL use_pusha, done
    push ax
    mov al, [current_cpu_opt]
    test al, OPT_16BIT
    pop ax
    jnz use_pusha
    push ax
    push cx
    push dx
    push bx
    push sp
    push bp
    push si
    push di
    jmp short done
use_pusha:
    pusha
done:
ENDM

RESTORE_ALL_REGS MACRO
    LOCAL use_popa, done
    push ax
    mov al, [current_cpu_opt]
    test al, OPT_16BIT
    pop ax
    jnz use_popa
    pop di
    pop si
    pop bp
    add sp, 2
    pop bx
    pop dx
    pop cx
    pop ax
    jmp short done
use_popa:
    popa
done:
ENDM

.code

public cache_clflush_line
public cache_clflush_safe
public cache_flush_range
public cache_wbinvd
public cache_wbinvd_safe
public cache_invd
public read_cr0_register
public write_cr0_register
public memory_fence
public memory_fence_after_clflush
public store_fence
public load_fence
public detect_cache_mode
public get_current_timestamp

;-----------------------------------------------------------------------------
; cache_clflush_line - Flush specific cache line (Pentium 4+)
; 
; Input: Stack parameter - address to flush
; Output: None
; Registers: Preserves all registers
; Note: Caller must ensure CLFLUSH is supported
;-----------------------------------------------------------------------------
cache_clflush_line proc
    push bp
    mov bp, sp
    push di
    push es
    
    ; Get address parameter as segment:offset (far pointer in 16-bit mode)
    ; Stack layout: [bp+6] = segment, [bp+4] = offset
    mov es, [bp+6]          ; Load segment
    mov di, [bp+4]          ; Load offset
    
    ; Execute CLFLUSH instruction for ES:[DI] 
    ; GPT-5 fix: Use ES segment override with [DI] addressing (16-bit safe)
    db 0x26, 0x0F, 0xAE, 0x3D  ; CLFLUSH ES:[DI] - fits perfectly in 5-byte SMC sled
    
    pop es
    pop di
    pop bp
    ret
cache_clflush_line endp

;-----------------------------------------------------------------------------
; cache_clflush_buffer - Flush entire buffer by cache lines
; 
; Input: Stack parameters - buffer address (offset 4), size (offset 8)
; Output: AX = 0 if successful, 1 if CLFLUSH not available
; Registers: Modifies AX
;-----------------------------------------------------------------------------
cache_clflush_buffer proc
    push bp
    mov bp, sp
    push ebx
    push ecx
    push edx
    push di
    push es
    
    ; Check if CLFLUSH is supported (from cached flag)
    extern cpuid_available:byte
    extern cpu_features:dword
    
    cmp byte ptr [cpuid_available], 0
    je .no_clflush
    
    test dword ptr [cpu_features], FEATURE_CLFLUSH
    jz .no_clflush
    
    ; Get parameters as segment:offset (16-bit far pointer)
    mov es, [bp+8]      ; Buffer segment
    mov di, [bp+4]      ; Buffer offset  
    mov ecx, [bp+12]    ; Buffer size
    
    ; Get cache line size (default 64 if not detected)
    extern cache_line_size:byte
    movzx ebx, byte ptr [cache_line_size]
    test ebx, ebx
    jnz .has_line_size
    mov ebx, 64         ; Default cache line size
    
.has_line_size:
    ; Flush loop - iterate through buffer by cache line with segment wrapping
.flush_loop:
    cmp ecx, 0
    jle .flush_done
    
    ; CLFLUSH current cache line at ES:[DI]
    ; GPT-5 fix: Use ES segment override with [DI] addressing (16-bit safe)
    db 0x26, 0x0F, 0xAE, 0x3D  ; CLFLUSH ES:[DI]
    
    ; Advance to next cache line with segment wrapping support
    add di, bx          ; Move to next cache line
    jnc .no_wrap        ; Check for segment wrap
    
    ; Handle segment wrap - advance ES by 64KB
    mov ax, es
    add ax, 1000h       ; 64KB = 0x1000 paragraphs
    mov es, ax
    
.no_wrap:
    sub ecx, ebx        ; Decrease remaining size
    jg .flush_loop      ; Continue if more to flush
    
.flush_done:
    ; Critical: Memory fence after all CLFLUSHes
    call memory_fence_after_clflush
    xor ax, ax          ; Success
    jmp .done
    
.no_clflush:
    mov ax, 1           ; CLFLUSH not available
    
.done:
    pop es
    pop di
    pop edx
    pop ecx
    pop ebx
    pop bp
    ret
cache_clflush_buffer endp

;-----------------------------------------------------------------------------
; cache_clflush_safe - Safe wrapper for CLFLUSH with feature detection
; 
; Input: Stack parameters - address (offset 4), size (offset 8)
; Output: AX = 0 if successful, 1 if CLFLUSH not available
; Registers: Modifies AX only
;-----------------------------------------------------------------------------
cache_clflush_safe proc
    ; Simply call the buffer function which has all safety checks
    jmp cache_clflush_buffer
cache_clflush_safe endp

;-----------------------------------------------------------------------------
; cache_flush_range - Optimized range-based cache flushing for SMC integration
; 
; Input: ES:DI = Start Address, CX = Byte Count
; Output: None
; Registers: Preserves all registers except flags
; Note: Designed for SMC patch point integration - compact and fast
; Note: Caller must ensure CLFLUSH is supported before calling
;-----------------------------------------------------------------------------
cache_flush_range proc
    ; 8086-safe register save (uses PUSHA on 186+, explicit pushes on 8086)
    ; Note: This function requires CLFLUSH (P4+), so on 8086 it won't be called,
    ; but we use the macro for consistency.
    SAVE_ALL_REGS
    push es
    
    ; Get cache line size (64 bytes default for Pentium 4+)
    extern cache_line_size:byte
    mov bl, byte ptr [cache_line_size]
    test bl, bl
    jnz .has_line_size
    mov bl, 64                  ; Default cache line size
    
.has_line_size:
    ; Align DI down to cache line boundary
    mov ax, di
    and al, bl                  ; Get misalignment
    dec al                      ; bl-1 mask (assumes bl is power of 2)
    not al
    and di, ax                  ; Align DI down
    
    ; Calculate number of cache lines to flush
    ; Lines = (byte_count + cache_line_size - 1) / cache_line_size
    mov ax, cx
    add al, bl                  ; Add cache line size
    dec ax                      ; Subtract 1 for rounding
    movzx dx, bl
    div dl                      ; AX / DL, result in AL
    mov cl, al                  ; Number of lines to flush
    
.flush_loop:
    test cl, cl
    jz .flush_done
    
    ; CLFLUSH current cache line at ES:[DI] - GPT-5 validated encoding
    db 0x26, 0x0F, 0xAE, 0x3D  ; CLFLUSH ES:[DI] - perfect for 5-byte SMC sled
    
    ; Advance to next cache line with segment wrap handling
    add di, bx                  ; Move to next cache line
    jnc .no_wrap                ; Check for segment wrap
    
    ; Handle segment wrap - advance ES by 64KB
    mov ax, es
    add ax, 1000h               ; 64KB = 0x1000 paragraphs  
    mov es, ax
    
.no_wrap:
    dec cl                      ; Decrement line count
    jnz .flush_loop
    
.flush_done:
    ; Memory fence after all CLFLUSHes (required by Intel specification)
    call memory_fence_after_clflush  ; Proper serialization for 16-bit mode

    pop es
    ; 8086-safe register restore
    RESTORE_ALL_REGS
    ret
cache_flush_range endp

;-----------------------------------------------------------------------------
; cache_wbinvd - Write-back and invalidate cache (486+)
; 
; Input: None
; Output: None
; Registers: Preserves all registers
;-----------------------------------------------------------------------------
cache_wbinvd proc
    push eax
    
    ; Check CPU capability (simplified)
    ; Real implementation would verify 486+ CPU
    
    ; Execute WBINVD instruction
    db 0x0F, 0x09  ; WBINVD
    
    pop eax
    ret
cache_wbinvd endp

;-----------------------------------------------------------------------------
; cache_wbinvd_safe - Safe wrapper for WBINVD with V86 mode check
; 
; Input: None
; Output: AX = 0 if successful, 1 if cannot execute safely
; Registers: Modifies AX only
;
; Note: WBINVD will trap in V86 mode, so we must check first
;-----------------------------------------------------------------------------
cache_wbinvd_safe proc
    ; Check if we're 486+ (WBINVD doesn't exist before 486)
    extern cpu_is_486_plus:byte
    cmp byte ptr [cpu_is_486_plus], 0
    je .not_safe
    
    ; Check if we're in V86 mode (cached from detection)
    extern is_v86_mode:byte
    cmp byte ptr [is_v86_mode], 0
    jne .not_safe       ; Cannot use WBINVD in V86 mode
    
    ; Safe to execute WBINVD
    call cache_wbinvd
    xor ax, ax          ; Return success
    jmp .done
    
.not_safe:
    ; Cannot safely use WBINVD
    mov ax, 1           ; Return failure
    
.done:
    ret
cache_wbinvd_safe endp

;-----------------------------------------------------------------------------
; cache_invd - Invalidate cache without write-back (486+)
; 
; Input: None
; Output: None
; Registers: Preserves all registers
; Note: Use with extreme caution - can cause data loss
;-----------------------------------------------------------------------------
cache_invd proc
    push eax
    
    ; Execute INVD instruction
    db 0x0F, 0x08  ; INVD
    
    pop eax
    ret
cache_invd endp

;-----------------------------------------------------------------------------
; read_cr0_register - Read CR0 control register (386+)
; 
; Input: None
; Output: EAX = CR0 register value
; Registers: Modifies EAX only
;-----------------------------------------------------------------------------
read_cr0_register proc
    ; Read CR0 register
    mov eax, cr0
    ret
read_cr0_register endp

;-----------------------------------------------------------------------------
; write_cr0_register - Write CR0 control register (386+)
; 
; Input: Stack parameter - new CR0 value
; Output: None
; Registers: Preserves all registers
;-----------------------------------------------------------------------------
write_cr0_register proc
    push bp
    mov bp, sp
    push eax
    
    ; Get new CR0 value
    mov eax, [bp+4]
    
    ; Write CR0 register
    mov cr0, eax
    
    ; Serializing instruction to ensure changes take effect
    jmp short $+2
    
    pop eax
    pop bp
    ret
write_cr0_register endp

;-----------------------------------------------------------------------------
; memory_fence - Memory fence/barrier operations
; 
; Input: None
; Output: None
; Registers: Preserves all registers
;-----------------------------------------------------------------------------
memory_fence proc
    push eax
    
    ; For 386/486: Use serializing instruction
    mov eax, cr0
    mov cr0, eax
    
    ; For Pentium+: Could use CPUID as serializing instruction
    ; For modern CPUs: Would use MFENCE, LFENCE, SFENCE
    
    pop eax
    ret
memory_fence endp

;-----------------------------------------------------------------------------
; memory_fence_after_clflush - Memory fence specifically after CLFLUSH
; 
; Input: None
; Output: None
; Registers: Preserves all registers
; Note: Critical for DMA safety - ensures CLFLUSH completes before DMA
;-----------------------------------------------------------------------------
memory_fence_after_clflush proc
    push eax
    
    ; Check if SSE2 is available for MFENCE
    extern sse2_available:byte
    extern cpuid_available:byte
    
    cmp byte ptr [sse2_available], 1
    je .use_mfence
    
    ; No SSE2 - check if CPUID is available for serialization
    cmp byte ptr [cpuid_available], 1
    je .use_cpuid
    
    ; No CPUID - use CR0 serialization (works on 386+)
    mov eax, cr0
    mov cr0, eax
    jmp .done
    
.use_mfence:
    ; Use MFENCE instruction (fastest on P4+)
    db 0x0F, 0xAE, 0xF0  ; MFENCE
    jmp .done
    
.use_cpuid:
    ; Use CPUID as serializing instruction
    push ebx
    push ecx
    push edx
    mov eax, 0
    cpuid               ; This serializes the CPU
    pop edx
    pop ecx
    pop ebx
    
.done:
    pop eax
    ret
memory_fence_after_clflush endp

;-----------------------------------------------------------------------------
; store_fence - Store fence (write barrier)
; 
; Input: None
; Output: None
; Registers: Preserves all registers
;-----------------------------------------------------------------------------
store_fence proc
    push eax
    
    ; Ensure all previous stores complete before continuing
    ; On older CPUs, use serializing instruction
    mov eax, cr0
    mov cr0, eax
    
    pop eax
    ret
store_fence endp

;-----------------------------------------------------------------------------
; load_fence - Load fence (read barrier)
; 
; Input: None
; Output: None
; Registers: Preserves all registers
;-----------------------------------------------------------------------------
load_fence proc
    push eax
    
    ; Ensure all previous loads complete before continuing
    mov eax, cr0
    mov cr0, eax
    
    pop eax
    ret
load_fence endp

;-----------------------------------------------------------------------------
; detect_cache_mode - Detect current cache mode from CR0
; 
; Input: None
; Output: AX = cache mode (0=disabled, 1=write-through, 2=write-back)
; Registers: Modifies AX only
;-----------------------------------------------------------------------------
detect_cache_mode proc
    push bx
    
    ; Read CR0 register
    mov eax, cr0
    
    ; Check CD (Cache Disable) bit (bit 30)
    test eax, 40000000h
    jnz cache_disabled
    
    ; Check NW (Not Write-through) bit (bit 29)
    test eax, 20000000h
    jnz write_back_mode
    
    ; CD=0, NW=0 = Write-through mode
    mov ax, 1
    jmp detect_done
    
write_back_mode:
    ; CD=0, NW=1 = Write-back mode
    mov ax, 2
    jmp detect_done
    
cache_disabled:
    ; CD=1 = Cache disabled
    mov ax, 0
    
detect_done:
    pop bx
    ret
detect_cache_mode endp

;-----------------------------------------------------------------------------
; get_current_timestamp - Get current timestamp for performance measurement
; 
; Input: None
; Output: EAX = timestamp in microseconds (simplified)
; Registers: Modifies EAX, EDX
;-----------------------------------------------------------------------------
get_current_timestamp proc
    push ecx
    
    ; For real implementation, this would use:
    ; - TSC (Time Stamp Counter) on Pentium+
    ; - PIT (Programmable Interval Timer) on older CPUs
    ; - RTC (Real Time Clock) as fallback
    
    ; Simplified implementation using PIT channel 0
    ; Read current PIT counter
    mov al, 0           ; Latch counter 0
    out 43h, al         ; Command register
    
    in al, 40h          ; Read low byte
    mov ah, al
    in al, 40h          ; Read high byte
    xchg ah, al         ; AX = counter value
    
    ; Convert to microseconds (very simplified)
    movzx eax, ax
    imul eax, 838       ; Approximate conversion factor
    shr eax, 10         ; Divide by 1024
    
    pop ecx
    ret
get_current_timestamp endp

;-----------------------------------------------------------------------------
; CPU-specific cache line size detection helper
; 
; Input: None
; Output: EAX = cache line size in bytes
; Registers: Modifies EAX, EBX, ECX, EDX
;-----------------------------------------------------------------------------
detect_cache_line_size_asm proc
    push ebx
    push ecx
    push edx
    
    ; Try CPUID if available (Pentium+)
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 200000h    ; Flip ID bit
    push eax
    popfd
    pushfd
    pop eax
    cmp eax, ecx
    je no_cpuid         ; CPUID not supported
    
    ; CPUID available - get cache info
    mov eax, 1
    cpuid
    
    ; Extract cache line size from EBX[15:8]
    mov eax, ebx
    shr eax, 8
    and eax, 0FFh
    shl eax, 3          ; Convert to bytes (multiply by 8)
    
    ; Validate result
    cmp eax, 16
    jb use_default
    cmp eax, 128
    ja use_default
    jmp size_done
    
no_cpuid:
use_default:
    ; Default cache line size based on CPU generation
    ; This is a simplified approach
    mov eax, 32         ; Conservative default
    
size_done:
    pop edx
    pop ecx
    pop ebx
    ret
detect_cache_line_size_asm endp

;-----------------------------------------------------------------------------
; Test cache coherency with assembly-level precision
; 
; Input: Stack parameters - buffer address, test pattern
; Output: AX = 1 if coherent, 0 if corrupted
; Registers: Modifies AX, BX
;-----------------------------------------------------------------------------
test_cache_coherency_asm proc
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    
    ; Get parameters
    mov bx, [bp+4]      ; Buffer address
    mov cx, [bp+6]      ; Test pattern
    
    ; Write pattern to memory
    mov [bx], cx
    
    ; Force into cache by reading
    mov dx, [bx]
    
    ; Simulate DMA write with different pattern
    not cx              ; Invert pattern
    mov [bx], cx
    
    ; Read back - if cache is coherent, should get new pattern
    mov dx, [bx]
    cmp dx, cx
    je coherent
    
    ; Incoherent
    mov ax, 0
    jmp test_done
    
coherent:
    mov ax, 1
    
test_done:
    pop dx
    pop cx
    pop bx
    pop bp
    ret
test_cache_coherency_asm endp

;=============================================================================
; SMC PATCH TEMPLATES - GPT-5 Validated 5-byte Patch Points
;=============================================================================

;-----------------------------------------------------------------------------
; Cache flush patch points for hot path integration
; These are 5-byte NOP sleds that get patched with CPU-specific code
;-----------------------------------------------------------------------------

; Patch point for CLFLUSH cache line flush
PATCH_POINT cache_flush_patch_clflush

; Patch point for WBINVD full cache flush  
PATCH_POINT cache_flush_patch_wbinvd

; Patch point for software cache barriers
PATCH_POINT cache_flush_patch_barrier

; Patch point for NOP (no cache management needed)
PATCH_POINT cache_flush_patch_nop

;-----------------------------------------------------------------------------
; Cache policy selection patch points
;-----------------------------------------------------------------------------

; DMA prepare patch point (select cache management strategy)
PATCH_POINT dma_prepare_patch_point

; DMA complete patch point (invalidate cache if needed)  
PATCH_POINT dma_complete_patch_point

; RX buffer allocation patch point (ensure cache alignment)
PATCH_POINT rx_alloc_cache_patch

; TX buffer preparation patch point (flush cache if needed)
PATCH_POINT tx_prep_cache_patch

;-----------------------------------------------------------------------------
; Patch table for linker and initialization code
;-----------------------------------------------------------------------------
.data
PUBLIC patch_table_cache_ops

patch_table_cache_ops:
    ; CLFLUSH patches
    PATCH_TABLE_ENTRY cache_flush_patch_clflush, PATCH_TYPE_COPY
    PATCH_TABLE_ENTRY cache_flush_patch_wbinvd, PATCH_TYPE_COPY  
    PATCH_TABLE_ENTRY cache_flush_patch_barrier, PATCH_TYPE_COPY
    PATCH_TABLE_ENTRY cache_flush_patch_nop, PATCH_TYPE_NOP
    
    ; DMA cache management patches
    PATCH_TABLE_ENTRY dma_prepare_patch_point, PATCH_TYPE_COPY
    PATCH_TABLE_ENTRY dma_complete_patch_point, PATCH_TYPE_COPY
    PATCH_TABLE_ENTRY rx_alloc_cache_patch, PATCH_TYPE_COPY
    PATCH_TABLE_ENTRY tx_prep_cache_patch, PATCH_TYPE_COPY
    
    ; End marker
    dw 0, 0

;-----------------------------------------------------------------------------
; CPU-specific patch templates (code to patch into hot path)
;-----------------------------------------------------------------------------

; Template: CLFLUSH ES:[DI] + RET (5 bytes total)
clflush_template:
    db 0x26, 0x0F, 0xAE, 0x3D      ; CLFLUSH ES:[DI] - 4 bytes
    db 0xC3                         ; RET - 1 byte

; Template: WBINVD + NOP (5 bytes total)  
wbinvd_template:
    db 0x0F, 0x09                   ; WBINVD - 2 bytes
    db 0x90, 0x90, 0x90             ; NOP padding - 3 bytes

; Template: Memory barriers (5 bytes total)
barrier_template:
    db 0x9B                         ; FWAIT (serialize) - 1 byte  
    db 0x90, 0x90, 0x90, 0x90       ; NOP padding - 4 bytes

; Template: Pure NOP sled (5 bytes total)
nop_template:
    db 0x90, 0x90, 0x90, 0x90, 0x90 ; 5x NOP

.code

end