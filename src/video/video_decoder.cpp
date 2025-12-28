#include "video_decoder.h"

#include <stdexcept>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

const int kMaxFrameSize = 4096;

VideoDecoder::VideoDecoder() {
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) throw std::runtime_error("h264 decoder not available");

    context = avcodec_alloc_context3(codec);
    // frame threading would hold frames back, slice threading doesn't
    context->thread_type = FF_THREAD_SLICE;
    context->flags |= AV_CODEC_FLAG_LOW_DELAY;
    if (avcodec_open2(context, codec, nullptr) < 0) {
        avcodec_free_context(&context);
        throw std::runtime_error("could not open h264 decoder");
    }
    frame = av_frame_alloc();
    packet = av_packet_alloc();
}

VideoDecoder::~VideoDecoder() {
    sws_freeContext(scaler);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&context);
}

bool VideoDecoder::decode(const std::string& data, std::vector<uint8_t>& rgba, int& width, int& height) {
    if (data.empty()) return false;
    packet->data = reinterpret_cast<uint8_t*>(const_cast<char*>(data.data()));
    packet->size = static_cast<int>(data.size());
    int sent = avcodec_send_packet(context, packet);
    packet->data = nullptr;
    packet->size = 0;
    if (sent < 0) return false;

    bool gotFrame = false;
    while (avcodec_receive_frame(context, frame) == 0) {
        if (frame->width <= 0 || frame->height <= 0 || frame->width > kMaxFrameSize || frame->height > kMaxFrameSize) {
            av_frame_unref(frame);
            return false;
        }
        width = frame->width;
        height = frame->height;
        scaler = sws_getCachedContext(scaler, width, height, static_cast<AVPixelFormat>(frame->format), width, height,
                                      AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        rgba.resize(size_t(width) * size_t(height) * 4);
        uint8_t* dst[1] = {rgba.data()};
        int dstStride[1] = {width * 4};
        sws_scale(scaler, frame->data, frame->linesize, 0, height, dst, dstStride);
        av_frame_unref(frame);
        gotFrame = true;
    }
    return gotFrame;
}
