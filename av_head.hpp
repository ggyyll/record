#ifndef __HEAD_H__
#define __HEAD_H__

#include <string>

#ifdef __cplusplus
extern "C" {
#endif

#include <libavutil/avutil.h>
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <SDL2/SDL.h>
#include <libavutil/parseutils.h>
#include <libavutil/bprint.h>
#include <libavutil/tree.h>
#include <libavutil/eval.h>
#include <libavutil/lfg.h>
#include <libavutil/timecode.h>
#include <libavutil/file.h>
#include <libavutil/random_seed.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>

#include <fenv.h>
#include <assert.h>
#include "SDL2/SDL_thread.h"
#ifdef __cplusplus
}
#endif

#endif
