#include "scene_stream.h"

#include <cstring>

#include "compression/zstd_codec.h"
#include "framing.h"

namespace {

struct Array {
    std::vector<float>* data;
    size_t width;
};

std::vector<Array> arraysOf(GaussianScene& scene) {
    size_t rest = floatsPerGaussian(scene.shDegree) - 14;
    return {{&scene.positions, 3}, {&scene.scales, 3}, {&scene.rotations, 4},
            {&scene.opacities, 1}, {&scene.shDC, 3},  {&scene.shRest, rest}};
}

std::shared_ptr<const std::string> frame(const splatstream::Message& message) {
    return std::make_shared<const std::string>(encodeMessage(message));
}

}

size_t floatsPerGaussian(int shDegree) {
    size_t coeffs = static_cast<size_t>((shDegree + 1) * (shDegree + 1));
    return 3 + 3 + 4 + 1 + 3 + 3 * (coeffs - 1);
}

std::shared_ptr<const PreparedScene> prepareScene(const GaussianScene& scene, uint32_t gaussiansPerChunk) {
    auto prepared = std::make_shared<PreparedScene>();
    uint64_t count = scene.count();
    uint32_t chunkCount = static_cast<uint32_t>((count + gaussiansPerChunk - 1) / gaussiansPerChunk);

    splatstream::Message info;
    info.mutable_scene_info()->set_gaussian_count(count);
    info.mutable_scene_info()->set_sh_degree(static_cast<uint32_t>(scene.shDegree));
    info.mutable_scene_info()->set_gaussians_per_chunk(gaussiansPerChunk);
    info.mutable_scene_info()->set_chunk_count(chunkCount);
    prepared->info = frame(info);

    size_t rest = floatsPerGaussian(scene.shDegree) - 14;
    std::vector<std::pair<const std::vector<float>*, size_t>> arrays = {
        {&scene.positions, 3}, {&scene.scales, 3}, {&scene.rotations, 4},
        {&scene.opacities, 1}, {&scene.shDC, 3},  {&scene.shRest, rest}};

    std::string raw;
    for (uint32_t index = 0; index < chunkCount; index++) {
        size_t first = size_t(index) * gaussiansPerChunk;
        size_t n = std::min<size_t>(gaussiansPerChunk, count - first);

        raw.clear();
        for (auto& [data, width] : arrays) {
            raw.append(reinterpret_cast<const char*>(data->data() + first * width), n * width * sizeof(float));
        }

        splatstream::Message message;
        message.mutable_scene_chunk()->set_index(index);
        message.mutable_scene_chunk()->set_raw_size(static_cast<uint32_t>(raw.size()));
        message.mutable_scene_chunk()->set_data(compress(raw.data(), raw.size()));

        prepared->rawBytes += raw.size();
        prepared->compressedBytes += message.scene_chunk().data().size();
        prepared->chunks.push_back(frame(message));
    }
    return prepared;
}

bool SceneReceiver::start(const splatstream::SceneInfo& info) {
    if (isStarted) return false;
    uint64_t count = info.gaussian_count();
    uint32_t per = info.gaussians_per_chunk();
    if (info.sh_degree() > 3 || count == 0 || count > kMaxStreamGaussians) return false;
    if (per == 0 || per * floatsPerGaussian(static_cast<int>(info.sh_degree())) * sizeof(float) > kMaxChunkRawBytes) return false;
    if (info.chunk_count() != (count + per - 1) / per) return false;

    scene = GaussianScene();
    scene.shDegree = static_cast<int>(info.sh_degree());
    for (Array& a : arraysOf(scene)) a.data->resize(count * a.width);

    perChunk = per;
    received.assign(info.chunk_count(), false);
    isStarted = true;
    return true;
}

bool SceneReceiver::addChunk(const splatstream::SceneChunk& chunk) {
    if (!isStarted || chunk.index() >= received.size() || received[chunk.index()]) return false;

    size_t first = size_t(chunk.index()) * perChunk;
    size_t n = std::min<size_t>(perChunk, scene.count() - first);
    size_t expected = n * floatsPerGaussian(scene.shDegree) * sizeof(float);
    if (chunk.raw_size() != expected) return false;
    if (!decompress(chunk.data(), expected, buffer)) return false;

    const char* p = buffer.data();
    for (Array& a : arraysOf(scene)) {
        size_t bytes = n * a.width * sizeof(float);
        std::memcpy(a.data->data() + first * a.width, p, bytes);
        p += bytes;
    }

    received[chunk.index()] = true;
    chunksReceived++;
    compressedTotal += chunk.data().size();
    return true;
}
