/*****************************************************************************
 * pcr_sync.c
 *****************************************************************************
 * Copyright (C) 2022 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdint.h>

#include <vlc_common.h>

#include <vlc_frame.h>
#include <vlc_list.h>
#include <vlc_tick.h>
#include <vlc_vector.h>

#include "pcr_sync.h"

struct es_dts_entry
{
    vlc_tick_t dts;
    bool passed;
    vlc_tick_t discontinuity;
};

typedef struct
{
    vlc_tick_t pcr;
    struct VLC_VECTOR(struct es_dts_entry) es_last_dts_entries;
    size_t entries_left;

    struct vlc_list node;
} pcr_event_t;

static inline void pcr_event_Delete(pcr_event_t *ev)
{
    vlc_vector_destroy(&ev->es_last_dts_entries);
    free(ev);
}

struct es_data
{
    bool is_deleted;
    vlc_tick_t last_input_dts;
    vlc_tick_t last_output_dts;
    vlc_tick_t discontinuity;
    pcr_event_t *last_pcr_event;
};

static inline struct es_data es_data_Init(void)
{
    return (struct es_data){.is_deleted = false,
                            .last_input_dts = VLC_TICK_INVALID,
                            .last_output_dts = VLC_TICK_INVALID,
                            .discontinuity = VLC_TICK_INVALID};
}

struct es_data_vec VLC_VECTOR(struct es_data);

struct vlc_pcr_sync
{
    struct vlc_list pcr_events;
    struct es_data_vec es_data;
    vlc_mutex_t lock;
};

vlc_pcr_sync_t *vlc_pcr_sync_New(void)
{
    vlc_pcr_sync_t *ret = malloc(sizeof(*ret));
    if (unlikely(ret == NULL))
        return NULL;

    vlc_vector_init(&ret->es_data);
    vlc_list_init(&ret->pcr_events);
    vlc_mutex_init(&ret->lock);
    return ret;
}

void vlc_pcr_sync_Delete(vlc_pcr_sync_t *pcr_sync)
{
    vlc_vector_destroy(&pcr_sync->es_data);

    pcr_event_t *it;
    vlc_list_foreach(it, &pcr_sync->pcr_events, node) { pcr_event_Delete(it); }

    free(pcr_sync);
}

static bool pcr_sync_ShouldFastForwardPCR(vlc_pcr_sync_t *pcr_sync)
{
    struct es_data it;
    vlc_vector_foreach(it, &pcr_sync->es_data)
    {
        if (!it.is_deleted && it.last_output_dts != it.last_input_dts)
            return false;
    }
    return vlc_list_is_empty(&pcr_sync->pcr_events);
}

static int pcr_sync_SignalPCRLocked(vlc_pcr_sync_t *pcr_sync, vlc_tick_t pcr)
{
    if (pcr_sync_ShouldFastForwardPCR(pcr_sync))
        return VLC_PCR_SYNC_FORWARD_PCR;

    pcr_event_t *event = malloc(sizeof(*event));
    if (unlikely(event == NULL))
        return VLC_ENOMEM;

    vlc_vector_init(&event->es_last_dts_entries);
    const bool alloc_succeeded =
        vlc_vector_reserve(&event->es_last_dts_entries, pcr_sync->es_data.size);
    if (unlikely(alloc_succeeded == false))
    {
        free(event);
        return VLC_ENOMEM;
    }

    size_t entries_left = 0;
    for (size_t i = 0; i < pcr_sync->es_data.size; ++i)
    {
        struct es_data *data = &pcr_sync->es_data.data[i];
        const bool passed = data->is_deleted
                         || data->last_input_dts == VLC_TICK_INVALID
                         || data->last_output_dts == data->last_input_dts;
        /* Preserve ES IDs, including vacant slots. Only frames that are still
         * in the processing unit can delay this PCR. */
        vlc_vector_push(&event->es_last_dts_entries,
                        ((struct es_dts_entry){.dts = data->last_input_dts,
                                               .passed = passed,
                                               .discontinuity = data->discontinuity}));
        if (!passed)
            ++entries_left;
        data->discontinuity = VLC_TICK_INVALID;
    }

    event->pcr = pcr;
    event->entries_left = entries_left;

    vlc_list_append(&event->node, &pcr_sync->pcr_events);

    return VLC_SUCCESS;
}

int vlc_pcr_sync_SignalPCR(vlc_pcr_sync_t *pcr_sync, vlc_tick_t pcr)
{
    vlc_mutex_lock(&pcr_sync->lock);
    const int result = pcr_sync_SignalPCRLocked(pcr_sync, pcr);
    vlc_mutex_unlock(&pcr_sync->lock);
    return result;
}

void vlc_pcr_sync_SignalFrame(vlc_pcr_sync_t *pcr_sync, unsigned int id, const vlc_frame_t *frame)
{
    vlc_mutex_lock(&pcr_sync->lock);
    assert(id < pcr_sync->es_data.size);

    struct es_data *data = &pcr_sync->es_data.data[id];
    assert(!data->is_deleted);
    if (frame->i_dts != VLC_TICK_INVALID)
        data->last_input_dts = frame->i_dts;
    if (frame->i_flags & VLC_FRAME_FLAG_DISCONTINUITY)
    {
        assert(frame->i_dts != VLC_TICK_INVALID);
        data->discontinuity = frame->i_dts;
    }

    vlc_mutex_unlock(&pcr_sync->lock);
}

int vlc_pcr_sync_NewESID(vlc_pcr_sync_t *pcr_sync, unsigned int *id)
{
    vlc_mutex_lock(&pcr_sync->lock);

    for (unsigned int i = 0; i < pcr_sync->es_data.size; ++i)
    {
        if (pcr_sync->es_data.data[i].is_deleted)
        {
            pcr_sync->es_data.data[i] = es_data_Init();
            vlc_mutex_unlock(&pcr_sync->lock);
            *id = i;
            return VLC_SUCCESS;
        }
    }

    const bool push_succeeded = vlc_vector_push(&pcr_sync->es_data, es_data_Init());
    const size_t ids_count = pcr_sync->es_data.size;

    vlc_mutex_unlock(&pcr_sync->lock);

    if (push_succeeded)
    {
        *id = ids_count - 1;
        return VLC_SUCCESS;
    }
    return VLC_ENOMEM;
}

void vlc_pcr_sync_DelESID(vlc_pcr_sync_t *pcr_sync, unsigned int id)
{
    vlc_mutex_lock(&pcr_sync->lock);

    assert(id < pcr_sync->es_data.size);
    assert(!pcr_sync->es_data.data[id].is_deleted);
    pcr_sync->es_data.data[id].is_deleted = true;
    pcr_sync->es_data.data[id].last_pcr_event = NULL;

    /* Queued events must no longer wait for this track. Mark its old slots
     * before the ID can be reused for a different track. */
    pcr_event_t *event;
    vlc_list_foreach(event, &pcr_sync->pcr_events, node)
    {
        if (id < event->es_last_dts_entries.size
            && !event->es_last_dts_entries.data[id].passed)
        {
            event->es_last_dts_entries.data[id].passed = true;
            assert(event->entries_left != 0);
            --event->entries_left;
        }
    }

    vlc_mutex_unlock(&pcr_sync->lock);
}

#define pcr_event_FirstEntry(head)                                                                 \
    vlc_list_first_entry_or_null(head, pcr_event_t, node)
#define pcr_event_NextEntry(head, entry)                                                           \
    vlc_list_next_entry_or_null(head, entry, pcr_event_t, node)

vlc_tick_t
vlc_pcr_sync_SignalFrameOutput(vlc_pcr_sync_t *pcr_sync, unsigned int id, const vlc_frame_t *frame)
{
    vlc_mutex_lock(&pcr_sync->lock);

    assert(id < pcr_sync->es_data.size);
    struct es_data *es = &pcr_sync->es_data.data[id];
    assert(!es->is_deleted);
    es->last_output_dts = frame->i_dts;

    pcr_event_t *event = es->last_pcr_event == NULL
                      ? pcr_event_FirstEntry(&pcr_sync->pcr_events)
                      : es->last_pcr_event;
    for (; event != NULL; event = es->last_pcr_event)
    {
        pcr_event_t *next = pcr_event_NextEntry(&pcr_sync->pcr_events, event);
        /* A new track cannot acknowledge frames from before it was added. */
        if (id < event->es_last_dts_entries.size)
        {
            struct es_dts_entry *entry = &event->es_last_dts_entries.data[id];
            if (!entry->passed)
            {
                if (entry->discontinuity != VLC_TICK_INVALID
                    && frame->i_dts > entry->discontinuity)
                    break;
                if (frame->i_dts < entry->dts)
                    break;

                entry->passed = true;
                assert(event->entries_left != 0);
                --event->entries_left;
            }
        }
        es->last_pcr_event = next;
    }

    /* A fast track can finish a later event first. Keep PCRs in input order,
     * including consecutive PCRs with no intervening frames. */
    event = pcr_event_FirstEntry(&pcr_sync->pcr_events);
    if (event == NULL || event->entries_left != 0)
    {
        vlc_mutex_unlock(&pcr_sync->lock);
        return VLC_TICK_INVALID;
    }

    const vlc_tick_t pcr = event->pcr;
    pcr_event_t *next = pcr_event_NextEntry(&pcr_sync->pcr_events, event);
    for (size_t i = 0; i < pcr_sync->es_data.size; ++i)
        if (pcr_sync->es_data.data[i].last_pcr_event == event)
            pcr_sync->es_data.data[i].last_pcr_event = next;
    vlc_list_remove(&event->node);
    pcr_event_Delete(event);

    vlc_mutex_unlock(&pcr_sync->lock);
    return pcr;
}
