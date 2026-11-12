// NNWeights.h - the network weights, read at startup from a weights file (they were
// compiled in as NN.cpp / NN128.cpp / NNValue.cpp; see tools/convert_weights.py).
#ifndef NN_WEIGHTS_H_INCLUDED
#define NN_WEIGHTS_H_INCLUDED

#include "config.h"
#include <array>
#include <string>

// The policy net: 192 filters for the OpenCL build ("conv*" in the file), the smaller
// 128-filter net for the CPU build ("cpu_conv*" in the file).
#ifdef USE_OPENCL
#define LEELA_POLICY_WEIGHTS(X) \
    X(conv1_w, 102400) \
    X(conv1_b, 128) \
    X(conv2_w, 221184) \
    X(conv2_b, 192) \
    X(conv3_w, 331776) \
    X(conv3_b, 192) \
    X(conv4_w, 331776) \
    X(conv4_b, 192) \
    X(conv5_w, 331776) \
    X(conv5_b, 192) \
    X(conv6_w, 331776) \
    X(conv6_b, 192) \
    X(conv7_w, 331776) \
    X(conv7_b, 192) \
    X(conv8_w, 331776) \
    X(conv8_b, 192) \
    X(conv9_w, 331776) \
    X(conv9_b, 192) \
    X(conv10_w, 331776) \
    X(conv10_b, 192) \
    X(conv11_w, 331776) \
    X(conv11_b, 192) \
    X(conv12_w, 331776) \
    X(conv12_b, 192) \
    X(conv13_w, 1728) \
    X(conv13_b, 1)
#define LEELA_POLICY_PREFIX ""
#else
#define LEELA_POLICY_WEIGHTS(X) \
    X(conv1_w, 76800) \
    X(conv1_b, 96) \
    X(conv2_w, 110592) \
    X(conv2_b, 128) \
    X(conv3_w, 147456) \
    X(conv3_b, 128) \
    X(conv4_w, 147456) \
    X(conv4_b, 128) \
    X(conv5_w, 147456) \
    X(conv5_b, 128) \
    X(conv6_w, 147456) \
    X(conv6_b, 128) \
    X(conv7_w, 147456) \
    X(conv7_b, 128) \
    X(conv8_w, 147456) \
    X(conv8_b, 128) \
    X(conv9_w, 147456) \
    X(conv9_b, 128) \
    X(conv10_w, 147456) \
    X(conv10_b, 128) \
    X(conv11_w, 147456) \
    X(conv11_b, 128) \
    X(conv12_w, 147456) \
    X(conv12_b, 128) \
    X(conv13_w, 1152) \
    X(conv13_b, 1)
#define LEELA_POLICY_PREFIX "cpu_"
#endif

#define LEELA_VALUE_WEIGHTS(X) \
    X(val_conv1_w, 51200) \
    X(val_conv1_b, 64) \
    X(val_conv2_w, 36864) \
    X(val_conv2_b, 64) \
    X(val_conv3_w, 36864) \
    X(val_conv3_b, 64) \
    X(val_conv4_w, 36864) \
    X(val_conv4_b, 64) \
    X(val_conv5_w, 36864) \
    X(val_conv5_b, 64) \
    X(val_conv6_w, 36864) \
    X(val_conv6_b, 64) \
    X(val_conv7_w, 36864) \
    X(val_conv7_b, 64) \
    X(val_conv8_w, 36864) \
    X(val_conv8_b, 64) \
    X(val_conv9_w, 36864) \
    X(val_conv9_b, 64) \
    X(val_conv10_w, 36864) \
    X(val_conv10_b, 64) \
    X(val_conv11_w, 36864) \
    X(val_conv11_b, 64) \
    X(val_conv12_w, 576) \
    X(val_conv12_b, 1) \
    X(val_ip13_w, 92416) \
    X(val_ip13_b, 256) \
    X(val_ip14_w, 256) \
    X(val_ip14_b, 1)

#define LEELA_DECLARE_WEIGHTS(name, size) extern std::array<float, size> name;
LEELA_POLICY_WEIGHTS(LEELA_DECLARE_WEIGHTS)
LEELA_VALUE_WEIGHTS(LEELA_DECLARE_WEIGHTS)
#undef LEELA_DECLARE_WEIGHTS

namespace Weights {
    // Fills every array from `path`; false with a reason when the file is missing,
    // malformed or lacks an array of the right size.
    bool load(const std::string & path, std::string & error);
    // The default file: leela-0.11.weights next to the program (argv[0]).
    std::string default_path(const std::string & argv0);
}

#endif
