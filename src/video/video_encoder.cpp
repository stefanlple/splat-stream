#include "video_encoder.h"

#include <stdexcept>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

VideoEncoder::VideoEncoder(int width, int height, int fps, int bitrateKbps) : width(width), height(height) {
    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) throw std::runtime_error("libx264 encoder not available");

    context = avcodec_alloc_context3(codec);
    context->width = width;
    context->height = height;
    context->time_base = {1, fps};
    context->framerate = {fps, 1};
    context->pix_fmt = AV_PIX_FMT_YUV420P;
    context->bit_rate = int64_t(bitrateKbps) * 1000;
    context->gop_size = fps * 2;
    context->max_b_frames = 0;
    av_opt_set(context->priv_data, "preset", "ultrafast", 0);
    av_opt_set(context->priv_data, "tune", "zerolatency", 0);

    if (avcodec_open2(context, codec, nullptr) < 0) {
        avcodec_free_context(&context);
        throw std::runtime_error("could not open h264 encoder");
    }

    frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = width;
    frame->height = height;
    av_frame_get_buffer(frame, 0);
    packet = av_packet_alloc();

    scaler = sws_getContext(width, height, AV_PIX_FMT_BGRA, width, height, AV_PIX_FMT_YUV420P,
                            SWS_BILINEAR, nullptr, nullptr, nullptr);
}

VideoEncoder::~VideoEncoder() {
    sws_freeContext(scaler);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&context);
}

std::string VideoEncoder::encode(const uint8_t* bgra) {
    av_frame_make_writable(frame);
    const uint8_t* src[1] = {bgra};
    int srcStride[1] = {width * 4};
    sws_scale(scaler, src, srcStride, 0, height, frame->data, frame->linesize);
    frame->pts = frameIndex++;

    if (avcodec_send_frame(context, frame) < 0) throw std::runtime_error("encoding failed");

    std::string out;
    while (avcodec_receive_packet(context, packet) == 0) {
        out.append(reinterpret_cast<const char*>(packet->data), static_cast<size_t>(packet->size));
        av_packet_unref(packet);
    }
    return out;
}
