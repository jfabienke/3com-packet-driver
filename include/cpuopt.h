/**
 * @file cpu_optimized.h
 * @brief CPU-optimized operations for 3Com Packet Driver
 *
 * This header provides CPU-aware optimizations that leverage processor-specific
 * features for improved performance. The optimizations are selected at runtime
 * based on CPU detection results.
 *
 * Key Features:
 * - CPU-aware memory operations (memset, memcpy)
 * - I/O operation optimizations for different CPU architectures
 * - Cache alignment hints and prefetching
 * - Ring buffer operations optimized for specific CPUs
 * - String operations with CPU-specific implementations
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 */

#ifndef _CPU_OPTIMIZED_H_
#define _CPU_OPTIMIZED_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "common.h"
#include "cpudet.h"

/* Cache line sizes for different CPU architectures */
#define CACHE_LINE_SIZE_8086        16      /* No cache, use 16-byte alignment */
#define CACHE_LINE_SIZE_80286       16      /* No cache, use 16-byte alignment */
#define CACHE_LINE_SIZE_80386       16      /* No cache, use 16-byte alignment */
#define CACHE_LINE_SIZE_80486       16      /* 486 has 16-byte cache lines */
#define CACHE_LINE_SIZE_PENTIUM     32      /* Pentium has 32-byte cache lines */
#define CACHE_LINE_SIZE_PENTIUM_PRO 32      /* Pentium Pro has 32-byte cache lines */

/* Memory operation thresholds for different optimizations */
#define CPU_OPT_SMALL_COPY_THRESHOLD    64  /* Use byte copy for small transfers */
#define CPU_OPT_WORD_COPY_THRESHOLD     256 /* Use word copy for medium transfers */
#define CPU_OPT_DWORD_COPY_THRESHOLD    1024/* Use dword copy for large transfers */

/* Cache alignment macros */
#define CPU_OPT_CACHE_ALIGN(ptr) \
    ((void*)(((uintptr_t)(ptr) + cpu_opt_get_cache_line_size() - 1) & \
             ~(cpu_opt_get_cache_line_size() - 1)))

#define CPU_OPT_IS_CACHE_ALIGNED(ptr) \
    (((uintptr_t)(ptr) & (cpu_opt_get_cache_line_size() - 1)) == 0)

/* Optimization flags */
#define CPU_OPT_FLAG_NONE           0x00    /* No special optimizations */
#define CPU_OPT_FLAG_CACHE_ALIGN    0x01    /* Ensure cache alignment */
#define CPU_OPT_FLAG_PREFETCH       0x02    /* Use prefetching if available */
#define CPU_OPT_FLAG_NON_TEMPORAL   0x04    /* Use non-temporal stores */
#define CPU_OPT_FLAG_UNROLL_LOOPS   0x08    /* Unroll loops for performance */

/* CPU optimization context */
typedef struct {
    cpu_type_t cpu_type;                    /* Detected CPU type */
    uint32_t cache_line_size;               /* Cache line size in bytes */
    uint32_t features;                      /* CPU features available */
    bool has_32bit_regs;                    /* 32-bit registers available */
    bool has_string_ops;                    /* String operations available */
    bool has_cache;                         /* CPU has cache */
    bool prefer_word_ops;                   /* Prefer 16-bit operations */
    bool prefer_dword_ops;                  /* Prefer 32-bit operations */
} cpu_opt_context_t;

/* Global CPU optimization context */
extern cpu_opt_context_t g_cpu_opt_context;

/* === Initialization Functions === */

/**
 * @brief Initialize CPU optimization system
 * @return 0 on success, negative on error
 */
int cpu_opt_init(void);

/**
 * @brief Get CPU optimization context
 * @return Pointer to CPU optimization context
 */
const cpu_opt_context_t* cpu_opt_get_context(void);

/**
 * @brief Get optimal cache line size for current CPU
 * @return Cache line size in bytes
 */
uint32_t cpu_opt_get_cache_line_size(void);

/* === Memory Operations === */

/**
 * @brief CPU-optimized memory set operation
 * @param dest Destination pointer
 * @param value Value to set
 * @param size Size in bytes
 * @param flags Optimization flags
 */
void cpu_opt_memset(void* dest, uint8_t value, size_t size, uint32_t flags);

/**
 * @brief CPU-optimized memory copy operation
 * @param dest Destination pointer
 * @param src Source pointer  
 * @param size Size in bytes
 * @param flags Optimization flags
 */
void cpu_opt_memcpy(void* dest, const void* src, size_t size, uint32_t flags);

/**
 * @brief CPU-optimized memory zero operation
 * @param dest Destination pointer
 * @param size Size in bytes
 */
void cpu_opt_memzero(void* dest, size_t size);

/**
 * @brief CPU-optimized memory move operation (handles overlapping regions)
 * @param dest Destination pointer
 * @param src Source pointer
 * @param size Size in bytes
 */
void cpu_opt_memmove(void* dest, const void* src, size_t size);

/**
 * @brief CPU-optimized memory compare operation
 * @param ptr1 First memory region
 * @param ptr2 Second memory region
 * @param size Size in bytes
 * @return 0 if equal, negative if ptr1 < ptr2, positive if ptr1 > ptr2
 */
int cpu_opt_memcmp(const void* ptr1, const void* ptr2, size_t size);

/* === String Operations === */

/**
 * @brief CPU-optimized string copy
 * @param dest Destination buffer
 * @param src Source string
 * @return Pointer to destination
 */
char* cpu_opt_strcpy(char* dest, const char* src);

/**
 * @brief CPU-optimized string length calculation
 * @param str String to measure
 * @return Length of string
 */
size_t cpu_opt_strlen(const char* str);

/**
 * @brief CPU-optimized string comparison
 * @param str1 First string
 * @param str2 Second string
 * @return 0 if equal, negative if str1 < str2, positive if str1 > str2
 */
int cpu_opt_strcmp(const char* str1, const char* str2);

/* === I/O Operations === */

/**
 * @brief CPU-optimized I/O read operations
 * @param port I/O port address
 * @return Value read from port
 */
uint8_t cpu_opt_inb(uint16_t port);
uint16_t cpu_opt_inw(uint16_t port);
uint32_t cpu_opt_inl(uint16_t port);

/**
 * @brief CPU-optimized I/O write operations
 * @param port I/O port address
 * @param value Value to write
 */
void cpu_opt_outb(uint16_t port, uint8_t value);
void cpu_opt_outw(uint16_t port, uint16_t value);
void cpu_opt_outl(uint16_t port, uint32_t value);

/**
 * @brief CPU-optimized I/O string operations
 * @param port I/O port address
 * @param buffer Data buffer
 * @param count Number of transfers
 */
void cpu_opt_insb(uint16_t port, void* buffer, uint32_t count);
void cpu_opt_insw(uint16_t port, void* buffer, uint32_t count);
void cpu_opt_insl(uint16_t port, void* buffer, uint32_t count);

void cpu_opt_outsb(uint16_t port, const void* buffer, uint32_t count);
void cpu_opt_outsw(uint16_t port, const void* buffer, uint32_t count);
void cpu_opt_outsl(uint16_t port, const void* buffer, uint32_t count);

/* === Ring Buffer Operations === */

/**
 * @brief CPU-optimized ring buffer copy
 * @param ring_buffer Ring buffer base
 * @param buffer_size Total ring buffer size
 * @param read_pos Current read position
 * @param dest Destination buffer
 * @param copy_size Number of bytes to copy
 * @return New read position
 */
uint32_t cpu_opt_ring_copy(const void* ring_buffer, uint32_t buffer_size,
                          uint32_t read_pos, void* dest, uint32_t copy_size);

/**
 * @brief CPU-optimized ring buffer write
 * @param ring_buffer Ring buffer base
 * @param buffer_size Total ring buffer size
 * @param write_pos Current write position
 * @param src Source buffer
 * @param copy_size Number of bytes to copy
 * @return New write position
 */
uint32_t cpu_opt_ring_write(void* ring_buffer, uint32_t buffer_size,
                           uint32_t write_pos, const void* src, uint32_t copy_size);

/**
 * @brief CPU-optimized ring buffer clear
 * @param ring_buffer Ring buffer base
 * @param buffer_size Total ring buffer size
 */
void cpu_opt_ring_clear(void* ring_buffer, uint32_t buffer_size);

/* === Cache Management === */

/**
 * @brief Prefetch data for future access
 * @param addr Address to prefetch
 */
void cpu_opt_prefetch(const void* addr);

/**
 * @brief Flush cache line containing address
 * @param addr Address to flush
 */
void cpu_opt_cache_flush_line(const void* addr);

/**
 * @brief Flush entire CPU cache
 */
void cpu_opt_cache_flush_all(void);

/**
 * @brief Allocate cache-aligned memory
 * @param size Size to allocate
 * @return Pointer to cache-aligned memory or NULL on failure
 */
void* cpu_opt_malloc_aligned(size_t size);

/**
 * @brief Free cache-aligned memory
 * @param ptr Pointer to free
 */
void cpu_opt_free_aligned(void* ptr);

/* === Delay and Timing === */

/**
 * @brief CPU-optimized delay loop
 * @param microseconds Delay in microseconds
 */
void cpu_opt_udelay(uint32_t microseconds);

/**
 * @brief CPU-optimized millisecond delay
 * @param milliseconds Delay in milliseconds
 */
void cpu_opt_mdelay(uint32_t milliseconds);

/**
 * @brief High-precision timer read
 * @return High-precision timestamp
 */
uint64_t cpu_opt_read_timer(void);

/* === Atomic Operations === */

/**
 * @brief CPU-optimized atomic increment
 * @param ptr Pointer to value
 * @return New value after increment
 */
uint32_t cpu_opt_atomic_inc(volatile uint32_t* ptr);

/**
 * @brief CPU-optimized atomic decrement
 * @param ptr Pointer to value
 * @return New value after decrement
 */
uint32_t cpu_opt_atomic_dec(volatile uint32_t* ptr);

/**
 * @brief CPU-optimized atomic compare and swap
 * @param ptr Pointer to value
 * @param old_val Expected old value
 * @param new_val New value to set
 * @return true if swap occurred, false otherwise
 */
bool cpu_opt_atomic_cas(volatile uint32_t* ptr, uint32_t old_val, uint32_t new_val);

/* === Error Handling Optimizations === */

/**
 * @brief CPU-optimized error log formatting
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @param format Format string
 * @param ... Arguments
 * @return Number of characters written
 */
int cpu_opt_error_sprintf(char* buffer, size_t buffer_size, const char* format, ...);

/**
 * @brief CPU-optimized memory validation
 * @param ptr Pointer to validate
 * @param size Size of memory region
 * @return true if valid, false otherwise
 */
bool cpu_opt_validate_memory(const void* ptr, size_t size);

/* === Network Packet Optimizations === */

/**
 * @brief CPU-optimized checksum calculation
 * @param data Data to checksum
 * @param length Length of data
 * @return Calculated checksum
 */
uint16_t cpu_opt_checksum(const void* data, uint32_t length);

/**
 * @brief CPU-optimized packet copy with checksum
 * @param dest Destination buffer
 * @param src Source buffer
 * @param length Length to copy
 * @return Calculated checksum
 */
uint16_t cpu_opt_copy_and_checksum(void* dest, const void* src, uint32_t length);

/* === Diagnostics and Debugging === */

/**
 * @brief Print CPU optimization information
 */
void cpu_opt_print_info(void);

/**
 * @brief Run CPU optimization benchmarks
 * @return 0 on success, negative on error
 */
int cpu_opt_benchmark(void);

/**
 * @brief Test CPU optimizations
 * @return 0 if all tests pass, negative on failure
 */
int cpu_opt_self_test(void);

/* === Low-level CPU-specific implementations === */

/* 8086/80286 optimized functions */
void cpu_opt_memset_8086(void* dest, uint8_t value, size_t size);
void cpu_opt_memcpy_8086(void* dest, const void* src, size_t size);

/* 80386+ optimized functions */
void cpu_opt_memset_386(void* dest, uint8_t value, size_t size);
void cpu_opt_memcpy_386(void* dest, const void* src, size_t size);

/* 80486+ optimized functions */
void cpu_opt_memset_486(void* dest, uint8_t value, size_t size);
void cpu_opt_memcpy_486(void* dest, const void* src, size_t size);

/* Pentium+ optimized functions */
void cpu_opt_memset_pentium(void* dest, uint8_t value, size_t size);
void cpu_opt_memcpy_pentium(void* dest, const void* src, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* _CPU_OPTIMIZED_H_ */
