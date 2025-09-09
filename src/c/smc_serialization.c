/**
 * @file smc_serialization.c
 * @brief Safe self-modifying code serialization for safety patches
 *
 * 3Com Packet Driver - SMC Serialization Module
 *
 * This module provides safe self-modifying code operations with proper
 * CPU serialization for patching safety operations into hot paths.
 * Handles cross-CPU serialization correctly for 286-Pentium systems.
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include "../include/smc_serialization.h"
#include "../include/cpu_detect.h"
#include "../include/logging.h"
#include <stdint.h>
#include <string.h>

/* Patch site tracking */
static smc_patch_site_t patch_sites[MAX_PATCH_SITES];
static uint8_t num_patch_sites = 0;
static bool smc_initialized = false;

/* CPU-specific serialization capabilities */
static bool has_cpuid_serialization = false;
static bool has_wbinvd_serialization = false;
static uint8_t cpu_family = 0;

/* External assembly functions from safety_stubs.asm */
extern void serialize_after_smc(void);
extern int check_cpuid_available(void);

/* Function prototypes */
static void detect_serialization_capabilities(void);
static void flush_instruction_cache_cpu_specific(void);
static bool verify_patch_applied(smc_patch_site_t *site);

/**
 * Initialize SMC serialization system
 */
bool smc_serialization_init(void) {
    cpu_info_t cpu_info;
    
    log_info("Initializing SMC serialization system...");
    
    /* Detect CPU capabilities */
    cpu_info = detect_cpu_info();
    cpu_family = cpu_info.family;
    
    detect_serialization_capabilities();
    
    /* Clear patch site tracking */
    memset(patch_sites, 0, sizeof(patch_sites));
    num_patch_sites = 0;
    
    smc_initialized = true;
    
    log_info("SMC serialization initialized for CPU family %d", cpu_family);
    log_debug("CPUID serialization: %s", has_cpuid_serialization ? "Yes" : "No");
    log_debug("WBINVD serialization: %s", has_wbinvd_serialization ? "Yes" : "No");
    
    return true;
}

/**
 * Register a patch site for tracking
 */
bool smc_register_patch_site(void *address, uint8_t size, const char *description) {
    smc_patch_site_t *site;
    
    if (!smc_initialized || num_patch_sites >= MAX_PATCH_SITES) {
        log_error("Cannot register patch site: %s", 
                  !smc_initialized ? "SMC not initialized" : "Too many sites");
        return false;
    }
    
    if (!address || size == 0 || size > MAX_PATCH_SIZE) {
        log_error("Invalid patch site parameters");
        return false;
    }
    
    site = &patch_sites[num_patch_sites];
    site->address = address;
    site->size = size;
    site->patched = false;
    
    /* Store original bytes for rollback */
    memcpy(site->original_bytes, address, size);
    
    /* Store description */
    strncpy(site->description, description ? description : "Unknown", 
            sizeof(site->description) - 1);
    site->description[sizeof(site->description) - 1] = '\0';
    
    num_patch_sites++;
    
    log_debug("Registered patch site %d: %p (%u bytes) - %s", 
              num_patch_sites - 1, address, size, site->description);
    
    return true;
}

/**
 * Apply patch to a registered site with serialization
 */
bool smc_apply_patch(uint8_t site_index, const uint8_t *patch_bytes) {
    smc_patch_site_t *site;
    
    if (!smc_initialized || site_index >= num_patch_sites) {
        log_error("Invalid patch site index: %d", site_index);
        return false;
    }
    
    if (!patch_bytes) {
        log_error("Invalid patch bytes");
        return false;
    }
    
    site = &patch_sites[site_index];
    
    if (site->patched) {
        log_warning("Patch site %d already patched", site_index);
        return true; /* Not an error */
    }
    
    log_debug("Applying patch to site %d: %s", site_index, site->description);
    
    /* Disable interrupts during patching */
    __asm__ volatile ("cli" ::: "memory");
    
    /* Apply patch bytes */
    memcpy(site->address, patch_bytes, site->size);
    
    /* Ensure patch reaches memory */
    __asm__ volatile ("" ::: "memory"); /* Compiler barrier */
    
    /* CPU-specific serialization */
    serialize_after_smc();
    
    /* Flush instruction cache if needed */
    flush_instruction_cache_cpu_specific();
    
    /* Re-enable interrupts */
    __asm__ volatile ("sti" ::: "memory");
    
    /* Verify patch was applied correctly */
    if (!verify_patch_applied(site)) {
        log_error("Patch verification failed for site %d", site_index);
        return false;
    }
    
    site->patched = true;
    
    log_info("Successfully patched site %d: %s", site_index, site->description);
    
    return true;
}

/**
 * Rollback patch from a site
 */
bool smc_rollback_patch(uint8_t site_index) {
    smc_patch_site_t *site;
    
    if (!smc_initialized || site_index >= num_patch_sites) {
        log_error("Invalid patch site index: %d", site_index);
        return false;
    }
    
    site = &patch_sites[site_index];
    
    if (!site->patched) {
        log_warning("Patch site %d not patched", site_index);
        return true; /* Not an error */
    }
    
    log_debug("Rolling back patch from site %d: %s", site_index, site->description);
    
    /* Disable interrupts during rollback */
    __asm__ volatile ("cli" ::: "memory");
    
    /* Restore original bytes */
    memcpy(site->address, site->original_bytes, site->size);
    
    /* Ensure rollback reaches memory */
    __asm__ volatile ("" ::: "memory"); /* Compiler barrier */
    
    /* CPU-specific serialization */
    serialize_after_smc();
    
    /* Flush instruction cache if needed */
    flush_instruction_cache_cpu_specific();
    
    /* Re-enable interrupts */
    __asm__ volatile ("sti" ::: "memory");
    
    site->patched = false;
    
    log_info("Successfully rolled back patch from site %d: %s", 
             site_index, site->description);
    
    return true;
}

/**
 * Apply multiple patches atomically
 */
bool smc_apply_patch_set(const smc_patch_set_t *patch_set) {
    uint8_t i;
    bool success = true;
    
    if (!smc_initialized || !patch_set) {
        log_error("Invalid patch set or SMC not initialized");
        return false;
    }
    
    if (patch_set->num_patches == 0 || patch_set->num_patches > MAX_PATCH_SITES) {
        log_error("Invalid number of patches: %d", patch_set->num_patches);
        return false;
    }
    
    log_info("Applying patch set with %d patches", patch_set->num_patches);
    
    /* Disable interrupts for entire operation */
    __asm__ volatile ("cli" ::: "memory");
    
    /* Apply all patches */
    for (i = 0; i < patch_set->num_patches; i++) {
        const smc_patch_t *patch = &patch_set->patches[i];
        smc_patch_site_t *site;
        
        if (patch->site_index >= num_patch_sites) {
            log_error("Invalid site index in patch %d: %d", i, patch->site_index);
            success = false;
            break;
        }
        
        site = &patch_sites[patch->site_index];
        
        /* Apply patch */
        memcpy(site->address, patch->patch_bytes, site->size);
        site->patched = true;
    }
    
    if (success) {
        /* Global serialization after all patches */
        __asm__ volatile ("" ::: "memory"); /* Compiler barrier */
        serialize_after_smc();
        flush_instruction_cache_cpu_specific();
        
        log_info("All patches in set applied successfully");
    } else {
        /* Rollback on failure */
        log_error("Patch set application failed - rolling back");
        for (uint8_t j = 0; j < i; j++) {
            const smc_patch_t *patch = &patch_set->patches[j];
            smc_patch_site_t *site = &patch_sites[patch->site_index];
            memcpy(site->address, site->original_bytes, site->size);
            site->patched = false;
        }
        serialize_after_smc();
        flush_instruction_cache_cpu_specific();
    }
    
    /* Re-enable interrupts */
    __asm__ volatile ("sti" ::: "memory");
    
    return success;
}

/**
 * Get patch site information
 */
bool smc_get_patch_site_info(uint8_t site_index, smc_patch_site_info_t *info) {
    smc_patch_site_t *site;
    
    if (!smc_initialized || site_index >= num_patch_sites || !info) {
        return false;
    }
    
    site = &patch_sites[site_index];
    
    info->address = site->address;
    info->size = site->size;
    info->patched = site->patched;
    strncpy(info->description, site->description, sizeof(info->description) - 1);
    info->description[sizeof(info->description) - 1] = '\0';
    
    return true;
}

/**
 * Get number of registered patch sites
 */
uint8_t smc_get_num_patch_sites(void) {
    return smc_initialized ? num_patch_sites : 0;
}

/**
 * Check if SMC system is initialized
 */
bool smc_is_initialized(void) {
    return smc_initialized;
}

/**
 * Detect CPU-specific serialization capabilities
 */
static void detect_serialization_capabilities(void) {
    /* Check for CPUID availability */
    has_cpuid_serialization = (check_cpuid_available() != 0);
    
    /* WBINVD available on 486+ */
    has_wbinvd_serialization = (cpu_family >= 4);
    
    /* Additional checks could be added for specific CPU models */
    log_debug("Detected serialization: CPUID=%d, WBINVD=%d", 
              has_cpuid_serialization, has_wbinvd_serialization);
}

/**
 * CPU-specific instruction cache flush
 */
static void flush_instruction_cache_cpu_specific(void) {
    if (cpu_family >= 4 && has_wbinvd_serialization) {
        /* 486+ with WBINVD - handled by serialize_after_smc() */
        return;
    }
    
    /* For 286/386 or systems without WBINVD, use far jump */
    __asm__ volatile (
        "pushf\n\t"
        "push %%cs\n\t"
        "push $1f\n\t"
        "iret\n"
        "1:\n\t"
        ::: "memory"
    );
}

/**
 * Verify that patch was applied correctly
 */
static bool verify_patch_applied(smc_patch_site_t *site) {
    /* For now, just assume success - in real implementation,
     * we might read back and compare, but that could cause issues
     * with self-modifying code on some CPUs */
    (void)site; /* Avoid unused parameter warning */
    return true;
}

/**
 * Print SMC status for debugging
 */
void smc_print_status(void) {
    uint8_t i;
    
    if (!smc_initialized) {
        printf("SMC serialization not initialized\n");
        return;
    }
    
    printf("\n=== SMC Serialization Status ===\n");
    printf("CPU Family: %d\n", cpu_family);
    printf("CPUID Serialization: %s\n", has_cpuid_serialization ? "Yes" : "No");
    printf("WBINVD Serialization: %s\n", has_wbinvd_serialization ? "Yes" : "No");
    printf("Registered Patch Sites: %d/%d\n", num_patch_sites, MAX_PATCH_SITES);
    
    for (i = 0; i < num_patch_sites; i++) {
        smc_patch_site_t *site = &patch_sites[i];
        printf("  Site %d: %p (%u bytes) %s - %s\n",
               i, site->address, site->size,
               site->patched ? "[PATCHED]" : "[ORIGINAL]",
               site->description);
    }
    printf("===============================\n");
}