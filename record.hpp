#ifndef __RECORD_HPP__
#define __RECORD_HPP__

#include <string>
#include <atomic>
#include <deque>
#include "mutex.hpp"

struct AVFormatContext;
struct AVCodecContext;
struct SwsContext;
struct AVFrame;
struct AVPacket;

struct AVFilterGraph;
struct AVFilterContext;
using AVFramePtr = AVFrame *;
using AVPacketPtr = AVPacket *;

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
private:
    void InitAv();
    void InitializeDecoder();
    void CleanAV();
    void InitFilter();
    void CleanFilter();
    void DemuxThread();
    void DecodeVideo();
    void FilterThread();
    void EncodeThread();
    //  demuxing
    struct FilterImpl
    {
        AVFilterGraph *graph_ = nullptr;
        AVFilterContext *src_ctx_ = nullptr;
        AVFilterContext *sink_ctx_ = nullptr;
    };

private:
    AVFormatContext *in_fmt_ctx = nullptr;
    AVCodecContext *in_codec_ctx = nullptr;
    SwsContext *converter_ctx_ = nullptr;
    int stream_index = -1;
    std::string url_;
    std::atomic<bool> runing_;
    FilterImpl filter_;
    // TODO thread safe
    Mutex packet_mutex_;
    CondVar packet_cond_ GUARDED_BY(packet_mutex_);
    std::deque<AVPacketPtr> packet_deque_ GUARDED_BY(packet_mutex_);

    Mutex frame_mutex_;
    CondVar frame_cond_ GUARDED_BY(frame_mutex_);
    std::deque<AVFramePtr> frame_deque_ GUARDED_BY(frame_mutex_);

    Mutex filter_mutex_;
    CondVar filter_cond_ GUARDED_BY(filter_mutex_);
    std::deque<AVFramePtr> filter_deque_ GUARDED_BY(filter_mutex_);
};

#endif  // __RECORD_HPP__
