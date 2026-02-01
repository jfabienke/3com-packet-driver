/**
 * @file entry_validation.c
 * @brief Entry point validation and environment checks implementation
 * 
 * GPT-5 A+ Enhancement: Phase 0 boot sequence validation
 * This module MUST be called first to prevent conflicts and corruption
 */

#include <dos.h>
#include "dos_io.h"
#include <stdlib.h>
#include <string.h>
#include "entval.h"
#include "logging.h"

/* Packet driver signature for installation check */
#define PKT_SIGNATURE       "PKT DRVR"
#define PKT_SIG_OFFSET      3

/* Minimum DOS version (2.0) */
#define MIN_DOS_VERSION     0x0200

/* Minimum free memory for TSR (KB) */
#define MIN_FREE_MEMORY_KB  64

/* Static helper functions */
static bool is_vector_hooked(uint8_t vector);
static uint32_t get_free_conventional_memory(void);
static bool check_pkt_signature(uint8_t vector);

/**
 * @brief Perform comprehensive entry validation
 */
int entry_validate(int argc, char *argv[], entry_validation_t *result) {
    cmdline_args_t args = {0};
    int ret;
    
    if (!result) {
        return ENTRY_ERR_CONFLICT;
    }
    
    memset(result, 0, sizeof(entry_validation_t));
    
    LOG_INFO("=== Phase 0: Entry Validation ===");
    
    /* Step 1: Parse command line */
    ret = parse_command_line(argc, argv, (cmdline_args_t __far *)&args);
    if (ret != ENTRY_SUCCESS) {
        LOG_ERROR("Command line parsing failed: %s", entry_error_string(ret));
        return ret;
    }
    
    result->target_vector = args.vector;
    LOG_INFO("Target interrupt vector: 0x%02X", result->target_vector);
    
    /* Step 2: Check if uninstall requested */
    if (args.uninstall) {
        LOG_INFO("Uninstall requested for vector 0x%02X", args.vector);
        return uninstall_packet_driver(args.vector);
    }
    
    /* Step 3: Check DOS version */
    result->dos_version = _dos_getversion();
    result->dos_compatible = check_dos_compatibility(MIN_DOS_VERSION);
    
    if (!result->dos_compatible) {
        LOG_ERROR("DOS version %d.%d too old (need 2.0+)",
                  (result->dos_version >> 8) & 0xFF,
                  result->dos_version & 0xFF);
        strcpy(result->conflict_desc, "DOS version too old");
        return ENTRY_ERR_DOS_VERSION;
    }
    
    LOG_INFO("DOS version %d.%d detected",
             (result->dos_version >> 8) & 0xFF,
             result->dos_version & 0xFF);
    
    /* Step 4: Check for existing packet driver */
    result->driver_already_installed = check_packet_driver_installed(args.vector);
    
    if (result->driver_already_installed) {
        result->existing_vector = args.vector;
        
        if (!args.force) {
            LOG_ERROR("Packet driver already installed on vector 0x%02X", args.vector);
            LOG_ERROR("Use -f to force installation or -u to uninstall");
            sprintf(result->conflict_desc, 
                    "Packet driver already on INT %02Xh", args.vector);
            return ENTRY_ERR_ALREADY_INSTALLED;
        } else {
            LOG_WARNING("Forcing installation despite existing driver");
        }
    }
    
    /* Step 5: Validate interrupt vector */
    result->vector_available = validate_interrupt_vector(args.vector);
    
    if (!result->vector_available && !args.force) {
        LOG_ERROR("Interrupt vector 0x%02X is not safe to use", args.vector);
        sprintf(result->conflict_desc, "INT %02Xh in use by another program", 
                args.vector);
        return ENTRY_ERR_VECTOR_IN_USE;
    }
    
    /* Step 6: Check memory availability */
    result->free_memory = get_free_conventional_memory();
    result->memory_sufficient = check_memory_available(MIN_FREE_MEMORY_KB);
    
    if (!result->memory_sufficient) {
        LOG_ERROR("Insufficient memory: %lu KB free, need %d KB",
                  result->free_memory / 1024, MIN_FREE_MEMORY_KB);
        sprintf(result->conflict_desc, "Only %lu KB free memory",
                result->free_memory / 1024);
        return ENTRY_ERR_MEMORY_INSUFFICIENT;
    }
    
    LOG_INFO("Free conventional memory: %lu KB", result->free_memory / 1024);
    
    /* Step 7: Additional conflict checks */
    
    /* Check for Windows */
    _asm {
        push ax
        mov ax, 0x1600
        int 0x2F
        cmp al, 0x00
        je not_windows
        cmp al, 0x80
        je not_windows
        
        ; Windows detected - warn but don't fail
        mov ax, 1
        jmp done_win_check
        
    not_windows:
        xor ax, ax
        
    done_win_check:
        pop ax
    }
    
    LOG_INFO("Entry validation complete - environment safe for installation");
    return ENTRY_SUCCESS;
}

/**
 * @brief Parse command line arguments
 */
int parse_command_line(int argc, char *argv[], cmdline_args_t __far *args) {
    int i;
    
    if (!args) {
        return ENTRY_ERR_CONFLICT;
    }
    
    /* Set defaults */
    args->vector = DEFAULT_PKT_VECTOR;
    args->uninstall = false;
    args->force = false;
    args->quiet = false;
    args->verbose = false;
    args->config_file[0] = '\0';
    
    /* Parse arguments */
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-' || argv[i][0] == '/') {
            switch (argv[i][1]) {
                case 'i':
                case 'I':
                    /* Interrupt vector */
                    if (i + 1 < argc) {
                        unsigned long vec;
                        const char *vec_str = argv[++i];
                        vec = dos_hextoul(vec_str);
                        if (vec == 0 && vec_str[0] != '0') {
                            /* dos_hextoul returned 0 but input wasn't "0" - parse error */
                            break;
                        }
                        if (vec <= 0xFF) {
                            args->vector = (uint8_t)vec;
                        } else {
                            LOG_ERROR("Invalid vector: 0x%lX", vec);
                            return ENTRY_ERR_INVALID_VECTOR;
                        }
                    }
                    break;
                    
                case 'u':
                case 'U':
                    /* Uninstall */
                    args->uninstall = true;
                    break;
                    
                case 'f':
                case 'F':
                    /* Force */
                    args->force = true;
                    break;
                    
                case 'q':
                case 'Q':
                    /* Quiet */
                    args->quiet = true;
                    break;
                    
                case 'v':
                case 'V':
                    /* Verbose */
                    args->verbose = true;
                    break;
                    
                case 'c':
                case 'C':
                    /* Config file */
                    if (i + 1 < argc) {
                        strncpy(args->config_file, argv[++i], 127);
                        args->config_file[127] = '\0';
                    }
                    break;
                    
                case '?':
                case 'h':
                case 'H':
                    /* Help */
                    printf("3Com Packet Driver\n");
                    printf("Usage: %s [options]\n", argv[0]);
                    printf("Options:\n");
                    printf("  -i XX    Use interrupt vector XX (hex)\n");
                    printf("  -u       Uninstall driver\n");
                    printf("  -f       Force installation\n");
                    printf("  -q       Quiet mode\n");
                    printf("  -v       Verbose output\n");
                    printf("  -c file  Configuration file\n");
                    printf("  -h       This help\n");
                    exit(0);
                    
                default:
                    LOG_WARNING("Unknown option: %s", argv[i]);
                    break;
            }
        }
    }
    
    /* Validate vector range */
    if (!((args->vector >= MIN_USER_VECTOR && args->vector <= MAX_USER_VECTOR) ||
          (args->vector >= MIN_ALT_VECTOR && args->vector <= MAX_ALT_VECTOR))) {
        LOG_ERROR("Vector 0x%02X outside valid ranges (60-7F, C0-CF)", 
                  args->vector);
        return ENTRY_ERR_INVALID_VECTOR;
    }
    
    return ENTRY_SUCCESS;
}

/**
 * @brief Check if packet driver already installed
 */
bool check_packet_driver_installed(uint8_t vector) {
    /* First check if vector is hooked */
    if (!is_vector_hooked(vector)) {
        return false;
    }
    
    /* Then check for packet driver signature */
    return check_pkt_signature(vector);
}

/**
 * @brief Check for packet driver signature
 */
static bool check_pkt_signature(uint8_t vector) {
    char __far *sig_ptr;
    void __far *vec_ptr;
    int i;
    
    /* Get interrupt vector */
    vec_ptr = _dos_getvect(vector);
    if (!vec_ptr) {
        return false;
    }
    
    /* Point to potential signature location */
    sig_ptr = (char __far *)vec_ptr + PKT_SIG_OFFSET;
    
    /* Check signature */
    for (i = 0; i < 8; i++) {
        if (sig_ptr[i] != PKT_SIGNATURE[i]) {
            return false;
        }
    }
    
    return true;
}

/**
 * @brief Validate interrupt vector is safe to use
 */
bool validate_interrupt_vector(uint8_t vector) {
    /* Check if vector is in valid range */
    if (!((vector >= MIN_USER_VECTOR && vector <= MAX_USER_VECTOR) ||
          (vector >= MIN_ALT_VECTOR && vector <= MAX_ALT_VECTOR))) {
        return false;
    }
    
    /* Check if vector is already hooked (but not by packet driver) */
    if (is_vector_hooked(vector) && !check_pkt_signature(vector)) {
        /* Vector is hooked by something else */
        return false;
    }
    
    return true;
}

/**
 * @brief Check if interrupt vector is hooked
 */
static bool is_vector_hooked(uint8_t vector) {
    void __far *vec_ptr;
    
    vec_ptr = _dos_getvect(vector);
    
    /* Check if vector points to NULL or IRET (0xCF) */
    if (!vec_ptr) {
        return false;
    }
    
    /* Check if it points to dummy IRET handler */
    if (*((uint8_t __far *)vec_ptr) == 0xCF) {
        return false;
    }
    
    return true;
}

/**
 * @brief Check DOS version compatibility
 */
bool check_dos_compatibility(uint16_t min_version) {
    uint16_t dos_version;
    
    dos_version = _dos_getversion();
    
    /* Version is in BCD format: major.minor */
    return (dos_version >= min_version);
}

/**
 * @brief Check if sufficient memory for TSR
 */
bool check_memory_available(uint16_t required_kb) {
    uint32_t free_mem;
    
    free_mem = get_free_conventional_memory();
    
    return (free_mem >= (uint32_t)required_kb * 1024);
}

/**
 * @brief Get free conventional memory in bytes
 */
static uint32_t get_free_conventional_memory(void) {
    union REGS regs;
    
    /* DOS function 48h - Allocate memory (with BX=FFFFh to get max) */
    regs.h.ah = 0x48;
    regs.x.bx = 0xFFFF;
    intdos(&regs, &regs);
    
    /* BX contains max paragraphs available */
    return (uint32_t)regs.x.bx * 16;
}

/**
 * @brief Get description of entry validation error
 */
const char* entry_error_string(entry_error_t error) {
    switch (error) {
        case ENTRY_SUCCESS:
            return "Success";
        case ENTRY_ERR_ALREADY_INSTALLED:
            return "Packet driver already installed";
        case ENTRY_ERR_VECTOR_IN_USE:
            return "Interrupt vector in use";
        case ENTRY_ERR_INVALID_VECTOR:
            return "Invalid interrupt vector";
        case ENTRY_ERR_DOS_VERSION:
            return "DOS version not supported";
        case ENTRY_ERR_MEMORY_INSUFFICIENT:
            return "Insufficient memory";
        case ENTRY_ERR_CONFLICT:
            return "Environment conflict detected";
        default:
            return "Unknown error";
    }
}

/**
 * @brief Print entry validation results
 */
void print_entry_validation(const entry_validation_t *result) {
    if (!result) {
        return;
    }
    
    printf("Entry Validation Results:\n");
    printf("  Target vector: 0x%02X\n", result->target_vector);
    printf("  DOS version: %d.%d\n", 
           (result->dos_version >> 8) & 0xFF,
           result->dos_version & 0xFF);
    printf("  Free memory: %lu KB\n", result->free_memory / 1024);
    printf("  Driver installed: %s\n", 
           result->driver_already_installed ? "YES" : "NO");
    printf("  Vector available: %s\n",
           result->vector_available ? "YES" : "NO");
    printf("  DOS compatible: %s\n",
           result->dos_compatible ? "YES" : "NO");
    printf("  Memory sufficient: %s\n",
           result->memory_sufficient ? "YES" : "NO");
    
    if (result->conflict_desc[0]) {
        printf("  Conflict: %s\n", result->conflict_desc);
    }
}

/**
 * @brief Uninstall existing packet driver
 */
int uninstall_packet_driver(uint8_t vector) {
    union REGS regs;
    
    /* Check if driver is installed */
    if (!check_packet_driver_installed(vector)) {
        LOG_ERROR("No packet driver found on vector 0x%02X", vector);
        return ENTRY_ERR_CONFLICT;
    }
    
    /* Call packet driver function 5 (terminate driver) */
    regs.h.ah = 0x05;
    int86(vector, &regs, &regs);
    
    if (regs.x.cflag) {
        LOG_ERROR("Failed to uninstall packet driver");
        return ENTRY_ERR_CONFLICT;
    }
    
    LOG_INFO("Packet driver uninstalled successfully");
    return ENTRY_SUCCESS;
}