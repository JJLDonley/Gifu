#include "NNWeights.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <vector>

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

// fp16 (IEEE half) to float, without relying on compiler support for _Float16.
static float half_to_float(uint16_t h) {
    uint32_t sign = uint32_t(h & 0x8000) << 16, exp = (h >> 10) & 0x1f, mant = h & 0x3ff, bits;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {   // subnormal: normalise
            exp = 127 - 15 + 1;
            while (!(mant & 0x400)) mant <<= 1, --exp;
            bits = sign | (exp << 23) | ((mant & 0x3ff) << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7f800000 | (mant << 13);
    } else {
        bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    }
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

static float le_float(const unsigned char * p) {
    uint32_t bits = le32(p);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// One array of a LEELAW2 file into `out` (n floats); `skip` reads past it.
// Encodings: 0 float32, 1 float16, 2 quantized: u8 bits, u32 output channels,
// float32 scale per channel, then n signed values of `bits` bits, LSB first.
// A quantized value is q * scale of its output channel (row = i / (n / channels)).
static bool read_array_v2(std::ifstream & in, uint32_t n, float * out, std::string & error) {
    unsigned char enc;
    if (!read_exact(in, &enc, 1)) return false;
    if (enc == 0 || enc == 1) {
        size_t width = enc == 0 ? 4 : 2;
        std::vector<unsigned char> raw(size_t(n) * width);
        if (!read_exact(in, raw.data(), raw.size())) return false;
        if (!out) return true;
        for (uint32_t i = 0; i < n; ++i) {
            out[i] = enc == 0 ? le_float(&raw[i * 4]) : half_to_float(uint16_t(raw[i * 2] | (raw[i * 2 + 1] << 8)));
        }
        return true;
    }
    if (enc != 2) {
        error = "unknown weights encoding " + std::to_string(enc);
        return false;
    }
    unsigned char bits, word[4];
    if (!read_exact(in, &bits, 1) || !read_exact(in, word, 4)) return false;
    uint32_t channels = le32(word);
    if (bits < 2 || bits > 8 || channels == 0 || n % channels != 0) {
        error = "bad quantized array";
        return false;
    }
    size_t size = size_t(channels) * 4 + (size_t(n) * bits + 7) / 8;
    std::vector<unsigned char> raw(size + 1, 0);   // + 1: the reader below may look one byte ahead
    if (!read_exact(in, raw.data(), size)) return false;
    if (!out) return true;
    const unsigned char * data = raw.data() + size_t(channels) * 4;
    uint32_t per_channel = n / channels;
    for (uint32_t i = 0; i < n; ++i) {
        size_t bit = size_t(i) * bits;
        uint32_t v = (data[bit / 8] | (uint32_t(data[bit / 8 + 1]) << 8)) >> (bit % 8);
        int q = int(v & ((1u << bits) - 1));
        if (q & (1 << (bits - 1))) q -= 1 << bits;   // sign
        out[i] = float(q) * le_float(&raw[(i / per_channel) * 4]);
    }
    return true;
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
    if (!read_exact(in, magic, 8) || !read_exact(in, word, 4) ||
        (std::memcmp(magic, "LEELAW1", 8) != 0 && std::memcmp(magic, "LEELAW2", 8) != 0)) {
        error = path + " is not a Leela weights file";
        return false;
    }
    bool v2 = magic[6] == '2';   // per-array encodings: fp16 / quantized (tools/pack_weights.py)
    uint32_t count = le32(word);
    for (uint32_t i = 0; i < count; ++i) {
        unsigned char len[2];
        if (!read_exact(in, len, 2)) break;
        std::string name(len[0] | (len[1] << 8), '\0');
        if (!read_exact(in, &name[0], name.size()) || !read_exact(in, word, 4)) break;
        uint32_t n = le32(word);
        auto it = slots.find(name);
        if (it == slots.end()) {   // the other build's policy net
            if (v2) {
                if (!read_array_v2(in, n, nullptr, error)) break;
            } else {
                in.seekg(static_cast<std::streamoff>(n) * 4, std::ios::cur);
            }
            continue;
        }
        if (n != it->second.size) {
            error = name + " has " + std::to_string(n) + " values, expected " + std::to_string(it->second.size);
            return false;
        }
        if (v2 ? !read_array_v2(in, n, it->second.data, error) : !read_exact(in, it->second.data, size_t(n) * 4)) break;
        it->second.filled = true;
    }
    if (!error.empty()) {
        error = path + ": " + error;
        return false;
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
