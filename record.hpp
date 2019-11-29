#ifndef __RECORD_HPP__
#define __RECORD_HPP__

#include <string>
#include <atomic>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

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
    explicit RecordScreen(const std::string &url, const std::string &filename);
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

private:
    void InitAv();
    void InitializeDecoder();
    void InitializeEncoder();
    void InitializeConverter();
    void InitSdlEnv();
    void SdlThread();
    void RunSdl();
    int RecordScreenAndDisplay();
    void CleanAV();
    void CleanSdl();
    Record AvRecordScreen();
    void SdlDisplayRecord(const Record &);

private:
    AVFormatContext *in_fmt_ctx = nullptr;
    AVCodecContext *in_codec_ctx = nullptr;
    AVFrame *raw_frame_ = nullptr;
    AVFrame *converter_frame = nullptr;
    AVPacket *decoding_packet = nullptr;
    SwsContext *converter_ctx_ = nullptr;
    int stream_index = -1;
    uint8_t *buf = nullptr;
    std::string url_;
    std::string filename_;
    bool runing = false;
    // sdl
    SdlImpl sdl_;
    //
};

#endif  // __RECORD_HPP__
