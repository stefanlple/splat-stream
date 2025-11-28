#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "messages.pb.h"
#include "scene/gaussian_scene.h"

const uint32_t kGaussiansPerChunk = 4096;
const uint64_t kMaxStreamGaussians = 16 * 1024 * 1024;
const size_t kMaxChunkRawBytes = 2 * 1024 * 1024;

size_t floatsPerGaussian(int shDegree);

// a scene split into encoded tcp frames, ready to send to any number of clients
struct PreparedScene {
    std::shared_ptr<const std::string> info;
    std::vector<std::shared_ptr<const std::string>> chunks;
    size_t rawBytes = 0;
    size_t compressedBytes = 0;
};

std::shared_ptr<const PreparedScene> prepareScene(const GaussianScene& scene,
                                                  uint32_t gaussiansPerChunk = kGaussiansPerChunk);

// rebuilds a scene from SceneInfo + SceneChunk messages.
// every function returns false if the message is invalid.
class SceneReceiver {
public:
    bool start(const splatstream::SceneInfo& info);
    bool addChunk(const splatstream::SceneChunk& chunk);

    bool started() const { return isStarted; }
    bool complete() const { return isStarted && chunksReceived == received.size(); }
    size_t chunkCount() const { return received.size(); }
    size_t chunksDone() const { return chunksReceived; }
    size_t compressedBytes() const { return compressedTotal; }

    GaussianScene scene;

private:
    bool isStarted = false;
    uint32_t perChunk = 0;
    std::vector<bool> received;
    size_t chunksReceived = 0;
    size_t compressedTotal = 0;
    std::string buffer;
};
