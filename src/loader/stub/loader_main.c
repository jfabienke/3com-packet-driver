/* Module Loader Stub - 3Com Packet Driver Modular Architecture
 * Version: 1.0
 * Date: 2025-08-22
 * 
 * Minimal loader demonstration showing module loading, symbol resolution,
 * and unloading sequence. Serves as ABI validation and reference implementation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include "../../include/module_abi.h"
#include "../../include/timing_measurement.h"

/* External functions from module_loader.c */
extern int load_module(const char *filename, module_instance_t *instance);
extern int unload_module(module_instance_t *instance);
extern void far *resolve_symbol(const char *symbol_name);

/* Demo functions */
static void print_module_info(const module_instance_t *instance) {
    const module_header_t *header = instance->header;
    
    printf("Module Information:\n");
    printf("  Name: %.11s\n", header->module_name);
    printf("  Type: %u\n", header->module_type);
    printf("  Module ID: 0x%04X\n", header->module_id);
    printf("  ABI Version: %u\n", header->abi_version);
    printf("  Total Size: %u paragraphs (%u bytes)\n", 
           header->total_size_para, header->total_size_para * 16);
    printf("  Resident Size: %u paragraphs (%u bytes)\n", 
           header->resident_size_para, header->resident_size_para * 16);
    printf("  Cold Size: %u paragraphs (%u bytes)\n", 
           header->cold_size_para, header->cold_size_para * 16);
    printf("  Exports: %u symbols\n", header->export_count);
    printf("  Relocations: %u entries\n", header->reloc_count);
    printf("  Required CPU: 0x%04X\n", header->required_cpu);
    printf("  Base Segment: 0x%04X\n", instance->module_segment);
    printf("  Status: %u\n", instance->status);
    printf("\n");
}

static void test_symbol_resolution(void) {
    printf("Testing Symbol Resolution:\n");
    
    /* Test resolving some common symbols */
    const char *test_symbols[] = {
        "hello",
        "init", 
        "cleanup",
        "nonexistent",
        NULL
    };
    
    for (int i = 0; test_symbols[i]; i++) {
        void far *addr = resolve_symbol(test_symbols[i]);
        if (addr) {
            printf("  Symbol '%s' found at %04X:%04X\n", 
                   test_symbols[i], FP_SEG(addr), FP_OFF(addr));
        } else {
            printf("  Symbol '%s' not found\n", test_symbols[i]);
        }
    }
    printf("\n");
}

static void demo_module_lifecycle(const char *module_filename) {
    module_instance_t instance;
    pit_timing_t timing;
    int result;
    
    printf("=== Module Lifecycle Demonstration ===\n");
    printf("Loading module: %s\n\n", module_filename);
    
    /* Initialize timing measurement */
    PIT_INIT();
    
    /* Measure complete loading sequence */
    PIT_START_TIMING(&timing);
    
    /* Load module */
    result = load_module(module_filename, &instance);
    
    PIT_END_TIMING(&timing);
    
    if (result != MODULE_SUCCESS) {
        printf("ERROR: Module loading failed with code 0x%04X\n", result);
        return;
    }
    
    printf("Module loaded successfully in %luμs\n", timing.elapsed_us);
    print_module_info(&instance);
    
    /* Test symbol resolution */
    test_symbol_resolution();
    
    /* Test calling module API (if it has one) */
    if (instance.header->api_offset != 0) {
        printf("Testing module API call...\n");
        
        module_api_func_t api_func = (module_api_func_t)
            MK_FP(instance.module_segment, instance.header->api_offset);
        
        /* Call API function 0 (typically "get info" or "hello") */
        TIME_FUNCTION_CALL(&timing, {
            result = api_func(0, NULL);
        });
        
        printf("API call returned 0x%04X in %luμs\n", result, timing.elapsed_us);
    }
    
    /* Unload module */
    printf("Unloading module...\n");
    result = unload_module(&instance);
    
    if (result == MODULE_SUCCESS) {
        printf("Module unloaded successfully\n");
    } else {
        printf("ERROR: Module unloading failed with code 0x%04X\n", result);
    }
    
    printf("\n");
}

static void print_usage(const char *program_name) {
    printf("3Com Packet Driver Module Loader Stub v1.0\n");
    printf("Usage: %s <module.mod> [module2.mod ...]\n", program_name);
    printf("\n");
    printf("This loader stub demonstrates the module ABI v1.0 implementation.\n");
    printf("It loads modules, tests symbol resolution, and validates the\n");
    printf("complete module lifecycle as defined in the loader contract.\n");
    printf("\n");
}

static void print_system_info(void) {
    printf("=== System Information ===\n");
    printf("DOS Version: %u.%u\n", _osmajor, _osminor);
    printf("CPU Type: ");
    
    /* Simple CPU detection */
    unsigned int cpu_type = 0;
    _asm {
        pushf
        pop ax
        mov cx, ax
        xor ax, 0x1000      ; Try to toggle AC bit
        push ax
        popf
        pushf
        pop ax
        xor ax, cx
        mov cpu_type, ax
    }
    
    if (cpu_type & 0x1000) {
        printf("80486+\n");
    } else {
        printf("80286/80386\n");
    }
    
    /* Check available memory */
    union REGS regs;
    regs.h.ah = 0x48;
    regs.x.bx = 0xFFFF;  /* Request impossible amount */
    int86(0x21, &regs, &regs);
    
    printf("Available Memory: %u paragraphs (%lu KB)\n", 
           regs.x.bx, (unsigned long)regs.x.bx * 16 / 1024);
    printf("\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    print_system_info();
    
    /* Process each module on command line */
    for (int i = 1; i < argc; i++) {
        demo_module_lifecycle(argv[i]);
    }
    
    printf("=== Module Loader Stub Demo Complete ===\n");
    return 0;
}