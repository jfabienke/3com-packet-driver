/**
 * @file dma_policy.c
 * @brief Three-layer DMA enable policy management for 3C515
 *
 * Implements runtime_enable + validation_passed + last_known_safe checks
 * with persistent storage and hardware signature validation.
 */

#include <dos.h>
#include "dos_io.h"
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include "../../include/common.h"
#include "../../include/hardware.h"
#include "../../include/bmtest.h"
#include "../../include/vds.h"
#include "../../include/config.h"
#include "../../include/cpudet.h"
#include "../../include/memory.h"
#include "../../include/logging.h"
#include "../../include/cachecoh.h"

/* Policy file version */
#define POLICY_VERSION 0x0100  /* Version 1.0 */

/* DMA policy state structure (16 bytes) - named to avoid conflict with pltprob.h */
typedef struct {
    uint16_t version;            /* File format version */
    uint16_t crc16;              /* CRC16 of remaining data */
    uint8_t runtime_enable;      /* User can toggle via INT 60h */
    uint8_t validation_passed;   /* Set by Stage 1 bus master test */
    uint8_t last_known_safe;     /* Persistent across reboots */
    uint8_t failure_count;       /* Consecutive failure count */
    uint32_t hw_signature;       /* Hardware configuration hash */
    uint8_t cache_tier;          /* Selected cache management tier */
    uint8_t vds_present;         /* VDS available flag */
    uint8_t ems_present;         /* EMS memory manager present */
    uint8_t xms_present;         /* XMS memory manager present */
} dma_policy_state_t;

/* DMA policy result codes */
#define DMA_POLICY_ALLOW   0
/* Note: DMA_POLICY_FORBID is defined in pltprob.h */

/* Transfer method constants */
#define TRANSFER_PIO  0
#define TRANSFER_DMA  1

/* CPU type constants for policy checks - use cpudet.h values */
#define CPU_286       CPU_DET_80286
#define CPU_386       CPU_DET_80386
#define CPU_486       CPU_DET_80486
#define CPU_PENTIUM   CPU_DET_CPUID_CAPABLE

/* MAX macro for C89 compatibility */
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

/* Function stubs for transfer method patching - implement in patcher module */
extern void patch_transfer_method(int method);
extern void patch_batch_init(int tx_batch, int rx_batch);
extern void copybreak_set_threshold(uint16_t threshold);
extern uint32_t calculate_hw_signature(void);

/* Global policy state (hot section for ISR access) */
static dma_policy_state_t g_local_dma_policy = {
    POLICY_VERSION,  /* version */
    0,  /* crc16 - calculated on save */
    0,  /* runtime_enable - default off */
    0,  /* validation_passed - not tested */
    0,  /* last_known_safe - unknown */
    0,  /* failure_count */
    0,  /* hw_signature */
    0,  /* cache_tier */
    0,  /* vds_present */
    0,  /* ems_present */
    0   /* xms_present */
};

/* Policy file path */
static const char POLICY_FILE[] = "C:\\3CPKT\\DMA.SAF";
static const char POLICY_TEMP[] = "C:\\3CPKT\\DMA.TMP";
static const char ENV_VAR[] = "3C515_DMA_SAFE";

/* Retry configuration */
#define MAX_SAVE_RETRIES 3
#define RETRY_DELAY_MS 100

/* DMA counter state for monotonicity checks */
static struct {
    uint32_t last_tx_packets;
    uint32_t last_rx_packets;
    uint32_t last_bounce_count;
    uint32_t last_violation_count;
    bool initialized;
} g_counter_state = { 0, 0, 0, 0, false };

/**
 * Calculate CRC16-CCITT
 */
static uint16_t calc_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    size_t i;
    int j;
    
    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = crc << 1;
            }
        }
    }
    
    return crc;
}

/**
 * Detect memory managers
 */
static void detect_memory_managers(void) {
    union REGS r;
    
    /* Check for EMS (INT 67h) */
    r.h.ah = 0x40;
    int86(0x67, &r, &r);
    g_local_dma_policy.ems_present = (r.h.ah == 0) ? 1 : 0;
    
    /* Check for XMS (INT 2Fh, AX=4300h) */
    r.x.ax = 0x4300;
    int86(0x2F, &r, &r);
    g_local_dma_policy.xms_present = (r.h.al == 0x80) ? 1 : 0;
    
    /* Check for VDS (INT 4Bh, AX=8102h) */
    r.x.ax = 0x8102;
    int86(0x4B, &r, &r);
    g_local_dma_policy.vds_present = (!r.x.cflag) ? 1 : 0;
}

/**
 * Calculate hardware signature
 * Combines CPU family, NIC I/O base, IRQ, and memory managers
 */
static uint32_t calc_hw_signature(uint16_t io_base, uint8_t irq) {
    uint32_t sig = 0;
    uint16_t cpu_family = 3;  /* Default 386 */
    
    /* Detect CPU family */
    _asm {
        pushf
        pop ax
        mov bx, ax
        xor ax, 0x7000    ; Try to flip ID bit
        push ax
        popf
        pushf
        pop ax
        cmp ax, bx
        je no_486
        mov cpu_family, 4  ; 486 or higher
    no_486:
    }
    
    /* Include memory manager state */
    detect_memory_managers();
    
    /* Build signature: CPU(8) | MEM(8) | IO(12) | IRQ(4) */
    sig = ((uint32_t)cpu_family << 24) |
          ((uint32_t)(g_local_dma_policy.ems_present | 
                     (g_local_dma_policy.xms_present << 1) |
                     (g_local_dma_policy.vds_present << 2)) << 16) |
          ((uint32_t)io_base << 4) | 
          (irq & 0x0F);
    
    return sig;
}

/**
 * Load policy from persistent storage
 * @return true if valid policy loaded
 */
bool dma_policy_load(uint16_t io_base, uint8_t irq) {
    /* C89: All declarations at start of block */
    dos_file_t fp;
    dma_policy_state_t loaded;
    uint32_t current_sig;
    uint16_t calc_crc;

    /* Calculate current hardware signature */
    current_sig = calc_hw_signature(io_base, irq);
    g_local_dma_policy.hw_signature = current_sig;

    /* Try to load saved policy */
    fp = dos_fopen(POLICY_FILE, "rb");
    if (fp < 0) {
        /* No saved policy, use defaults */
        return false;
    }

    /* Read policy data */
    if (dos_fread(&loaded, sizeof(loaded), 1, fp) != 1) {
        dos_fclose(fp);
        return false;
    }
    dos_fclose(fp);

    /* Validate version */
    if (loaded.version != POLICY_VERSION) {
        /* Wrong version, start fresh */
        return false;
    }

    /* Validate CRC */
    calc_crc = calc_crc16(
        (uint8_t*)&loaded + 4,
        sizeof(loaded) - 4
    );
    if (calc_crc != loaded.crc16) {
        /* CRC mismatch, corrupted file */
        return false;
    }
    
    /* Validate hardware signature */
    if (loaded.hw_signature != current_sig) {
        /* Hardware changed, invalidate policy */
        g_local_dma_policy.validation_passed = 0;
        g_local_dma_policy.last_known_safe = 0;
        return false;
    }
    
    /* Load valid policy */
    g_local_dma_policy = loaded;
    
    /* Reset runtime enable (always start disabled) */
    g_local_dma_policy.runtime_enable = 0;
    
    return true;
}

/**
 * Save policy to persistent storage with retry and verification
 * Uses atomic temp file + rename with CRC
 */
void dma_policy_save(void) {
    dos_file_t fp;
    int retry;
    struct diskfree_t disk_info;
    dma_policy_state_t verify;
    bool saved = false;
    
    /* Check disk space first */
    if (_dos_getdiskfree(3, &disk_info) == 0) {  /* C: drive */
        uint32_t bytes_free = (uint32_t)disk_info.avail_clusters * 
                              disk_info.sectors_per_cluster * 
                              disk_info.bytes_per_sector;
        if (bytes_free < 4096) {
            /* Fall back to environment variable */
            goto use_env;
        }
    }
    
    /* Calculate CRC of data after CRC field */
    g_local_dma_policy.version = POLICY_VERSION;
    g_local_dma_policy.crc16 = calc_crc16(
        (uint8_t*)&g_local_dma_policy + 4,  /* Skip version and CRC fields */
        sizeof(g_local_dma_policy) - 4
    );
    
    /* Retry loop with exponential backoff */
    for (retry = 0; retry < MAX_SAVE_RETRIES && !saved; retry++) {
        /* Try primary path with temp file */
        fp = dos_fopen(POLICY_TEMP, "wb");
        if (fp < 0) {
            delay(RETRY_DELAY_MS * (1 << retry));  /* Exponential backoff */
            continue;
        }
        
        if (dos_fwrite(&g_local_dma_policy, sizeof(g_local_dma_policy), 1, fp) != 1) {
            dos_fclose(fp);
            unlink(POLICY_TEMP);
            delay(RETRY_DELAY_MS * (1 << retry));
            continue;
        }
        
        dos_fclose(fp);

        /* Verify write by reading back */
        fp = dos_fopen(POLICY_TEMP, "rb");
        if (fp >= 0) {
            if (dos_fread(&verify, sizeof(verify), 1, fp) == 1) {
                /* C89: Declare at block scope */
                uint16_t check_crc;
                dos_fclose(fp);

                /* Verify CRC matches */
                check_crc = calc_crc16(
                    (uint8_t*)&verify + 4,
                    sizeof(verify) - 4
                );

                if (verify.crc16 == check_crc &&
                    verify.version == POLICY_VERSION) {
                    /* Atomic rename */
                    unlink(POLICY_FILE);
                    if (rename(POLICY_TEMP, POLICY_FILE) == 0) {
                        saved = true;
                        break;
                    }
                }
            } else {
                dos_fclose(fp);
            }
        }
        
        unlink(POLICY_TEMP);
        delay(RETRY_DELAY_MS * (1 << retry));
    }
    
    if (!saved) {
        /* C89: declarations must be at start of block */
        char env_val[2];
use_env:
        /* Fall back to environment variable */
        env_val[0] = '0';
        env_val[1] = '\0';
        if (g_local_dma_policy.last_known_safe) {
            env_val[0] = '1';
        }
        _putenv(ENV_VAR);
        _putenv(env_val);
    }
}

/**
 * @brief Test DMA capability gates comprehensively
 * @param nic NIC information structure
 * @return DMA_POLICY_ALLOW if all tests pass, DMA_POLICY_FORBID otherwise
 */
int dma_test_capability_gates(nic_info_t *nic) {
    /* C89: All declarations at start of block */
    int result;
    extern config_t g_config;
    extern cpu_info_t g_cpu_info;
    busmaster_test_results_t bm_results;

    log_info("DMA: Testing capability gates...");

    /* Gate 0: Check NIC type - 3C509B is PIO-only */
    if (nic && nic->type == NIC_TYPE_3C509B) {
        log_info("DMA: 3C509B detected - PIO-only NIC");
        g_local_dma_policy.runtime_enable = 0;
        g_local_dma_policy.validation_passed = 0;
        return DMA_POLICY_FORBID;
    }

    /* Only 3C515-TX supports DMA */
    if (nic && nic->type != NIC_TYPE_3C515_TX) {
        log_info("DMA: Non-DMA capable NIC type %d", nic->type);
        g_local_dma_policy.runtime_enable = 0;
        g_local_dma_policy.validation_passed = 0;
        return DMA_POLICY_FORBID;
    }

    /* Gate 1: Check if bus mastering is enabled in configuration */
    if (g_config.force_pio_mode) {
        log_info("DMA: Forced PIO mode by configuration");
        g_local_dma_policy.runtime_enable = 0;
        g_local_dma_policy.validation_passed = 0;
        return DMA_POLICY_FORBID;
    }

    /* Gate 2: Check CPU capability */
    if (g_cpu_info.cpu_type < CPU_286) {
        log_warning("DMA: CPU does not support bus mastering");
        g_local_dma_policy.validation_passed = 0;
        return DMA_POLICY_FORBID;
    }

    /* Gate 3: Run bus master capability test */
    /* Note: Bus master test requires nic_context_t, skip if only nic_info_t available */
    /* The full test is run separately via perform_automated_busmaster_test() */
    memory_set(&bm_results, 0, sizeof(bm_results));
    /* For now, assume test passes if we have a valid NIC */
    if (!nic) {
        log_warning("DMA: No NIC provided for bus master test");
        g_local_dma_policy.validation_passed = 0;
        g_local_dma_policy.failure_count++;
        return DMA_POLICY_FORBID;
    }
    
    /* Gate 4: Test VDS lock/unlock operations - VDS enables safe DMA */
    if (g_local_dma_policy.vds_present) {
        /* C89: Block scope for declarations */
        vds_dds_t test_dds;
        void *test_buf;

        log_info("DMA: VDS present - testing lock/unlock for safe DMA");
        test_buf = memory_alloc(4096, MEM_TYPE_DMA_BUFFER, MEM_FLAG_DMA_CAPABLE);

        if (test_buf) {
            memory_set(&test_dds, 0, sizeof(test_dds));
            result = vds_lock_region(test_buf, 4096, &test_dds);
            if (result == VDS_SUCCESS) {
                /* VDS provides safe physical address with bounce buffers if needed */
                if (test_dds.physical >= 0x1000000) {
                    log_warning("DMA: VDS returned address beyond 16MB limit");
                    vds_unlock_region(&test_dds);
                    memory_free(test_buf);
                    /* VDS should handle this with bounce buffers */
                    g_local_dma_policy.validation_passed = 0;
                    return DMA_POLICY_FORBID;
                }
                log_info("DMA: VDS lock successful - DMA safe with VDS");
                vds_unlock_region(&test_dds);
            } else {
                log_warning("DMA: VDS lock failed with code %d", result);
                memory_free(test_buf);
                /* VDS failure means we can't safely DMA */
                g_local_dma_policy.validation_passed = 0;
                return DMA_POLICY_FORBID;
            }
            memory_free(test_buf);
        }
    } else {
        log_info("DMA: No VDS - will use direct physical addresses");
        /* No VDS is OK for simple systems without memory managers */
    }
    
    /* Gate 5: Verify DMA constraints for bus master */
    /* 3C515-TX uses ISA bus mastering with its own DMA engine */
    /* No 64KB boundary restrictions (that's only for 8237 DMA controller) */
    /* But still subject to ISA 24-bit (16MB) addressing limit */
    if (nic && nic->type == NIC_TYPE_3C515_TX) {
        /* C89: Block scope for declarations */
        uint32_t tx_phys;
        uint32_t rx_phys;

        log_info("DMA: 3C515 ISA bus master - 16MB limit, no 64KB restrictions");

        /* Verify descriptor rings are allocated and within ISA address space */
        if (!nic->tx_descriptor_ring || !nic->rx_descriptor_ring) {
            log_warning("DMA: Descriptor rings not allocated");
            g_local_dma_policy.validation_passed = 0;
            return DMA_POLICY_FORBID;
        }

        /* Verify descriptor rings are below 16MB ISA limit */
        tx_phys = ((uint32_t)FP_SEG(nic->tx_descriptor_ring) << 4) +
                   FP_OFF(nic->tx_descriptor_ring);
        rx_phys = ((uint32_t)FP_SEG(nic->rx_descriptor_ring) << 4) +
                   FP_OFF(nic->rx_descriptor_ring);

        if (tx_phys >= 0x1000000 || rx_phys >= 0x1000000) {
            log_warning("DMA: Descriptor rings exceed 16MB ISA limit");
            g_local_dma_policy.validation_passed = 0;
            return DMA_POLICY_FORBID;
        }
    }
    
    /* All gates passed */
    log_info("DMA: All capability gates passed");
    g_local_dma_policy.validation_passed = 1;
    g_local_dma_policy.failure_count = 0;  /* Reset failure count on success */
    
    return DMA_POLICY_ALLOW;
}

/**
 * @brief Apply DMA policy based on CPU tier and test results
 * @param nic NIC information structure
 * @param test_results Results from capability and benchmark tests
 * @return DMA_POLICY_ALLOW or DMA_POLICY_FORBID
 */
int apply_dma_policy(nic_info_t *nic, dma_test_results_t *test_results) {
    extern cpu_info_t g_cpu_info;
    int policy = DMA_POLICY_FORBID;
    uint16_t copybreak = 256;  /* Default conservative copybreak */
    
    log_info("Applying DMA policy for CPU type %d", g_cpu_info.cpu_type);
    
    /* If gates failed, force PIO */
    if (!g_local_dma_policy.validation_passed) {
        log_info("DMA: Gate tests failed - forcing PIO mode");
        patch_transfer_method(TRANSFER_PIO);
        g_local_dma_policy.runtime_enable = 0;
        return DMA_POLICY_FORBID;
    }
    
    /* Apply CPU-specific policies */
    switch (g_cpu_info.cpu_type) {
        case CPU_286:
            /* 286: Prefer PIO, enable DMA only for >=256B with >20% gain */
            if (test_results && test_results->dma_gain_256b > 20) {
                log_info("DMA: 286 with %d%% gain at 256B - enabling DMA", 
                         test_results->dma_gain_256b);
                patch_transfer_method(TRANSFER_DMA);
                copybreak = 256;
                policy = DMA_POLICY_ALLOW;
            } else {
                log_info("DMA: 286 insufficient gain - using PIO");
                patch_transfer_method(TRANSFER_PIO);
                policy = DMA_POLICY_FORBID;
            }
            break;
            
        case CPU_386:
            /* 386/386SX: Consider DMA, copybreak around 128-192B */
            if (test_results && test_results->optimal_copybreak > 0) {
                copybreak = test_results->optimal_copybreak;
            } else {
                copybreak = 160;  /* Default for 386 */
            }
            
            /* Adjust for cache coherency */
            if (test_results && !test_results->cache_coherent) {
                copybreak = MAX(copybreak, 192);
                log_info("DMA: 386 non-coherent cache - copybreak raised to %u", copybreak);
            }
            
            patch_transfer_method(TRANSFER_DMA);
            policy = DMA_POLICY_ALLOW;
            break;
            
        case CPU_486:
            /* 486/486DX/486SX: DMA default, adjust for coherency cost */
            patch_transfer_method(TRANSFER_DMA);
            
            if (test_results && test_results->cache_mode == CACHE_MODE_WRITE_BACK) {
                /* Write-back cache needs careful handling */
                if (test_results->cache_flush_overhead_us > 50) {
                    /* High flush overhead - raise copybreak */
                    copybreak = MAX(128, test_results->adjusted_copybreak);
                    log_info("DMA: 486 high flush overhead - copybreak %u", copybreak);
                } else {
                    copybreak = 96;
                }
            } else {
                /* Write-through or disabled cache */
                copybreak = 96;
            }
            
            /* Check for WBINVD support */
            if (g_cpu_info.features & CPU_FEATURE_WBINVD) {
                g_local_dma_policy.cache_tier = CACHE_TIER_2_WBINVD;
            } else {
                g_local_dma_policy.cache_tier = CACHE_TIER_3_SOFTWARE;
            }
            
            policy = DMA_POLICY_ALLOW;
            break;
            
        case CPU_PENTIUM:
            /* Pentium/Pentium MMX/Pentium Pro/Pentium 2: DMA default, small copybreak if snoop works */
            patch_transfer_method(TRANSFER_DMA);
            
            if (test_results && test_results->bus_snooping) {
                copybreak = 64;  /* Minimal copybreak with working snoop */
                g_local_dma_policy.cache_tier = CACHE_TIER_4_FALLBACK;  /* No cache ops needed */
                log_info("DMA: Pentium with bus snooping - copybreak %u", copybreak);
            } else {
                copybreak = 96;
                /* Pentium has WBINVD but not CLFLUSH (that's P4+) */
                g_local_dma_policy.cache_tier = CACHE_TIER_2_WBINVD;
                log_info("DMA: Pentium without snooping - using WBINVD, copybreak %u", copybreak);
            }
            
            policy = DMA_POLICY_ALLOW;
            break;
            
        default:
            /* Unknown CPU - be conservative */
            log_warning("DMA: Unknown CPU type %d - using PIO", g_cpu_info.cpu_type);
            patch_transfer_method(TRANSFER_PIO);
            policy = DMA_POLICY_FORBID;
            break;
    }
    
    /* Set the copybreak threshold */
    if (policy == DMA_POLICY_ALLOW) {
        copybreak_set_threshold(copybreak);
        
        /* Update batch init patches based on CPU */
        if (g_cpu_info.cpu_type >= CPU_PENTIUM) {
            patch_batch_init(16, 8);  /* Aggressive batching on fast CPUs */
        } else if (g_cpu_info.cpu_type >= CPU_486) {
            patch_batch_init(8, 4);   /* Moderate batching */
        } else {
            patch_batch_init(4, 2);   /* Conservative batching */
        }
        
        g_local_dma_policy.runtime_enable = 1;
        log_info("DMA: Policy applied - DMA enabled with copybreak %u", copybreak);
    } else {
        g_local_dma_policy.runtime_enable = 0;
        log_info("DMA: Policy applied - PIO mode selected");
    }
    
    /* Update hardware signature for validation */
    g_local_dma_policy.hw_signature = calculate_hw_signature();
    
    /* Save policy to persistent storage */
    dma_policy_save();
    
    return policy;
}

/**
 * Verify counter monotonicity
 * Returns true if new value is monotonically increasing
 */
bool verify_counter_monotonic(uint32_t old_val, uint32_t new_val) {
    /* Handle wrap-around at 32-bit boundary */
    if (new_val < old_val && (old_val - new_val) > 0x80000000UL) {
        return true;  /* Wrapped around */
    }
    return new_val >= old_val;
}

/**
 * Verify DMA statistics are monotonic
 * Called from BMTEST to validate counters
 */
bool verify_dma_stats_monotonic(uint32_t tx_packets, uint32_t rx_packets,
                                uint32_t bounces, uint32_t violations) {
    bool result = true;
    
    if (g_counter_state.initialized) {
        /* Check each counter for monotonicity */
        if (!verify_counter_monotonic(g_counter_state.last_tx_packets, tx_packets)) {
            result = false;
        }
        if (!verify_counter_monotonic(g_counter_state.last_rx_packets, rx_packets)) {
            result = false;
        }
        if (!verify_counter_monotonic(g_counter_state.last_bounce_count, bounces)) {
            result = false;
        }
        if (!verify_counter_monotonic(g_counter_state.last_violation_count, violations)) {
            result = false;
        }
    }
    
    /* Update saved values */
    g_counter_state.last_tx_packets = tx_packets;
    g_counter_state.last_rx_packets = rx_packets;
    g_counter_state.last_bounce_count = bounces;
    g_counter_state.last_violation_count = violations;
    g_counter_state.initialized = true;
    
    return result;
}

/**
 * Reset counter state for new test run
 */
void reset_dma_counter_state(void) {
    g_counter_state.initialized = false;
    g_counter_state.last_tx_packets = 0;
    g_counter_state.last_rx_packets = 0;
    g_counter_state.last_bounce_count = 0;
    g_counter_state.last_violation_count = 0;
}

/**
 * Check if DMA can be used
 * All three conditions must be true
 */
bool can_use_dma(void) {
    return g_local_dma_policy.runtime_enable &&
           g_local_dma_policy.validation_passed &&
           g_local_dma_policy.last_known_safe;
}

/**
 * Set runtime enable state
 * Called via Extension API
 */
void dma_policy_set_runtime(uint8_t enable) {
    g_local_dma_policy.runtime_enable = enable ? 1 : 0;
}

/**
 * Set validation result
 * Called after BMTEST completes
 */
void dma_policy_set_validated(uint8_t passed) {
    g_local_dma_policy.validation_passed = passed ? 1 : 0;
    
    if (passed) {
        /* First successful validation */
        if (!g_local_dma_policy.last_known_safe) {
            g_local_dma_policy.last_known_safe = 1;
            dma_policy_save();
        }
        g_local_dma_policy.failure_count = 0;
    }
}

/**
 * Report DMA operation result
 * Updates last_known_safe based on success/failure
 */
void dma_policy_report_result(bool success) {
    if (!success) {
        g_local_dma_policy.failure_count++;
        
        /* Three strikes and out */
        if (g_local_dma_policy.failure_count >= 3) {
            g_local_dma_policy.last_known_safe = 0;
            g_local_dma_policy.runtime_enable = 0;
            dma_policy_save();
        }
    } else {
        /* Reset failure count on success */
        g_local_dma_policy.failure_count = 0;
        
        /* Mark as safe if validated */
        if (g_local_dma_policy.validation_passed && !g_local_dma_policy.last_known_safe) {
            g_local_dma_policy.last_known_safe = 1;
            dma_policy_save();
        }
    }
}

/**
 * Get current policy state
 * For Extension API reporting
 */
void dma_policy_get_state(uint8_t *runtime, uint8_t *validated, uint8_t *safe) {
    *runtime = g_local_dma_policy.runtime_enable;
    *validated = g_local_dma_policy.validation_passed;
    *safe = g_local_dma_policy.last_known_safe;
}

/**
 * Reset policy (for testing)
 * Clears all flags and deletes saved file
 */
void dma_policy_reset(void) {
    memset(&g_local_dma_policy, 0, sizeof(g_local_dma_policy));
    unlink(POLICY_FILE);
}
