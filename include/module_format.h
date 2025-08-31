/**
 * @file module_format.h
 * @brief Module Binary Format Specification for 3Com Packet Driver
 * 
 * Phase 3A: Dynamic Module Loading - Module Format Standards
 * 
 * This header defines the complete binary format for .MOD files,
 * including layout, relocation, and validation requirements.
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef MODULE_FORMAT_H
#define MODULE_FORMAT_H

#include "module_api.h"
#include <stdint.h>

/* Module file format version */
#define MODULE_FORMAT_VERSION 0x0100  /* Version 1.0 */

/* Module file signature */
#define MODULE_FILE_SIGNATURE "3CMOD"
#define MODULE_FILE_SIGNATURE_LENGTH 5

/* Section alignment requirements */
#define MODULE_SECTION_ALIGN 16  /* 16-byte alignment for all sections */
#define MODULE_CODE_ALIGN 4      /* 4-byte alignment for code */
#define MODULE_DATA_ALIGN 2      /* 2-byte alignment for data */

/* ============================================================================
 * Module File Header
 * ============================================================================ */

/**
 * @brief Module file header (separate from module_header_t)
 * 
 * This header appears at the beginning of the .MOD file and contains
 * file-level information. The module_header_t follows this.
 */
typedef struct {
    char     signature[MODULE_FILE_SIGNATURE_LENGTH]; /**< "3CMOD" signature */
    uint16_t format_version;                          /**< Format version */
    uint16_t file_flags;                              /**< File-level flags */
    uint32_t file_size;                               /**< Total file size */
    uint32_t header_offset;                           /**< Offset to module_header_t */
    uint32_t code_offset;                             /**< Offset to code section */
    uint32_t data_offset;                             /**< Offset to data section */
    uint32_t reloc_offset;                            /**< Offset to relocation table */
    uint32_t symbol_offset;                           /**< Offset to symbol table */
    uint32_t string_offset;                           /**< Offset to string table */
    uint16_t section_count;                           /**< Number of sections */
    uint16_t reloc_count;                             /**< Number of relocations */
    uint16_t symbol_count;                            /**< Number of symbols */
    uint16_t string_table_size;                       /**< String table size */
    uint32_t checksum;                                /**< File checksum (CRC32) */
    uint32_t reserved[4];                             /**< Reserved for future use */
} module_file_header_t;

/**
 * @brief Module file flags
 */
typedef enum {
    MODULE_FILE_FLAG_RELOCATABLE = 0x0001,  /**< Module requires relocation */
    MODULE_FILE_FLAG_COMPRESSED  = 0x0002,  /**< Module is compressed */
    MODULE_FILE_FLAG_ENCRYPTED   = 0x0004,  /**< Module is encrypted */
    MODULE_FILE_FLAG_DEBUG       = 0x0008,  /**< Debug information included */
    MODULE_FILE_FLAG_STRIPPED    = 0x0010,  /**< Symbols stripped */
    MODULE_FILE_FLAG_SIGNED      = 0x0020   /**< Module is digitally signed */
} module_file_flags_t;

/* ============================================================================
 * Section Definitions
 * ============================================================================ */

/**
 * @brief Section types
 */
typedef enum {
    SECTION_TYPE_NULL    = 0,  /**< Null section */
    SECTION_TYPE_CODE    = 1,  /**< Executable code */
    SECTION_TYPE_DATA    = 2,  /**< Initialized data */
    SECTION_TYPE_BSS     = 3,  /**< Uninitialized data */
    SECTION_TYPE_RODATA  = 4,  /**< Read-only data */
    SECTION_TYPE_RELOC   = 5,  /**< Relocation information */
    SECTION_TYPE_SYMBOL  = 6,  /**< Symbol table */
    SECTION_TYPE_STRING  = 7,  /**< String table */
    SECTION_TYPE_DEBUG   = 8,  /**< Debug information */
    SECTION_TYPE_INIT    = 9,  /**< Initialization code */
    SECTION_TYPE_CLEANUP = 10  /**< Cleanup code */
} section_type_t;

/**
 * @brief Section flags
 */
typedef enum {
    SECTION_FLAG_ALLOC     = 0x01,  /**< Section occupies memory */
    SECTION_FLAG_EXEC      = 0x02,  /**< Section is executable */
    SECTION_FLAG_WRITE     = 0x04,  /**< Section is writable */
    SECTION_FLAG_MERGE     = 0x08,  /**< Section can be merged */
    SECTION_FLAG_STRINGS   = 0x10,  /**< Section contains strings */
    SECTION_FLAG_INFO_LINK = 0x20,  /**< Section contains link info */
    SECTION_FLAG_COMPRESSED= 0x40   /**< Section is compressed */
} section_flags_t;

/**
 * @brief Section header
 */
typedef struct {
    uint32_t name_offset;      /**< Offset to section name in string table */
    uint32_t type;             /**< Section type (section_type_t) */
    uint32_t flags;            /**< Section flags */
    uint32_t virtual_address;  /**< Virtual address when loaded */
    uint32_t file_offset;      /**< Offset in file */
    uint32_t size;             /**< Section size */
    uint32_t alignment;        /**< Required alignment */
    uint32_t info;             /**< Type-specific information */
    uint32_t entry_size;       /**< Size of entries (if applicable) */
} section_header_t;

/* ============================================================================
 * Relocation Information
 * ============================================================================ */

/**
 * @brief Relocation types
 */
typedef enum {
    RELOC_TYPE_NONE     = 0,   /**< No relocation */
    RELOC_TYPE_OFFSET16 = 1,   /**< 16-bit offset */
    RELOC_TYPE_SEGMENT  = 2,   /**< Segment address */
    RELOC_TYPE_FAR_PTR  = 3,   /**< Far pointer (segment:offset) */
    RELOC_TYPE_OFFSET32 = 4,   /**< 32-bit offset */
    RELOC_TYPE_RELATIVE = 5,   /**< PC-relative offset */
    RELOC_TYPE_BASE     = 6    /**< Base address relocation */
} relocation_type_t;

/**
 * @brief Relocation entry
 */
typedef struct {
    uint32_t offset;           /**< Offset in section to relocate */
    uint32_t symbol_index;     /**< Symbol table index */
    uint16_t type;             /**< Relocation type */
    uint16_t section_index;    /**< Section containing the relocation */
    int32_t  addend;           /**< Addend for relocation */
} relocation_entry_t;

/* ============================================================================
 * Symbol Table
 * ============================================================================ */

/**
 * @brief Symbol binding types
 */
typedef enum {
    SYMBOL_BIND_LOCAL  = 0,  /**< Local symbol */
    SYMBOL_BIND_GLOBAL = 1,  /**< Global symbol */
    SYMBOL_BIND_WEAK   = 2   /**< Weak symbol */
} symbol_binding_t;

/**
 * @brief Symbol types
 */
typedef enum {
    SYMBOL_TYPE_NOTYPE  = 0,  /**< No type specified */
    SYMBOL_TYPE_OBJECT  = 1,  /**< Data object */
    SYMBOL_TYPE_FUNC    = 2,  /**< Function */
    SYMBOL_TYPE_SECTION = 3,  /**< Section */
    SYMBOL_TYPE_FILE    = 4   /**< Source file */
} symbol_type_t;

/**
 * @brief Symbol table entry
 */
typedef struct {
    uint32_t name_offset;      /**< Offset to symbol name in string table */
    uint32_t value;            /**< Symbol value */
    uint32_t size;             /**< Symbol size */
    uint8_t  info;             /**< Symbol type and binding info */
    uint8_t  other;            /**< Reserved */
    uint16_t section_index;    /**< Section index */
} symbol_entry_t;

/* Macros for symbol info field */
#define SYMBOL_BIND(info) ((info) >> 4)
#define SYMBOL_TYPE(info) ((info) & 0xF)
#define SYMBOL_INFO(bind, type) (((bind) << 4) | ((type) & 0xF))

/* ============================================================================
 * Module Loading and Relocation
 * ============================================================================ */

/**
 * @brief Module load context
 */
typedef struct {
    void*    base_address;     /**< Module base address in memory */
    size_t   total_size;       /**< Total module size */
    uint32_t load_flags;       /**< Loading flags */
    
    /* Section mappings */
    struct {
        void*    address;      /**< Section address in memory */
        size_t   size;         /**< Section size */
        uint32_t flags;        /**< Section flags */
    } sections[16];            /**< Maximum 16 sections */
    
    /* Symbol resolution */
    struct {
        const char* name;      /**< Symbol name */
        void*       address;   /**< Symbol address */
    } *symbol_table;           /**< Resolved symbol table */
    uint16_t symbol_count;     /**< Number of symbols */
} module_load_context_t;

/**
 * @brief Module loader interface
 */
typedef struct {
    /* File operations */
    bool (*validate_file)(const void* file_data, size_t file_size);
    bool (*parse_headers)(const void* file_data, module_file_header_t* file_hdr, module_header_t* mod_hdr);
    
    /* Memory management */
    void* (*allocate_module_memory)(size_t size, uint32_t flags);
    bool  (*free_module_memory)(void* ptr, size_t size);
    
    /* Loading operations */
    bool (*load_sections)(module_load_context_t* ctx, const void* file_data);
    bool (*apply_relocations)(module_load_context_t* ctx, const void* file_data);
    bool (*resolve_symbols)(module_load_context_t* ctx, const void* file_data);
    
    /* Initialization */
    bool (*call_module_init)(module_load_context_t* ctx, core_services_t* core);
    void (*call_module_cleanup)(module_load_context_t* ctx);
} module_loader_t;

/* ============================================================================
 * Module Validation and Security
 * ============================================================================ */

/**
 * @brief Module validation flags
 */
typedef enum {
    VALIDATE_FLAG_CHECKSUM     = 0x0001,  /**< Verify file checksum */
    VALIDATE_FLAG_SIGNATURE    = 0x0002,  /**< Verify digital signature */
    VALIDATE_FLAG_SYMBOLS      = 0x0004,  /**< Validate symbol table */
    VALIDATE_FLAG_RELOCATIONS  = 0x0008,  /**< Validate relocations */
    VALIDATE_FLAG_DEPENDENCIES = 0x0010,  /**< Check dependencies */
    VALIDATE_FLAG_VERSION      = 0x0020,  /**< Check version compatibility */
    VALIDATE_FLAG_STRICT       = 0x8000   /**< Strict validation mode */
} validation_flags_t;

/**
 * @brief Validation result
 */
typedef struct {
    bool     valid;            /**< Overall validation result */
    uint32_t errors;           /**< Error flags */
    uint32_t warnings;         /**< Warning flags */
    char     error_message[256]; /**< Detailed error message */
} validation_result_t;

/**
 * @brief Validate module file
 * 
 * @param file_data Module file data
 * @param file_size File size
 * @param flags Validation flags
 * @param result Validation result
 * @return true if valid, false otherwise
 */
bool validate_module_file(const void* file_data, size_t file_size, 
                         uint32_t flags, validation_result_t* result);

/* ============================================================================
 * Module Builder Interface
 * ============================================================================ */

/**
 * @brief Module builder context
 */
typedef struct {
    /* Output file */
    void*    output_buffer;    /**< Output buffer */
    size_t   output_size;      /**< Output buffer size */
    size_t   output_used;      /**< Bytes used in output buffer */
    
    /* Section information */
    struct {
        const void* data;      /**< Section data */
        size_t      size;      /**< Section size */
        uint32_t    type;      /**< Section type */
        uint32_t    flags;     /**< Section flags */
        const char* name;      /**< Section name */
    } sections[16];
    uint16_t section_count;
    
    /* Symbol information */
    struct {
        const char* name;      /**< Symbol name */
        uint32_t    value;     /**< Symbol value */
        uint32_t    size;      /**< Symbol size */
        uint8_t     info;      /**< Symbol info */
        uint16_t    section;   /**< Section index */
    } symbols[256];
    uint16_t symbol_count;
    
    /* Relocation information */
    relocation_entry_t relocations[512];
    uint16_t relocation_count;
    
    /* String table */
    char*    string_table;
    size_t   string_table_size;
    size_t   string_table_used;
} module_builder_t;

/**
 * @brief Initialize module builder
 * 
 * @param builder Builder context
 * @param output_buffer Output buffer
 * @param buffer_size Buffer size
 * @return true on success, false on failure
 */
bool module_builder_init(module_builder_t* builder, void* output_buffer, size_t buffer_size);

/**
 * @brief Add section to module
 * 
 * @param builder Builder context
 * @param name Section name
 * @param type Section type
 * @param flags Section flags
 * @param data Section data
 * @param size Section size
 * @return Section index, -1 on error
 */
int module_builder_add_section(module_builder_t* builder, const char* name,
                              uint32_t type, uint32_t flags, 
                              const void* data, size_t size);

/**
 * @brief Add symbol to module
 * 
 * @param builder Builder context
 * @param name Symbol name
 * @param value Symbol value
 * @param size Symbol size
 * @param binding Symbol binding
 * @param type Symbol type
 * @param section Section index
 * @return Symbol index, -1 on error
 */
int module_builder_add_symbol(module_builder_t* builder, const char* name,
                             uint32_t value, uint32_t size,
                             symbol_binding_t binding, symbol_type_t type,
                             uint16_t section);

/**
 * @brief Add relocation to module
 * 
 * @param builder Builder context
 * @param offset Offset to relocate
 * @param symbol_index Symbol index
 * @param type Relocation type
 * @param section_index Section index
 * @param addend Relocation addend
 * @return true on success, false on error
 */
bool module_builder_add_relocation(module_builder_t* builder, uint32_t offset,
                                  uint32_t symbol_index, relocation_type_t type,
                                  uint16_t section_index, int32_t addend);

/**
 * @brief Finalize module and write to output
 * 
 * @param builder Builder context
 * @param module_header Module header to include
 * @return Final module size, 0 on error
 */
size_t module_builder_finalize(module_builder_t* builder, const module_header_t* module_header);

/* ============================================================================
 * Module Compression and Optimization
 * ============================================================================ */

/**
 * @brief Compression types
 */
typedef enum {
    COMPRESS_NONE = 0,   /**< No compression */
    COMPRESS_LZSS = 1,   /**< LZSS compression */
    COMPRESS_LZ77 = 2,   /**< LZ77 compression */
    COMPRESS_RLE  = 3    /**< Run-length encoding */
} compression_type_t;

/**
 * @brief Compress module section
 * 
 * @param input Input data
 * @param input_size Input size
 * @param output Output buffer
 * @param output_size Output buffer size
 * @param compression Compression type
 * @return Compressed size, 0 on error
 */
size_t compress_module_section(const void* input, size_t input_size,
                              void* output, size_t output_size,
                              compression_type_t compression);

/**
 * @brief Decompress module section
 * 
 * @param input Compressed data
 * @param input_size Compressed size
 * @param output Output buffer
 * @param output_size Output buffer size
 * @param compression Compression type
 * @return Decompressed size, 0 on error
 */
size_t decompress_module_section(const void* input, size_t input_size,
                                void* output, size_t output_size,
                                compression_type_t compression);

/* ============================================================================
 * Utility Functions and Macros
 * ============================================================================ */

/**
 * @brief Calculate CRC32 checksum
 * 
 * @param data Data to checksum
 * @param size Data size
 * @return CRC32 checksum
 */
uint32_t calculate_crc32(const void* data, size_t size);

/**
 * @brief Align value to boundary
 */
#define ALIGN_TO(value, boundary) (((value) + (boundary) - 1) & ~((boundary) - 1))

/**
 * @brief Check if value is aligned
 */
#define IS_ALIGNED_TO(value, boundary) (((value) & ((boundary) - 1)) == 0)

/**
 * @brief Get section by name
 * 
 * @param sections Section array
 * @param count Section count
 * @param strings String table
 * @param name Section name
 * @return Section header pointer, NULL if not found
 */
section_header_t* find_section_by_name(section_header_t* sections, uint16_t count,
                                      const char* strings, const char* name);

/**
 * @brief Get symbol by name
 * 
 * @param symbols Symbol array
 * @param count Symbol count
 * @param strings String table
 * @param name Symbol name
 * @return Symbol entry pointer, NULL if not found
 */
symbol_entry_t* find_symbol_by_name(symbol_entry_t* symbols, uint16_t count,
                                    const char* strings, const char* name);

/**
 * @brief Verify module format compatibility
 * 
 * @param file_version File format version
 * @return true if compatible, false otherwise
 */
static inline bool is_format_compatible(uint16_t file_version) {
    uint8_t file_major = (file_version >> 8) & 0xFF;
    uint8_t current_major = (MODULE_FORMAT_VERSION >> 8) & 0xFF;
    
    /* Major version must match */
    return (file_major == current_major);
}

/**
 * @brief Get string from string table
 * 
 * @param string_table String table
 * @param table_size String table size
 * @param offset Offset in string table
 * @return String pointer, NULL if invalid offset
 */
static inline const char* get_string(const char* string_table, size_t table_size, uint32_t offset) {
    if (offset >= table_size) return NULL;
    return string_table + offset;
}

#endif /* MODULE_FORMAT_H */