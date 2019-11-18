#ifndef __RECORD_H__
#define __RECORD_H__
#include <string>
#include <vector>
#include <atomic>

struct AVDictionary;
struct AVFormatContext;
struct AVInputFormat;
struct AVOutputFormat;
struct AVCodecContext;
struct AVCodec;
struct SwsContext;
struct AVFrame;
struct AVStream;
class Record
{
public:
    Record(const std::string& filename);
    ~Record();
    void Run();
    void Stop();
    void InitEnv();

private:
    void InitInput();
    void InitOutput();
    void DecodeAndEncode();

private:
    //
    AVFormatContext* input_fmt_ctx_;
    AVInputFormat* input_fmt_;
    AVCodecContext* decoder_codec_ctx_;
    //
    AVFormatContext* output_fmt_ctx_;
    AVOutputFormat* output_fmt_;
    AVCodec* output_codec_;
    AVCodecContext* encoder_codec_ctx_;
    AVStream* output_stream_;
    //
    struct SwsContext* sws_ctx_;
    AVFrame* yuv_frame_;
    uint8_t* image_buffer_;
    //
    std::string filename_;
    std::string out_name_;
    std::vector<uint8_t> stream_indexs_;
    std::atomic<bool> runing_;
};

#endif
