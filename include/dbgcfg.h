/**
 * @file debug_config.h
 * @brief Debug Configuration and Hot Path Optimization (GPT-5 A)
 *
 * Controls debug output compilation to prevent performance impact
 * in release builds, especially in TX/RX hot paths.
 */

#ifndef DEBUG_CONFIG_H
#define DEBUG_CONFIG_H

/* GPT-5 A: Define DEBUG_BUILD for development builds */
/* In production, this should be undefined to optimize hot paths */
/* #define DEBUG_BUILD */

/* Hot path logging optimization */
#ifdef DEBUG_BUILD
    #define DEBUG_LOG_ENABLED 1
    #define HOT_PATH_LOG_ENABLED 1
#else
    #define DEBUG_LOG_ENABLED 0
    #define HOT_PATH_LOG_ENABLED 0  /* Disable hot path logging in release */
#endif

/**
 * @brief Hot path debug macro that compiles out in release builds
 */
#if HOT_PATH_LOG_ENABLED
    #define LOG_HOT_PATH(level, fmt, ...) log_at_level(level, fmt, ##__VA_ARGS__)
#else
    #define LOG_HOT_PATH(level, fmt, ...) do { } while(0)
#endif

/**
 * @brief Conditional debug logging for performance-critical sections
 */
#if DEBUG_LOG_ENABLED
    #define LOG_DEBUG_COND(fmt, ...) log_debug(fmt, ##__VA_ARGS__)
#else
    #define LOG_DEBUG_COND(fmt, ...) do { } while(0)
#endif

#endif /* DEBUG_CONFIG_H */