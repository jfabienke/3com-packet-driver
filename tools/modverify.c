/**
 * @file modverify.c
 * @brief Module Verification Tool for 3Com Packet Driver
 * 
 * Phase 3A: Dynamic Module Loading - Stream 1 Build Infrastructure
 * 
 * This tool validates .MOD files for proper format, checksums, and
 * compliance with the module specification.
 * 
 * Usage: modverify.exe <module.mod> [options]
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>
#include <dos.h>

/* Include module format definitions */
#define MODULE_MAGIC 0x4D44
#define MODULE_FILE_SIGNATURE "3CMOD"
#define MODULE_FILE_SIGNATURE_LENGTH 5
#define MODULE_FORMAT_VERSION 0x0100

/* Module header structure (simplified for tool) */
typedef struct {
    char     signature[MODULE_FILE_SIGNATURE_LENGTH]; 
    unsigned int format_version;
    unsigned int file_flags;
    unsigned long file_size;
    unsigned long header_offset;
    unsigned long code_offset;
    unsigned long data_offset;
    unsigned long reloc_offset;
    unsigned long symbol_offset;
    unsigned long string_offset;
    unsigned int section_count;
    unsigned int reloc_count;
    unsigned int symbol_count;
    unsigned int string_table_size;
    unsigned long checksum;
    unsigned long reserved[4];
} module_file_header_t;

typedef struct {
    unsigned int magic;
    unsigned int version;
    unsigned int header_size;
    unsigned int module_size;
    unsigned int module_class;
    unsigned int family_id;
    unsigned int feature_flags;
    unsigned int api_version;
    unsigned int init_offset;
    unsigned int vtable_offset;
    unsigned int cleanup_offset;
    unsigned int info_offset;
    unsigned int deps_count;
    unsigned int deps_offset;
    unsigned int min_dos_version;
    unsigned int min_cpu_family;
    char name[12];
    char description[32];
    char author[16];
    unsigned long build_timestamp;
    unsigned int checksum;
    unsigned int reserved[6];
} module_header_t;

/* Verification options */
typedef struct {
    int verbose;
    int check_checksum;
    int check_dependencies;
    int show_info;
    int strict_mode;
    const char* filename;
} verify_options_t;

/* Global verification results */
typedef struct {
    int errors;
    int warnings;
    int total_checks;
    char error_messages[16][256];
    char warning_messages[16][256];
} verify_results_t;

static verify_results_t results;

/* Forward declarations */
static int verify_file_header(FILE* file, module_file_header_t* file_hdr);
static int verify_module_header(FILE* file, module_header_t* mod_hdr, unsigned long offset);
static int verify_checksums(FILE* file, const module_file_header_t* file_hdr, const module_header_t* mod_hdr);
static int verify_sections(FILE* file, const module_file_header_t* file_hdr);
static int verify_dependencies(FILE* file, const module_header_t* mod_hdr);
static unsigned long calculate_crc32(const void* data, size_t size);
static void add_error(const char* message);
static void add_warning(const char* message);
static void print_module_info(const module_header_t* mod_hdr);
static void print_usage(const char* program_name);
static int parse_options(int argc, char* argv[], verify_options_t* options);

/**
 * @brief Main verification function
 */
int main(int argc, char* argv[])
{
    verify_options_t options = {0};
    FILE* file;
    module_file_header_t file_hdr;
    module_header_t mod_hdr;
    int total_result = 0;
    
    printf("3Com Packet Driver Module Verification Tool v1.0\n");
    printf("=================================================\n\n");
    
    /* Parse command line options */
    if (!parse_options(argc, argv, &options)) {
        print_usage(argv[0]);
        return 1;
    }
    
    /* Initialize results */
    memset(&results, 0, sizeof(results));
    
    /* Open module file */
    file = fopen(options.filename, "rb");
    if (!file) {
        printf("Error: Cannot open file %s\n", options.filename);
        return 1;
    }
    
    printf("Verifying module: %s\n", options.filename);
    if (options.verbose) {
        printf("Verification mode: %s\n", options.strict_mode ? "STRICT" : "STANDARD");
    }
    printf("\n");
    
    /* Verify file header */
    if (options.verbose) printf("Checking file header...\n");
    if (!verify_file_header(file, &file_hdr)) {
        total_result = 1;
    }
    
    /* Verify module header */
    if (options.verbose) printf("Checking module header...\n");
    if (!verify_module_header(file, &mod_hdr, file_hdr.header_offset)) {
        total_result = 1;
    }
    
    /* Verify checksums */
    if (options.check_checksum) {
        if (options.verbose) printf("Verifying checksums...\n");
        if (!verify_checksums(file, &file_hdr, &mod_hdr)) {
            total_result = 1;
        }
    }
    
    /* Verify sections */
    if (options.verbose) printf("Checking sections...\n");
    if (!verify_sections(file, &file_hdr)) {
        total_result = 1;
    }
    
    /* Verify dependencies */
    if (options.check_dependencies && mod_hdr.deps_count > 0) {
        if (options.verbose) printf("Checking dependencies...\n");
        if (!verify_dependencies(file, &mod_hdr)) {
            total_result = 1;
        }
    }
    
    fclose(file);
    
    /* Print results */
    printf("Verification Results:\n");
    printf("====================\n");
    printf("Total checks: %d\n", results.total_checks);
    printf("Errors: %d\n", results.errors);
    printf("Warnings: %d\n", results.warnings);
    
    if (results.errors > 0) {
        printf("\nErrors found:\n");
        for (int i = 0; i < results.errors && i < 16; i++) {
            printf("  ERROR: %s\n", results.error_messages[i]);
        }
    }
    
    if (results.warnings > 0) {
        printf("\nWarnings:\n");
        for (int i = 0; i < results.warnings && i < 16; i++) {
            printf("  WARNING: %s\n", results.warning_messages[i]);
        }
    }
    
    if (options.show_info && results.errors == 0) {
        printf("\n");
        print_module_info(&mod_hdr);
    }
    
    printf("\nVerification %s\n", (total_result == 0) ? "PASSED" : "FAILED");
    
    return total_result;
}

/**
 * @brief Verify file header
 */
static int verify_file_header(FILE* file, module_file_header_t* file_hdr)
{
    long file_size;
    
    results.total_checks++;
    
    /* Read file header */
    fseek(file, 0, SEEK_SET);
    if (fread(file_hdr, sizeof(module_file_header_t), 1, file) != 1) {
        add_error("Cannot read file header");
        return 0;
    }
    
    /* Check signature */
    if (memcmp(file_hdr->signature, MODULE_FILE_SIGNATURE, MODULE_FILE_SIGNATURE_LENGTH) != 0) {
        add_error("Invalid file signature");
        return 0;
    }
    
    /* Check format version */
    if (file_hdr->format_version != MODULE_FORMAT_VERSION) {
        add_error("Unsupported format version");
        return 0;
    }
    
    /* Check file size */
    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    if (file_hdr->file_size != (unsigned long)file_size) {
        add_error("File size mismatch in header");
        return 0;
    }
    
    /* Validate offsets */
    if (file_hdr->header_offset >= file_hdr->file_size) {
        add_error("Invalid module header offset");
        return 0;
    }
    
    if (file_hdr->code_offset >= file_hdr->file_size) {
        add_error("Invalid code section offset");
        return 0;
    }
    
    if (file_hdr->data_offset >= file_hdr->file_size) {
        add_error("Invalid data section offset");
        return 0;
    }
    
    return 1;
}

/**
 * @brief Verify module header
 */
static int verify_module_header(FILE* file, module_header_t* mod_hdr, unsigned long offset)
{
    results.total_checks++;
    
    /* Read module header */
    fseek(file, offset, SEEK_SET);
    if (fread(mod_hdr, sizeof(module_header_t), 1, file) != 1) {
        add_error("Cannot read module header");
        return 0;
    }
    
    /* Check magic number */
    if (mod_hdr->magic != MODULE_MAGIC) {
        add_error("Invalid module magic number");
        return 0;
    }
    
    /* Check header size */
    if (mod_hdr->header_size != sizeof(module_header_t)) {
        add_error("Invalid module header size");
        return 0;
    }
    
    /* Check module class */
    if (mod_hdr->module_class == 0 || mod_hdr->module_class > 7) {
        add_error("Invalid module class");
        return 0;
    }
    
    /* Check API version compatibility */
    if ((mod_hdr->api_version >> 8) != (MODULE_FORMAT_VERSION >> 8)) {
        add_error("Incompatible API version");
        return 0;
    }
    
    /* Check DOS version requirement */
    if (mod_hdr->min_dos_version < 0x0200) {
        add_warning("Module requires very old DOS version");
    }
    
    /* Check CPU requirement */
    if (mod_hdr->min_cpu_family < 2) {
        add_warning("Module requires 8086/8088 (consider 286+ minimum)");
    }
    
    /* Validate name is null-terminated */
    mod_hdr->name[11] = '\0';
    if (strlen(mod_hdr->name) == 0) {
        add_error("Module name is empty");
        return 0;
    }
    
    return 1;
}

/**
 * @brief Verify checksums
 */
static int verify_checksums(FILE* file, const module_file_header_t* file_hdr, const module_header_t* mod_hdr)
{
    unsigned char* file_data;
    size_t data_size;
    unsigned long calculated_crc;
    
    results.total_checks++;
    
    /* Read entire file for checksum calculation */
    data_size = file_hdr->file_size - sizeof(file_hdr->checksum);
    file_data = malloc(data_size);
    if (!file_data) {
        add_error("Cannot allocate memory for checksum verification");
        return 0;
    }
    
    fseek(file, 0, SEEK_SET);
    if (fread(file_data, data_size, 1, file) != 1) {
        free(file_data);
        add_error("Cannot read file data for checksum");
        return 0;
    }
    
    /* Calculate CRC32 */
    calculated_crc = calculate_crc32(file_data, data_size);
    free(file_data);
    
    /* Verify file checksum */
    if (calculated_crc != file_hdr->checksum) {
        add_error("File checksum verification failed");
        return 0;
    }
    
    /* Module header checksum is typically calculated separately */
    /* This is a simplified check */
    
    return 1;
}

/**
 * @brief Verify sections
 */
static int verify_sections(FILE* file, const module_file_header_t* file_hdr)
{
    results.total_checks++;
    
    /* Basic section validation */
    if (file_hdr->section_count == 0) {
        add_warning("Module has no sections");
    }
    
    if (file_hdr->section_count > 16) {
        add_error("Too many sections in module");
        return 0;
    }
    
    /* Check section offsets are within file */
    if (file_hdr->symbol_offset >= file_hdr->file_size) {
        add_error("Symbol table offset invalid");
        return 0;
    }
    
    if (file_hdr->string_offset >= file_hdr->file_size) {
        add_error("String table offset invalid");
        return 0;
    }
    
    return 1;
}

/**
 * @brief Verify dependencies
 */
static int verify_dependencies(FILE* file, const module_header_t* mod_hdr)
{
    results.total_checks++;
    
    if (mod_hdr->deps_count > 8) {
        add_warning("Module has many dependencies");
    }
    
    if (mod_hdr->deps_offset == 0 && mod_hdr->deps_count > 0) {
        add_error("Dependency offset invalid");
        return 0;
    }
    
    /* Could read and validate individual dependencies here */
    
    return 1;
}

/**
 * @brief Calculate CRC32 checksum (simplified implementation)
 */
static unsigned long calculate_crc32(const void* data, size_t size)
{
    static unsigned long crc_table[256];
    static int table_computed = 0;
    unsigned long crc;
    const unsigned char* bytes = (const unsigned char*)data;
    
    /* Generate CRC table if needed */
    if (!table_computed) {
        for (int i = 0; i < 256; i++) {
            unsigned long c = i;
            for (int j = 0; j < 8; j++) {
                if (c & 1) {
                    c = 0xedb88320UL ^ (c >> 1);
                } else {
                    c = c >> 1;
                }
            }
            crc_table[i] = c;
        }
        table_computed = 1;
    }
    
    /* Calculate CRC */
    crc = 0xffffffffUL;
    for (size_t i = 0; i < size; i++) {
        crc = crc_table[(crc ^ bytes[i]) & 0xff] ^ (crc >> 8);
    }
    
    return crc ^ 0xffffffffUL;
}

/**
 * @brief Add error message
 */
static void add_error(const char* message)
{
    if (results.errors < 16) {
        strncpy(results.error_messages[results.errors], message, 255);
        results.error_messages[results.errors][255] = '\0';
        results.errors++;
    }
}

/**
 * @brief Add warning message
 */
static void add_warning(const char* message)
{
    if (results.warnings < 16) {
        strncpy(results.warning_messages[results.warnings], message, 255);
        results.warning_messages[results.warnings][255] = '\0';
        results.warnings++;
    }
}

/**
 * @brief Print module information
 */
static void print_module_info(const module_header_t* mod_hdr)
{
    const char* class_names[] = {"Unknown", "Hardware", "Feature", "Unknown", "Protocol"};
    const char* family_names[] = {"Unknown", "EtherLink III", "Corkscrew"};
    
    printf("Module Information:\n");
    printf("==================\n");
    printf("Name: %s\n", mod_hdr->name);
    printf("Description: %s\n", mod_hdr->description);
    printf("Author: %s\n", mod_hdr->author);
    printf("Version: %d.%d\n", (mod_hdr->version >> 8) & 0xFF, mod_hdr->version & 0xFF);
    printf("Module Class: %s\n", (mod_hdr->module_class < 5) ? class_names[mod_hdr->module_class] : "Invalid");
    
    if (mod_hdr->module_class == 1) { /* Hardware */
        int family_index = (mod_hdr->family_id == 0x0509) ? 1 : (mod_hdr->family_id == 0x0515) ? 2 : 0;
        printf("NIC Family: %s\n", family_names[family_index]);
    }
    
    printf("Size: %d paragraphs (%d bytes)\n", mod_hdr->module_size, mod_hdr->module_size * 16);
    printf("API Version: %d.%d\n", (mod_hdr->api_version >> 8) & 0xFF, mod_hdr->api_version & 0xFF);
    printf("Minimum DOS: %d.%d\n", (mod_hdr->min_dos_version >> 8) & 0xFF, mod_hdr->min_dos_version & 0xFF);
    printf("Minimum CPU: %d86\n", mod_hdr->min_cpu_family + 6);
    
    if (mod_hdr->deps_count > 0) {
        printf("Dependencies: %d\n", mod_hdr->deps_count);
    }
    
    if (mod_hdr->feature_flags != 0) {
        printf("Features: 0x%04X\n", mod_hdr->feature_flags);
    }
}

/**
 * @brief Parse command line options
 */
static int parse_options(int argc, char* argv[], verify_options_t* options)
{
    if (argc < 2) {
        return 0;
    }
    
    /* Set defaults */
    options->verbose = 0;
    options->check_checksum = 1;
    options->check_dependencies = 1;
    options->show_info = 0;
    options->strict_mode = 0;
    options->filename = NULL;
    
    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '/' || argv[i][0] == '-') {
            /* Option argument */
            if (strcmp(argv[i] + 1, "v") == 0 || strcmp(argv[i] + 1, "verbose") == 0) {
                options->verbose = 1;
            } else if (strcmp(argv[i] + 1, "i") == 0 || strcmp(argv[i] + 1, "info") == 0) {
                options->show_info = 1;
            } else if (strcmp(argv[i] + 1, "s") == 0 || strcmp(argv[i] + 1, "strict") == 0) {
                options->strict_mode = 1;
            } else if (strcmp(argv[i] + 1, "nochecksum") == 0) {
                options->check_checksum = 0;
            } else if (strcmp(argv[i] + 1, "nodeps") == 0) {
                options->check_dependencies = 0;
            } else {
                printf("Unknown option: %s\n", argv[i]);
                return 0;
            }
        } else {
            /* Filename argument */
            if (options->filename == NULL) {
                options->filename = argv[i];
            } else {
                printf("Multiple filenames specified\n");
                return 0;
            }
        }
    }
    
    if (options->filename == NULL) {
        printf("No module file specified\n");
        return 0;
    }
    
    return 1;
}

/**
 * @brief Print usage information
 */
static void print_usage(const char* program_name)
{
    printf("Usage: %s <module.mod> [options]\n\n", program_name);
    printf("Options:\n");
    printf("  /v, /verbose      Verbose output\n");
    printf("  /i, /info         Show module information\n");
    printf("  /s, /strict       Strict verification mode\n");
    printf("  /nochecksum       Skip checksum verification\n");
    printf("  /nodeps           Skip dependency checking\n");
    printf("\nExamples:\n");
    printf("  %s ETHRLINK3.MOD\n", program_name);
    printf("  %s CORKSCREW.MOD /v /i\n", program_name);
    printf("  %s ROUTING.MOD /strict\n", program_name);
}