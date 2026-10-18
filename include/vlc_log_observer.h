// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_LOG_OBSERVER_H
#define VLC_LOG_OBSERVER_H
#include <vlc_common.h>

#ifdef __cplusplus
extern "C" {
#endif

struct vlc_log_subscription;

/**
 * Observe messages without replacing the active logger.
 * The callback runs on the thread that logs the message. Copy any message
 * data that must outlive the callback. Keep the callback short. Do not log,
 * access the network or change subscriptions from a message callback.
 *
 * The instance must remain alive until unsubscribe returns. On success,
 * ops->destroy is called at unsubscribe or instance shutdown. On failure,
 * the caller keeps ownership of data. Handles are invalid after instance
 * shutdown.
 */
VLC_API VLC_USED struct vlc_log_subscription *
vlc_LogSubscribe(libvlc_int_t *, const struct vlc_logger_operations *, void *data);

/** Stop observation and wait for all active callbacks. NULL is allowed. */
VLC_API void vlc_LogUnsubscribe(struct vlc_log_subscription *);

#ifdef __cplusplus
}
#endif
#endif
