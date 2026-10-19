// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_configuration.h>
#include <vlc_fs.h>
#include <vlc_spawn.h>
#include <vlc_variables.h>
#include <locale.h>
#ifdef _WIN32
# include <windows.h>
#endif

static void selected(int argc, const char *const *argv, const char *expected)
{
    char *language = config_GetInterfaceLanguage(argc, argv);
    assert(language != NULL && !strcmp(language, expected));
    free(language);
    /* Reading preferences must not change the embedding host's locale. */
    assert(!strcmp(getenv("LANGUAGE"), "en"));
    assert(!strcmp(setlocale(LC_NUMERIC, NULL), "C"));
}

static void check_getter(const char *file)
{
    const char *profile[] = {"--config", file};
    selected(ARRAY_SIZE(profile), profile, "fr");
    const char *override[] = {"--config", file, "--language=ar", "--language", "en"};
    selected(ARRAY_SIZE(override), override, "en");
    const char *ignore[] = {"--config", file, "--ignore-config"};
    selected(ARRAY_SIZE(ignore), ignore, "auto");
    const char *reset[] = {"--config", file, "--reset-config"};
    selected(ARRAY_SIZE(reset), reset, "auto");
    const char *explicit[] = {"--config", file, "--ignore-config", "--language=ar"};
    selected(ARRAY_SIZE(explicit), explicit, "ar");
    const char *terminator[] = {"--config", file, "--", "--language=ar"};
    selected(ARRAY_SIZE(terminator), terminator, "fr");
    const char *system[] = {"--config", file, "--language=auto"};
    selected(ARRAY_SIZE(system), system, "auto");
    const char *invalid[] = {"--language=../ar"};
    assert(config_ApplyInterfaceLanguage(ARRAY_SIZE(invalid), invalid) == VLC_EINVAL);
    assert(!strcmp(getenv("LANGUAGE"), "en"));
    const char *bcp47[] = {"--language=pt-BR"};
    assert(config_ApplyInterfaceLanguage(ARRAY_SIZE(bcp47), bcp47) == VLC_SUCCESS);
    assert(!strcmp(getenv("LANGUAGE"), "pt_BR"));
    char *original_language = config_GetSystemInterfaceLanguage();
    assert(original_language != NULL && !strcmp(original_language, "en"));
    free(original_language);
}

static void scenario(const char *name)
{
    /* Each scenario runs in a separate process. Never modify the process
     * locale while a LibVLC instance or its threads are active. */
    setenv("LANG", "C.UTF-8", 1);
    setenv("LANGUAGE", "en", 1);
    setlocale(LC_ALL, "C");
    char file[] = "interface-language-XXXXXX";
    int fd = vlc_mkstemp(file);
    assert(fd >= 0);
    FILE *stream = fdopen(fd, "w");
    assert(stream != NULL);
    fputs("\xef\xbb\xbf[core]\r\n#language=es\r\nlanguage=de\r\nlanguage=fr\r\n", stream);
    if (!strcmp(name, "saved-extra") || !strcmp(name, "override"))
        fputs("startup-options=--language=ar\n", stream);
    if (!strcmp(name, "recovery"))
        fputs("startup-options=--language=ar --removed-plugin-setting\n", stream);
    if (!strcmp(name, "system")) fputs("language=auto\n", stream);
    fclose(stream);
    if (!strncmp(name, "snapshot-", 9)) {
        const char *expected;
#ifdef _WIN32
        char native_language[LOCALE_NAME_MAX_LENGTH];
#endif
        if (!strcmp(name, "snapshot-list")) {
            setenv("LANGUAGE", "fr_CA:de", 1);
            expected = "fr_CA:de";
        } else if (!strcmp(name, "snapshot-c")) {
            setenv("LC_ALL", "C", 1);
            setenv("LANGUAGE", "fr", 1);
            expected = "C";
        } else {
            unsetenv("LANGUAGE"); unsetenv("LC_ALL");
            unsetenv("LC_MESSAGES"); unsetenv("LANG");
            expected = NULL;
#ifdef _WIN32
            wchar_t native[LOCALE_NAME_MAX_LENGTH];
            assert(LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT),
                                   native, ARRAY_SIZE(native), 0));
            assert(WideCharToMultiByte(CP_UTF8, 0, native, -1, native_language,
                                       sizeof(native_language), NULL, NULL));
#endif
        }
        const char *override[] = {"--language=ar"};
        assert(config_ApplyInterfaceLanguage(ARRAY_SIZE(override), override) == VLC_SUCCESS);
        char *original_language = config_GetSystemInterfaceLanguage();
#ifdef _WIN32
        if (!expected) {
            /* The native UI language must survive an explicit startup override
             * even when the process originally had no locale environment. */
            assert(original_language && strstr(original_language, native_language));
        } else
#endif
        assert(expected ? original_language && !strcmp(original_language, expected) : !original_language);
        assert(!strcmp(getenv("LANGUAGE"), "ar"));
        const char *second[] = {"--language=fr"};
        assert(config_ApplyInterfaceLanguage(ARRAY_SIZE(second), second) == VLC_SUCCESS);
        char *again = config_GetSystemInterfaceLanguage();
        assert(original_language ? again && !strcmp(original_language, again) : !again);
        free(again);
        free(original_language);
    }
    else if (!strcmp(name, "getter")) check_getter(file);
    else {
        const char *args[] = {"--config", file, "--no-ignore-config", "--no-media-library",
            "--no-metadata-network-access", "--no-plugins-cache", "--load-startup-options",
            !strcmp(name, "override") ? "--language=en" : !strcmp(name, "ignore") ? "--ignore-config" : !strcmp(name, "reset") ? "--reset-config" : "--no-video"};
        bool embedded = !strcmp(name, "embedded");
        libvlc_instance_t *instance = embedded ? libvlc_new(ARRAY_SIZE(args), args)
            : libvlc_new_standalone(ARRAY_SIZE(args), args);
        assert(instance != NULL);
        const char *expected = embedded || !strcmp(name, "override") || !strcmp(name, "system") || !strcmp(name, "ignore") || !strcmp(name, "reset")
            ? "en" : !strcmp(name, "saved-extra") ? "ar" : "fr";
        assert(!strcmp(getenv("LANGUAGE"), expected));
        char *original_language = config_GetSystemInterfaceLanguage();
        assert(original_language != NULL && !strcmp(original_language, "en"));
        free(original_language);
        assert(!strcmp(setlocale(LC_NUMERIC, NULL), "C"));
#ifndef _WIN32
        if (embedded) assert(!strcmp(setlocale(LC_MESSAGES, NULL), "C"));
#endif
#ifdef ENABLE_NLS
        /* Integration runs supply real compiled po/fr.po and po/ar.po files. */
        if (getenv("VLC_TEST_LANGUAGE_CATALOGS"))
            assert(!strcmp(vlc_gettext("C"), !strcmp(expected, "en") ? "C" : expected));
#endif
        if (!strcmp(name, "reset")) {
            char *saved = config_GetPsz("language");
            assert(saved != NULL && !strcmp(saved, "auto"));
            free(saved);
        }
        libvlc_release(instance);
    }
    assert(vlc_unlink(file) == 0);
}

int main(int argc, char **argv)
{
    test_init();
    if (argc == 2) { scenario(argv[1]); return 0; }
    const char *cases[] = {"getter", "profile", "system", "ignore", "reset", "embedded", "saved-extra", "override", "recovery",
        "snapshot-list", "snapshot-c", "snapshot-platform"};
    for (size_t i = 0; i < ARRAY_SIZE(cases); ++i) {
        const char *args[] = {argv[0], cases[i], NULL};
        const int fds[] = {-1, 1, 2, -1};
        pid_t pid;
        assert(vlc_spawnp(&pid, argv[0], fds, args) == 0);
        assert(vlc_waitpid(pid) == 0);
    }
    return 0;
}
