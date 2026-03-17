#ifndef ULOG_H
#define ULOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef ULOG_BUFFER_SIZE
#define ULOG_BUFFER_SIZE 1024 /* Must be power of 2 */
#endif

#ifndef ULOG_LINE_MAX
#define ULOG_LINE_MAX 128 /* Max formatted line length */
#endif

typedef enum
{
    ULOG_LEVEL_NONE = 0,
    ULOG_LEVEL_ERROR,
    ULOG_LEVEL_WARN,
    ULOG_LEVEL_INFO,
    ULOG_LEVEL_DEBUG,
} ulog_level_t;

void ulog_init(ulog_level_t level);

/* NOT ISR-safe. Output is deferred to the flush task via ring buffer. */
void ulog(ulog_level_t level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

uint32_t ulog_drain(uint8_t *buf, uint32_t max_len);
uint32_t ulog_pending(void);



#define ulog_error(msg, ...) ulog(ULOG_LEVEL_ERROR, "[E] " msg, ##__VA_ARGS__)
#define ulog_warn(msg, ...)  ulog(ULOG_LEVEL_WARN, "[W] " msg, ##__VA_ARGS__)
#define ulog_info(msg, ...)  ulog(ULOG_LEVEL_INFO, "[I] " msg, ##__VA_ARGS__)
#define ulog_debug(msg, ...) ulog(ULOG_LEVEL_DEBUG, "[D] " msg, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* ULOG_H */
