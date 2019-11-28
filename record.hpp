#ifndef __RECORD_HPP__
#define __RECORD_HPP__

#include <string>
#include <atomic>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;

// filter
struct AVFilterGraph;
struct AVFilterContext;

// sdl

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

class noncopyable
{
protected:
    noncopyable() {}
    ~noncopyable() {}

private:
    noncopyable(const noncopyable &);
    const noncopyable &operator=(const noncopyable &);
};

class RecordScreen : noncopyable
{
public:
    explicit RecordScreen(const std::string &url);
    ~RecordScreen();
    void InitEnv();
    void Run();
    void Stop();
    static int SdlThread(void *arg);

private:
    struct Record
    {
        bool record_;
        AVFrame *screen;
    };

    struct SdlImpl
    {
        SDL_Window *screen;
        SDL_Renderer *sdlRenderer;
        SDL_Texture *sdlTexture;
        int w;  // window width;
        int h;  // window height
        std::atomic<bool> sdl_stop_;
        std::atomic<bool> sdl_refresh_;
    };
    struct FilterImpl
    {
        AVFilterGraph *graph_ = nullptr;
        AVFilterContext *src_ctx_ = nullptr;
        AVFilterContext *sink_ctx_ = nullptr;
        AVFrame *frame_ = nullptr;
        bool invalid;
    };

private:
    void InitAv();
    void InitDecoder();
    void InitializeEncoder();
    void InitConverter();
    void InitFilter();
    void InitSdlEnv();
    void SdlThread();
    void RunSdl();
    int RecordScreenAndDisplay();
    void CleanAV();
    void CleanSdl();
    void CleanFilter();
    void FilterFrame(const Record &);
    Record AvRecordScreen();
    void SdlDisplayRecord();

private:
    AVFormatContext *in_fmt_ctx = nullptr;
    AVCodecContext *in_codec_ctx = nullptr;
    AVFrame *raw_frame_ = nullptr;
    AVPacket *decoding_packet = nullptr;
    int stream_index = -1;
    std::string url_;
    bool runing = false;
    // sdl
    SdlImpl sdl_;
    // filter
    FilterImpl filter_;
    //
};

#endif  // __RECORD_HPP__
