#include <string>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <stdbool.h>
#include <assert.h>

// copy from ffmpeg.c
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswresample/swresample.h"
#include "libavutil/opt.h"
#include "libavutil/channel_layout.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/fifo.h"
#include "libavutil/hwcontext.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/dict.h"
#include "libavutil/display.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avstring.h"
#include "libavutil/imgutils.h"
#include "libavutil/timestamp.h"
#include "libavutil/bprint.h"
#include "libavutil/time.h"
#include "libavutil/threadmessage.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersrc.h"
#include "libavfilter/buffersink.h"

#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#ifdef __cplusplus
};
#endif

template <typename Callback>
class scoped_exit
{
public:
    template <typename C>
    scoped_exit(C&& c)
        : callback_(std::forward<C>(c))
    {
    }
    scoped_exit(scoped_exit&& mv)
        : callback_(std::move(mv.callback_))
        , canceled_(mv.canceled_)
    {
        mv.canceled_ = true;
    }
    scoped_exit(const scoped_exit&) = delete;
    scoped_exit& operator=(const scoped_exit&) = delete;
    scoped_exit& operator=(scoped_exit&& mv) = delete;
    ~scoped_exit() { call(); }
    void cancel() { canceled_ = true; }

private:
    void call()
    {
        if (!canceled_)
        {
            try
            {
                callback_();
            } catch (...)
            {
            }
        }
    }
    Callback callback_;
    bool canceled_ = false;
};

template <typename Callback>
static scoped_exit<Callback> make_scoped_exit(Callback&& c)
{
    return scoped_exit<Callback>(std::forward<Callback>(c));
}

void print_error(const char* filename, int err)
{
    char errbuf[128];
    const char* errbuf_ptr = errbuf;

    if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
        errbuf_ptr = strerror(AVUNERROR(err));
    av_log(NULL, AV_LOG_ERROR, "%s: %s\n", filename, errbuf_ptr);
}

bool runing = true;

void handler(int signum)
{
    runing = false;
    printf("record desktop over\n");
}

int open_codec_context(int* stream_idx, AVCodecContext** dec_ctx, AVFormatContext* fmt_ctx, enum AVMediaType type);

int main(int argc, char** argv)
{
    signal(SIGINT, handler);
    avdevice_register_all();

    const char* filename = ":0.0+10,20";
    char outfilename[64] = {0};
    sprintf(outfilename, "%ld.yuv", time(NULL));

    AVDictionary* options = NULL;

    av_dict_set(&options, "video_size", "640x480", 0);
    AVFormatContext* ifmt_ctx = NULL;
    AVInputFormat* ifmt = av_find_input_format("x11grab");
    if (!ifmt)
    {
        printf("not found x11\n");
        exit(1);
    }
    int ret = avformat_open_input(&ifmt_ctx, filename, ifmt, &options);
    if (ret < 0)
    {
        print_error(filename, ret);
        printf("Couldn't open input stream.\n");
        return -1;
    }

    assert(ifmt_ctx);

    auto scoped_in_fmt = make_scoped_exit([& in_fmt_ctx = ifmt_ctx]() {
        avformat_close_input(&in_fmt_ctx);
        avformat_free_context(in_fmt_ctx);
    });

    if (avformat_find_stream_info(ifmt_ctx, NULL) < 0)
    {
        printf("Couldn't find stream information.\n");
        return -1;
    }
    av_dump_format(ifmt_ctx, 0, filename, 0);

    int stream_index = -1;
    AVCodecContext* video_decodec_ctx = NULL;
    ret = open_codec_context(&stream_index, &video_decodec_ctx, ifmt_ctx, AVMEDIA_TYPE_VIDEO);
    if (ret < 0)
    {
        printf("open codec context failed\n");
        exit(1);
    }
    assert(video_decodec_ctx);

    auto scoped_codec = make_scoped_exit([& codec = video_decodec_ctx]() { avcodec_close(codec); });

    int decodec_width = video_decodec_ctx->width;
    int decodec_height = video_decodec_ctx->height;
    //
    AVFrame* frame = av_frame_alloc();
    AVFrame* pFrameYUV = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();

    if (!frame || !pFrameYUV || !packet)
    {
        printf("frame or packet alloc failed\n");
        exit(1);
    }

    auto scoped_frame = make_scoped_exit([&]() {
        av_frame_free(&frame);
        av_frame_free(&pFrameYUV);
        av_packet_free(&packet);
    });

    av_init_packet(packet);

    unsigned char* out_buffer = (unsigned char*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P,
                                                                                   decodec_width,
                                                                                   decodec_height,
                                                                                   1));
    assert(out_buffer);

    auto scoped_buffer = make_scoped_exit([& buffer = out_buffer]() { av_free(buffer); });

    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer, AV_PIX_FMT_YUV420P, decodec_width, decodec_height, 1);

    struct SwsContext* img_convert_ctx = sws_getContext(decodec_width, decodec_height, video_decodec_ctx->pix_fmt, decodec_width, decodec_height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
    assert(img_convert_ctx);

    auto scoped_sws = make_scoped_exit([& sws = img_convert_ctx]() { sws_freeContext(sws); });

    FILE* fp_yuv = fopen(outfilename, "wb+");
    assert(fp_yuv);
    auto scoped = make_scoped_exit([& fp = fp_yuv]() { fclose(fp); });

    int got_picture = 0;
    while (runing)
    {
        ret = av_read_frame(ifmt_ctx, packet);
        if (ret < 0)
        {
            break;
        }

        auto scoped_packet = make_scoped_exit([& pkt = packet]() { av_packet_unref(pkt); });

        if (packet->stream_index != stream_index)
        {
            continue;
        }

        int ret = avcodec_send_packet(video_decodec_ctx, packet);
        if (ret < 0)
        {
            //LOG << "Error sending a packet for decoding";
            break;
        }
        while (ret >= 0)
        {
            ret = avcodec_receive_frame(video_decodec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                //LOG << " EAGAIN or AVERROR_EOF";
                break;
            }
            else if (ret < 0)
            {
                // Reset ?
                break;
            }
            assert(ret == 0);

            sws_scale(img_convert_ctx, (const unsigned char* const*)frame->data, frame->linesize, 0, video_decodec_ctx->height, pFrameYUV->data, pFrameYUV->linesize);
            int y_size = video_decodec_ctx->width * video_decodec_ctx->height;
            fwrite(pFrameYUV->data[0], 1, y_size, fp_yuv);      //Y
            fwrite(pFrameYUV->data[1], 1, y_size / 4, fp_yuv);  //U
            fwrite(pFrameYUV->data[2], 1, y_size / 4, fp_yuv);  //V
        }
    }

    return 0;
}

// copy form ffmpeg/doc/example/demuxing_decoding.c
int open_codec_context(int* stream_idx, AVCodecContext** dec_ctx, AVFormatContext* fmt_ctx, enum AVMediaType type)
{
    int ret, stream_index;
    AVStream* st;
    AVCodec* dec = NULL;
    AVDictionary* opts = NULL;

    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0)
    {
        fprintf(stderr, "Could not find %s stream in input file \n", av_get_media_type_string(type));
        return ret;
    }
    else
    {
        stream_index = ret;
        st = fmt_ctx->streams[stream_index];

        /* find decoder for the stream */
        dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec)
        {
            fprintf(stderr, "Failed to find %s codec\n", av_get_media_type_string(type));
            return AVERROR(EINVAL);
        }

        /* Allocate a codec context for the decoder */
        *dec_ctx = avcodec_alloc_context3(dec);
        if (!*dec_ctx)
        {
            fprintf(stderr, "Failed to allocate the %s codec context\n", av_get_media_type_string(type));
            return AVERROR(ENOMEM);
        }

        /* Copy codec parameters from input stream to output codec context */
        if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0)
        {
            fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n", av_get_media_type_string(type));
            return ret;
        }

        /* Init the decoders, with or without reference counting */
        if ((ret = avcodec_open2(*dec_ctx, dec, NULL)) < 0)
        {
            fprintf(stderr, "Failed to open %s codec\n", av_get_media_type_string(type));
            return ret;
        }
        (*dec_ctx)->thread_count = 8;
        *stream_idx = stream_index;
    }

    return 0;
}
