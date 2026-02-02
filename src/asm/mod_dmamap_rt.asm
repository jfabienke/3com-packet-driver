; mod_dmamap_rt.asm - Unified DMA mapping layer (Runtime JIT Module)
; Created: 2026-02-01 00:00:00
; CPU: 8086
; NASM 16-bit code

        cpu     8086
        bits    16

%include "patch_macros.inc"

        section .text class=MODULE

; ============================================================================
; MODULE HEADER (64 bytes)
; ============================================================================
global _mod_dmamap_rt_header
_mod_dmamap_rt_header:
header:
    db 'PKTDRV',0                   ; 7 bytes - module signature
    db 1, 0                         ; 2 bytes - version 1.0
    dw hot_start                    ; hot section start offset
    dw hot_end                      ; hot section end offset
    dw 0, 0                         ; cold_start, cold_end (unused)
    dw patch_table                  ; patch table offset
    dw PATCH_COUNT                  ; number of patch entries
    dw (hot_end - header)           ; module_size
    dw (hot_end - hot_start)        ; required_memory
    db 0                            ; cpu_requirements (8086)
    db 0                            ; nic_type (0 = any)
    dw 8000h                        ; capability flags (MOD_CAP_CORE)
    times (64 - ($ - header)) db 0  ; pad header to 64 bytes

hot_start:

; ============================================================================
; EXTERNAL SYMBOLS (from dmabnd and cache modules)
; ============================================================================
extern dma_check_buffer_safety_
extern dma_get_tx_bounce_buffer_
extern dma_release_tx_bounce_buffer_
extern dma_get_rx_bounce_buffer_
extern dma_release_rx_bounce_buffer_
extern is_safe_for_direct_dma_

; ============================================================================
; PUBLIC FUNCTIONS
; ============================================================================
global dma_map_tx_
global dma_map_tx_flags_
global dma_unmap_tx_
global dma_map_rx_
global dma_map_rx_flags_
global dma_unmap_rx_
global dma_map_buffer_
global dma_map_buffer_flags_
global dma_unmap_buffer_
global dma_mapping_get_address_
global dma_mapping_get_phys_addr_
global dma_mapping_get_length_
global dma_mapping_uses_bounce_
global dma_mapping_is_coherent_
global dma_mapping_uses_vds_
global dma_mapping_sync_for_device_
global dma_mapping_sync_for_cpu_
global dma_mapping_is_fast_path_enabled_
global dma_mapping_get_cache_hit_rate_

; ============================================================================
; CONSTANTS
; ============================================================================
PATCH_COUNT     equ     0

; DMA mapping flags (offset +10 in struct)
DMA_FLAG_BOUNCE     equ 0x0001      ; Bit 0: using bounce buffer
DMA_FLAG_VDS        equ 0x0002      ; Bit 1: using VDS
DMA_FLAG_COHERENT   equ 0x0004      ; Bit 2: cache coherent

; DMA mapping structure size
DMAMAP_SIZE     equ     16

; ============================================================================
; HOT DATA SECTION
; ============================================================================
        section .data

; Mapping cache (4 slots for concurrent mappings)
map_cache:
        times (DMAMAP_SIZE * 4) db 0    ; 4 mapping structs = 64 bytes

map_cache_idx:
        dw      0                       ; Round-robin index (0-3)

; Statistics
dmamap_stats:
        times 32 db 0

fast_path_enabled:
        dw      0                       ; Fast path flag

; ============================================================================
; CODE SECTION
; ============================================================================
        section .text

; ============================================================================
; INTERNAL HELPER: Get next mapping slot
; Returns: ES:DI = pointer to mapping struct
; Preserves: All registers except DI, ES
; ============================================================================
get_mapping_slot:
        push    ax
        push    bx

        ; Get current index
        mov     bx, [map_cache_idx]

        ; Calculate offset: bx = bx * 16
        mov     ax, bx
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1                   ; ax = bx * 16

        ; Get segment and offset
        mov     di, map_cache
        add     di, ax                  ; DI = offset into cache

        ; Advance index (mod 4)
        inc     bx
        and     bx, 3                   ; Keep in range 0-3
        mov     [map_cache_idx], bx

        ; Set ES to data segment
        mov     ax, ds
        mov     es, ax

        pop     bx
        pop     ax
        ret

; ============================================================================
; INTERNAL HELPER: Calculate physical address from seg:off
; Input:  DX:AX = seg:off
; Output: DX:AX = 32-bit physical address
; Preserves: All except AX, DX, CX
; ============================================================================
calc_phys_addr:
        push    bx

        ; phys = (seg << 4) + offset
        ; We need to handle the full 20-bit result

        mov     bx, dx                  ; BX = segment
        mov     cx, ax                  ; CX = offset

        ; Calculate high nibble: seg >> 12
        mov     ax, bx
        mov     dx, bx
        shr     dx, 1
        shr     dx, 1
        shr     dx, 1
        shr     dx, 1
        shr     dx, 1
        shr     dx, 1
        shr     dx, 1
        shr     dx, 1
        shr     dx, 1
        shr     dx, 1
        shr     dx, 1
        shr     dx, 1                   ; DX = seg >> 12 (high nibble)

        ; Calculate low part: (seg << 4) + offset
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1                   ; AX = (seg & 0xFFF) << 4

        add     ax, cx                  ; AX = (seg << 4) + offset
        adc     dx, 0                   ; Handle carry into high word

        pop     bx
        ret

; ============================================================================
; INTERNAL HELPER: Copy memory
; Input:  DS:SI = source, ES:DI = dest, CX = count
; Preserves: Segment registers
; ============================================================================
memcpy_helper:
        push    si
        push    di
        push    cx

        ; Simple byte copy
        cld
        rep     movsb

        pop     cx
        pop     di
        pop     si
        ret

; ============================================================================
; dma_map_tx_ - Map buffer for TX DMA
; Input:  DX:AX = buffer far ptr (seg:off), BX = length
; Output: DX:AX = far ptr to mapping struct
; ============================================================================
dma_map_tx_:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si
        push    di
        push    es

        ; Save buffer info
        push    dx                      ; [bp-12] = buffer seg
        push    ax                      ; [bp-14] = buffer off
        push    bx                      ; [bp-16] = length

        ; Check if buffer is safe for direct DMA
        ; is_safe_for_direct_dma_(DX:AX, BX) -> AX
        call    far is_safe_for_direct_dma_

        test    ax, ax
        jz      .use_bounce             ; Not safe, use bounce buffer

        ; Safe for direct DMA
.direct_dma:
        call    get_mapping_slot        ; ES:DI = mapping struct

        ; Restore buffer info
        mov     dx, [bp-12]             ; Buffer seg
        mov     ax, [bp-14]             ; Buffer off
        mov     bx, [bp-16]             ; Length

        ; Fill mapping struct
        mov     [es:di+0], dx           ; virt_seg
        mov     [es:di+2], ax           ; virt_off

        ; Calculate physical address
        push    di
        call    calc_phys_addr          ; DX:AX = phys addr
        pop     di

        mov     [es:di+4], ax           ; phys_lo
        mov     [es:di+6], dx           ; phys_hi
        mov     [es:di+8], bx           ; length
        mov     word [es:di+10], 0      ; flags (no bounce)
        mov     word [es:di+12], 0      ; bounce_seg = 0
        mov     word [es:di+14], 0      ; bounce_off = 0

        jmp     .done

.use_bounce:
        ; Get TX bounce buffer
        mov     bx, [bp-16]             ; Length
        call    far dma_get_tx_bounce_buffer_
                                        ; Returns DX:AX = bounce buffer

        test    dx, dx
        jz      .no_bounce              ; Failed to get bounce buffer

        ; Save bounce buffer
        push    dx                      ; [bp-18] = bounce seg
        push    ax                      ; [bp-20] = bounce off

        ; Copy data to bounce buffer
        mov     si, [bp-14]             ; Source offset
        push    ds
        mov     ds, [bp-12]             ; Source segment
        mov     di, ax                  ; Dest offset (bounce)
        mov     es, dx                  ; Dest segment (bounce)
        mov     cx, [bp-16]             ; Length
        call    memcpy_helper
        pop     ds

        ; Get mapping slot
        call    get_mapping_slot        ; ES:DI = mapping struct

        ; Fill mapping struct with bounce info
        mov     dx, [bp-12]             ; Original buffer seg
        mov     ax, [bp-14]             ; Original buffer off
        mov     [es:di+0], dx           ; virt_seg
        mov     [es:di+2], ax           ; virt_off

        ; Calculate physical address of bounce buffer
        mov     dx, [bp-18]             ; Bounce seg
        mov     ax, [bp-20]             ; Bounce off
        push    di
        call    calc_phys_addr          ; DX:AX = phys addr
        pop     di

        mov     [es:di+4], ax           ; phys_lo
        mov     [es:di+6], dx           ; phys_hi
        mov     bx, [bp-16]             ; Length
        mov     [es:di+8], bx           ; length
        mov     word [es:di+10], DMA_FLAG_BOUNCE ; flags
        mov     dx, [bp-18]
        mov     [es:di+12], dx          ; bounce_seg
        mov     ax, [bp-20]
        mov     [es:di+14], ax          ; bounce_off

        add     sp, 4                   ; Clean up bounce ptr
        jmp     .done

.no_bounce:
        ; Failed - return NULL
        xor     ax, ax
        xor     dx, dx
        jmp     .exit

.done:
        ; Return far pointer to mapping struct
        mov     ax, di
        mov     dx, es

.exit:
        add     sp, 6                   ; Clean up buffer info
        pop     es
        pop     di
        pop     si
        pop     cx
        pop     bx
        pop     bp
        retf

; ============================================================================
; dma_map_tx_flags_ - Map buffer for TX DMA with flags
; Input:  DX:AX = buffer far ptr, BX = length, CX = flags
; Output: DX:AX = far ptr to mapping struct
; ============================================================================
dma_map_tx_flags_:
        ; For now, ignore CX flags and call standard function
        ; (Forward compatibility placeholder)
        jmp     dma_map_tx_

; ============================================================================
; dma_unmap_tx_ - Unmap TX buffer
; Input:  DX:AX = mapping far ptr
; Output: void
; ============================================================================
dma_unmap_tx_:
        push    bp
        mov     bp, sp
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es

        ; Load mapping pointer
        mov     es, dx
        mov     di, ax

        ; Check if bounce buffer was used
        mov     ax, [es:di+10]          ; flags
        test    ax, DMA_FLAG_BOUNCE
        jz      .no_bounce

        ; Release TX bounce buffer
        mov     dx, [es:di+12]          ; bounce_seg
        mov     ax, [es:di+14]          ; bounce_off
        call    far dma_release_tx_bounce_buffer_

.no_bounce:
        ; Clear mapping struct
        mov     cx, DMAMAP_SIZE
        xor     ax, ax
        cld
        rep     stosb

        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        pop     bp
        retf

; ============================================================================
; dma_map_rx_ - Map buffer for RX DMA
; Input:  DX:AX = buffer far ptr, BX = length
; Output: DX:AX = far ptr to mapping struct
; ============================================================================
dma_map_rx_:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si
        push    di
        push    es

        ; Save buffer info
        push    dx                      ; [bp-12] = buffer seg
        push    ax                      ; [bp-14] = buffer off
        push    bx                      ; [bp-16] = length

        ; Check if buffer is safe for direct DMA
        call    far is_safe_for_direct_dma_

        test    ax, ax
        jz      .use_bounce

        ; Safe for direct DMA
.direct_dma:
        call    get_mapping_slot        ; ES:DI = mapping struct

        mov     dx, [bp-12]             ; Buffer seg
        mov     ax, [bp-14]             ; Buffer off
        mov     bx, [bp-16]             ; Length

        ; Fill mapping struct
        mov     [es:di+0], dx           ; virt_seg
        mov     [es:di+2], ax           ; virt_off

        push    di
        call    calc_phys_addr
        pop     di

        mov     [es:di+4], ax           ; phys_lo
        mov     [es:di+6], dx           ; phys_hi
        mov     [es:di+8], bx           ; length
        mov     word [es:di+10], 0      ; flags
        mov     word [es:di+12], 0      ; bounce_seg
        mov     word [es:di+14], 0      ; bounce_off

        jmp     .done

.use_bounce:
        ; Get RX bounce buffer
        mov     bx, [bp-16]             ; Length
        call    far dma_get_rx_bounce_buffer_

        test    dx, dx
        jz      .no_bounce

        push    dx                      ; [bp-18] = bounce seg
        push    ax                      ; [bp-20] = bounce off

        call    get_mapping_slot        ; ES:DI = mapping struct

        ; Fill mapping struct
        mov     dx, [bp-12]             ; Original buffer seg
        mov     ax, [bp-14]             ; Original buffer off
        mov     [es:di+0], dx           ; virt_seg
        mov     [es:di+2], ax           ; virt_off

        ; Calculate physical address of bounce buffer
        mov     dx, [bp-18]             ; Bounce seg
        mov     ax, [bp-20]             ; Bounce off
        push    di
        call    calc_phys_addr
        pop     di

        mov     [es:di+4], ax           ; phys_lo
        mov     [es:di+6], dx           ; phys_hi
        mov     bx, [bp-16]
        mov     [es:di+8], bx           ; length
        mov     word [es:di+10], DMA_FLAG_BOUNCE ; flags
        mov     dx, [bp-18]
        mov     [es:di+12], dx          ; bounce_seg
        mov     ax, [bp-20]
        mov     [es:di+14], ax          ; bounce_off

        add     sp, 4                   ; Clean up bounce ptr
        jmp     .done

.no_bounce:
        xor     ax, ax
        xor     dx, dx
        jmp     .exit

.done:
        mov     ax, di
        mov     dx, es

.exit:
        add     sp, 6                   ; Clean up buffer info
        pop     es
        pop     di
        pop     si
        pop     cx
        pop     bx
        pop     bp
        retf

; ============================================================================
; dma_map_rx_flags_ - Map buffer for RX DMA with flags
; ============================================================================
dma_map_rx_flags_:
        jmp     dma_map_rx_

; ============================================================================
; dma_unmap_rx_ - Unmap RX buffer
; Input:  DX:AX = mapping far ptr
; Output: void
; ============================================================================
dma_unmap_rx_:
        push    bp
        mov     bp, sp
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es

        mov     es, dx
        mov     di, ax

        ; Check if bounce buffer was used
        mov     ax, [es:di+10]          ; flags
        test    ax, DMA_FLAG_BOUNCE
        jz      .no_bounce

        ; Copy data from bounce to original buffer (for RX)
        push    ds

        mov     si, [es:di+14]          ; bounce_off (source)
        mov     ds, [es:di+12]          ; bounce_seg (source)

        mov     bx, di                  ; Save DI
        mov     di, [es:bx+2]           ; virt_off (dest)
        push    es
        mov     es, [es:bx+0]           ; virt_seg (dest)
        mov     cx, [es:bx+8]           ; length

        call    memcpy_helper

        pop     es
        mov     di, bx                  ; Restore DI
        pop     ds

        ; Release RX bounce buffer
        mov     dx, [es:di+12]          ; bounce_seg
        mov     ax, [es:di+14]          ; bounce_off
        call    far dma_release_rx_bounce_buffer_

.no_bounce:
        ; Clear mapping struct
        mov     cx, DMAMAP_SIZE
        xor     ax, ax
        cld
        rep     stosb

        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        pop     bp
        retf

; ============================================================================
; dma_map_buffer_ / dma_map_buffer_flags_ - Generic mapping (use TX pool)
; ============================================================================
dma_map_buffer_:
        jmp     dma_map_tx_

dma_map_buffer_flags_:
        jmp     dma_map_tx_flags_

; ============================================================================
; dma_unmap_buffer_ - Generic unmap
; ============================================================================
dma_unmap_buffer_:
        jmp     dma_unmap_tx_

; ============================================================================
; ACCESSOR FUNCTIONS
; ============================================================================

; dma_mapping_get_address_ - Get virtual address
; Input:  DX:AX = mapping ptr
; Output: DX:AX = virt ptr (seg:off)
dma_mapping_get_address_:
        push    es
        push    di

        mov     es, dx
        mov     di, ax

        mov     dx, [es:di+0]           ; virt_seg
        mov     ax, [es:di+2]           ; virt_off

        pop     di
        pop     es
        retf

; dma_mapping_get_phys_addr_ - Get physical address
; Input:  DX:AX = mapping ptr
; Output: DX:AX = 32-bit physical address
dma_mapping_get_phys_addr_:
        push    es
        push    di

        mov     es, dx
        mov     di, ax

        mov     ax, [es:di+4]           ; phys_lo
        mov     dx, [es:di+6]           ; phys_hi

        pop     di
        pop     es
        retf

; dma_mapping_get_length_ - Get length
; Input:  DX:AX = mapping ptr
; Output: AX = length
dma_mapping_get_length_:
        push    es
        push    di

        mov     es, dx
        mov     di, ax

        mov     ax, [es:di+8]           ; length

        pop     di
        pop     es
        retf

; dma_mapping_uses_bounce_ - Check if using bounce buffer
; Input:  DX:AX = mapping ptr
; Output: AX = 1 if bounce, 0 otherwise
dma_mapping_uses_bounce_:
        push    es
        push    di

        mov     es, dx
        mov     di, ax

        mov     ax, [es:di+10]          ; flags
        and     ax, DMA_FLAG_BOUNCE
        jz      .not_bounce
        mov     ax, 1
.not_bounce:

        pop     di
        pop     es
        retf

; dma_mapping_is_coherent_ - Check if cache coherent
; Input:  DX:AX = mapping ptr
; Output: AX = 1 if coherent, 0 otherwise
dma_mapping_is_coherent_:
        push    es
        push    di

        mov     es, dx
        mov     di, ax

        mov     ax, [es:di+10]          ; flags
        and     ax, DMA_FLAG_COHERENT
        jz      .not_coherent
        mov     ax, 1
.not_coherent:

        pop     di
        pop     es
        retf

; dma_mapping_uses_vds_ - Check if using VDS
; Input:  DX:AX = mapping ptr
; Output: AX = 1 if VDS, 0 otherwise
dma_mapping_uses_vds_:
        push    es
        push    di

        mov     es, dx
        mov     di, ax

        mov     ax, [es:di+10]          ; flags
        and     ax, DMA_FLAG_VDS
        jz      .not_vds
        mov     ax, 1
.not_vds:

        pop     di
        pop     es
        retf

; dma_mapping_sync_for_device_ - Sync data to device
; Input:  DX:AX = mapping ptr
; Output: AX = 0
dma_mapping_sync_for_device_:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si
        push    di
        push    es
        push    ds

        mov     es, dx
        mov     di, ax

        ; Check if bounce buffer is used
        mov     ax, [es:di+10]          ; flags
        test    ax, DMA_FLAG_BOUNCE
        jz      .done                   ; No bounce, nothing to sync

        ; Copy virt → bounce
        mov     si, [es:di+2]           ; virt_off (source)
        mov     ds, [es:di+0]           ; virt_seg (source)

        mov     bx, di                  ; Save DI
        mov     di, [es:bx+14]          ; bounce_off (dest)
        push    es
        mov     es, [es:bx+12]          ; bounce_seg (dest)
        mov     cx, [es:bx+8]           ; length

        call    memcpy_helper

        pop     es

.done:
        xor     ax, ax

        pop     ds
        pop     es
        pop     di
        pop     si
        pop     cx
        pop     bx
        pop     bp
        retf

; dma_mapping_sync_for_cpu_ - Sync data from device
; Input:  DX:AX = mapping ptr
; Output: AX = 0
dma_mapping_sync_for_cpu_:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si
        push    di
        push    es
        push    ds

        mov     es, dx
        mov     di, ax

        ; Check if bounce buffer is used
        mov     ax, [es:di+10]          ; flags
        test    ax, DMA_FLAG_BOUNCE
        jz      .done

        ; Copy bounce → virt
        mov     si, [es:di+14]          ; bounce_off (source)
        mov     ds, [es:di+12]          ; bounce_seg (source)

        mov     bx, di                  ; Save DI
        mov     di, [es:bx+2]           ; virt_off (dest)
        push    es
        mov     es, [es:bx+0]           ; virt_seg (dest)
        mov     cx, [es:bx+8]           ; length

        call    memcpy_helper

        pop     es

.done:
        xor     ax, ax

        pop     ds
        pop     es
        pop     di
        pop     si
        pop     cx
        pop     bx
        pop     bp
        retf

; dma_mapping_is_fast_path_enabled_ - Check fast path status
; Input:  none
; Output: AX = fast_path_enabled flag
dma_mapping_is_fast_path_enabled_:
        mov     ax, [fast_path_enabled]
        retf

; dma_mapping_get_cache_hit_rate_ - Get cache hit rate
; Input:  none
; Output: DX:AX = 0:0 (not implemented)
dma_mapping_get_cache_hit_rate_:
        xor     ax, ax
        xor     dx, dx
        retf

hot_end:

patch_table:
