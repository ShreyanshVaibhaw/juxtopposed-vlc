// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_configuration.h>
#include <vlc_media_library.h>
#include <vlc_modules.h>

int main(void)
{
    test_init();
    const char *base[] = {"--ignore-config", "--no-plugins-cache", "-Idummy", "--no-media-library"};
    libvlc_instance_t *controller = libvlc_new(ARRAY_SIZE(base), base);
    assert(controller != NULL);
    if (!module_exists("medialibrary")) {
        libvlc_release(controller);
        return 77;
    }
    const struct {
        bool application;
        bool saved_startup;
        bool saved_legacy;
        int command_line; /* -1: no explicit media-library option */
        bool expected;
    } cases[] = {
        {true,  true,  false, -1, true},
        {true,  false, false, -1, false},
        {true,  false, true,  -1, false},
        {true,  false, false,  1, true},
        {true,  true,  false,  0, false},
        {false, true,  false, -1, false},
        {false, false, false, -1, false},
        {false, false, false,  1, true},
        {false, true,  true,  -1, true},
        {false, true,  true,   0, false},
    };
    for (size_t i = 0; i < ARRAY_SIZE(cases); ++i) {
        config_PutInt("media-library-startup", cases[i].saved_startup);
        config_PutInt("media-library", cases[i].saved_legacy);
        const char *args[7] = {"--ignore-config", "--no-plugins-cache", "-Idummy", "--no-metadata-network-access"};
        int count = 4;
        if (cases[i].application) args[count++] = "--media-library-default";
        if (cases[i].command_line >= 0)
            args[count++] = cases[i].command_line ? "--media-library" : "--no-media-library";
        libvlc_instance_t *instance = libvlc_new(count, args);
        assert(instance != NULL);
        const bool actual = vlc_ml_instance_get(instance->p_libvlc_int) != NULL;
        fprintf(stderr, "library startup case %zu: application=%d saved=%d legacy=%d command=%d actual=%d expected=%d\n",
            i, cases[i].application, cases[i].saved_startup, cases[i].saved_legacy,
            cases[i].command_line, actual, cases[i].expected);
        assert(actual == cases[i].expected);
        assert(config_GetInt("media-library") == cases[i].saved_legacy);
        libvlc_release(instance);
    }
    config_PutInt("media-library", false);
    config_PutInt("media-library-startup", true);
    libvlc_release(controller);
    return 0;
}
