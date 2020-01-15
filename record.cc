#include "record.hpp"
#include "av_head.hpp"
#include "scoped_exit.hpp"
#include <thread>

#define WIDTH 1920
#define HEIGHT 1080
#define LINX_X11 "x11grab"
#define SFM_REFRESH_EVENT (SDL_USEREVENT + 1)
#define SFM_BREAK_EVENT (SDL_USEREVENT + 2)

static void WriteYuvFrameToFile(AVFrame *frame, FILE *fp)
{
    assert(frame);
    assert(fp);
    printf("w %d h %d\n", frame->width, frame->height);
    int y_size = frame->width * frame->height;
    fwrite(frame->data[0], 1, y_size, fp);      // Y
    fwrite(frame->data[1], 1, y_size / 4, fp);  // U
    fwrite(frame->data[2], 1, y_size / 4, fp);  // V
}

static void ConverterYuvFrame(SwsContext *sws_ctx, AVFrame *dst, AVFrame *src)
{
    av_frame_make_writable(dst);
    auto h = src->height;
    auto ptr = reinterpret_cast<const uint8_t *const *>(src->data);
    auto data_ptr = dst->data;
    sws_scale(sws_ctx, ptr, src->linesize, 0, h, data_ptr, dst->linesize);
    av_frame_copy_props(dst, src);
}

RecordScreen::RecordScreen(const std::string &url)
    : url_ {url}
    , packet_cond_(&packet_mutex_)
    , frame_cond_(&frame_mutex_)
    , filter_cond_(&filter_mutex_) {};

RecordScreen::~RecordScreen()
{
    assert(runing_.load() == false);
    CleanAV();
    CleanFilter();
    fprintf(stderr, "RecordScreen Deconstructor\n");
}

void RecordScreen::InitEnv()
{
    InitAv();
    InitializeDecoder();
    InitFilter();
}

void RecordScreen::DecodeVideo()
{
    while (true)
    {
        AVPacketPtr pkt = nullptr;
        {
            MutexLock lock(&packet_mutex_);
            packet_cond_.Wait();
            if (runing_.load() == false)
            {
                break;
            }
            pkt = packet_deque_.front();
            packet_deque_.pop_front();
        }
        assert(pkt);
        auto pkt_exit = make_scoped_exit([&]() { av_packet_free(&pkt); });
        int status = avcodec_send_packet(in_codec_ctx, pkt);
        if (status < 0)
        {
            continue;
        }
        AVFramePtr frame = av_frame_alloc();
        status = avcodec_receive_frame(in_codec_ctx, frame);
        if (status < 0)
        {
            av_frame_free(&frame);
            continue;
        }
        {
            MutexLock lock(&frame_mutex_);
            frame_deque_.push_back(frame);
            frame_cond_.Signal();
        }
    }
    MutexLock lock(&frame_mutex_);
    frame_cond_.SignalAll();
}

void RecordScreen::FilterThread()
{
    while (true)
    {
        AVFramePtr frame = nullptr;
        {
            MutexLock lock(&frame_mutex_);
            frame_cond_.Wait();
            if (runing_.load() == false)
            {
                break;
            }
            frame = frame_deque_.front();
            frame_deque_.pop_front();
        }

        assert(frame);
        auto frame_exit = make_scoped_exit([&]() { av_frame_free(&frame); });

        int ref = AV_BUFFERSRC_FLAG_KEEP_REF;
        int status = av_buffersrc_add_frame_flags(filter_.src_ctx_, frame, ref);
        if (status < 0)
        {
            break;
        }
        while (true)
        {
            AVFramePtr filter_frame = av_frame_alloc();
            int status = av_buffersink_get_frame(filter_.sink_ctx_, filter_frame);
            if (status < 0)
            {
                av_frame_free(&filter_frame);
                break;
            }
            {
                MutexLock lock(&filter_mutex_);
                filter_deque_.push_back(filter_frame);
                filter_cond_.Signal();
            }
        }
    }
    MutexLock lock(&filter_mutex_);
    filter_cond_.SignalAll();
}

void RecordScreen::EncodeThread()
{
    FILE *fp = fopen("record.yuv", "wb");
    auto file_close = make_scoped_exit([&]() { fclose(fp); });

    while (true)
    {
        AVFramePtr filter_frame = nullptr;
        {
            MutexLock lock(&filter_mutex_);
            filter_cond_.Wait();
            if (runing_.load() == false)
            {
                break;
            }
            filter_frame = filter_deque_.front();
            filter_deque_.pop_front();
        }
        assert(filter_frame);
        auto frame_exit = make_scoped_exit([&]() { av_frame_free(&filter_frame); });
        WriteYuvFrameToFile(filter_frame, fp);
    }
}

void RecordScreen::DemuxThread()
{
    while (runing_.load())
    {
        AVPacket *pkt = av_packet_alloc();
        auto dec_pkt = make_scoped_exit([&]() { av_packet_free(&pkt); });
        if (av_read_frame(in_fmt_ctx, pkt) < 0)
        {
            break;
        }
        if (pkt->stream_index != stream_index)
        {
            continue;
        }
        auto clone = av_packet_clone(pkt);
        {
            MutexLock lock(&packet_mutex_);
            packet_deque_.push_back(clone);
            packet_cond_.Signal();
        }
    }
    runing_.store(false);
    MutexLock lock(&packet_mutex_);
    packet_cond_.SignalAll();
}
static void thread_join(std::thread *t)
{
    if (t->joinable())
    {
        t->join();
    }
}
void RecordScreen::Run()
{
    runing_.store(true);
    std::thread demuxer([&]() { DemuxThread(); });
    std::thread decoder([&]() { DecodeVideo(); });
    std::thread filter([&]() { FilterThread(); });
    std::thread encoder([&]() { EncodeThread(); });
    // do something ?
    thread_join(&demuxer);
    thread_join(&decoder);
    thread_join(&filter);
    thread_join(&encoder);
    assert(runing_.load() == false);
}

void RecordScreen::InitAv() { avdevice_register_all(); }

void RecordScreen::InitializeDecoder()
{
    AVInputFormat *input_fmt = av_find_input_format(LINX_X11);
    in_fmt_ctx = avformat_alloc_context();

    AVDictionary *options = nullptr;
    av_dict_set(&options, "video_size", "1920*1080", 0);
    av_dict_set(&options, "pixel_format", av_get_pix_fmt_name(AV_PIX_FMT_YUV420P), 0);
    av_dict_set(&options, "framerate", "25", 0);
    auto opt_exit = make_scoped_exit([&op = options]() { av_dict_free(&op); });

    int status = avformat_open_input(&in_fmt_ctx, url_.data(), input_fmt, &options);
    assert(status == 0);

    status = avformat_find_stream_info(in_fmt_ctx, nullptr);
    assert(status >= 0);

    av_dump_format(in_fmt_ctx, 0, url_.data(), 0);

    AVCodec *in_codec = nullptr;
    stream_index = av_find_best_stream(in_fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &in_codec, 0);
    assert(stream_index >= 0);
    assert(in_codec);
    AVCodecParameters *cpr = in_fmt_ctx->streams[stream_index]->codecpar;
    in_codec_ctx = avcodec_alloc_context3(in_codec);
    assert(in_codec_ctx);
    status = avcodec_parameters_to_context(in_codec_ctx, cpr);
    assert(status >= 0);
    status = avcodec_open2(in_codec_ctx, in_codec, NULL);
    assert(status == 0);
}

void RecordScreen::CleanAV()
{
    fprintf(stderr, "~~~~~ CleanAV begin ~~~~~\n");
    avformat_close_input(&in_fmt_ctx);
    avformat_free_context(in_fmt_ctx);
    avcodec_free_context(&in_codec_ctx);
    fprintf(stderr, "~~~~~ CleanAV end ~~~~~\n");
}

void RecordScreen::InitFilter()
{

    AVFilterGraph *graph = nullptr;
    AVFilterContext *src_ctx = nullptr;
    AVFilterContext *sink_ctx = nullptr;

    AVFrame *frame = av_frame_alloc();
    graph = avfilter_graph_alloc();
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();

    assert(frame && graph && inputs && outputs);

    const AVFilter *src = avfilter_get_by_name("buffer");
    const AVFilter *sink = avfilter_get_by_name("buffersink");

    // see https://www.ffmpeg.org/ffmpeg-all.html#Commands-37
    // https://www.ffmpeg.org/ffmpeg-all.html#drawtext-1
    const char *filter_descr
        = "[in]fps=fps=15/"
          "1[fps_out];[fps_out]drawtext=fontfile=UbuntuMonoforPowerlineNerdFontCompleteMono.ttf:x="
          "500:y=10:fontsize=100:fontcolor=green:text='Hello "
          "World'[out_t];movie=ubuntu.png[ubuntu];[out_t][ubuntu]overlay=10:10[record_in];[record_"
          "in]drawbox=100:200:200:60:red@0.5:t=1[scale_in];[scale_in]scale=in_w/2:in_h/2";

    int status = avfilter_graph_parse2(graph, filter_descr, &inputs, &outputs);
    assert(status >= 0);

    auto pix_fmt = in_codec_ctx->pix_fmt;
    int w = in_codec_ctx->width;
    int h = in_codec_ctx->height;

    AVStream *s = in_fmt_ctx->streams[stream_index];
    int tn = s->time_base.num;
    int td = s->time_base.den;
    int sn = s->sample_aspect_ratio.num;
    int sd = s->sample_aspect_ratio.den;
    int rn = s->r_frame_rate.num;
    int rd = s->r_frame_rate.den;

    char args[255] = {0};
    const char *fmt = "width=%d:height=%d:pix_fmt=%d:time_base=%d/%d:sar=%d/%d:frame_rate=%d/%d";
    snprintf(args, sizeof(args) - 1, fmt, w, h, pix_fmt, tn, td, sn, sd, rn, rd);
    printf("%s\n", args);

    // create buffer source with the specified params
    int i = 0;
    AVFilterInOut *cur = inputs;
    for (; cur; cur = cur->next, ++i)
    {
        AVBufferSrcParameters *params = av_buffersrc_parameters_alloc();
        memset(params, 0, sizeof(*params));
        params->format = AV_PIX_FMT_NONE;

        char filter_name[255] = {0};
        snprintf(filter_name, sizeof(filter_name) - 1, "in_%d_%d", i, AVMEDIA_TYPE_VIDEO);

        int status = avfilter_graph_create_filter(&src_ctx, src, filter_name, args, NULL, graph);
        assert(status >= 0);
        status = av_buffersrc_parameters_set(src_ctx, params);
        assert(status >= 0);
        av_freep(&params);
        status = avfilter_link(src_ctx, 0, cur->filter_ctx, cur->pad_idx);
        assert(status >= 0);
    }

    for (cur = outputs, i = 0; cur; cur = cur->next, ++i)
    {
        char fmt[256] = {0};
        snprintf(fmt, sizeof(fmt) - 1, "out_%d_%d", i, AVMEDIA_TYPE_VIDEO);

        status = avfilter_graph_create_filter(&sink_ctx, sink, fmt, NULL, NULL, graph);
        assert(status >= 0);

        status = avfilter_link(cur->filter_ctx, cur->pad_idx, sink_ctx, 0);
        assert(status >= 0);
    }

    status = avfilter_graph_config(graph, nullptr);
    assert(status >= 0);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    filter_.graph_ = graph;
    filter_.sink_ctx_ = sink_ctx;
    filter_.src_ctx_ = src_ctx;
}

void RecordScreen::CleanFilter() { avfilter_graph_free(&filter_.graph_); }

void RecordScreen::Stop() { runing_.store(false); }
