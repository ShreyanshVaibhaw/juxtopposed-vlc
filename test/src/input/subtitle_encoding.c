// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_codec.h>
#include <vlc_modules.h>

struct owner {
    decoder_t decoder;
    const char *expected;
    unsigned queued;
};

static subpicture_t *new_buffer(decoder_t *dec, const subpicture_updater_t *updater)
{
    (void)dec;
    return subpicture_New(updater);
}

static void queue_subtitle(decoder_t *dec, subpicture_t *subpic)
{
    struct owner *owner = container_of(dec, struct owner, decoder);
    video_format_t video;
    video_format_Init(&video, VLC_CODEC_RGBA);
    video.i_width = video.i_visible_width = 1280;
    video.i_height = video.i_visible_height = 720;
    video.i_sar_num = video.i_sar_den = 1;
    subpicture_Update(subpic, &video, &video, 1280, 720, VLC_TICK_FROM_SEC(1));
    char actual[256] = "";
    subpicture_region_t *region;
    vlc_spu_regions_foreach(region, &subpic->regions)
        for (text_segment_t *text = region->p_text; text; text = text->p_next) {
            if (!text->psz_text) continue;
            assert(strlen(actual) + strlen(text->psz_text) < sizeof(actual));
            strcat(actual, text->psz_text);
        }
    assert(strcmp(actual, owner->expected) == 0);
    owner->queued++;
    subpicture_Delete(subpic);
    video_format_Clean(&video);
}

int main(void)
{
    test_init();
    setenv("LC_ALL", "C", 1);
    const char *args[] = {"--ignore-config", "--no-media-library", "--no-plugins-cache"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance);
    if (!module_exists("subsdec")) {
        libvlc_release(instance);
        return 77;
    }
    const char *utf8 = "café, €";
    const char *legacy = "caf\xe9, \x80";
    const char *cyrillic = "\xcf\xf0\xe8\xe2\xe5\xf2";
    const struct {
        const char *encoding;
        bool automatic;
        const char *demux_encoding;
        const char *input;
        const char *expected;
    } cases[] = {
        {"", true, NULL, utf8, utf8},
        {"", true, NULL, legacy, utf8},
        {"Windows-1252", true, NULL, utf8, utf8},
        {"Windows-1252", false, NULL, legacy, utf8},
        {"Windows-1252", false, NULL, utf8, "cafÃ©, â‚¬"},
        {"UTF-8", false, NULL, utf8, utf8},
        {"Windows-1251", false, NULL, cyrillic, "Привет"},
        {"CP1251", true, NULL, cyrillic, "Привет"},
        {"CP1251", false, NULL, cyrillic, "Привет"},
        {"Windows-1252", false, "UTF-8", utf8, utf8},
        {"UTF-8", true, "Windows-1251", cyrillic, "Привет"},
    };
    static const struct decoder_owner_callbacks callbacks = {
        .spu = {.buffer_new = new_buffer, .queue = queue_subtitle},
    };
    for (size_t i = 0; i < ARRAY_SIZE(cases); ++i) {
        struct owner *owner = vlc_object_create(instance->p_libvlc_int, sizeof(*owner));
        assert(owner);
        decoder_t *dec = &owner->decoder;
        owner->expected = cases[i].expected;
        owner->queued = 0;
        es_format_t format, input;
        es_format_Init(&format, SPU_ES, VLC_CODEC_SUBT);
        if (cases[i].demux_encoding) format.subs.psz_encoding = strdup(cases[i].demux_encoding);
        decoder_Init(dec, &input, &format);
        es_format_Clean(&format);
        dec->cbs = &callbacks;
        var_Create(dec, "subsdec-encoding", VLC_VAR_STRING);
        var_SetString(dec, "subsdec-encoding", cases[i].encoding);
        var_Create(dec, "subsdec-autodetect-utf8", VLC_VAR_BOOL);
        var_SetBool(dec, "subsdec-autodetect-utf8", cases[i].automatic);
        dec->p_module = module_need(dec, "spu decoder", "subsdec", true);
        assert(dec->p_module);
        block_t *block = block_Alloc(strlen(cases[i].input));
        assert(block);
        memcpy(block->p_buffer, cases[i].input, block->i_buffer);
        block->i_pts = VLC_TICK_FROM_SEC(1);
        block->i_length = VLC_TICK_FROM_SEC(2);
        assert(dec->pf_decode(dec, block) == VLCDEC_SUCCESS);
        assert(owner->queued == 1);
        decoder_Clean(dec);
        es_format_Clean(&input);
        vlc_object_delete(dec);
    }
    fprintf(stderr, "%zu subtitle encoding cases passed\n", ARRAY_SIZE(cases));
    libvlc_release(instance);
    return 0;
}
