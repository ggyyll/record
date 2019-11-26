#include "record.hpp"
#include "head.hpp"
#include "scoped_exit.hpp"

#define WIDTH 1920
#define HEIGHT 1080
#define LINX_X11 "x11grab"

static void WriteYuvFrameToFile(AVFrame *frame, FILE *fp)
{
    assert(frame);
    assert(fp);
    int y_size = frame->width * frame->height;
    fwrite(frame->data[0], 1, y_size, fp);      // Y
    fwrite(frame->data[1], 1, y_size / 4, fp);  // U
    fwrite(frame->data[2], 1, y_size / 4, fp);  // V
}

static void WritePacketToFile(AVPacket *pkt, FILE *fp)
{
    assert(pkt);
    assert(fp);
    fwrite(pkt->data, 1, pkt->size, fp);
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

static int DecodePacketToFrame(AVCodecContext *ctx, AVFrame *frame, AVPacket *packet)
{

    int status = avcodec_send_packet(ctx, packet);

    if (status < 0)
    {
        return status;
    }

    status = avcodec_receive_frame(ctx, frame);

    if (status == AVERROR(EAGAIN) || status == AVERROR_EOF)
    {
        return status;
    }

    if (status < 0)
    {
        return status;
    }

    return status;
}

static int EncodeFrameToPacket(AVCodecContext *ctx, AVFrame *frame, AVPacket *packet)
{

    int status = avcodec_send_frame(ctx, frame);
    if (status < 0)
    {
        return status;
    }

    status = avcodec_receive_packet(ctx, packet);

    if (status == AVERROR(EAGAIN) || status == AVERROR_EOF)
    {
        return status;
    }

    if (status < 0)
    {
        return status;
    }

    return status;
}

RecordScreen::RecordScreen(const std::string &url)
    : url_ {url}
    , runing {false} {};

RecordScreen::~RecordScreen()
{
    assert(runing == false);
    CleanUp();
}

void RecordScreen::InitEnv()
{
    InitAv();
    InitializeDecoder();
    InitializeEncoder();
    InitializeConverter();
}
void RecordScreen::Run()
{
    std::string file_name = std::to_string(time(NULL));
    file_name += ".264";
    FILE *fp = fopen(file_name.data(), "wb");
    assert(fp);
    auto close_file = make_scoped_exit([&fp = fp]() { fclose(fp); });
    runing = true;
    auto stop_clean = make_scoped_exit([&run = runing]() { run = false; });
    while (runing)
    {
        if (av_read_frame(in_fmt_ctx, decoding_packet) < 0)
        {
            break;  // log ?
        }
        auto dec_pkt = make_scoped_exit([&pkt = decoding_packet]() { av_packet_unref(pkt); });

        if (decoding_packet->stream_index != stream_index)
        {
            continue;
        }
        if (DecodePacketToFrame(in_codec_ctx, raw_frame_, decoding_packet) < 0)
        {
            continue;
        }
        auto raw_fm = make_scoped_exit([&f = raw_frame_]() { av_frame_unref(f); });
        ConverterYuvFrame(converter_ctx_, converter_frame, raw_frame_);
        if (EncodeFrameToPacket(out_codec_ctx, converter_frame, out_pkt) < 0)
        {
            continue;
        }
        auto pkt = make_scoped_exit([&pkt = out_pkt]() { av_packet_unref(pkt); });
        fwrite(out_pkt->data, 1, out_pkt->size, fp);
    }
    // flush codec ?
    assert(runing == false);
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
    decoding_packet = av_packet_alloc();
    av_init_packet(decoding_packet);
    raw_frame_ = av_frame_alloc();
}
void RecordScreen::InitializeEncoder()
{
    int statCode = avformat_alloc_output_context2(&out_fmt_ctx, nullptr, "null", nullptr);
    assert(statCode >= 0);

    AVCodec *out_codec = avcodec_find_encoder_by_name("libx264");
    assert(out_codec);

    AVStream *stream = avformat_new_stream(out_fmt_ctx, out_codec);
    assert(stream);
    stream->id = out_fmt_ctx->nb_streams - 1;

    out_codec_ctx = avcodec_alloc_context3(out_codec);
    assert(out_codec_ctx);

    out_codec_ctx->width = WIDTH;
    out_codec_ctx->height = HEIGHT;
    out_codec_ctx->bit_rate = 400000;
    out_codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    out_codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;

    out_codec_ctx->time_base.den = 25;
    out_codec_ctx->time_base.num = 1;

    if (out_fmt_ctx->flags & AVFMT_GLOBALHEADER)
    {
        out_codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    avcodec_parameters_from_context(stream->codecpar, out_codec_ctx);
    AVDictionary *options = nullptr;
    out_codec_ctx->me_range = 16;
    out_codec_ctx->max_qdiff = 4;
    out_codec_ctx->qmin = 10;
    out_codec_ctx->qmax = 51;
    out_codec_ctx->qcompress = 0.6;
    out_codec_ctx->refs = 3;
    out_codec_ctx->bit_rate = 500000;

    statCode = avcodec_open2(out_codec_ctx, out_codec, NULL);
    av_dict_free(&options);
    assert(statCode == 0);
    statCode = avformat_write_header(out_fmt_ctx, nullptr);
    assert(statCode >= 0);

    av_dump_format(out_fmt_ctx, stream->index, "null", 1);

    out_pkt = av_packet_alloc();
    av_init_packet(out_pkt);
}
void RecordScreen::InitializeConverter()
{
    assert(in_codec_ctx);
    assert(out_codec_ctx);
    int in_h = in_codec_ctx->height;
    int in_w = in_codec_ctx->width;
    int out_h = out_codec_ctx->height;
    int out_w = out_codec_ctx->width;
    converter_frame = av_frame_alloc();
    converter_frame->height = out_h;
    converter_frame->width = out_w;
    enum AVPixelFormat pix_fmt = AV_PIX_FMT_YUV420P;
    enum AVPixelFormat in_pix_fmt = in_codec_ctx->pix_fmt;
    // libx264 yuv420
    converter_frame->format = pix_fmt;

    int size = av_image_get_buffer_size(pix_fmt, in_w, in_h, 1);
    buf = (uint8_t *)av_malloc(size);
    auto ptr = converter_frame->data;
    auto lines_ptr = converter_frame->linesize;
    av_image_fill_arrays(ptr, lines_ptr, buf, pix_fmt, in_w, in_h, 1);

    converter_ctx_ = sws_getContext(
        in_w, in_h, in_pix_fmt, out_w, out_h, pix_fmt, SWS_BICUBIC, NULL, NULL, NULL);
}

void RecordScreen::CleanUp()
{
    av_free(buf);
    av_frame_free(&converter_frame);
    sws_freeContext(converter_ctx_);

    av_frame_free(&raw_frame_);
    av_packet_free(&decoding_packet);
    avformat_close_input(&in_fmt_ctx);
    avformat_free_context(in_fmt_ctx);
    avcodec_free_context(&in_codec_ctx);

    if (out_fmt_ctx && out_fmt_ctx->oformat && !(out_fmt_ctx->oformat->flags & AVFMT_NOFILE))
    {
        avio_closep(&out_fmt_ctx->pb);
    }
    av_packet_free(&out_pkt);
    avformat_free_context(out_fmt_ctx);
    avcodec_free_context(&out_codec_ctx);
}

void RecordScreen::Stop() { runing = false; }
