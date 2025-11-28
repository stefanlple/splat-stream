#include "zstd_codec.h"

#include <stdexcept>
#include <zstd.h>

std::string compress(const char* data, size_t size, int level) {
    std::string out(ZSTD_compressBound(size), '\0');
    size_t written = ZSTD_compress(out.data(), out.size(), data, size, level);
    if (ZSTD_isError(written)) {
        throw std::runtime_error(std::string("zstd compress failed: ") + ZSTD_getErrorName(written));
    }
    out.resize(written);
    return out;
}

bool decompress(const std::string& data, size_t expectedSize, std::string& out) {
    // check the size written in the frame header before allocating anything
    unsigned long long frameSize = ZSTD_getFrameContentSize(data.data(), data.size());
    if (frameSize != expectedSize) return false;

    out.resize(expectedSize);
    size_t written = ZSTD_decompress(out.data(), out.size(), data.data(), data.size());
    return !ZSTD_isError(written) && written == expectedSize;
}
