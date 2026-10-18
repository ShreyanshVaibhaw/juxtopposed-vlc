// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#define MODULE_NAME test_instance_policy
#undef VLC_DYNAMIC_PLUGIN
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include "../../../src/instance_policy.h"
#include <vlc_plugin.h>

const char vlc_module_name[] = MODULE_STRING;
vlc_module_begin()
    set_description("Instance policy test options")
#if !defined(_WIN32) && !defined(HAVE_DBUS) && !defined(__OS2__)
    add_bool("one-instance", false, "One window", NULL)
    add_bool("one-instance-when-started-from-file", true, "File manager", NULL)
    add_bool("started-from-file", false, "Started from file", NULL)
#endif
vlc_module_end()
VLC_EXPORT const vlc_plugin_cb vlc_static_modules[] = {VLC_SYMBOL(vlc_entry), NULL};

static vlc_object_t *owner(libvlc_int_t *parent, bool grouped, bool from_file)
{
    vlc_object_t *obj = vlc_object_create(parent, sizeof(*obj));
    assert(obj);
    var_Create(obj, "single-instance-default", VLC_VAR_BOOL);
    var_SetBool(obj, "single-instance-default", grouped);
    var_Create(obj, "started-from-file", VLC_VAR_BOOL);
    var_SetBool(obj, "started-from-file", from_file);
    return obj;
}

int main(void)
{
    test_init();
    const char *args[] = {"--ignore-config", "--no-plugins-cache", "--no-media-library"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance);
    const bool legacy[] = {0, 0, 0, 1, 1, 1, 1, 1};
    const bool grouped[] = {0, 0, 0, 0, 1, 0, 1, 1};
    unsigned count = 0;
    for (unsigned mode = 0; mode < 2; ++mode)
        for (unsigned bits = 0; bits < 8; ++bits) {
            config_PutInt("one-instance", (bits >> 2) & 1);
            config_PutInt("one-instance-when-started-from-file", (bits >> 1) & 1);
            vlc_object_t *obj = owner(instance->p_libvlc_int, mode, bits & 1);
            assert(vlc_instance_ShouldReuse(obj) == (mode ? grouped[bits] : legacy[bits]));
            vlc_object_delete(obj);
            count++;
        }
    const char *keys[] = {"one-instance", "one-instance-when-started-from-file",
                          "playlist-enqueue"};
    for (unsigned key = 0; key < ARRAY_SIZE(keys); ++key)
        for (unsigned value = 0; value < 2; ++value)
            for (unsigned bits = 0; bits < 8; ++bits) {
                config_PutInt("one-instance", (bits >> 2) & 1);
                config_PutInt("one-instance-when-started-from-file", (bits >> 1) & 1);
                config_PutInt("single-instance-preferences-version", 0);
                vlc_object_t *obj = owner(instance->p_libvlc_int, true, bits & 1);
                var_Create(obj, keys[key], VLC_VAR_BOOL);
                var_SetBool(obj, keys[key], value);
                unsigned effective = key == 0 ? (bits & 3) | (value << 2)
                                              : key == 1 ? (bits & 5) | (value << 1)
                                                         : bits;
                assert(vlc_instance_ShouldReuse(obj) == legacy[effective]);
                vlc_instance_MigratePreferences(obj);
                assert(config_GetInt("single-instance-preferences-version") == 0);
                assert(config_GetInt("one-instance-when-started-from-file") == ((bits >> 1) & 1));
                vlc_object_delete(obj);
                count++;
            }
    vlc_object_t *obj = owner(instance->p_libvlc_int, true, true);
    config_PutInt("one-instance", true);
    config_PutInt("one-instance-when-started-from-file", false);
    config_PutInt("single-instance-preferences-version", 0);
    vlc_instance_MigratePreferences(obj);
    assert(config_GetInt("one-instance-when-started-from-file") == true);
    assert(config_GetInt("single-instance-preferences-version") == 1);
    config_PutInt("one-instance-when-started-from-file", false);
    vlc_instance_MigratePreferences(obj);
    assert(config_GetInt("one-instance-when-started-from-file") == false);
    assert(!vlc_instance_ShouldReuse(obj));
    vlc_object_delete(obj);
    obj = owner(instance->p_libvlc_int, false, true);
    config_PutInt("single-instance-preferences-version", 0);
    vlc_instance_MigratePreferences(obj);
    assert(config_GetInt("single-instance-preferences-version") == 0);
    vlc_object_delete(obj);
    config_PutInt("one-instance", false);
    config_PutInt("one-instance-when-started-from-file", true);
    config_PutInt("single-instance-preferences-version", 0);
    fprintf(stderr, "%u startup decisions and migration/override checks passed\n", count);
    libvlc_release(instance);
    return 0;
}
