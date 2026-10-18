// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc_common.h>
#include <vlc_configuration.h>
#include <vlc_charset.h>
#include <ctype.h>
#include <locale.h>
#include "configuration.h"
#ifdef _WIN32
# include <windows.h>
#elif defined(__APPLE__)
# include <CoreFoundation/CoreFoundation.h>
#endif

static char *legacy_language(void)
{
#ifdef _WIN32
    WCHAR value[128];
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\VideoLAN\\VLC", L"Lang",
                     RRF_RT_REG_SZ, NULL, value, &size) == ERROR_SUCCESS)
        return FromWide(value);
#elif defined(__APPLE__)
    CFPropertyListRef value = CFPreferencesCopyAppValue(CFSTR("language"), kCFPreferencesCurrentApplication);
    char *result = NULL;
    if (value != NULL)
    {
        if (CFGetTypeID(value) == CFStringGetTypeID() && CFStringGetLength(value) < 128)
        {
            char buffer[512];
            if (CFStringGetCString(value, buffer, sizeof(buffer), kCFStringEncodingUTF8))
                result = strdup(buffer);
        }
        CFRelease(value);
    }
    return result;
#endif
    return NULL;
}

char *config_GetInterfaceLanguage(int argc, const char *const argv[])
{
    const struct config_startup_settings settings = config_GetStartupSettings(argc, argv);
    /* Explicit launch language overrides ignore/reset; only persisted settings
     * are bypassed. An empty choice preserves the old platform preference. */
    char *language = settings.language != NULL ? strdup(settings.language)
                   : settings.ignore || settings.reset ? strdup("auto")
                   : config_ReadStartupSetting(settings.filename, "language");
    if (language == NULL || *language == '\0')
    {
        free(language);
        language = legacy_language();
    }
    if (language == NULL || *language == '\0')
    {
        free(language);
        return strdup("auto");
    }
    return language;
}

int config_ApplyInterfaceLanguage(int argc, const char *const argv[])
{
    char *language = config_GetInterfaceLanguage(argc, argv);
    if (language == NULL) return VLC_ENOMEM;
    if (!strcmp(language, "auto")) { free(language); return VLC_SUCCESS; }
    size_t length = strlen(language);
    if (length > 64 || length == 0) { free(language); return VLC_EINVAL; }
    const size_t base_length = strcspn(language, ".@");
    if (!isalpha((unsigned char)language[0]) || strstr(language, ".."))
    { free(language); return VLC_EINVAL; }
    for (size_t i = 0; i < length; ++i)
    {
        const unsigned char c = language[i];
        if (c > 127 || !(isalnum(c) || c == '_' || c == '-' || c == '.' || c == '@'))
        { free(language); return VLC_EINVAL; }
        /* gettext uses POSIX locale separators, not BCP 47 separators. */
        if (c == '-' && i < base_length) language[i] = '_';
    }
    const bool had_lang = getenv("LANG") != NULL;
    const bool had_language = getenv("LANGUAGE") != NULL;
    char *old_lang = had_lang ? strdup(getenv("LANG")) : NULL;
    char *old_language = had_language ? strdup(getenv("LANGUAGE")) : NULL;
    if ((had_lang && old_lang == NULL) || (had_language && old_language == NULL))
    { free(language); free(old_lang); free(old_language); return VLC_ENOMEM; }
    int ret = setenv("LANG", language, 1);
    if (ret == 0) ret = setenv("LANGUAGE", language, 1);
    free(language);
    if (ret != 0)
    {
        if (old_lang) setenv("LANG", old_lang, 1); else unsetenv("LANG");
        if (old_language) setenv("LANGUAGE", old_language, 1); else unsetenv("LANGUAGE");
    }
    free(old_lang);free(old_language);
    if (ret != 0) return VLC_ENOMEM;
#ifndef _WIN32
    /* GNU gettext ignores LANGUAGE in the plain C/POSIX message locale. Use
     * an available UTF-8 message locale without changing numeric formatting. */
    const char *messages = setlocale(LC_MESSAGES, NULL);
    if (messages && (!strcmp(messages, "C") || !strcmp(messages, "POSIX")))
        if (setlocale(LC_MESSAGES, "C.UTF-8") == NULL)
            if (setlocale(LC_MESSAGES, "en_US.UTF-8") == NULL)
                setlocale(LC_MESSAGES, "");
#endif
    return VLC_SUCCESS;
}
