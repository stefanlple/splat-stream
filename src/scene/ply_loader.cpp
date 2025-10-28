#include "ply_loader.h"

#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace {

struct Property {
    std::string type;
    size_t offset;
};

size_t typeSize(const std::string& type) {
    if (type == "char" || type == "uchar" || type == "int8" || type == "uint8") return 1;
    if (type == "short" || type == "ushort" || type == "int16" || type == "uint16") return 2;
    if (type == "int" || type == "uint" || type == "int32" || type == "uint32") return 4;
    if (type == "float" || type == "float32") return 4;
    if (type == "double" || type == "float64") return 8;
    throw std::runtime_error("unknown ply property type: " + type);
}

float readFloat(const char* vertex, size_t offset) {
    float value;
    std::memcpy(&value, vertex + offset, sizeof(float));
    return value;
}

}

GaussianScene loadPly(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("could not open " + path);
    }

    std::string line;
    std::getline(file, line);
    if (line != "ply" && line != "ply\r") {
        throw std::runtime_error("not a ply file: " + path);
    }

    size_t vertexCount = 0;
    size_t stride = 0;
    bool foundVertex = false;
    bool readingVertex = false;
    bool foundEnd = false;
    std::map<std::string, Property> properties;

    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::istringstream words(line);
        std::string keyword;
        words >> keyword;

        if (keyword == "format") {
            std::string format;
            words >> format;
            if (format != "binary_little_endian") {
                throw std::runtime_error("only binary_little_endian ply files are supported");
            }
        } else if (keyword == "element") {
            std::string name;
            words >> name;
            if (name == "vertex") {
                if (!(words >> vertexCount)) throw std::runtime_error("bad vertex count");
                foundVertex = true;
                readingVertex = true;
            } else {
                if (!foundVertex) throw std::runtime_error("vertex must be the first element");
                readingVertex = false;
            }
        } else if (keyword == "property" && readingVertex) {
            std::string type, name;
            words >> type >> name;
            if (type == "list") throw std::runtime_error("list properties are not supported");
            if (properties.count(name)) throw std::runtime_error("duplicate property " + name);
            properties[name] = {type, stride};
            stride += typeSize(type);
        } else if (keyword == "end_header") {
            foundEnd = true;
            break;
        }
    }

    if (!foundVertex || !foundEnd || stride == 0) {
        throw std::runtime_error("invalid ply header");
    }

    auto offsetOf = [&](const std::string& name) {
        auto it = properties.find(name);
        if (it == properties.end()) throw std::runtime_error("missing property " + name);
        if (it->second.type != "float" && it->second.type != "float32") {
            throw std::runtime_error("property " + name + " must be float");
        }
        return it->second.offset;
    };

    size_t restCount = 0;
    while (properties.count("f_rest_" + std::to_string(restCount))) restCount++;

    GaussianScene scene;
    if (restCount == 0) scene.shDegree = 0;
    else if (restCount == 9) scene.shDegree = 1;
    else if (restCount == 24) scene.shDegree = 2;
    else if (restCount == 45) scene.shDegree = 3;
    else throw std::runtime_error("unexpected number of f_rest properties: " + std::to_string(restCount));

    // check the size before allocating so a broken header can't make us allocate gigabytes
    auto dataStart = file.tellg();
    file.seekg(0, std::ios::end);
    auto dataSize = static_cast<size_t>(file.tellg() - dataStart);
    file.seekg(dataStart);
    if (vertexCount > dataSize / stride) {
        throw std::runtime_error("ply file is truncated");
    }

    size_t pos[3], scale[3], dc[3], rot[4];
    for (int i = 0; i < 3; i++) {
        pos[i] = offsetOf(std::string(1, "xyz"[i]));
        scale[i] = offsetOf("scale_" + std::to_string(i));
        dc[i] = offsetOf("f_dc_" + std::to_string(i));
    }
    for (int i = 0; i < 4; i++) {
        rot[i] = offsetOf("rot_" + std::to_string(i));
    }
    size_t opacity = offsetOf("opacity");
    std::vector<size_t> rest;
    for (size_t i = 0; i < restCount; i++) {
        rest.push_back(offsetOf("f_rest_" + std::to_string(i)));
    }

    scene.positions.resize(vertexCount * 3);
    scene.scales.resize(vertexCount * 3);
    scene.rotations.resize(vertexCount * 4);
    scene.opacities.resize(vertexCount);
    scene.shDC.resize(vertexCount * 3);
    scene.shRest.resize(vertexCount * restCount);

    std::vector<char> vertex(stride);
    for (size_t v = 0; v < vertexCount; v++) {
        if (!file.read(vertex.data(), static_cast<std::streamsize>(stride))) {
            throw std::runtime_error("ply file is truncated");
        }
        for (int i = 0; i < 3; i++) {
            scene.positions[v * 3 + i] = readFloat(vertex.data(), pos[i]);
            scene.scales[v * 3 + i] = readFloat(vertex.data(), scale[i]);
            scene.shDC[v * 3 + i] = readFloat(vertex.data(), dc[i]);
        }
        for (int i = 0; i < 4; i++) {
            scene.rotations[v * 4 + i] = readFloat(vertex.data(), rot[i]);
        }
        scene.opacities[v] = readFloat(vertex.data(), opacity);
        for (size_t i = 0; i < restCount; i++) {
            scene.shRest[v * restCount + i] = readFloat(vertex.data(), rest[i]);
        }
    }

    return scene;
}
