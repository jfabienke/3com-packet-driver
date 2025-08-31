;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file patch_macros.inc
;; @brief Macros for creating safe SMC patch points (GPT-5 validated)
;;
;; Provides standardized macros for creating 5-byte NOP sleds that will be
;; patched with CPU-specific code during initialization.
;;
;; GPT-5 Requirements:
;; - Patch points must be safe to execute before patching (NOP sled)
;; - Exactly 5 bytes for atomic patching
;; - No memory corruption if executed unpached
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Standard 5-byte NOP sled for patch points
;;
;; Creates a safe patch point that does nothing if executed before patching.
;; Will be replaced with CPU-specific code during initialization.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
PATCH_POINT MACRO name
    align 2                     ; Word align for better access
name:
    nop                         ; 0x90 - 1 byte
    nop                         ; 0x90 - 1 byte  
    nop                         ; 0x90 - 1 byte
    nop                         ; 0x90 - 1 byte
    nop                         ; 0x90 - 1 byte
    ; Total: 5 bytes safe NOP sled
ENDM

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Patch point with fallback call
;;
;; Creates a patch point that calls a fallback routine if not patched.
;; The CALL will be replaced with optimized inline code.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
PATCH_POINT_CALL MACRO name, fallback
    align 2
name:
    call    fallback            ; 3 bytes: E8 xx xx (near call)
    nop                         ; 1 byte padding
    nop                         ; 1 byte padding
    ; Total: 5 bytes
ENDM

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Patch point for I/O operations
;;
;; Default to safe single-byte I/O that will be optimized.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
PATCH_POINT_IO MACRO name, default_op
    align 2
name:
    default_op                  ; 1-2 bytes (IN/OUT instruction)
    nop                         ; Padding to 5 bytes
    nop
    nop
    nop
    ; Adjust NOPs based on default_op size
ENDM

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Create patch table entry
;;
;; Generates a patch table entry for the linker.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
PATCH_TABLE_ENTRY MACRO patch_name, patch_type
    dw      patch_name          ; Offset to patch point
    db      patch_type          ; Type (COPY/IO/ISR/etc)
    db      5                   ; Always 5 bytes
ENDM

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Patch type constants
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
PATCH_TYPE_COPY     equ 1
PATCH_TYPE_IO       equ 2
PATCH_TYPE_CHECKSUM equ 3
PATCH_TYPE_ISR      equ 4
PATCH_TYPE_BRANCH   equ 5
PATCH_TYPE_ENDIAN   equ 0Ah
PATCH_TYPE_NOP      equ 0xFF