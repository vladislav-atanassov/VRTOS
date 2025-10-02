#ifndef UTILS_H
#define UTILS_H

#include "rtos_assert.h"

/** ============================================================
 * Generic ALIGN macros
 * ============================================================ */

/* Return-value version: returns the aligned value (for assignment) */
#define ALIGN_UP(value, alignment)                                             \
    ({                                                                         \
        RTOS_STATIC_ASSERT(((alignment) & ((alignment) - 1)) == 0,             \
                           "alignment must be a power of 2");                  \
        typeof(value) _v = (value);                                            \
        _v = (_v + (alignment - 1)) & ~(alignment - 1);                        \
        _v;                                                                    \
    })

#define ALIGN_DOWN(value, alignment)                                           \
    ({                                                                         \
        RTOS_STATIC_ASSERT(((alignment) & ((alignment) - 1)) == 0,             \
                           "alignment must be a power of 2");                  \
        typeof(value) _v = (value);                                            \
        _v = _v & ~(alignment - 1);                                            \
        _v;                                                                    \
    })

/** ============================================================
 * In-place version: modifies variable directly
 * ============================================================ */
#define ALIGN_UP_INPLACE(var, alignment)                                       \
    do {                                                                       \
        RTOS_STATIC_ASSERT(((alignment) & ((alignment) - 1)) == 0,             \
                           "alignment must be a power of 2");                  \
        (var) = ((var) + ((alignment) - 1)) & ~((alignment) - 1);              \
    } while (0)

#define ALIGN_DOWN_INPLACE(var, alignment)                                     \
    do {                                                                       \
        RTOS_STATIC_ASSERT(((alignment) & ((alignment) - 1)) == 0,             \
                           "alignment must be a power of 2");                  \
        (var) = (var) & ~((alignment) - 1);                                    \
    } while (0)

/** ============================================================
 * Convenience macros for common alignments
 * ============================================================ */

/* In-place versions */
#define ALIGN4_UP(var) ALIGN_UP_INPLACE(var, 4)
#define ALIGN8_UP(var) ALIGN_UP_INPLACE(var, 8)
#define ALIGN16_UP(var) ALIGN_UP_INPLACE(var, 16)
#define ALIGN32_UP(var) ALIGN_UP_INPLACE(var, 32)

#define ALIGN4_DOWN(var) ALIGN_DOWN_INPLACE(var, 4)
#define ALIGN8_DOWN(var) ALIGN_DOWN_INPLACE(var, 8)
#define ALIGN16_DOWN(var) ALIGN_DOWN_INPLACE(var, 16)
#define ALIGN32_DOWN(var) ALIGN_DOWN_INPLACE(var, 32)

/* Return-value convenience versions */
#define ALIGN4_UP_VALUE(val) ALIGN_UP(val, 4)
#define ALIGN8_UP_VALUE(val) ALIGN_UP(val, 8)
#define ALIGN16_UP_VALUE(val) ALIGN_UP(val, 16)
#define ALIGN32_UP_VALUE(val) ALIGN_UP(val, 32)

#define ALIGN4_DOWN_VALUE(val) ALIGN_DOWN(val, 4)
#define ALIGN8_DOWN_VALUE(val) ALIGN_DOWN(val, 8)
#define ALIGN16_DOWN_VALUE(val) ALIGN_DOWN(val, 16)
#define ALIGN32_DOWN_VALUE(val) ALIGN_DOWN(val, 32)

#endif /* UTILS_H */
