// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_configuration.h>
#include <vlc_fs.h>
#include <vlc_spawn.h>
#include <vlc_variables.h>
#include <locale.h>

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
    if (!strcmp(name, "getter")) check_getter(file);
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
    const char *cases[] = {"getter", "profile", "system", "ignore", "reset", "embedded", "saved-extra", "override", "recovery"};
    for (size_t i = 0; i < ARRAY_SIZE(cases); ++i) {
        const char *args[] = {argv[0], cases[i], NULL};
        const int fds[] = {-1, 1, 2, -1};
        pid_t pid;
        assert(vlc_spawnp(&pid, argv[0], fds, args) == 0);
        assert(vlc_waitpid(pid) == 0);
    }
    return 0;
}
