/**
 * @file pci_shim_enhanced.c
 * @brief Enhanced PCI BIOS shim with V86 detection and config caching
 * 
 * Builds on the basic PCI shim to add:
 * - V86 mode detection using existing cpu_detect module
 * - Config space caching for performance
 * - Extended statistics tracking
 * - Integration with existing cache coherency module
 */

#include <dos.h>
#include <string.h>
#include "portabl.h"
#include "pci_bios.h"
#include "pci_shim.h"
#include "cpudet.h"

/* PCI BIOS function constants */
#ifndef PCI_FUNCTION_ID
#define PCI_FUNCTION_ID         0xB1
#endif
#ifndef PCI_BIOS_PRESENT
#define PCI_BIOS_PRESENT        0x01
#endif
#ifndef PCI_SUCCESSFUL
#define PCI_SUCCESSFUL          0x00
#endif
#ifndef PCI_BAD_REGISTER_NUMBER
#define PCI_BAD_REGISTER_NUMBER 0x87
#endif

/* Logging macros - stubbed for C89 compatibility (no variadic macros) */
/* Use parentheses trick: LOG_DEBUG(("fmt", args)) expands to log_noop(("fmt", args)) */
#ifndef LOG_DEBUG
#define LOG_DEBUG(x)   ((void)0)
#endif
#ifndef LOG_INFO
#define LOG_INFO(x)    ((void)0)
#endif
#ifndef LOG_WARNING
#define LOG_WARNING(x) ((void)0)
#endif
#ifndef LOG_ERROR
#define LOG_ERROR(x)   ((void)0)
#endif

/* Watcom uses inp/outp, others use inportb/outportb */
#ifdef __WATCOMC__
#include <conio.h>
#define inportb(p)    inp(p)
#define outportb(p,v) outp(p,v)
#endif

/* External assembly helpers for 32-bit port I/O */
extern uint32_t inportd(uint16_t port);
extern void outportd(uint16_t port, uint32_t value);

/*
 * Inline assembly NOP delay for I/O timing
 * Watcom uses #pragma aux, MSVC/GCC use different inline asm syntax
 */
#ifdef __WATCOMC__
extern void io_delay_nops(void);
#pragma aux io_delay_nops = \
    "nop" \
    "nop" \
    "nop" \
    parm [] \
    modify exact [];
#elif defined(_MSC_VER) || defined(__TURBOC__)
/* MSVC or Turbo C style inline assembly */
static void io_delay_nops(void) {
    _asm { nop; nop; nop; }
}
#elif defined(__GNUC__)
/* GCC style inline assembly */
static void io_delay_nops(void) {
    __asm__ __volatile__("nop; nop; nop" ::: );
}
#else
/* Fallback - no delay (compiler will hopefully not optimize this away) */
static void io_delay_nops(void) {
    volatile int i = 0;
    (void)i;
}
#endif

/* Config cache entry */
typedef struct {
    bool valid;
    uint32_t timestamp;     /* Access count for LRU */
    uint8_t data[256];      /* Full config space */
} config_cache_entry_t;

/* Config cache - 16 devices max */
#define MAX_CACHED_DEVICES 16
static struct {
    uint16_t bus_dev_func;  /* Packed BDF */
    config_cache_entry_t cache;
} config_cache[MAX_CACHED_DEVICES];

static uint32_t cache_access_count = 0;

/* Enhanced shim state */
static struct {
    bool installed;
    bool enabled;
    uint8_t mechanism;      /* 1 or 2 */
    uint8_t last_bus;
    bool in_v86_mode;       /* V86 mode detected */
    bool cache_enabled;     /* Config caching enabled */
    void (__interrupt __far *old_int1a)();

    /* Statistics */
    uint32_t total_calls;
    uint32_t fallback_calls;
    uint32_t bios_errors;
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint32_t v86_traps;     /* V86 mode I/O traps */
} enhanced_state = {
    false,      /* installed */
    true,       /* enabled */
    0,          /* mechanism */
    0,          /* last_bus */
    false,      /* in_v86_mode */
    true,       /* cache_enabled */
    NULL,       /* old_int1a */
    0,          /* total_calls */
    0,          /* fallback_calls */
    0,          /* bios_errors */
    0,          /* cache_hits */
    0,          /* cache_misses */
    0           /* v86_traps */
};

/**
 * Get cache entry for a device
 */
static config_cache_entry_t* get_cache_entry(uint8_t bus, uint8_t dev, uint8_t func) {
    uint16_t bdf = (bus << 8) | (dev << 3) | func;
    int oldest_idx = -1;
    uint32_t oldest_time = 0xFFFFFFFF;
    int i;

    /* Look for existing entry */
    for (i = 0; i < MAX_CACHED_DEVICES; i++) {
        if (config_cache[i].cache.valid && config_cache[i].bus_dev_func == bdf) {
            config_cache[i].cache.timestamp = ++cache_access_count;
            enhanced_state.cache_hits++;
            return &config_cache[i].cache;
        }

        /* Track oldest for LRU replacement */
        if (!config_cache[i].cache.valid ||
            config_cache[i].cache.timestamp < oldest_time) {
            oldest_time = config_cache[i].cache.timestamp;
            oldest_idx = i;
        }
    }
    
    /* Not found - use LRU slot */
    enhanced_state.cache_misses++;
    if (oldest_idx >= 0) {
        config_cache[oldest_idx].bus_dev_func = bdf;
        config_cache[oldest_idx].cache.valid = false;  /* Will be populated */
        return &config_cache[oldest_idx].cache;
    }
    
    return NULL;
}

/**
 * Invalidate cache entry on write
 */
static void invalidate_cache(uint8_t bus, uint8_t dev, uint8_t func) {
    uint16_t bdf = (bus << 8) | (dev << 3) | func;
    int i;

    for (i = 0; i < MAX_CACHED_DEVICES; i++) {
        if (config_cache[i].cache.valid && config_cache[i].bus_dev_func == bdf) {
            config_cache[i].cache.valid = false;
            /* Debug: Invalidated cache for bus:dev.func */
            break;
        }
    }
}

/**
 * Populate cache for a device
 */
static void populate_cache(uint8_t bus, uint8_t dev, uint8_t func) {
    config_cache_entry_t* cache = get_cache_entry(bus, dev, func);
    uint32_t address;
    uint32_t dword;
    int i;

    if (!cache || cache->valid) return;

    /* Read entire config space using Mechanism #1 */
    for (i = 0; i < 256; i += 4) {
        address = 0x80000000L |
                 ((uint32_t)bus << 16) |
                 ((uint32_t)dev << 11) |
                 ((uint32_t)func << 8) |
                 (i & 0xFC);

        _disable();
        outportd(0xCF8, address);
        dword = inportd(0xCFC);
        _enable();

        cache->data[i] = (uint8_t)(dword & 0xFF);
        cache->data[i+1] = (uint8_t)((dword >> 8) & 0xFF);
        cache->data[i+2] = (uint8_t)((dword >> 16) & 0xFF);
        cache->data[i+3] = (uint8_t)((dword >> 24) & 0xFF);
    }
    
    cache->valid = true;
    cache->timestamp = ++cache_access_count;

    /* Debug: Populated cache for bus:dev.func */
}

/**
 * V86-aware Mechanism #1 config read
 */
static uint8_t v86_safe_mech1_read_byte(uint8_t bus, uint8_t dev,
                                        uint8_t func, uint8_t offset) {
    uint32_t address;
    uint8_t value;
    config_cache_entry_t* cache;

    /* Check cache first if enabled */
    if (enhanced_state.cache_enabled) {
        cache = get_cache_entry(bus, dev, func);
        if (cache && cache->valid) {
            return cache->data[offset];
        }
    }
    
    /* Build configuration address */
    address = 0x80000000L |
              ((uint32_t)bus << 16) |
              ((uint32_t)dev << 11) |
              ((uint32_t)func << 8) |
              (offset & 0xFC);
    
    /* In V86 mode, be extra careful with I/O */
    if (enhanced_state.in_v86_mode) {
        /* Some memory managers may trap these ports */
        /* Use more conservative timing */
        _disable();
        outportd(0xCF8, address);
        io_delay_nops();  /* Small delay */
        value = inportb(0xCFC + (offset & 3));
        _enable();
        enhanced_state.v86_traps++;
    } else {
        /* Normal mode - fast access */
        _disable();
        outportd(0xCF8, address);
        value = inportb(0xCFC + (offset & 3));
        _enable();
    }
    
    /* Populate cache if this was vendor ID read */
    if (enhanced_state.cache_enabled && offset == 0 && value != 0xFF) {
        populate_cache(bus, dev, func);
    }
    
    return value;
}

/**
 * V86-aware Mechanism #1 config write
 */
static uint8_t v86_safe_mech1_write_byte(uint8_t bus, uint8_t dev,
                                         uint8_t func, uint8_t offset,
                                         uint8_t value) {
    uint32_t address;
    
    /* Invalidate cache on write */
    if (enhanced_state.cache_enabled) {
        invalidate_cache(bus, dev, func);
    }
    
    /* Build configuration address */
    address = 0x80000000L |
              ((uint32_t)bus << 16) |
              ((uint32_t)dev << 11) |
              ((uint32_t)func << 8) |
              (offset & 0xFC);
    
    /* V86-aware I/O */
    if (enhanced_state.in_v86_mode) {
        _disable();
        outportd(0xCF8, address);
        io_delay_nops();
        outportb(0xCFC + (offset & 3), value);
        _enable();
        enhanced_state.v86_traps++;
    } else {
        _disable();
        outportd(0xCF8, address);
        outportb(0xCFC + (offset & 3), value);
        _enable();
    }
    
    return PCI_SUCCESSFUL;
}

/**
 * Initialize enhanced PCI shim
 * Note: Return type is int for C89/Watcom compatibility where bool is defined as int
 */
int pci_shim_enhanced_install(void) {
    union REGS regs;
    struct SREGS sregs;
    
    if (enhanced_state.installed) {
        /* Warning: Enhanced PCI shim already installed */
        return true;
    }
    
    /* Check if running in V86 mode using cpu_detect module */
    enhanced_state.in_v86_mode = asm_is_v86_mode();
    if (enhanced_state.in_v86_mode) {
        /* Info: V86 mode detected - using conservative I/O timing */
        /* Info: Cache enabled to minimize I/O port access */
        enhanced_state.cache_enabled = true;  /* Force cache in V86 */
    }
    
    /* Check if PCI BIOS is present */
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    
    regs.x.ax = (PCI_FUNCTION_ID << 8) | PCI_BIOS_PRESENT;
    int86x(0x1A, &regs, &regs, &sregs);
    
    if (regs.x.cflag != 0) {
        /* Error: PCI BIOS not present */
        return false;
    }
    
    /* Check mechanism support */
    if (regs.h.al & 0x01) {
        enhanced_state.mechanism = 1;
        /* Info: Using PCI Mechanism #1 (preferred) */
    } else if (regs.h.al & 0x02) {
        enhanced_state.mechanism = 2;
        /* Warning: Using PCI Mechanism #2 (limited) */
    } else {
        /* Error: No PCI mechanism available */
        return false;
    }
    
    enhanced_state.last_bus = regs.h.cl;
    
    /* Initialize cache */
    memset(config_cache, 0, sizeof(config_cache));
    cache_access_count = 0;
    
    /* Note: Full INT 1Ah hooking would be done here */
    /* For now, we just provide the enhanced functions */
    
    enhanced_state.installed = true;
    /* Info: Enhanced PCI shim installed (V86=X, Cache=Y) */

    return true;
}

/**
 * Uninstall enhanced PCI shim
 * Note: Return type is int for C89/Watcom compatibility where bool is defined as int
 */
int pci_shim_enhanced_uninstall(void) {
    if (!enhanced_state.installed) {
        return false;
    }
    
    /* Log final statistics */
    {
        uint32_t total_cache_ops;
        uint32_t hit_percent;

        total_cache_ops = enhanced_state.cache_hits + enhanced_state.cache_misses + 1;
        hit_percent = (enhanced_state.cache_hits * 100) / total_cache_ops;

        /* Info: Enhanced shim stats */
        /* Info: Total calls, Fallback calls, Cache hits (%), V86 I/O traps */
        (void)total_cache_ops;
        (void)hit_percent;
    }
    
    /* Clear cache */
    memset(config_cache, 0, sizeof(config_cache));
    
    enhanced_state.installed = false;
    return true;
}

/**
 * Get extended statistics
 */
void pci_shim_get_extended_stats(pci_shim_stats_t* stats) {
    if (stats) {
        stats->total_calls = enhanced_state.total_calls;
        stats->fallback_calls = enhanced_state.fallback_calls;
        stats->bios_errors = enhanced_state.bios_errors;
        stats->cache_hits = enhanced_state.cache_hits;
        stats->cache_misses = enhanced_state.cache_misses;
        stats->in_v86_mode = enhanced_state.in_v86_mode;
        stats->cache_enabled = enhanced_state.cache_enabled;
        stats->mechanism = enhanced_state.mechanism;
    }
}

/**
 * Control config caching
 * Note: Parameter type is int for C89/Watcom compatibility where bool is defined as int
 */
void pci_shim_set_cache_enabled(int enabled) {
    /* Don't allow disabling cache in V86 mode */
    if (enhanced_state.in_v86_mode && !enabled) {
        /* Warning: Cannot disable cache in V86 mode */
        return;
    }
    
    enhanced_state.cache_enabled = enabled;
    
    /* Clear cache when disabling */
    if (!enabled) {
        memset(config_cache, 0, sizeof(config_cache));
        cache_access_count = 0;
        /* Info: Config cache disabled and cleared */
    }
    /* else: Info: Config cache enabled */
}

/**
 * Clear config cache
 */
void pci_shim_clear_cache(void) {
    memset(config_cache, 0, sizeof(config_cache));
    cache_access_count = 0;
    enhanced_state.cache_hits = 0;
    enhanced_state.cache_misses = 0;
    /* Info: Config cache cleared */
}

/**
 * Enhanced config space access functions
 */
uint8_t pci_enhanced_read_config_byte(uint8_t bus, uint8_t dev, 
                                      uint8_t func, uint8_t offset) {
    enhanced_state.total_calls++;
    
    if (enhanced_state.mechanism == 1) {
        return v86_safe_mech1_read_byte(bus, dev, func, offset);
    } else {
        /* Fall back to basic mechanism */
        return pci_read_config_byte(bus, dev, func, offset);
    }
}

uint8_t pci_enhanced_write_config_byte(uint8_t bus, uint8_t dev,
                                       uint8_t func, uint8_t offset,
                                       uint8_t value) {
    enhanced_state.total_calls++;

    if (enhanced_state.mechanism == 1) {
        return v86_safe_mech1_write_byte(bus, dev, func, offset, value);
    } else {
        /* Fall back to basic mechanism - convert bool to status code */
        return pci_write_config_byte(bus, dev, func, offset, value) ? PCI_SUCCESSFUL : 0x87;
    }
}

uint16_t pci_enhanced_read_config_word(uint8_t bus, uint8_t dev,
                                       uint8_t func, uint8_t offset) {
    uint16_t value;
    config_cache_entry_t* cache;

    enhanced_state.total_calls++;

    /* Check alignment */
    if (offset & 1) {
        /* Error: Unaligned word read at offset */
        return 0xFFFF;
    }

    /* Use cache if available */
    if (enhanced_state.cache_enabled) {
        cache = get_cache_entry(bus, dev, func);
        if (cache && cache->valid) {
            value = cache->data[offset] | (cache->data[offset+1] << 8);
            return value;
        }
    }
    
    /* Read as two bytes */
    value = pci_enhanced_read_config_byte(bus, dev, func, offset);
    value |= (uint16_t)pci_enhanced_read_config_byte(bus, dev, func, offset+1) << 8;
    
    return value;
}

uint16_t pci_enhanced_write_config_word(uint8_t bus, uint8_t dev,
                                        uint8_t func, uint8_t offset,
                                        uint16_t value) {
    enhanced_state.total_calls++;
    
    /* Check alignment */
    if (offset & 1) {
        /* Error: Unaligned word write at offset */
        return PCI_BAD_REGISTER_NUMBER;
    }
    
    /* Invalidate cache */
    if (enhanced_state.cache_enabled) {
        invalidate_cache(bus, dev, func);
    }
    
    /* Write as two bytes */
    pci_enhanced_write_config_byte(bus, dev, func, offset, value & 0xFF);
    pci_enhanced_write_config_byte(bus, dev, func, offset+1, (value >> 8) & 0xFF);
    
    return PCI_SUCCESSFUL;
}

uint32_t pci_enhanced_read_config_dword(uint8_t bus, uint8_t dev,
                                        uint8_t func, uint8_t offset) {
    uint32_t value;
    config_cache_entry_t* cache;

    enhanced_state.total_calls++;

    /* Check alignment */
    if (offset & 3) {
        /* Error: Unaligned dword read at offset */
        return 0xFFFFFFFF;
    }

    /* Use cache if available */
    if (enhanced_state.cache_enabled) {
        cache = get_cache_entry(bus, dev, func);
        if (cache && cache->valid) {
            value = cache->data[offset] |
                   ((uint32_t)cache->data[offset+1] << 8) |
                   ((uint32_t)cache->data[offset+2] << 16) |
                   ((uint32_t)cache->data[offset+3] << 24);
            return value;
        }
    }
    
    /* Read as two words */
    value = pci_enhanced_read_config_word(bus, dev, func, offset);
    value |= (uint32_t)pci_enhanced_read_config_word(bus, dev, func, offset+2) << 16;
    
    return value;
}

uint32_t pci_enhanced_write_config_dword(uint8_t bus, uint8_t dev,
                                         uint8_t func, uint8_t offset,
                                         uint32_t value) {
    enhanced_state.total_calls++;
    
    /* Check alignment */
    if (offset & 3) {
        /* Error: Unaligned dword write at offset */
        return PCI_BAD_REGISTER_NUMBER;
    }
    
    /* Invalidate cache */
    if (enhanced_state.cache_enabled) {
        invalidate_cache(bus, dev, func);
    }
    
    /* Write as two words */
    pci_enhanced_write_config_word(bus, dev, func, offset, value & 0xFFFF);
    pci_enhanced_write_config_word(bus, dev, func, offset+2, (value >> 16) & 0xFFFF);

    return PCI_SUCCESSFUL;
}
