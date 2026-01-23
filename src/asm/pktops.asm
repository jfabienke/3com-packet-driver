; @file packet_ops.asm
; @brief Packet copying, checksum, CPU-optimized operations
;
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
;
; This file is part of the 3Com Packet Driver project.
;

.MODEL SMALL
.386

; Packet operation constants
MIN_PACKET_SIZE     EQU 60      ; Minimum Ethernet frame size
MAX_PACKET_SIZE     EQU 1514    ; Maximum Ethernet frame size
ETHER_HEADER_SIZE   EQU 14      ; Ethernet header size
ETHER_CRC_SIZE      EQU 4       ; Ethernet CRC size

; Common packet sizes for fast path optimization
PACKET_SIZE_64      EQU 64      ; Small packet size
PACKET_SIZE_128     EQU 128     ; Medium packet size
PACKET_SIZE_512     EQU 512     ; Large packet size
PACKET_SIZE_1518    EQU 1518    ; Maximum Ethernet packet

; CPU optimization flags
; OPT_8086 is the baseline - 8086/8088 compatible code only
; Higher flags indicate additional instruction sets available
OPT_8086            EQU 0       ; 8086/8088 baseline (no 186+ instructions)
OPT_NONE            EQU 0       ; Alias for OPT_8086 (backward compatibility)
OPT_16BIT           EQU 1       ; 16-bit optimizations (186+: PUSHA, INS/OUTS, shifts)
OPT_32BIT           EQU 2       ; 32-bit optimizations (386+: EAX, etc.)
OPT_PUSHA           EQU 4       ; PUSHA/POPA support (186+)
OPT_486_ENHANCED    EQU 8       ; 486+ enhanced instructions (BSWAP, CMPXCHG)
OPT_PENTIUM         EQU 16      ; Pentium pipeline optimizations

; DMA alignment requirements
DMA_ALIGNMENT_4     EQU 4       ; 4-byte DMA alignment
DMA_ALIGNMENT_16    EQU 16      ; 16-byte cache line alignment
DMA_ALIGNMENT_32    EQU 32      ; 32-byte cache line alignment

; Checksum types
CHECKSUM_IP         EQU 0       ; IP header checksum
CHECKSUM_UDP        EQU 1       ; UDP checksum
CHECKSUM_TCP        EQU 2       ; TCP checksum

; Data segment
_DATA SEGMENT
        ASSUME  DS:_DATA

; CPU optimization settings
current_cpu_opt     db OPT_NONE     ; Current optimization level
patch_enabled       db 0            ; Code patching enabled flag
cpu_family          db 0            ; CPU family (0=8086, 1=286, 2=386, 3=486, 4=Pentium)

; Function pointer tables for CPU-specific routines
copy_func_table     dw 5 dup(0)     ; Copy function pointers
checksum_func_table dw 5 dup(0)     ; Checksum function pointers
memset_func_table   dw 5 dup(0)     ; Memory set function pointers

; Performance measurement
tsc_available       db 0            ; TSC (Time Stamp Counter) available flag
performance_cycles  dd 0            ; Last measured cycles
last_tsc_high       dd 0            ; High 32 bits of last TSC reading
last_tsc_low        dd 0            ; Low 32 bits of last TSC reading

; Fast path statistics
packets_64          dd 0            ; Small packets (64 bytes)
packets_128         dd 0            ; Medium packets (65-128 bytes)
packets_512         dd 0            ; Large packets (129-512 bytes)
packets_1518        dd 0            ; Maximum packets (513-1518 bytes)
fast_path_hits      dd 0            ; Fast path cache hits

; Statistics
packets_copied      dd 0            ; Total packets copied
bytes_copied        dd 0            ; Total bytes copied
checksum_errors     dw 0            ; Checksum error count
alignment_fixes     dw 0            ; Number of alignment fixes applied

; Patch locations for 32-bit optimizations
patch_copy_16       dw 0            ; Address to patch for 16-bit copy
patch_copy_32       dw 0            ; Address to patch for 32-bit copy
patch_checksum_16   dw 0            ; Address to patch for 16-bit checksum
patch_checksum_32   dw 0            ; Address to patch for 32-bit checksum

; 386+ Performance optimization state variables
; Set during init based on CPU type
use_66_prefix       db 0            ; Use 0x66 operand-size prefix (386+)
pipeline_enabled    db 0            ; Pipeline optimizations enabled (486+)
agi_avoidance       db 0            ; AGI stall avoidance enabled (Pentium)
memory_ops_optimized dd 0           ; Counter for optimized memory operations

; Thresholds for optimization paths
PERF_32BIT_THRESHOLD    EQU 32      ; Minimum bytes for 32-bit optimization
PERF_PIPELINE_THRESHOLD EQU 64      ; Minimum bytes for pipeline optimization

_DATA ENDS

; Code segment
_TEXT SEGMENT
        ASSUME  CS:_TEXT, DS:_DATA

; Public function exports  
PUBLIC packet_ops_init
PUBLIC packet_copy_fast
PUBLIC packet_copy_optimized
PUBLIC packet_copy_64_bytes
PUBLIC packet_copy_128_bytes 
PUBLIC packet_copy_512_bytes
PUBLIC packet_copy_1518_bytes
PUBLIC packet_checksum_ip
PUBLIC packet_checksum_tcp_udp
PUBLIC packet_ops_receive
PUBLIC packet_ops_patch_32bit
PUBLIC set_packet_optimization
PUBLIC get_performance_cycles
PUBLIC read_timestamp_counter
PUBLIC init_function_tables
PUBLIC packet_memset_optimized
PUBLIC packet_copy_aligned_dma
PUBLIC analyze_copy_parameters
PUBLIC packet_copy_32bit_optimized
PUBLIC packet_copy_pipeline_optimized
PUBLIC packet_copy_cache_optimized
PUBLIC packet_copy_pentium_agi_aware

; External references
EXTRN get_cpu_features:PROC         ; From cpu_detect.asm
EXTRN hardware_read_packet:PROC     ; From hardware.asm
; perf_copy_* functions implemented locally below (no longer external)

;-----------------------------------------------------------------------------
; packet_ops_init - Initialize packet operations
;
; Input:  None
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
packet_ops_init PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Initialize optimization settings based on CPU
        ; Get CPU features from Phase 1 CPU detection
        call    get_cpu_features
        mov     bx, ax

        ; Determine CPU family and optimization level
        mov     al, OPT_NONE
        mov     byte ptr [cpu_family], 0    ; Assume 8086
        
        ; Check for 286+ features (PUSHA/POPA)
        test    bx, 1
        jz      init_check_32bit
        or      al, OPT_PUSHA or OPT_16BIT
        mov     byte ptr [cpu_family], 1    ; 286

init_check_32bit:
        ; Check for 386+ features (32-bit operations)
        test    bx, 2
        jz      init_check_486
        or      al, OPT_32BIT
        mov     byte ptr [cpu_family], 2    ; 386

        ; Apply 32-bit optimizations
        call    packet_ops_patch_32bit

init_check_486:
        ; Check for 486+ enhanced features
        test    bx, 4
        jz      init_check_pentium
        or      al, OPT_486_ENHANCED
        mov     byte ptr [cpu_family], 3    ; 486

init_check_pentium:
        ; Check for Pentium features (TSC)
        test    bx, 0100h               ; TSC feature from CPUID
        jz      init_no_tsc
        or      al, OPT_PENTIUM
        mov     byte ptr [cpu_family], 4    ; Pentium
        mov     byte ptr [tsc_available], 1

init_no_tsc:
        mov     [current_cpu_opt], al

        ; Initialize 386+ performance optimization flags
        mov     byte ptr [use_66_prefix], 0
        mov     byte ptr [pipeline_enabled], 0
        mov     byte ptr [agi_avoidance], 0
        mov     dword ptr [memory_ops_optimized], 0

        ; Enable 32-bit optimizations on 386+
        test    al, OPT_32BIT
        jz      .init_func_tables
        mov     byte ptr [use_66_prefix], 1

        ; Enable pipeline optimizations on 486+
        test    al, OPT_486_ENHANCED
        jz      .init_func_tables
        mov     byte ptr [pipeline_enabled], 1

        ; Enable AGI avoidance on Pentium
        test    al, OPT_PENTIUM
        jz      init_func_tables
        mov     byte ptr [agi_avoidance], 1

init_func_tables:
        ; Initialize function tables based on CPU capabilities
        call    init_function_tables

        ; Clear all statistics
        mov     dword ptr [packets_copied], 0
        mov     dword ptr [bytes_copied], 0
        mov     word ptr [checksum_errors], 0
        mov     word ptr [alignment_fixes], 0
        mov     dword ptr [packets_64], 0
        mov     dword ptr [packets_128], 0
        mov     dword ptr [packets_512], 0
        mov     dword ptr [packets_1518], 0
        mov     dword ptr [fast_path_hits], 0
        mov     dword ptr [performance_cycles], 0

        ; Initialize TSC baseline if available
        cmp     byte ptr [tsc_available], 1
        jne     init_no_tsc_init
        call    read_timestamp_counter  ; Sets last_tsc_high/low

init_no_tsc_init:
        ; Success
        mov     ax, 0

        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
packet_ops_init ENDP

;-----------------------------------------------------------------------------
; packet_copy_fast - Fast packet copying routine
; Uses CPU-optimized instructions based on detected capabilities
;
; Input:  DS:SI = source, ES:DI = destination, CX = byte count
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
packet_copy_fast PROC
        push    bp
        mov     bp, sp
        push    bx
        push    dx
        push    si
        push    di

        ; Validate parameters
        ; DS:SI = source, ES:DI = destination, CX = byte count
        cmp     cx, 0
        je      pcf_success         ; Nothing to copy
        cmp     cx, MAX_PACKET_SIZE
        ja      pcf_invalid_size

        ; Ensure direction flag is correct
        cld

        ; Check CPU optimization level and select optimal copy routine
        mov     bl, [current_cpu_opt]
        test    bl, OPT_32BIT
        jnz     pcf_copy_32bit

        ; Use 16-bit optimized copy (286+)
        test    bl, OPT_16BIT
        jnz     pcf_copy_16bit

        ; Use basic 8-bit copy (8086/8088)
        jmp     pcf_copy_8bit

pcf_copy_32bit:
        ; 32-bit optimized copy for 386+
        ; Check for 4-byte alignment for optimal performance
        mov     ax, si
        or      ax, di
        test    ax, 3               ; Check if both pointers are 4-byte aligned
        jnz     pcf_copy_16bit      ; Fall back to 16-bit if not aligned

patch_copy_32_location:
        ; 32-bit copy with MOVSD - patched at runtime for 386+
        mov     dx, cx              ; Save original count
        shr     cx, 2               ; Convert to dword count
        jz      pcf_copy_32_remainder

        ; Use 0x66 prefix for 32-bit operation in real mode
        db      66h                 ; 32-bit operand size prefix
        rep     movsd               ; Copy dwords

pcf_copy_32_remainder:
        mov     cx, dx
        and     cx, 3               ; Get remainder bytes
        jz      pcf_success
        rep     movsb               ; Copy remaining bytes
        jmp     pcf_success

pcf_copy_16bit:
        ; 16-bit optimized copy for 286+
        ; Check for word alignment
        mov     ax, si
        or      ax, di
        test    ax, 1               ; Check if both pointers are word-aligned
        jnz     pcf_copy_8bit       ; Fall back to byte copy if not aligned

        mov     dx, cx              ; Save original count
        shr     cx, 1               ; Convert to word count
        jz      pcf_copy_16_remainder
        rep     movsw               ; Copy words

pcf_copy_16_remainder:
        test    dx, 1               ; Check if odd byte count
        jz      pcf_success
        movsb                       ; Copy the last byte
        jmp     pcf_success

pcf_copy_8bit:
        ; Basic byte copy for 8086/8088
        rep     movsb
        jmp     pcf_success

pcf_invalid_size:
        mov     ax, 1
        jmp     pcf_exit

pcf_success:
        ; Update statistics
        inc     dword ptr [packets_copied]
        mov     ax, [bp+8]          ; Original CX value from stack
        add     dword ptr [bytes_copied], eax
        mov     ax, 0

pcf_exit:
        pop     di
        pop     si
        pop     dx
        pop     bx
        pop     bp
        ret
packet_copy_fast ENDP

;-----------------------------------------------------------------------------
; packet_copy_optimized - Optimized packet copy with alignment
; Provides best performance by handling memory alignment
;
; Input:  DS:SI = source, ES:DI = destination, CX = byte count
; Output: AX = 0 for success, non-zero for error
; Uses:   All registers
;-----------------------------------------------------------------------------
packet_copy_optimized PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Enhanced alignment checking and optimization selection
        call    analyze_copy_parameters
        test    ax, ax
        jnz     pco_use_optimized_path

        ; Fall back to standard fast copy for unoptimal parameters
        call    packet_copy_fast
        jmp     pco_copy_complete

pco_use_optimized_path:
        ; AX contains optimization strategy:
        ; 1 = aligned 32-bit copy
        ; 2 = pipeline-optimized copy
        ; 3 = cache-line optimized copy
        ; 4 = Pentium AGI-aware copy

        cmp     al, 1
        je      pco_copy_aligned_32bit
        cmp     al, 2
        je      pco_copy_pipeline_opt
        cmp     al, 3
        je      pco_copy_cache_opt
        cmp     al, 4
        je      pco_copy_pentium_opt

        ; Default fallback
        call    packet_copy_fast
        jmp     pco_copy_complete

pco_copy_aligned_32bit:
        call    packet_copy_32bit_optimized
        jmp     pco_copy_complete

pco_copy_pipeline_opt:
        call    packet_copy_pipeline_optimized
        jmp     pco_copy_complete

pco_copy_cache_opt:
        call    packet_copy_cache_optimized
        jmp     pco_copy_complete

pco_copy_pentium_opt:
        call    packet_copy_pentium_agi_aware

pco_copy_complete:

        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
packet_copy_optimized ENDP

;-----------------------------------------------------------------------------
; packet_checksum_ip - Calculate IP header checksum
; Implements standard Internet checksum algorithm
;
; Input:  DS:SI = IP header, CX = header length in bytes
; Output: AX = checksum (0 = valid, non-zero = invalid)
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
packet_checksum_ip PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Implement Internet checksum algorithm per RFC 1071
        ; DS:SI = data pointer, CX = length in bytes
        
        ; Initialize checksum accumulator
        xor     dx, dx              ; DX = 16-bit checksum accumulator
        xor     di, di              ; DI = high 16 bits for 32-bit operations

        ; Handle zero length
        cmp     cx, 0
        je      cksum_checksum_done

        ; Check for 32-bit optimization
        mov     bl, [current_cpu_opt]
        test    bl, OPT_32BIT
        jnz     cksum_checksum_32bit

        ; 16-bit checksum implementation
        ; Process pairs of bytes as 16-bit words
        shr     cx, 1               ; Convert to word count
        jnc     cksum_checksum_16_loop   ; No odd byte
        
        ; Handle odd byte at the end
        push    cx
        mov     cx, [bp+4]          ; Get original length
        and     cx, 1               ; Check if odd
        jz      cksum_no_odd_byte

        ; Add the odd byte (in high byte position)
        ; Note: CX is 1 here, so we need to add SI + original_length - 1
        push    bx
        mov     bx, [bp+4]          ; Get original length
        dec     bx                  ; bx = original_length - 1
        mov     al, [si + bx]       ; Get last byte using BX (valid addressing)
        pop     bx
        xor     ah, ah
        shl     ax, 8               ; Move to high byte
        add     dx, ax
        adc     dx, 0

cksum_no_odd_byte:
        pop     cx

cksum_checksum_16_loop:
        cmp     cx, 0
        je      cksum_checksum_done

        lodsw                       ; Load word from DS:SI
        add     dx, ax              ; Add to checksum
        adc     dx, 0               ; Add carry
        dec     cx
        jmp     cksum_checksum_16_loop

cksum_checksum_32bit:
patch_checksum_32_location:
        ; 32-bit optimized checksum for 386+
        ; Use 32-bit accumulator for better performance
        mov     bx, cx              ; Save original byte count

        ; Process 4 bytes at a time when possible
        shr     cx, 2               ; Convert to dword count
        jz      cksum_checksum_32_words

cksum_checksum_32_dword_loop:
        ; Use 0x66 prefix for 32-bit operation
        db      66h                 ; 32-bit operand size prefix
        lodsd                       ; Load dword from DS:SI
        db      66h                 ; 32-bit operand size prefix
        add     dx, ax              ; Add low word
        db      66h                 ; 32-bit operand size prefix
        shr     ax, 16              ; Get high word
        adc     dx, ax              ; Add high word with carry
        adc     dx, 0               ; Add final carry
        dec     cx
        jnz     cksum_checksum_32_dword_loop

cksum_checksum_32_words:
        ; Handle remaining words
        mov     cx, bx
        and     cx, 3               ; Get remainder bytes
        shr     cx, 1               ; Convert to words
        jz      cksum_checksum_32_byte

cksum_checksum_32_word_loop:
        lodsw
        add     dx, ax
        adc     dx, 0
        dec     cx
        jnz     cksum_checksum_32_word_loop

cksum_checksum_32_byte:
        ; Handle final odd byte if present
        test    bx, 1
        jz      cksum_checksum_done

        mov     al, [si]
        xor     ah, ah
        shl     ax, 8               ; Move to high byte position
        add     dx, ax
        adc     dx, 0

cksum_checksum_done:
        ; Fold carries and take one's complement
        mov     ax, dx

        ; Add any remaining carries
cksum_fold_loop:
        mov     bx, ax
        shr     bx, 16              ; Get high 16 bits
        and     ax, 0FFFFh          ; Keep low 16 bits
        add     ax, bx              ; Add high to low
        jnc     cksum_fold_done     ; No carry, we're done
        inc     ax                  ; Add the carry
        jmp     cksum_fold_loop     ; Check for more carries

cksum_fold_done:
        ; Take one's complement
        not     ax

        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
packet_checksum_ip ENDP

;-----------------------------------------------------------------------------
; packet_checksum_tcp_udp - Calculate TCP/UDP checksum with pseudo-header
;
; Input:  DS:SI = packet data, CX = data length, 
;         ES:DI = pseudo-header (12 bytes)
; Output: AX = checksum
; Uses:   All registers
;-----------------------------------------------------------------------------
packet_checksum_tcp_udp PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; TCP/UDP checksum with pseudo-header implementation

        ; Initialize checksum with pseudo-header
        push    ds
        push    es
        pop     ds
        mov     si, di              ; DS:SI = pseudo-header
        mov     cx, 12              ; Pseudo-header is 12 bytes
        call    packet_checksum_ip
        mov     dx, ax              ; Save pseudo-header checksum
        pop     ds

        ; Add actual TCP/UDP header and data checksum
        mov     cx, [bp-4]          ; Restore original CX
        call    packet_checksum_ip
        add     dx, ax              ; Combine checksums
        adc     dx, 0

        ; Final fold and complement
        mov     ax, dx
        shr     dx, 16
        add     ax, dx
        adc     ax, 0
        not     ax

        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
packet_checksum_tcp_udp ENDP

;-----------------------------------------------------------------------------
; packet_ops_receive - Handle received packet processing
; Called from interrupt handler when packet is received
;
; Input:  AL = NIC index
; Output: AX = 0 for success, non-zero for error
; Uses:   All registers
;-----------------------------------------------------------------------------
packet_ops_receive PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es

        ; Enhanced packet receive processing
        ; AL = NIC index
        ; Returns: AX = 0 for success, non-zero for error
        
        mov     bl, al              ; Save NIC index
        
        ; Call hardware layer to read packet
        call    hardware_read_packet
        cmp     ax, 0
        jne     por_read_error

        ; At this point, packet data should be in a buffer
        ; The hardware layer should have provided:
        ; - Buffer pointer in ES:DI
        ; - Packet length in CX

        ; Validate minimum frame size
        cmp     cx, 64              ; ETH_MIN_FRAME
        jb      por_frame_too_small
        cmp     cx, 1518            ; ETH_MAX_FRAME
        ja      por_frame_too_large

        ; Quick Ethernet header validation
        ; Check that we have at least 14 bytes for header
        cmp     cx, 14
        jb      por_invalid_header

        ; Extract EtherType from frame (bytes 12-13)
        mov     si, di              ; Point to frame data
        add     si, 12              ; Point to EtherType field
        mov     ax, es:[si]         ; Load EtherType (network byte order)

        ; Convert from network to host byte order
        xchg    ah, al              ; Swap bytes

        ; Check for common protocols
        cmp     ax, 0800h           ; IP protocol
        je      por_valid_protocol
        cmp     ax, 0806h           ; ARP protocol
        je      por_valid_protocol
        cmp     ax, 86DDh           ; IPv6 protocol
        je      por_valid_protocol

        ; Unknown protocol - still process but log
        ; Fall through to valid_protocol

por_valid_protocol:
        ; Call C packet processing function
        ; packet_receive_process(buffer, length, nic_index)
        push    cx                  ; Length
        push    bx                  ; NIC index (saved earlier)
        push    es                  ; Buffer segment
        push    di                  ; Buffer offset

        ; Note: This is a simplified calling convention
        ; Real implementation would follow proper C calling convention
        call    packet_receive_process
        add     sp, 8               ; Clean up stack

        ; Success
        mov     ax, 0
        jmp     por_exit

por_frame_too_small:
        inc     word ptr [checksum_errors]
        mov     ax, 2
        jmp     por_exit

por_frame_too_large:
        inc     word ptr [checksum_errors]
        mov     ax, 3
        jmp     por_exit

por_invalid_header:
        inc     word ptr [checksum_errors]
        mov     ax, 4
        jmp     por_exit

por_read_error:
        inc     word ptr [checksum_errors]
        mov     ax, 1
        jmp     por_exit

por_exit:
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
packet_ops_receive ENDP

;-----------------------------------------------------------------------------
; packet_ops_patch_32bit - Apply 32-bit optimizations to packet operations
; Called by CPU detection module when 386+ features are available
;
; Input:  None
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX, SI, DI, ES
;-----------------------------------------------------------------------------
packet_ops_patch_32bit PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es

        ; Apply 32-bit optimizations for 386+ processors
        ; This enables 0x66 prefix usage for 32-bit operations in real mode
        
        ; Check if already patched
        cmp     byte ptr [patch_enabled], 1
        je      pop32_already_patched

        ; Verify we're on a 386+ processor
        call    get_cpu_features
        test    ax, 2               ; Check for 32-bit capability
        jz      pop32_not_386

        ; Enable 32-bit optimization flag
        or      byte ptr [current_cpu_opt], OPT_32BIT

        ; Store patch locations for debugging/verification
        mov     word ptr [patch_copy_32], OFFSET patch_copy_32_location
        mov     word ptr [patch_checksum_32], OFFSET patch_checksum_32_location

        ; The actual patches are already in place using db 66h directives
        ; These enable 32-bit operand size in real mode for 386+
        ; No runtime patching needed - the code is conditionally executed

        ; Verify 32-bit instructions work by testing a simple operation
        push    eax                 ; This will fail on < 386
        mov     eax, 12345678h      ; Test 32-bit immediate
        cmp     eax, 12345678h      ; Test 32-bit comparison
        pop     eax
        jne     pop32_patch_failed

        ; Mark as patched and optimized
        mov     byte ptr [patch_enabled], 1

        ; Success
        mov     ax, 0
        jmp     pop32_exit

pop32_not_386:
        ; CPU doesn't support 32-bit operations
        mov     ax, 2
        jmp     pop32_exit

pop32_patch_failed:
        ; 32-bit test failed
        and     byte ptr [current_cpu_opt], NOT OPT_32BIT
        mov     ax, 3
        jmp     pop32_exit

pop32_already_patched:
        mov     ax, 0
        jmp     pop32_exit

pop32_exit:
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
packet_ops_patch_32bit ENDP

;-----------------------------------------------------------------------------
; set_packet_optimization - Set packet operation optimization level
;
; Input:  AL = optimization flags
; Output: AX = 0 for success, non-zero for error
; Uses:   AX
;-----------------------------------------------------------------------------
set_packet_optimization PROC
        push    bp
        mov     bp, sp
        push    bx

        ; Validate and apply new optimization settings
        ; AL = new optimization flags
        
        ; Validate optimization flags
        mov     bl, al
        and     bl, 0F8h            ; Check for invalid bits (should be 0)
        jnz     spo_invalid_flags

        ; Check if 32-bit optimization is requested
        test    al, OPT_32BIT
        jz      spo_no_32bit_check

        ; Verify CPU supports 32-bit operations
        call    get_cpu_features
        test    ax, 2               ; Check for 32-bit capability
        jz      spo_unsupported_32bit

spo_no_32bit_check:
        ; Store new optimization level
        mov     [current_cpu_opt], al

        ; Apply optimizations if 32-bit was enabled
        test    al, OPT_32BIT
        jz      spo_optimization_set

        ; Enable 32-bit patches
        call    packet_ops_patch_32bit

spo_optimization_set:
        ; Success
        mov     ax, 0
        jmp     spo_exit

spo_invalid_flags:
        mov     ax, 1
        jmp     spo_exit

spo_unsupported_32bit:
        mov     ax, 2
        jmp     spo_exit

spo_exit:
        pop     bx
        pop     bp
        ret
set_packet_optimization ENDP

;-----------------------------------------------------------------------------
; packet_copy_aligned - Aligned packet copy for maximum performance
; Handles memory alignment automatically for best performance
;
; Input:  DS:SI = source, ES:DI = destination, CX = byte count
; Output: AX = 0 for success, non-zero for error
; Uses:   All registers
;-----------------------------------------------------------------------------
packet_copy_aligned PROC
        push    bp
        mov     bp, sp
        push    bx
        push    dx
        
        ; Save original parameters
        mov     bx, cx              ; Save count
        
        ; Check alignment of source and destination
        mov     ax, si
        or      ax, di
        
        ; Check CPU optimization level
        mov     dl, [current_cpu_opt]
        test    dl, OPT_32BIT
        jz      pca_check_16bit_align

        ; 32-bit alignment check
        test    ax, 3               ; Check 4-byte alignment
        jz      pca_copy_32bit_aligned

        ; Not 32-bit aligned, align to 32-bit boundary
        call    pca_align_to_32bit
        jmp     pca_copy_32bit_bulk

pca_check_16bit_align:
        test    dl, OPT_16BIT
        jz      pca_copy_unaligned

        ; 16-bit alignment check
        test    ax, 1               ; Check word alignment
        jz      pca_copy_16bit_aligned

        ; Not word aligned, copy first byte
        movsb
        dec     cx

pca_copy_16bit_aligned:
        shr     cx, 1               ; Convert to words
        rep     movsw

        ; Handle remainder
        test    bx, 1
        jz      pca_aligned_done
        movsb
        jmp     pca_aligned_done

pca_copy_32bit_aligned:
pca_copy_32bit_bulk:
        ; Optimized 32-bit copy
        mov     dx, cx
        shr     cx, 2               ; Convert to dwords

        ; Use 32-bit copy
        db      66h                 ; 32-bit prefix
        rep     movsd

        ; Handle remainder
        mov     cx, dx
        and     cx, 3
        rep     movsb
        jmp     pca_aligned_done

pca_copy_unaligned:
        ; Basic unaligned copy
        rep     movsb

pca_aligned_done:
        mov     ax, 0               ; Success

        pop     dx
        pop     bx
        pop     bp
        ret

; Helper: Align to 32-bit boundary
pca_align_to_32bit:
        ; Copy bytes until SI is 32-bit aligned
        mov     ax, si
        and     ax, 3               ; Get misalignment
        jz      pca_align_32_done   ; Already aligned

        mov     dx, 4
        sub     dx, ax              ; Bytes needed to align
        cmp     dx, cx              ; Don't copy more than we have
        jbe     pca_align_32_copy
        mov     dx, cx

pca_align_32_copy:
        sub     cx, dx              ; Adjust remaining count
        mov     ax, dx              ; Copy alignment bytes

pca_align_32_loop:
        movsb
        dec     ax
        jnz     pca_align_32_loop

pca_align_32_done:
        ret

packet_copy_aligned ENDP

;-----------------------------------------------------------------------------
; Optimized packet copy routines for specific sizes (fast paths)
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; packet_copy_64_bytes - Optimized copy for 64-byte packets
; Input:  DS:SI = source, ES:DI = destination
; Output: AX = 0 for success
; Uses:   AX, CX, SI, DI
;-----------------------------------------------------------------------------
packet_copy_64_bytes PROC
        push    bp
        mov     bp, sp
        push    cx

        ; Update statistics
        inc     dword ptr [packets_64]
        inc     dword ptr [fast_path_hits]

        ; Check CPU optimization level
        mov     al, [current_cpu_opt]
        test    al, OPT_32BIT
        jnz     pc64_copy_32bit

        ; 16-bit copy: 32 words = 64 bytes
        mov     cx, 32
        cld
        rep     movsw
        jmp     pc64_copy_done

pc64_copy_32bit:
        ; 32-bit copy: 16 dwords = 64 bytes
        mov     cx, 16
        cld
        db      66h                 ; 32-bit prefix
        rep     movsd

pc64_copy_done:
        mov     ax, 0               ; Success

        pop     cx
        pop     bp
        ret
packet_copy_64_bytes ENDP

;-----------------------------------------------------------------------------
; packet_copy_128_bytes - Optimized copy for 128-byte packets
; Input:  DS:SI = source, ES:DI = destination
; Output: AX = 0 for success
; Uses:   AX, CX, SI, DI
;-----------------------------------------------------------------------------
packet_copy_128_bytes PROC
        push    bp
        mov     bp, sp
        push    cx

        ; Update statistics
        inc     dword ptr [packets_128]
        inc     dword ptr [fast_path_hits]

        ; Check CPU optimization level
        mov     al, [current_cpu_opt]
        test    al, OPT_32BIT
        jnz     pc128_copy_32bit

        ; 16-bit copy: 64 words = 128 bytes
        mov     cx, 64
        cld
        rep     movsw
        jmp     pc128_copy_done

pc128_copy_32bit:
        ; 32-bit copy: 32 dwords = 128 bytes
        mov     cx, 32
        cld
        db      66h                 ; 32-bit prefix
        rep     movsd

pc128_copy_done:
        mov     ax, 0               ; Success

        pop     cx
        pop     bp
        ret
packet_copy_128_bytes ENDP

;-----------------------------------------------------------------------------
; packet_copy_512_bytes - Optimized copy for 512-byte packets
; Input:  DS:SI = source, ES:DI = destination  
; Output: AX = 0 for success
; Uses:   AX, CX, SI, DI
;-----------------------------------------------------------------------------
packet_copy_512_bytes PROC
        push    bp
        mov     bp, sp
        push    cx

        ; Update statistics
        inc     dword ptr [packets_512]
        inc     dword ptr [fast_path_hits]

        ; Check CPU optimization and alignment
        mov     al, [current_cpu_opt]
        test    al, OPT_PENTIUM
        jnz     pc512_copy_pentium
        test    al, OPT_32BIT
        jnz     pc512_copy_32bit

        ; 16-bit copy: 256 words = 512 bytes
        mov     cx, 256
        cld
        rep     movsw
        jmp     pc512_copy_done

pc512_copy_32bit:
        ; 32-bit copy: 128 dwords = 512 bytes
        mov     cx, 128
        cld
        db      66h                 ; 32-bit prefix
        rep     movsd
        jmp     pc512_copy_done

pc512_copy_pentium:
        ; Pentium optimized: unroll loop to avoid AGI stalls
        mov     cx, 16              ; 16 iterations of 32 bytes each
        cld

pc512_copy_pentium_loop:
        ; Copy 32 bytes per iteration (8 dwords)
        db      66h                 ; 32-bit prefix
        movsd
        db      66h
        movsd
        db      66h
        movsd
        db      66h
        movsd
        db      66h
        movsd
        db      66h
        movsd
        db      66h
        movsd
        db      66h
        movsd
        dec     cx
        jnz     pc512_copy_pentium_loop

pc512_copy_done:
        mov     ax, 0               ; Success

        pop     cx
        pop     bp
        ret
packet_copy_512_bytes ENDP

;-----------------------------------------------------------------------------
; packet_copy_1518_bytes - Optimized copy for maximum Ethernet frame
; Input:  DS:SI = source, ES:DI = destination
; Output: AX = 0 for success  
; Uses:   AX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
packet_copy_1518_bytes PROC
        push    bp
        mov     bp, sp
        push    cx
        push    dx

        ; Update statistics
        inc     dword ptr [packets_1518]
        inc     dword ptr [fast_path_hits]

        ; Check CPU optimization
        mov     al, [current_cpu_opt]
        test    al, OPT_PENTIUM
        jnz     pc1518_copy_pentium
        test    al, OPT_32BIT
        jnz     pc1518_copy_32bit

        ; 16-bit copy: 759 words = 1518 bytes
        mov     cx, 759
        cld
        rep     movsw
        jmp     pc1518_copy_done

pc1518_copy_32bit:
        ; 32-bit copy: 379 dwords + 2 bytes
        mov     cx, 379
        cld
        db      66h                 ; 32-bit prefix
        rep     movsd
        ; Copy remaining 2 bytes
        movsw
        jmp     pc1518_copy_done

pc1518_copy_pentium:
        ; Pentium optimized with cache-friendly block copies
        mov     dx, 47              ; 47 iterations of 32 bytes = 1504 bytes
        cld

pc1518_copy_pentium_loop:
        ; Copy 32 bytes per iteration (8 dwords)
        db      66h
        movsd
        db      66h
        movsd
        db      66h
        movsd
        db      66h
        movsd
        db      66h
        movsd
        db      66h
        movsd
        db      66h
        movsd
        db      66h
        movsd
        dec     dx
        jnz     pc1518_copy_pentium_loop

        ; Copy remaining 14 bytes (1518 - 1504)
        mov     cx, 7
        rep     movsw

pc1518_copy_done:
        mov     ax, 0               ; Success

        pop     dx
        pop     cx
        pop     bp
        ret
packet_copy_1518_bytes ENDP

;-----------------------------------------------------------------------------
; packet_memset_optimized - CPU-optimized memory set operation
; Input:  ES:DI = destination, AL = fill byte, CX = count
; Output: AX = 0 for success
; Uses:   AX, CX, DI
;-----------------------------------------------------------------------------
packet_memset_optimized PROC
        push    bp
        mov     bp, sp
        push    bx
        push    dx
        
        ; Save fill byte
        mov     ah, al              ; Create word pattern
        mov     bl, [current_cpu_opt]
        
        ; Check for 32-bit optimization
        test    bl, OPT_32BIT
        jz      pms_memset_16bit

        ; Create 32-bit fill pattern
        mov     dx, ax              ; Save 16-bit pattern
        shl     eax, 16             ; Shift to high word
        mov     ax, dx              ; Complete 32-bit pattern

        ; 32-bit optimized set
        mov     bx, cx              ; Save original count
        shr     cx, 2               ; Convert to dwords
        cld
        db      66h                 ; 32-bit prefix
        rep     stosd

        ; Handle remainder bytes
        mov     cx, bx
        and     cx, 3
        mov     al, dl              ; Restore byte value
        rep     stosb
        jmp     pms_memset_done

pms_memset_16bit:
        ; 16-bit optimized set
        mov     dx, cx              ; Save original count
        shr     cx, 1               ; Convert to words
        cld
        rep     stosw

        ; Handle odd byte
        test    dx, 1
        jz      pms_memset_done
        stosb

pms_memset_done:
        mov     ax, 0               ; Success
        
        pop     dx
        pop     bx
        pop     bp
        ret
packet_memset_optimized ENDP

;-----------------------------------------------------------------------------
; packet_copy_aligned_dma - DMA-aligned packet copy for 3C515-TX
; Input:  DS:SI = source, ES:DI = destination, CX = byte count
; Output: AX = 0 for success, 1 for alignment error
; Uses:   All registers
;-----------------------------------------------------------------------------
packet_copy_aligned_dma PROC
        push    bp
        mov     bp, sp
        push    bx
        push    dx
        
        ; Validate DMA alignment requirements
        ; 3C515-TX requires 4-byte alignment for bus mastering
        mov     ax, di
        test    ax, 3               ; Check 4-byte alignment
        jz      dma_aligned

        ; Alignment error
        inc     word ptr [alignment_fixes]
        mov     ax, 1
        jmp     dma_copy_exit

dma_aligned:
        ; Check for cache line alignment for better performance
        test    ax, 31              ; 32-byte cache line alignment
        jnz     dma_copy_basic

        ; Cache-aligned DMA copy (optimal for Pentium+)
        mov     bl, [current_cpu_opt]
        test    bl, OPT_PENTIUM
        jz      dma_copy_32bit

        ; Pentium cache-optimized copy
        mov     dx, cx
        shr     cx, 5               ; 32-byte blocks
        jz      dma_copy_remainder

dma_copy_cache_loop:
        ; Copy 32 bytes (8 dwords) with optimal Pentium pairing
        db      66h
        movsd
        db      66h
        movsd
        db      66h
        movsd
        db      66h
        movsd
        db      66h
        movsd
        db      66h
        movsd
        db      66h
        movsd
        db      66h
        movsd
        dec     cx
        jnz     dma_copy_cache_loop

        ; Handle remainder
        mov     cx, dx
        and     cx, 31
        jmp     dma_copy_remainder

dma_copy_32bit:
        ; Standard 32-bit aligned copy
        mov     dx, cx
        shr     cx, 2               ; Convert to dwords
        cld
        db      66h                 ; 32-bit prefix
        rep     movsd

        ; Copy remainder bytes
        mov     cx, dx
        and     cx, 3

dma_copy_remainder:
        rep     movsb
        jmp     dma_copy_success

dma_copy_basic:
        ; Basic aligned copy
        call    packet_copy_fast

dma_copy_success:
        mov     ax, 0               ; Success

dma_copy_exit:
        pop     dx
        pop     bx
        pop     bp
        ret
packet_copy_aligned_dma ENDP

;-----------------------------------------------------------------------------
; Performance measurement functions
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; read_timestamp_counter - Read CPU Time Stamp Counter (Pentium+)
; Output: EDX:EAX = TSC value, stores in last_tsc_high/low
; Uses:   EAX, EDX
;-----------------------------------------------------------------------------
read_timestamp_counter PROC
        push    bp
        mov     bp, sp

        ; Check if TSC is available
        cmp     byte ptr [tsc_available], 1
        jne     rtsc_no_tsc

        ; Read Time Stamp Counter (RDTSC instruction)
        db      0fh, 31h            ; RDTSC opcode

        ; Store the result
        mov     dword ptr [last_tsc_low], eax
        mov     dword ptr [last_tsc_high], edx

        pop     bp
        ret

rtsc_no_tsc:
        ; TSC not available, return 0
        xor     eax, eax
        xor     edx, edx
        pop     bp
        ret
read_timestamp_counter ENDP

;-----------------------------------------------------------------------------
; get_performance_cycles - Calculate cycles elapsed since last measurement
; Output: EAX = cycles elapsed (0 if TSC not available)
; Uses:   EAX, EDX
;-----------------------------------------------------------------------------
get_performance_cycles PROC
        push    bp
        mov     bp, sp
        push    ebx
        push    ecx

        ; Check if TSC is available
        cmp     byte ptr [tsc_available], 1
        jne     gpc_no_perf_tsc

        ; Read current TSC
        db      0fh, 31h            ; RDTSC opcode

        ; Calculate difference from last reading
        sub     eax, dword ptr [last_tsc_low]
        sbb     edx, dword ptr [last_tsc_high]

        ; For simplicity, only return low 32 bits
        ; (High 32 bits in EDX are usually 0 for short measurements)
        mov     dword ptr [performance_cycles], eax

        ; Update last TSC values for next measurement
        mov     dword ptr [last_tsc_low], eax
        mov     dword ptr [last_tsc_high], edx

        pop     ecx
        pop     ebx
        pop     bp
        ret

gpc_no_perf_tsc:
        xor     eax, eax            ; No performance data available
        pop     ecx
        pop     ebx
        pop     bp
        ret
get_performance_cycles ENDP

;-----------------------------------------------------------------------------
; init_function_tables - Initialize CPU-specific function pointer tables
; Input:  None (uses current_cpu_opt)
; Output: AX = 0 for success
; Uses:   AX, BX, SI
;-----------------------------------------------------------------------------
init_function_tables PROC
        push    bp
        mov     bp, sp
        push    bx
        push    si
        
        ; Initialize copy function table based on CPU family
        mov     si, OFFSET copy_func_table
        mov     bl, [cpu_family]
        
        ; 8086 functions (index 0)
        mov     word ptr [si+0], OFFSET copy_8086
        ; 286 functions (index 1) 
        mov     word ptr [si+2], OFFSET copy_286
        ; 386 functions (index 2)
        mov     word ptr [si+4], OFFSET copy_386
        ; 486 functions (index 3)
        mov     word ptr [si+6], OFFSET copy_486
        ; Pentium functions (index 4)
        mov     word ptr [si+8], OFFSET copy_pentium
        
        ; Initialize checksum function table
        mov     si, OFFSET checksum_func_table
        mov     word ptr [si+0], OFFSET checksum_8086
        mov     word ptr [si+2], OFFSET checksum_286
        mov     word ptr [si+4], OFFSET checksum_386
        mov     word ptr [si+6], OFFSET checksum_486
        mov     word ptr [si+8], OFFSET checksum_pentium
        
        ; Initialize memset function table
        mov     si, OFFSET memset_func_table
        mov     word ptr [si+0], OFFSET memset_8086
        mov     word ptr [si+2], OFFSET memset_286
        mov     word ptr [si+4], OFFSET memset_386
        mov     word ptr [si+6], OFFSET memset_486
        mov     word ptr [si+8], OFFSET memset_pentium
        
        mov     ax, 0               ; Success
        
        pop     si
        pop     bx
        pop     bp
        ret
init_function_tables ENDP

;-----------------------------------------------------------------------------
; CPU-specific implementation stubs (to be filled in with optimal routines)
;-----------------------------------------------------------------------------
copy_8086 PROC
        rep     movsb               ; Basic 8-bit copy
        ret
copy_8086 ENDP

copy_286 PROC
        ; Use PUSHA/POPA for efficient register save/restore
        pusha
        cld
        shr     cx, 1               ; Word copy
        rep     movsw
        adc     cx, cx              ; Handle odd byte
        rep     movsb
        popa
        ret
copy_286 ENDP

copy_386 PROC
        ; 32-bit optimized copy
        cld
        mov     edx, ecx
        shr     ecx, 2
        db      66h
        rep     movsd
        mov     ecx, edx
        and     ecx, 3
        rep     movsb
        ret
copy_386 ENDP

copy_486 PROC
        ; 486 optimized with better pipeline usage
        call    copy_386            ; Use 386 routine for now
        ret
copy_486 ENDP

copy_pentium PROC
        ; Pentium optimized with pairing considerations
        call    copy_386            ; Use 386 routine for now
        ret
copy_pentium ENDP

checksum_8086 PROC
        ; Basic 8086 checksum
        ret
checksum_8086 ENDP

checksum_286 PROC
        ; 286 optimized checksum
        ret
checksum_286 ENDP

checksum_386 PROC
        ; 386+ optimized checksum
        ret
checksum_386 ENDP

checksum_486 PROC
        ; 486+ optimized checksum  
        ret
checksum_486 ENDP

checksum_pentium PROC
        ; Pentium optimized checksum
        ret
checksum_pentium ENDP

memset_8086 PROC
        rep     stosb
        ret
memset_8086 ENDP

memset_286 PROC
        mov     ah, al
        shr     cx, 1
        rep     stosw
        adc     cx, cx
        rep     stosb
        ret
memset_286 ENDP

memset_386 PROC
        mov     ah, al
        mov     dx, ax
        shl     eax, 16
        mov     ax, dx
        mov     edx, ecx
        shr     ecx, 2
        db      66h
        rep     stosd
        mov     ecx, edx
        and     ecx, 3
        rep     stosb
        ret
memset_386 ENDP

memset_486 PROC
        call    memset_386
        ret
memset_486 ENDP

memset_pentium PROC
        call    memset_386
        ret
memset_pentium ENDP

; Export additional functions
PUBLIC packet_copy_aligned

;-----------------------------------------------------------------------------
; Enhanced Copy Analysis and Optimization Functions
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; analyze_copy_parameters - Analyze copy parameters and select optimal strategy
;
; Input:  DS:SI = source, ES:DI = destination, CX = byte count
; Output: AX = optimization strategy (0=none, 1=32bit, 2=pipeline, 3=cache, 4=pentium)
; Uses:   AX, BX, DX
;-----------------------------------------------------------------------------
analyze_copy_parameters PROC
        push    bp
        mov     bp, sp
        push    bx
        push    dx
        
        ; Start with no optimization
        mov     ax, 0

        ; Check minimum size threshold (must be >= 16 bytes for optimization)
        cmp     cx, 16
        jb      acp_no_optimization

        ; Check CPU capabilities
        mov     bl, [current_cpu_opt]

        ; Analyze alignment
        mov     dx, si
        or      dx, di              ; Combine source and dest addresses

        ; Test for various alignment levels
        test    dx, 3               ; 4-byte aligned?
        jnz     acp_check_word_align

        ; 32-bit aligned - check for advanced optimizations
        test    bl, OPT_PENTIUM
        jz      acp_check_486_opt

        ; Pentium optimization available
        cmp     cx, 64              ; Large enough for Pentium optimization?
        jb      acp_use_32bit_copy

        ; Check for 8-byte alignment (optimal for Pentium)
        test    dx, 7
        jnz     acp_use_pipeline_copy
        mov     ax, 4               ; Pentium AGI-aware copy
        jmp     acp_analysis_done

acp_check_486_opt:
        test    bl, OPT_486_ENHANCED
        jz      acp_use_32bit_copy

        ; 486+ pipeline optimization
        cmp     cx, 32              ; Large enough for pipeline optimization?
        jb      acp_use_32bit_copy

        ; Check for cache line alignment
        test    dx, 31              ; 32-byte cache line aligned?
        jnz     acp_use_pipeline_copy

        cmp     cx, 128             ; Large enough for cache optimization?
        jb      acp_use_pipeline_copy
        mov     ax, 3               ; Cache-line optimized copy
        jmp     acp_analysis_done

acp_use_pipeline_copy:
        mov     ax, 2               ; Pipeline-optimized copy
        jmp     acp_analysis_done

acp_use_32bit_copy:
        test    bl, OPT_32BIT
        jz      acp_check_word_align
        mov     ax, 1               ; 32-bit aligned copy
        jmp     acp_analysis_done

acp_check_word_align:
        test    dx, 1               ; Word aligned?
        jnz     acp_no_optimization

        ; Word-aligned but not dword-aligned
        test    bl, OPT_16BIT
        jz      acp_no_optimization

        ; Use 16-bit optimization (handled by standard fast copy)
        mov     ax, 0               ; No special optimization needed

acp_no_optimization:
        mov     ax, 0

acp_analysis_done:
        pop     dx
        pop     bx
        pop     bp
        ret
analyze_copy_parameters ENDP

;-----------------------------------------------------------------------------
; packet_copy_32bit_optimized - 32-bit aligned optimized copy
;
; Input:  DS:SI = source, ES:DI = destination, CX = byte count
; Output: AX = 0 for success
; Uses:   All registers (optimized for 386+)
;-----------------------------------------------------------------------------
packet_copy_32bit_optimized PROC
        push    bp
        mov     bp, sp
        
        ; Use the performance-optimized 32-bit copy with 0x66 prefix
        call    perf_copy_with_66_prefix
        
        ; Update packet-specific statistics
        inc     dword ptr [memory_ops_optimized]
        
        pop     bp
        ret
packet_copy_32bit_optimized ENDP

;-----------------------------------------------------------------------------
; packet_copy_pipeline_optimized - Pipeline-optimized copy for 486+
;
; Input:  DS:SI = source, ES:DI = destination, CX = byte count
; Output: AX = 0 for success
; Uses:   All registers (optimized for 486+ pipeline)
;-----------------------------------------------------------------------------
packet_copy_pipeline_optimized PROC
        push    bp
        mov     bp, sp
        
        ; Use the performance-optimized pipeline copy
        call    perf_copy_pipeline_optimized
        
        ; Update packet-specific statistics
        inc     dword ptr [memory_ops_optimized]
        
        pop     bp
        ret
packet_copy_pipeline_optimized ENDP

;-----------------------------------------------------------------------------
; packet_copy_cache_optimized - Cache-line optimized copy
;
; Input:  DS:SI = source, ES:DI = destination, CX = byte count
; Output: AX = 0 for success
; Uses:   All registers (optimized for cache efficiency)
;-----------------------------------------------------------------------------
packet_copy_cache_optimized PROC
        push    bp
        mov     bp, sp
        push    bx
        push    dx
        
        ; Specialized cache-line friendly copy for network packets
        ; Most network packets are either 64, 128, 512, or 1518 bytes

        ; Check for common packet sizes and use optimized routines
        cmp     cx, 64
        je      pcco_copy_64_cache
        cmp     cx, 128
        je      pcco_copy_128_cache
        cmp     cx, 512
        je      pcco_copy_512_cache
        cmp     cx, 1518
        je      pcco_copy_1518_cache

        ; General cache-optimized copy
        call    general_cache_copy
        jmp     pcco_cache_copy_done

pcco_copy_64_cache:
        call    packet_copy_64_bytes
        jmp     pcco_cache_copy_done

pcco_copy_128_cache:
        call    packet_copy_128_bytes
        jmp     pcco_cache_copy_done

pcco_copy_512_cache:
        call    packet_copy_512_bytes
        jmp     pcco_cache_copy_done

pcco_copy_1518_cache:
        call    packet_copy_1518_bytes
        jmp     pcco_cache_copy_done

pcco_cache_copy_done:
        ; Update statistics
        inc     dword ptr [memory_ops_optimized]

        mov     ax, 0               ; Success

        pop     dx
        pop     bx
        pop     bp
        ret

; Helper function for general cache-optimized copy
general_cache_copy:
        ; Copy in 32-byte cache-line chunks
        mov     bx, cx
        shr     cx, 5               ; 32-byte chunks
        jz      gcc_cache_remainder

gcc_cache_chunk_loop:
        ; Copy 32 bytes with cache-friendly pattern
        mov     eax, ds:[si]
        mov     edx, ds:[si+4]
        mov     es:[di], eax
        mov     es:[di+4], edx

        mov     eax, ds:[si+8]
        mov     edx, ds:[si+12]
        mov     es:[di+8], eax
        mov     es:[di+12], edx

        mov     eax, ds:[si+16]
        mov     edx, ds:[si+20]
        mov     es:[di+16], eax
        mov     es:[di+20], edx

        mov     eax, ds:[si+24]
        mov     edx, ds:[si+28]
        mov     es:[di+24], eax
        mov     es:[di+28], edx

        add     si, 32
        add     di, 32
        dec     cx
        jnz     gcc_cache_chunk_loop

gcc_cache_remainder:
        mov     cx, bx
        and     cx, 31              ; Remaining bytes
        rep     movsb
        ret

packet_copy_cache_optimized ENDP

;-----------------------------------------------------------------------------
; packet_copy_pentium_agi_aware - Pentium-optimized copy with AGI stall avoidance
;
; Input:  DS:SI = source, ES:DI = destination, CX = byte count
; Output: AX = 0 for success
; Uses:   All registers (optimized for Pentium dual pipeline + AGI avoidance)
;-----------------------------------------------------------------------------
packet_copy_pentium_agi_aware PROC
        push    bp
        mov     bp, sp
        
        ; Use the performance-optimized Pentium copy with AGI avoidance
        call    perf_copy_pentium_optimized
        
        ; Update packet-specific statistics
        inc     dword ptr [memory_ops_optimized]
        
        pop     bp
        ret
packet_copy_pentium_agi_aware ENDP

;-----------------------------------------------------------------------------
; Enhanced Fast Path Packet Size Optimizations
;-----------------------------------------------------------------------------

; Override the existing packet size functions with cache-aware implementations
; packet_copy_64_bytes, packet_copy_128_bytes, packet_copy_512_bytes, and 
; packet_copy_1518_bytes are already implemented above, but let's enhance them
; with better cache behavior

;-----------------------------------------------------------------------------
; packet_copy_64_bytes_enhanced - Cache-optimized 64-byte copy
;-----------------------------------------------------------------------------
packet_copy_64_bytes_enhanced PROC
        push    bp
        mov     bp, sp
        push    cx
        push    eax
        push    ebx

        ; Update statistics
        inc     dword ptr [packets_64]
        inc     dword ptr [fast_path_hits]

        ; Check CPU optimization level for best strategy
        mov     al, [current_cpu_opt]
        test    al, OPT_PENTIUM
        jnz     pc64e_copy_pentium
        test    al, OPT_486_ENHANCED
        jnz     pc64e_copy_486
        test    al, OPT_32BIT
        jnz     pc64e_copy_32bit

        ; 16-bit copy: 32 words = 64 bytes
        mov     cx, 32
        cld
        rep     movsw
        jmp     pc64e_copy_done

pc64e_copy_32bit:
        ; 32-bit copy: 16 dwords = 64 bytes
        mov     cx, 16
        cld
        db      66h                 ; 32-bit prefix
        rep     movsd
        jmp     pc64e_copy_done

pc64e_copy_486:
        ; 486 pipeline-optimized: unroll for U/V pipe pairing
        cld
        ; Copy 64 bytes as 8x8-byte blocks for optimal pairing
        mov     eax, ds:[si]        ; U-pipe
        mov     ebx, ds:[si+4]      ; V-pipe (pairs)
        mov     es:[di], eax        ; U-pipe
        mov     es:[di+4], ebx      ; V-pipe (pairs)

        mov     eax, ds:[si+8]      ; U-pipe
        mov     ebx, ds:[si+12]     ; V-pipe (pairs)
        mov     es:[di+8], eax      ; U-pipe
        mov     es:[di+12], ebx     ; V-pipe (pairs)

        ; Continue pattern for remaining 48 bytes...
        add     si, 16
        add     di, 16

        ; Use rep movsd for remaining 48 bytes (12 dwords)
        mov     cx, 12
        db      66h
        rep     movsd
        jmp     pc64e_copy_done

pc64e_copy_pentium:
        ; Pentium dual-pipeline optimized with AGI avoidance
        cld

        ; Copy 64 bytes as 8x8-byte operations with optimal pairing
        mov     eax, ds:[si]        ; U-pipe
        mov     ebx, ds:[si+4]      ; V-pipe (pairs)
        add     si, 8               ; Calculate next address (avoid AGI)
        nop                         ; Fill slot
        mov     es:[di], eax        ; U-pipe
        mov     es:[di+4], ebx      ; V-pipe (pairs)
        add     di, 8               ; U-pipe

        ; Repeat pattern 7 more times...
        mov     cx, 7               ; Remaining 8-byte blocks
pc64e_pentium_64_loop:
        mov     eax, ds:[si]        ; U-pipe
        mov     ebx, ds:[si+4]      ; V-pipe (pairs)
        add     si, 8               ; Avoid AGI
        nop                         ; Fill slot
        mov     es:[di], eax        ; U-pipe
        mov     es:[di+4], ebx      ; V-pipe (pairs)
        add     di, 8               ; U-pipe
        dec     cx                  ; V-pipe (pairs)
        jnz     pc64e_pentium_64_loop

pc64e_copy_done:
        mov     ax, 0               ; Success

        pop     ebx
        pop     eax
        pop     cx
        pop     bp
        ret
packet_copy_64_bytes_enhanced ENDP

;=============================================================================
; 386+ PERFORMANCE-OPTIMIZED COPY FUNCTIONS
;=============================================================================
; These functions are called by packet_copy_*_optimized wrappers above.
; They implement CPU-specific memory copy optimizations:
; - perf_copy_with_66_prefix: 386+ 32-bit copy using REP MOVSD
; - perf_copy_pipeline_optimized: 486+ dual-pipe U/V pairing
; - perf_copy_pentium_optimized: Pentium AGI stall avoidance
;=============================================================================

;-----------------------------------------------------------------------------
; perf_copy_with_66_prefix - 386+ optimized copy using 32-bit operations
;
; Uses 0x66 operand-size prefix to perform 32-bit MOVSD in 16-bit segment.
; Provides 2x throughput vs 16-bit REP MOVSW for aligned data.
;
; Input:  DS:SI = source, ES:DI = destination, CX = byte count
; Output: AX = 0 for success
; Uses:   EAX, ECX, ESI, EDI (all preserved via stack)
;-----------------------------------------------------------------------------
perf_copy_with_66_prefix PROC
        push    bp
        mov     bp, sp

        ; Check if 32-bit optimization is enabled
        cmp     byte ptr [use_66_prefix], 1
        jne     p66_fallback_copy

        ; Check minimum size threshold (overhead not worth it for small copies)
        cmp     cx, PERF_32BIT_THRESHOLD
        jb      p66_fallback_copy

        ; Save 32-bit registers
        db      66h                     ; OPERAND32
        push    eax
        db      66h
        push    ecx
        db      66h
        push    esi
        db      66h
        push    edi

        ; Zero-extend CX to ECX
        db      66h
        movzx   ecx, cx

        ; Save total count for remainder handling
        db      66h
        push    ecx

        ; Calculate DWORD count
        db      66h
        shr     ecx, 2                  ; ECX = byte_count / 4
        jz      p66_remainder

        ; Perform 32-bit copy: REP MOVSD
        cld
        db      66h                     ; 32-bit operand prefix
        rep movsd                       ; Copy ECX DWORDs (4 bytes each)

p66_remainder:
        ; Handle 0-3 remaining bytes
        db      66h
        pop     ecx                     ; Restore original count
        db      66h
        and     ecx, 3                  ; Remainder bytes (0-3)
        jz      p66_done
        rep movsb                       ; Copy remaining bytes

p66_done:
        ; Update statistics
        inc     dword ptr [memory_ops_optimized]

        ; Restore 32-bit registers
        db      66h
        pop     edi
        db      66h
        pop     esi
        db      66h
        pop     ecx
        db      66h
        pop     eax

        mov     ax, 0                   ; Success
        jmp     p66_exit

p66_fallback_copy:
        ; Fall back to 16-bit REP MOVSW
        push    dx
        mov     dx, cx
        shr     cx, 1
        cld
        rep movsw
        test    dx, 1
        jz      p66_fb_done
        movsb
p66_fb_done:
        pop     dx
        mov     ax, 0

p66_exit:
        pop     bp
        ret
perf_copy_with_66_prefix ENDP

;-----------------------------------------------------------------------------
; perf_copy_pipeline_optimized - 486+ pipeline-optimized copy
;
; Implements U/V pipe pairing for maximum instruction throughput on 486+.
; Copies 8 bytes per iteration with paired instructions that execute in
; parallel on the dual-issue pipeline.
;
; Input:  DS:SI = source, ES:DI = destination, CX = byte count
; Output: AX = 0 for success
; Uses:   EAX, EBX, ECX, EDX, ESI, EDI
;-----------------------------------------------------------------------------
perf_copy_pipeline_optimized PROC
        push    bp
        mov     bp, sp

        ; Check if pipeline optimization is enabled
        cmp     byte ptr [pipeline_enabled], 1
        jne     pp_standard_copy

        ; Check size threshold (need enough data to benefit from unrolling)
        cmp     cx, PERF_PIPELINE_THRESHOLD
        jb      pp_standard_copy

        ; Save 32-bit registers
        db      66h
        push    eax
        db      66h
        push    ebx
        db      66h
        push    ecx
        db      66h
        push    edx
        db      66h
        push    esi
        db      66h
        push    edi

        ; Zero-extend CX to ECX
        db      66h
        movzx   ecx, cx
        db      66h
        mov     edx, ecx                ; Save original count

        ; Check alignment for cache-friendly 32-byte blocks
        mov     ax, si
        or      ax, di
        test    ax, 31                  ; 32-byte cache line alignment
        jz      pp_cache_aligned_copy

        ;-----------------------------------------------------------------------
        ; Unaligned path: 8-byte blocks with U/V pipe pairing
        ;-----------------------------------------------------------------------
        db      66h
        shr     ecx, 3                  ; 8-byte blocks
        jz      pp_unaligned_remainder

        cld
pp_pipeline_loop:
        ; U/V pipe pairing for 486+
        db      66h
        mov     eax, ds:[si]            ; U-pipe: load dword 1
        db      66h
        mov     ebx, ds:[si+4]          ; V-pipe: load dword 2 (pairs)
        db      66h
        mov     es:[di], eax            ; U-pipe: store dword 1
        db      66h
        mov     es:[di+4], ebx          ; V-pipe: store dword 2 (pairs)

        add     si, 8                   ; U-pipe: advance source
        add     di, 8                   ; V-pipe: advance dest (pairs)
        db      66h
        dec     ecx                     ; U-pipe
        jnz     pp_pipeline_loop

pp_unaligned_remainder:
        db      66h
        mov     ecx, edx                ; Restore original count
        db      66h
        and     ecx, 7                  ; Remainder (0-7 bytes)
        rep movsb
        jmp     pp_done

        ;-----------------------------------------------------------------------
        ; Cache-aligned path: 32-byte blocks for maximum performance
        ;-----------------------------------------------------------------------
pp_cache_aligned_copy:
        db      66h
        shr     ecx, 5                  ; 32-byte cache line blocks
        jz      pp_cache_remainder

        cld
pp_cache_loop:
        ; Copy full 32-byte cache line with optimal pairing
        db      66h
        mov     eax, ds:[si]            ; Bytes 0-3
        db      66h
        mov     ebx, ds:[si+4]          ; Bytes 4-7
        db      66h
        mov     es:[di], eax
        db      66h
        mov     es:[di+4], ebx

        db      66h
        mov     eax, ds:[si+8]          ; Bytes 8-11
        db      66h
        mov     ebx, ds:[si+12]         ; Bytes 12-15
        db      66h
        mov     es:[di+8], eax
        db      66h
        mov     es:[di+12], ebx

        db      66h
        mov     eax, ds:[si+16]         ; Bytes 16-19
        db      66h
        mov     ebx, ds:[si+20]         ; Bytes 20-23
        db      66h
        mov     es:[di+16], eax
        db      66h
        mov     es:[di+20], ebx

        db      66h
        mov     eax, ds:[si+24]         ; Bytes 24-27
        db      66h
        mov     ebx, ds:[si+28]         ; Bytes 28-31
        db      66h
        mov     es:[di+24], eax
        db      66h
        mov     es:[di+28], ebx

        add     si, 32
        add     di, 32
        db      66h
        dec     ecx
        jnz     pp_cache_loop

pp_cache_remainder:
        db      66h
        mov     ecx, edx                ; Restore original count
        db      66h
        and     ecx, 31                 ; Remainder (0-31 bytes)
        rep movsb

pp_done:
        ; Update statistics
        inc     dword ptr [memory_ops_optimized]

        ; Restore 32-bit registers
        db      66h
        pop     edi
        db      66h
        pop     esi
        db      66h
        pop     edx
        db      66h
        pop     ecx
        db      66h
        pop     ebx
        db      66h
        pop     eax

        mov     ax, 0
        jmp     pp_exit

pp_standard_copy:
        ; Fall back to basic 32-bit copy
        call    perf_copy_with_66_prefix

pp_exit:
        pop     bp
        ret
perf_copy_pipeline_optimized ENDP

;-----------------------------------------------------------------------------
; perf_copy_pentium_optimized - Pentium-specific copy with AGI avoidance
;
; Implements Address Generation Interlock (AGI) avoidance for Pentium.
; On Pentium, modifying a register used for addressing in the previous
; instruction causes a 1-cycle stall. This routine carefully orders
; instructions to avoid this:
;   - Calculate next address AFTER loading from current address
;   - Insert NOPs or unrelated instructions between address calc and use
;
; Input:  DS:SI = source, ES:DI = destination, CX = byte count
; Output: AX = 0 for success
; Uses:   EAX, EBX, ECX, EDX, ESI, EDI
;-----------------------------------------------------------------------------
perf_copy_pentium_optimized PROC
        push    bp
        mov     bp, sp

        ; Check if Pentium AGI avoidance is enabled
        cmp     byte ptr [agi_avoidance], 1
        jne     pent_fallback

        ; Save 32-bit registers
        db      66h
        push    eax
        db      66h
        push    ebx
        db      66h
        push    ecx
        db      66h
        push    edx
        db      66h
        push    esi
        db      66h
        push    edi

        ; Zero-extend and save count
        db      66h
        movzx   ecx, cx
        db      66h
        mov     edx, ecx

        ; Check 8-byte alignment for optimal access
        test    si, 7
        jnz     pent_use_pipeline
        test    di, 7
        jnz     pent_use_pipeline

        ;-----------------------------------------------------------------------
        ; AGI-free 8-byte aligned copy
        ;-----------------------------------------------------------------------
        db      66h
        shr     ecx, 3                  ; 8-byte blocks
        jz      pent_remainder

        cld
pent_agi_loop:
        ; Load from current address BEFORE calculating next
        db      66h
        mov     eax, ds:[si]            ; U-pipe: load from SI
        db      66h
        mov     ebx, ds:[si+4]          ; V-pipe: load from SI+4 (pairs)

        ; Now calculate next source address (AGI-safe: not using SI yet)
        add     si, 8                   ; Advance source pointer

        ; NOP to fill execution slot and ensure no AGI on next iteration
        ; (The DEC/JNZ will execute here, masking the AGI window)

        ; Store to destination
        db      66h
        mov     es:[di], eax            ; U-pipe: store to DI
        db      66h
        mov     es:[di+4], ebx          ; V-pipe: store to DI+4 (pairs)

        ; Calculate next dest address
        add     di, 8                   ; U-pipe: advance dest
        db      66h
        dec     ecx                     ; V-pipe: decrement counter (pairs)
        jnz     pent_agi_loop

pent_remainder:
        db      66h
        mov     ecx, edx                ; Restore original count
        db      66h
        and     ecx, 7                  ; Remainder (0-7 bytes)
        rep movsb
        jmp     pent_done

pent_use_pipeline:
        ; Restore registers and fall through to pipeline copy
        db      66h
        pop     edi
        db      66h
        pop     esi
        db      66h
        pop     edx
        db      66h
        pop     ecx
        db      66h
        pop     ebx
        db      66h
        pop     eax
        pop     bp
        jmp     perf_copy_pipeline_optimized

pent_done:
        ; Update statistics
        inc     dword ptr [memory_ops_optimized]

        ; Restore 32-bit registers
        db      66h
        pop     edi
        db      66h
        pop     esi
        db      66h
        pop     edx
        db      66h
        pop     ecx
        db      66h
        pop     ebx
        db      66h
        pop     eax

        mov     ax, 0
        jmp     pent_exit

pent_fallback:
        ; Use pipeline-optimized copy if AGI avoidance not available
        call    perf_copy_pipeline_optimized

pent_exit:
        pop     bp
        ret
perf_copy_pentium_optimized ENDP

_TEXT ENDS

END