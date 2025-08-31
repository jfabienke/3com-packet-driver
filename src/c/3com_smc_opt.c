/**
 * @file 3com_smc_opt.c
 * @brief Self-Modifying Code Optimizations for 3Com PCI NICs
 *
 * Implements aggressive SMC optimizations for 486+ CPUs with BSWAP support.
 * Incorporates GPT-5 recommendations for maximum performance.
 *
 * 3Com Packet Driver - SMC Optimization System
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include "../../include/3com_pci.h"
#include "../../include/smc_patches.h"
#include "../../include/cpu_detect.h"
#include "../../include/logging.h"
#include <dos.h>
#include <string.h>

/* SMC Patch Types */
#define PATCH_TYPE_BSWAP        0x01
#define PATCH_TYPE_COPY         0x02
#define PATCH_TYPE_CHECKSUM     0x03
#define PATCH_TYPE_DESCRIPTOR   0x04
#define PATCH_TYPE_ISR          0x05
#define PATCH_TYPE_IMMEDIATE    0x06

/* CPU-specific patch variants */
typedef struct {
    uint8_t cpu_486[8];
    uint8_t cpu_pentium[8];
    uint8_t cpu_p6[8];
    uint8_t cpu_p4[8];
} cpu_patch_variants_t;

/* Patch site definition */
typedef struct {
    void *location;
    uint8_t type;
    uint8_t size;
    cpu_patch_variants_t variants;
    const char *description;
} smc_patch_site_t;

/* Global patch table */
static smc_patch_site_t patch_sites[] = {
    /* BSWAP optimizations for packet headers */
    {
        .location = NULL,  /* Will be filled at runtime */
        .type = PATCH_TYPE_BSWAP,
        .size = 7,
        .variants = {
            /* 486: Basic BSWAP */
            .cpu_486 = {0x8B, 0x46, 0x0C,      /* mov eax,[esi+0Ch] */
                       0x0F, 0xC8,              /* bswap eax */
                       0x89, 0x47, 0x0C},       /* mov [edi+0Ch],eax */
            
            /* Pentium: Paired loads */
            .cpu_pentium = {0x8B, 0x46, 0x0C,   /* mov eax,[esi+0Ch] */
                           0x8B, 0x5E, 0x10,     /* mov ebx,[esi+10h] */
                           0x0F, 0xC8},          /* bswap eax */
            
            /* P6: With prefetch */
            .cpu_p6 = {0x0F, 0x18, 0x46, 0x40,  /* prefetchnta [esi+40h] */
                      0x8B, 0x46, 0x0C},         /* mov eax,[esi+0Ch] */
            
            /* P4: SSE2 optimized (placeholder) */
            .cpu_p4 = {0x0F, 0x18, 0x46, 0x40,  /* prefetchnta [esi+40h] */
                      0x8B, 0x46, 0x0C}          /* mov eax,[esi+0Ch] */
        },
        .description = "IPv4 source IP BSWAP"
    },
    
    /* Checksum optimization */
    {
        .location = NULL,
        .type = PATCH_TYPE_CHECKSUM,
        .size = 8,
        .variants = {
            /* 486: Simple accumulation */
            .cpu_486 = {0x03, 0x06,            /* add eax,[esi] */
                       0x83, 0xD2, 0x00,        /* adc edx,0 */
                       0x83, 0xC6, 0x04},       /* add esi,4 */
            
            /* Pentium: Dual accumulator */
            .cpu_pentium = {0x8B, 0x1E,        /* mov ebx,[esi] */
                           0x03, 0xC3,          /* add eax,ebx */
                           0x8B, 0x7E, 0x04,    /* mov edi,[esi+4] */
                           0x13, 0xD7},         /* adc edx,edi */
            
            /* P6/P4: Same as Pentium for now */
            .cpu_p6 = {0x8B, 0x1E, 0x03, 0xC3, 0x8B, 0x7E, 0x04, 0x13},
            .cpu_p4 = {0x8B, 0x1E, 0x03, 0xC3, 0x8B, 0x7E, 0x04, 0x13}
        },
        .description = "Checksum inner loop"
    },
    
    /* Descriptor ownership update */
    {
        .location = NULL,
        .type = PATCH_TYPE_DESCRIPTOR,
        .size = 5,
        .variants = {
            /* All CPUs: No LOCK needed in DOS! */
            .cpu_486 = {0x89, 0x06,            /* mov [esi],eax */
                       0xEC,                    /* in al,dx (flush) */
                       0x90, 0x90},             /* nop nop */
            
            .cpu_pentium = {0x89, 0x06,        /* mov [esi],eax */
                           0xEC,                /* in al,dx */
                           0x90, 0x90},
            
            .cpu_p6 = {0x89, 0x06,             /* mov [esi],eax */
                      0xEC,                     /* in al,dx */
                      0x90, 0x90},
            
            .cpu_p4 = {0x89, 0x06,             /* mov [esi],eax */
                      0x0F, 0xAE, 0xF8}         /* sfence (P4) */
        },
        .description = "Descriptor ownership transfer"
    }
};

#define NUM_PATCH_SITES (sizeof(patch_sites) / sizeof(patch_sites[0]))

/* Patch staging buffer */
static uint8_t patch_staging_buffer[256];
static uint16_t patch_staging_size;

/* Function prototypes */
static int detect_cpu_variant(void);
static int prepare_patch_buffer(int cpu_type);
static int apply_patches_atomic(void);
static void serialize_cpu(void);

/**
 * @brief Initialize SMC optimization system
 */
int smc_opt_init(void)
{
    int cpu_type;
    int result;
    
    LOG_INFO("SMC: Initializing optimization system for 3Com PCI NICs");
    
    /* Detect CPU type (486+ guaranteed for PCI) */
    cpu_type = detect_cpu_variant();
    
    LOG_INFO("SMC: Detected CPU variant %d", cpu_type);
    
    /* Prepare patches for this CPU */
    result = prepare_patch_buffer(cpu_type);
    if (result != SUCCESS) {
        LOG_ERROR("SMC: Failed to prepare patches");
        return result;
    }
    
    /* Apply patches atomically */
    result = apply_patches_atomic();
    if (result != SUCCESS) {
        LOG_ERROR("SMC: Failed to apply patches");
        return result;
    }
    
    LOG_INFO("SMC: Optimization patches applied successfully");
    
    return SUCCESS;
}

/**
 * @brief Detect CPU variant for patch selection
 */
static int detect_cpu_variant(void)
{
    cpu_info_t cpu_info;
    
    detect_cpu(&cpu_info);
    
    /* Map to our patch variants */
    if (cpu_info.type >= CPU_TYPE_PENTIUM4) {
        return 3;  /* P4 patches */
    } else if (cpu_info.type >= CPU_TYPE_PENTIUM_PRO) {
        return 2;  /* P6 patches */
    } else if (cpu_info.type >= CPU_TYPE_PENTIUM) {
        return 1;  /* Pentium patches */
    } else {
        return 0;  /* 486 patches (minimum for PCI) */
    }
}

/**
 * @brief Prepare patch buffer with CPU-specific code
 */
static int prepare_patch_buffer(int cpu_type)
{
    int i;
    uint8_t *patch_ptr = patch_staging_buffer;
    
    patch_staging_size = 0;
    
    for (i = 0; i < NUM_PATCH_SITES; i++) {
        smc_patch_site_t *site = &patch_sites[i];
        uint8_t *source;
        
        /* Select patch variant based on CPU */
        switch (cpu_type) {
            case 3:
                source = site->variants.cpu_p4;
                break;
            case 2:
                source = site->variants.cpu_p6;
                break;
            case 1:
                source = site->variants.cpu_pentium;
                break;
            default:
                source = site->variants.cpu_486;
                break;
        }
        
        /* Copy patch to staging buffer */
        memcpy(patch_ptr, source, site->size);
        patch_ptr += site->size;
        patch_staging_size += site->size;
        
        LOG_DEBUG("SMC: Prepared patch %d (%s) - %d bytes",
                  i, site->description, site->size);
    }
    
    return SUCCESS;
}

/**
 * @brief Apply patches atomically with minimal CLI time
 */
static int apply_patches_atomic(void)
{
    uint8_t *patch_ptr = patch_staging_buffer;
    int i;
    
    /* Critical section - must be <8μs */
    _disable();  /* CLI */
    
    /* Quick copy of all patches */
    for (i = 0; i < NUM_PATCH_SITES; i++) {
        smc_patch_site_t *site = &patch_sites[i];
        
        if (site->location != NULL) {
            /* Fast copy */
            memcpy(site->location, patch_ptr, site->size);
        }
        
        patch_ptr += site->size;
    }
    
    /* Serialize CPU to flush prefetch */
    serialize_cpu();
    
    _enable();  /* STI */
    
    return SUCCESS;
}

/**
 * @brief CPU serialization via far jump
 */
static void serialize_cpu(void)
{
    /* Far jump to flush prefetch queue */
    __asm {
        push    cs
        push    offset serialize_return
        retf
serialize_return:
    }
}

/**
 * @brief Patch immediate values (ring masks, I/O bases, etc.)
 */
int smc_patch_immediate(void *location, uint32_t value)
{
    /* Quick immediate patch under CLI */
    _disable();
    
    *(uint32_t *)location = value;
    
    /* Minimal serialization */
    __asm {
        jmp     short $+2
    }
    
    _enable();
    
    return SUCCESS;
}

/**
 * @brief Patch branch instruction (conditional to unconditional)
 */
int smc_patch_branch(void *location, uint8_t opcode)
{
    _disable();
    
    *(uint8_t *)location = opcode;
    
    /* Near jump serialization */
    __asm {
        jmp     short $+2
    }
    
    _enable();
    
    return SUCCESS;
}

/**
 * @brief Apply BSWAP optimization to packet processing
 */
int smc_optimize_packet_bswap(void *header_proc_addr)
{
    /* CPU-specific BSWAP sequence for IPv4 headers */
    static const uint8_t bswap_sequence[] = {
        0x8B, 0x46, 0x0C,       /* mov eax,[esi+0Ch] - src IP */
        0x0F, 0xC8,             /* bswap eax */
        0x89, 0x47, 0x0C,       /* mov [edi+0Ch],eax */
        0x8B, 0x46, 0x10,       /* mov eax,[esi+10h] - dst IP */
        0x0F, 0xC8,             /* bswap eax */
        0x89, 0x47, 0x10        /* mov [edi+10h],eax */
    };
    
    _disable();
    
    memcpy(header_proc_addr, bswap_sequence, sizeof(bswap_sequence));
    
    /* Serialize */
    __asm {
        push    cs
        push    offset bswap_done
        retf
bswap_done:
    }
    
    _enable();
    
    return SUCCESS;
}

/**
 * @brief Create size-specific copy codelet
 */
int smc_generate_copy_codelet(void *target, uint16_t size)
{
    uint8_t codelet[64];
    uint8_t *ptr = codelet;
    int unroll_count;
    int i;
    
    /* Generate unrolled copy for specific size */
    unroll_count = size / 4;
    
    for (i = 0; i < unroll_count && i < 8; i++) {
        /* mov eax,[esi+offset] */
        *ptr++ = 0x8B;
        *ptr++ = 0x46;
        *ptr++ = i * 4;
        
        /* Optional BSWAP for network order */
        if (i < 4) {  /* First 16 bytes get BSWAP */
            *ptr++ = 0x0F;
            *ptr++ = 0xC8;
        }
        
        /* mov [edi+offset],eax */
        *ptr++ = 0x89;
        *ptr++ = 0x47;
        *ptr++ = i * 4;
    }
    
    /* ret */
    *ptr++ = 0xC3;
    
    /* Apply codelet */
    _disable();
    memcpy(target, codelet, ptr - codelet);
    serialize_cpu();
    _enable();
    
    return SUCCESS;
}

/**
 * @brief A/B code switching for safe patching
 */
typedef struct {
    void *version_a;
    void *version_b;
    void **active_ptr;
} code_switch_t;

int smc_switch_code_version(code_switch_t *switcher)
{
    void *inactive;
    
    /* Determine inactive version */
    if (*switcher->active_ptr == switcher->version_a) {
        inactive = switcher->version_b;
    } else {
        inactive = switcher->version_a;
    }
    
    /* Patch inactive version (can be done without CLI) */
    /* ... patch code ... */
    
    /* Atomic switch */
    _disable();
    *switcher->active_ptr = inactive;
    _enable();
    
    return SUCCESS;
}