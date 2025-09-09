;
; @file tsr_proper.asm
; @brief Proper TSR memory calculation and installation
;
; Correctly calculates resident size from PSP, frees environment block,
; and handles DOS memory management properly.
;

SEGMENT _TEXT PUBLIC CLASS=CODE USE16

; External references
EXTERN _hot_section_end: BYTE    ; End of hot code/data
EXTERN _driver_init: NEAR        ; Initialization function

;=============================================================================
; COLD/INIT SEGMENT - Functions used only during initialization
; This segment is discarded after TSR installation (GPT-5 optimization)
;=============================================================================
SEGMENT _COLD_TEXT PUBLIC CLASS=INIT_CODE USE16

; Cold/init-only functions (discarded after use)
PUBLIC _tsr_get_resident_size
PUBLIC _tsr_free_environment
PUBLIC _tsr_install

;-----------------------------------------------------------------------------
; Calculate proper TSR resident size in paragraphs
; Returns: AX = paragraphs to keep resident
; NOTE: This function is in COLD segment - discarded after TSR installation
;-----------------------------------------------------------------------------
_tsr_get_resident_size PROC NEAR
        push    bp
        mov     bp, sp
        push    bx
        push    dx
        
        ; Get PSP segment
        mov     ah, 62h         ; Get PSP
        int     21h            ; BX = PSP segment
        mov     dx, bx          ; Save PSP segment
        
        ; Calculate end in paragraphs using paragraph math to avoid overflow
        ; End = SEG(_hot_section_end) + (OFFSET(_hot_section_end) + stack + 15) / 16
        
        mov     ax, SEG _hot_section_end  ; Segment already in paragraphs
        mov     bx, OFFSET _hot_section_end
        add     bx, 512         ; Add stack space
        add     bx, 15          ; Round up
        mov     cl, 4
        shr     bx, cl          ; Convert offset to paragraphs
        add     ax, bx          ; AX = total paragraphs from 0
        
        ; Subtract PSP segment to get paragraphs to keep
        sub     ax, dx          ; AX = paragraphs from PSP
        
        pop     dx
        pop     bx
        pop     bp
        ret
_tsr_get_resident_size ENDP

;-----------------------------------------------------------------------------
; Free the environment block
; Must be called before TSR installation
; NOTE: This function is in COLD segment - discarded after TSR installation
;-----------------------------------------------------------------------------
_tsr_free_environment PROC NEAR
        push    ax
        push    bx
        push    es
        
        ; Get PSP segment
        mov     ah, 62h
        int     21h             ; BX = PSP segment
        push    bx              ; Save PSP segment
        
        ; Get environment segment from PSP:2Ch
        mov     es, bx
        mov     ax, WORD PTR es:[2Ch]  ; Get environment segment
        
        ; Early-out if already freed or never allocated
        test    ax, ax
        jz      .no_env         ; Jump if already 0
        
        ; Set ES to environment segment for freeing
        mov     es, ax
        
        ; Free environment block
        mov     ah, 49h         ; Free memory
        int     21h
        jc      .free_failed    ; Check carry flag for error
        
        ; Reload PSP segment to clear environment pointer
        pop     bx              ; Restore PSP segment
        push    bx              ; Keep it on stack
        mov     es, bx          ; ES = PSP segment
        mov     WORD PTR es:[2Ch], 0  ; Clear environment pointer
        jmp     .done
        
.free_failed:
        ; Leave environment pointer intact if free failed
        
.no_env:
.done:
        pop     bx              ; Clean up stack
        pop     es
        pop     bx
        pop     ax
        ret
_tsr_free_environment ENDP

;-----------------------------------------------------------------------------
; Install as TSR with proper memory management
; Entry: None
; Exit: Does not return (TSR installed)
; NOTE: This function is in COLD segment - discarded after TSR installation
;-----------------------------------------------------------------------------
_tsr_install PROC NEAR
        push    bp
        mov     bp, sp
        
        ; Initialize the driver
        call    _driver_init
        test    ax, ax
        jnz     .init_failed
        
        ; Free environment block to save memory
        call    _tsr_free_environment
        
        ; Calculate resident size
        call    _tsr_get_resident_size
        mov     dx, ax          ; DX = paragraphs to keep
        
        ; Display success message
        push    dx
        mov     ah, 09h
        mov     dx, OFFSET install_msg
        int     21h
        pop     dx
        
        ; Install as TSR
        mov     ax, 3100h       ; TSR with return code 0
        int     21h
        ; Never returns
        
.init_failed:
        ; Display error and exit normally
        mov     ah, 09h
        mov     dx, OFFSET error_msg
        int     21h
        
        mov     ax, 4C01h       ; Exit with error code 1
        int     21h
        
        pop     bp
        ret
_tsr_install ENDP


;-----------------------------------------------------------------------------
; Alternative calculation using MCB (Memory Control Block)
; More accurate but requires understanding of DOS internals
;-----------------------------------------------------------------------------
_tsr_calc_from_mcb PROC NEAR
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    es
        
        ; Get PSP
        mov     ah, 62h
        int     21h
        mov     es, bx          ; ES = PSP
        
        ; Get MCB segment (PSP - 1)
        dec     bx
        mov     es, bx          ; ES = MCB
        
        ; MCB structure:
        ; +0: Signature ('M' or 'Z')
        ; +1: Owner PSP
        ; +3: Size in paragraphs
        ; +5: Reserved/Name
        
        ; Get allocated size from MCB
        mov     ax, es:[3]      ; Size in paragraphs
        
        ; Calculate how much we actually need
        push    ax              ; Save total size
        
        ; Get end of our code/data
        mov     ax, OFFSET _hot_section_end
        add     ax, 256         ; Add PSP size
        add     ax, 512         ; Add stack
        add     ax, 15          ; Round up
        shr     ax, 4           ; Convert to paragraphs
        
        pop     cx              ; CX = total allocated
        
        ; Return minimum of allocated and needed
        cmp     ax, cx
        jb      .use_calculated
        mov     ax, cx          ; Use full allocation
        
.use_calculated:
        pop     es
        pop     cx
        pop     bx
        pop     bp
        ret
_tsr_calc_from_mcb ENDP

;-----------------------------------------------------------------------------
; Cold data section - initialization-only strings (GPT-5 optimization)
;-----------------------------------------------------------------------------
install_msg     DB 'PCI Packet Driver installed as TSR', 13, 10
                DB 'Resident size: '
resident_size   DB '     paragraphs', 13, 10, '$'

error_msg       DB 'Failed to initialize driver', 13, 10, '$'

ENDS

END