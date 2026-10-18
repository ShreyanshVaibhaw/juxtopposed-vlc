// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_configuration.h>
#include <vlc_filter.h>
#include <vlc_modules.h>
#include <vlc_subpicture.h>

static void Render(filter_t *filter, vlc_fourcc_t chroma, uint8_t font_alpha,
                   uint8_t shadow_alpha)
{
    subpicture_region_t *input = subpicture_region_NewText();
    assert(input != NULL);
    input->p_text = text_segment_New("Background sample");
    assert(input->p_text != NULL);
    text_style_t *style = input->p_text->style = text_style_Create(STYLE_FULLY_SET);
    assert(style != NULL);
    style->i_font_size = 48;
    style->i_font_alpha = font_alpha;
    style->i_outline_alpha = 0;
    style->i_shadow_alpha = shadow_alpha;
    style->i_shadow_color = 0;
    style->i_background_alpha = 0;
    style->i_style_flags = STYLE_OUTLINE | STYLE_SHADOW;
    input->text_flags |= VLC_SUBPIC_TEXT_FLAG_PIXEL_SIZE;
    const vlc_fourcc_t chromas[] = {chroma, 0};
    subpicture_region_t *output = filter->ops->render(filter, input, chromas);
    assert(output != NULL && output->p_picture != NULL);
    const picture_t *picture = output->p_picture;
    const plane_t *plane = &picture->p[chroma == VLC_CODEC_YUVA ? 3 : 0];
    const unsigned step = chroma == VLC_CODEC_YUVA ? 1 : 4;
    const unsigned alpha = chroma == VLC_CODEC_RGBA ? 3 : 0;
    size_t visible = 0;
    for (unsigned y = 0; y < picture->format.i_visible_height; ++y)
        for (unsigned x = 0; x < picture->format.i_visible_width; ++x) {
            const uint8_t *pixel = plane->p_pixels + y * plane->i_pitch + x * step;
            if (pixel[alpha] == 0) continue;
            visible++;
        }
    assert((visible > 0) == (font_alpha > 0));
    subpicture_region_Delete(output);
    subpicture_region_Delete(input);
}

int main(void)
{
    const char *args[] = {"--ignore-config", "--no-plugins-cache", "--no-media-library", "-Idummy"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance != NULL);
    if (config_GetType("freetype-background-opacity") == 0) {
        libvlc_release(instance);
        return 77;
    }
    config_PutInt("freetype-background-opacity", 0);
    config_PutInt("freetype-outline-thickness", 0);
    config_PutInt("freetype-shadow-opacity", 0);
    filter_t *filter = vlc_object_create(instance->p_libvlc_int, sizeof(*filter));
    assert(filter != NULL);
    es_format_Init(&filter->fmt_in, VIDEO_ES, 0);
    es_format_Init(&filter->fmt_out, VIDEO_ES, 0);
    video_format_Setup(&filter->fmt_out.video, VLC_CODEC_RGBA, 1280, 720, 1280, 720, 1, 1);
    filter->p_module = vlc_filter_LoadModule(filter, "text renderer", "freetype", true);
    assert(filter->p_module != NULL);
    const vlc_fourcc_t chromas[] = {VLC_CODEC_RGBA, VLC_CODEC_ARGB, VLC_CODEC_YUVA};
    for (size_t i = 0; i < ARRAY_SIZE(chromas); ++i) {
        /* A zero-alpha shadow has no bitmap or space in the layout bounds. */
        Render(filter, chromas[i], 255, 0);
        Render(filter, chromas[i], 255, 128);
        Render(filter, chromas[i], 0, 0);
    }
    vlc_filter_UnloadModule(filter);
    es_format_Clean(&filter->fmt_in);
    es_format_Clean(&filter->fmt_out);
    vlc_object_delete(filter);
    libvlc_release(instance);
    return 0;
}
