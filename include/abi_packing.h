/**
 * @file abi_packing.h
 * @brief ABI Structure Packing Definitions
 * 
 * Provides compiler-independent structure packing macros to ensure
 * consistent memory layout across different compilers and versions.
 * This is critical for ABI stability in modular driver architecture.
 * 
 * ARCHITECTURE: Cross-compiler ABI compatibility
 * - Consistent structure alignment
 * - Predictable field offsets
 * - Binary compatibility guarantee
 */

#ifndef ABI_PACKING_H
#define ABI_PACKING_H

/* Compiler detection and packing directives */
#ifdef __WATCOMC__
    /* Watcom C/C++ */
    #define PACKED_STRUCT_BEGIN         _Packed struct
    #define PACKED_STRUCT_END           
    #define PACKED_UNION_BEGIN          _Packed union
    #define PACKED_UNION_END            
    #define ALIGN_ATTR(n)               /* Watcom uses #pragma aux for alignment */
    #define FORCE_ALIGN(n)              
    
    /* Watcom-specific pragmas for fine control */
    #define PACK_PUSH()                 _Pragma("pack(push, 1)")
    #define PACK_POP()                  _Pragma("pack(pop)")
    
#elif defined(__GNUC__)
    /* GCC */
    #define PACKED_STRUCT_BEGIN         struct
    #define PACKED_STRUCT_END           __attribute__((packed))
    #define PACKED_UNION_BEGIN          union
    #define PACKED_UNION_END            __attribute__((packed))
    #define ALIGN_ATTR(n)               __attribute__((aligned(n)))
    #define FORCE_ALIGN(n)              __attribute__((aligned(n)))
    
    #define PACK_PUSH()                 _Pragma("pack(push, 1)")
    #define PACK_POP()                  _Pragma("pack(pop)")
    
#elif defined(_MSC_VER)
    /* Microsoft Visual C++ */
    #define PACKED_STRUCT_BEGIN         __pragma(pack(push, 1)) struct
    #define PACKED_STRUCT_END           __pragma(pack(pop))
    #define PACKED_UNION_BEGIN          __pragma(pack(push, 1)) union
    #define PACKED_UNION_END            __pragma(pack(pop))
    #define ALIGN_ATTR(n)               __declspec(align(n))
    #define FORCE_ALIGN(n)              __declspec(align(n))
    
    #define PACK_PUSH()                 __pragma(pack(push, 1))
    #define PACK_POP()                  __pragma(pack(pop))
    
#else
    /* Generic/unknown compiler - use C99 standard approach */
    #define PACKED_STRUCT_BEGIN         struct
    #define PACKED_STRUCT_END           
    #define PACKED_UNION_BEGIN          union
    #define PACKED_UNION_END            
    #define ALIGN_ATTR(n)               
    #define FORCE_ALIGN(n)              
    
    #define PACK_PUSH()                 
    #define PACK_POP()                  
    
    #warning "Unknown compiler - structure packing may not work correctly"
#endif

/* Standard ABI structure packing patterns */

/**
 * @brief Declare ABI-stable structure
 * 
 * Use this for structures that cross module boundaries or are
 * part of the stable ABI.
 * 
 * Example:
 * ABI_STRUCT(my_struct) {
 *     uint32_t field1;
 *     uint16_t field2;
 * } ABI_STRUCT_END;
 */
#define ABI_STRUCT(name)            PACK_PUSH() PACKED_STRUCT_BEGIN name
#define ABI_STRUCT_END              PACKED_STRUCT_END PACK_POP()

/**
 * @brief Declare ABI-stable union
 */
#define ABI_UNION(name)             PACK_PUSH() PACKED_UNION_BEGIN name
#define ABI_UNION_END               PACKED_UNION_END PACK_POP()

/**
 * @brief Force specific alignment for ABI structure
 * 
 * Example:
 * ABI_ALIGNED_STRUCT(my_struct, 4) {
 *     uint32_t field;
 * } ABI_STRUCT_END;
 */
#define ABI_ALIGNED_STRUCT(name, alignment) \
    PACK_PUSH() PACKED_STRUCT_BEGIN name ALIGN_ATTR(alignment)

/**
 * @brief ABI field with specific alignment
 */
#define ABI_FIELD_ALIGNED(type, name, alignment) \
    type name ALIGN_ATTR(alignment)

/* Size validation macros */

/**
 * @brief Compile-time size validation
 * 
 * Ensures structure size matches expected size for ABI compatibility.
 * 
 * Example:
 * ABI_VALIDATE_SIZE(my_struct, 16);
 */
#define ABI_VALIDATE_SIZE(struct_type, expected_size) \
    typedef char ABI_SIZE_CHECK_##struct_type[sizeof(struct_type) == (expected_size) ? 1 : -1]

/**
 * @brief Compile-time offset validation
 * 
 * Ensures field offset matches expected offset for ABI compatibility.
 * 
 * Example:
 * ABI_VALIDATE_OFFSET(my_struct, field_name, 8);
 */
#define ABI_VALIDATE_OFFSET(struct_type, field_name, expected_offset) \
    typedef char ABI_OFFSET_CHECK_##struct_type##_##field_name[ \
        offsetof(struct_type, field_name) == (expected_offset) ? 1 : -1]

/* Runtime validation functions */

/**
 * @brief Runtime structure size validation
 * 
 * @param struct_name Structure name (for logging)
 * @param actual_size Actual structure size
 * @param expected_size Expected structure size
 * @return SUCCESS if sizes match, ERROR_ABI_MISMATCH otherwise
 */
int abi_validate_struct_size(const char *struct_name, 
                             size_t actual_size, 
                             size_t expected_size);

/**
 * @brief Runtime field offset validation
 * 
 * @param struct_name Structure name (for logging)
 * @param field_name Field name (for logging)
 * @param actual_offset Actual field offset
 * @param expected_offset Expected field offset
 * @return SUCCESS if offsets match, ERROR_ABI_MISMATCH otherwise
 */
int abi_validate_field_offset(const char *struct_name,
                              const char *field_name,
                              size_t actual_offset,
                              size_t expected_offset);

/**
 * @brief Initialize ABI validation system
 * 
 * Performs basic ABI compatibility checks at startup.
 * 
 * @return SUCCESS on success, negative error code on failure
 */
int abi_init_validation(void);

/* Common ABI structure patterns for 3Com packet driver */

/**
 * @brief Standard packet header (ABI-stable)
 */
ABI_STRUCT(packet_header) {
    uint16_t length;            /* Total packet length */
    uint16_t type;              /* Packet type */
    uint8_t flags;              /* Packet flags */
    uint8_t reserved;           /* Reserved byte for alignment */
    uint16_t checksum;          /* Header checksum */
} ABI_STRUCT_END;

ABI_VALIDATE_SIZE(packet_header, 8);
ABI_VALIDATE_OFFSET(packet_header, length, 0);
ABI_VALIDATE_OFFSET(packet_header, type, 2);
ABI_VALIDATE_OFFSET(packet_header, flags, 4);
ABI_VALIDATE_OFFSET(packet_header, checksum, 6);

/**
 * @brief Module interface structure (ABI-stable)
 */
ABI_STRUCT(module_interface) {
    uint32_t version;           /* Interface version */
    uint32_t size;              /* Structure size */
    uint32_t flags;             /* Interface flags */
    uint32_t reserved;          /* Reserved for future use */
    void far *function_table;   /* Function pointer table */
} ABI_STRUCT_END;

ABI_VALIDATE_SIZE(module_interface, 20);
ABI_VALIDATE_OFFSET(module_interface, version, 0);
ABI_VALIDATE_OFFSET(module_interface, size, 4);
ABI_VALIDATE_OFFSET(module_interface, flags, 8);

/* Error codes for ABI validation */
#define ERROR_ABI_MISMATCH      -30     /* ABI structure mismatch */
#define ERROR_ABI_SIZE          -31     /* Incorrect structure size */
#define ERROR_ABI_ALIGNMENT     -32     /* Incorrect field alignment */

/* Debugging and diagnostics */

/**
 * @brief Print structure layout information
 * 
 * Debug function to print structure size and field offsets.
 * Only available in debug builds.
 * 
 * @param struct_name Structure name
 * @param struct_size Structure size
 */
void abi_debug_print_layout(const char *struct_name, size_t struct_size);

/**
 * @brief Check compiler-specific structure packing
 * 
 * Diagnostic function to verify packing is working correctly.
 * 
 * @return SUCCESS if packing works, negative error code otherwise
 */
int abi_test_packing(void);

#endif /* ABI_PACKING_H */