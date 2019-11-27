#include "record.hpp"
#include "head.hpp"
#include "scoped_exit.hpp"

#define WIDTH 1920
#define HEIGHT 1080
#define LINX_X11 "x11grab"
#define SFM_REFRESH_EVENT (SDL_USEREVENT + 1)
#define SFM_BREAK_EVENT (SDL_USEREVENT + 2)

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

RecordScreen::RecordScreen(const std::string &url)
    : url_ {url}
    , runing {false} {};

RecordScreen::~RecordScreen()
{
    assert(runing == false);
    CleanAV();
    CleanSdl();
    fprintf(stderr, "RecordScreen Deconstructor\n");
}

void RecordScreen::InitEnv()
{
    InitAv();
    InitializeDecoder();
    InitializeConverter();
    InitSdlEnv();
}

void RecordScreen::Run()
{
    // sdl
    RunSdl();
    //
    runing = true;

    SDL_Event event;
    while (runing)
    {
        SDL_WaitEvent(&event);
        if (event.type == SFM_REFRESH_EVENT)
        {
            if (RecordScreenAndDisplay() != 0)
            {
                break;
            }
        }
        else if (event.type == SDL_KEYDOWN)
        {
            if (event.key.keysym.sym == SDLK_SPACE)
            {
                sdl_.sdl_refresh_.store(!sdl_.sdl_refresh_.load());
            }
        }
        else if (event.type == SDL_QUIT)
        {
            break;
        }
        else if (event.type == SFM_BREAK_EVENT)
        {
            break;
        }
    }
    // flush codec ?
    sdl_.sdl_stop_.store(true);
    runing = false;
}

int RecordScreen::RecordScreenAndDisplay()
{

#define RECORD_ERROR -1
#define RECORD_EAGAIN 0
#define RECORD_SUCCESS 0

    Record r = AvRecordScreen();
    if (!r.record_)
    {
        return RECORD_ERROR;
    }
    assert(r.record_);
    if (!r.screen)
    {
        return RECORD_EAGAIN;
    }
    assert(r.screen);
    //
    SdlDisplayRecord(r);
    return RECORD_SUCCESS;
}

void RecordScreen::SdlDisplayRecord(const Record &record)
{
    SDL_Rect sdlRect;
    sdlRect.x = 0;
    sdlRect.y = 0;
    sdlRect.w = sdl_.w;
    sdlRect.h = sdl_.h;

    auto record_frame = record.screen;
    SDL_UpdateYUVTexture(sdl_.sdlTexture,
                         &sdlRect,
                         record_frame->data[0],
                         record_frame->linesize[0],
                         record_frame->data[1],
                         record_frame->linesize[1],
                         record_frame->data[2],
                         record_frame->linesize[2]);

    SDL_RenderClear(sdl_.sdlRenderer);
    SDL_RenderCopy(sdl_.sdlRenderer, sdl_.sdlTexture, NULL, &sdlRect);
    SDL_RenderPresent(sdl_.sdlRenderer);
}

RecordScreen::Record RecordScreen::AvRecordScreen()
{
    if (av_read_frame(in_fmt_ctx, decoding_packet) < 0)
    {
        return {false, NULL};
    }
    auto dec_pkt = make_scoped_exit([&pkt = decoding_packet]() { av_packet_unref(pkt); });

    if (decoding_packet->stream_index != stream_index)
    {
        return {true, NULL};
    }
    if (DecodePacketToFrame(in_codec_ctx, raw_frame_, decoding_packet) < 0)
    {
        return {true, NULL};
    }
    auto raw_fm = make_scoped_exit([&f = raw_frame_]() { av_frame_unref(f); });
    ConverterYuvFrame(converter_ctx_, converter_frame, raw_frame_);
    return {true, converter_frame};
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

void RecordScreen::InitializeConverter()
{
    assert(in_codec_ctx);
    int in_h = in_codec_ctx->height;
    int in_w = in_codec_ctx->width;
    converter_frame = av_frame_alloc();
    converter_frame->height = in_h;
    converter_frame->width = in_w;
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
        in_w, in_h, in_pix_fmt, in_w, in_h, pix_fmt, SWS_BICUBIC, NULL, NULL, NULL);
}

void RecordScreen::CleanAV()
{
    fprintf(stderr, "~~~~~ CleanAV begin ~~~~~\n");
    av_free(buf);
    av_frame_free(&converter_frame);
    sws_freeContext(converter_ctx_);

    av_frame_free(&raw_frame_);
    av_packet_free(&decoding_packet);
    avformat_close_input(&in_fmt_ctx);
    avformat_free_context(in_fmt_ctx);
    avcodec_free_context(&in_codec_ctx);

    fprintf(stderr, "~~~~~ CleanAV end ~~~~~\n");
}

void RecordScreen::Stop() { runing = false; }

void RecordScreen::InitSdlEnv()
{
    int w = in_codec_ctx->width;
    int h = in_codec_ctx->height;
    const char *title = "screen player";
    int x = SDL_WINDOWPOS_UNDEFINED;
    int y = SDL_WINDOWPOS_UNDEFINED;
    auto screen = SDL_CreateWindow(title, x, y, w, h, SDL_WINDOW_OPENGL);
    assert(screen);
    auto renderer = SDL_CreateRenderer(screen, -1, 0);
    assert(renderer);
    uint32_t format = SDL_PIXELFORMAT_IYUV;
    auto texture = SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_STREAMING, w, h);
    assert(texture);
    sdl_.screen = screen;
    sdl_.sdlRenderer = renderer;
    sdl_.sdlTexture = texture;
    sdl_.w = w;
    sdl_.h = h;
}

void RecordScreen::CleanSdl()
{
    fprintf(stderr, "~~~~~ CleanSdl begin ~~~~~\n");
    SDL_DestroyTexture(sdl_.sdlTexture);
    SDL_DestroyRenderer(sdl_.sdlRenderer);
    SDL_DestroyWindow(sdl_.screen);

    fprintf(stderr, "~~~~~ CleanSdl end ~~~~~\n");
}

void RecordScreen::RunSdl() { SDL_CreateThread(SdlThread, "sdl", this); }

int RecordScreen::SdlThread(void *arg)
{
    RecordScreen *that = static_cast<RecordScreen *>(arg);
    assert(that);
    that->SdlThread();
    return 0;
}

void RecordScreen::SdlThread()
{
    sdl_.sdl_stop_.store(false);
    sdl_.sdl_refresh_.store(true);
    SDL_Event event;
    event.type = SFM_REFRESH_EVENT;
    while (!sdl_.sdl_stop_.load())
    {
        if (sdl_.sdl_refresh_.load())
        {
            SDL_PushEvent(&event);
        }
        SDL_Delay(25);
    }
    sdl_.sdl_stop_.store(false);
    sdl_.sdl_refresh_.store(false);

    {
        SDL_Event event;
        event.type = SFM_BREAK_EVENT;
        SDL_PushEvent(&event);
    }
}
