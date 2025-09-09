;-----------------------------------------------------------------------------
; @file vds_dma_safe.asm
; @brief VDS (Virtual DMA Services) integration for safe ISA DMA
;
; GPT-5 Requirement: Use VDS for all DMA operations on 3C515-TX
; Handles DMA buffer locking, physical address translation, and
; cache coherency hints from VDS provider.
;
; ISA Reality: 3C515-TX is limited to 16MB (24-bit addressing)
; and cannot cross 64KB boundaries.
;-----------------------------------------------------------------------------

.MODEL SMALL
.386

_TEXT SEGMENT
        ASSUME CS:_TEXT, DS:_DATA

;=============================================================================
; VDS SERVICE NUMBERS (INT 4Bh)
;=============================================================================

VDS_GET_VERSION         equ 8102h   ; AX=8102h Get VDS Version
VDS_LOCK_DMA_REGION     equ 8103h   ; AX=8103h Lock DMA Region  
VDS_UNLOCK_DMA_REGION   equ 8104h   ; AX=8104h Unlock DMA Region
VDS_REQUEST_DMA_BUFFER  equ 8107h   ; AX=8107h Request DMA Buffer
VDS_RELEASE_DMA_BUFFER  equ 8108h   ; AX=8108h Release DMA Buffer
VDS_DISABLE_DMA_XLAT    equ 810Bh   ; AX=810Bh Disable DMA Translation
VDS_ENABLE_DMA_XLAT     equ 810Ch   ; AX=810Ch Enable DMA Translation

; VDS Flags
VDS_AUTO_REMAP          equ 02h     ; Automatic remap
VDS_COPY_ALLOWED        equ 04h     ; Copy to contig buffer OK
VDS_NO_ALLOC            equ 08h     ; Don't allocate buffer
VDS_64K_ALIGN           equ 10h     ; Align on 64K boundary
VDS_128K_ALIGN          equ 20h     ; Align on 128K boundary

; DDS (DMA Descriptor Structure) offsets
DDS_SIZE                equ 16      ; Size of DDS structure
DDS_REGION_SIZE         equ 0       ; DWORD: Region size
DDS_OFFSET              equ 4       ; DWORD: Linear offset
DDS_SEGMENT_SEL         equ 8       ; WORD: Segment/Selector
DDS_BUFFER_ID           equ 10      ; WORD: Buffer ID
DDS_PHYSICAL_ADDR       equ 12      ; DWORD: Physical address

;=============================================================================
; DATA SEGMENT
;=============================================================================

_DATA SEGMENT

; VDS availability and capabilities
vds_available           db 0        ; 1 if VDS present
vds_version             dw 0        ; VDS version (BCD)
vds_product_num         dw 0        ; Product number
vds_product_rev         dw 0        ; Product revision
vds_max_dma_size        dd 0        ; Max DMA buffer size

; DMA buffer management (for 3C515-TX)
MAX_DMA_BUFFERS         equ 16      ; Support 16 DMA buffers
dma_buffer_count        dw 0        ; Active buffer count

; DMA Descriptor Structure array
dma_descriptors:
        db (MAX_DMA_BUFFERS * DDS_SIZE) dup(0)

; Statistics
vds_lock_count          dw 0        ; Successful locks
vds_unlock_count        dw 0        ; Successful unlocks
vds_remap_count         dw 0        ; Buffers remapped
vds_copy_count          dw 0        ; Buffers copied
vds_error_count         dw 0        ; VDS errors

; Cache policy from VDS
vds_cache_policy        db 0        ; 0=coherent, 1=flush needed

_DATA ENDS

;=============================================================================
; PUBLIC FUNCTIONS
;=============================================================================

PUBLIC vds_init
PUBLIC vds_lock_buffer
PUBLIC vds_unlock_buffer
PUBLIC vds_check_dma_constraints
PUBLIC vds_get_physical_address
PUBLIC vds_cache_policy_hint

;=============================================================================
; VDS IMPLEMENTATION
;=============================================================================

_TEXT SEGMENT

;-----------------------------------------------------------------------------
; vds_init - Initialize VDS support
;
; Output: AX = 0 if VDS available, -1 if not
;         Sets vds_available flag
;-----------------------------------------------------------------------------
vds_init PROC
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        
        ; Check for VDS presence
        mov     ax, VDS_GET_VERSION
        mov     dx, 0                   ; Required to be 0
        int     4Bh
        jc      .no_vds
        
        ; Check magic return value
        cmp     ah, 81h                 ; Must return 81h
        jne     .no_vds
        
        ; VDS is present - save version info
        mov     [vds_available], 1
        mov     [vds_version], bx       ; BCD version
        mov     [vds_product_num], cx   ; Product number
        mov     [vds_product_rev], si   ; Product revision
        
        ; Get maximum DMA buffer size
        mov     word ptr [vds_max_dma_size], di
        mov     word ptr [vds_max_dma_size+2], 0
        
        ; Determine cache policy hint
        ; If VDS version >= 2.00, check for cache coherency
        cmp     bh, 02h                 ; Major version >= 2?
        jb      .assume_coherent
        
        ; Query cache policy (vendor-specific)
        ; For now, assume coherent unless EMM386/QEMM detected
        call    detect_memory_manager
        test    ax, ax
        jz      .assume_coherent
        
        ; Memory manager present - may need cache management
        mov     [vds_cache_policy], 1
        jmp     .init_done
        
.assume_coherent:
        mov     [vds_cache_policy], 0   ; Hardware coherent
        
.init_done:
        xor     ax, ax                  ; Success
        jmp     .exit
        
.no_vds:
        mov     [vds_available], 0
        mov     ax, -1                  ; VDS not available
        
.exit:
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret
vds_init ENDP

;-----------------------------------------------------------------------------
; vds_lock_buffer - Lock buffer for DMA and get physical address
;
; Input:  ES:DI = Buffer address
;         CX = Buffer size in bytes
;         BX = Buffer index (0-15)
; Output: AX = 0 on success, error code on failure
;         Physical address stored in descriptor
;-----------------------------------------------------------------------------
vds_lock_buffer PROC
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        push    ds
        
        ; Check VDS availability
        cmp     [vds_available], 0
        je      .no_vds_fallback
        
        ; Calculate DDS offset for this buffer
        mov     ax, bx
        mov     dx, DDS_SIZE
        mul     dx                      ; AX = index * 16
        mov     si, ax
        add     si, offset dma_descriptors
        
        ; Fill in DDS structure
        mov     word ptr [si + DDS_REGION_SIZE], cx
        mov     word ptr [si + DDS_REGION_SIZE + 2], 0
        mov     word ptr [si + DDS_OFFSET], di
        mov     word ptr [si + DDS_OFFSET + 2], 0
        mov     [si + DDS_SEGMENT_SEL], es
        mov     word ptr [si + DDS_BUFFER_ID], 0
        
        ; Call VDS to lock region
        push    ds
        pop     es
        mov     di, si                  ; ES:DI -> DDS
        mov     dx, VDS_AUTO_REMAP | VDS_COPY_ALLOWED
        mov     ax, VDS_LOCK_DMA_REGION
        int     4Bh
        jc      .lock_failed
        
        ; Check if buffer was remapped or copied
        test    dl, VDS_AUTO_REMAP
        jz      .check_copy
        inc     [vds_remap_count]
        
.check_copy:
        test    dl, VDS_COPY_ALLOWED
        jz      .lock_success
        inc     [vds_copy_count]
        
.lock_success:
        inc     [vds_lock_count]
        
        ; Verify physical address is below 16MB (ISA limit)
        mov     eax, dword ptr [si + DDS_PHYSICAL_ADDR]
        cmp     eax, 1000000h           ; 16MB
        jae     .above_16mb
        
        ; Check for 64KB boundary crossing
        mov     dx, ax                  ; Low 16 bits
        add     dx, cx                  ; Add size
        jc      .crosses_64k            ; Carry = crosses boundary
        
        xor     ax, ax                  ; Success
        jmp     .exit
        
.above_16mb:
        ; Physical address above 16MB - ISA can't reach
        mov     ax, -2
        jmp     .exit
        
.crosses_64k:
        ; Crosses 64KB boundary - ISA DMA controller limitation
        mov     ax, -3
        jmp     .exit
        
.lock_failed:
        inc     [vds_error_count]
        mov     ax, -1
        jmp     .exit
        
.no_vds_fallback:
        ; No VDS - calculate physical address directly (real mode only)
        ; Physical = Segment * 16 + Offset
        mov     ax, es
        movzx   eax, ax
        shl     eax, 4
        movzx   edx, di
        add     eax, edx
        
        ; Store in descriptor
        mov     si, bx
        shl     si, 4                   ; index * 16
        add     si, offset dma_descriptors
        mov     dword ptr [si + DDS_PHYSICAL_ADDR], eax
        
        ; Check constraints
        cmp     eax, 1000000h           ; 16MB limit
        jae     .above_16mb
        
        mov     dx, ax
        add     dx, cx
        jc      .crosses_64k
        
        xor     ax, ax                  ; Success
        
.exit:
        pop     ds
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret
vds_lock_buffer ENDP

;-----------------------------------------------------------------------------
; vds_unlock_buffer - Unlock DMA buffer
;
; Input:  BX = Buffer index (0-15)
; Output: AX = 0 on success
;-----------------------------------------------------------------------------
vds_unlock_buffer PROC
        push    bx
        push    di
        push    es
        push    ds
        
        ; Check VDS availability
        cmp     [vds_available], 0
        je      .no_vds
        
        ; Calculate DDS offset
        mov     ax, bx
        mov     dx, DDS_SIZE
        mul     dx
        mov     di, ax
        add     di, offset dma_descriptors
        
        ; Call VDS to unlock
        push    ds
        pop     es                      ; ES:DI -> DDS
        mov     ax, VDS_UNLOCK_DMA_REGION
        int     4Bh
        jc      .unlock_failed
        
        inc     [vds_unlock_count]
        xor     ax, ax                  ; Success
        jmp     .exit
        
.unlock_failed:
        inc     [vds_error_count]
        mov     ax, -1
        jmp     .exit
        
.no_vds:
        ; No VDS - nothing to unlock
        xor     ax, ax
        
.exit:
        pop     ds
        pop     es
        pop     di
        pop     bx
        ret
vds_unlock_buffer ENDP

;-----------------------------------------------------------------------------
; vds_get_physical_address - Get physical address for DMA buffer
;
; Input:  BX = Buffer index
; Output: DX:AX = Physical address (24-bit for ISA)
;-----------------------------------------------------------------------------
vds_get_physical_address PROC
        push    bx
        push    si
        
        ; Calculate descriptor offset
        mov     ax, bx
        mov     dx, DDS_SIZE
        mul     dx
        mov     si, ax
        add     si, offset dma_descriptors
        
        ; Get physical address
        mov     ax, word ptr [si + DDS_PHYSICAL_ADDR]
        mov     dx, word ptr [si + DDS_PHYSICAL_ADDR + 2]
        
        ; Mask to 24 bits for ISA
        and     dx, 00FFh
        
        pop     si
        pop     bx
        ret
vds_get_physical_address ENDP

;-----------------------------------------------------------------------------
; vds_cache_policy_hint - Get cache management policy from VDS
;
; Output: AL = 0 if cache coherent (no flush needed)
;         AL = 1 if cache flush required
;-----------------------------------------------------------------------------
vds_cache_policy_hint PROC
        mov     al, [vds_cache_policy]
        ret
vds_cache_policy_hint ENDP

;-----------------------------------------------------------------------------
; detect_memory_manager - Check for EMM386/QEMM/etc
;
; Output: AX = 1 if memory manager detected, 0 if not
;-----------------------------------------------------------------------------
detect_memory_manager PROC
        push    bx
        push    cx
        push    dx
        
        ; Check for EMM386 signature
        mov     ax, 3567h               ; Get INT 67h vector
        int     21h
        mov     ax, es
        or      ax, bx
        jz      .no_emm
        
        ; Check EMM signature at INT 67h handler
        mov     ax, es:[0Ah]            ; EMM signature location
        cmp     ax, 'ME'                ; "EMM" signature
        jne     .no_emm
        
        mov     ax, 1                   ; EMM detected
        jmp     .exit
        
.no_emm:
        xor     ax, ax                  ; No memory manager
        
.exit:
        pop     dx
        pop     cx
        pop     bx
        ret
detect_memory_manager ENDP

_TEXT ENDS
END