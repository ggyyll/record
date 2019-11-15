#include "record.h"

template <typename Callback>
static scoped_exit<Callback> make_scoped_exit(Callback&& c)
{
    return scoped_exit<Callback>(std::forward<Callback>(c));
}


bool runing = true;

void handler(int signum)
{
    runing = false;
    printf("record desktop over\n");
}


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
