#pragma once

#include <cstdint>
#include <string>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

// H.264 with libx264, tuned for low latency: every frame that goes in comes out immediately
class VideoEncoder {
public:
    VideoEncoder(int width, int height, int fps, int bitrateKbps);
    ~VideoEncoder();
    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    // bgra is width * height * 4 bytes. returns the encoded frame (annex b)
    std::string encode(const uint8_t* bgra);

private:
    AVCodecContext* context = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* scaler = nullptr;
    int width;
    int height;
    int64_t frameIndex = 0;
};
