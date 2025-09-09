/**
 * @file el3_smc.c
 * @brief Self-Modifying Code Optimizer for 3Com EtherLink III
 *
 * Runtime code generation to eliminate unnecessary checks and optimize
 * hot paths based on detected hardware capabilities.
 *
 * This is essentially a primitive JIT compiler for DOS that patches
 * critical code paths at initialization time to remove runtime checks.
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include <stdint.h>
#include <string.h>
#include <dos.h>
#include "el3_core.h"
#include "../../include/logging.h"

/* x86 instruction opcodes */
#define X86_NOP         0x90        /* No operation */
#define X86_RET         0xC3        /* Return */
#define X86_JMP_SHORT   0xEB        /* Short jump */
#define X86_JMP_NEAR    0xE9        /* Near jump */
#define X86_MOV_AX_IMM  0xB8        /* MOV AX, immediate */
#define X86_MOV_DX_IMM  0xBA        /* MOV DX, immediate */
#define X86_OUT_DX_AX   0xEF        /* OUT DX, AX */
#define X86_IN_AX_DX    0xED        /* IN AX, DX */
#define X86_PUSH_AX     0x50        /* PUSH AX */
#define X86_POP_AX      0x58        /* POP AX */

/* Patch point identifiers */
enum patch_point {
    PATCH_WINDOW_SWITCH,        /* Window switching code */
    PATCH_FIFO_CHECK,          /* FIFO size checks */
    PATCH_BUS_MASTER_CHECK,    /* Bus master capability check */
    PATCH_GENERATION_CHECK,    /* Generation-specific branches */
    PATCH_CHECKSUM_OFFLOAD,    /* Hardware checksum branch */
    PATCH_MAX
};

/* Patch descriptor */
struct patch_desc {
    void *address;             /* Address to patch */
    uint8_t *original;         /* Original code backup */
    uint8_t *patched;          /* Patched code */
    uint16_t length;           /* Patch length */
    const char *name;          /* Patch name for logging */
};

/* Global patch table */
static struct patch_desc g_patches[PATCH_MAX];
static int g_patches_applied = 0;

/* Forward declarations */
static int el3_smc_patch_window_switch(struct el3_dev *dev);
static int el3_smc_patch_fifo_checks(struct el3_dev *dev);
static int el3_smc_patch_bus_master(struct el3_dev *dev);
static int el3_smc_patch_generation(struct el3_dev *dev);
static int el3_smc_patch_checksum(struct el3_dev *dev);
static void el3_smc_write_code(void *addr, const uint8_t *code, uint16_t len);
static void el3_smc_backup_code(void *addr, uint8_t *backup, uint16_t len);
static int el3_smc_verify_patch(void *addr, const uint8_t *expected, uint16_t len);

/**
 * @brief Initialize and apply SMC optimizations
 *
 * Analyzes device capabilities and patches hot path code to remove
 * unnecessary runtime checks.
 *
 * @param dev Device structure with detected capabilities
 * @return 0 on success, negative on error
 */
int el3_smc_init(struct el3_dev *dev)
{
    int ret;
    
    LOG_INFO("EL3-SMC: Initializing Self-Modifying Code optimizer");
    LOG_INFO("EL3-SMC: Device: %s, Generation: %d", 
             dev->name, dev->generation);
    
    /* Clear patch table */
    memset(g_patches, 0, sizeof(g_patches));
    
    /* Apply patches based on capabilities */
    
    /* 1. Window switching optimization for Vortex+ */
    if (dev->caps.has_permanent_window1) {
        ret = el3_smc_patch_window_switch(dev);
        if (ret < 0) {
            LOG_WARN("EL3-SMC: Failed to patch window switching");
        }
    }
    
    /* 2. FIFO size optimization */
    ret = el3_smc_patch_fifo_checks(dev);
    if (ret < 0) {
        LOG_WARN("EL3-SMC: Failed to patch FIFO checks");
    }
    
    /* 3. Bus master optimization */
    if (dev->caps.has_bus_master) {
        ret = el3_smc_patch_bus_master(dev);
        if (ret < 0) {
            LOG_WARN("EL3-SMC: Failed to patch bus master checks");
        }
    }
    
    /* 4. Generation-specific optimization */
    ret = el3_smc_patch_generation(dev);
    if (ret < 0) {
        LOG_WARN("EL3-SMC: Failed to patch generation checks");
    }
    
    /* 5. Hardware checksum optimization */
    if (dev->caps.has_hw_checksum) {
        ret = el3_smc_patch_checksum(dev);
        if (ret < 0) {
            LOG_WARN("EL3-SMC: Failed to patch checksum offload");
        }
    }
    
    LOG_INFO("EL3-SMC: Applied %d optimizations", g_patches_applied);
    
    return 0;
}

/**
 * @brief Patch window switching code for Vortex+
 *
 * Removes window switch instructions for cards with permanent window 1.
 */
static int el3_smc_patch_window_switch(struct el3_dev *dev)
{
    extern void el3_window_switch_point(void);  /* Assembly marker */
    uint8_t nop_sled[5] = {X86_NOP, X86_NOP, X86_NOP, X86_NOP, X86_NOP};
    void *patch_addr;
    
    /* Get patch point address */
    patch_addr = (void *)el3_window_switch_point;
    
    /* Backup original code */
    el3_smc_backup_code(patch_addr, 
                       g_patches[PATCH_WINDOW_SWITCH].original, 5);
    
    /* Apply NOP sled to skip window switch */
    el3_smc_write_code(patch_addr, nop_sled, 5);
    
    /* Verify patch */
    if (el3_smc_verify_patch(patch_addr, nop_sled, 5) < 0) {
        LOG_ERROR("EL3-SMC: Window switch patch verification failed");
        return -1;
    }
    
    /* Record patch */
    g_patches[PATCH_WINDOW_SWITCH].address = patch_addr;
    g_patches[PATCH_WINDOW_SWITCH].patched = nop_sled;
    g_patches[PATCH_WINDOW_SWITCH].length = 5;
    g_patches[PATCH_WINDOW_SWITCH].name = "Window Switch Removal";
    
    g_patches_applied++;
    LOG_DEBUG("EL3-SMC: Removed window switching for Vortex+");
    
    return 0;
}

/**
 * @brief Patch FIFO size checks
 *
 * Hardcodes FIFO size to avoid runtime lookups.
 */
static int el3_smc_patch_fifo_checks(struct el3_dev *dev)
{
    extern void el3_fifo_check_point(void);  /* Assembly marker */
    uint8_t patch_code[5];
    void *patch_addr;
    
    /* Get patch point address */
    patch_addr = (void *)el3_fifo_check_point;
    
    /* Generate patch: MOV AX, fifo_size */
    patch_code[0] = X86_MOV_AX_IMM;
    *(uint16_t *)&patch_code[1] = dev->caps.fifo_size;
    patch_code[3] = X86_NOP;
    patch_code[4] = X86_NOP;
    
    /* Backup original code */
    el3_smc_backup_code(patch_addr,
                       g_patches[PATCH_FIFO_CHECK].original, 5);
    
    /* Apply patch */
    el3_smc_write_code(patch_addr, patch_code, 5);
    
    /* Verify patch */
    if (el3_smc_verify_patch(patch_addr, patch_code, 5) < 0) {
        LOG_ERROR("EL3-SMC: FIFO check patch verification failed");
        return -1;
    }
    
    /* Record patch */
    g_patches[PATCH_FIFO_CHECK].address = patch_addr;
    g_patches[PATCH_FIFO_CHECK].patched = patch_code;
    g_patches[PATCH_FIFO_CHECK].length = 5;
    g_patches[PATCH_FIFO_CHECK].name = "FIFO Size Hardcode";
    
    g_patches_applied++;
    LOG_DEBUG("EL3-SMC: Hardcoded FIFO size to %d", dev->caps.fifo_size);
    
    return 0;
}

/**
 * @brief Patch bus master capability checks
 *
 * Removes branches for DMA vs PIO based on capability.
 */
static int el3_smc_patch_bus_master(struct el3_dev *dev)
{
    extern void el3_bus_master_check_point(void);  /* Assembly marker */
    uint8_t patch_code[2];
    void *patch_addr;
    
    /* Get patch point address */
    patch_addr = (void *)el3_bus_master_check_point;
    
    /* Generate patch: JMP to DMA path (skip PIO) */
    patch_code[0] = X86_JMP_SHORT;
    patch_code[1] = 0x10;  /* Jump offset to DMA code */
    
    /* Backup original code */
    el3_smc_backup_code(patch_addr,
                       g_patches[PATCH_BUS_MASTER_CHECK].original, 2);
    
    /* Apply patch */
    el3_smc_write_code(patch_addr, patch_code, 2);
    
    /* Record patch */
    g_patches[PATCH_BUS_MASTER_CHECK].address = patch_addr;
    g_patches[PATCH_BUS_MASTER_CHECK].patched = patch_code;
    g_patches[PATCH_BUS_MASTER_CHECK].length = 2;
    g_patches[PATCH_BUS_MASTER_CHECK].name = "Bus Master Direct Jump";
    
    g_patches_applied++;
    LOG_DEBUG("EL3-SMC: Optimized for bus master DMA path");
    
    return 0;
}

/**
 * @brief Patch generation-specific code
 *
 * Removes generation checks for known hardware.
 */
static int el3_smc_patch_generation(struct el3_dev *dev)
{
    extern void el3_generation_check_point(void);  /* Assembly marker */
    uint8_t patch_code[3];
    void *patch_addr;
    uint8_t jump_offset;
    
    /* Get patch point address */
    patch_addr = (void *)el3_generation_check_point;
    
    /* Calculate jump offset based on generation */
    switch (dev->generation) {
        case EL3_GEN_3C509B:
            jump_offset = 0x00;  /* No jump, fall through */
            break;
        case EL3_GEN_3C515TX:
            jump_offset = 0x20;  /* Jump to 3C515 code */
            break;
        case EL3_GEN_VORTEX:
            jump_offset = 0x40;  /* Jump to Vortex code */
            break;
        case EL3_GEN_BOOMERANG:
            jump_offset = 0x60;  /* Jump to Boomerang code */
            break;
        case EL3_GEN_CYCLONE:
            jump_offset = 0x80;  /* Jump to Cyclone code */
            break;
        case EL3_GEN_TORNADO:
            jump_offset = 0xA0;  /* Jump to Tornado code */
            break;
        default:
            return -1;  /* Unknown generation */
    }
    
    if (jump_offset == 0) {
        /* NOP out the check for 3C509B */
        patch_code[0] = X86_NOP;
        patch_code[1] = X86_NOP;
        patch_code[2] = X86_NOP;
    } else {
        /* Direct jump to generation-specific code */
        patch_code[0] = X86_JMP_SHORT;
        patch_code[1] = jump_offset;
        patch_code[2] = X86_NOP;
    }
    
    /* Backup original code */
    el3_smc_backup_code(patch_addr,
                       g_patches[PATCH_GENERATION_CHECK].original, 3);
    
    /* Apply patch */
    el3_smc_write_code(patch_addr, patch_code, 3);
    
    /* Record patch */
    g_patches[PATCH_GENERATION_CHECK].address = patch_addr;
    g_patches[PATCH_GENERATION_CHECK].patched = patch_code;
    g_patches[PATCH_GENERATION_CHECK].length = 3;
    g_patches[PATCH_GENERATION_CHECK].name = "Generation Direct Path";
    
    g_patches_applied++;
    LOG_DEBUG("EL3-SMC: Optimized for generation %d", dev->generation);
    
    return 0;
}

/**
 * @brief Patch hardware checksum offload checks
 */
static int el3_smc_patch_checksum(struct el3_dev *dev)
{
    extern void el3_checksum_check_point(void);  /* Assembly marker */
    uint8_t patch_code[2];
    void *patch_addr;
    
    /* Get patch point address */
    patch_addr = (void *)el3_checksum_check_point;
    
    /* Generate patch: JMP to hardware checksum path */
    patch_code[0] = X86_JMP_SHORT;
    patch_code[1] = 0x08;  /* Jump to HW checksum code */
    
    /* Backup original code */
    el3_smc_backup_code(patch_addr,
                       g_patches[PATCH_CHECKSUM_OFFLOAD].original, 2);
    
    /* Apply patch */
    el3_smc_write_code(patch_addr, patch_code, 2);
    
    /* Record patch */
    g_patches[PATCH_CHECKSUM_OFFLOAD].address = patch_addr;
    g_patches[PATCH_CHECKSUM_OFFLOAD].patched = patch_code;
    g_patches[PATCH_CHECKSUM_OFFLOAD].length = 2;
    g_patches[PATCH_CHECKSUM_OFFLOAD].name = "HW Checksum Direct";
    
    g_patches_applied++;
    LOG_DEBUG("EL3-SMC: Enabled direct hardware checksum path");
    
    return 0;
}

/**
 * @brief Write code to memory
 *
 * Handles segment protection in real mode.
 */
static void el3_smc_write_code(void *addr, const uint8_t *code, uint16_t len)
{
    /* In DOS real mode, code segment is writable */
    memcpy(addr, code, len);
    
    /* Flush prefetch queue on 486+ */
    __asm {
        jmp short $+2   ; Near jump to flush prefetch
    }
}

/**
 * @brief Backup original code
 */
static void el3_smc_backup_code(void *addr, uint8_t *backup, uint16_t len)
{
    memcpy(backup, addr, len);
}

/**
 * @brief Verify patch was applied correctly
 */
static int el3_smc_verify_patch(void *addr, const uint8_t *expected, uint16_t len)
{
    return memcmp(addr, expected, len) == 0 ? 0 : -1;
}

/**
 * @brief Restore original code (for debugging)
 */
int el3_smc_restore(void)
{
    int i;
    int restored = 0;
    
    LOG_INFO("EL3-SMC: Restoring original code");
    
    for (i = 0; i < PATCH_MAX; i++) {
        if (g_patches[i].address && g_patches[i].original) {
            el3_smc_write_code(g_patches[i].address,
                             g_patches[i].original,
                             g_patches[i].length);
            restored++;
            LOG_DEBUG("EL3-SMC: Restored %s", g_patches[i].name);
        }
    }
    
    LOG_INFO("EL3-SMC: Restored %d patches", restored);
    g_patches_applied = 0;
    
    return 0;
}

/**
 * @brief Get SMC statistics
 */
void el3_smc_get_stats(struct el3_smc_stats *stats)
{
    int i;
    
    stats->patches_applied = g_patches_applied;
    stats->code_bytes_modified = 0;
    
    for (i = 0; i < PATCH_MAX; i++) {
        if (g_patches[i].address) {
            stats->code_bytes_modified += g_patches[i].length;
        }
    }
    
    /* Estimate performance improvement */
    stats->cycles_saved_per_packet = 0;
    
    if (g_patches[PATCH_WINDOW_SWITCH].address) {
        stats->cycles_saved_per_packet += 20;  /* Window switch overhead */
    }
    
    if (g_patches[PATCH_FIFO_CHECK].address) {
        stats->cycles_saved_per_packet += 5;   /* Memory lookup */
    }
    
    if (g_patches[PATCH_BUS_MASTER_CHECK].address) {
        stats->cycles_saved_per_packet += 10;  /* Branch misprediction */
    }
    
    if (g_patches[PATCH_GENERATION_CHECK].address) {
        stats->cycles_saved_per_packet += 15;  /* Multiple comparisons */
    }
    
    if (g_patches[PATCH_CHECKSUM_OFFLOAD].address) {
        stats->cycles_saved_per_packet += 8;   /* Capability check */
    }
}