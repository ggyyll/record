#ifndef __RECORD_HPP__
#define __RECORD_HPP__

#include <string>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

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

private:
    void InitAv();
    void InitializeDecoder();
    void InitializeEncoder();
    void InitializeConverter();
    void CleanUp();

private:
    AVFormatContext *in_fmt_ctx = nullptr;
    AVCodecContext *in_codec_ctx = nullptr;
    AVFormatContext *out_fmt_ctx = nullptr;
    AVCodecContext *out_codec_ctx = nullptr;
    AVFrame *raw_frame_ = nullptr;
    AVFrame *converter_frame = nullptr;
    AVPacket *out_pkt = nullptr;
    AVPacket *decoding_packet = nullptr;
    SwsContext *converter_ctx_ = nullptr;
    int stream_index = -1;
    uint8_t *buf = nullptr;
    std::string url_;
    bool runing = false;
};

#endif  // __RECORD_HPP__
