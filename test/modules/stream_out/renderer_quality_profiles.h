// SPDX-License-Identifier: LGPL-2.1-or-later
// Saved manual-quality profiles recorded before Automatic was added.
#ifndef VLC_RENDERER_QUALITY_PROFILES_H
#define VLC_RENDERER_QUALITY_PROFILES_H
static const char *const manual_profiles[] = {
    "vcodec=h264,venc=qsv{target-usage=quality},vb=8000000,maxwidth=1920,maxheight=1080,",
    "vcodec=h264,venc=qsv{target-usage=balanced},vb=8000000,maxwidth=1920,maxheight=1080,",
    "vcodec=h264,venc=qsv{target-usage=balanced},vb=3000000,maxwidth=1280,maxheight=720,",
    "vcodec=h264,venc=qsv{target-usage=speed},vb=3000000,maxwidth=1280,maxheight=720,",
    "vcodec=h264,venc=qsv{target-usage=quality},vb=8000000,maxwidth=1920,maxheight=1080,",
    "vcodec=h264,venc=qsv{target-usage=balanced},vb=8000000,maxwidth=1920,maxheight=1080,",
    "vcodec=h264,venc=qsv{target-usage=balanced},vb=3000000,width=1280,maxwidth=1280,maxheight=720,",
    "vcodec=h264,venc=qsv{target-usage=speed},vb=3000000,width=1280,maxwidth=1280,maxheight=720,",
    "vcodec=h264,venc=qsv{target-usage=quality},vb=8000000,width=1920,maxwidth=1920,maxheight=1080,fps=24,",
    "vcodec=h264,venc=qsv{target-usage=balanced},vb=8000000,width=1920,maxwidth=1920,maxheight=1080,fps=24,",
    "vcodec=h264,venc=qsv{target-usage=balanced},vb=3000000,width=1280,maxwidth=1280,maxheight=720,fps=24,",
    "vcodec=h264,venc=qsv{target-usage=speed},vb=3000000,width=1280,maxwidth=1280,maxheight=720,fps=24,",
    "vcodec=h264,venc=qsv{target-usage=quality},vb=8000000,maxwidth=1920,maxheight=1080,fps=24,",
    "vcodec=h264,venc=qsv{target-usage=balanced},vb=8000000,maxwidth=1920,maxheight=1080,fps=24,",
    "vcodec=h264,venc=qsv{target-usage=balanced},vb=3000000,width=1280,maxwidth=1280,maxheight=720,fps=24,",
    "vcodec=h264,venc=qsv{target-usage=speed},vb=3000000,width=1280,maxwidth=1280,maxheight=720,fps=24,",
    "vcodec=h264,venc=x264{preset=veryfast,crf=21},maxwidth=1920,maxheight=1080,",
    "vcodec=h264,venc=x264{preset=veryfast,crf=21},maxwidth=1920,maxheight=1080,",
    "vcodec=h264,venc=x264{preset=veryfast,crf=23},maxwidth=1280,maxheight=720,",
    "vcodec=h264,venc=x264{preset=ultrafast,crf=23},maxwidth=1280,maxheight=720,",
    "vcodec=h264,venc=x264{preset=veryfast,crf=21},maxwidth=1920,maxheight=1080,",
    "vcodec=h264,venc=x264{preset=veryfast,crf=23},maxwidth=1920,maxheight=1080,",
    "vcodec=h264,venc=x264{preset=veryfast,crf=23},width=1280,maxwidth=1280,maxheight=720,",
    "vcodec=h264,venc=x264{preset=ultrafast,crf=23},width=1280,maxwidth=1280,maxheight=720,",
    "vcodec=h264,venc=x264{preset=veryfast,crf=21},width=1920,maxwidth=1920,maxheight=1080,fps=24,",
    "vcodec=h264,venc=x264{preset=veryfast,crf=23},width=1920,maxwidth=1920,maxheight=1080,fps=24,",
    "vcodec=h264,venc=x264{preset=veryfast,crf=23},width=1280,maxwidth=1280,maxheight=720,fps=24,",
    "vcodec=h264,venc=x264{preset=ultrafast,crf=23},width=1280,maxwidth=1280,maxheight=720,fps=24,",
    "vcodec=h264,venc=x264{preset=veryfast,crf=21},maxwidth=1920,maxheight=1080,fps=24,",
    "vcodec=h264,venc=x264{preset=veryfast,crf=23},maxwidth=1920,maxheight=1080,fps=24,",
    "vcodec=h264,venc=x264{preset=veryfast,crf=23},width=1280,maxwidth=1280,maxheight=720,fps=24,",
    "vcodec=h264,venc=x264{preset=ultrafast,crf=23},width=1280,maxwidth=1280,maxheight=720,fps=24,",
};
#endif
