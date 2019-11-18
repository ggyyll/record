#include <string>
#include "record.h"
#include "scoped_exit.h"

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

static int open_codec_context(int* stream_idx, AVCodecContext** dec_ctx, AVFormatContext* fmt_ctx, enum AVMediaType type)
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
Record::Record(const std::string& filename)
    : filename_(filename)
    , stream_indexs_(255, 0)
    , runing_(false) {};

Record::~Record()
{
    avformat_close_input(&input_fmt_ctx_);
    avformat_free_context(input_fmt_ctx_);
    avcodec_close(decoder_codec_ctx_);
    av_frame_free(&yuv_frame_);
    av_free(image_buffer_);
    sws_freeContext(sws_ctx_);

    avformat_close_input(&output_fmt_ctx_);
    avformat_free_context(output_fmt_ctx_);
    avcodec_close(encoder_codec_ctx_);
}

void Record::InitEnv()
{
    avdevice_register_all();

    InitInput();

    InitOutput();
}

void Record::Stop()
{
    runing_ = false;
}
void Record::InitInput()
{
    AVDictionary* options = NULL;
    int ret = av_dict_set(&options, "capture_cursor", "1", 0);
    if (ret < 0)
    {
        printf("\nerror in setting capture_cursor values");
        exit(1);
    }

    ret = av_dict_set(&options, "capture_mouse_clicks", "1", 0);
    if (ret < 0)
    {
        printf("\nerror in setting capture_cursor values");
        exit(1);
    }

    ret = av_dict_set(&options, "pixel_format", "yuyv420", 0);
    if (ret < 0)
    {
        printf("\nerror in setting pixel_format values");
        exit(1);
    }

    ret = av_dict_set(&options, "framerate", "60", 0);
    if (ret < 0)
    {
        printf("\nerror in setting pixel_format values");
        exit(1);
    }

    ret = av_dict_set(&options, "video_size", "1920x1080", 0);
    if (ret < 0)
    {
        printf("\nerror in setting pixel_format values");
        exit(1);
    }

    auto dict_scoped = make_scoped_exit([& op = options]() { av_dict_free(&op); });

    input_fmt_ = av_find_input_format("x11grab");
    if (!input_fmt_)
    {
        printf("not found x11\n");
        exit(1);
    }
    input_fmt_ctx_ = NULL;
    ret = avformat_open_input(&input_fmt_ctx_, filename_.data(), input_fmt_, &options);
    if (ret < 0)
    {
        printf("Couldn't open input stream.\n");
        exit(1);
    }

    assert(input_fmt_ctx_);

    if (avformat_find_stream_info(input_fmt_ctx_, NULL) < 0)
    {
        printf("Couldn't find stream information.\n");
        exit(1);
    }

    av_dump_format(input_fmt_ctx_, 0, filename_.data(), 0);

    int video_stream_idx = -1;
    decoder_codec_ctx_ = NULL;
    ret = open_codec_context(&video_stream_idx, &decoder_codec_ctx_, input_fmt_ctx_, AVMEDIA_TYPE_VIDEO);
    if (ret < 0)
    {
        printf("open codec context failed\n");
        exit(1);
    }

    assert(decoder_codec_ctx_);
    assert(video_stream_idx != -1);
    assert(video_stream_idx < 255);
    stream_indexs_[video_stream_idx] = 1;

    int decodec_width = decoder_codec_ctx_->width;
    int decodec_height = decoder_codec_ctx_->height;
    yuv_frame_ = av_frame_alloc();
    assert(yuv_frame_);
    int image_size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, decodec_width, decodec_height, 1);
    assert(image_size >= 0);
    image_buffer_ = (uint8_t*)av_malloc(image_size);
    assert(image_buffer_);

    av_image_fill_arrays(yuv_frame_->data, yuv_frame_->linesize, image_buffer_, AV_PIX_FMT_YUV420P, decodec_width, decodec_height, 1);

    sws_ctx_ = sws_getContext(decodec_width, decodec_height, decoder_codec_ctx_->pix_fmt, decodec_width, decodec_height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
    assert(sws_ctx_);
}

void Record::InitOutput()
{
    output_fmt_ctx_ = NULL;
    out_name_ = std::to_string(::time(NULL));
    out_name_ += ".mp4";
    avformat_alloc_output_context2(&output_fmt_ctx_, NULL, NULL, out_name_.data());
    assert(output_fmt_ctx_);

    output_fmt_ = av_guess_format(NULL, out_name_.data(), NULL);
    assert(output_fmt_);
    output_fmt_ctx_->oformat = output_fmt_;
    output_stream_ = avformat_new_stream(output_fmt_ctx_, NULL);
    assert(output_stream_);

    output_codec_ = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    if (!output_codec_)
    {
        printf("Could not find encoder for '%s'\n", avcodec_get_name(AV_CODEC_ID_MPEG4));
        exit(1);
    }

    encoder_codec_ctx_ = avcodec_alloc_context3(output_codec_);
    assert(encoder_codec_ctx_);
    encoder_codec_ctx_->codec_id = AV_CODEC_ID_MPEG4;
    encoder_codec_ctx_->codec_type = AVMEDIA_TYPE_VIDEO;
    encoder_codec_ctx_->bit_rate = 400000;
    encoder_codec_ctx_->width = decoder_codec_ctx_->width;
    encoder_codec_ctx_->height = decoder_codec_ctx_->height;
    encoder_codec_ctx_->time_base = (AVRational) {1, 25};
    encoder_codec_ctx_->gop_size = 12;
    encoder_codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;

    if (output_fmt_->flags & AVFMT_GLOBALHEADER)
    {
        encoder_codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    int ret = avcodec_open2(encoder_codec_ctx_, output_codec_, NULL);
    if (ret < 0)
    {
        printf("avcodec_open2 failed\n");
        exit(1);
    }

    avcodec_parameters_from_context(output_stream_->codecpar, encoder_codec_ctx_);

    if (!(output_fmt_->flags & AVFMT_NOFILE))
    {
        int ret = avio_open(&output_fmt_ctx_->pb, out_name_.data(), AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            printf("Could not open output file '%s'", out_name_.data());
            exit(1);
        }
    }

    AVDictionary* opt = NULL;
    av_dict_set_int(&opt, "video_track_timescale", 25, 0);
    auto scoped_opt = make_scoped_exit([&opt]() { av_dict_free(&opt); });
    ret = avformat_write_header(output_fmt_ctx_, &opt);
    if (ret < 0)
    {
        printf("Error occurred when opening output file\n");
        exit(1);
    }
}

void Record::Run()
{
    AVFrame* frame = av_frame_alloc();
    AVFrame* pFrameYUV = yuv_frame_;
    AVPacket* packet = av_packet_alloc();

    if (!frame || !packet)
    {
        printf("frame or packet alloc failed\n");
        exit(1);
    }
    av_init_packet(packet);

    auto scoped_frame = make_scoped_exit([&]() {
        av_frame_free(&frame);
        av_packet_free(&packet);
    });
    std::string file_name = std::to_string(::time(NULL));
    file_name += ".yuv";

    FILE* fp_yuv = fopen(file_name.data(), "wb+");
    assert(fp_yuv);
    auto scoped = make_scoped_exit([& fp = fp_yuv]() { fclose(fp); });
    runing_ = true;
    while (runing_)
    {
        int ret = av_read_frame(input_fmt_ctx_, packet);
        if (ret < 0)
        {
            break;
        }
        if (stream_indexs_[packet->stream_index] != 1)
        {
            continue;
        }

        auto scoped_packet = make_scoped_exit([& pkt = packet]() { av_packet_unref(pkt); });

        ret = avcodec_send_packet(decoder_codec_ctx_, packet);
        if (ret < 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            // flush codec ? continue
            break;
        }
        while (ret >= 0)
        {
            ret = avcodec_receive_frame(decoder_codec_ctx_, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                break;
            }
            else if (ret < 0)
            {
                // Reset ?
                break;
            }
            assert(ret == 0);

            sws_scale(sws_ctx_, (const unsigned char* const*)frame->data, frame->linesize, 0, decoder_codec_ctx_->height, pFrameYUV->data, pFrameYUV->linesize);
            int y_size = decoder_codec_ctx_->width * decoder_codec_ctx_->height;
            fwrite(pFrameYUV->data[0], 1, y_size, fp_yuv);      //Y
            fwrite(pFrameYUV->data[1], 1, y_size / 4, fp_yuv);  //U
            fwrite(pFrameYUV->data[2], 1, y_size / 4, fp_yuv);  //V

            pFrameYUV->format = AV_PIX_FMT_YUV420P;
            pFrameYUV->width = encoder_codec_ctx_->width;
            pFrameYUV->height = encoder_codec_ctx_->height;

            ret = avcodec_send_frame(encoder_codec_ctx_, pFrameYUV);
            if (ret < 0)
            {
                printf("%s Error sending a frame for encoding\n", av_err2str(ret));
                assert(false);
                break;
            }

            while (ret >= 0)
            {
                AVPacket out_packet = {0};
                av_init_packet(&out_packet);
                out_packet.data = NULL;  // packet data will be allocated by the encoder
                out_packet.size = 0;

                ret = avcodec_receive_packet(encoder_codec_ctx_, &out_packet);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                {
                    break;
                }
                else if (ret < 0)
                {
                    printf("Error during encoding\n");
                    assert(false);
                    break;
                }

                auto scoped_packet = make_scoped_exit([& pkt = out_packet]() { av_packet_unref(&pkt); });

                if (out_packet.pts != AV_NOPTS_VALUE)
                    out_packet.pts = av_rescale_q(out_packet.pts, encoder_codec_ctx_->time_base, output_stream_->time_base);

                if (av_write_frame(output_fmt_ctx_, &out_packet) != 0)
                {
                    printf("\nerror in writing video frame\n");
                    assert(false);
                }
            }
        }
    }

    int ret = av_write_trailer(output_fmt_ctx_);
    if (ret < 0)
    {
        printf("\nerror in writing av trailer\n");
        exit(1);
    }
}
