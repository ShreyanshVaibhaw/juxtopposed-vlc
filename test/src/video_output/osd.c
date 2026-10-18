// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_vout.h>
#include <vlc_vout_osd.h>
#include <vlc_subpicture.h>

/* Compile the real OSD builders with only the output queue replaced. Inspect
 * their timestamps without starting a display or waiting for wall-clock time. */
static subpicture_t *captured;

void CaptureSubpicture(vout_thread_t *vout, subpicture_t *subpicture)
{
    (void)vout;
    assert(captured == NULL);
    captured = subpicture;
}

static void ExpectDuration(unsigned milliseconds)
{
    assert(captured != NULL);
    assert(captured->i_stop - captured->i_start == VLC_TICK_FROM_MS(milliseconds));
    assert(captured->b_ephemer && captured->b_fade);
    subpicture_Delete(captured);
    captured = NULL;
}

static void Message(vout_thread_t *vout, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vout_OSDMessageVa(vout, VOUT_SPU_CHANNEL_OSD, format, args);
    va_end(args);
}

static void CheckFeedback(vout_thread_t *vout, unsigned duration)
{
    Message(vout, "Speed: %s", "1.25");
    ExpectDuration(duration);
    vout_OSDSlider(vout, VOUT_SPU_CHANNEL_OSD, 75, OSD_HOR_SLIDER);
    ExpectDuration(duration);
    vout_OSDSlider(vout, VOUT_SPU_CHANNEL_OSD, 75, OSD_VERT_SLIDER);
    ExpectDuration(duration);
    vout_OSDIcon(vout, VOUT_SPU_CHANNEL_OSD, OSD_PLAY_ICON);
    ExpectDuration(duration);
}

int main(void)
{
    const char *args[] = {"--ignore-config", "--no-plugins-cache", "--no-media-library", "--intf=dummy"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance != NULL);
    vlc_object_t *parent = VLC_OBJECT(instance->p_libvlc_int);
    vout_thread_t *vout = vlc_object_create(parent, sizeof(*vout));
    assert(vout != NULL);

    CheckFeedback(vout, 1000);
    var_Create(parent, "osd-duration", VLC_VAR_INTEGER);
    var_SetInteger(parent, "osd-duration", 2750);
    CheckFeedback(vout, 2750);

    var_Create(vout, "osd-duration", VLC_VAR_INTEGER);
    const unsigned durations[] = {500, 1500, 5000};
    for (size_t i = 0; i < ARRAY_SIZE(durations); ++i) {
        var_SetInteger(vout, "osd-duration", durations[i]);
        CheckFeedback(vout, durations[i]);
    }

    /* Explicit text lifetimes (such as the filename) remain independent. */
    vout_OSDText(vout, VOUT_SPU_CHANNEL_OSD, SUBPICTURE_ALIGN_BOTTOM,
                 VLC_TICK_FROM_SEC(8), "Filename");
    ExpectDuration(8000);

    var_Create(vout, "osd", VLC_VAR_BOOL);
    var_SetBool(vout, "osd", false);
    Message(vout, "Hidden");
    vout_OSDSlider(vout, VOUT_SPU_CHANNEL_OSD, 75, OSD_HOR_SLIDER);
    vout_OSDIcon(vout, VOUT_SPU_CHANNEL_OSD, OSD_PAUSE_ICON);
    assert(captured == NULL);
    var_SetBool(vout, "osd", true);
    CheckFeedback(vout, 5000);

    vlc_object_delete(vout);
    libvlc_release(instance);
    return 0;
}
