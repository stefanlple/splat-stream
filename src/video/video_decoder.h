#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    // returns true if a frame came out. rgba gets width * height * 4 bytes.
    // returns false for broken data instead of throwing
    bool decode(const std::string& data, std::vector<uint8_t>& rgba, int& width, int& height);

private:
    AVCodecContext* context = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* scaler = nullptr;
};
