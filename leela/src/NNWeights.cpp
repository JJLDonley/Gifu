#include "NNWeights.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <unordered_map>

#define LEELA_DEFINE_WEIGHTS(name, size) std::array<float, size> name;
LEELA_POLICY_WEIGHTS(LEELA_DEFINE_WEIGHTS)
LEELA_VALUE_WEIGHTS(LEELA_DEFINE_WEIGHTS)
#undef LEELA_DEFINE_WEIGHTS

namespace {
struct Slot {
    float * data;
    size_t size;
    bool filled;
};

bool read_exact(std::ifstream & in, void * out, size_t n) {
    in.read(static_cast<char *>(out), static_cast<std::streamsize>(n));
    return static_cast<size_t>(in.gcount()) == n;
}

// The file is little endian; so is every platform Leela builds for.
uint32_t le32(const unsigned char * p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
}

bool Weights::load(const std::string & path, std::string & error) {
    std::unordered_map<std::string, Slot> slots;
#define LEELA_SLOT(name, size) slots[LEELA_POLICY_PREFIX #name] = Slot{ name.data(), size, false };
    LEELA_POLICY_WEIGHTS(LEELA_SLOT)
#undef LEELA_SLOT
#define LEELA_SLOT(name, size) slots[#name] = Slot{ name.data(), size, false };
    LEELA_VALUE_WEIGHTS(LEELA_SLOT)
#undef LEELA_SLOT

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open the weights file " + path;
        return false;
    }
    char magic[8];
    unsigned char word[4];
    if (!read_exact(in, magic, 8) || std::memcmp(magic, "LEELAW1", 8) != 0 || !read_exact(in, word, 4)) {
        error = path + " is not a Leela weights file";
        return false;
    }
    uint32_t count = le32(word);
    for (uint32_t i = 0; i < count; ++i) {
        unsigned char len[2];
        if (!read_exact(in, len, 2)) break;
        std::string name(len[0] | (len[1] << 8), '\0');
        if (!read_exact(in, &name[0], name.size()) || !read_exact(in, word, 4)) break;
        uint32_t n = le32(word);
        auto it = slots.find(name);
        if (it == slots.end()) {   // the other build's policy net
            in.seekg(static_cast<std::streamoff>(n) * 4, std::ios::cur);
            continue;
        }
        if (n != it->second.size) {
            error = name + " has " + std::to_string(n) + " values, expected " + std::to_string(it->second.size);
            return false;
        }
        if (!read_exact(in, it->second.data, size_t(n) * 4)) break;
        it->second.filled = true;
    }
    for (const auto & s : slots) {
        if (!s.second.filled) {
            error = path + " is truncated or lacks " + s.first;
            return false;
        }
    }
    return true;
}

std::string Weights::default_path(const std::string & argv0) {
    size_t slash = argv0.find_last_of("/\\");
    std::string dir = slash == std::string::npos ? std::string() : argv0.substr(0, slash + 1);
    return dir + "leela-0.11.weights";
}
