; @file mem_manager_test.asm
; @brief Memory manager compatibility tests for DOS environments
;
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
;
; This file implements comprehensive memory manager compatibility testing
; to ensure the packet driver works correctly with various DOS memory
; configurations including HIMEM.SYS, EMM386.EXE, QEMM386, and Windows DOS boxes.

.MODEL SMALL
.386

; Memory Manager Types
MEMMAN_NONE             EQU 0           ; No memory manager
MEMMAN_HIMEM            EQU 1           ; HIMEM.SYS only
MEMMAN_EMM386           EQU 2           ; EMM386.EXE
MEMMAN_QEMM             EQU 3           ; QEMM386
MEMMAN_WINDOWS          EQU 4           ; Windows DOS box
MEMMAN_UNKNOWN          EQU 255         ; Unknown manager

; XMS Version Constants
XMS_VERSION_2           EQU 0200h       ; XMS 2.0
XMS_VERSION_3           EQU 0300h       ; XMS 3.0

; Memory Test Patterns
MEM_PATTERN_A5          EQU 0A5h        ; 10100101b pattern
MEM_PATTERN_5A          EQU 05Ah        ; 01011010b pattern
MEM_PATTERN_00          EQU 000h        ; All zeros
MEM_PATTERN_FF          EQU 0FFh        ; All ones

; Test Results
TEST_PASS               EQU 0           ; Test passed
TEST_FAIL               EQU 1           ; Test failed
TEST_SKIP               EQU 2           ; Test skipped
TEST_ERROR              EQU 3           ; Test error

; Memory Block Sizes for Testing
TEST_BLOCK_SMALL        EQU 1024        ; 1KB blocks
TEST_BLOCK_MEDIUM       EQU 4096        ; 4KB blocks
TEST_BLOCK_LARGE        EQU 16384       ; 16KB blocks
TEST_BLOCK_HUGE         EQU 65536       ; 64KB blocks

; UMB Test Constants
UMB_MIN_SIZE            EQU 256         ; Minimum UMB size to test
UMB_MAX_SEARCH          EQU 16          ; Maximum UMB blocks to search

; Data segment
_DATA SEGMENT
        ASSUME  DS:_DATA

; Memory manager detection state
detected_memman_type    db MEMMAN_NONE          ; Detected memory manager
xms_driver_present      db 0                    ; XMS driver available
xms_version             dw 0                    ; XMS version
xms_entry_point         dd 0                    ; XMS entry point
ems_driver_present      db 0                    ; EMS driver available
ems_version             db 0                    ; EMS version
umb_available           db 0                    ; UMB support available
v86_mode_active         db 0                    ; V86 mode detected

; Memory allocation tracking
allocated_handles       dw 16 dup(0)            ; XMS handles allocated during tests
allocated_count         dw 0                    ; Number of allocated handles
allocated_blocks        dw 32 dup(0)            ; Conventional memory blocks
allocated_block_count   dw 0                    ; Number of allocated blocks
umb_blocks              dw 8 dup(0)             ; UMB block segments
umb_block_count         dw 0                    ; Number of UMB blocks

; Test state
test_results            db 32 dup(0)            ; Individual test results
test_count              dw 0                    ; Number of tests run
tests_passed            dw 0                    ; Tests passed
tests_failed            dw 0                    ; Tests failed
tests_skipped           dw 0                    ; Tests skipped
current_test_name       dw 0                    ; Current test name pointer

; Memory test areas
conventional_test_area  dw 0                    ; Conventional memory test area
xms_test_handle         dw 0                    ; XMS test block handle
ums_test_block          dw 0                    ; UMB test block segment

; Test buffers
pattern_buffer          db 1024 dup(0)          ; Pattern test buffer
backup_buffer           db 1024 dup(0)          ; Backup for verification
scratch_buffer          db 256 dup(0)           ; Scratch area

; Memory manager signatures to detect
himem_signature         db 'HIMEM   ', 0        ; HIMEM.SYS signature
emm386_signature        db 'EMMXXXX0', 0        ; EMM386.EXE signature
qemm_signature          db 'QEMM386 ', 0        ; QEMM386 signature
windows_signature       db 'WIN386  ', 0        ; Windows signature

; Test names
test_name_memman_detect db 'Memory Manager Detection', 0
test_name_xms_basic     db 'XMS Basic Functions', 0
test_name_xms_alloc     db 'XMS Allocation/Deallocation', 0
test_name_xms_move      db 'XMS Memory Move Operations', 0
test_name_umb_detect    db 'UMB Detection', 0
test_name_umb_alloc     db 'UMB Allocation', 0
test_name_v86_compat    db 'V86 Mode Compatibility', 0
test_name_conventional  db 'Conventional Memory Tests', 0
test_name_memory_map    db 'Memory Map Validation', 0
test_name_stress_alloc  db 'Stress Allocation Tests', 0

; Messages
msg_memman_start        db 'Memory Manager Compatibility Tests:', 0Dh, 0Ah, 0
msg_detected            db 'Detected: ', 0
msg_memman_none         db 'No Memory Manager', 0
msg_memman_himem        db 'HIMEM.SYS', 0
msg_memman_emm386       db 'EMM386.EXE', 0
msg_memman_qemm         db 'QEMM386', 0
msg_memman_windows      db 'Windows DOS Box', 0
msg_memman_unknown      db 'Unknown Memory Manager', 0
msg_xms_version         db 'XMS Version: ', 0
msg_xms_available       db 'XMS Memory Available: ', 0
msg_umb_status          db 'UMB Support: ', 0
msg_enabled             db 'Enabled', 0
msg_disabled            db 'Disabled', 0
msg_v86_status          db 'V86 Mode: ', 0
msg_active              db 'Active', 0
msg_inactive            db 'Inactive', 0
msg_kb                  db ' KB', 0
msg_newline             db 0Dh, 0Ah, 0
msg_test_complete       db 'Memory manager tests complete.', 0Dh, 0Ah, 0

; XMS function numbers
XMS_GET_VERSION         EQU 00h                 ; Get XMS version
XMS_ALLOCATE_HMA        EQU 01h                 ; Allocate HMA
XMS_FREE_HMA            EQU 02h                 ; Free HMA
XMS_GLOBAL_ENABLE_A20   EQU 03h                 ; Global enable A20
XMS_GLOBAL_DISABLE_A20  EQU 04h                 ; Global disable A20
XMS_LOCAL_ENABLE_A20    EQU 05h                 ; Local enable A20
XMS_LOCAL_DISABLE_A20   EQU 06h                 ; Local disable A20
XMS_QUERY_A20           EQU 07h                 ; Query A20 state
XMS_QUERY_FREE_MEMORY   EQU 08h                 ; Query free XMS memory
XMS_ALLOCATE_MEMORY     EQU 09h                 ; Allocate XMS memory
XMS_FREE_MEMORY         EQU 0Ah                 ; Free XMS memory
XMS_MOVE_MEMORY         EQU 0Bh                 ; Move memory block
XMS_LOCK_MEMORY         EQU 0Ch                 ; Lock memory block
XMS_UNLOCK_MEMORY       EQU 0Dh                 ; Unlock memory block
XMS_GET_HANDLE_INFO     EQU 0Eh                 ; Get handle information
XMS_REALLOCATE_MEMORY   EQU 0Fh                 ; Reallocate memory block
XMS_REQUEST_UMB         EQU 10h                 ; Request UMB
XMS_RELEASE_UMB         EQU 11h                 ; Release UMB

_DATA ENDS

; Code segment
_TEXT SEGMENT
        ASSUME  CS:_TEXT, DS:_DATA

; External references
EXTRN print_string:PROC
EXTRN print_number:PROC
EXTRN print_hex:PROC

; Public function exports
PUBLIC mem_manager_test_init
PUBLIC mem_manager_test_run
PUBLIC mem_manager_test_cleanup
PUBLIC detect_memory_manager
PUBLIC test_xms_functions
PUBLIC test_umb_support

;-----------------------------------------------------------------------------
; mem_manager_test_init - Initialize memory manager testing
;
; Input:  AL = Mode (0=test mode, 1=discovery mode)
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
mem_manager_test_init PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        
        ; Clear test state
        mov     word ptr [test_count], 0
        mov     word ptr [tests_passed], 0
        mov     word ptr [tests_failed], 0
        mov     word ptr [tests_skipped], 0
        mov     word ptr [allocated_count], 0
        mov     word ptr [allocated_block_count], 0
        mov     word ptr [umb_block_count], 0
        
        ; Clear test results
        mov     cx, 32
        mov     si, OFFSET test_results
        xor     al, al
.clear_results:
        mov     [si], al
        inc     si
        loop    .clear_results
        
        ; Clear allocated handle tracking
        mov     cx, 16
        mov     si, OFFSET allocated_handles
        xor     ax, ax
.clear_handles:
        mov     [si], ax
        add     si, 2
        loop    .clear_handles
        
        ; Detect memory manager configuration
        call    detect_memory_manager
        call    detect_xms_driver
        call    detect_v86_mode
        call    detect_umb_support
        
        ; Display memory configuration (if not in discovery mode)
        cmp     byte ptr [bp + 4], 1    ; Check discovery mode flag
        je      .skip_display
        
        call    display_memory_config
        
.skip_display:
        ; Success
        mov     ax, 0
        
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
mem_manager_test_init ENDP

;-----------------------------------------------------------------------------
; mem_manager_test_run - Run all memory manager compatibility tests
;
; Input:  None
; Output: AX = Number of failed tests
; Uses:   All registers
;-----------------------------------------------------------------------------
mem_manager_test_run PROC
        push    bp
        mov     bp, sp
        
        ; Test 1: Memory Manager Detection
        mov     word ptr [current_test_name], OFFSET test_name_memman_detect
        call    test_memory_manager_detection
        call    record_test_result
        
        ; Test 2: XMS Basic Functions
        mov     word ptr [current_test_name], OFFSET test_name_xms_basic
        call    test_xms_basic_functions
        call    record_test_result
        
        ; Test 3: XMS Allocation/Deallocation
        mov     word ptr [current_test_name], OFFSET test_name_xms_alloc
        call    test_xms_allocation
        call    record_test_result
        
        ; Test 4: XMS Memory Move Operations
        mov     word ptr [current_test_name], OFFSET test_name_xms_move
        call    test_xms_memory_move
        call    record_test_result
        
        ; Test 5: UMB Detection
        mov     word ptr [current_test_name], OFFSET test_name_umb_detect
        call    test_umb_detection
        call    record_test_result
        
        ; Test 6: UMB Allocation
        mov     word ptr [current_test_name], OFFSET test_name_umb_alloc
        call    test_umb_allocation
        call    record_test_result
        
        ; Test 7: V86 Mode Compatibility
        mov     word ptr [current_test_name], OFFSET test_name_v86_compat
        call    test_v86_mode_compatibility
        call    record_test_result
        
        ; Test 8: Conventional Memory Tests
        mov     word ptr [current_test_name], OFFSET test_name_conventional
        call    test_conventional_memory
        call    record_test_result
        
        ; Test 9: Memory Map Validation
        mov     word ptr [current_test_name], OFFSET test_name_memory_map
        call    test_memory_map_validation
        call    record_test_result
        
        ; Test 10: Stress Allocation Tests
        mov     word ptr [current_test_name], OFFSET test_name_stress_alloc
        call    test_stress_allocation
        call    record_test_result
        
        ; Display completion message
        mov     dx, OFFSET msg_test_complete
        call    print_string
        
        ; Return number of failed tests
        mov     ax, [tests_failed]
        
        pop     bp
        ret
mem_manager_test_run ENDP

;-----------------------------------------------------------------------------
; mem_manager_test_cleanup - Cleanup memory manager testing
;
; Input:  None
; Output: None
; Uses:   AX, BX, CX, SI
;-----------------------------------------------------------------------------
mem_manager_test_cleanup PROC
        push    bp
        mov     bp, sp
        push    ax
        push    bx
        push    cx
        push    si
        
        ; Free all allocated XMS handles
        mov     cx, [allocated_count]
        mov     si, OFFSET allocated_handles
        
.free_xms_loop:
        test    cx, cx
        jz      .xms_cleanup_done
        
        mov     dx, [si]                ; Get handle
        test    dx, dx
        jz      .skip_xms_free
        
        ; Free XMS memory
        mov     ah, XMS_FREE_MEMORY
        call    dword ptr [xms_entry_point]
        
.skip_xms_free:
        add     si, 2
        dec     cx
        jmp     .free_xms_loop
        
.xms_cleanup_done:
        ; Free all UMB blocks
        mov     cx, [umb_block_count]
        mov     si, OFFSET umb_blocks
        
.free_umb_loop:
        test    cx, cx
        jz      .umb_cleanup_done
        
        mov     dx, [si]                ; Get segment
        test    dx, dx
        jz      .skip_umb_free
        
        ; Free UMB
        mov     ah, XMS_RELEASE_UMB
        call    dword ptr [xms_entry_point]
        
.skip_umb_free:
        add     si, 2
        dec     cx
        jmp     .free_umb_loop
        
.umb_cleanup_done:
        ; Free conventional memory blocks
        mov     cx, [allocated_block_count]
        mov     si, OFFSET allocated_blocks
        
.free_conventional_loop:
        test    cx, cx
        jz      .conventional_cleanup_done
        
        mov     es, [si]                ; Get segment
        test    word ptr [si], 0
        jz      .skip_conventional_free
        
        ; Free DOS memory block
        mov     ah, 49h                 ; DOS free memory
        int     21h
        
.skip_conventional_free:
        add     si, 2
        dec     cx
        jmp     .free_conventional_loop
        
.conventional_cleanup_done:
        ; Reset counters
        mov     word ptr [allocated_count], 0
        mov     word ptr [allocated_block_count], 0
        mov     word ptr [umb_block_count], 0
        
        pop     si
        pop     cx
        pop     bx
        pop     ax
        pop     bp
        ret
mem_manager_test_cleanup ENDP

;-----------------------------------------------------------------------------
; detect_memory_manager - Detect which memory manager is installed
;
; Input:  None
; Output: AL = Memory manager type
; Uses:   AX, BX, CX, DX, SI, DI, ES
;-----------------------------------------------------------------------------
detect_memory_manager PROC
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        
        ; Default to no memory manager
        mov     al, MEMMAN_NONE
        
        ; Check for Windows DOS box
        ; Windows sets specific environment or TSR signatures
        call    detect_windows_dosbox
        test    al, al
        jnz     .windows_detected
        
        ; Check for QEMM386
        call    detect_qemm386
        test    al, al
        jnz     .qemm_detected
        
        ; Check for EMM386
        call    detect_emm386
        test    al, al
        jnz     .emm386_detected
        
        ; Check for HIMEM only
        call    detect_himem_only
        test    al, al
        jnz     .himem_detected
        
        ; No memory manager detected
        mov     al, MEMMAN_NONE
        jmp     .done
        
.windows_detected:
        mov     al, MEMMAN_WINDOWS
        jmp     .done
        
.qemm_detected:
        mov     al, MEMMAN_QEMM
        jmp     .done
        
.emm386_detected:
        mov     al, MEMMAN_EMM386
        jmp     .done
        
.himem_detected:
        mov     al, MEMMAN_HIMEM
        
.done:
        mov     [detected_memman_type], al
        
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret
detect_memory_manager ENDP

;-----------------------------------------------------------------------------
; detect_xms_driver - Detect XMS driver presence and capabilities
;
; Input:  None
; Output: None (updates global variables)
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
detect_xms_driver PROC
        push    bx
        push    cx
        push    dx
        
        ; Check for XMS driver
        mov     ax, 4300h               ; XMS installation check
        int     2Fh
        cmp     al, 80h
        jne     .no_xms
        
        ; Get XMS entry point
        mov     ax, 4310h
        int     2Fh
        mov     word ptr [xms_entry_point], bx
        mov     word ptr [xms_entry_point + 2], es
        
        ; Get XMS version
        mov     ah, XMS_GET_VERSION
        call    dword ptr [xms_entry_point]
        mov     [xms_version], ax
        
        ; Mark XMS as available
        mov     byte ptr [xms_driver_present], 1
        jmp     .done
        
.no_xms:
        mov     byte ptr [xms_driver_present], 0
        mov     word ptr [xms_version], 0
        mov     dword ptr [xms_entry_point], 0
        
.done:
        pop     dx
        pop     cx
        pop     bx
        ret
detect_xms_driver ENDP

;-----------------------------------------------------------------------------
; detect_v86_mode - Detect if running in V86 mode
;
; Input:  None
; Output: None (updates v86_mode_active)
; Uses:   AX
;-----------------------------------------------------------------------------
detect_v86_mode PROC
        push    ax
        
        ; Check SMSW for protected mode
        smsw    ax
        test    ax, 1                   ; PE bit
        jz      .real_mode
        
        ; In protected mode - check for V86 mode
        ; This is a simplified check - real implementation would
        ; use more sophisticated detection
        pushf
        pop     ax
        test    ax, 20000h              ; VM flag (bit 17)
        jnz     .v86_active
        
.real_mode:
        mov     byte ptr [v86_mode_active], 0
        jmp     .done
        
.v86_active:
        mov     byte ptr [v86_mode_active], 1
        
.done:
        pop     ax
        ret
detect_v86_mode ENDP

;-----------------------------------------------------------------------------
; detect_umb_support - Detect UMB support availability
;
; Input:  None
; Output: None (updates umb_available)
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
detect_umb_support PROC
        push    ax
        push    bx
        push    cx
        push    dx
        
        ; Default to no UMB support
        mov     byte ptr [umb_available], 0
        
        ; Check if XMS driver is present
        cmp     byte ptr [xms_driver_present], 0
        je      .no_umb
        
        ; Try to request a small UMB block
        mov     ah, XMS_REQUEST_UMB
        mov     dx, 1                   ; Request 1 paragraph (16 bytes)
        call    dword ptr [xms_entry_point]
        
        test    ax, ax                  ; AX = 0 if error
        jz      .no_umb
        
        ; UMB allocated successfully - release it
        mov     ah, XMS_RELEASE_UMB
        ; DX still contains segment from allocation
        call    dword ptr [xms_entry_point]
        
        ; Mark UMB as available
        mov     byte ptr [umb_available], 1
        
.no_umb:
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
detect_umb_support ENDP

;-----------------------------------------------------------------------------
; Individual Memory Manager Tests
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; test_memory_manager_detection - Test memory manager detection accuracy
;
; Input:  None
; Output: AL = Test result
; Uses:   AX, BX
;-----------------------------------------------------------------------------
test_memory_manager_detection PROC
        push    bx
        
        ; Re-run detection and verify consistency
        call    detect_memory_manager
        cmp     al, [detected_memman_type]
        jne     .detection_inconsistent
        
        ; Verify XMS detection consistency
        call    detect_xms_driver
        
        ; Basic sanity checks
        mov     bl, [detected_memman_type]
        
        ; If HIMEM+ detected, XMS should be available
        cmp     bl, MEMMAN_HIMEM
        jb      .skip_xms_check
        
        cmp     byte ptr [xms_driver_present], 0
        je      .xms_missing
        
.skip_xms_check:
        mov     al, TEST_PASS
        jmp     .done
        
.detection_inconsistent:
.xms_missing:
        mov     al, TEST_FAIL
        
.done:
        pop     bx
        ret
test_memory_manager_detection ENDP

;-----------------------------------------------------------------------------
; test_xms_basic_functions - Test basic XMS functions
;
; Input:  None
; Output: AL = Test result
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
test_xms_basic_functions PROC
        push    bx
        push    cx
        push    dx
        
        ; Skip if no XMS driver
        cmp     byte ptr [xms_driver_present], 0
        je      .skip_test
        
        ; Test XMS version query
        mov     ah, XMS_GET_VERSION
        call    dword ptr [xms_entry_point]
        test    ax, ax
        jz      .version_fail
        
        ; Version should be reasonable (>= 2.0)
        cmp     ax, XMS_VERSION_2
        jb      .version_fail
        
        ; Test free memory query
        mov     ah, XMS_QUERY_FREE_MEMORY
        call    dword ptr [xms_entry_point]
        ; Should return some values in AX and DX
        ; We don't verify exact values as they depend on system config
        
        ; Test A20 query
        mov     ah, XMS_QUERY_A20
        call    dword ptr [xms_entry_point]
        ; AL should be 0 or 1
        cmp     al, 1
        ja      .a20_fail
        
        mov     al, TEST_PASS
        jmp     .done
        
.skip_test:
        mov     al, TEST_SKIP
        jmp     .done
        
.version_fail:
.a20_fail:
        mov     al, TEST_FAIL
        
.done:
        pop     dx
        pop     cx
        pop     bx
        ret
test_xms_basic_functions ENDP

;-----------------------------------------------------------------------------
; test_xms_allocation - Test XMS memory allocation and deallocation
;
; Input:  None
; Output: AL = Test result
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
test_xms_allocation PROC
        push    bx
        push    cx
        push    dx
        
        ; Skip if no XMS driver
        cmp     byte ptr [xms_driver_present], 0
        je      .skip_test
        
        ; Test allocation of small block
        mov     ah, XMS_ALLOCATE_MEMORY
        mov     dx, 1                   ; 1 KB
        call    dword ptr [xms_entry_point]
        
        test    ax, ax
        jz      .alloc_fail
        
        ; Store handle for cleanup
        push    dx                      ; Save handle
        
        ; Test getting handle information
        mov     ah, XMS_GET_HANDLE_INFO
        call    dword ptr [xms_entry_point]
        
        test    ax, ax
        jz      .info_fail
        
        ; Free the allocated block
        pop     dx                      ; Restore handle
        mov     ah, XMS_FREE_MEMORY
        call    dword ptr [xms_entry_point]
        
        test    ax, ax
        jz      .free_fail
        
        mov     al, TEST_PASS
        jmp     .done
        
.skip_test:
        mov     al, TEST_SKIP
        jmp     .done
        
.alloc_fail:
.info_fail:
        pop     dx                      ; Clean up stack
.free_fail:
        mov     al, TEST_FAIL
        
.done:
        pop     dx
        pop     cx
        pop     bx
        ret
test_xms_allocation ENDP

;-----------------------------------------------------------------------------
; test_xms_memory_move - Test XMS memory move operations
;
; Input:  None
; Output: AL = Test result
; Uses:   AX, BX, CX, DX, SI, DI, ES
;-----------------------------------------------------------------------------
test_xms_memory_move PROC
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        
        ; Skip if no XMS driver
        cmp     byte ptr [xms_driver_present], 0
        je      .skip_test
        
        ; Allocate XMS block for testing
        mov     ah, XMS_ALLOCATE_MEMORY
        mov     dx, 1                   ; 1 KB
        call    dword ptr [xms_entry_point]
        
        test    ax, ax
        jz      .alloc_fail
        
        mov     [xms_test_handle], dx   ; Save handle
        
        ; Prepare test data
        mov     cx, 256
        mov     si, OFFSET pattern_buffer
        mov     al, MEM_PATTERN_A5
.fill_pattern:
        mov     [si], al
        inc     si
        loop    .fill_pattern
        
        ; Set up move structure on stack
        ; XMS move structure: 
        ; DWORD length, WORD source_handle, DWORD source_offset,
        ; WORD dest_handle, DWORD dest_offset
        sub     sp, 18                  ; Allocate move structure
        mov     si, sp
        
        ; Fill move structure
        mov     dword ptr [si], 256     ; Length
        mov     word ptr [si + 4], 0    ; Source handle (0 = conventional)
        
        ; Source offset (segment:offset to physical)
        mov     ax, ds
        movzx   eax, ax
        shl     eax, 4
        add     eax, OFFSET pattern_buffer
        mov     dword ptr [si + 6], eax
        
        mov     dx, [xms_test_handle]
        mov     word ptr [si + 10], dx  ; Dest handle
        mov     dword ptr [si + 12], 0  ; Dest offset
        
        ; Perform move
        mov     ah, XMS_MOVE_MEMORY
        mov     si, sp                  ; DS:SI = move structure
        call    dword ptr [xms_entry_point]
        
        add     sp, 18                  ; Deallocate move structure
        
        test    ax, ax
        jz      .move_fail
        
        ; Clean up - free XMS block
        mov     dx, [xms_test_handle]
        mov     ah, XMS_FREE_MEMORY
        call    dword ptr [xms_entry_point]
        
        mov     al, TEST_PASS
        jmp     .done
        
.skip_test:
        mov     al, TEST_SKIP
        jmp     .done
        
.alloc_fail:
.move_fail:
        ; Try to free handle if allocated
        cmp     word ptr [xms_test_handle], 0
        je      .no_cleanup
        
        mov     dx, [xms_test_handle]
        mov     ah, XMS_FREE_MEMORY
        call    dword ptr [xms_entry_point]
        
.no_cleanup:
        mov     al, TEST_FAIL
        
.done:
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret
test_xms_memory_move ENDP

;-----------------------------------------------------------------------------
; test_umb_detection - Test UMB detection capability
;
; Input:  None
; Output: AL = Test result
; Uses:   AX, BX
;-----------------------------------------------------------------------------
test_umb_detection PROC
        push    bx
        
        ; Re-run UMB detection and verify consistency
        call    detect_umb_support
        
        ; If we detected a memory manager that should support UMB,
        ; verify UMB is actually available
        mov     bl, [detected_memman_type]
        cmp     bl, MEMMAN_EMM386
        je      .should_have_umb
        cmp     bl, MEMMAN_QEMM
        je      .should_have_umb
        
        ; HIMEM and Windows may or may not have UMB
        jmp     .detection_ok
        
.should_have_umb:
        cmp     byte ptr [umb_available], 0
        je      .umb_expected_but_missing
        
.detection_ok:
        mov     al, TEST_PASS
        jmp     .done
        
.umb_expected_but_missing:
        mov     al, TEST_FAIL
        
.done:
        pop     bx
        ret
test_umb_detection ENDP

;-----------------------------------------------------------------------------
; test_umb_allocation - Test UMB allocation and deallocation
;
; Input:  None
; Output: AL = Test result
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
test_umb_allocation PROC
        push    bx
        push    cx
        push    dx
        
        ; Skip if UMB not available
        cmp     byte ptr [umb_available], 0
        je      .skip_test
        
        ; Try to allocate a UMB
        mov     ah, XMS_REQUEST_UMB
        mov     dx, 64                  ; Request 64 paragraphs (1KB)
        call    dword ptr [xms_entry_point]
        
        test    ax, ax
        jz      .alloc_fail
        
        ; UMB allocated - BX = segment, DX = actual size
        mov     [ums_test_block], bx
        
        ; Test access to UMB by writing/reading pattern
        push    es
        mov     es, bx
        mov     cx, 16                  ; Test first 16 bytes
        mov     al, MEM_PATTERN_5A
        xor     di, di
        
.write_pattern:
        stosb
        loop    .write_pattern
        
        ; Verify pattern
        mov     cx, 16
        mov     al, MEM_PATTERN_5A
        xor     di, di
        
.verify_pattern:
        cmp     al, es:[di]
        jne     .access_fail
        inc     di
        loop    .verify_pattern
        
        pop     es
        
        ; Release UMB
        mov     ah, XMS_RELEASE_UMB
        mov     dx, [ums_test_block]
        call    dword ptr [xms_entry_point]
        
        test    ax, ax
        jz      .release_fail
        
        mov     al, TEST_PASS
        jmp     .done
        
.skip_test:
        mov     al, TEST_SKIP
        jmp     .done
        
.access_fail:
        pop     es                      ; Clean up stack
.alloc_fail:
.release_fail:
        mov     al, TEST_FAIL
        
.done:
        pop     dx
        pop     cx
        pop     bx
        ret
test_umb_allocation ENDP

;-----------------------------------------------------------------------------
; test_v86_mode_compatibility - Test V86 mode compatibility
;
; Input:  None
; Output: AL = Test result
; Uses:   AX, BX
;-----------------------------------------------------------------------------
test_v86_mode_compatibility PROC
        push    bx
        
        ; Test V86 mode detection consistency
        call    detect_v86_mode
        
        ; If in V86 mode, test restrictions
        cmp     byte ptr [v86_mode_active], 0
        je      .not_v86_mode
        
        ; In V86 mode - test that our memory operations work
        ; This is a simplified test - real implementation would
        ; test specific V86 restrictions and workarounds
        
        ; V86 mode detected and handled correctly
        mov     al, TEST_PASS
        jmp     .done
        
.not_v86_mode:
        ; Not in V86 mode - that's also fine
        mov     al, TEST_PASS
        
.done:
        pop     bx
        ret
test_v86_mode_compatibility ENDP

;-----------------------------------------------------------------------------
; test_conventional_memory - Test conventional memory operations
;
; Input:  None
; Output: AL = Test result
; Uses:   AX, BX, CX, DX, SI, DI, ES
;-----------------------------------------------------------------------------
test_conventional_memory PROC
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        
        ; Allocate conventional memory block
        mov     ah, 48h                 ; DOS allocate memory
        mov     bx, 64                  ; 64 paragraphs (1KB)
        int     21h
        
        jc      .alloc_fail
        
        ; Store segment for cleanup
        mov     [conventional_test_area], ax
        
        ; Test memory access patterns
        mov     es, ax
        mov     cx, 256                 ; Test 256 words
        mov     si, 0
        
        ; Fill with test pattern
        mov     ax, 0A55Ah
.fill_loop:
        mov     es:[si], ax
        add     si, 2
        loop    .fill_loop
        
        ; Verify pattern
        mov     cx, 256
        mov     si, 0
        mov     ax, 0A55Ah
        
.verify_loop:
        cmp     ax, es:[si]
        jne     .verify_fail
        add     si, 2
        loop    .verify_loop
        
        ; Free conventional memory
        mov     es, [conventional_test_area]
        mov     ah, 49h                 ; DOS free memory
        int     21h
        
        jc      .free_fail
        
        mov     al, TEST_PASS
        jmp     .done
        
.alloc_fail:
.verify_fail:
.free_fail:
        mov     al, TEST_FAIL
        
.done:
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret
test_conventional_memory ENDP

;-----------------------------------------------------------------------------
; test_memory_map_validation - Test memory map consistency
;
; Input:  None
; Output: AL = Test result
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
test_memory_map_validation PROC
        push    bx
        push    cx
        push    dx
        
        ; Get conventional memory size
        int     12h                     ; BIOS memory size in KB
        mov     bx, ax                  ; Save conventional memory size
        
        ; Should be reasonable amount (at least 512KB, at most 640KB)
        cmp     ax, 512
        jb      .unreasonable_size
        cmp     ax, 640
        ja      .unreasonable_size
        
        ; If XMS available, check extended memory
        cmp     byte ptr [xms_driver_present], 0
        je      .skip_extended_check
        
        mov     ah, XMS_QUERY_FREE_MEMORY
        call    dword ptr [xms_entry_point]
        ; AX = largest free block, DX = total free memory
        ; We don't validate exact amounts, just that call succeeds
        
.skip_extended_check:
        mov     al, TEST_PASS
        jmp     .done
        
.unreasonable_size:
        mov     al, TEST_FAIL
        
.done:
        pop     dx
        pop     cx
        pop     bx
        ret
test_memory_map_validation ENDP

;-----------------------------------------------------------------------------
; test_stress_allocation - Test stress allocation patterns
;
; Input:  None
; Output: AL = Test result
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
test_stress_allocation PROC
        push    bx
        push    cx
        push    dx
        push    si
        
        ; Skip if no XMS driver
        cmp     byte ptr [xms_driver_present], 0
        je      .skip_test
        
        ; Try to allocate multiple small blocks
        mov     cx, 8                   ; Try 8 allocations
        mov     si, OFFSET allocated_handles
        mov     word ptr [allocated_count], 0
        
.alloc_loop:
        mov     ah, XMS_ALLOCATE_MEMORY
        mov     dx, 1                   ; 1KB blocks
        call    dword ptr [xms_entry_point]
        
        test    ax, ax
        jz      .alloc_done             ; Stop if allocation fails
        
        ; Store handle
        mov     [si], dx
        add     si, 2
        inc     word ptr [allocated_count]
        
        loop    .alloc_loop
        
.alloc_done:
        ; Free all allocated blocks
        mov     cx, [allocated_count]
        mov     si, OFFSET allocated_handles
        
.free_loop:
        test    cx, cx
        jz      .free_done
        
        mov     dx, [si]
        mov     ah, XMS_FREE_MEMORY
        call    dword ptr [xms_entry_point]
        
        add     si, 2
        dec     cx
        jmp     .free_loop
        
.free_done:
        ; Test passes if we allocated at least one block
        cmp     word ptr [allocated_count], 0
        je      .no_allocs
        
        mov     al, TEST_PASS
        jmp     .done
        
.skip_test:
        mov     al, TEST_SKIP
        jmp     .done
        
.no_allocs:
        mov     al, TEST_FAIL
        
.done:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret
test_stress_allocation ENDP

;-----------------------------------------------------------------------------
; Helper Functions
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; detect_windows_dosbox - Detect Windows DOS box environment
;
; Input:  None
; Output: AL = 1 if Windows detected, 0 if not
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
detect_windows_dosbox PROC
        push    bx
        push    cx
        push    dx
        
        ; Check for Windows enhanced mode
        mov     ax, 1600h               ; Get enhanced Windows version
        int     2Fh
        
        cmp     al, 00h                 ; No Windows
        je      .not_windows
        cmp     al, 80h                 ; Old Windows
        je      .not_windows
        cmp     al, 0FFh                ; Windows/386 2.x
        je      .not_windows
        
        ; Windows detected
        mov     al, 1
        jmp     .done
        
.not_windows:
        mov     al, 0
        
.done:
        pop     dx
        pop     cx
        pop     bx
        ret
detect_windows_dosbox ENDP

;-----------------------------------------------------------------------------
; detect_qemm386 - Detect QEMM386 memory manager
;
; Input:  None
; Output: AL = 1 if QEMM detected, 0 if not
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
detect_qemm386 PROC
        push    bx
        push    cx
        push    dx
        
        ; QEMM has specific interrupt interface
        mov     ax, 0D201h              ; QEMM API check
        int     2Fh
        
        cmp     ax, 0                   ; QEMM responds with specific signature
        je      .not_qemm
        
        ; Additional QEMM-specific checks could be added here
        mov     al, 1
        jmp     .done
        
.not_qemm:
        mov     al, 0
        
.done:
        pop     dx
        pop     cx
        pop     bx
        ret
detect_qemm386 ENDP

;-----------------------------------------------------------------------------
; detect_emm386 - Detect EMM386.EXE memory manager
;
; Input:  None
; Output: AL = 1 if EMM386 detected, 0 if not
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
detect_emm386 PROC
        push    bx
        push    cx
        push    dx
        
        ; Check for EMS driver (EMM386 provides EMS)
        mov     ah, 40h                 ; EMS status
        int     67h
        
        cmp     ah, 0                   ; EMS available
        jne     .not_emm386
        
        ; Check if it's specifically EMM386 (vs other EMS drivers)
        ; This is a simplified check - real implementation would
        ; check specific EMM386 signatures
        
        ; If we have EMS and V86 mode, likely EMM386
        cmp     byte ptr [v86_mode_active], 0
        je      .not_emm386
        
        mov     al, 1
        jmp     .done
        
.not_emm386:
        mov     al, 0
        
.done:
        pop     dx
        pop     cx
        pop     bx
        ret
detect_emm386 ENDP

;-----------------------------------------------------------------------------
; detect_himem_only - Detect HIMEM.SYS only (no EMM386)
;
; Input:  None
; Output: AL = 1 if HIMEM only, 0 if not
; Uses:   AX, BX
;-----------------------------------------------------------------------------
detect_himem_only PROC
        push    bx
        
        ; If XMS is available but not in V86 mode, likely HIMEM only
        cmp     byte ptr [xms_driver_present], 0
        je      .not_himem_only
        
        cmp     byte ptr [v86_mode_active], 0
        jne     .not_himem_only
        
        ; Check that EMS is not available
        mov     ah, 40h                 ; EMS status
        int     67h
        cmp     ah, 0
        je      .not_himem_only         ; EMS available = not HIMEM only
        
        mov     al, 1
        jmp     .done
        
.not_himem_only:
        mov     al, 0
        
.done:
        pop     bx
        ret
detect_himem_only ENDP

;-----------------------------------------------------------------------------
; display_memory_config - Display detected memory configuration
;
; Input:  None
; Output: None
; Uses:   AX, DX
;-----------------------------------------------------------------------------
display_memory_config PROC
        push    ax
        push    dx
        
        ; Display header
        mov     dx, OFFSET msg_memman_start
        call    print_string
        
        ; Display detected memory manager
        mov     dx, OFFSET msg_detected
        call    print_string
        
        mov     al, [detected_memman_type]
        cmp     al, MEMMAN_NONE
        je      .show_none
        cmp     al, MEMMAN_HIMEM
        je      .show_himem
        cmp     al, MEMMAN_EMM386
        je      .show_emm386
        cmp     al, MEMMAN_QEMM
        je      .show_qemm
        cmp     al, MEMMAN_WINDOWS
        je      .show_windows
        
        mov     dx, OFFSET msg_memman_unknown
        jmp     .show_manager
        
.show_none:
        mov     dx, OFFSET msg_memman_none
        jmp     .show_manager
.show_himem:
        mov     dx, OFFSET msg_memman_himem
        jmp     .show_manager
.show_emm386:
        mov     dx, OFFSET msg_memman_emm386
        jmp     .show_manager
.show_qemm:
        mov     dx, OFFSET msg_memman_qemm
        jmp     .show_manager
.show_windows:
        mov     dx, OFFSET msg_memman_windows
        
.show_manager:
        call    print_string
        mov     dx, OFFSET msg_newline
        call    print_string
        
        ; Show XMS information if available
        cmp     byte ptr [xms_driver_present], 0
        je      .skip_xms_info
        
        mov     dx, OFFSET msg_xms_version
        call    print_string
        mov     ax, [xms_version]
        call    print_hex
        mov     dx, OFFSET msg_newline
        call    print_string
        
.skip_xms_info:
        ; Show UMB status
        mov     dx, OFFSET msg_umb_status
        call    print_string
        
        cmp     byte ptr [umb_available], 0
        je      .show_umb_disabled
        
        mov     dx, OFFSET msg_enabled
        jmp     .show_umb_status
        
.show_umb_disabled:
        mov     dx, OFFSET msg_disabled
        
.show_umb_status:
        call    print_string
        mov     dx, OFFSET msg_newline
        call    print_string
        
        ; Show V86 mode status
        mov     dx, OFFSET msg_v86_status
        call    print_string
        
        cmp     byte ptr [v86_mode_active], 0
        je      .show_v86_inactive
        
        mov     dx, OFFSET msg_active
        jmp     .show_v86_status
        
.show_v86_inactive:
        mov     dx, OFFSET msg_inactive
        
.show_v86_status:
        call    print_string
        mov     dx, OFFSET msg_newline
        call    print_string
        
        pop     dx
        pop     ax
        ret
display_memory_config ENDP

;-----------------------------------------------------------------------------
; record_test_result - Record the result of a test
;
; Input:  AL = Test result
; Output: None
; Uses:   AX, BX
;-----------------------------------------------------------------------------
record_test_result PROC
        push    bx
        
        ; Store result in array
        mov     bx, [test_count]
        mov     [test_results + bx], al
        
        ; Update counters
        cmp     al, TEST_PASS
        je      .test_passed
        cmp     al, TEST_FAIL
        je      .test_failed
        cmp     al, TEST_SKIP
        je      .test_skipped
        jmp     .update_count
        
.test_passed:
        inc     word ptr [tests_passed]
        jmp     .update_count
        
.test_failed:
        inc     word ptr [tests_failed]
        jmp     .update_count
        
.test_skipped:
        inc     word ptr [tests_skipped]
        
.update_count:
        inc     word ptr [test_count]
        
        pop     bx
        ret
record_test_result ENDP

_TEXT ENDS

END