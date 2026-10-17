// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_picture.h>
#include "../../../modules/video_filter/puzzle/puzzle_lib.h"
#include <stdio.h>
#undef NDEBUG
#include <assert.h>

static const struct {
    const char *name;
    vlc_fourcc_t chroma;
    uint8_t black[4], white[4];
} cases[] = {
    {"GBR", VLC_CODEC_GBR_PLANAR, {0, 0, 0}, {255, 255, 255}},
    {"GBRA", VLC_CODEC_GBRA_PLANAR, {0, 0, 0, 255}, {255, 255, 255, 255}},
    {"I420", VLC_CODEC_I420, {0, 128, 128}, {255, 128, 128}},
    {"NV12", VLC_CODEC_NV12, {0, 128}, {255, 128}},
    {"NV21", VLC_CODEC_NV21, {0, 128}, {255, 128}},
    {"I422", VLC_CODEC_I422, {0, 128, 128}, {255, 128, 128}},
    {"I444", VLC_CODEC_I444, {0, 128, 128}, {255, 128, 128}},
    {"YV12", VLC_CODEC_YV12, {0, 128, 128}, {255, 128, 128}},
    {"YUVA", VLC_CODEC_YUVA, {0, 128, 128, 255}, {255, 128, 128, 255}},
    {"Grey", VLC_CODEC_GREY, {0}, {255}},
};

static void CheckPixel(const picture_t *picture, unsigned x, unsigned y,
                       const uint8_t expected[4])
{
    for (int i = 0; i < picture->i_planes; ++i) {
        const plane_t *plane = &picture->p[i];
        const unsigned px = x * plane->i_visible_pitch / picture->p[0].i_visible_pitch;
        const unsigned py = y * plane->i_visible_lines / picture->p[0].i_visible_lines;
        assert(plane->p_pixels[py * plane->i_pitch + px] == expected[i]);
    }
}

static void CheckDrawing(unsigned index)
{
    picture_t *picture = picture_New(cases[index].chroma, 16, 16, 1, 1);
    assert(picture);
    const uint8_t *black = cases[index].black, *white = cases[index].white;

    puzzle_preset_desk_background(picture, 0, 128, 128);
    for (unsigned y = 0; y < 16; ++y)
        for (unsigned x = 0; x < 16; ++x)
            CheckPixel(picture, x, y, black);

    puzzle_preset_desk_background(picture, 255, 128, 128);
    puzzle_draw_rectangle(picture, 4, 4, 8, 8, 0, 128, 128);
    CheckPixel(picture, 2, 2, white);
    CheckPixel(picture, 4, 4, black);
    CheckPixel(picture, 10, 10, white);
    CheckPixel(picture, 11, 11, black);
    puzzle_fill_rectangle(picture, 4, 4, 8, 8, 0, 128, 128);
    CheckPixel(picture, 6, 6, black);
    CheckPixel(picture, 12, 12, white);

    static const char *sign[] = {"o.", ".o"};
    puzzle_preset_desk_background(picture, 0, 128, 128);
    puzzle_draw_sign(picture, 4, 4, 2, 2, sign, false);
    CheckPixel(picture, 4, 4, white);
    CheckPixel(picture, 5, 5, white);
    uint8_t shade[4];
    for (unsigned i = 0; i < ARRAY_SIZE(shade); ++i)
        shade[i] = (black[i] + white[i]) / 2;
    CheckPixel(picture, 5, 4, shade);
    CheckPixel(picture, 8, 8, black);

    puzzle_preset_desk_background(picture, 255, 128, 128);
    puzzle_draw_sign(picture, 4, 4, 2, 2, sign, true);
    CheckPixel(picture, 4, 4, shade);
    CheckPixel(picture, 5, 4, black);

    puzzle_preset_desk_background(picture, 0, 128, 128);
    puzzle_draw_sign(picture, -1, -1, 2, 2, sign, false);
    CheckPixel(picture, 0, 0, white);
    CheckPixel(picture, 2, 2, black);
    puzzle_draw_sign(picture, 100, 100, 2, 2, sign, false);
    puzzle_draw_sign(picture, -100, -100, 2, 2, sign, false);
    CheckPixel(picture, 2, 2, black);
    picture_Release(picture);
    printf("Puzzle drawing: %s passed\n", cases[index].name);
}

static void CheckClippedSign(void)
{
    uint8_t pixels[64] = {0};
    picture_t picture = {
        .format = {.i_chroma = VLC_CODEC_GREY},
        .i_planes = 1,
        .p = {{.p_pixels = pixels, .i_pitch = 8, .i_lines = 8,
               .i_visible_pitch = 8, .i_visible_lines = 8, .i_pixel_pitch = 1}},
    };
    static const char *sign[] = {"o.", ".o"};
    puzzle_draw_sign(&picture, -1, -1, 2, 2, sign, false);
    assert(pixels[0] == 255 && pixels[1] == 0 && pixels[8] == 0);
    puzzle_draw_sign(&picture, 100, 100, 2, 2, sign, false);
    puzzle_draw_sign(&picture, -100, -100, 2, 2, sign, false);
    puzzle_draw_sign(&picture, 0, 0, 0, 2, sign, false);
    assert(pixels[0] == 255 && pixels[1] == 0);
    puts("Puzzle drawing: clipped symbols passed");
}

int main(void)
{
    CheckClippedSign();
    for (unsigned i = 0; i < ARRAY_SIZE(cases); ++i)
        CheckDrawing(i);
    return 0;
}
