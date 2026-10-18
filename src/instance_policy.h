/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef VLC_INSTANCE_POLICY_H
#define VLC_INSTANCE_POLICY_H

#include <vlc_common.h>
#include <vlc_configuration.h>

static inline bool vlc_instance_HasNativeOverride(vlc_object_t *instance)
{
    return var_Type(instance, "one-instance") != 0
        || var_Type(instance, "one-instance-when-started-from-file") != 0
        || var_Type(instance, "playlist-enqueue") != 0;
}

static inline bool vlc_instance_UsesGroupedPreferences(vlc_object_t *instance)
{
    return var_InheritBool(instance, "single-instance-default")
        && !vlc_instance_HasNativeOverride(instance);
}

static inline bool vlc_instance_ShouldReuse(vlc_object_t *instance)
{
    const bool enabled = var_InheritBool(instance, "one-instance");
    const bool files = var_InheritBool(instance, "one-instance-when-started-from-file");
    const bool from_file = var_InheritBool(instance, "started-from-file");
    if (vlc_instance_UsesGroupedPreferences(instance))
        return enabled && (!from_file || files);
    return enabled || (files && from_file);
}

static inline void vlc_instance_MigratePreferences(vlc_object_t *instance)
{
    if (!vlc_instance_UsesGroupedPreferences(instance)
        || config_GetType("one-instance") == 0
        || config_GetInt("single-instance-preferences-version") != 0)
        return;
    /* The old global switch included file-manager launches even when its
     * separate switch was off. Preserve that behavior on the first upgrade. */
    if (config_GetInt("one-instance"))
        config_PutInt("one-instance-when-started-from-file", true);
    config_PutInt("single-instance-preferences-version", 1);
}

#endif
