/*
 * module.ld - Module Linker Script for 3Com Packet Driver
 * Phase 3A: Dynamic Module Loading - Stream 1 Build Infrastructure
 *
 * This linker script defines the memory layout and sections for .MOD files.
 * It ensures proper alignment, section ordering, and symbol resolution.
 */

OUTPUT_FORMAT("binary")
ENTRY(module_entry)

/* Module memory layout constants */
MODULE_LOAD_BASE = 0x10000;    /* Default load address for modules */
MODULE_ALIGN = 0x10;           /* 16-byte paragraph alignment */
MODULE_MAX_SIZE = 0x10000;     /* 64KB maximum module size */

MEMORY
{
    /* Module memory region - relocatable */
    MODULE (rwx) : ORIGIN = MODULE_LOAD_BASE, LENGTH = MODULE_MAX_SIZE
}

SECTIONS
{
    /* ====================================================================
     * Module Header Section - Must be first
     * ====================================================================*/
    
    .module_header : ALIGN(MODULE_ALIGN)
    {
        __module_start = .;
        __module_header_start = .;
        
        /* Module header must be first */
        KEEP(*(.module_header))
        
        /* Ensure header is exactly 128 bytes */
        . = __module_header_start + 128;
        __module_header_end = .;
        
    } > MODULE AT > MODULE
    
    /* ====================================================================
     * Code Section
     * ====================================================================*/
    
    .text : ALIGN(4)
    {
        __text_start = .;
        
        /* Module entry point */
        KEEP(*(.module_init))
        
        /* Main code sections */
        *(.text)
        *(.text.*)
        
        /* Constructors and destructors */
        *(.ctors)
        *(.dtors)
        
        __text_end = .;
        
    } > MODULE AT > MODULE
    
    /* ====================================================================
     * Data Sections
     * ====================================================================*/
    
    .rodata : ALIGN(2)
    {
        __rodata_start = .;
        
        /* Read-only data */
        *(.rodata)
        *(.rodata.*)
        
        /* String literals */
        *(.rodata.str*)
        
        __rodata_end = .;
        
    } > MODULE AT > MODULE
    
    .data : ALIGN(2)
    {
        __data_start = .;
        
        /* Initialized data */
        *(.data)
        *(.data.*)
        
        /* Global offset table for position-independent code */
        *(.got)
        *(.got.*)
        
        __data_end = .;
        
    } > MODULE AT > MODULE
    
    /* ====================================================================
     * Module Interface Tables
     * ====================================================================*/
    
    .vtable : ALIGN(2)
    {
        __vtable_start = .;
        
        /* Hardware module operation tables */
        *(.vtable)
        *(.vtable.*)
        
        /* Feature module API tables */
        *(.api_table)
        *(.api_table.*)
        
        __vtable_end = .;
        
    } > MODULE AT > MODULE
    
    .dependencies : ALIGN(2)
    {
        __deps_start = .;
        
        /* Module dependency information */
        *(.dependencies)
        *(.deps)
        
        __deps_end = .;
        
    } > MODULE AT > MODULE
    
    /* ====================================================================
     * Uninitialized Data (BSS)
     * ====================================================================*/
    
    .bss : ALIGN(2)
    {
        __bss_start = .;
        
        /* Uninitialized data */
        *(.bss)
        *(.bss.*)
        *(COMMON)
        
        __bss_end = .;
        
    } > MODULE AT > MODULE
    
    /* ====================================================================
     * Module Information and Metadata
     * ====================================================================*/
    
    .module_info : ALIGN(2)
    {
        __module_info_start = .;
        
        /* Extended module information */
        *(.module_info)
        
        __module_info_end = .;
        
    } > MODULE AT > MODULE
    
    /* ====================================================================
     * Debug and Symbol Information (Optional)
     * ====================================================================*/
    
    .debug_info : ALIGN(1)
    {
        __debug_start = .;
        
        /* Debug information (stripped in release builds) */
        *(.debug_info)
        *(.debug_abbrev)
        *(.debug_line)
        *(.debug_str)
        
        __debug_end = .;
        
    } > MODULE AT > MODULE
    
    .symbols : ALIGN(2)
    {
        __symbols_start = .;
        
        /* Symbol table for debugging */
        *(.symbols)
        *(.symtab)
        *(.strtab)
        
        __symbols_end = .;
        
    } > MODULE AT > MODULE
    
    /* ====================================================================
     * Module End Marker
     * ====================================================================*/
    
    . = ALIGN(MODULE_ALIGN);
    __module_end = .;
    __module_size = __module_end - __module_start;
    
    /* ====================================================================
     * Discarded Sections
     * ====================================================================*/
    
    /DISCARD/ :
    {
        /* Discard sections not needed in modules */
        *(.comment)
        *(.note)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
        
        /* Discard debugging in release builds */
        *(.debug_aranges)
        *(.debug_pubnames)
        *(.debug_pubtypes)
        *(.debug_loc)
        *(.debug_macinfo)
        *(.debug_ranges)
    }
}

/* ========================================================================
 * Symbol Definitions for Module Builder
 * ========================================================================*/

/* Provide symbols for module header offsets */
PROVIDE(__header_offset = __module_header_start - __module_start);
PROVIDE(__code_offset = __text_start - __module_start);
PROVIDE(__data_offset = __data_start - __module_start);
PROVIDE(__vtable_offset = __vtable_start - __module_start);
PROVIDE(__deps_offset = __deps_start - __module_start);
PROVIDE(__info_offset = __module_info_start - __module_start);

/* Provide section sizes */
PROVIDE(__header_size = __module_header_end - __module_header_start);
PROVIDE(__code_size = __text_end - __text_start);
PROVIDE(__data_size = __data_end - __data_start);
PROVIDE(__rodata_size = __rodata_end - __rodata_start);
PROVIDE(__bss_size = __bss_end - __bss_start);
PROVIDE(__vtable_size = __vtable_end - __vtable_start);

/* ========================================================================
 * Module Size Validation
 * ========================================================================*/

/* Ensure module doesn't exceed maximum size */
ASSERT(__module_size <= MODULE_MAX_SIZE, "Module exceeds maximum size of 64KB")

/* Ensure header is properly sized */
ASSERT(__header_size == 128, "Module header must be exactly 128 bytes")

/* Ensure proper alignment */
ASSERT((__module_start & (MODULE_ALIGN - 1)) == 0, "Module start not properly aligned")
ASSERT((__text_start & 3) == 0, "Code section not properly aligned")
ASSERT((__data_start & 1) == 0, "Data section not properly aligned")

/* ========================================================================
 * DOS Memory Model Compatibility
 * ========================================================================*/

/* For large memory model compatibility */
PROVIDE(__LARGE__ = 1);

/* DOS segment definitions for relocation */
PROVIDE(__text_segment = (__text_start >> 4));
PROVIDE(__data_segment = (__data_start >> 4));

/* ========================================================================
 * Module Checksum Area
 * ========================================================================*/

/* Reserve space for module checksum (calculated post-link) */
.checksum_area : ALIGN(1)
{
    __checksum_start = .;
    LONG(0x00000000)  /* CRC32 checksum placeholder */
    __checksum_end = .;
} > MODULE AT > MODULE

/* ========================================================================
 * Entry Point Declaration
 * ========================================================================*/

/* 
 * Module entry point - this should be defined in each module as:
 * 
 * For hardware modules:
 *   nic_ops_t* module_entry(uint8_t nic_id, core_services_t* core, 
 *                          const hardware_info_t* hw_info);
 * 
 * For feature modules:
 *   bool module_entry(core_services_t* core, const module_config_t* config);
 */
EXTERN(module_entry)

/* ========================================================================
 * Version Information
 * ========================================================================*/

PROVIDE(__linker_script_version = 0x0100);  /* Version 1.0 */
PROVIDE(__module_format_version = 0x0100);  /* Module format version 1.0 */