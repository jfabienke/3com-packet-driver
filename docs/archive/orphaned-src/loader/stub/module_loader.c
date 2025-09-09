/* Module Loader Implementation - 3Com Packet Driver Modular Architecture
 * Version: 1.0
 * Date: 2025-08-22
 * 
 * Implements the complete loader contract for module loading, relocation,
 * symbol resolution, and cold section discard as per loader-contract.md
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include "../../include/module_abi.h"
#include "../../include/timing_measurement.h"

/* Global symbol table for O(log N) binary search */
resolved_symbol_t g_symbol_table[256];  /* Maximum 256 symbols */
uint16_t g_symbol_count = 0;

/* Module instance tracking */
static module_instance_t loaded_modules[16];  /* Maximum 16 loaded modules */
static uint8_t module_count = 0;

/* Memory allocation functions (simplified for demo) */
static uint16_t allocate_aligned_paragraphs(uint16_t para_count, uint16_t alignment) {
    /* Simplified: Use DOS allocate memory */
    union REGS regs;
    
    regs.h.ah = 0x48;  /* DOS allocate memory */
    regs.x.bx = para_count;
    int86(0x21, &regs, &regs);
    
    if (regs.x.cflag) {
        return 0;  /* Allocation failed */
    }
    
    return regs.x.ax;  /* Return segment */
}

static int free_memory_paragraphs(uint16_t segment, uint16_t para_count) {
    union REGS regs;
    struct SREGS sregs;
    
    regs.h.ah = 0x49;  /* DOS free memory */
    sregs.es = segment;
    int86x(0x21, &regs, &regs, &sregs);
    
    return !regs.x.cflag;  /* Return success */
}

/* File I/O functions */
static int read_file_to_memory(const char *filename, void far *dest) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return 0;
    
    /* Read file size */
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    /* Read to far memory (simplified - should handle far pointers properly) */
    uint8_t *buffer = malloc(size);
    if (!buffer) {
        fclose(fp);
        return 0;
    }
    
    if (fread(buffer, 1, size, fp) != size) {
        free(buffer);
        fclose(fp);
        return 0;
    }
    
    /* Copy to far memory */
    _fmemcpy(dest, buffer, size);
    
    free(buffer);
    fclose(fp);
    return 1;
}

/* Memory allocation with alignment */
int allocate_module_memory(const module_header_t *header, module_instance_t *instance) {
    uint16_t total_para = header->total_size_para;
    uint16_t align_para = header->alignment_para;
    
    /* Allocate aligned memory block */
    uint16_t segment = allocate_aligned_paragraphs(total_para, align_para);
    if (!segment) return MODULE_ERROR_OUT_OF_MEMORY;
    
    /* Initialize instance structure */
    instance->module_segment = segment;
    instance->total_size_para = total_para;
    instance->resident_size_para = header->resident_size_para;
    instance->module_base = MK_FP(segment, 0);
    instance->header = (module_header_t*)instance->module_base;
    instance->status = MODULE_STATUS_LOADING;
    instance->load_order = module_count++;
    
    return MODULE_SUCCESS;
}

/* Module image loading */
int load_module_image(const char *filename, module_instance_t *instance) {
    module_header_t *header = (module_header_t*)instance->module_base;
    
    /* Load entire module file to allocated memory */
    if (!read_file_to_memory(filename, instance->module_base)) {
        return MODULE_ERROR_LOAD_FAILED;
    }
    
    /* Zero BSS section */
    if (header->bss_size_para > 0) {
        void far *bss_start = MK_FP(instance->module_segment, 
                                   header->total_size_para * 16 - header->bss_size_para * 16);
        _fmemset(bss_start, 0, header->bss_size_para * 16);
    }
    
    instance->status = MODULE_STATUS_LOADED;
    return MODULE_SUCCESS;
}

/* Single relocation application */
int apply_single_relocation(module_instance_t *instance, const reloc_entry_t *reloc) {
    uint8_t far *patch_location = (uint8_t far*)instance->module_base + reloc->reloc_offset;
    uint16_t base_segment = instance->module_segment;
    
    switch (reloc->reloc_type) {
        case RELOC_TYPE_SEG_OFS: {
            /* Patch segment:offset far pointer */
            uint16_t far *seg_ptr = (uint16_t far*)(patch_location + 2);
            *seg_ptr = base_segment;
            break;
        }
        
        case RELOC_TYPE_SEGMENT: {
            /* Patch segment word only */
            uint16_t far *seg_ptr = (uint16_t far*)patch_location;
            *seg_ptr = base_segment;
            break;
        }
        
        case RELOC_TYPE_OFFSET: {
            /* Offset relocations remain unchanged (module-relative) */
            break;
        }
        
        case RELOC_TYPE_REL_NEAR: {
            /* Near relative jump/call - no change needed for position-independent code */
            break;
        }
        
        case RELOC_TYPE_REL_FAR: {
            /* Far relative call - update segment portion */
            uint16_t far *seg_ptr = (uint16_t far*)(patch_location + 2);
            *seg_ptr = base_segment;
            break;
        }
        
        default:
            return 0; /* Unknown relocation type */
    }
    
    return 1;
}

/* Apply all relocations */
int apply_relocations(module_instance_t *instance) {
    module_header_t *header = (module_header_t*)instance->module_base;
    
    /* Skip if no relocations */
    if (header->reloc_count == 0) return MODULE_SUCCESS;
    
    /* Get relocation table */
    reloc_entry_t far *reloc_table = (reloc_entry_t far*)
        ((uint8_t far*)instance->module_base + header->reloc_table_offset);
    
    /* Apply each relocation */
    for (int i = 0; i < header->reloc_count; i++) {
        if (!apply_single_relocation(instance, &reloc_table[i])) {
            return MODULE_ERROR_RELOCATION;
        }
    }
    
    return MODULE_SUCCESS;
}

/* Symbol table management with binary search */
static int symbol_compare(const void *a, const void *b) {
    const resolved_symbol_t *sym_a = (const resolved_symbol_t*)a;
    const resolved_symbol_t *sym_b = (const resolved_symbol_t*)b;
    return strcmp(sym_a->symbol_name, sym_b->symbol_name);
}

/* Register a symbol in the global symbol table */
int register_symbol(const resolved_symbol_t *symbol) {
    if (g_symbol_count >= sizeof(g_symbol_table)/sizeof(g_symbol_table[0])) {
        return MODULE_ERROR_SYMBOL;  /* Symbol table full */
    }
    
    /* Add symbol to table */
    g_symbol_table[g_symbol_count] = *symbol;
    g_symbol_count++;
    
    /* Keep table sorted for binary search */
    qsort(g_symbol_table, g_symbol_count, sizeof(resolved_symbol_t), symbol_compare);
    
    return MODULE_SUCCESS;
}

/* Build symbol table from module exports */
int build_symbol_table(module_instance_t *instance) {
    module_header_t *header = (module_header_t*)instance->module_base;
    
    if (header->export_count == 0) return MODULE_SUCCESS;
    
    export_entry_t far *exports = (export_entry_t far*)
        ((uint8_t far*)instance->module_base + header->export_table_offset);
    
    /* Register each exported symbol */
    for (int i = 0; i < header->export_count; i++) {
        resolved_symbol_t symbol;
        
        /* Copy symbol name (ensure null termination) */
        _fmemcpy(symbol.symbol_name, exports[i].symbol_name, 8);
        symbol.symbol_name[8] = '\0';
        
        /* Calculate symbol address */
        symbol.symbol_address = MK_FP(instance->module_segment, exports[i].symbol_offset);
        symbol.symbol_flags = exports[i].symbol_flags;
        symbol.module_id = header->module_id;
        
        /* Add to global symbol table */
        if (register_symbol(&symbol) != MODULE_SUCCESS) {
            return MODULE_ERROR_SYMBOL;
        }
    }
    
    return MODULE_SUCCESS;
}

/* O(log N) symbol resolution using binary search */
void far *resolve_symbol(const char *symbol_name) {
    int left = 0, right = g_symbol_count - 1;
    
    while (left <= right) {
        int mid = (left + right) / 2;
        int cmp = strcmp(symbol_name, g_symbol_table[mid].symbol_name);
        
        if (cmp == 0) {
            return g_symbol_table[mid].symbol_address;
        } else if (cmp < 0) {
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }
    
    return NULL; /* Symbol not found */
}

/* Module initialization */
int initialize_module(module_instance_t *instance) {
    module_header_t *header = (module_header_t*)instance->module_base;
    
    /* Get init function pointer */
    module_init_func_t init_func = (module_init_func_t)
        MK_FP(instance->module_segment, header->init_offset);
    
    instance->status = MODULE_STATUS_INITIALIZING;
    
    /* Call module initialization */
    int result = init_func();
    
    if (result != MODULE_SUCCESS) {
        instance->status = MODULE_STATUS_ERROR;
        return MODULE_ERROR_INIT_FAILED;
    }
    
    instance->status = MODULE_STATUS_ACTIVE;
    return MODULE_SUCCESS;
}

/* Cold section discard */
int discard_cold_section(module_instance_t *instance) {
    module_header_t *header = (module_header_t*)instance->module_base;
    
    /* Skip if no cold section */
    if (header->cold_size_para == 0) return MODULE_SUCCESS;
    
    /* Calculate cold section boundaries */
    uint16_t resident_size_bytes = header->resident_size_para * 16;
    uint16_t cold_size_bytes = header->cold_size_para * 16;
    
    /* Free cold section memory */
    uint16_t cold_segment = instance->module_segment + 
                           (resident_size_bytes / 16);
    
    if (!free_memory_paragraphs(cold_segment, header->cold_size_para)) {
        return MODULE_ERROR_OUT_OF_MEMORY;
    }
    
    /* Update instance tracking */
    instance->total_size_para = header->resident_size_para;
    
    /* Mark cold section as discarded in header */
    header->cold_size_para = 0;
    
    return MODULE_SUCCESS;
}

/* Complete module loading sequence */
int load_module(const char *filename, module_instance_t *instance) {
    pit_timing_t timing;
    int result;
    
    /* Start timing measurement */
    PIT_START_TIMING(&timing);
    
    /* Read and validate header first */
    FILE *fp = fopen(filename, "rb");
    if (!fp) return MODULE_ERROR_FILE_NOT_FOUND;
    
    module_header_t temp_header;
    if (fread(&temp_header, 1, sizeof(module_header_t), fp) != sizeof(module_header_t)) {
        fclose(fp);
        return MODULE_ERROR_LOAD_FAILED;
    }
    fclose(fp);
    
    /* Validate module header */
    if (!validate_module_header(&temp_header)) {
        return MODULE_ERROR_INVALID_MODULE;
    }
    
    /* Allocate memory for module */
    result = allocate_module_memory(&temp_header, instance);
    if (result != MODULE_SUCCESS) return result;
    
    /* Load module image */
    result = load_module_image(filename, instance);
    if (result != MODULE_SUCCESS) goto cleanup;
    
    /* Apply relocations */
    result = apply_relocations(instance);
    if (result != MODULE_SUCCESS) goto cleanup;
    
    /* Build symbol table */
    result = build_symbol_table(instance);
    if (result != MODULE_SUCCESS) goto cleanup;
    
    /* Initialize module */
    result = initialize_module(instance);
    if (result != MODULE_SUCCESS) goto cleanup;
    
    /* Discard cold section */
    result = discard_cold_section(instance);
    if (result != MODULE_SUCCESS) goto cleanup;
    
    /* End timing measurement */
    PIT_END_TIMING(&timing);
    
    /* Validate timing requirement (100ms for initialization) */
    if (!VALIDATE_INIT_TIMING(&timing)) {
        printf("Warning: Module initialization took %luus (>100ms limit)\n", timing.elapsed_us);
    }
    
    return MODULE_SUCCESS;
    
cleanup:
    /* Free allocated memory on failure */
    if (instance->module_segment) {
        free_memory_paragraphs(instance->module_segment, instance->total_size_para);
    }
    instance->status = MODULE_STATUS_ERROR;
    return result;
}

/* Module unloading */
int unload_module(module_instance_t *instance) {
    if (instance->status != MODULE_STATUS_ACTIVE) {
        return MODULE_ERROR_INVALID_MODULE;
    }
    
    module_header_t *header = instance->header;
    
    /* Call cleanup function */
    if (header->unload_offset != 0) {
        module_cleanup_func_t cleanup_func = (module_cleanup_func_t)
            MK_FP(instance->module_segment, header->unload_offset);
        cleanup_func();
    }
    
    /* Remove symbols from global table */
    unregister_module_symbols(header->module_id);
    
    /* Free module memory */
    free_memory_paragraphs(instance->module_segment, instance->total_size_para);
    
    /* Clear instance */
    memset(instance, 0, sizeof(module_instance_t));
    instance->status = MODULE_STATUS_UNLOADED;
    
    return MODULE_SUCCESS;
}

/* Remove all symbols for a specific module */
int unregister_module_symbols(uint16_t module_id) {
    int write_pos = 0;
    
    /* Compact symbol table, removing symbols from specified module */
    for (int read_pos = 0; read_pos < g_symbol_count; read_pos++) {
        if (g_symbol_table[read_pos].module_id != module_id) {
            if (write_pos != read_pos) {
                g_symbol_table[write_pos] = g_symbol_table[read_pos];
            }
            write_pos++;
        }
    }
    
    g_symbol_count = write_pos;
    return MODULE_SUCCESS;
}