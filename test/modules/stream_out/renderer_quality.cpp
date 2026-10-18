// SPDX-License-Identifier: LGPL-2.1-or-later
#define MODULE_NAME renderer_quality_test
#undef VLC_DYNAMIC_PLUGIN
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_plugin.h>
#include <vlc_configuration.h>
#include "../../../modules/stream_out/renderer_common.hpp"
#include "renderer_quality_profiles.h"

const char vlc_module_name[] = MODULE_STRING;

static const char *backend;
static unsigned opened, closed, added, removed;
static char stream_id;

static sout_stream_t *TestChainNew(vlc_object_t *parent, const char *chain,
                                 sout_stream_t *end)
{
    assert(end == nullptr);
    const std::string option = std::string("venc=") + backend + "{";
    if (std::string(chain).find(option) == std::string::npos)
        return nullptr;
    opened++;
    return static_cast<sout_stream_t *>(
        vlc_object_create(parent, sizeof(sout_stream_t)));
}

static void *TestIdAdd(sout_stream_t *, const es_format_t *format, const char *)
{
    assert(format->i_codec == VLC_CODEC_I420);
    assert(format->video.i_width == 1920 && format->video.i_height == 1080);
    assert(format->video.i_frame_rate == 30 && format->video.i_frame_rate_base == 1);
    added++;
    return &stream_id;
}

static void TestIdDel(sout_stream_t *, void *id)
{
    assert(id == &stream_id);
    removed++;
}

static void TestChainDelete(sout_stream_t *stream, sout_stream_t *end)
{
    assert(end == nullptr);
    closed++;
    vlc_object_delete(stream);
}

// Exercise the production profile and size logic without opening an encoder.
#define sout_StreamChainNew TestChainNew
#define sout_StreamIdAdd TestIdAdd
#define sout_StreamIdDel TestIdDel
#define sout_StreamChainDelete TestChainDelete
#include "../../../modules/stream_out/renderer_common.cpp"

static std::string Profile(sout_stream_t *stream, const video_format_t &video,
                           int quality)
{
    vlc_fourcc_t codec = 0;
    auto result = vlc_sout_renderer_GetVcodecOption(stream, {VLC_CODEC_H264},
                                                  &codec, &video, quality);
    assert(codec == VLC_CODEC_H264);
    assert(opened == closed && added == removed && added == opened);
    return result;
}

vlc_module_begin()
    add_renderer_opts("test-cast-")
vlc_module_end()

extern "C" VLC_EXPORT const vlc_plugin_cb vlc_static_modules[];
extern "C" const vlc_plugin_cb vlc_static_modules[] = {VLC_SYMBOL(vlc_entry), nullptr};

static void Configuration(libvlc_instance_t *instance)
{
    const char *name = "test-cast-conversion-quality";
    const auto *config = config_FindConfig(name);
    // PE dynamic builds do not discover builtin test modules. Use the real
    // casting module there when it is available.
    if (!config) {
        name = "sout-chromecast-conversion-quality";
        config = config_FindConfig(name);
    }
    if (!config) {
        fputs("Casting option unavailable; running the profile checks only\n", stderr);
        return;
    }
    assert(config->orig.i == -1);
    assert(config_GetInt(name) == -1);
    int64_t *values = nullptr;
    char **labels = nullptr;
    assert(config_GetIntChoices(name, &values, &labels) == 5);
    for (int i = 0; i < 5; i++) {
        assert(values[i] == i - 1);
        config_PutInt(name, values[i]);
        const int quality = var_InheritInteger(instance->p_libvlc_int, name);
        assert(quality == values[i]);
        assert(vlc_sout_renderer_ResolveConversionQuality(quality)
               == (i == 0 ? CONVERSION_QUALITY_DEFAULT : quality));
        free(labels[i]);
    }
    free(values);
    free(labels);
    config_PutInt(name, -1);
    printf("Cast quality configuration: %s default=-1 values=-1,0,1,2,3\n", name);
}

int main(void)
{
    test_init();
    const char *args[] = {"--ignore-config", "--no-plugins-cache", "--no-media-library"};
    auto *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance);
    Configuration(instance);
    auto *stream = static_cast<sout_stream_t *>(
        vlc_object_create(instance->p_libvlc_int, sizeof(sout_stream_t)));
    assert(stream);
    video_format_t videos[4] = {};
    const unsigned sizes[][4] = {{1280, 720, 30, 1}, {1920, 1080, 30, 1},
                                {3840, 2160, 60, 1}, {1920, 1080, 0, 0}};
    for (size_t i = 0; i < ARRAY_SIZE(videos); i++) {
        video_format_Setup(&videos[i], VLC_CODEC_I420, sizes[i][0], sizes[i][1],
                          sizes[i][0], sizes[i][1], 1, 1);
        videos[i].i_frame_rate = sizes[i][2];
        videos[i].i_frame_rate_base = sizes[i][3];
    }
    size_t profile_index = 0;
    for (const char *name : {"qsv", "x264"}) {
        backend = name;
        for (const auto &video : videos)
            for (int quality = 0; quality < 4; quality++) {
                const auto actual = Profile(stream, video, quality);
                assert(profile_index < ARRAY_SIZE(manual_profiles));
                assert(actual == manual_profiles[profile_index++]);
                printf("MANUAL %s %ux%u/%u:%u %d %s\n", backend,
                       video.i_width, video.i_height, video.i_frame_rate,
                       video.i_frame_rate_base, quality, actual.c_str());
            }
    }
    assert(profile_index == ARRAY_SIZE(manual_profiles));
    fflush(stdout);
    for (const char *name : {"qsv", "x264"}) {
        backend = name;
        for (const auto &video : videos)
            assert(Profile(stream, video, -1) == Profile(stream, video, CONVERSION_QUALITY_DEFAULT));
    }
    for (auto &video : videos)
        video_format_Clean(&video);
    vlc_object_delete(stream);
    libvlc_release(instance);
    puts("Cast quality: 32 manual profiles unchanged; 8 Automatic profiles use the platform default; no encoder opened");
    return 0;
}
