/**
 * @file test_enhanced_cpu_detect.c
 * @brief Test program for enhanced 386/486 CPU detection features
 *
 * This test program validates the new 386 and 486 specific detection
 * routines that were added to cpu_detect.asm
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "include/cpu_detect.h"

int main(void) {
    printf("Enhanced CPU Detection Test\n");
    printf("===========================\n\n");
    
    // Initialize CPU detection
    int init_result = cpu_detect_init();
    if (init_result != 0) {
        printf("ERROR: CPU detection initialization failed with code %d\n", init_result);
        return 1;
    }
    
    // Get basic CPU information
    cpu_info_t cpu_info;
    cpu_get_info(&cpu_info);
    
    printf("Basic CPU Information:\n");
    printf("  Type: %s\n", cpu_type_to_string(cpu_info.type));
    printf("  Vendor: %s\n", cpu_info.vendor);
    printf("  Family: %d, Model: %d, Stepping: %d\n", 
           cpu_info.family, cpu_info.model, cpu_info.stepping);
    printf("  CPUID Available: %s\n", cpu_info.has_cpuid ? "Yes" : "No");
    printf("\n");
    
    // Test basic features
    printf("Basic Features:\n");
    printf("  FPU: %s\n", cpu_has_fpu() ? "Present" : "Not Present");
    printf("  32-bit operations: %s\n", cpu_supports_32bit() ? "Supported" : "Not Supported");
    printf("  TSC: %s\n", cpu_has_tsc() ? "Present" : "Not Present");
    printf("\n");
    
    // Test 386-specific features
    printf("386-Specific Features:\n");
    printf("  Paging Support: %s\n", cpu_has_386_paging() ? "Available" : "Not Available");
    printf("  Virtual 8086 Mode: %s\n", cpu_has_386_v86_mode() ? "Supported" : "Not Supported");
    printf("  Alignment Check Flag: %s\n", cpu_has_386_ac_flag() ? "Available" : "Not Available");
    printf("\n");
    
    // Test 486-specific features
    printf("486-Specific Features:\n");
    printf("  Internal Cache: %s\n", cpu_has_486_cache() ? "Present" : "Not Present");
    printf("  Write-Back Cache: %s\n", cpu_has_486_writeback_cache() ? "Supported" : "Not Supported");
    printf("  BSWAP Instruction: %s\n", cpu_has_bswap() ? "Available" : "Not Available");
    printf("  CMPXCHG Instruction: %s\n", cpu_has_cmpxchg() ? "Available" : "Not Available");
    printf("  INVLPG Instruction: %s\n", cpu_has_invlpg() ? "Available" : "Not Available");
    printf("\n");
    
    // Test cache configuration if available
    if (cpu_has_486_cache()) {
        printf("Cache Information:\n");
        uint16_t cache_config = detect_486_cache_config();
        printf("  Cache Configuration: 0x%04X\n", cache_config);
        
        uint16_t cache_type = test_cache_type();
        printf("  Cache Type: %s\n", 
               (cache_type & 1) ? "Write-Back" : "Write-Through");
        printf("\n");
    }
    
    // Raw feature flags
    uint32_t features = cpu_get_features();
    printf("Raw Feature Flags: 0x%08X\n", features);
    
    // Detailed feature breakdown
    printf("\nDetailed Feature Breakdown:\n");
    if (features & CPU_FEATURE_FPU) printf("  - Floating Point Unit\n");
    if (features & CPU_FEATURE_VME) printf("  - Virtual 8086 Mode Extensions\n");
    if (features & CPU_FEATURE_DE) printf("  - Debugging Extensions\n");
    if (features & CPU_FEATURE_PSE) printf("  - Page Size Extensions\n");
    if (features & CPU_FEATURE_TSC) printf("  - Time Stamp Counter\n");
    if (features & CPU_FEATURE_MSR) printf("  - Model Specific Registers\n");
    if (features & CPU_FEATURE_PAE) printf("  - Physical Address Extension\n");
    if (features & CPU_FEATURE_MCE) printf("  - Machine Check Exception\n");
    if (features & CPU_FEATURE_CX8) printf("  - CMPXCHG8B instruction\n");
    if (features & CPU_FEATURE_APIC) printf("  - On-chip APIC\n");
    if (features & CPU_FEATURE_SEP) printf("  - SYSENTER/SYSEXIT\n");
    if (features & CPU_FEATURE_MTRR) printf("  - Memory Type Range Registers\n");
    if (features & CPU_FEATURE_PGE) printf("  - Page Global Enable\n");
    if (features & CPU_FEATURE_MCA) printf("  - Machine Check Architecture\n");
    if (features & CPU_FEATURE_CMOV) printf("  - Conditional Move instructions\n");
    
    // New 386/486 specific features
    if (features & CPU_FEATURE_386_PAGING) printf("  - 386 Paging Support\n");
    if (features & CPU_FEATURE_386_V86) printf("  - 386 Virtual 8086 Mode\n");
    if (features & CPU_FEATURE_386_AC) printf("  - 386 Alignment Check Flag\n");
    if (features & CPU_FEATURE_486_CACHE) printf("  - 486 Internal Cache\n");
    if (features & CPU_FEATURE_486_WRITEBACK) printf("  - 486 Write-Back Cache\n");
    if (features & CPU_FEATURE_BSWAP) printf("  - BSWAP Instruction\n");
    if (features & CPU_FEATURE_CMPXCHG) printf("  - CMPXCHG Instruction\n");
    if (features & CPU_FEATURE_INVLPG) printf("  - INVLPG Instruction\n");
    
    printf("\nCPU Detection Test Completed Successfully!\n");
    return 0;
}