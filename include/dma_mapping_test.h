/**
 * @file dma_mapping_test.h
 * @brief Header for comprehensive DMA mapping test suite
 *
 * This header defines the interface for testing the centralized DMA mapping layer.
 */

#ifndef DMA_MAPPING_TEST_H
#define DMA_MAPPING_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run comprehensive DMA mapping test suite
 * @return 0 on success (all tests pass), -1 on failure
 */
int run_dma_mapping_tests(void);

/**
 * @brief Run DMA mapping self-test (for integration with driver)
 * @return 0 on success, -1 on failure
 */
int dma_mapping_run_self_test(void);

#ifdef __cplusplus
}
#endif

#endif /* DMA_MAPPING_TEST_H */