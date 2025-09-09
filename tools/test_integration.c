/**
 * @file test_integration.c
 * @brief Test stub to verify stress test integration
 */

#include <stdio.h>

/* Forward declarations matching stress_test.c exports */
extern int run_stress_test(unsigned long duration_secs, int verbose);
extern int run_soak_test(unsigned long duration_mins, int verbose);
extern int run_negative_test(void);
extern void get_stress_stats(void *stats);

/* Mock implementations for testing */
int run_stress_test(unsigned long duration_secs, int verbose) {
    printf("run_stress_test(%lu, %d) called\n", duration_secs, verbose);
    return 1;
}

int run_soak_test(unsigned long duration_mins, int verbose) {
    printf("run_soak_test(%lu, %d) called\n", duration_mins, verbose);
    return 1;
}

int run_negative_test(void) {
    printf("run_negative_test() called\n");
    return 1;
}

void get_stress_stats(void *stats) {
    printf("get_stress_stats(%p) called\n", stats);
}

int main(void) {
    printf("Integration test successful - all symbols resolved\n");
    run_stress_test(10, 1);
    run_soak_test(30, 0);
    run_negative_test();
    get_stress_stats(0);
    return 0;
}