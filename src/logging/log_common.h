#ifndef LOG_COMMON_H
#define LOG_COMMON_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Compile-time basename of a string literal path.
 *
 * Use with @c __FILE__ to log just the file name instead of the full path
 * the compiler embeds. @c __builtin_strrchr is constant-folded by GCC when
 * called on a string literal, so this resolves to a pointer into the
 * original literal at compile time — no runtime scan, no extra storage.
 *
 * Handles forward slashes only; for runtime paths or paths that may use
 * backslashes (Windows-style), use @ref log_basename instead.
 */
#define LOG_BASENAME(literal) \
    (__builtin_strrchr((literal), '/') ? __builtin_strrchr((literal), '/') + 1 : (literal))

/**
 * @brief Runtime basename of an arbitrary path pointer.
 *
 * Walks the string once and returns a pointer to the character just past
 * the last @c '/' or @c '\\'. Use this when the path is only known at
 * runtime (e.g. a pointer field in a stored log packet); for string
 * literals, prefer the constant-folded @ref LOG_BASENAME macro.
 */
static inline const char *log_basename(const char *path)
{
    if (path == 0)
    {
        return path;
    }

    const char *last_sep = path;
    for (const char *c = path; *c != '\0'; c++)
    {
        if (*c == '/' || *c == '\\')
        {
            last_sep = c + 1;
        }
    }
    return last_sep;
}

#ifdef __cplusplus
}
#endif

#endif /* LOG_COMMON_H */
