/**
 * @file direct_pio_enhanced.h
 * @brief Enhanced direct PIO operations with CPU-specific optimizations
 *
 * 3Com Packet Driver - CPU-Optimized I/O Operations for Phase 1
 *
 * This header provides declarations for enhanced direct PIO operations that
 * leverage 32-bit DWORD I/O instructions on 386+ systems while maintaining
 * full compatibility with 286 systems through runtime CPU detection.
 *
 * Key Features:
 * - Runtime CPU detection and optimization selection
 * - 32-bit DWORD I/O operations (INSL/OUTSL) for 386+ systems  
 * - Automatic fallback to 16-bit operations on 286 systems
 * - Enhanced packet transmission with adaptive I/O sizing
 * - Diagnostic functions for optimization level reporting
 *
 * Usage:
 * 1. Call direct_pio_init_cpu_detection() during driver initialization
 * 2. Use enhanced functions for optimal performance on detected CPU
 * 3. Functions automatically select best I/O method based on CPU capabilities
 */

#ifndef _DIRECT_PIO_ENHANCED_H_
#define _DIRECT_PIO_ENHANCED_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

/* CPU optimization levels */
#define PIO_OPT_LEVEL_286    0    /* 286: 16-bit operations only */
#define PIO_OPT_LEVEL_386    1    /* 386: 32-bit operations available */
#define PIO_OPT_LEVEL_486    2    /* 486+: enhanced 32-bit optimizations */

/* Threshold for using 32-bit operations (bytes) */
#define PIO_32BIT_THRESHOLD  32   /* Use 32-bit ops for packets >= 32 bytes */

/**
 * @brief Initialize CPU detection for enhanced PIO operations
 * 
 * This function must be called during driver initialization to detect CPU
 * capabilities and configure optimization settings. It determines whether
 * 32-bit DWORD I/O operations are available and sets up runtime flags.
 *
 * @return void
 * @note Must be called before using any enhanced PIO functions
 */
void direct_pio_init_cpu_detection(void);

/**
 * @brief Enhanced direct PIO output using optimal word size
 * 
 * Performs direct PIO output using 32-bit OUTSL on 386+ systems or
 * 16-bit OUTSW on 286 systems. Automatically selects optimal transfer
 * method based on detected CPU capabilities.
 *
 * @param src_buffer Pointer to source buffer
 * @param dst_port Destination I/O port
 * @param dword_count Number of 32-bit dwords to transfer
 * @return void
 * @note On 286 systems, dword_count is converted to word_count automatically
 */
void direct_pio_outsl(const void* src_buffer, uint16_t dst_port, uint16_t dword_count);

/**
 * @brief Enhanced direct PIO input using optimal word size
 * 
 * Performs direct PIO input using 32-bit INSL on 386+ systems or
 * 16-bit INSW on 286 systems. Automatically selects optimal transfer
 * method based on detected CPU capabilities.
 *
 * @param dst_buffer Pointer to destination buffer
 * @param src_port Source I/O port
 * @param dword_count Number of 32-bit dwords to transfer
 * @return void
 * @note On 286 systems, dword_count is converted to word_count automatically
 */
void direct_pio_insl(void* dst_buffer, uint16_t src_port, uint16_t dword_count);

/**
 * @brief Enhanced packet transmission with CPU-optimized I/O
 * 
 * Performs packet transmission using optimal I/O operations based on
 * detected CPU capabilities and packet size. Uses 32-bit DWORD operations
 * for large packets on 386+ systems, with automatic fallback to 16-bit
 * operations as needed.
 *
 * @param stack_buffer Pointer to packet buffer
 * @param length Packet length in bytes
 * @param io_base NIC I/O base address
 * @return 0 on success, negative error code on failure
 * @note Automatically handles odd byte boundaries and packet padding
 */
int send_packet_direct_pio_enhanced(const void* stack_buffer, uint16_t length, uint16_t io_base);

/**
 * @brief Get current I/O optimization level
 * 
 * Returns the current optimization level for diagnostic and reporting
 * purposes. Can be used to verify CPU detection results and optimization
 * status.
 *
 * @return Optimization level (0=286, 1=386, 2=486+)
 */
uint8_t direct_pio_get_optimization_level(void);

/**
 * @brief Get CPU support information
 * 
 * Returns CPU support flags for diagnostic purposes. Indicates whether
 * 32-bit operations are available on the current system.
 *
 * @return 1 if 32-bit operations supported, 0 if not
 */
uint8_t direct_pio_get_cpu_support_info(void);

/**
 * @brief Check if enhanced operations should be used for given packet size
 * 
 * Helper function to determine if enhanced 32-bit operations should be
 * used based on packet size and CPU capabilities. Considers both CPU
 * support and packet size thresholds.
 *
 * @param packet_size Size of packet in bytes
 * @return 1 if enhanced operations recommended, 0 if standard operations preferred
 */
static inline int should_use_enhanced_pio(uint16_t packet_size) {
    return (direct_pio_get_cpu_support_info() && packet_size >= PIO_32BIT_THRESHOLD);
}

/**
 * @brief Get optimal transfer unit size for current CPU
 * 
 * Returns the optimal transfer unit size (in bytes) for the current CPU.
 * This can be used to align buffers and optimize transfer operations.
 *
 * @return Transfer unit size (2 for 286, 4 for 386+)
 */
static inline uint8_t get_optimal_transfer_unit(void) {
    return direct_pio_get_cpu_support_info() ? 4 : 2;
}

/* Legacy compatibility functions - these call the original implementations */
extern void direct_pio_outsw(const void* src_buffer, uint16_t dst_port, uint16_t word_count);
extern int send_packet_direct_pio_asm(const void* stack_buffer, uint16_t length, uint16_t io_base);
extern void direct_pio_header_and_payload(uint16_t io_port, const uint8_t* dest_mac,
                                         const uint8_t* src_mac, uint16_t ethertype,
                                         const void* payload, uint16_t payload_len);

#ifdef __cplusplus
}
#endif

#endif /* _DIRECT_PIO_ENHANCED_H_ */