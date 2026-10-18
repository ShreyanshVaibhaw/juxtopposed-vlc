// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_configuration.h>
#include <vlc_plugin.h>
#include <vlc_filter.h>
#include <vlc_modules.h>
#include <vlc_subpicture.h>

static size_t Render(filter_t *filter, vlc_fourcc_t chroma, uint8_t font_alpha,
                     uint8_t shadow_alpha, bool knockout)
{
    config_PutInt("freetype-outline-thickness", knockout ? 4 : 0);
    subpicture_region_t *input = subpicture_region_NewText();
    assert(input != NULL);
    input->p_text = text_segment_New("Background sample");
    assert(input->p_text != NULL);
    text_style_t *style = input->p_text->style = text_style_Create(STYLE_FULLY_SET);
    assert(style != NULL);
    style->i_font_size = 48;
    style->i_font_alpha = font_alpha;
    style->i_outline_alpha = knockout ? 255 : 0;
    style->i_shadow_alpha = shadow_alpha;
    style->i_shadow_color = 0;
    style->i_background_alpha = 0;
    style->e_blending_mode = knockout ? STYLE_BLENDING_TRANSPARENT : STYLE_BLENDING_DEFAULT;
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
    assert((visible > 0) == (font_alpha > 0 || knockout));
    subpicture_region_Delete(output);
    subpicture_region_Delete(input);
    return visible;
}

struct raster {
    unsigned width, height;
    int x, y;
    uint64_t alpha_hash;
};

static struct raster CaptureRaster(const subpicture_region_t *output, vlc_fourcc_t chroma)
{
    assert(output && output->p_picture);
    const picture_t *picture = output->p_picture;
    struct raster result = {.width = picture->format.i_visible_width,
        .height = picture->format.i_visible_height, .x = output->i_x, .y = output->i_y,
        .alpha_hash = UINT64_C(14695981039346656037)};
    const plane_t *plane = &picture->p[chroma == VLC_CODEC_YUVA ? 3 : 0];
    const unsigned step = chroma == VLC_CODEC_YUVA ? 1 : 4;
    const unsigned alpha = chroma == VLC_CODEC_RGBA ? 3 : 0;
    for (unsigned y = 0; y < result.height; ++y)
        for (unsigned x = 0; x < result.width; ++x) {
            result.alpha_hash ^= plane->p_pixels[y * plane->i_pitch + step * x + alpha];
            result.alpha_hash *= UINT64_C(1099511628211);
        }
    return result;
}

static struct raster RenderText(filter_t *filter, const char *text, const char *family, bool monospace)
{
    subpicture_region_t *input = subpicture_region_NewText();
    assert(input);
    input->p_text = text_segment_New(text);
    assert(input->p_text);
    text_style_t *style = input->p_text->style = text_style_Create(STYLE_FULLY_SET);
    assert(style);
    style->i_font_size = 48;
    style->i_font_alpha = 255;
    style->i_outline_alpha = style->i_shadow_alpha = style->i_background_alpha = 0;
    style->i_style_flags = monospace ? STYLE_MONOSPACED : 0;
    if (family) {
        style->psz_fontname = strdup(family);
        assert(style->psz_fontname);
    }
    input->text_flags |= VLC_SUBPIC_TEXT_FLAG_PIXEL_SIZE;
    const vlc_fourcc_t chromas[] = {VLC_CODEC_RGBA, 0};
    subpicture_region_t *output = filter->ops->render(filter, input, chromas);
    const struct raster result = CaptureRaster(output, VLC_CODEC_RGBA);
    subpicture_region_Delete(output);
    subpicture_region_Delete(input);
    return result;
}

static struct raster RenderFamily(filter_t *filter, const char *family, bool monospace)
{
    return RenderText(filter, "Hamburgefonts 012345 iii WWW", family, monospace);
}

static bool SameRaster(struct raster a, struct raster b)
{
    return a.width == b.width && a.height == b.height && a.x == b.x && a.y == b.y
        && a.alpha_hash == b.alpha_hash;
}

static void FontDefaults(filter_t *filter)
{
    const module_config_t *cfg = config_FindConfig("freetype-font");
    if (cfg == NULL || cfg->i_type != CONFIG_ITEM_FONT)
        return;
    char *font = config_GetPsz("freetype-font");
    char *mono = config_GetPsz("freetype-monofont");
    assert(!font || !*font);
    assert(!mono || !*mono);
    free(font); free(mono);
#if defined(_WIN32)
    const char *sans = "Arial", *monospace = "Courier New";
#elif defined(__APPLE__)
    const char *sans = "Helvetica Neue", *monospace = "Monaco";
#elif defined(__OS2__)
    const char *sans = "Times New Roman WT K", *monospace = "Monotype Sans Duospace WT K";
#else
    const char *sans = "sans-serif", *monospace = "Monospace";
#endif
    filter->p_module = vlc_filter_LoadModule(filter, "text renderer", "freetype", true);
    assert(filter->p_module);
    const struct raster normal = RenderFamily(filter, NULL, false);
    config_PutPsz("freetype-font", sans);
    assert(SameRaster(normal, RenderFamily(filter, NULL, false)));
    config_PutPsz("freetype-font", monospace);
    const struct raster custom = RenderFamily(filter, NULL, false);
    assert(!SameRaster(normal, custom));
    config_PutPsz("freetype-font", "");
    assert(SameRaster(normal, RenderFamily(filter, NULL, false)));
    assert(SameRaster(custom, RenderFamily(filter, monospace, false)));
    const struct raster default_mono = RenderFamily(filter, NULL, true);
    config_PutPsz("freetype-monofont", monospace);
    assert(SameRaster(default_mono, RenderFamily(filter, NULL, true)));
    config_PutPsz("freetype-monofont", sans);
    assert(SameRaster(normal, RenderFamily(filter, NULL, true)));
    config_PutPsz("freetype-monofont", "");
    assert(SameRaster(default_mono, RenderFamily(filter, NULL, true)));
    vlc_filter_UnloadModule(filter);
    fprintf(stderr, "Default, explicit, authored and live font-family rasters passed\n");
}

static struct raster RenderShadow(filter_t *filter, vlc_fourcc_t chroma)
{
    subpicture_region_t *input = subpicture_region_NewText();
    assert(input);
    input->p_text = text_segment_New("Live shadow sample");
    assert(input->p_text);
    text_style_t *style = input->p_text->style = text_style_Create(STYLE_FULLY_SET);
    assert(style);
    style->i_font_size = 48;
    style->i_font_alpha = 255;
    style->i_outline_alpha = style->i_background_alpha = 0;
    style->i_shadow_alpha = 192;
    style->i_style_flags = STYLE_SHADOW;
    input->text_flags |= VLC_SUBPIC_TEXT_FLAG_PIXEL_SIZE;
    const vlc_fourcc_t chromas[] = {chroma, 0};
    subpicture_region_t *output = filter->ops->render(filter, input, chromas);
    const struct raster result = CaptureRaster(output, chroma);
    subpicture_region_Delete(output);
    subpicture_region_Delete(input);
    return result;
}

static struct raster FreshShadow(filter_t *parent, vlc_fourcc_t chroma)
{
    filter_t *filter = vlc_object_create(parent, sizeof(*filter));
    assert(filter);
    assert(es_format_Copy(&filter->fmt_in, &parent->fmt_in) == VLC_SUCCESS);
    assert(es_format_Copy(&filter->fmt_out, &parent->fmt_out) == VLC_SUCCESS);
    filter->p_module = vlc_filter_LoadModule(filter, "text renderer", "freetype", true);
    assert(filter->p_module);
    const struct raster result = RenderShadow(filter, chroma);
    vlc_filter_UnloadModule(filter);
    es_format_Clean(&filter->fmt_in); es_format_Clean(&filter->fmt_out);
    vlc_object_delete(filter);
    return result;
}

static void LiveShadow(filter_t *filter)
{
    const vlc_fourcc_t chromas[] = {VLC_CODEC_RGBA, VLC_CODEC_ARGB, VLC_CODEC_YUVA};
    for (int mode = 0; mode <= 1; ++mode) {
        config_PutInt("freetype-blending-mode", mode);
        config_PutFloat("freetype-shadow-angle", 0);
        config_PutFloat("freetype-shadow-distance", .1);
        filter->p_module = vlc_filter_LoadModule(filter, "text renderer", "freetype", true);
        assert(filter->p_module);
        for (size_t i = 0; i < ARRAY_SIZE(chromas); ++i) {
            const struct raster initial = RenderShadow(filter, chromas[i]);
            config_PutFloat("freetype-shadow-distance", .3);
            const struct raster distance = RenderShadow(filter, chromas[i]);
            assert(!SameRaster(initial, distance));
            config_PutFloat("freetype-shadow-angle", 90);
            const struct raster angle = RenderShadow(filter, chromas[i]);
            assert(!SameRaster(distance, angle));
            assert(SameRaster(angle, FreshShadow(filter, chromas[i])));
            config_PutFloat("freetype-shadow-angle", 0);
            config_PutFloat("freetype-shadow-distance", .1);
            assert(SameRaster(initial, RenderShadow(filter, chromas[i])));
        }
        vlc_filter_UnloadModule(filter);
    }
    config_PutFloat("freetype-shadow-angle", -45);
    config_PutFloat("freetype-shadow-distance", .06);
    fprintf(stderr, "Live shadow angle/distance/reset rasters passed in six output modes\n");
}

static void ParagraphDirection(filter_t *filter)
{
    if (config_GetType("freetype-text-direction") == 0)
        return;
    assert(config_GetInt("freetype-text-direction") == 2);
    config_PutInt("freetype-blending-mode", 0);
    filter->p_module = vlc_filter_LoadModule(filter, "text renderer", "freetype", true);
    assert(filter->p_module);
    const char *rtl = "שלום (abc 123)!";
    const struct raster automatic_rtl = RenderText(filter, rtl, NULL, false);
    config_PutInt("freetype-text-direction", 1);
    assert(SameRaster(automatic_rtl, RenderText(filter, rtl, NULL, false)));
    config_PutInt("freetype-text-direction", 0);
    assert(!SameRaster(automatic_rtl, RenderText(filter, rtl, NULL, false)));
    config_PutInt("freetype-text-direction", 2);
    assert(SameRaster(automatic_rtl, RenderText(filter, rtl, NULL, false)));
    const char *ltr = "abc 123 שלום";
    const struct raster automatic_ltr = RenderText(filter, ltr, NULL, false);
    config_PutInt("freetype-text-direction", 0);
    assert(SameRaster(automatic_ltr, RenderText(filter, ltr, NULL, false)));
    config_PutInt("freetype-text-direction", 2);
    vlc_filter_UnloadModule(filter);
    fprintf(stderr, "Automatic/LTR/RTL paragraph-direction rasters passed\n");
}

int main(void)
{
    test_init();
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
    FontDefaults(filter);
    LiveShadow(filter);
    ParagraphDirection(filter);
    const vlc_fourcc_t chromas[] = {VLC_CODEC_RGBA, VLC_CODEC_ARGB, VLC_CODEC_YUVA};
    for (int mode = 0; mode <= 1; ++mode) {
        config_PutInt("freetype-blending-mode", mode);
        filter->p_module = vlc_filter_LoadModule(filter, "text renderer", "freetype", true);
        assert(filter->p_module != NULL);
        for (size_t i = 0; i < ARRAY_SIZE(chromas); ++i) {
            /* A zero-alpha shadow has no bitmap or space in the layout bounds. */
            Render(filter, chromas[i], 255, 0, false);
            Render(filter, chromas[i], 255, 128, false);
            Render(filter, chromas[i], 0, 0, false);
            if (mode == 1) {
                const size_t filled = Render(filter, chromas[i], 255, 0, true);
                const size_t hollow = Render(filter, chromas[i], 0, 0, true);
                assert(hollow < filled);
            }
        }
        vlc_filter_UnloadModule(filter);
    }
    es_format_Clean(&filter->fmt_in);
    es_format_Clean(&filter->fmt_out);
    vlc_object_delete(filter);
    libvlc_release(instance);
    return 0;
}
