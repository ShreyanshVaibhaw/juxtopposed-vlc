// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_input_item.h>
#include <vlc_playlist.h>

static input_item_t *append_directory(vlc_playlist_t *playlist, const char *name)
{
    input_item_t *item = input_item_NewDirectory(
        "mock://node_count=2;length=1000000", name, ITEM_LOCAL);
    assert(item);
    vlc_playlist_Lock(playlist);
    assert(vlc_playlist_AppendOne(playlist, item) == VLC_SUCCESS);
    vlc_playlist_Unlock(playlist);
    return item;
}

static void wait_for_expansion(vlc_playlist_t *playlist, input_item_t *root)
{
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
    for (;;) {
        vlc_playlist_Lock(playlist);
        const bool done = vlc_playlist_IndexOfMedia(playlist, root) == -1;
        vlc_playlist_Unlock(playlist);
        if (done) return;
        assert(vlc_tick_now() < deadline);
        vlc_tick_sleep(VLC_TICK_FROM_MS(10));
    }
}

int main(void)
{
    test_init();
    const char *args[] = {"--ignore-config", "--no-plugins-cache",
        "--no-media-library", "--no-metadata-network-access", "--aout=adummy"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance);
    vlc_playlist_t *playlist = vlc_playlist_New(VLC_OBJECT(instance->p_libvlc_int),
        VLC_PLAYLIST_PREPARSING_COLLAPSE, 1, VLC_TICK_FROM_SEC(5));
    assert(playlist);
    input_item_t *first = append_directory(playlist, "First");
    wait_for_expansion(playlist, first);

    vlc_playlist_Lock(playlist);
    assert(vlc_playlist_Count(playlist) == 2);
    const uint64_t first_ids[] = {
        vlc_playlist_item_GetId(vlc_playlist_Get(playlist, 0)),
        vlc_playlist_item_GetId(vlc_playlist_Get(playlist, 1)),
    };
    vlc_playlist_SetPreparseDepth(playlist, VLC_PLAYLIST_PREPARSING_ENABLED);
    assert(vlc_playlist_GetPreparseDepth(playlist) == VLC_PLAYLIST_PREPARSING_ENABLED);
    vlc_playlist_Unlock(playlist);
    input_item_t *deferred = append_directory(playlist, "Deferred");

    vlc_playlist_Lock(playlist);
    vlc_playlist_SetPreparseDepth(playlist, VLC_PLAYLIST_PREPARSING_RECURSIVE);
    assert(vlc_playlist_GetPreparseDepth(playlist) == VLC_PLAYLIST_PREPARSING_RECURSIVE);
    vlc_playlist_Unlock(playlist);
    input_item_t *last = append_directory(playlist, "Last");
    wait_for_expansion(playlist, last);

    /* A single parser worker processes Deferred before Last. The depth change
     * must not change an existing request or scan the old queue again. */
    vlc_playlist_Lock(playlist);
    assert(vlc_playlist_Count(playlist) == 5);
    assert(vlc_playlist_IndexOfMedia(playlist, deferred) == 2);
    for (size_t i = 0; i < ARRAY_SIZE(first_ids); ++i)
        assert(vlc_playlist_item_GetId(vlc_playlist_Get(playlist, i)) == first_ids[i]);
    vlc_playlist_Unlock(playlist);
    vlc_playlist_Delete(playlist);
    input_item_Release(first);
    input_item_Release(deferred);
    input_item_Release(last);

    playlist = vlc_playlist_New(VLC_OBJECT(instance->p_libvlc_int),
        VLC_PLAYLIST_PREPARSING_DISABLED, 1, VLC_TICK_FROM_SEC(5));
    assert(playlist);
    vlc_playlist_Lock(playlist);
    vlc_playlist_SetPreparseDepth(playlist, VLC_PLAYLIST_PREPARSING_RECURSIVE);
    assert(vlc_playlist_GetPreparseDepth(playlist) == VLC_PLAYLIST_PREPARSING_DISABLED);
    vlc_playlist_Unlock(playlist);
    input_item_t *disabled = append_directory(playlist, "Disabled");
    vlc_playlist_Lock(playlist);
    assert(vlc_playlist_Count(playlist) == 1);
    assert(vlc_playlist_IndexOfMedia(playlist, disabled) == 0);
    vlc_playlist_Unlock(playlist);
    vlc_playlist_Delete(playlist);
    input_item_Release(disabled);
    libvlc_release(instance);
    return 0;
}
