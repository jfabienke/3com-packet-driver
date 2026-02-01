/**
 * @file ovl_data.h
 * @brief Overlay-local data segment placement macros
 *
 * 3Com Packet Driver - 16-bit DOS Memory Optimization
 * Last Updated: 2026-01-27 17:00:00 UTC
 *
 * This header provides macros to mark data intended for overlay-local segments.
 * Currently these resolve to 'const' - the -zc compiler flag moves string
 * literals to code segments which provides most of the benefit.
 *
 * IMPORTANT: Data marked with OVL_*_CONST is intended to be init-only.
 * Pointers to this data must NOT escape the overlay phase!
 *
 * Future enhancement: Use #pragma data_seg() wrappers or segment-based
 * allocation to place struct data (not just strings) in overlay segments.
 */

#ifndef OVL_DATA_H
#define OVL_DATA_H

/*
 * Current implementation: Simple const marking.
 *
 * The -zc compiler flag handles string literals (moves to code segment).
 * For struct arrays, they remain in CONST/DGROUP but this is acceptable
 * because:
 * 1. The strings within them are moved to code segment by -zc
 * 2. The struct overhead (pointers, flags) is small
 * 3. The critical vtable escape bugs have been fixed
 *
 * TODO: For further optimization, wrap these with:
 *   #pragma data_seg("OVL_*_D")
 *   ... data ...
 *   #pragma data_seg()
 * But this requires source file restructuring.
 */

/* INIT_EARLY overlay data (stages 0-4) */
#define OVL_EARLY_DATA
#define OVL_EARLY_CONST  const

/* INIT_DETECT overlay data (stages 5-9) */
#define OVL_DETECT_DATA
#define OVL_DETECT_CONST const

/* INIT_FINAL overlay data (stages 10-14) */
#define OVL_FINAL_DATA
#define OVL_FINAL_CONST  const

/* Accessor macro - no-op currently */
#define OVL_ACCESS(ptr)  (ptr)

#endif /* OVL_DATA_H */
