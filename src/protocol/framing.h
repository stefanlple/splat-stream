#pragma once

#include <cstdint>
#include <string>

#include "messages.pb.h"

const uint32_t kProtocolVersion = 1;
const uint32_t kMaxMessageSize = 1024 * 1024;

// 4 byte big endian length followed by the serialized message
std::string encodeMessage(const splatstream::Message& message);

// returns 0 if the length is invalid (zero or too big)
uint32_t decodeLength(const uint8_t header[4]);

bool decodeMessage(const char* data, size_t size, splatstream::Message& out);

bool decodeCamera(const char* data, size_t size, splatstream::CameraUpdate& out);

uint64_t nowMicros();
