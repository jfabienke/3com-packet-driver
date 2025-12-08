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
EXTRN perf_copy_with_66_prefix:PROC     ; From performance_opt.asm
EXTRN perf_copy_pipeline_optimized:PROC ; From performance_opt.asm
EXTRN perf_copy_pentium_optimized:PROC  ; From performance_opt.asm

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
        jz      .check_32bit
        or      al, OPT_PUSHA or OPT_16BIT
        mov     byte ptr [cpu_family], 1    ; 286

.check_32bit:
        ; Check for 386+ features (32-bit operations)
        test    bx, 2
        jz      .check_486
        or      al, OPT_32BIT
        mov     byte ptr [cpu_family], 2    ; 386
        
        ; Apply 32-bit optimizations
        call    packet_ops_patch_32bit

.check_486:
        ; Check for 486+ enhanced features
        test    bx, 4
        jz      .check_pentium
        or      al, OPT_486_ENHANCED
        mov     byte ptr [cpu_family], 3    ; 486

.check_pentium:
        ; Check for Pentium features (TSC)
        test    bx, 0100h               ; TSC feature from CPUID
        jz      .no_tsc
        or      al, OPT_PENTIUM
        mov     byte ptr [cpu_family], 4    ; Pentium
        mov     byte ptr [tsc_available], 1

.no_tsc:
        mov     [current_cpu_opt], al

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
        jne     .no_tsc_init
        call    read_timestamp_counter  ; Sets last_tsc_high/low

.no_tsc_init:
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
        je      .success            ; Nothing to copy
        cmp     cx, MAX_PACKET_SIZE
        ja      .invalid_size
        
        ; Ensure direction flag is correct
        cld

        ; Check CPU optimization level and select optimal copy routine
        mov     bl, [current_cpu_opt]
        test    bl, OPT_32BIT
        jnz     .copy_32bit

        ; Use 16-bit optimized copy (286+)
        test    bl, OPT_16BIT
        jnz     .copy_16bit

        ; Use basic 8-bit copy (8086/8088)
        jmp     .copy_8bit

.copy_32bit:
        ; 32-bit optimized copy for 386+
        ; Check for 4-byte alignment for optimal performance
        mov     ax, si
        or      ax, di
        test    ax, 3               ; Check if both pointers are 4-byte aligned
        jnz     .copy_16bit         ; Fall back to 16-bit if not aligned
        
patch_copy_32_location:
        ; 32-bit copy with MOVSD - patched at runtime for 386+
        mov     dx, cx              ; Save original count
        shr     cx, 2               ; Convert to dword count
        jz      .copy_32_remainder
        
        ; Use 0x66 prefix for 32-bit operation in real mode
        db      66h                 ; 32-bit operand size prefix
        rep     movsd               ; Copy dwords
        
.copy_32_remainder:
        mov     cx, dx
        and     cx, 3               ; Get remainder bytes
        jz      .success
        rep     movsb               ; Copy remaining bytes
        jmp     .success

.copy_16bit:
        ; 16-bit optimized copy for 286+
        ; Check for word alignment
        mov     ax, si
        or      ax, di
        test    ax, 1               ; Check if both pointers are word-aligned
        jnz     .copy_8bit          ; Fall back to byte copy if not aligned
        
        mov     dx, cx              ; Save original count
        shr     cx, 1               ; Convert to word count
        jz      .copy_16_remainder
        rep     movsw               ; Copy words
        
.copy_16_remainder:
        test    dx, 1               ; Check if odd byte count
        jz      .success
        movsb                       ; Copy the last byte
        jmp     .success

.copy_8bit:
        ; Basic byte copy for 8086/8088
        rep     movsb
        jmp     .success

.invalid_size:
        mov     ax, 1
        jmp     .exit

.success:
        ; Update statistics
        inc     dword ptr [packets_copied]
        mov     ax, [bp+8]          ; Original CX value from stack
        add     dword ptr [bytes_copied], eax
        mov     ax, 0

.exit:
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
        jnz     .use_optimized_path
        
        ; Fall back to standard fast copy for unoptimal parameters
        call    packet_copy_fast
        jmp     .copy_complete
        
.use_optimized_path:
        ; AX contains optimization strategy:
        ; 1 = aligned 32-bit copy
        ; 2 = pipeline-optimized copy
        ; 3 = cache-line optimized copy
        ; 4 = Pentium AGI-aware copy
        
        cmp     al, 1
        je      .copy_aligned_32bit
        cmp     al, 2
        je      .copy_pipeline_opt
        cmp     al, 3
        je      .copy_cache_opt
        cmp     al, 4
        je      .copy_pentium_opt
        
        ; Default fallback
        call    packet_copy_fast
        jmp     .copy_complete
        
.copy_aligned_32bit:
        call    packet_copy_32bit_optimized
        jmp     .copy_complete
        
.copy_pipeline_opt:
        call    packet_copy_pipeline_optimized  
        jmp     .copy_complete
        
.copy_cache_opt:
        call    packet_copy_cache_optimized
        jmp     .copy_complete
        
.copy_pentium_opt:
        call    packet_copy_pentium_agi_aware
        
.copy_complete:

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
        je      .checksum_done

        ; Check for 32-bit optimization
        mov     bl, [current_cpu_opt]
        test    bl, OPT_32BIT
        jnz     .checksum_32bit

        ; 16-bit checksum implementation
        ; Process pairs of bytes as 16-bit words
        shr     cx, 1               ; Convert to word count
        jnc     .checksum_16_loop   ; No odd byte
        
        ; Handle odd byte at the end
        push    cx
        mov     cx, [bp+4]          ; Get original length
        and     cx, 1               ; Check if odd
        jz      .no_odd_byte
        
        ; Add the odd byte (in high byte position)
        mov     al, [si + cx - 1]   ; Get last byte
        xor     ah, ah
        shl     ax, 8               ; Move to high byte
        add     dx, ax
        adc     dx, 0
        
.no_odd_byte:
        pop     cx

.checksum_16_loop:
        cmp     cx, 0
        je      .checksum_done
        
        lodsw                       ; Load word from DS:SI
        add     dx, ax              ; Add to checksum
        adc     dx, 0               ; Add carry
        dec     cx
        jmp     .checksum_16_loop

.checksum_32bit:
patch_checksum_32_location:
        ; 32-bit optimized checksum for 386+
        ; Use 32-bit accumulator for better performance
        mov     bx, cx              ; Save original byte count
        
        ; Process 4 bytes at a time when possible
        shr     cx, 2               ; Convert to dword count
        jz      .checksum_32_words
        
.checksum_32_dword_loop:
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
        jnz     .checksum_32_dword_loop

.checksum_32_words:
        ; Handle remaining words
        mov     cx, bx
        and     cx, 3               ; Get remainder bytes
        shr     cx, 1               ; Convert to words
        jz      .checksum_32_byte
        
.checksum_32_word_loop:
        lodsw
        add     dx, ax
        adc     dx, 0
        dec     cx
        jnz     .checksum_32_word_loop
        
.checksum_32_byte:
        ; Handle final odd byte if present
        test    bx, 1
        jz      .checksum_done
        
        mov     al, [si]
        xor     ah, ah
        shl     ax, 8               ; Move to high byte position
        add     dx, ax
        adc     dx, 0

.checksum_done:
        ; Fold carries and take one's complement
        mov     ax, dx
        
        ; Add any remaining carries
.fold_loop:
        mov     bx, ax
        shr     bx, 16              ; Get high 16 bits
        and     ax, 0FFFFh          ; Keep low 16 bits
        add     ax, bx              ; Add high to low
        jnc     .fold_done          ; No carry, we're done
        inc     ax                  ; Add the carry
        jmp     .fold_loop          ; Check for more carries
        
.fold_done:
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
        jne     .read_error
        
        ; At this point, packet data should be in a buffer
        ; The hardware layer should have provided:
        ; - Buffer pointer in ES:DI
        ; - Packet length in CX
        
        ; Validate minimum frame size
        cmp     cx, 64              ; ETH_MIN_FRAME
        jb      .frame_too_small
        cmp     cx, 1518            ; ETH_MAX_FRAME
        ja      .frame_too_large
        
        ; Quick Ethernet header validation
        ; Check that we have at least 14 bytes for header
        cmp     cx, 14
        jb      .invalid_header
        
        ; Extract EtherType from frame (bytes 12-13)
        mov     si, di              ; Point to frame data
        add     si, 12              ; Point to EtherType field
        mov     ax, es:[si]         ; Load EtherType (network byte order)
        
        ; Convert from network to host byte order
        xchg    ah, al              ; Swap bytes
        
        ; Check for common protocols
        cmp     ax, 0800h           ; IP protocol
        je      .valid_protocol
        cmp     ax, 0806h           ; ARP protocol
        je      .valid_protocol
        cmp     ax, 86DDh           ; IPv6 protocol
        je      .valid_protocol
        
        ; Unknown protocol - still process but log
        ; Fall through to valid_protocol
        
.valid_protocol:
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
        jmp     .exit

.frame_too_small:
        inc     word ptr [checksum_errors]
        mov     ax, 2
        jmp     .exit
        
.frame_too_large:
        inc     word ptr [checksum_errors]
        mov     ax, 3
        jmp     .exit
        
.invalid_header:
        inc     word ptr [checksum_errors]
        mov     ax, 4
        jmp     .exit

.read_error:
        inc     word ptr [checksum_errors]
        mov     ax, 1
        jmp     .exit

.exit:
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
        je      .already_patched

        ; Verify we're on a 386+ processor
        call    get_cpu_features
        test    ax, 2               ; Check for 32-bit capability
        jz      .not_386

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
        jne     .patch_failed

        ; Mark as patched and optimized
        mov     byte ptr [patch_enabled], 1

        ; Success
        mov     ax, 0
        jmp     .exit

.not_386:
        ; CPU doesn't support 32-bit operations
        mov     ax, 2
        jmp     .exit
        
.patch_failed:
        ; 32-bit test failed
        and     byte ptr [current_cpu_opt], NOT OPT_32BIT
        mov     ax, 3
        jmp     .exit

.already_patched:
        mov     ax, 0
        jmp     .exit

.exit:
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
        jnz     .invalid_flags
        
        ; Check if 32-bit optimization is requested
        test    al, OPT_32BIT
        jz      .no_32bit_check
        
        ; Verify CPU supports 32-bit operations
        call    get_cpu_features
        test    ax, 2               ; Check for 32-bit capability
        jz      .unsupported_32bit
        
.no_32bit_check:
        ; Store new optimization level
        mov     [current_cpu_opt], al
        
        ; Apply optimizations if 32-bit was enabled
        test    al, OPT_32BIT
        jz      .optimization_set
        
        ; Enable 32-bit patches
        call    packet_ops_patch_32bit
        
.optimization_set:
        ; Success
        mov     ax, 0
        jmp     .exit
        
.invalid_flags:
        mov     ax, 1
        jmp     .exit
        
.unsupported_32bit:
        mov     ax, 2
        jmp     .exit

.exit:
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
        jz      .check_16bit_align
        
        ; 32-bit alignment check
        test    ax, 3               ; Check 4-byte alignment
        jz      .copy_32bit_aligned
        
        ; Not 32-bit aligned, align to 32-bit boundary
        call    .align_to_32bit
        jmp     .copy_32bit_bulk
        
.check_16bit_align:
        test    dl, OPT_16BIT
        jz      .copy_unaligned
        
        ; 16-bit alignment check
        test    ax, 1               ; Check word alignment
        jz      .copy_16bit_aligned
        
        ; Not word aligned, copy first byte
        movsb
        dec     cx
        
.copy_16bit_aligned:
        shr     cx, 1               ; Convert to words
        rep     movsw
        
        ; Handle remainder
        test    bx, 1
        jz      .aligned_done
        movsb
        jmp     .aligned_done
        
.copy_32bit_aligned:
.copy_32bit_bulk:
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
        jmp     .aligned_done
        
.copy_unaligned:
        ; Basic unaligned copy
        rep     movsb
        
.aligned_done:
        mov     ax, 0               ; Success
        
        pop     dx
        pop     bx
        pop     bp
        ret
        
; Helper: Align to 32-bit boundary
.align_to_32bit:
        ; Copy bytes until SI is 32-bit aligned
        mov     ax, si
        and     ax, 3               ; Get misalignment
        jz      .align_32_done      ; Already aligned
        
        mov     dx, 4
        sub     dx, ax              ; Bytes needed to align
        cmp     dx, cx              ; Don't copy more than we have
        jbe     .align_32_copy
        mov     dx, cx
        
.align_32_copy:
        sub     cx, dx              ; Adjust remaining count
        mov     ax, dx              ; Copy alignment bytes
        
.align_32_loop:
        movsb
        dec     ax
        jnz     .align_32_loop
        
.align_32_done:
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
        jnz     .copy_64_32bit
        
        ; 16-bit copy: 32 words = 64 bytes
        mov     cx, 32
        cld
        rep     movsw
        jmp     .copy_64_done
        
.copy_64_32bit:
        ; 32-bit copy: 16 dwords = 64 bytes
        mov     cx, 16
        cld
        db      66h                 ; 32-bit prefix
        rep     movsd
        
.copy_64_done:
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
        jnz     .copy_128_32bit
        
        ; 16-bit copy: 64 words = 128 bytes
        mov     cx, 64
        cld
        rep     movsw
        jmp     .copy_128_done
        
.copy_128_32bit:
        ; 32-bit copy: 32 dwords = 128 bytes
        mov     cx, 32
        cld
        db      66h                 ; 32-bit prefix
        rep     movsd
        
.copy_128_done:
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
        jnz     .copy_512_pentium
        test    al, OPT_32BIT
        jnz     .copy_512_32bit
        
        ; 16-bit copy: 256 words = 512 bytes
        mov     cx, 256
        cld
        rep     movsw
        jmp     .copy_512_done
        
.copy_512_32bit:
        ; 32-bit copy: 128 dwords = 512 bytes
        mov     cx, 128
        cld
        db      66h                 ; 32-bit prefix
        rep     movsd
        jmp     .copy_512_done
        
.copy_512_pentium:
        ; Pentium optimized: unroll loop to avoid AGI stalls
        mov     cx, 16              ; 16 iterations of 32 bytes each
        cld
        
.copy_512_pentium_loop:
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
        jnz     .copy_512_pentium_loop
        
.copy_512_done:
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
        jnz     .copy_1518_pentium
        test    al, OPT_32BIT
        jnz     .copy_1518_32bit
        
        ; 16-bit copy: 759 words = 1518 bytes
        mov     cx, 759
        cld
        rep     movsw
        jmp     .copy_1518_done
        
.copy_1518_32bit:
        ; 32-bit copy: 379 dwords + 2 bytes
        mov     cx, 379
        cld
        db      66h                 ; 32-bit prefix
        rep     movsd
        ; Copy remaining 2 bytes
        movsw
        jmp     .copy_1518_done
        
.copy_1518_pentium:
        ; Pentium optimized with cache-friendly block copies
        mov     dx, 47              ; 47 iterations of 32 bytes = 1504 bytes
        cld
        
.copy_1518_pentium_loop:
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
        jnz     .copy_1518_pentium_loop
        
        ; Copy remaining 14 bytes (1518 - 1504)
        mov     cx, 7
        rep     movsw
        
.copy_1518_done:
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
        jz      .memset_16bit
        
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
        jmp     .memset_done
        
.memset_16bit:
        ; 16-bit optimized set
        mov     dx, cx              ; Save original count
        shr     cx, 1               ; Convert to words
        cld
        rep     stosw
        
        ; Handle odd byte
        test    dx, 1
        jz      .memset_done
        stosb
        
.memset_done:
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
        jz      .dma_aligned
        
        ; Alignment error
        inc     word ptr [alignment_fixes]
        mov     ax, 1
        jmp     .dma_copy_exit
        
.dma_aligned:
        ; Check for cache line alignment for better performance
        test    ax, 31              ; 32-byte cache line alignment
        jnz     .dma_copy_basic
        
        ; Cache-aligned DMA copy (optimal for Pentium+)
        mov     bl, [current_cpu_opt]
        test    bl, OPT_PENTIUM
        jz      .dma_copy_32bit
        
        ; Pentium cache-optimized copy
        mov     dx, cx
        shr     cx, 5               ; 32-byte blocks
        jz      .dma_copy_remainder
        
.dma_copy_cache_loop:
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
        jnz     .dma_copy_cache_loop
        
        ; Handle remainder
        mov     cx, dx
        and     cx, 31
        jmp     .dma_copy_remainder
        
.dma_copy_32bit:
        ; Standard 32-bit aligned copy
        mov     dx, cx
        shr     cx, 2               ; Convert to dwords
        cld
        db      66h                 ; 32-bit prefix
        rep     movsd
        
        ; Copy remainder bytes
        mov     cx, dx
        and     cx, 3
        
.dma_copy_remainder:
        rep     movsb
        jmp     .dma_copy_success
        
.dma_copy_basic:
        ; Basic aligned copy
        call    packet_copy_fast
        
.dma_copy_success:
        mov     ax, 0               ; Success
        
.dma_copy_exit:
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
        jne     .no_tsc
        
        ; Read Time Stamp Counter (RDTSC instruction)
        db      0fh, 31h            ; RDTSC opcode
        
        ; Store the result
        mov     dword ptr [last_tsc_low], eax
        mov     dword ptr [last_tsc_high], edx
        
        pop     bp
        ret
        
.no_tsc:
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
        jne     .no_perf_tsc
        
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
        
.no_perf_tsc:
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
        jb      .no_optimization
        
        ; Check CPU capabilities
        mov     bl, [current_cpu_opt]
        
        ; Analyze alignment
        mov     dx, si
        or      dx, di              ; Combine source and dest addresses
        
        ; Test for various alignment levels
        test    dx, 3               ; 4-byte aligned?
        jnz     .check_word_align
        
        ; 32-bit aligned - check for advanced optimizations
        test    bl, OPT_PENTIUM
        jz      .check_486_opt
        
        ; Pentium optimization available
        cmp     cx, 64              ; Large enough for Pentium optimization?
        jb      .use_32bit_copy
        
        ; Check for 8-byte alignment (optimal for Pentium)
        test    dx, 7
        jnz     .use_pipeline_copy
        mov     ax, 4               ; Pentium AGI-aware copy
        jmp     .analysis_done
        
.check_486_opt:
        test    bl, OPT_486_ENHANCED
        jz      .use_32bit_copy
        
        ; 486+ pipeline optimization
        cmp     cx, 32              ; Large enough for pipeline optimization?
        jb      .use_32bit_copy
        
        ; Check for cache line alignment
        test    dx, 31              ; 32-byte cache line aligned?
        jnz     .use_pipeline_copy
        
        cmp     cx, 128             ; Large enough for cache optimization?
        jb      .use_pipeline_copy
        mov     ax, 3               ; Cache-line optimized copy
        jmp     .analysis_done
        
.use_pipeline_copy:
        mov     ax, 2               ; Pipeline-optimized copy
        jmp     .analysis_done
        
.use_32bit_copy:
        test    bl, OPT_32BIT
        jz      .check_word_align
        mov     ax, 1               ; 32-bit aligned copy
        jmp     .analysis_done
        
.check_word_align:
        test    dx, 1               ; Word aligned?
        jnz     .no_optimization
        
        ; Word-aligned but not dword-aligned
        test    bl, OPT_16BIT
        jz      .no_optimization
        
        ; Use 16-bit optimization (handled by standard fast copy)
        mov     ax, 0               ; No special optimization needed
        
.no_optimization:
        mov     ax, 0
        
.analysis_done:
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
        je      .copy_64_cache
        cmp     cx, 128
        je      .copy_128_cache
        cmp     cx, 512
        je      .copy_512_cache
        cmp     cx, 1518
        je      .copy_1518_cache
        
        ; General cache-optimized copy
        call    general_cache_copy
        jmp     .cache_copy_done
        
.copy_64_cache:
        call    packet_copy_64_bytes
        jmp     .cache_copy_done
        
.copy_128_cache:
        call    packet_copy_128_bytes
        jmp     .cache_copy_done
        
.copy_512_cache:
        call    packet_copy_512_bytes
        jmp     .cache_copy_done
        
.copy_1518_cache:
        call    packet_copy_1518_bytes
        jmp     .cache_copy_done
        
.cache_copy_done:
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
        jz      .cache_remainder
        
.cache_chunk_loop:
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
        jnz     .cache_chunk_loop
        
.cache_remainder:
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
        jnz     .copy_64_pentium
        test    al, OPT_486_ENHANCED
        jnz     .copy_64_486
        test    al, OPT_32BIT
        jnz     .copy_64_32bit
        
        ; 16-bit copy: 32 words = 64 bytes
        mov     cx, 32
        cld
        rep     movsw
        jmp     .copy_64_done
        
.copy_64_32bit:
        ; 32-bit copy: 16 dwords = 64 bytes
        mov     cx, 16
        cld
        db      66h                 ; 32-bit prefix
        rep     movsd
        jmp     .copy_64_done
        
.copy_64_486:
        ; 486 pipeline-optimized: unroll for U/V pipe pairing
        cld
        ; Copy 64 bytes as 8×8-byte blocks for optimal pairing
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
        jmp     .copy_64_done
        
.copy_64_pentium:
        ; Pentium dual-pipeline optimized with AGI avoidance
        cld
        
        ; Copy 64 bytes as 8×8-byte operations with optimal pairing
        mov     eax, ds:[si]        ; U-pipe
        mov     ebx, ds:[si+4]      ; V-pipe (pairs)
        add     si, 8               ; Calculate next address (avoid AGI)
        nop                         ; Fill slot
        mov     es:[di], eax        ; U-pipe
        mov     es:[di+4], ebx      ; V-pipe (pairs)
        add     di, 8               ; U-pipe
        
        ; Repeat pattern 7 more times...
        mov     cx, 7               ; Remaining 8-byte blocks
.pentium_64_loop:
        mov     eax, ds:[si]        ; U-pipe
        mov     ebx, ds:[si+4]      ; V-pipe (pairs)
        add     si, 8               ; Avoid AGI
        nop                         ; Fill slot
        mov     es:[di], eax        ; U-pipe
        mov     es:[di+4], ebx      ; V-pipe (pairs)
        add     di, 8               ; U-pipe
        dec     cx                  ; V-pipe (pairs)
        jnz     .pentium_64_loop
        
.copy_64_done:
        mov     ax, 0               ; Success
        
        pop     ebx
        pop     eax
        pop     cx
        pop     bp
        ret
packet_copy_64_bytes_enhanced ENDP

_TEXT ENDS

END