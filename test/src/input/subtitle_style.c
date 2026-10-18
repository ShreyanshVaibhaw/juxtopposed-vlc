// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../libvlc/test.h"
#include <vlc_common.h>
#include <vlc_codec.h>
#include <vlc_configuration.h>
#include "../../../modules/codec/substext.h"

static subpicture_t *new_subtitle(bool subtitle)
{
    subtext_updater_sys_t *sys = calloc(1, sizeof(*sys));
    assert(sys != NULL);
    SubpictureUpdaterSysRegionInit(&sys->region);
    sys->p_default_style = text_style_Create(STYLE_NO_DEFAULTS);
    sys->region.p_segments = text_segment_New("A styled subtitle");
    assert(sys->p_default_style != NULL && sys->region.p_segments != NULL);
    text_style_t *style = sys->region.p_segments->style = text_style_Create(STYLE_FULLY_SET);
    assert(style != NULL);
    style->psz_fontname = strdup("Authored Font");
    style->i_font_color = 0xff0000;
    style->i_style_flags |= STYLE_ITALIC;
    style->i_background_alpha = 96;
    style->i_style_flags |= STYLE_BACKGROUND;
    static const struct vlc_spu_updater_ops ops = {
        .update = SubpictureTextUpdate, .destroy = SubpictureTextDestroy,
    };
    const subpicture_updater_t updater = {.ops = &ops, .sys = sys};
    subpicture_t *subpic = subpicture_New(&updater);
    assert(subpic != NULL);
    subpic->b_subtitle = subtitle;
    return subpic;
}

static subpicture_region_t *update_size(subpicture_t *subpic, const struct vlc_subtitle_text_size *size)
{
    video_format_t fmt;
    video_format_Init(&fmt, VLC_CODEC_RGBA);
    fmt.i_width = fmt.i_visible_width = 1280;
    fmt.i_height = fmt.i_visible_height = 720;
    fmt.i_sar_num = fmt.i_sar_den = 1;
    subpicture_UpdateWithTextSize(subpic, &fmt, &fmt, 1280, 720, 0, size);
    return vlc_spu_regions_first_or_null(&subpic->regions);
}

static subpicture_region_t *update(subpicture_t *subpic)
{
    return update_size(subpic, NULL);
}

int main(void)
{
    test_init();
    const char *args[] = {"--ignore-config", "-Idummy", "--no-media-library"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance != NULL);
    if (config_GetType("freetype-color") == 0) {
        libvlc_release(instance);
        return 77;
    }
    config_PutInt("freetype-color", 0x123456);
    config_PutInt("freetype-outline-color", 0x654321);
    config_PutInt("freetype-background-opacity", 128);
    config_PutPsz("freetype-font", "User Font");
    config_PutInt("sub-text-use-embedded-style", true);
    config_PutInt("sub-text-size-mode", 0);
    subpicture_t *subtitle = new_subtitle(true);
    subpicture_region_t *region = update(subtitle);
    assert(region != NULL && region->p_text->style->i_font_color == 0xff0000);
    assert(strcmp(region->p_text->style->psz_fontname, "Authored Font") == 0);
    // Runtime shortcuts can change output sizing without saving new defaults.
    struct vlc_subtitle_text_size size = {.scale = 100, .pixels = true, .pixel_size = 42};
    region = update_size(subtitle, &size);
    assert(region->text_flags & VLC_SUBPIC_TEXT_FLAG_PIXEL_SIZE);
    assert(region->p_text->style->i_font_size == 42);
    assert(config_GetInt("sub-text-size-mode") == 0);
    size.pixel_size = 43;
    region = update_size(subtitle, &size);
    assert(region->p_text->style->i_font_size == 43);
    size.pixels = false;
    region = update_size(subtitle, &size);
    assert(!(region->text_flags & VLC_SUBPIC_TEXT_FLAG_PIXEL_SIZE));
    // A scale change must discard a cached region even with unchanged geometry.
    region->p_text->style->i_font_color = 0;
    size.scale = 150;
    region = update_size(subtitle, &size);
    assert(region->p_text->style->i_font_color == 0xff0000);

    // Same cue and geometry: changing appearance must bypass its render cache.
    config_PutInt("sub-text-use-embedded-style", false);
    vlc_subpicture_InvalidateTextStyle();
    region = update(subtitle);
    assert(region->p_text->style->i_font_color == 0x123456);
    assert(region->p_text->style->i_outline_color == 0x654321);
    assert(region->p_text->style->i_background_alpha == 128);
    assert(region->text_flags & VLC_SUBPIC_TEXT_FLAG_NO_REGION_BG);
    assert(strcmp(region->p_text->style->psz_fontname, "User Font") == 0);
    assert(!(region->p_text->style->i_style_flags & STYLE_ITALIC));

    config_PutPsz("freetype-font", "");
    vlc_subpicture_InvalidateTextStyle();
    region = update(subtitle);
    assert(region->p_text->style->psz_fontname == NULL
        || region->p_text->style->psz_fontname[0] == '\0');
    config_PutPsz("freetype-font", "User Font");
    vlc_subpicture_InvalidateTextStyle();
    region = update(subtitle);
    assert(strcmp(region->p_text->style->psz_fontname, "User Font") == 0);

    // Background visibility and opacity are independent for manual styling.
    config_PutInt("freetype-background-mode", 0);
    vlc_subpicture_InvalidateTextStyle();
    region = update(subtitle);
    assert(region->p_text->style->i_background_alpha == 0);
    assert(!(region->p_text->style->i_style_flags & STYLE_BACKGROUND));
    assert(config_GetInt("freetype-background-opacity") == 128);
    config_PutInt("freetype-background-opacity", 64);
    vlc_subpicture_InvalidateTextStyle();
    region = update(subtitle);
    assert(region->p_text->style->i_background_alpha == 0);
    config_PutInt("freetype-background-mode", 1);
    vlc_subpicture_InvalidateTextStyle();
    region = update(subtitle);
    assert(region->p_text->style->i_background_alpha == 64);
    assert(region->p_text->style->i_style_flags & STYLE_BACKGROUND);
    // Automatic preserves older opacity-only configuration and presets.
    config_PutInt("freetype-background-mode", -1);
    config_PutInt("freetype-background-opacity", 128);
    vlc_subpicture_InvalidateTextStyle();
    region = update(subtitle);
    assert(region->p_text->style->i_background_alpha == 128);

    config_PutInt("sub-text-size-mode", 1);
    config_PutInt("sub-text-size-pixels", 36);
    vlc_subpicture_InvalidateTextStyle();
    region = update(subtitle);
    assert(region->text_flags & VLC_SUBPIC_TEXT_FLAG_PIXEL_SIZE);
    assert(region->p_text->style->i_font_size == 36);
    assert(region->p_text->style->f_font_relsize == 0);

    // The original author style must survive toggles and preset restoration.
    config_PutInt("sub-text-use-embedded-style", true);
    config_PutInt("sub-text-size-mode", 0);
    config_PutInt("freetype-background-mode", 0);
    vlc_subpicture_InvalidateTextStyle();
    region = update(subtitle);
    assert(!(region->text_flags & VLC_SUBPIC_TEXT_FLAG_PIXEL_SIZE));
    assert(region->p_text->style->i_font_color == 0xff0000);
    assert(strcmp(region->p_text->style->psz_fontname, "Authored Font") == 0);
    assert(region->p_text->style->i_background_alpha == 96);
    subpicture_Delete(subtitle);

    config_PutInt("sub-text-use-embedded-style", false);
    config_PutInt("sub-text-size-mode", 1);
    subpicture_t *osd = new_subtitle(false);
    size.pixels = true;
    region = update_size(osd, &size);
    assert(!(region->text_flags & VLC_SUBPIC_TEXT_FLAG_PIXEL_SIZE));
    assert(region->p_text->style->i_font_color == 0xff0000);
    assert(strcmp(region->p_text->style->psz_fontname, "Authored Font") == 0);
    subpicture_Delete(osd);
    libvlc_release(instance);
    return 0;
}
