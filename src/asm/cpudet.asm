; @file cpu_detect.asm
; @brief CPU detection routines
;
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
;
; This file is part of the 3Com Packet Driver project.
;

.MODEL SMALL
.386

; CPU Type Constants
; Simplified CPU type constants - CPUID-capable CPUs use family/model
CPU_8086            EQU 0       ; 8086/8088 processor (no CPUID)
CPU_80186           EQU 1       ; 80186/80188 processor (ENTER/LEAVE, PUSHA/POPA)
CPU_80286           EQU 2       ; 80286 processor (no CPUID)
CPU_80386           EQU 3       ; 80386 processor (no CPUID)
CPU_80486           EQU 4       ; 80486 processor (may have CPUID)
CPU_CPUID_CAPABLE   EQU 5       ; Has CPUID - use family/model for details

; CPU Feature Flags
FEATURE_NONE        EQU 0000h   ; No special features
FEATURE_PUSHA       EQU 0001h   ; PUSHA/POPA instructions (286+)
FEATURE_32BIT       EQU 0002h   ; 32-bit operations (386+)
FEATURE_CPUID       EQU 0004h   ; CPUID instruction (486+)
FEATURE_FPU         EQU 0008h   ; Floating point unit present

; 386-Specific Feature Flags
FEATURE_386_PAGING  EQU 0010h   ; 386 paging support (CR0 access)
FEATURE_386_V86     EQU 0020h   ; Virtual 8086 mode capability
FEATURE_386_AC      EQU 0040h   ; Alignment Check flag capability

; 486-Specific Feature Flags  
FEATURE_486_CACHE   EQU 0080h   ; 486 internal cache present
FEATURE_486_WRITEBACK EQU 0100h ; Write-back cache mode
FEATURE_BSWAP       EQU 0200h   ; BSWAP instruction support
FEATURE_CMPXCHG     EQU 0400h   ; CMPXCHG instruction support
FEATURE_INVLPG      EQU 0800h   ; INVLPG instruction support

; Extended CPUID Feature Flags (from CPUID leaf 1, EDX register)
FEATURE_TSC         EQU 1000h   ; Time Stamp Counter
FEATURE_MSR         EQU 2000h   ; Model Specific Registers
FEATURE_CX8         EQU 4000h   ; CMPXCHG8B instruction
FEATURE_MMX         EQU 8000h   ; MMX technology
FEATURE_SSE         EQU 10000h  ; SSE (Streaming SIMD Extensions)
FEATURE_SSE2        EQU 20000h  ; SSE2 extensions
FEATURE_HT          EQU 40000h  ; Hyper-Threading Technology
FEATURE_SYSCALL     EQU 80000h  ; SYSCALL/SYSRET instructions (AMD)

; DMA Safety Critical Features (Tier 1)
FEATURE_CLFLUSH     EQU 100000h ; CLFLUSH instruction (P4+)
FEATURE_WBINVD_SAFE EQU 200000h ; WBINVD safe to use (not V86 mode)

; V86 Mode Detection
FEATURE_V86_MODE    EQU 400000h ; Running in Virtual 8086 mode

; CPU Vendor IDs (for non-Intel x86 clones)
VENDOR_INTEL        EQU 0       ; Intel (default)
VENDOR_AMD          EQU 1       ; AMD
VENDOR_CYRIX        EQU 2       ; Cyrix/IBM/TI
VENDOR_NEXGEN       EQU 3       ; NexGen
VENDOR_UMC          EQU 4       ; UMC
VENDOR_TRANSMETA    EQU 5       ; Transmeta
VENDOR_RISE         EQU 6       ; Rise
VENDOR_VIA          EQU 7       ; VIA/Centaur/IDT
VENDOR_UNKNOWN      EQU 0FFh    ; Unknown vendor

; Cache sizes (in KB)
CACHE_SIZE_UNKNOWN  EQU 0       ; Unknown cache size
MAX_CACHE_ENTRIES   EQU 16      ; Maximum cache descriptor entries

; Success/Error codes
CPU_SUCCESS         EQU 0       ; CPU detection successful
CPU_ERROR_UNSUPPORTED EQU 1     ; CPU not supported (< 286)

; Data segment
_DATA SEGMENT
        ASSUME  DS:_DATA

; CPU detection results
detected_cpu_type   db CPU_8086     ; Detected CPU type
cpu_features        dd FEATURE_NONE ; Detected CPU features (expanded to 32-bit)
cpu_vendor_id       db 13 dup(0)    ; CPU vendor string (12 chars + null)
cpu_step_id         db 0            ; CPU stepping ID
cpu_family_id       db 0            ; CPU family
cpu_model_id        db 0            ; CPU model
cpu_signature       dd 0            ; Full CPUID signature
cpu_brand_string    db 49 dup(0)    ; CPU brand string (48 chars + null)

; Cache information structures
cache_l1_data_size  dw CACHE_SIZE_UNKNOWN ; L1 data cache size (KB)
cache_l1_code_size  dw CACHE_SIZE_UNKNOWN ; L1 instruction cache size (KB) 
cache_l2_size       dw CACHE_SIZE_UNKNOWN ; L2 cache size (KB)
cache_l1_data_assoc db 0            ; L1 data cache associativity
cache_l1_code_assoc db 0            ; L1 instruction cache associativity
cache_l2_assoc      db 0            ; L2 cache associativity
cache_line_size     db 0            ; Cache line size
cache_descriptors   db MAX_CACHE_ENTRIES dup(0) ; Cache descriptor table

; CPUID and V86 mode info
cpuid_max_level     dd 0            ; Maximum CPUID function supported
is_v86_mode         db 0            ; 1 if running in V86 mode

; Safety flags for instruction gating
cpuid_available     db 0            ; 1 if CPUID instruction is available
cpu_is_386_plus     db 0            ; 1 if CPU is 386 or higher
cpu_is_486_plus     db 0            ; 1 if CPU is 486 or higher
sse2_available      db 0            ; 1 if SSE2 is available (for MFENCE)
extended_family     db 0            ; Extended family value (family 15+)

; Vendor detection results
detected_vendor     db VENDOR_UNKNOWN ; Detected CPU vendor
cyrix_dir0_present  db 0            ; 1 if Cyrix DIR0 register detected
nexgen_cpuid_works  db 0            ; 1 if NexGen CPUID works despite no ID flag
cyrix_dir0_value    db 0            ; Original DIR0 value (for restoration)

; CPU speed detection variables
detected_cpu_mhz    dw 0            ; Detected CPU speed in MHz
pit_start_count     dw 0            ; PIT start counter value
pit_end_count       dw 0            ; PIT end counter value
rdtsc_start_low     dd 0            ; RDTSC start value (low 32 bits)
rdtsc_start_high    dd 0            ; RDTSC start value (high 32 bits)

; Multi-trial speed detection for statistical robustness
speed_trials        dw 5 dup(0)     ; Array for 5 speed measurements
speed_confidence    db 0            ; Confidence level (0-100%)
loop_overhead_ticks dw 0            ; Overhead from empty loop calibration
port_61h_state      db 0            ; Saved port 61h state for PIT channel 2

; TLB information
tlb_data_entries    dw 0            ; Data TLB entries
tlb_code_entries    dw 0            ; Code TLB entries
tlb_page_size       dw 0            ; TLB page size

; Extended features (from CPUID leaf 0x80000001)
extended_features   dd 0            ; Extended feature flags

; TSC characteristics
invariant_tsc       db 0            ; 1 if TSC is invariant (doesn't vary with power states)
has_rdtscp          db 0            ; 1 if RDTSCP instruction is available

; Virtualization detection
is_hypervisor       db 0            ; 1 if running under hypervisor/VM
max_extended_leaf   dd 0            ; Maximum extended CPUID leaf supported

_DATA ENDS

; Code segment
_TEXT SEGMENT
        ASSUME  CS:_TEXT, DS:_DATA

; Public function exports
PUBLIC cpu_detect_main
PUBLIC get_cpu_type
PUBLIC get_cpu_features
PUBLIC check_cpu_feature
PUBLIC asm_detect_cpu_type
PUBLIC asm_get_cpu_flags
PUBLIC asm_get_cpu_family
PUBLIC asm_get_cpuid_max_level
PUBLIC asm_is_v86_mode
PUBLIC asm_get_interrupt_flag
PUBLIC asm_get_cpu_model
PUBLIC asm_get_cpu_stepping
PUBLIC asm_get_cpu_vendor
PUBLIC asm_get_cpu_vendor_string
PUBLIC asm_has_cyrix_extensions
PUBLIC asm_get_cache_info
PUBLIC asm_get_cpu_speed
PUBLIC asm_get_speed_confidence
PUBLIC asm_has_invariant_tsc
PUBLIC asm_has_rdtscp
PUBLIC asm_is_hypervisor

; New 386/486 specific detection functions
PUBLIC detect_386_features
PUBLIC detect_486_features  
PUBLIC test_cache_type
PUBLIC detect_486_cache_config

; Enhanced CPUID feature detection functions
PUBLIC get_cpu_signature_info
PUBLIC get_cache_descriptors
PUBLIC parse_cache_descriptors
PUBLIC get_brand_string
PUBLIC get_extended_features
PUBLIC detect_v86_mode
PUBLIC get_cpuid_max_level
PUBLIC detect_clflush_support
PUBLIC check_wbinvd_safety

; External references removed - SMC patching moved to separate module

;-----------------------------------------------------------------------------
; cpu_detect_main - Main CPU detection routine
; This is the primary entry point called during driver initialization
;
; Input:  None
; Output: AX = 0 for success, non-zero for error
; Uses:   All registers
;-----------------------------------------------------------------------------
cpu_detect_main PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Perform comprehensive CPU type detection
        call    detect_cpu_type
        mov     [detected_cpu_type], al

        ; Check for valid CPU type (accept 8086+)
        cmp     al, CPU_UNKNOWN
        je      .cpu_unsupported

        ; Log 8086/8088 detection for simplified boot path
        cmp     al, CPU_80286
        jae     .cpu_ok
        ; 8086/8088 detected - will use simplified boot path
        ; Features will be minimal (no CPUID, no 32-bit, etc.)
.cpu_ok:

        ; Detect CPU features based on detected type
        call    detect_cpu_features
        mov     dword ptr [cpu_features], eax

        ; Clear vendor ID string initially
        mov     si, OFFSET cpu_vendor_id
        mov     cx, 12
        mov     al, 0
        rep     stosb

        ; Get vendor ID if CPUID is available
        test    dword ptr [cpu_features], FEATURE_CPUID
        jz      .no_vendor_id
        
        ; Get maximum CPUID level first (safety)
        call    get_cpuid_max_level
        
        call    get_cpu_vendor_id

.no_vendor_id:
        ; Detect V86 mode (critical for cache operations)
        call    detect_v86_mode
        
        ; Check WBINVD safety based on V86 mode
        call    check_wbinvd_safety
        
        ; Initialize stepping ID (default to 0 if not detectable)
        mov     byte ptr [cpu_step_id], 0
        
        ; Get comprehensive CPU information if CPUID available
        test    dword ptr [cpu_features], FEATURE_CPUID
        jz      .no_comprehensive_info
        
        ; Get detailed signature information
        call    get_cpu_signature_info
        
        ; Detect CLFLUSH support (Tier 1 DMA safety)
        call    detect_clflush_support
        
        ; Get cache descriptors and parse them
        call    get_cache_descriptors
        
        ; Try extended CPUID for AMD/VIA cache info
        call    get_extended_cache_info
        
        ; Get CPU brand string
        call    get_brand_string
        
        ; Get extended features
        call    get_extended_features
        
        ; Check if TSC is invariant (for power management awareness)
        call    check_invariant_tsc
        
        ; Check if running under hypervisor
        call    detect_hypervisor

.no_comprehensive_info:
        ; Success - CPU detection completed
        mov     ax, CPU_SUCCESS
        jmp     .exit

.cpu_unsupported:
        ; CPU type is below minimum requirement (< 286)
        mov     ax, CPU_ERROR_UNSUPPORTED
        jmp     .exit

.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
cpu_detect_main ENDP

;-----------------------------------------------------------------------------
; get_cpu_vendor_id - Get CPU vendor string using CPUID
;
; Input:  None
; Output: Vendor string stored in cpu_vendor_id
; Uses:   EAX, EBX, ECX, EDX, SI
;-----------------------------------------------------------------------------
get_cpu_vendor_id PROC
        push    bp
        mov     bp, sp
        push    si
        push    di
        
        ; Execute CPUID function 0 to get vendor string
        mov     eax, 0
        db      0fh, 0a2h       ; CPUID instruction
        
        ; Vendor string is returned in EBX, EDX, ECX (in that order)
        mov     si, OFFSET cpu_vendor_id
        
        ; Store EBX (first 4 characters)
        mov     dword ptr [si], ebx
        
        ; Store EDX (next 4 characters)
        mov     dword ptr [si+4], edx
        
        ; Store ECX (last 4 characters)
        mov     dword ptr [si+8], ecx
        
        ; Null terminate the string
        mov     byte ptr [si+12], 0
        
        pop     di
        pop     si
        pop     bp
        ret
get_cpu_vendor_id ENDP

;-----------------------------------------------------------------------------
; get_cpu_stepping - Get CPU stepping information using CPUID
;
; Input:  None
; Output: Stepping stored in cpu_step_id
; Uses:   EAX, EDX
;-----------------------------------------------------------------------------
get_cpu_stepping PROC
        push    bp
        mov     bp, sp
        
        ; Execute CPUID function 1 to get stepping info
        mov     eax, 1
        db      0fh, 0a2h       ; CPUID instruction
        
        ; Stepping is in bits 0-3 of EAX
        and     al, 0fh
        mov     [cpu_step_id], al
        
        pop     bp
        ret
get_cpu_stepping ENDP

;-----------------------------------------------------------------------------
; get_cpu_signature_info - Extract CPU signature information from CPUID
;
; Input:  None
; Output: CPU family, model, stepping stored in respective variables
; Uses:   EAX, EBX, ECX, EDX
;-----------------------------------------------------------------------------
get_cpu_signature_info PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        
        ; Check if CPUID is available using safety flag
        cmp     byte ptr [cpuid_available], 0
        je      .no_cpuid
        
        ; Execute CPUID function 1 to get signature info
        mov     eax, 1
        db      0fh, 0a2h       ; CPUID instruction
        
        ; Store the full signature
        mov     dword ptr [cpu_signature], eax
        
        ; Extract stepping (bits 0-3)
        mov     bl, al
        and     bl, 0fh
        mov     [cpu_step_id], bl
        
        ; Extract model (bits 4-7)
        mov     bl, al
        shr     bl, 4
        and     bl, 0fh
        
        ; Extract family (bits 8-11)
        mov     cl, ah
        and     cl, 0fh
        
        ; Handle extended family/model encoding (family >= 15)
        cmp     cl, 15
        jb      .standard_encoding
        
        ; Extended encoding: add extended family (bits 20-27)
        push    eax
        shr     eax, 20         ; Shift to get bits 20-27
        and     eax, 0ffh
        mov     byte ptr [extended_family], al  ; Store extended family
        add     cl, al          ; Add to base family
        pop     eax
        
        ; Extended model: add extended model (bits 16-19) * 16
        mov     dx, eax  
        shr     dx, 16
        and     dx, 0fh
        shl     dx, 4
        add     bl, dl
        
.standard_encoding:
        mov     [cpu_family_id], cl
        mov     [cpu_model_id], bl
        jmp     .done
        
.no_cpuid:
        ; No CPUID available - set defaults
        mov     byte ptr [cpu_family_id], 0
        mov     byte ptr [cpu_model_id], 0
        mov     byte ptr [cpu_step_id], 0
        mov     dword ptr [cpu_signature], 0
        
.done:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
get_cpu_signature_info ENDP

;-----------------------------------------------------------------------------
; detect_cpu_type - Detect specific CPU type
;
; Detection flow:
;   1. 8086 vs 286+: Shift wrap test
;   2. 286 vs 386+: FLAGS bits 12-15 behavior
;   3. 386 vs 486: AC flag (bit 18) toggle test
;   4. Early 486 vs Late 486: CPUID availability
;   5. 486 vs Pentium+: CPUID family check
;
; Input:  None
; Output: AL = CPU type constant
; Uses:   AX, BX, CX, DX, Flags
;-----------------------------------------------------------------------------
detect_cpu_type PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Initialize defaults
        mov     byte ptr [cpu_is_386_plus], 0
        mov     byte ptr [cpu_is_486_plus], 0
        mov     byte ptr [cpuid_available], 0
        
        ; Test 1: 8086/186 vs 286+
        ; Try to clear FLAGS bits 12-15 ONLY (preserve IF and other flags)
        ; 286+ will read back 0F000h (bits always set in real mode)
        ; 8086/186 will read back 0000h (bits truly cleared)
        pushf
        pop     ax              ; Get current flags
        mov     bx, ax          ; Save original
        and     ax, 0FFFh       ; Clear ONLY bits 12-15, preserve IF
        push    ax
        popf                    ; Load modified flags
        pushf
        pop     ax              ; Read back
        and     ax, 0F000h      ; Check bits 12-15
        push    bx              ; Restore original
        popf
        cmp     ax, 0F000h      ; 286+ forces these bits to 1
        jne     .is_8086_or_186 ; If not forced to 1, it's 8086 or 186

        ; Test 2: 286 vs 386+
        ; Only 386+ can actually clear bits 12-15 briefly
        pushf
        pop     ax
        mov     bx, ax          ; Save original
        and     ax, 0FFFh       ; Clear bits 12-15
        push    ax
        popf
        pushf
        pop     ax
        push    bx              ; Restore original
        popf
        and     ax, 0F000h      ; Check bits 12-15
        cmp     ax, 0           ; 386+ can clear them briefly
        jne     .is_286         ; 286 cannot clear them

        ; We now know it's 386+
        mov     byte ptr [cpu_is_386_plus], 1
        
        ; Test 3: 386 vs 486+ using AC flag (bit 18)
        ; GPT-5: Added CPU check before PUSHFD/POPFD usage
        ; CRITICAL: PUSHFD/POPFD are 386+ instructions, verify we're on 386+
        cmp     byte ptr [cpu_is_386_plus], 1
        jne     .skip_ac_test   ; Skip if not 386+ (should never happen here)
        
        pushfd                  ; 32-bit push (safe on 386+)
        pop     eax
        mov     edx, eax        ; Save original EFLAGS
        xor     eax, 40000h     ; Toggle AC flag (bit 18)
        push    eax
        popfd                   ; Try to set modified EFLAGS
        pushfd
        pop     eax             ; Read back
        push    edx             ; Restore original
        popfd
        xor     eax, edx        ; What changed?
        test    eax, 40000h     ; Did AC bit toggle?
        jz      .is_386         ; No toggle = 386
        jmp     .ac_test_done
        
.skip_ac_test:
        ; Should never reach here, but handle gracefully
        jmp     .is_386
        
.ac_test_done:
        
        ; AC flag toggles = 486+
        mov     byte ptr [cpu_is_486_plus], 1
        jmp     .check_486_cpuid
        
.is_386:
        ; 386 detected - no CPUID possible
        mov     al, CPU_80386
        call    detect_cpu_vendor_no_cpuid
        jmp     .done
        
.check_486_cpuid:
        ; Check if this 486 has CPUID
        call    test_cpuid_available
        cmp     al, 0
        je      .is_486_no_cpuid
        
        ; 486 with CPUID - continue to family check
        mov     byte ptr [cpuid_available], 1
        jmp     .has_cpuid
        
.is_486_no_cpuid:
        ; Early 486 without CPUID
        mov     al, CPU_80486
        call    detect_cpu_vendor_no_cpuid
        jmp     .done

.has_cpuid:
        ; CPUID available - check if late 486 or Pentium+
        ; Note: cpuid_available already set to 1 by the 486 check above
        
        ; Check if it's a 486 with CPUID (family 4) or newer
        push    eax
        push    ebx
        push    ecx
        push    edx
        
        mov     eax, 1
        db      0fh, 0a2h       ; CPUID
        mov     bl, ah          ; Get family from bits 8-11
        and     bl, 0fh
        
        cmp     bl, 4           ; Family 4 = 486
        jne     .not_486_cpuid
        
        ; It's a 486 with CPUID
        mov     al, CPU_80486
        pop     edx
        pop     ecx
        pop     ebx
        pop     eax
        jmp     .done
        
.not_486_cpuid:
        ; Family 5+ (Pentium and newer)
        mov     al, CPU_CPUID_CAPABLE
        pop     edx
        pop     ecx  
        pop     ebx
        pop     eax
        jmp     .done

.is_286:
        mov     al, CPU_80286
        mov     byte ptr [cpu_is_386_plus], 0
        mov     byte ptr [cpu_is_486_plus], 0
        jmp     .done

.is_8086_or_186:
        ; Test 8086 vs 186 using PUSH SP behavior
        ; 8086: SP decremented before push (pushes SP-2)
        ; 186+: SP decremented after push (pushes original SP)
        mov     sp, 0FFFFh      ; Set SP to known value
        push    sp              ; Push SP
        pop     ax              ; Get pushed value
        cmp     ax, 0FFFFh      ; Check if original SP was pushed
        je      .is_186         ; If equal, it's 186+
        
.is_8086:
        ; CPU is 8086/8088
        mov     al, CPU_8086
        mov     byte ptr [cpu_is_386_plus], 0
        mov     byte ptr [cpu_is_486_plus], 0
        jmp     .done
        
.is_186:
        ; CPU is 80186/80188
        mov     al, CPU_80186
        mov     byte ptr [cpu_is_386_plus], 0
        mov     byte ptr [cpu_is_486_plus], 0

.done:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
detect_cpu_type ENDP

;-----------------------------------------------------------------------------
; test_cpuid_available - Test if CPUID instruction is available
;
; Input:  None
; Output: AL = 1 if CPUID available, 0 if not
; Uses:   AX, Flags
;-----------------------------------------------------------------------------
test_cpuid_available PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        
        ; CRITICAL SAFETY CHECK: Must be 386+ to use PUSHFD/POPFD
        cmp     byte ptr [cpu_is_386_plus], 1
        jne     .no_cpuid_early_cpu
        
        ; Test if we can modify the ID flag (bit 21) in EFLAGS
        ; This test only works on 486+ CPUs (practically)
        
        ; Get current EFLAGS (safe on 386+)
        pushfd
        pop     eax
        mov     ebx, eax        ; Save original EFLAGS
        
        ; Try to toggle ID flag (bit 21)
        xor     eax, 200000h    ; Flip bit 21
        push    eax
        popfd                   ; Load modified EFLAGS
        
        ; Check if bit 21 actually changed
        pushfd
        pop     eax
        
        ; Restore original EFLAGS
        push    ebx
        popfd
        
        ; Compare original and modified EFLAGS
        xor     eax, ebx        ; XOR to see what changed
        and     eax, 200000h    ; Check only bit 21
        
        ; If bit 21 changed, CPUID is available
        mov     al, 0           ; Assume not available
        cmp     eax, 0
        je      .no_cpuid
        mov     al, 1           ; CPUID is available
        jmp     .done
        
.no_cpuid_early_cpu:
        ; 8086/286 CPU - CPUID definitely not available
        mov     al, 0
        jmp     .done
        
.no_cpuid:
        mov     al, 0
        
.done:
        pop     cx
        pop     bx
        pop     bp
        ret
test_cpuid_available ENDP

;-----------------------------------------------------------------------------
; detect_cpu_features - Detect CPU features based on type
;
; Input:  None (uses detected_cpu_type)
; Output: EAX = feature flags (32-bit)
; Uses:   EAX, EBX, ECX, EDX
;-----------------------------------------------------------------------------
detect_cpu_features PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Initialize features to none
        mov     eax, FEATURE_NONE

        ; Check CPU type and set appropriate features
        mov     bl, [detected_cpu_type]
        
        cmp     bl, CPU_80186
        jb      .done           ; 8086/8088 - no special features
        
        cmp     bl, CPU_80186
        jne     .not_186
        ; 186 features - PUSHA/POPA, ENTER/LEAVE
        or      eax, FEATURE_PUSHA
        jmp     .test_fpu
        
.not_186:
        cmp     bl, CPU_80286
        jb      .done           ; Should not happen
        
        ; 286+ features
        or      eax, FEATURE_PUSHA

        cmp     bl, CPU_80386
        jb      .test_fpu       ; Skip to FPU test for 286
        
        ; 386+ features
        or      eax, FEATURE_32BIT
        
        ; Detect 386-specific features
        push    eax             ; Save current features
        call    detect_386_features
        mov     ebx, eax        ; Get 386 features in EBX
        pop     eax             ; Restore current features
        or      eax, ebx        ; Combine 386 features

        cmp     byte ptr [detected_cpu_type], CPU_80486
        jb      .test_fpu       ; Skip to FPU test for 386
        
        ; 486+ features (even without CPUID)
        or      eax, FEATURE_BSWAP     ; BSWAP instruction
        or      eax, FEATURE_CMPXCHG   ; CMPXCHG instruction
        or      eax, FEATURE_INVLPG    ; INVLPG instruction
        
        ; Only set CPUID feature if actually available
        cmp     byte ptr [cpuid_available], 1
        jne     .no_cpuid_feature
        or      eax, FEATURE_CPUID
.no_cpuid_feature:
        
        ; Detect 486-specific features
        push    eax             ; Save current features
        call    detect_486_features
        mov     ebx, eax        ; Get 486 features in EBX
        pop     eax             ; Restore current features
        or      eax, ebx        ; Combine 486 features
        
        ; Get additional features from CPUID if available
        call    get_cpuid_features
        or      eax, ebx        ; Combine with CPUID features

.test_fpu:
        ; Test for FPU presence on all CPU types
        call    test_fpu_present
        cmp     cl, 0
        je      .done
        or      eax, FEATURE_FPU

.done:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
detect_cpu_features ENDP

;-----------------------------------------------------------------------------
; test_fpu_present - Test for floating point unit presence
;
; Input:  None
; Output: CL = 1 if FPU present, 0 if not
; Uses:   AX, CX
;-----------------------------------------------------------------------------
test_fpu_present PROC
        push    bp
        mov     bp, sp
        
        ; Initialize FPU and test for its presence
        ; This is a safe method that works on all x86 CPUs
        
        mov     cl, 0           ; Assume no FPU
        
        ; Try to initialize FPU
        fninit                  ; Initialize FPU (no-wait version)
        
        ; Test FPU status word
        ; After FNINIT, status word should be 0
        mov     ax, 5a5ah       ; Put known pattern in AX
        fnstsw  ax              ; Store FPU status word
        
        ; If FPU is present, AX should now be 0
        ; If no FPU, AX retains the original pattern
        cmp     ax, 0
        jne     .no_fpu
        
        ; Additional test: try to set and read control word
        fnstcw  word ptr [bp-2] ; Store control word
        mov     ax, word ptr [bp-2]
        and     ax, 103fh       ; Mask valid bits
        cmp     ax, 003fh       ; Expected initial value
        jne     .no_fpu
        
        mov     cl, 1           ; FPU is present
        
.no_fpu:
        pop     bp
        ret
test_fpu_present ENDP

;-----------------------------------------------------------------------------
; get_cpuid_features - Get additional features from CPUID instruction
;
; Input:  None
; Output: BX = Additional feature flags from CPUID
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
get_cpuid_features PROC
        push    bp
        mov     bp, sp
        push    cx
        push    dx
        
        mov     bx, 0           ; Initialize additional features
        
        ; Check if CPUID is available using safety flag
        cmp     byte ptr [cpuid_available], 0
        je      .done           ; No CPUID available
        
        ; Check max CPUID level first (safety check)
        cmp     dword ptr [cpuid_max_level], 1
        jb      .done           ; Need at least level 1 for features
        
        ; Safe to execute CPUID function 1 (feature flags)
        mov     eax, 1
        db      0fh, 0a2h       ; CPUID instruction (machine code for compatibility)
        
        ; EDX contains standard feature flags, ECX contains extended features
        
        ; Extract TSC (Time Stamp Counter) - bit 4 in EDX
        test    edx, 10h
        jz      .no_tsc
        or      bx, FEATURE_TSC
.no_tsc:
        
        ; Extract MSR (Model Specific Registers) - bit 5 in EDX
        test    edx, 20h
        jz      .no_msr
        or      bx, FEATURE_MSR
.no_msr:
        
        ; Extract CX8 (CMPXCHG8B) - bit 8 in EDX
        test    edx, 100h
        jz      .no_cx8
        or      bx, FEATURE_CX8
.no_cx8:
        
        ; Extract MMX - bit 23 in EDX
        test    edx, 800000h
        jz      .no_mmx
        or      bx, FEATURE_MMX
.no_mmx:
        
        ; Extract SSE - bit 25 in EDX
        test    edx, 2000000h
        jz      .no_sse
        or      bx, FEATURE_SSE
.no_sse:
        
        ; Extract SSE2 - bit 26 in EDX
        test    edx, 4000000h
        jz      .no_sse2
        or      bx, FEATURE_SSE2
        mov     byte ptr [sse2_available], 1   ; Set SSE2 flag for MFENCE
.no_sse2:
        
        ; Extract Hyper-Threading - bit 28 in EDX
        test    edx, 10000000h
        jz      .no_ht
        or      bx, FEATURE_HT
.no_ht:
        
        ; Note: ECX features could be added here for newer CPUs
        ; For DOS packet driver, the EDX features are most relevant
        
.done:
        pop     dx
        pop     cx
        pop     bp
        ret
get_cpuid_features ENDP

; SMC patching functions removed - moved to separate module
; All self-modifying code optimization has been moved to smc_patch.asm
; This module now focuses exclusively on CPU detection

;-----------------------------------------------------------------------------
; get_cpu_type - Return detected CPU type
;
; Input:  None
; Output: AL = CPU type constant
; Uses:   AL
;-----------------------------------------------------------------------------
get_cpu_type PROC
        mov     al, [detected_cpu_type]
        ret
get_cpu_type ENDP

;-----------------------------------------------------------------------------
; get_cpu_features - Return detected CPU features
;
; Input:  None
; Output: AX = CPU feature flags
; Uses:   AX
;-----------------------------------------------------------------------------
get_cpu_features PROC
        mov     eax, dword ptr [cpu_features]
        ret
get_cpu_features ENDP

;-----------------------------------------------------------------------------
; check_cpu_feature - Check if specific CPU feature is available
;
; Input:  AX = feature flag to check
; Output: AX = 0 if not available, non-zero if available
; Uses:   AX
;-----------------------------------------------------------------------------
check_cpu_feature PROC
        push    bp
        mov     bp, sp
        push    bx

        mov     ebx, eax                ; Save feature to check
        mov     eax, dword ptr [cpu_features] ; Get current features
        and     eax, ebx                ; Test for specific feature
        ; AX now contains 0 if feature not present, non-zero if present

        pop     bx
        pop     bp
        ret
check_cpu_feature ENDP

;-----------------------------------------------------------------------------
; asm_detect_cpu_type - C-callable wrapper for CPU type detection
;
; Input:  None
; Output: AX = CPU type constant
; Uses:   AX
;-----------------------------------------------------------------------------
asm_detect_cpu_type PROC
        mov     al, [detected_cpu_type]
        mov     ah, 0                   ; Clear high byte for 16-bit return
        ret
asm_detect_cpu_type ENDP

;-----------------------------------------------------------------------------
; asm_get_cpu_flags - C-callable wrapper for CPU feature flags
;
; Input:  None
; Output: DX:AX = CPU feature flags (32-bit value)
;         AX = Low 16 bits
;         DX = High 16 bits
; Uses:   AX, DX
;-----------------------------------------------------------------------------
asm_get_cpu_flags PROC
        mov     eax, dword ptr [cpu_features] ; Get full 32 bits
        mov     dx, ax          ; Copy low word to DX temporarily
        shr     eax, 16         ; Shift high word into AX
        xchg    ax, dx          ; AX = low word, DX = high word
        ret
asm_get_cpu_flags ENDP

;-----------------------------------------------------------------------------
; detect_386_features - Detect 386-specific features and capabilities
;
; Input:  None
; Output: EAX = 386-specific feature flags
; Uses:   EAX, EBX, ECX, EDX, Flags, CR0
;-----------------------------------------------------------------------------
detect_386_features PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        
        ; Initialize 386 feature flags
        mov     eax, 0
        
        ; Verify we're actually on a 386+ CPU
        cmp     byte ptr [detected_cpu_type], CPU_80386
        jb      .not_386
        
        ; Test 1: Alignment Check (AC) flag capability 
        ; This distinguishes 386 from 486+ (AC flag introduced in 486)
        call    test_alignment_check_flag
        cmp     bl, 0
        je      .no_ac_flag
        or      eax, FEATURE_386_AC
.no_ac_flag:

        ; Test 2: Virtual 8086 mode capability
        ; Test if we can set VM flag (bit 17) in EFLAGS
        call    test_v86_mode_capability
        cmp     bl, 0
        je      .no_v86
        or      eax, FEATURE_386_V86
.no_v86:

        ; Test 3: 32-bit register operation verification
        ; This is inherent in 386+, just verify 32-bit ops work
        call    test_32bit_operations
        cmp     bl, 0
        je      .no_32bit_verified
        ; Note: This overlaps with FEATURE_32BIT, but confirms 386+ capability
        
.no_32bit_verified:
        ; Test 4: Paging support detection (CR0 register access)
        call    test_paging_support
        cmp     bl, 0
        je      .no_paging
        or      eax, FEATURE_386_PAGING
.no_paging:

.not_386:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
detect_386_features ENDP

;-----------------------------------------------------------------------------
; test_alignment_check_flag - Test if AC flag can be set in EFLAGS
;
; Input:  None
; Output: BL = 1 if AC flag can be set (486+), 0 if not (386)
; Uses:   AX, BX, EAX, EBX, Flags
;-----------------------------------------------------------------------------
test_alignment_check_flag PROC
        push    bp
        mov     bp, sp
        push    ecx
        
        mov     bl, 0           ; Assume no AC flag capability (386)
        
        ; CRITICAL SAFETY CHECK: Must be 386+ to use PUSHFD/POPFD
        cmp     byte ptr [cpu_is_386_plus], 1
        jne     .no_ac_early_cpu
        
        ; Save current EFLAGS (safe on 386+)
        pushfd
        pop     eax
        mov     ecx, eax        ; Save original EFLAGS
        
        ; Try to toggle AC flag (bit 18) - more robust test
        xor     eax, 40000h     ; Toggle bit 18 (AC flag)
        push    eax
        popfd                   ; Load modified flags
        
        ; Read back and check if AC flag changed
        pushfd
        pop     eax
        
        ; Restore original EFLAGS before testing
        push    ecx
        popfd
        
        ; Check if AC flag toggled
        xor     eax, ecx        ; XOR to see what changed
        and     eax, 40000h     ; Isolate AC flag bit
        jz      .no_ac          ; If bit didn't change, it's 386
        
        mov     bl, 1           ; AC flag toggleable = 486+
        jmp     .done
        
.no_ac_early_cpu:
        ; Pre-386 CPU - AC flag doesn't exist
        mov     bl, 0
        jmp     .done
        
.no_ac:
        mov     bl, 0
        
.done:
        pop     ecx
        pop     bp
        ret
test_alignment_check_flag ENDP

;-----------------------------------------------------------------------------
; test_v86_mode_capability - Test Virtual 8086 mode flag capability
;
; Input:  None  
; Output: BL = 1 if VM flag settable, 0 if not
; Uses:   AX, BX, EAX, EBX, Flags
;-----------------------------------------------------------------------------
test_v86_mode_capability PROC
        push    bp
        mov     bp, sp
        
        mov     bl, 0           ; Assume no V86 capability
        
        ; We can't actually enter V86 mode from real mode,
        ; but we can test if the VM flag (bit 17) is recognized
        
        ; Save current EFLAGS  
        pushfd
        pop     eax
        mov     ebx, eax        ; Save original
        
        ; Try to set VM flag (bit 17)
        or      eax, 20000h     ; Set bit 17 (VM flag)
        push    eax
        popfd                   ; Load modified flags
        
        ; Check if VM flag actually got set
        pushfd
        pop     eax
        
        ; Restore original EFLAGS
        push    ebx
        popfd
        
        ; Check if VM flag was settable
        and     eax, 20000h     ; Isolate VM flag
        cmp     eax, 0
        je      .no_v86
        mov     bl, 1           ; VM flag is settable (386+)
        
.no_v86:
        pop     bp
        ret
test_v86_mode_capability ENDP

;-----------------------------------------------------------------------------
; test_32bit_operations - Verify 32-bit register operations work
;
; Input:  None
; Output: BL = 1 if 32-bit ops work, 0 if not  
; Uses:   AX, BX, EAX, EBX
;-----------------------------------------------------------------------------
test_32bit_operations PROC
        push    bp
        mov     bp, sp
        
        mov     bl, 0           ; Assume no 32-bit capability
        
        ; Test 32-bit register operations
        ; Use a pattern that requires 32-bit arithmetic
        mov     eax, 12345678h
        mov     ebx, 87654321h
        add     eax, ebx        ; 32-bit addition
        
        ; Expected result: 12345678h + 87654321h = 99999999h
        cmp     eax, 99999999h
        jne     .no_32bit
        
        ; Test 32-bit shifts
        mov     eax, 80000001h
        ror     eax, 1          ; Rotate right 1 bit
        cmp     eax, 0C0000000h
        jne     .no_32bit
        
        mov     bl, 1           ; 32-bit operations work
        
.no_32bit:
        pop     bp
        ret
test_32bit_operations ENDP

;-----------------------------------------------------------------------------
; test_paging_support - Test access to CR0 register for paging support
;
; Input:  None
; Output: BL = 1 if CR0 accessible (386+ with paging), 0 if not
; Uses:   AX, BX, EAX, Flags
;-----------------------------------------------------------------------------
test_paging_support PROC
        push    bp
        mov     bp, sp
        
        mov     bl, 0           ; Assume no paging support
        
        ; CRITICAL SAFETY CHECK: Must be 386+ to access CR0
        cmp     byte ptr [cpu_is_386_plus], 1
        jne     .no_paging      ; Not 386+ - no CR0 access
        
        ; CRITICAL SAFETY CHECK: Must not be in V86 mode (CR0 access will GP fault)
        pushfd
        pop     eax
        test    eax, 20000h     ; Check VM flag (bit 17)
        jnz     .no_paging      ; In V86 mode - skip CR0 access
        
        ; Safe to access CR0 - we're 386+ and not in V86 mode
        mov     eax, cr0        ; This instruction exists only on 386+
        mov     ebx, eax        ; Save current value
        
        ; Test paging capability by checking CR0 structure
        ; Bit 31 (PG) indicates paging capability exists
        ; We won't actually enable paging, just verify the bit exists
        
        ; Try to set a non-critical bit and see if it sticks
        ; Use bit 1 (MP - Math Present) which is safe to toggle
        xor     eax, 2          ; Toggle MP bit
        mov     cr0, eax        ; Write back
        mov     eax, cr0        ; Read again
        mov     cr0, ebx        ; Restore original
        
        ; If we got here without faulting, CR0 is accessible
        mov     bl, 1           ; Paging support available
        jmp     .done
        
.no_paging:
        ; CR0 not accessible (pre-386 or V86 mode)
        mov     bl, 0
        
.done:
        pop     bp
        ret
test_paging_support ENDP

;-----------------------------------------------------------------------------
; detect_486_features - Detect 486-specific features and capabilities  
;
; Input:  None
; Output: EAX = 486-specific feature flags
; Uses:   EAX, EBX, ECX, EDX, Flags
;-----------------------------------------------------------------------------
detect_486_features PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        
        ; Initialize 486 feature flags
        mov     eax, 0
        
        ; Verify we're actually on a 486+ CPU
        cmp     byte ptr [detected_cpu_type], CPU_80486
        jb      .not_486
        
        ; Test 1: Internal cache presence and configuration
        call    detect_486_cache_config
        test    bx, 1           ; Check if cache detected
        jz      .no_cache
        or      eax, FEATURE_486_CACHE
        
        ; Test cache type (Write-Back vs Write-Through)
        call    test_cache_type
        cmp     cl, 1           ; CL=1 means Write-Back
        jne     .no_writeback
        or      eax, FEATURE_486_WRITEBACK
.no_writeback:
.no_cache:

        ; Test 2: BSWAP instruction availability
        call    test_bswap_instruction
        cmp     bl, 0
        je      .no_bswap
        or      eax, FEATURE_BSWAP
.no_bswap:

        ; Test 3: CMPXCHG instruction availability  
        call    test_cmpxchg_instruction
        cmp     bl, 0
        je      .no_cmpxchg
        or      eax, FEATURE_CMPXCHG
.no_cmpxchg:

        ; Test 4: INVLPG instruction availability
        call    test_invlpg_instruction
        cmp     bl, 0
        je      .no_invlpg
        or      eax, FEATURE_INVLPG
.no_invlpg:

.not_486:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
detect_486_features ENDP

;-----------------------------------------------------------------------------
; detect_486_cache_config - Detect internal cache configuration
;
; Input:  None
; Output: BX = cache configuration flags (bit 0 = cache present)
;         CX = cache size (if detectable)
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
detect_486_cache_config PROC
        push    bp
        mov     bp, sp
        push    dx
        
        mov     bx, 0           ; Assume no cache
        mov     cx, 0           ; Unknown cache size
        
        ; 486 internal cache detection method:
        ; Use cache-specific timing differences or control registers
        
        ; Method 1: Check if CPUID is available for cache info
        cmp     byte ptr [detected_cpu_type], CPU_80486
        jb      .no_cache_info
        
        ; If CPUID available, try to get cache info
        test    dword ptr [cpu_features], FEATURE_CPUID
        jz      .no_cpuid_cache
        
        ; Use CPUID function 2 for cache information (if supported)
        mov     eax, 0          ; Check max CPUID function
        db      0fh, 0a2h       ; CPUID
        cmp     eax, 2          ; Check if function 2 available
        jb      .no_cpuid_cache
        
        ; Get cache descriptors
        mov     eax, 2
        db      0fh, 0a2h       ; CPUID function 2
        
        ; Basic cache present indication
        ; If we got valid CPUID response, assume cache exists
        or      bx, 1           ; Set cache present flag
        mov     cx, 8           ; Assume 8KB cache (typical 486)
        jmp     .done
        
.no_cpuid_cache:
        ; Method 2: Timing-based cache detection
        ; Compare memory access patterns that would show cache behavior
        call    timing_based_cache_detection
        mov     bx, ax          ; Copy result
        
.no_cache_info:
.done:
        pop     dx
        pop     bp
        ret
detect_486_cache_config ENDP

;-----------------------------------------------------------------------------
; timing_based_cache_detection - Use timing to detect cache presence
;
; Input:  None
; Output: AX = 1 if cache detected, 0 if not
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
timing_based_cache_detection PROC
        push    bp
        mov     bp, sp
        
        ; Simplified timing-based detection
        ; In a real implementation, this would measure access times
        ; For now, assume cache is present on 486+
        mov     ax, 1           ; Assume cache present
        
        pop     bp
        ret
timing_based_cache_detection ENDP

;-----------------------------------------------------------------------------
; test_cache_type - Test for Write-Back vs Write-Through cache mode
;
; Input:  None
; Output: CL = 0 for Write-Through, 1 for Write-Back  
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
test_cache_type PROC
        push    bp
        mov     bp, sp
        push    ax
        push    bx
        push    dx
        
        mov     cl, 0           ; Assume Write-Through
        
        ; CRITICAL SAFETY CHECK: Must be 486+ to access CR0
        cmp     byte ptr [cpu_is_486_plus], 1
        jne     .no_cache_control
        
        ; CRITICAL SAFETY CHECK: Must not be in V86 mode (CR0 access will GP fault)
        pushfd
        pop     eax
        test    eax, 20000h     ; Check VM flag (bit 17)
        jnz     .no_cache_control ; In V86 mode - skip CR0 access
        
        ; Safe to access CR0 - we're 486+ and not in V86 mode
        mov     eax, cr0
        
        ; Check if cache control bits are accessible
        ; Bit 29 (NW) and Bit 30 (CD) control cache behavior
        test    eax, 60000000h  ; Test CD and NW bits
        
        ; For most 486 systems, if caching is enabled and
        ; no specific Write-Through mode is set, assume Write-Back
        ; This is a simplified test
        mov     cl, 1           ; Assume Write-Back capability
        jmp     .done
        
.no_cache_control:
        ; Pre-486 CPU or no cache control
        mov     cl, 0           ; Default to Write-Through
        
.done:
        pop     dx
        pop     bx
        pop     ax
        pop     bp
        ret
test_cache_type ENDP

;-----------------------------------------------------------------------------
; test_bswap_instruction - Test BSWAP instruction availability
;
; Input:  None
; Output: BL = 1 if BSWAP available, 0 if not
; Uses:   AX, BX, EAX, EBX
;-----------------------------------------------------------------------------
test_bswap_instruction PROC
        push    bp
        mov     bp, sp
        
        mov     bl, 0           ; Assume not available
        
        ; CRITICAL SAFETY CHECK: Must be 486+ to test BSWAP
        cmp     byte ptr [cpu_is_486_plus], 1
        jne     .no_bswap_early_cpu
        
        ; Test BSWAP instruction (486+ only)
        ; Set a test pattern and try BSWAP
        mov     eax, 12345678h
        
        ; Execute BSWAP EAX (machine code: 0F C8)
        db      0fh, 0c8h       ; BSWAP EAX
        
        ; Check if bytes were swapped correctly
        ; 12345678h should become 78563412h after BSWAP
        cmp     eax, 78563412h
        jne     .no_bswap
        
        ; Swap back to verify
        db      0fh, 0c8h       ; BSWAP EAX again  
        cmp     eax, 12345678h
        jne     .no_bswap
        
        mov     bl, 1           ; BSWAP instruction works
        jmp     .done
        
.no_bswap_early_cpu:
        ; Pre-486 CPU - BSWAP not available
        mov     bl, 0
        jmp     .done
        
.no_bswap:
        mov     bl, 0
        
.done:
        pop     bp
        ret
test_bswap_instruction ENDP

;-----------------------------------------------------------------------------
; test_cmpxchg_instruction - Test CMPXCHG instruction availability
;
; Input:  None
; Output: BL = 1 if CMPXCHG available, 0 if not
; Uses:   AX, BX, CX, EAX, EBX, ECX
;-----------------------------------------------------------------------------
test_cmpxchg_instruction PROC
        push    bp
        mov     bp, sp
        
        mov     bl, 0           ; Assume not available
        
        ; CRITICAL SAFETY CHECK: Must be 486+ to test CMPXCHG
        cmp     byte ptr [cpu_is_486_plus], 1
        jne     .no_cmpxchg_early_cpu
        
        ; Test CMPXCHG instruction (486+ only)
        ; Set up test values
        mov     eax, 12345678h  ; Accumulator
        mov     ebx, 87654321h  ; Destination
        mov     ecx, 0abcdef00h ; Source
        
        ; Execute CMPXCHG EBX, ECX (machine code: 0F B1 CB)
        ; This compares EAX with EBX, if equal sets EBX=ECX, else EAX=EBX
        db      0fh, 0b1h, 0cbh ; CMPXCHG EBX, ECX
        
        ; Since EAX != EBX initially, EAX should now equal EBX
        cmp     eax, 87654321h
        jne     .no_cmpxchg
        
        ; EBX should remain unchanged
        cmp     ebx, 87654321h  
        jne     .no_cmpxchg
        
        mov     bl, 1           ; CMPXCHG instruction works
        jmp     .done
        
.no_cmpxchg_early_cpu:
        ; Pre-486 CPU - CMPXCHG not available
        mov     bl, 0
        jmp     .done
        
.no_cmpxchg:
        mov     bl, 0
        
.done:
        pop     bp
        ret
test_cmpxchg_instruction ENDP

;-----------------------------------------------------------------------------
; test_invlpg_instruction - Test INVLPG instruction availability
;
; Input:  None
; Output: BL = 1 if INVLPG available, 0 if not
; Uses:   AX, BX
;-----------------------------------------------------------------------------
test_invlpg_instruction PROC
        push    bp
        mov     bp, sp
        
        mov     bl, 0           ; Assume not available
        
        ; INVLPG instruction (486+ only) invalidates page in TLB
        ; We can't easily test this without setting up paging
        ; For safety, we'll use CPU type detection
        
        ; If we're on 486+, assume INVLPG is available
        cmp     byte ptr [detected_cpu_type], CPU_80486
        jb      .no_invlpg
        
        mov     bl, 1           ; INVLPG assumed available on 486+
        
.no_invlpg:
        pop     bp
        ret
test_invlpg_instruction ENDP

;-----------------------------------------------------------------------------
; get_cache_descriptors - Get cache descriptors from CPUID leaf 2
;
; Input:  None
; Output: Cache descriptors stored in cache_descriptors array
; Uses:   EAX, EBX, ECX, EDX, SI
;-----------------------------------------------------------------------------
get_cache_descriptors PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        
        ; Check if CPUID is available
        test    dword ptr [cpu_features], FEATURE_CPUID
        jz      .no_cpuid
        
        ; Clear cache descriptors array
        mov     si, OFFSET cache_descriptors
        mov     cx, MAX_CACHE_ENTRIES
        mov     al, 0
        rep     stosb
        
        ; Execute CPUID function 2 to get cache descriptors
        ; First call to determine iteration count
        mov     eax, 2
        db      0fh, 0a2h       ; CPUID instruction
        
        ; AL contains the number of times we need to execute CPUID leaf 2
        movzx   cx, al          ; Get iteration count
        cmp     cx, 0
        jne     .count_valid
        ; AL=0 edge case (non-Intel CPU): process current results as single iteration
        mov     cx, 1           ; Force at least one iteration
.count_valid:
        cmp     cx, 16          ; Sanity check - max iterations
        jbe     .count_ok
        mov     cx, 16          ; Limit iterations
.count_ok:
        
        ; Initialize descriptor storage pointer
        mov     si, OFFSET cache_descriptors
        xor     di, di          ; Descriptor index
        mov     bx, cx          ; Save original iteration count for comparison
        
.descriptor_loop:
        push    cx              ; Save iteration counter
        
        ; Execute CPUID(2) for this iteration
        mov     eax, 2
        db      0fh, 0a2h       ; CPUID instruction
        
        ; Check if we should skip EAX (bit 31 set means ignore)
        test    eax, 80000000h
        jnz     .skip_eax_iter
        
        ; Store EAX descriptors (skip AL on first iteration)
        pop     cx
        push    cx              ; Peek at iteration counter
        cmp     cx, bx          ; Check if this is first iteration (cx == original count)
        je      .skip_al        ; Skip AL on first iteration (contains count)
        cmp     di, MAX_CACHE_ENTRIES - 1
        jae     .skip_eax_iter
        mov     [si+di], al
        inc     di
.skip_al:
        cmp     di, MAX_CACHE_ENTRIES - 3
        jae     .skip_eax_iter
        mov     [si+di], ah
        inc     di
        shr     eax, 16
        mov     [si+di], al
        inc     di
        mov     [si+di], ah
        inc     di
        
.skip_eax_iter:
        ; Store descriptors from EBX (if bit 31 is clear)
        test    ebx, 80000000h
        jnz     .skip_ebx_iter
        cmp     di, MAX_CACHE_ENTRIES - 4
        jae     .skip_ebx_iter
        mov     [si+di], bl
        inc     di
        mov     [si+di], bh
        inc     di
        shr     ebx, 16
        mov     [si+di], bl
        inc     di
        mov     [si+di], bh
        inc     di
        
.skip_ebx_iter:
        ; Store descriptors from ECX (if bit 31 is clear)
        test    ecx, 80000000h
        jnz     .skip_ecx_iter
        cmp     di, MAX_CACHE_ENTRIES - 4
        jae     .skip_ecx_iter
        mov     [si+di], cl
        inc     di
        mov     [si+di], ch
        inc     di
        shr     ecx, 16
        mov     [si+di], cl
        inc     di
        mov     [si+di], ch
        inc     di
        
.skip_ecx_iter:
        ; Store descriptors from EDX (if bit 31 is clear)
        test    edx, 80000000h
        jnz     .skip_edx_iter
        cmp     di, MAX_CACHE_ENTRIES - 4
        jae     .skip_edx_iter
        mov     [si+di], dl
        inc     di
        mov     [si+di], dh
        inc     di
        shr     edx, 16
        mov     [si+di], dl
        inc     di
        mov     [si+di], dh
        inc     di
        
.skip_edx_iter:
        ; Continue loop for remaining iterations
        pop     cx
        dec     cx
        jnz     .descriptor_loop
        
        ; Parse the collected descriptors
        call    parse_cache_descriptors
        jmp     .done
        
.no_cpuid:
        ; No CPUID available - clear cache info
        mov     word ptr [cache_l1_data_size], CACHE_SIZE_UNKNOWN
        mov     word ptr [cache_l1_code_size], CACHE_SIZE_UNKNOWN
        mov     word ptr [cache_l2_size], CACHE_SIZE_UNKNOWN
        
.done:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
get_cache_descriptors ENDP

;-----------------------------------------------------------------------------
; parse_cache_descriptors - Parse cache descriptor bytes and extract info
;
; Input:  None (uses cache_descriptors array)
; Output: Cache information stored in cache size/associativity variables
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
parse_cache_descriptors PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        
        ; Initialize cache information to unknown
        mov     word ptr [cache_l1_data_size], CACHE_SIZE_UNKNOWN
        mov     word ptr [cache_l1_code_size], CACHE_SIZE_UNKNOWN
        mov     word ptr [cache_l2_size], CACHE_SIZE_UNKNOWN
        mov     byte ptr [cache_l1_data_assoc], 0
        mov     byte ptr [cache_l1_code_assoc], 0
        mov     byte ptr [cache_l2_assoc], 0
        mov     byte ptr [cache_line_size], 0
        
        ; Initialize TLB information to unknown
        mov     word ptr [tlb_data_entries], 0
        mov     word ptr [tlb_code_entries], 0
        mov     word ptr [tlb_page_size], 0
        
        ; Parse each descriptor byte
        mov     si, OFFSET cache_descriptors
        mov     cx, MAX_CACHE_ENTRIES
        
.parse_loop:
        push    cx
        mov     al, [si]        ; Get descriptor byte
        cmp     al, 0           ; Skip null descriptors
        je      .next_descriptor
        
        ; Parse common cache descriptors (Intel format)
        ; This is a simplified parser - full implementation would have complete table
        
        ; L1 Data Cache descriptors
        cmp     al, 0ah         ; 8KB L1 data cache, 2-way set associative
        je      .l1_data_8kb_2way
        cmp     al, 0ch         ; 16KB L1 data cache, 4-way set associative  
        je      .l1_data_16kb_4way
        cmp     al, 0dh         ; 16KB L1 data cache, 4-way set associative (alt)
        je      .l1_data_16kb_4way
        cmp     al, 2ch         ; 32KB L1 data cache, 8-way set associative
        je      .l1_data_32kb_8way
        cmp     al, 60h         ; 16KB L1 data cache, sectored, 8-way
        je      .l1_data_16kb_8way
        cmp     al, 66h         ; 8KB L1 data cache, sectored, 4-way
        je      .l1_data_8kb_4way
        cmp     al, 67h         ; 16KB L1 data cache, sectored, 4-way
        je      .l1_data_16kb_4way
        cmp     al, 68h         ; 32KB L1 data cache, sectored, 4-way
        je      .l1_data_32kb_4way
        
        ; L1 Instruction Cache descriptors
        cmp     al, 06h         ; 8KB L1 instruction cache, 4-way set associative
        je      .l1_code_8kb_4way
        cmp     al, 08h         ; 16KB L1 instruction cache, 4-way set associative
        je      .l1_code_16kb_4way
        cmp     al, 09h         ; 32KB L1 instruction cache, 4-way set associative
        je      .l1_code_32kb_4way
        cmp     al, 30h         ; 32KB L1 instruction cache, 8-way set associative
        je      .l1_code_32kb_8way
        
        ; L2 Cache descriptors
        cmp     al, 22h         ; 512KB L3 cache, 4-way set associative (actually L3)
        je      .l2_512kb_4way  ; Treat as L2 for simplicity
        cmp     al, 41h         ; 128KB L2 cache, 4-way set associative
        je      .l2_128kb_4way
        cmp     al, 42h         ; 256KB L2 cache, 4-way set associative
        je      .l2_256kb_4way
        cmp     al, 43h         ; 512KB L2 cache, 4-way set associative
        je      .l2_512kb_4way
        cmp     al, 44h         ; 1MB L2 cache, 4-way set associative
        je      .l2_1mb_4way
        cmp     al, 45h         ; 2MB L2 cache, 4-way set associative
        je      .l2_2mb_4way
        cmp     al, 78h         ; 1MB L2 cache, 4-way set associative, 64 byte line
        je      .l2_1mb_4way
        cmp     al, 79h         ; 128KB L2 cache, 8-way set associative, sectored
        je      .l2_128kb_8way
        cmp     al, 7ah         ; 256KB L2 cache, 8-way set associative, sectored
        je      .l2_256kb_8way
        cmp     al, 7bh         ; 512KB L2 cache, 8-way set associative, sectored
        je      .l2_512kb_8way
        cmp     al, 7ch         ; 1MB L2 cache, 8-way set associative, sectored
        je      .l2_1mb_8way
        cmp     al, 7dh         ; 2MB L2 cache, 8-way set associative
        je      .l2_2mb_8way
        
        ; TLB descriptors (Data TLB)
        cmp     al, 01h         ; Data TLB: 4KB pages, 4-way, 32 entries
        je      .tlb_data_4kb_32
        cmp     al, 02h         ; Data TLB: 4MB pages, fully associative, 2 entries
        je      .tlb_data_4mb_2
        
        ; TLB descriptors (Instruction TLB)  
        cmp     al, 50h         ; Instruction TLB: 4KB/2MB/4MB pages, 64 entries
        je      .tlb_code_mixed_64
        cmp     al, 51h         ; Instruction TLB: 4KB/2MB/4MB pages, 128 entries
        je      .tlb_code_mixed_128
        
        jmp     .next_descriptor
        
.l1_data_8kb_2way:
        mov     word ptr [cache_l1_data_size], 8
        mov     byte ptr [cache_l1_data_assoc], 2
        mov     byte ptr [cache_line_size], 32
        jmp     .next_descriptor

.l1_data_8kb_4way:
        mov     word ptr [cache_l1_data_size], 8
        mov     byte ptr [cache_l1_data_assoc], 4
        mov     byte ptr [cache_line_size], 32
        jmp     .next_descriptor
        
.l1_data_16kb_4way:
        mov     word ptr [cache_l1_data_size], 16
        mov     byte ptr [cache_l1_data_assoc], 4
        mov     byte ptr [cache_line_size], 32
        jmp     .next_descriptor

.l1_data_16kb_8way:
        mov     word ptr [cache_l1_data_size], 16
        mov     byte ptr [cache_l1_data_assoc], 8
        mov     byte ptr [cache_line_size], 64
        jmp     .next_descriptor

.l1_data_32kb_4way:
        mov     word ptr [cache_l1_data_size], 32
        mov     byte ptr [cache_l1_data_assoc], 4
        mov     byte ptr [cache_line_size], 64
        jmp     .next_descriptor

.l1_data_32kb_8way:
        mov     word ptr [cache_l1_data_size], 32
        mov     byte ptr [cache_l1_data_assoc], 8
        mov     byte ptr [cache_line_size], 64
        jmp     .next_descriptor

.l1_code_8kb_4way:
        mov     word ptr [cache_l1_code_size], 8
        mov     byte ptr [cache_l1_code_assoc], 4
        jmp     .next_descriptor
        
.l1_code_16kb_4way:
        mov     word ptr [cache_l1_code_size], 16
        mov     byte ptr [cache_l1_code_assoc], 4
        jmp     .next_descriptor
        
.l1_code_32kb_4way:
        mov     word ptr [cache_l1_code_size], 32
        mov     byte ptr [cache_l1_code_assoc], 4
        jmp     .next_descriptor

.l1_code_32kb_8way:
        mov     word ptr [cache_l1_code_size], 32
        mov     byte ptr [cache_l1_code_assoc], 8
        jmp     .next_descriptor
        
.l2_128kb_4way:
        mov     word ptr [cache_l2_size], 128
        mov     byte ptr [cache_l2_assoc], 4
        jmp     .next_descriptor
        
.l2_256kb_4way:
        mov     word ptr [cache_l2_size], 256
        mov     byte ptr [cache_l2_assoc], 4
        jmp     .next_descriptor
        
.l2_512kb_4way:
        mov     word ptr [cache_l2_size], 512
        mov     byte ptr [cache_l2_assoc], 4
        jmp     .next_descriptor
        
.l2_1mb_4way:
        mov     word ptr [cache_l2_size], 1024
        mov     byte ptr [cache_l2_assoc], 4
        jmp     .next_descriptor

.l2_2mb_4way:
        mov     word ptr [cache_l2_size], 2048
        mov     byte ptr [cache_l2_assoc], 4
        jmp     .next_descriptor

.l2_128kb_8way:
        mov     word ptr [cache_l2_size], 128
        mov     byte ptr [cache_l2_assoc], 8
        jmp     .next_descriptor

.l2_256kb_8way:
        mov     word ptr [cache_l2_size], 256
        mov     byte ptr [cache_l2_assoc], 8
        jmp     .next_descriptor

.l2_512kb_8way:
        mov     word ptr [cache_l2_size], 512
        mov     byte ptr [cache_l2_assoc], 8
        jmp     .next_descriptor

.l2_1mb_8way:
        mov     word ptr [cache_l2_size], 1024
        mov     byte ptr [cache_l2_assoc], 8
        jmp     .next_descriptor

.l2_2mb_8way:
        mov     word ptr [cache_l2_size], 2048
        mov     byte ptr [cache_l2_assoc], 8
        jmp     .next_descriptor

.tlb_data_4kb_32:
        mov     word ptr [tlb_data_entries], 32
        mov     word ptr [tlb_page_size], 4
        jmp     .next_descriptor
        
.tlb_data_4mb_2:
        mov     word ptr [tlb_data_entries], 2
        mov     word ptr [tlb_page_size], 4096     ; 4MB in KB
        jmp     .next_descriptor
        
.tlb_code_mixed_64:
        mov     word ptr [tlb_code_entries], 64
        mov     word ptr [tlb_page_size], 4        ; Mixed, default to 4KB
        jmp     .next_descriptor
        
.tlb_code_mixed_128:
        mov     word ptr [tlb_code_entries], 128
        mov     word ptr [tlb_page_size], 4        ; Mixed, default to 4KB
        jmp     .next_descriptor
        
.next_descriptor:
        inc     si
        pop     cx
        loop    .parse_loop
        
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
parse_cache_descriptors ENDP

;-----------------------------------------------------------------------------
; get_extended_cache_info - Get cache info from extended CPUID (AMD/VIA)
;
; Input:  None
; Output: Updates cache size variables if extended info available
; Uses:   EAX, EBX, ECX, EDX
;-----------------------------------------------------------------------------
get_extended_cache_info PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        
        ; Check if CPUID is available
        test    dword ptr [cpu_features], FEATURE_CPUID
        jz      .done
        
        ; Check if extended CPUID is available
        mov     eax, 80000000h
        db      0fh, 0a2h       ; CPUID instruction
        cmp     eax, 80000006h  ; Need at least 0x80000006
        jb      .done
        
        ; Get L1 cache information (leaf 0x80000005)
        mov     eax, 80000005h
        db      0fh, 0a2h       ; CPUID instruction
        
        ; ECX = L1 data cache info
        ; Bits 31-24: Size in KB
        ; Bits 23-16: Associativity
        ; Bits 15-8: Lines per tag
        ; Bits 7-0: Line size
        mov     al, ch          ; Get size (bits 31-24)
        xor     ah, ah
        cmp     ax, 0
        je      .check_l1i
        mov     word ptr [cache_l1_data_size], ax
        
        mov     al, cl          ; Get line size (bits 7-0)
        mov     byte ptr [cache_line_size], al
        
.check_l1i:
        ; EDX = L1 instruction cache info
        ; Same format as ECX
        mov     al, dh          ; Get size (bits 31-24)
        xor     ah, ah
        cmp     ax, 0
        je      .check_l2
        mov     word ptr [cache_l1_code_size], ax
        
.check_l2:
        ; Get L2 cache information (leaf 0x80000006)
        mov     eax, 80000006h
        db      0fh, 0a2h       ; CPUID instruction
        
        ; ECX = L2 cache info
        ; Bits 31-16: Size in KB
        ; Bits 15-12: Associativity
        ; Bits 11-8: Lines per tag  
        ; Bits 7-0: Line size
        mov     ax, cx
        shr     eax, 16         ; Get size (bits 31-16)
        cmp     ax, 0
        je      .done
        mov     word ptr [cache_l2_size], ax
        
        ; Update line size if not already set
        cmp     byte ptr [cache_line_size], 0
        jne     .done
        mov     al, cl          ; Get line size (bits 7-0)
        mov     byte ptr [cache_line_size], al
        
.done:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
get_extended_cache_info ENDP

;-----------------------------------------------------------------------------
; check_invariant_tsc - Check if TSC is invariant (power management safe)
;
; Input:  None
; Output: invariant_tsc flag is set
; Uses:   EAX, EBX, ECX, EDX
;-----------------------------------------------------------------------------
check_invariant_tsc PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        
        ; Default to non-invariant
        mov     byte ptr [invariant_tsc], 0
        
        ; Check if CPUID is available
        test    dword ptr [cpu_features], FEATURE_TSC
        jz      .done           ; No TSC, no need to check
        
        ; Check if extended CPUID 0x80000007 is available
        mov     eax, 80000000h
        db      0fh, 0a2h       ; CPUID instruction
        cmp     eax, 80000007h  ; Need at least 0x80000007
        jb      .done
        
        ; Get advanced power management info
        mov     eax, 80000007h
        db      0fh, 0a2h       ; CPUID instruction
        
        ; Check EDX bit 8 for invariant TSC
        test    edx, 100h       ; Bit 8 = Invariant TSC
        jz      .done
        
        ; TSC is invariant
        mov     byte ptr [invariant_tsc], 1
        
.done:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
check_invariant_tsc ENDP

;-----------------------------------------------------------------------------
; detect_hypervisor - Detect if running under hypervisor/virtual machine
;
; Input:  None
; Output: is_hypervisor variable is set
; Uses:   EAX, EBX, ECX, EDX
;-----------------------------------------------------------------------------
detect_hypervisor PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        
        ; Default to not running under hypervisor
        mov     byte ptr [is_hypervisor], 0
        
        ; Check if CPUID is available
        test    dword ptr [cpu_features], FEATURE_CPUID
        jz      .done
        
        ; First check CPUID leaf 1, ECX bit 31 (hypervisor present)
        mov     eax, 1
        db      0fh, 0a2h       ; CPUID instruction
        test    ecx, 80000000h  ; Bit 31 = hypervisor present
        jz      .done           ; No hypervisor bit set
        
        ; Hypervisor detected
        mov     byte ptr [is_hypervisor], 1
        
        ; Try to get hypervisor vendor ID (CPUID 0x40000000)
        mov     eax, 40000000h
        db      0fh, 0a2h       ; CPUID instruction
        
        ; Check if this is a valid hypervisor leaf
        ; Valid responses are >= 0x40000000
        cmp     eax, 40000000h
        jb      .done
        
        ; EBX, ECX, EDX contain hypervisor vendor string
        ; Common hypervisors:
        ; - "VMwareVMware" (VMware)
        ; - "Microsoft Hv" (Hyper-V)
        ; - "KVMKVMKVM   " (KVM)
        ; - "XenVMMXenVMM" (Xen)
        ; - " lrpepyh vr " (Parallels - byte-swapped vendor fallback)
        ; - "VBoxVBoxVBox" (VirtualBox)
        ; - "BhyveBhyveBhyve" (FreeBSD Bhyve)
        ; - "ACRNACRNACRN" (ACRN)
        ; - "TCGTCGTCGTCG" (QEMU TCG mode)
        ; - "Microsoft Hv" (Azure/Hyper-V)
        ; - "prl hyperv  " (Parallels)
        
        ; We could store the vendor string if needed, but for now
        ; just knowing we're in a hypervisor is sufficient
        ; Future enhancement: Store vendor for hypervisor-specific optimizations
        
.done:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
detect_hypervisor ENDP

;-----------------------------------------------------------------------------
; get_brand_string - Get CPU brand string from CPUID leaves 0x80000002-0x80000004
;
; Input:  None
; Output: Brand string stored in cpu_brand_string
; Uses:   EAX, EBX, ECX, EDX, SI, DI
;-----------------------------------------------------------------------------
get_brand_string PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        
        ; Check if CPUID is available
        test    dword ptr [cpu_features], FEATURE_CPUID
        jz      .no_cpuid
        
        ; First check if extended CPUID functions are supported
        mov     eax, 80000000h
        db      0fh, 0a2h       ; CPUID instruction
        cmp     eax, 80000004h  ; Check if brand string functions are available
        jb      .no_brand_string
        
        ; Clear brand string buffer
        mov     di, OFFSET cpu_brand_string
        mov     cx, 48
        mov     al, 0
        rep     stosb
        
        ; Get brand string part 1 (CPUID 0x80000002)
        mov     eax, 80000002h
        db      0fh, 0a2h       ; CPUID instruction
        mov     di, OFFSET cpu_brand_string
        mov     dword ptr [di], eax
        mov     dword ptr [di+4], ebx
        mov     dword ptr [di+8], ecx
        mov     dword ptr [di+12], edx
        
        ; Get brand string part 2 (CPUID 0x80000003)
        mov     eax, 80000003h
        db      0fh, 0a2h       ; CPUID instruction
        mov     dword ptr [di+16], eax
        mov     dword ptr [di+20], ebx
        mov     dword ptr [di+24], ecx
        mov     dword ptr [di+28], edx
        
        ; Get brand string part 3 (CPUID 0x80000004)
        mov     eax, 80000004h
        db      0fh, 0a2h       ; CPUID instruction
        mov     dword ptr [di+32], eax
        mov     dword ptr [di+36], ebx
        mov     dword ptr [di+40], ecx
        mov     dword ptr [di+44], edx
        
        ; Null terminate the string
        mov     byte ptr [di+48], 0
        jmp     .done
        
.no_brand_string:
.no_cpuid:
        ; Clear brand string if not available
        mov     di, OFFSET cpu_brand_string
        mov     cx, 49
        mov     al, 0
        rep     stosb
        
.done:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
get_brand_string ENDP

;-----------------------------------------------------------------------------
; get_extended_features - Get extended features from CPUID leaf 0x80000001
;
; Input:  None
; Output: Extended features stored in extended_features variable
; Uses:   EAX, EDX
;-----------------------------------------------------------------------------
get_extended_features PROC
        push    bp
        mov     bp, sp
        push    dx
        
        ; Initialize extended features to none
        mov     dword ptr [extended_features], 0
        
        ; Check if CPUID is available
        test    dword ptr [cpu_features], FEATURE_CPUID
        jz      .no_cpuid
        
        ; First check if extended CPUID functions are supported
        mov     eax, 80000000h
        db      0fh, 0a2h       ; CPUID instruction
        
        ; Store maximum extended CPUID leaf
        mov     dword ptr [max_extended_leaf], eax
        
        ; Validate that this is a valid extended leaf response
        ; Valid extended leaves are >= 0x80000000
        cmp     eax, 80000000h
        jb      .no_extended    ; Invalid response, no extended CPUID
        
        ; Check if extended feature function 0x80000001 is available
        cmp     eax, 80000001h
        jb      .no_extended
        
        ; Get extended features (CPUID 0x80000001)
        mov     eax, 80000001h
        db      0fh, 0a2h       ; CPUID instruction
        
        ; Store extended features from EDX
        mov     dword ptr [extended_features], edx
        
        ; Check for RDTSCP support (bit 27 in EDX)
        test    edx, 08000000h  ; Bit 27 - RDTSCP
        jz      .no_rdtscp
        mov     byte ptr [has_rdtscp], 1
        
.no_rdtscp:
        ; Update main feature flags with extended features
        mov     eax, dword ptr [cpu_features]
        
        ; Check for SYSCALL/SYSRET (bit 11 in extended features)
        test    edx, 800h
        jz      .no_syscall
        or      eax, FEATURE_SYSCALL
        
.no_syscall:
        ; Store updated features
        mov     dword ptr [cpu_features], eax
        jmp     .done
        
.no_extended:
.no_cpuid:
        ; No extended features available
        
.done:
        pop     dx
        pop     bp
        ret
get_extended_features ENDP

;-----------------------------------------------------------------------------
; detect_v86_mode - Detect if running in Virtual 8086 mode
;
; Input:  None
; Output: AL = 1 if in V86 mode, 0 if not
; Uses:   AX, Flags
;
; V86 mode detection is critical for safe WBINVD usage as it will trap
;-----------------------------------------------------------------------------
detect_v86_mode PROC
        push    bp
        mov     bp, sp
        
        ; Check if we're 386+ using safety flag (V86 mode doesn't exist on < 386)
        cmp     byte ptr [cpu_is_386_plus], 0
        je      .not_v86        ; Not 386+, can't be in V86 mode
        
        ; Safe to use 386+ instructions now
        push    eax
        
        ; Method 1: Try to read VM flag from EFLAGS (bit 17)
        ; In V86 mode, this bit will be set but we can't modify it
        pushfd
        pop     eax
        test    eax, 20000h     ; Check VM flag (bit 17)
        jz      .not_v86_pop    ; Not in V86, need to restore EAX
        
        ; We're in V86 mode
        mov     byte ptr [is_v86_mode], 1
        or      dword ptr [cpu_features], FEATURE_V86_MODE
        pop     eax
        mov     al, 1
        jmp     .done
        
.not_v86_pop:
        pop     eax
        
.not_v86:
        mov     al, 0
        mov     byte ptr [is_v86_mode], 0
        
.done:
        pop     bp
        ret
detect_v86_mode ENDP

;-----------------------------------------------------------------------------
; get_cpuid_max_level - Get maximum supported CPUID function
;
; Input:  None
; Output: EAX = Maximum standard CPUID function supported
; Uses:   EAX, EBX, ECX, EDX
;-----------------------------------------------------------------------------
get_cpuid_max_level PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        
        ; Check if CPUID is available using safety flag
        cmp     byte ptr [cpuid_available], 0
        je      .no_cpuid
        
        ; Safe to use CPUID
        ; Get maximum standard CPUID level
        mov     eax, 0
        db      0fh, 0a2h       ; CPUID instruction
        
        ; Store max level
        mov     dword ptr [cpuid_max_level], eax
        jmp     .done
        
.no_cpuid:
        mov     eax, 0
        mov     dword ptr [cpuid_max_level], 0
        
.done:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
get_cpuid_max_level ENDP

;-----------------------------------------------------------------------------
; detect_clflush_support - Detect CLFLUSH instruction and cache line size
;
; Input:  None
; Output: AL = 1 if CLFLUSH supported, 0 if not
; Uses:   EAX, EBX, ECX, EDX
;-----------------------------------------------------------------------------
detect_clflush_support PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        
        mov     al, 0           ; Assume not supported
        
        ; Check if we have CPUID and max level >= 1
        cmp     dword ptr [cpuid_max_level], 1
        jb      .no_clflush
        
        ; Execute CPUID function 1
        mov     eax, 1
        db      0fh, 0a2h       ; CPUID
        
        ; Check for CLFLUSH support (bit 19 in EDX)
        test    edx, 80000h     ; Bit 19
        jz      .no_clflush
        
        ; CLFLUSH is supported - get cache line size
        ; Cache line size is in bits 8-15 of EBX (in 8-byte chunks)
        mov     al, bh          ; Get bits 8-15
        shl     al, 3           ; Multiply by 8 for bytes
        mov     byte ptr [cache_line_size], al
        
        ; Set feature flag
        or      dword ptr [cpu_features], FEATURE_CLFLUSH
        mov     al, 1           ; Return success
        jmp     .done
        
.no_clflush:
        ; Default cache line size to 64 bytes (common default)
        mov     byte ptr [cache_line_size], 64
        
.done:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
detect_clflush_support ENDP

;-----------------------------------------------------------------------------
; check_wbinvd_safety - Check if WBINVD can be safely used
;
; Input:  None
; Output: AL = 1 if safe, 0 if not safe
; Uses:   AX
;
; WBINVD will trap in V86 mode, so we need to check first
;-----------------------------------------------------------------------------
check_wbinvd_safety PROC
        push    bp
        mov     bp, sp
        
        ; Check if we're 486+ (WBINVD doesn't exist on < 486)
        cmp     byte ptr [detected_cpu_type], CPU_80486
        jb      .not_safe
        cmp     byte ptr [detected_cpu_type], CPU_CPUID_CAPABLE
        jae     .check_v86      ; Need to check for V86 mode
        
.check_v86:
        ; Check if we're in V86 mode
        cmp     byte ptr [is_v86_mode], 0
        jne     .not_safe       ; Can't use WBINVD in V86 mode
        
        ; Safe to use WBINVD
        or      dword ptr [cpu_features], FEATURE_WBINVD_SAFE
        mov     al, 1
        jmp     .done
        
.not_safe:
        mov     al, 0
        
.done:
        pop     bp
        ret
check_wbinvd_safety ENDP

;-----------------------------------------------------------------------------
; detect_cyrix_cpu - Detect Cyrix processors via DIR0/DIR1 registers
;
; Input:  None
; Output: AL = VENDOR_CYRIX if Cyrix detected, VENDOR_UNKNOWN otherwise
; Uses:   AX, BX
;
; Cyrix CPUs have configuration registers at ports 22h/23h that Intel lacks
;-----------------------------------------------------------------------------
detect_cyrix_cpu PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        
        ; Default to not Cyrix
        mov     byte ptr [detected_vendor], VENDOR_UNKNOWN
        
        ; Save interrupt state and disable
        pushf
        cli
        
        ; Try to access Cyrix DIR0 register (index 0FEh)
        mov     al, 0FEh        ; DIR0 index
        out     22h, al         ; Write index to port 22h
        jmp     short $+2       ; I/O delay
        in      al, 23h         ; Read DIR0 from port 23h
        mov     bl, al          ; Save original value
        mov     byte ptr [cyrix_dir0_value], al
        
        ; Try to modify DIR0 (toggle bit 7)
        xor     al, 80h         ; Toggle bit 7
        mov     cl, al          ; Save modified value
        out     23h, al         ; Write back
        jmp     short $+2       ; I/O delay
        
        ; Read it again to see if it changed
        mov     al, 0FEh
        out     22h, al
        jmp     short $+2
        in      al, 23h
        
        ; Check if the bit toggled
        xor     al, bl          ; Compare with original
        and     al, 80h         ; Isolate bit 7
        jz      .not_cyrix      ; Didn't change - not Cyrix
        
        ; It's a Cyrix - restore original value
        mov     al, 0FEh
        out     22h, al
        mov     al, bl          ; Original value
        out     23h, al
        
        ; Mark as Cyrix
        mov     byte ptr [detected_vendor], VENDOR_CYRIX
        mov     byte ptr [cyrix_dir0_present], 1
        
        ; Could check DIR1 (index 0FFh) for specific model
        
.not_cyrix:
        ; Restore interrupt state
        popf
        
        mov     al, byte ptr [detected_vendor]
        
        pop     cx
        pop     bx
        pop     bp
        ret
detect_cyrix_cpu ENDP

;-----------------------------------------------------------------------------
; detect_amd_cpu - Detect AMD processors via unique behaviors
;
; Input:  None
; Output: AL = VENDOR_AMD if AMD detected, VENDOR_UNKNOWN otherwise
; Uses:   AX, BX, CX, DX
;
; AMD CPUs have different undefined opcode and flag behaviors
;-----------------------------------------------------------------------------
detect_amd_cpu PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        
        ; Default to not AMD
        mov     byte ptr [detected_vendor], VENDOR_UNKNOWN
        
        ; Test 1: UMOV instruction behavior (different on AMD)
        ; UMOV is an undefined opcode on Intel 486 but behaves differently on AMD
        mov     ecx, 0FFFFFFFFh
        ; UMOV ECX, ECX - undefined instruction
        db      0Fh, 10h, 0C9h  
        
        ; AMD preserves ECX, Intel may modify it
        cmp     ecx, 0FFFFFFFFh
        jne     .not_amd        ; Intel modified it
        
        ; Test 2: Division undefined flags behavior
        xor     eax, eax
        sahf                    ; Clear flags
        mov     eax, 5
        mov     ebx, 2
        div     ebx             ; Divide - affects undefined flags differently
        lahf                    ; Get flags
        
        ; Check for AMD-specific flag pattern
        ; AMD sets undefined flags differently than Intel
        and     ah, 0C4h        ; Check SF, ZF, PF
        cmp     ah, 044h        ; AMD pattern
        jne     .not_amd
        
        ; Likely AMD
        mov     byte ptr [detected_vendor], VENDOR_AMD
        
.not_amd:
        mov     al, byte ptr [detected_vendor]
        
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
detect_amd_cpu ENDP

;-----------------------------------------------------------------------------
; detect_nexgen_cpu - Detect NexGen Nx586 despite no ID flag support
;
; Input:  None
; Output: AL = VENDOR_NEXGEN if NexGen detected, VENDOR_UNKNOWN otherwise
; Uses:   EAX, EBX, ECX, EDX
;
; NexGen supports CPUID but doesn't support ID flag test
;-----------------------------------------------------------------------------
detect_nexgen_cpu PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        
        ; Default to not NexGen
        mov     byte ptr [detected_vendor], VENDOR_UNKNOWN
        
        ; NexGen appears as 386 (no AC flag) but has CPUID
        ; Try CPUID despite ID flag test failing
        mov     eax, 0
        db      0Fh, 0A2h       ; CPUID instruction
        
        ; Check for "NexGenDriven" vendor string
        cmp     ebx, 4765784Eh  ; "NexG" in little-endian
        jne     .not_nexgen
        cmp     edx, 72446E65h  ; "enDr" in little-endian
        jne     .not_nexgen
        cmp     ecx, 6E657669h  ; "iven" in little-endian
        jne     .not_nexgen
        
        ; It's a NexGen
        mov     byte ptr [detected_vendor], VENDOR_NEXGEN
        mov     byte ptr [nexgen_cpuid_works], 1
        
.not_nexgen:
        mov     al, byte ptr [detected_vendor]
        
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
detect_nexgen_cpu ENDP

;-----------------------------------------------------------------------------
; detect_cpu_vendor_no_cpuid - Detect CPU vendor without CPUID
;
; Input:  None
; Output: Vendor stored in detected_vendor
; Uses:   AX, BX, CX, DX
;
; Tries various vendor-specific detection methods
;-----------------------------------------------------------------------------
detect_cpu_vendor_no_cpuid PROC
        push    bp
        mov     bp, sp
        
        ; Try Cyrix first (most reliable test)
        call    detect_cyrix_cpu
        cmp     al, VENDOR_CYRIX
        je      .done
        
        ; Try AMD-specific behaviors
        call    detect_amd_cpu
        cmp     al, VENDOR_AMD
        je      .done
        
        ; Try NexGen (CPUID despite no ID flag)
        call    detect_nexgen_cpu
        cmp     al, VENDOR_NEXGEN
        je      .done
        
        ; Default to Intel if unknown
        cmp     byte ptr [detected_vendor], VENDOR_UNKNOWN
        jne     .done
        mov     byte ptr [detected_vendor], VENDOR_INTEL
        
.done:
        pop     bp
        ret
detect_cpu_vendor_no_cpuid ENDP

;-----------------------------------------------------------------------------
; asm_get_cpu_family - C-callable wrapper to get CPU family ID
;
; Input:  None
; Output: AX = CPU family ID
; Uses:   AX
;-----------------------------------------------------------------------------
asm_get_cpu_family PROC
        mov     al, [cpu_family_id]
        mov     ah, 0
        ret
asm_get_cpu_family ENDP

;-----------------------------------------------------------------------------
; asm_get_cpu_model - C-callable wrapper to get CPU model ID
;
; Input:  None
; Output: AL = CPU model ID
; Uses:   AX
;-----------------------------------------------------------------------------
asm_get_cpu_model PROC
        mov     al, [cpu_model_id]
        xor     ah, ah
        ret
asm_get_cpu_model ENDP

;-----------------------------------------------------------------------------
; asm_get_cpu_stepping - C-callable wrapper to get CPU stepping ID
;
; Input:  None
; Output: AL = CPU stepping ID
; Uses:   AX
;-----------------------------------------------------------------------------
asm_get_cpu_stepping PROC
        mov     al, [cpu_step_id]
        xor     ah, ah
        ret
asm_get_cpu_stepping ENDP

;-----------------------------------------------------------------------------
; asm_get_cpuid_max_level - C-callable wrapper to get max CPUID level
;
; Input:  None
; Output: EAX = Maximum CPUID level
; Uses:   EAX
;-----------------------------------------------------------------------------
asm_get_cpuid_max_level PROC
        mov     eax, dword ptr [cpuid_max_level]
        ret
asm_get_cpuid_max_level ENDP

;-----------------------------------------------------------------------------
; asm_is_v86_mode - C-callable wrapper to check V86 mode
;
; Input:  None
; Output: AX = 1 if in V86 mode, 0 if not
; Uses:   AX
;-----------------------------------------------------------------------------
asm_is_v86_mode PROC
        mov     al, [is_v86_mode]
        mov     ah, 0
        ret
asm_is_v86_mode ENDP

;-----------------------------------------------------------------------------
; asm_get_interrupt_flag - C-callable wrapper to check IF flag status
;
; Input:  None
; Output: AX = 1 if interrupts enabled (IF=1), 0 if disabled (IF=0)
; Uses:   AX, Flags
;
; CRITICAL: Used by VDS safety layer for ISR context detection
;-----------------------------------------------------------------------------
asm_get_interrupt_flag PROC
        pushf                   ; Push flags register onto stack
        pop     ax              ; Pop flags into AX
        and     ax, 0200h       ; Isolate IF flag (bit 9)
        jz      .ints_disabled
        mov     ax, 1           ; Interrupts enabled
        ret
.ints_disabled:
        xor     ax, ax          ; Interrupts disabled (return 0)
        ret
asm_get_interrupt_flag ENDP

;-----------------------------------------------------------------------------
; asm_get_cpu_vendor - C-callable wrapper to get CPU vendor
;
; Input:  None
; Output: AX = Vendor constant (VENDOR_INTEL, VENDOR_AMD, etc.)
; Uses:   AX
;-----------------------------------------------------------------------------
asm_get_cpu_vendor PROC
        mov     al, [detected_vendor]
        mov     ah, 0
        ret
asm_get_cpu_vendor ENDP

;-----------------------------------------------------------------------------
; asm_get_cpu_vendor_string - C-callable wrapper to get vendor string
;
; Input:  None
; Output: AX:DX = Far pointer to vendor ID string (12 chars)
; Uses:   AX, DX
;-----------------------------------------------------------------------------
asm_get_cpu_vendor_string PROC
        ; Return pointer to cpu_vendor_id string
        mov     ax, OFFSET cpu_vendor_id
        mov     dx, seg cpu_vendor_id
        ret
asm_get_cpu_vendor_string ENDP

;-----------------------------------------------------------------------------
; asm_has_cyrix_extensions - Check if Cyrix CPU extensions present
;
; Input:  None
; Output: AX = 1 if Cyrix extensions detected, 0 otherwise
; Uses:   AX
;-----------------------------------------------------------------------------
asm_has_cyrix_extensions PROC
        mov     al, [cyrix_dir0_present]
        mov     ah, 0
        ret
asm_has_cyrix_extensions ENDP

;-----------------------------------------------------------------------------
; asm_get_cache_info - C-callable wrapper to get cache information
;
; Input:  None
; Output: AX = L1 data cache size (KB)
;         BX = L1 instruction cache size (KB)
;         CX = L2 cache size (KB)
;         DX = Cache line size (bytes)
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
asm_get_cache_info PROC
        mov     ax, word ptr [cache_l1_data_size]
        mov     bx, word ptr [cache_l1_code_size]
        mov     cx, word ptr [cache_l2_size]
        mov     dl, byte ptr [cache_line_size]
        xor     dh, dh          ; Clear high byte of DX
        ret
asm_get_cache_info ENDP

;-----------------------------------------------------------------------------
; detect_cpu_speed - Detect CPU speed in MHz using PIT or RDTSC
;
; Input:  None
; Output: detected_cpu_mhz and speed_confidence are set
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
detect_cpu_speed PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        
        ; Check if running in V86 mode (timing may be unreliable)
        cmp     byte ptr [is_v86_mode], 1
        je      .use_conservative_timing
        
        ; Perform multiple trials for statistical robustness
        mov     di, 5           ; 5 trials
        mov     si, OFFSET speed_trials
        
.trial_loop:
        push    si
        push    di
        call    single_speed_trial
        pop     di
        pop     si
        mov     [si], ax        ; Store result
        add     si, 2
        dec     di
        jnz     .trial_loop
        
        ; Sort trials and pick median
        call    sort_speed_trials
        mov     ax, word ptr [speed_trials+4]  ; 3rd of 5 sorted values (median)
        mov     word ptr [detected_cpu_mhz], ax
        
        ; Calculate confidence based on variance
        call    calculate_confidence
        jmp     .done
        
.use_conservative_timing:
        ; In V86 mode, use single measurement with fallback
        call    single_speed_trial
        mov     word ptr [detected_cpu_mhz], ax
        mov     byte ptr [speed_confidence], 50  ; Low confidence in V86
        
.done:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
detect_cpu_speed ENDP

;-----------------------------------------------------------------------------
; single_speed_trial - Perform a single speed measurement
;
; Input:  None
; Output: AX = Measured speed in MHz
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
single_speed_trial PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        
        ; First calibrate loop overhead if not done
        cmp     word ptr [loop_overhead_ticks], 0
        jne     .overhead_calibrated
        call    calibrate_loop_overhead
.overhead_calibrated:
        
        ; Check if RDTSC is available (Pentium+)
        test    dword ptr [cpu_features], FEATURE_TSC
        jnz     .use_rdtsc
        
.use_pit:
        ; PIT-based timing using channel 2 (speaker timer) for safety
        cli                     ; Disable interrupts for accurate timing
        
        ; Save port 61h state (speaker control)
        in      al, 61h
        mov     byte ptr [port_61h_state], al
        and     al, 0FCh        ; Clear bits 0-1 (gate=0, speaker=0)
        ; Bit 0 = Timer 2 gate
        ; Bit 1 = Speaker data enable (force to 0 to prevent clicks)
        out     61h, al         ; Force gate low and speaker off
        
        ; Program PIT channel 2 for one-shot mode
        mov     al, 0B0h        ; Channel 2, LSB/MSB, mode 0
        out     43h, al
        mov     ax, 0FFFFh      ; Load count = 65535
        out     42h, al         ; Write LSB to channel 2 data port
        mov     al, ah          ; Get MSB
        out     42h, al         ; Write MSB to channel 2 data port
        
        ; Now create rising edge on gate to start counter
        in      al, 61h
        or      al, 01h         ; Set gate bit (rising edge starts mode 0)
        out     61h, al
        
        ; Latch and read channel 2 counter with timeout protection
        mov     al, 80h         ; Latch counter 2
        out     43h, al
        
        ; Timeout protection for PIT reads (max 1000 attempts)
        mov     dx, 1000
.read_pit_start:
        jmp     short $+2       ; I/O delay
        in      al, 42h         ; Read low byte
        mov     bl, al
        in      al, 42h         ; Read high byte
        mov     bh, al          ; BX = start count
        
        ; Verify we got a valid reading (not stuck at 0xFFFF or 0x0000)
        cmp     bx, 0FFFFh
        je      .pit_retry
        cmp     bx, 0
        je      .pit_retry
        jmp     .got_start_count
        
.pit_retry:
        dec     dx
        jnz     .read_pit_start
        ; PIT timeout - mark measurement as low confidence
        mov     byte ptr [speed_confidence], 0
        mov     bx, 8000h       ; Use middle value as fallback
        
.got_start_count:
        mov     word ptr [pit_start_count], bx
        
        ; Calibrated timing loop (10000 iterations, ~9 cycles each)
        mov     cx, 10000
        align   16              ; Align loop for consistent timing
.timing_loop:
        nop                     ; 1 cycle
        nop                     ; 1 cycle
        nop                     ; 1 cycle
        nop                     ; 1 cycle
        loop    .timing_loop    ; 5-17 cycles depending on CPU
        
        ; Read channel 2 counter again with timeout protection
        mov     al, 80h         ; Latch counter 2
        out     43h, al
        
        ; Timeout protection for PIT end read
        push    cx
        mov     cx, 1000
.read_pit_end:
        jmp     short $+2       ; I/O delay
        in      al, 42h         ; Read low byte
        mov     dl, al
        in      al, 42h         ; Read high byte
        mov     dh, al          ; DX = end count
        
        ; Verify we got a valid reading
        cmp     dx, 0FFFFh
        je      .pit_end_retry
        cmp     dx, 0
        je      .pit_end_retry
        jmp     .got_end_count
        
.pit_end_retry:
        dec     cx
        jnz     .read_pit_end
        ; PIT timeout - use estimated value
        mov     dx, 6000h       ; Use estimated end value
        mov     byte ptr [speed_confidence], 0
        
.got_end_count:
        pop     cx
        mov     word ptr [pit_end_count], dx
        
        ; Restore port 61h state
        mov     al, byte ptr [port_61h_state]
        out     61h, al
        
        sti                     ; Re-enable interrupts
        
        ; Calculate ticks elapsed
        ; PIT counts down, so elapsed = start - end
        mov     bx, word ptr [pit_start_count]
        sub     bx, dx          ; BX = elapsed ticks
        jnc     .no_wrap_pit
        ; Handle wraparound (end > start means counter wrapped)
        add     bx, 0FFFFh
        inc     bx
.no_wrap_pit:
        
        ; Subtract loop overhead
        sub     bx, word ptr [loop_overhead_ticks]
        jc      .use_fallback   ; If negative, measurement failed
        
        ; Check for zero ticks to avoid division by zero
        or      bx, bx
        jz      .use_fallback
        
        ; Calculate MHz
        ; PIT runs at 1.193182 MHz
        ; Each loop iteration is ~9 cycles (4 NOPs + loop)
        ; MHz = (iterations * cycles * PIT_freq) / (ticks * 1000000)
        ; Simplified: MHz = 107386 / ticks
        mov     ax, 107386 AND 0FFFFh  ; Low word of constant
        mov     dx, 107386 SHR 16      ; High word (1 for 107386)
        div     bx                     ; AX = MHz
        jmp     .store_result
        
.use_rdtsc:
        ; RDTSC-based timing with CPUID serialization for accuracy
        ;
        ; IMPORTANT EDGE CASES:
        ; 1. V86 Mode with CR4.TSD=1: In Virtual 8086 mode under some memory
        ;    managers, if CR4.TSD (Time Stamp Disable) is set, RDTSC/RDTSCP
        ;    will cause #GP fault even though CPUID indicates support. This
        ;    is rare but can occur with certain DOS extenders. Real mode code
        ;    typically runs at CPL0, but V86 tasks run at CPL3.
        ;
        ; 2. SMI (System Management Interrupt): SMIs can occur during our
        ;    timing window and cannot be blocked by CLI. This can cause the
        ;    measured time to be longer than actual CPU cycles used, leading
        ;    to underestimation of CPU speed. Multiple measurements and median
        ;    selection help mitigate this.
        ;
        cli                     ; Disable interrupts (Note: cannot block NMI/SMI)
        
        ; Save and setup PIT channel 2 for calibration
        in      al, 61h
        mov     byte ptr [port_61h_state], al
        and     al, 0FCh        ; Clear bits 0-1 (gate=0, speaker=0)
        ; Ensures no audio clicks during measurement
        out     61h, al         ; Force gate low and speaker silent
        
        ; Program PIT channel 2
        mov     al, 0B0h        ; Channel 2, LSB/MSB, mode 0
        out     43h, al
        mov     ax, 0FFFFh      ; Load count = 65535
        out     42h, al         ; Write LSB
        mov     al, ah          ; Get MSB
        out     42h, al         ; Write MSB
        
        ; Create rising edge on gate to start counter
        in      al, 61h
        or      al, 01h         ; Set gate bit (rising edge starts mode 0)
        out     61h, al
        
        ; Latch and read channel 2 for calibration with timeout protection
        mov     al, 80h
        out     43h, al
        
        ; Timeout protection for calibration start read
        push    dx
        mov     dx, 1000
.read_calib_start:
        jmp     short $+2
        in      al, 42h
        mov     bl, al
        in      al, 42h
        mov     bh, al          ; BX = PIT start
        
        ; Verify valid reading
        cmp     bx, 0FFFFh
        je      .calib_retry
        cmp     bx, 0
        je      .calib_retry
        jmp     .got_calib_start
        
.calib_retry:
        dec     dx
        jnz     .read_calib_start
        mov     bx, 8000h       ; Use middle value as fallback
        
.got_calib_start:
        pop     dx
        
        ; CPUID serialization fence before RDTSC
        mov     eax, 0
        db      0fh, 0a2h       ; CPUID instruction - serializes execution
        
        ; Read RDTSC start
        db      0fh, 31h        ; RDTSC instruction
        mov     dword ptr [rdtsc_start_low], eax
        mov     dword ptr [rdtsc_start_high], edx
        
        ; Known delay loop (100000 iterations for better resolution)
        mov     ecx, 100000     ; Use ECX for 32-bit counter
.rdtsc_delay:
        nop
        nop
        dec     ecx
        jnz     .rdtsc_delay
        
        ; Check if RDTSCP is available for end measurement
        cmp     byte ptr [has_rdtscp], 0
        jz      .use_rdtsc_with_fence
        
        ; Use RDTSCP for end measurement (self-serializing)
        db      0fh, 01h, 0f9h  ; RDTSCP instruction
        jmp     .got_end_tsc
        
.use_rdtsc_with_fence:
        ; CPUID serialization fence before second RDTSC
        mov     eax, 0
        db      0fh, 0a2h       ; CPUID instruction - serializes execution
        
        ; Read RDTSC end
        db      0fh, 31h        ; RDTSC instruction
        
.got_end_tsc:
        ; Calculate cycles elapsed (end - start)
        sub     eax, dword ptr [rdtsc_start_low]
        sbb     edx, dword ptr [rdtsc_start_high]
        mov     si, ax          ; Save low 16 bits of cycle count
        mov     di, dx          ; Save next 16 bits (32-bit total)
        
        ; Read channel 2 end for calibration with timeout protection
        mov     al, 80h
        out     43h, al
        
        ; Timeout protection for calibration end read
        push    cx
        mov     cx, 1000
.read_calib_end:
        jmp     short $+2
        in      al, 42h
        mov     dl, al
        in      al, 42h
        mov     dh, al          ; DX = PIT end
        
        ; Verify valid reading
        cmp     dx, 0FFFFh
        je      .calib_end_retry
        cmp     dx, 0
        je      .calib_end_retry
        jmp     .got_calib_end
        
.calib_end_retry:
        dec     cx
        jnz     .read_calib_end
        mov     dx, 6000h       ; Use estimated end value
        
.got_calib_end:
        pop     cx
        
        ; Restore port 61h
        mov     al, byte ptr [port_61h_state]
        out     61h, al
        
        sti                     ; Re-enable interrupts
        
        ; Calculate PIT ticks
        sub     bx, dx          ; BX = PIT ticks
        jnc     .no_wrap_rdtsc
        add     bx, 0FFFFh
        inc     bx
.no_wrap_rdtsc:
        
        ; Calculate MHz using full 32-bit math
        ; Cycles in DI:SI, PIT ticks in BX
        ; MHz = (cycles * 1193) / (ticks * 100000)
        
        ; Check for zero ticks to avoid division by zero
        or      bx, bx
        jz      .use_fallback
        
        ; Check if cycles are very large (> 2GHz CPUs)
        cmp     di, 1000h       ; More than 256M cycles?
        ja      .use_safe_path  ; Use safe division for very fast CPUs
        
        ; Full 32-bit calculation: (DI:SI * 1193) / (BX * 100)
        ; First multiply low word
        mov     ax, si
        mov     cx, 1193
        mul     cx              ; DX:AX = SI * 1193
        push    ax              ; Save low result
        push    dx              ; Save high result
        
        ; Multiply high word
        mov     ax, di
        mul     cx              ; DX:AX = DI * 1193
        pop     cx              ; Get high result from SI*1193
        add     ax, cx          ; Add to current result
        adc     dx, 0           ; Propagate carry
        
        ; Now DX:AX has high 32 bits, stack has low 16 bits
        ; Divide by (BX * 100) to get MHz
        mov     cx, 100
        push    dx
        push    ax
        mov     ax, bx
        mul     cx              ; DX:AX = ticks * 100
        mov     cx, ax          ; CX = divisor low
        mov     bx, dx          ; BX = divisor high (should be 0 for reasonable ticks)
        pop     ax
        pop     dx
        
        ; 32-bit divide DX:AX by CX (assuming BX=0 for ticks < 655)
        div     cx              ; AX = quotient (MHz)
        
        add     sp, 2           ; Clean low word from stack
        jmp     .store_result
        
.use_safe_path:
        ; For 386+ CPUs with very fast speeds, use true 32-bit division
        ; Check if we're on 386+ (has 32-bit operations)
        test    dword ptr [cpu_features], FEATURE_32BIT
        jz      .use_approx_path ; Fall back to approximation on 286
        
        ; True 32-bit division path for 386+
        ; Calculate: (cycles_32 * 1193) / (ticks * 100) using 32-bit DIV
        
        ; Build full 32-bit cycle count in EAX
        push    bx              ; Save ticks
        mov     ax, si          ; AX = low 16 bits of cycles
        db      66h             ; 32-bit operand prefix
        movzx   eax, ax         ; EAX = zero-extended low word
        mov     ax, di          ; AX = high 16 bits of cycles
        db      66h             ; 32-bit operand prefix
        shl     eax, 16         ; Shift high word to upper 16 bits
        mov     ax, si          ; Get low word again
        db      66h             ; 32-bit operand prefix
        or      eax, ax         ; EAX = full 32-bit cycle count
        
        ; Multiply by 1193 for 64-bit result in EDX:EAX
        db      66h             ; 32-bit operand prefix
        mov     edx, eax        ; EDX = cycles
        db      66h             ; 32-bit operand prefix
        mov     eax, 1193       ; EAX = 1193
        db      66h             ; 32-bit operand prefix
        mul     edx             ; EDX:EAX = cycles * 1193
        
        ; Calculate divisor: ticks * 100 in ECX
        pop     bx              ; Restore ticks
        movzx   cx, bx          ; CX = ticks
        db      66h             ; 32-bit operand prefix
        movzx   ecx, cx         ; ECX = ticks (32-bit)
        db      66h, 69h, 0C9h, 64h, 00h, 00h, 00h ; imul ecx, ecx, 100
        
        ; Belt-and-suspenders: ensure divisor is non-zero
        db      66h             ; 32-bit operand prefix
        or      ecx, ecx        ; Check if ECX is zero
        jz      .use_fallback   ; Bail if zero (shouldn't happen with timeouts)
        
        ; Perform 64/32 division: EDX:EAX / ECX
        db      66h             ; 32-bit operand prefix
        div     ecx             ; EAX = MHz, EDX = remainder
        
        ; Result in EAX, low 16 bits go to AX
        ; AX already contains the result
        jmp     .store_result
        
.use_approx_path:
        ; Fallback for 286 or when 32-bit not available
        ; Calculate: (cycles / 100) * 1193 / ticks (less precise)
        mov     ax, si
        mov     dx, di
        mov     cx, 100
        div     cx              ; DX:AX = cycles / 100
        push    dx              ; Save remainder
        mov     cx, 1193
        mul     cx              ; DX:AX = (cycles/100) * 1193
        xor     cx, cx
        div     bx              ; AX = MHz (approximate)
        pop     dx              ; Clean stack
        jmp     .store_result
        
.use_16bit_path:
        ; Simplified path using only low 16 bits for very fast CPUs
        mov     ax, si
        mov     dx, 1193
        mul     dx              ; DX:AX = cycles * 1193
        mov     cx, 100
        div     cx              ; AX = (cycles * 1193) / 100
        xor     dx, dx
        div     bx              ; AX = MHz
        
.store_result:
        ; No vendor-specific adjustments - proper measurement is more accurate
        ; Sanity check the result for retry decision
        cmp     ax, 3           ; Less than 3 MHz?
        jb      .retry_or_fallback
        cmp     ax, 10000       ; More than 10 GHz? (unrealistic)
        ja      .retry_or_fallback
        
        ; Result is reasonable, return it
        jmp     .exit_trial
        
.retry_or_fallback:
        ; Check if we should retry or use fallback
        ; For now, use fallback (retry logic handled at higher level)
        jmp     .use_fallback
        
.use_fallback:
        ; Use typical values based on CPU type
        mov     bl, byte ptr [detected_cpu_type]
        cmp     bl, CPU_8086
        jne     .check_186
        mov     ax, 5           ; 8086: typically 4.77-8 MHz
        jmp     .set_fallback
        
.check_186:
        cmp     bl, CPU_80186
        jne     .check_286
        mov     ax, 8           ; 80186: typically 6-12 MHz
        jmp     .set_fallback
        
.check_286:
        cmp     bl, CPU_80286
        jne     .check_386
        mov     ax, 12          ; 80286: typically 6-20 MHz
        jmp     .set_fallback
        
.check_386:
        cmp     bl, CPU_80386
        jne     .check_486
        mov     ax, 33          ; 80386: typically 16-40 MHz
        jmp     .set_fallback
        
.check_486:
        cmp     bl, CPU_80486
        jne     .check_cpuid
        mov     ax, 66          ; 80486: typically 25-100 MHz
        jmp     .set_fallback
        
.check_cpuid:
        mov     ax, 133         ; Pentium+: typically 60-200+ MHz
        
.set_fallback:
        ; Return the fallback value in AX
        
.exit_trial:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
single_speed_trial ENDP

;-----------------------------------------------------------------------------
; calibrate_loop_overhead - Measure empty loop overhead for subtraction
;
; Input:  None
; Output: loop_overhead_ticks is set
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
calibrate_loop_overhead PROC
        push    bp
        mov     bp, sp
        push    ax
        push    bx
        push    cx
        push    dx
        
        cli                     ; Disable interrupts
        
        ; Save and setup PIT channel 2
        in      al, 61h
        push    ax              ; Save original state
        and     al, 0FCh        ; Clear bits 0-1 (gate=0, speaker=0)
        out     61h, al         ; Force gate low first
        
        ; Program PIT channel 2
        mov     al, 0B0h        ; Channel 2, LSB/MSB, mode 0
        out     43h, al
        mov     ax, 0FFFFh      ; Load count = 65535
        out     42h, al         ; Write LSB
        mov     al, ah          ; Get MSB
        out     42h, al         ; Write MSB
        
        ; Create rising edge on gate to start counter
        in      al, 61h
        or      al, 01h         ; Set gate bit (rising edge starts mode 0)
        out     61h, al
        
        ; Read start count
        mov     al, 80h
        out     43h, al
        in      al, 42h
        mov     bl, al
        in      al, 42h
        mov     bh, al          ; BX = start
        
        ; Empty loop (just the loop instruction)
        mov     cx, 10000
.overhead_loop:
        loop    .overhead_loop
        
        ; Read end count
        mov     al, 80h
        out     43h, al
        in      al, 42h
        mov     dl, al
        in      al, 42h
        mov     dh, al          ; DX = end
        
        ; Restore port 61h
        pop     ax
        out     61h, al
        
        sti                     ; Re-enable interrupts
        
        ; Calculate overhead ticks
        sub     bx, dx
        jnc     .no_wrap
        add     bx, 0FFFFh
        inc     bx
.no_wrap:
        mov     word ptr [loop_overhead_ticks], bx
        
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        pop     bp
        ret
calibrate_loop_overhead ENDP

;-----------------------------------------------------------------------------
; sort_speed_trials - Sort the 5 speed trial results (bubble sort)
;
; Input:  speed_trials array contains 5 measurements
; Output: speed_trials array is sorted in ascending order
; Uses:   AX, BX, CX, SI
;-----------------------------------------------------------------------------
sort_speed_trials PROC
        push    bp
        mov     bp, sp
        push    ax
        push    bx
        push    cx
        push    si
        
        mov     cx, 4           ; 4 passes for 5 elements
.outer_loop:
        push    cx
        mov     si, OFFSET speed_trials
        mov     cx, 4           ; 4 comparisons per pass
        
.inner_loop:
        mov     ax, [si]
        mov     bx, [si+2]
        cmp     ax, bx
        jbe     .no_swap
        ; Swap
        mov     [si], bx
        mov     [si+2], ax
.no_swap:
        add     si, 2
        loop    .inner_loop
        
        pop     cx
        loop    .outer_loop
        
        pop     si
        pop     cx
        pop     bx
        pop     ax
        pop     bp
        ret
sort_speed_trials ENDP

;-----------------------------------------------------------------------------
; calculate_confidence - Calculate confidence based on relative variance
;
; Input:  speed_trials array contains sorted measurements
; Output: speed_confidence is set (0-100%)
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
calculate_confidence PROC
        push    bp
        mov     bp, sp
        push    ax
        push    bx
        push    cx
        push    dx
        
        ; Get median value (3rd of 5)
        mov     bx, word ptr [speed_trials+4]
        
        ; Avoid division by zero
        or      bx, bx
        jz      .low_confidence
        
        ; Calculate spread as (max - min)
        mov     ax, word ptr [speed_trials+8]  ; Max (5th element)
        sub     ax, word ptr [speed_trials]    ; Min (1st element)
        
        ; Calculate relative spread: (spread * 100) / median
        ; This gives us percentage variation
        mov     dx, 100
        mul     dx              ; DX:AX = spread * 100
        div     bx              ; AX = (spread * 100) / median
        
        ; Convert relative spread to confidence
        ; If spread <= 5%, confidence = 100%
        ; If spread >= 50%, confidence = 0%
        ; Linear scale between
        cmp     ax, 5
        jbe     .high_confidence
        cmp     ax, 50
        jae     .low_confidence
        
        ; Calculate confidence = 100 - (spread * 2)
        ; Since spread is already a percentage, we scale it
        shl     ax, 1           ; spread * 2
        mov     bx, 100
        sub     bx, ax
        jns     .store_confidence
        xor     bx, bx          ; Clamp to 0
        jmp     .store_confidence
        
.high_confidence:
        mov     bx, 100
        jmp     .store_confidence
        
.low_confidence:
        mov     bx, 0
        
.store_confidence:
        mov     byte ptr [speed_confidence], bl
        
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        pop     bp
        ret
calculate_confidence ENDP

;-----------------------------------------------------------------------------
; asm_get_cpu_speed - C-callable wrapper to get CPU speed
;
; Input:  None
; Output: AX = CPU speed in MHz
; Uses:   AX
;-----------------------------------------------------------------------------
asm_get_cpu_speed PROC
        ; First detect speed if not already done
        cmp     word ptr [detected_cpu_mhz], 0
        jne     .already_detected
        call    detect_cpu_speed
.already_detected:
        mov     ax, word ptr [detected_cpu_mhz]
        ret
asm_get_cpu_speed ENDP

;-----------------------------------------------------------------------------
; asm_get_speed_confidence - C-callable wrapper to get speed confidence
;
; Input:  None
; Output: AL = Speed confidence (0-100%)
; Uses:   AX
;-----------------------------------------------------------------------------
asm_get_speed_confidence PROC
        mov     al, byte ptr [speed_confidence]
        xor     ah, ah
        ret
asm_get_speed_confidence ENDP

;-----------------------------------------------------------------------------
; asm_has_invariant_tsc - C-callable wrapper to check invariant TSC
;
; Input:  None
; Output: AL = 1 if TSC is invariant, 0 otherwise
; Uses:   AX
;-----------------------------------------------------------------------------
asm_has_invariant_tsc PROC
        mov     al, byte ptr [invariant_tsc]
        xor     ah, ah
        ret
asm_has_invariant_tsc ENDP

;-----------------------------------------------------------------------------
; asm_has_rdtscp - C-callable wrapper to check RDTSCP availability
;
; Input:  None
; Output: AL = 1 if RDTSCP is available, 0 otherwise
; Uses:   AX
;-----------------------------------------------------------------------------
asm_has_rdtscp PROC
        mov     al, byte ptr [has_rdtscp]
        xor     ah, ah
        ret
asm_has_rdtscp ENDP

;-----------------------------------------------------------------------------
; asm_is_hypervisor - C-callable wrapper to check if running under hypervisor
;
; Input:  None
; Output: AL = 1 if running under hypervisor, 0 otherwise
; Uses:   AX
;-----------------------------------------------------------------------------
asm_is_hypervisor PROC
        mov     al, byte ptr [is_hypervisor]
        xor     ah, ah
        ret
asm_is_hypervisor ENDP

_TEXT ENDS

END