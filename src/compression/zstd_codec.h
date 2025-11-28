#pragma once

#include <cstddef>
#include <string>

std::string compress(const char* data, size_t size, int level = 3);

// fails unless the data decompresses to exactly expectedSize bytes
bool decompress(const std::string& data, size_t expectedSize, std::string& out);
