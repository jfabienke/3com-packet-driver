/**
 * @file memory_test.h
 * @brief Test and validation functions for memory management system
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 */

#ifndef _MEMORY_TEST_H_
#define _MEMORY_TEST_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Test function prototypes */
int memory_run_comprehensive_tests(void);
int memory_stress_test(void);

/* Integration example functions */
int memory_system_complete_init(void);
int memory_example_packet_allocation(void);
int memory_example_direct_allocation(void);
int memory_complete_demonstration(void);
void memory_system_complete_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* _MEMORY_TEST_H_ */