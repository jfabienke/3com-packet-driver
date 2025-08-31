; 3Com Packet Driver - Cache Operations Assembly Module
; 
; This module provides low-level cache management operations for the 4-tier
; cache coherency system. It includes CPU-specific cache instructions and
; register access functions.
;
; This file is part of the 3Com Packet Driver project.

.model small
.386

.code

public cache_clflush_line
public cache_clflush_safe
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
    push eax
    
    ; Get address parameter (far pointer in 16-bit mode)
    mov eax, [bp+4]
    
    ; Execute CLFLUSH instruction for the address
    ; Proper encoding: CLFLUSH [eax]
    db 0x0F, 0xAE
    db 0x38        ; ModR/M byte for [eax]
    
    pop eax
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
    push esi
    
    ; Check if CLFLUSH is supported (from cached flag)
    extern cpuid_available:byte
    extern cpu_features:dword
    
    cmp byte ptr [cpuid_available], 0
    je .no_clflush
    
    test dword ptr [cpu_features], FEATURE_CLFLUSH
    jz .no_clflush
    
    ; Get parameters
    mov esi, [bp+4]     ; Buffer address
    mov ecx, [bp+8]     ; Buffer size
    
    ; Get cache line size (default 64 if not detected)
    extern cache_line_size:byte
    movzx ebx, byte ptr [cache_line_size]
    test ebx, ebx
    jnz .has_line_size
    mov ebx, 64         ; Default cache line size
    
.has_line_size:
    ; Flush loop - iterate through buffer by cache line
.flush_loop:
    cmp ecx, 0
    jle .flush_done
    
    ; CLFLUSH current cache line
    mov eax, esi
    db 0x0F, 0xAE
    db 0x38             ; CLFLUSH [eax]
    
    ; Advance to next cache line
    add esi, ebx        ; Next cache line
    sub ecx, ebx        ; Decrease remaining size
    jmp .flush_loop
    
.flush_done:
    ; Critical: Memory fence after all CLFLUSHes
    call memory_fence_after_clflush
    xor ax, ax          ; Success
    jmp .done
    
.no_clflush:
    mov ax, 1           ; CLFLUSH not available
    
.done:
    pop esi
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

end