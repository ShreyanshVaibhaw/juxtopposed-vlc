// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_WIN32_PROXY_H
#define VLC_WIN32_PROXY_H

#include <stdlib.h>
#include <string.h>

/* Windows proxy lists use semicolons or whitespace between entries. */
static inline bool proxy_separator(char c)
{
    return c == ';' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static inline bool proxy_pattern_matches(const char *pattern, size_t length,
                                         const char *host)
{
    const char *end = pattern + length, *star = NULL, *retry = NULL;
    while (*host)
    {
        if (pattern < end && (*pattern == '?' ||
                             !strncasecmp(pattern, host, 1)))
        {
            pattern++;
            host++;
        }
        else if (pattern < end && *pattern == '*')
        {
            star = ++pattern;
            retry = host;
        }
        else if (star)
        {
            pattern = star;
            host = ++retry;
        }
        else
            return false;
    }
    while (pattern < end && *pattern == '*')
        pattern++;
    return pattern == end;
}

static inline bool proxy_bypasses(const char *list, const char *host,
                                  unsigned port)
{
    if (list == NULL)
        return false;

    char *authority;
    if (asprintf(&authority, strchr(host, ':') ? "[%s]:%u" : "%s:%u",
                 host, port) < 0)
        return false;

    bool bypass = false;
    while (*list)
    {
        while (proxy_separator(*list))
            list++;
        const char *end = list;
        while (*end && !proxy_separator(*end))
            end++;
        size_t length = end - list;
        if ((length == 7 && !strncasecmp(list, "<local>", length)
             && !strchr(host, '.') && !strchr(host, ':')) ||
            (length && (proxy_pattern_matches(list, length, host) ||
                        proxy_pattern_matches(list, length, authority) ||
                        (length > 2 && list[0] == '[' && end[-1] == ']' &&
                         proxy_pattern_matches(list + 1, length - 2, host)))))
        {
            bypass = true;
            break;
        }
        list = end;
    }
    free(authority);
    return bypass;
}

static inline char *proxy_for_scheme(const char *list, const char *scheme)
{
    const char *fallback = NULL;
    size_t fallback_length = 0;
    if (list == NULL)
        return NULL;

    while (*list)
    {
        while (proxy_separator(*list))
            list++;
        const char *end = list;
        while (*end && !proxy_separator(*end))
            end++;
        const char *equal = memchr(list, '=', end - list);
        if (equal)
        {
            if ((size_t)(equal - list) == strlen(scheme) &&
                !strncasecmp(list, scheme, equal - list))
                return equal + 1 < end ? strndup(equal + 1, end - equal - 1)
                                      : NULL;
        }
        else if (end > list && fallback == NULL)
        {
            fallback = list;
            fallback_length = end - list;
        }
        list = end;
    }
    return fallback ? strndup(fallback, fallback_length) : NULL;
}

#endif
