#include "config.h"
#include <algorithm>
#include <cassert>
#include <list>
#include <set>
#include <iostream>
#include <fstream>
#include <memory>
#include <cmath>
#include <random>
#include <array>
#include <thread>

#include "Im2Col.h"
#include "Gemm.h"
#ifdef USE_OPENCL
#include "OpenCL.h"
#include "UCTNode.h"
#endif

#include "SGFTree.h"
#include "SGFParser.h"
#include "Utils.h"
#include "FastBoard.h"
#include "Random.h"
#include "Network.h"
#include "GTP.h"
#include "Utils.h"

using namespace Utils;

Network* Network::s_Net = nullptr;

#include "NNWeights.h"

Network * Network::get_Network(void) {
    if (!s_Net) {
        s_Net = new Network();
        s_Net->initialize();
    }
    return s_Net;
}

void Network::benchmark(FastState * state) {
    {
        // Policy
        int BENCH_AMOUNT = 2000;
        int cpus = cfg_num_threads;
        int iters_per_thread = (BENCH_AMOUNT + (cpus - 1)) / cpus;

        Time start;

        ThreadGroup tg(thread_pool);
        for (int i = 0; i < cpus; i++) {
            tg.add_task([iters_per_thread, state]() {
                FastState mystate = *state;
                for (int loop = 0; loop < iters_per_thread; loop++) {
                    auto vec = get_scored_moves(&mystate, Ensemble::RANDOM_ROTATION);
                }
            });
        };
        tg.wait_all();

        Time end;

        myprintf("%5d predictions in %5.2f seconds -> %d p/s\n",
                 BENCH_AMOUNT,
                 (float)Time::timediff(start,end)/100.0,
                 (int)((float)BENCH_AMOUNT/((float)Time::timediff(start,end)/100.0)));
    }
    {
        // Value
        int BENCH_AMOUNT = 10000;
        int cpus = cfg_num_threads;
        int iters_per_thread = (BENCH_AMOUNT + (cpus - 1)) / cpus;

        Time start;
        ThreadGroup tg(thread_pool);
        for (int i = 0; i < cpus; i++) {
            tg.add_task([iters_per_thread, state]() {
                FastState mystate = *state;
                for (int loop = 0; loop < iters_per_thread; loop++) {
                    auto vec = get_value(&mystate, Ensemble::RANDOM_ROTATION);
                }
            });
        };
        tg.wait_all();

        Time end;

        myprintf("%5d evaluations in %5.2f seconds -> %d p/s\n",
                 BENCH_AMOUNT,
                 (float)Time::timediff(start,end)/100.0,
                 (int)((float)BENCH_AMOUNT/((float)Time::timediff(start,end)/100.0)));
    }
}

void Network::initialize(void) {
#ifdef USE_OPENCL
    myprintf("Initializing OpenCL\n");
    opencl.initialize();
    myprintf("Transferring weights to GPU...");
    opencl_policy_net.push_convolve(5, conv1_w, conv1_b);
    opencl_policy_net.push_convolve(3, conv2_w, conv2_b);
    opencl_policy_net.push_convolve(3, conv3_w, conv3_b);
    opencl_policy_net.push_convolve(3, conv4_w, conv4_b);
    opencl_policy_net.push_convolve(3, conv5_w, conv5_b);
    opencl_policy_net.push_convolve(3, conv6_w, conv6_b);
    opencl_policy_net.push_convolve(3, conv7_w, conv7_b);
    opencl_policy_net.push_convolve(3, conv8_w, conv8_b);
    opencl_policy_net.push_convolve(3, conv9_w, conv9_b);
    opencl_policy_net.push_convolve(3, conv10_w, conv10_b);
    opencl_policy_net.push_convolve(3, conv11_w, conv11_b);
    opencl_policy_net.push_convolve(3, conv12_w, conv12_b);
    opencl_policy_net.push_convolve(3, conv13_w, conv13_b);

    opencl_value_net.push_convolve(5, val_conv1_w, val_conv1_b);
    opencl_value_net.push_convolve(3, val_conv2_w, val_conv2_b);
    opencl_value_net.push_convolve(3, val_conv3_w, val_conv3_b);
    opencl_value_net.push_convolve(3, val_conv4_w, val_conv4_b);
    opencl_value_net.push_convolve(3, val_conv5_w, val_conv5_b);
    opencl_value_net.push_convolve(3, val_conv6_w, val_conv6_b);
    opencl_value_net.push_convolve(3, val_conv7_w, val_conv7_b);
    opencl_value_net.push_convolve(3, val_conv8_w, val_conv8_b);
    opencl_value_net.push_convolve(3, val_conv9_w, val_conv9_b);
    opencl_value_net.push_convolve(3, val_conv10_w, val_conv10_b);
    opencl_value_net.push_convolve(3, val_conv11_w, val_conv11_b);
    opencl_value_net.push_convolve(3, val_conv12_w, val_conv12_b);
    opencl_value_net.push_innerproduct(val_ip13_w, val_ip13_b);
    opencl_value_net.push_innerproduct(val_ip14_w, val_ip14_b);
    myprintf("done\n");
#endif
#ifdef USE_BLAS
#endif
}

#ifdef USE_BLAS
template<unsigned int filter_size,
         unsigned int channels, unsigned int outputs,
         size_t W, size_t B>
void convolve(std::vector<float>& input,
              const std::array<float, W>& weights,
              const std::array<float, B>& biases,
              std::vector<float>& output) {
    // fixed for 19x19
    constexpr unsigned int width = 19;
    constexpr unsigned int height = 19;
    constexpr unsigned int spatial_out = width * height;

    constexpr unsigned int filter_len = filter_size * filter_size;
    constexpr unsigned int filter_dim = filter_len * channels;

    std::vector<float> col(filter_dim * width * height);
    im2col<channels, filter_size>(input, col);

    // Weight shape (output, input, filter_size, filter_size)
    // 96 22 5 5
    // outputs[96,19x19] = weights[96,22x9] x col[22x9,19x19]
    // C←αAB + βC
    // M Number of rows in matrices A and C.
    // N Number of columns in matrices B and C.
    // K Number of columns in matrix A; number of rows in matrix B.
    // lda The size of the first dimention of matrix A; if you are
    // passing a matrix A[m][n], the value should be m.
    //    cblas_sgemm(CblasRowMajor, TransA, TransB, M, N, K, alpha, A, lda, B,
    //                ldb, beta, C, N);

    Gemm::sgemm(outputs, spatial_out, filter_dim, &weights[0], &col[0], &output[0]);

    auto lambda_ELU = [](float val) { return (val > 0.0f) ?
                                      val : 1.0f * (std::exp(val) - 1.0f); };
    //auto lambda_ReLU = [](float val) { return (val > 0.0f) ?
    //                                   val : 0.0f; };

    for (unsigned int o = 0; o < outputs; o++) {
        for (unsigned int b = 0; b < spatial_out; b++) {
            output[(o * spatial_out) + b] =
                lambda_ELU(biases[o] + output[(o * spatial_out) + b]);
        }
    }
}

template<unsigned int inputs,
         unsigned int outputs,
         size_t W, size_t B>
void innerproduct(std::vector<float>& input,
                  const std::array<float, W>& weights,
                  const std::array<float, B>& biases,
                  std::vector<float>& output) {
    assert(B == outputs);

    Gemm::sgemv(outputs, inputs, &weights[0], &input[0], &output[0]);

    auto lambda_ELU = [](float val) { return (val > 0.0f) ?
                                      val : 1.0f * (std::exp(val) - 1.0f); };
    //auto lambda_ReLU = [](float val) { return (val > 0.0f) ?
    //                                   val : 0.0f; };

    for (unsigned int o = 0; o < outputs; o++) {
        float val = biases[o] + output[o];
        if (outputs > 1) {
            val = lambda_ELU(val);
        }
        output[o] = val;
    }
}

template<unsigned int channels,
         unsigned int spatial_size>
void batchnorm(std::vector<float>& input,
               std::array<float, channels>& means,
               std::array<float, channels>& variances,
               std::array<float, 1> scale,
               std::vector<float>& output)
{
    constexpr float epsilon = 1e-5;

    for (unsigned int c = 0; c < channels; ++c) {
        float mean = means[c] / scale[0];
        float variance = variances[c] / scale[0];
        variance += epsilon;
        float scale_stddiv = 1.0f / std::sqrt(variance);

        float * out = &output[c * spatial_size];
        float const * in  = &input[c * spatial_size];
        for (unsigned int b = 0; b < spatial_size; b++) {
            out[b] = scale_stddiv * (in[b] - mean);
        }
    }
}
#endif

void Network::softmax(std::vector<float>& input,
                      std::vector<float>& output,
                      float temperature) {
    assert(&input != &output);

    float alpha = *std::max_element(input.begin(),
                                    input.begin() + output.size());
    alpha /= temperature;

    float denom = 0.0f;
    std::vector<float> helper(output.size());
    for (size_t i = 0; i < output.size(); i++) {
        float val  = std::exp((input[i]/temperature) - alpha);
        helper[i]  = val;
        denom     += val;
    }
    for (size_t i = 0; i < output.size(); i++) {
        output[i] = helper[i] / denom;
    }
}

#ifdef USE_OPENCL
class CallbackData {
public:
    std::atomic<int> * m_nodecount;
    FastState m_state;
    UCTNode * m_node;
    int m_rotation;
    std::atomic<int> * m_thread_results_outstanding;
    std::vector<float> m_output_data;
    std::vector<float> m_input_data;
    Network::BoardPlane m_ladder;
};

extern "C" void CL_CALLBACK forward_cb(cl_event event, cl_int status,
                                       void* data) {
    CallbackData * cb_data = static_cast<CallbackData*>(data);

    // Mark the kernels as available
    cb_data->m_thread_results_outstanding->fetch_sub(1, std::memory_order_release);

    constexpr int width = 19;
    constexpr int height = 19;
    std::vector<float> softmax_data(width * height);
    Network::softmax(cb_data->m_output_data, softmax_data, cfg_softmax_temp);
    std::vector<float>& outputs = softmax_data;

    Network::Netresult result;

    for (size_t idx = 0; idx < outputs.size(); idx++) {
        int rot_idx = Network::rev_rotate_nn_idx(idx, cb_data->m_rotation);
        float val = outputs[rot_idx];
        int x = idx % 19;
        int y = idx / 19;
        int vtx = cb_data->m_state.board.get_vertex(x, y);
        if (cb_data->m_state.board.get_square(vtx) == FastBoard::EMPTY) {
            result.push_back(std::make_pair(val, vtx));
        }
    }

    /* prune losing ladders completely */
    for (auto & sm : result) {
        std::pair<int, int> xy = cb_data->m_state.board.get_xy(sm.second);
        int bitmappos = (xy.second * 19) + xy.first;
        if (cb_data->m_ladder[bitmappos]) {
            //myprintf("Ladder at %s (%d) score %f\n",
            //         state->board.move_to_text(sm.second).c_str(),
            //         sm.second,
            //         sm.first);
            sm.first = 0.0f;
        }
    }

    // Network::show_heatmap(&cb_data->m_state, result, false);

    cb_data->m_node->scoring_cb(cb_data->m_nodecount, cb_data->m_state,
                                result, false);

    delete cb_data;

    // Reduce the count of things having pointers to UCTNodes
    // or UCTSearch. We cannot destroy the search till these
    // have finished.
    opencl.callback_finished();
}

void Network::async_scored_moves(std::atomic<int> * nodecount,
                                 FastState * state,
                                 UCTNode * node,
                                 Ensemble ensemble,
                                 int rotation) {
    if (state->board.get_boardsize() != 19) {
        return;
    }

    assert(ensemble == DIRECT || ensemble == RANDOM_ROTATION);
    if (ensemble == RANDOM_ROTATION) {
        assert(rotation == -1);
        rotation = Random::get_Rng()->randfix<8>();
    } else {
        assert(ensemble == DIRECT);
    }

    CallbackData * cb_data = new CallbackData();

    NNPlanes planes;
    BoardPlane *ladder;
    gather_features_policy(state, planes, &ladder);

    constexpr int width = 19;
    constexpr int height = 19;

    cb_data->m_nodecount = nodecount;
    cb_data->m_state = *state;
    cb_data->m_node = node;
    cb_data->m_input_data.resize(Network::MAX_CHANNELS * 19 * 19);
    cb_data->m_output_data.resize(Network::MAX_CHANNELS * 19 * 19);
    cb_data->m_thread_results_outstanding = opencl.get_thread_results_outstanding();
    //assert(cb_data->m_thread_result_outstanding.load(boost::memory_order_acquire) == 0);
    cb_data->m_rotation = rotation;
    cb_data->m_ladder = *ladder;

    for (int c = 0; c < Network::POLICY_CHANNELS; ++c) {
        for (int h = 0; h < height; ++h) {
            for (int w = 0; w < width; ++w) {
                int vtx = rotate_nn_idx(h * 19 + w, rotation);
                cb_data->m_input_data[(c * height + h) * width + w] =
                    (float)planes[c][vtx];
            }
        }
    }

    void * data = static_cast<void*>(cb_data);

    opencl_policy_net.forward(cb_data->m_input_data,
                                    cb_data->m_output_data,
                                    forward_cb, data);
}
#endif

float Network::get_value(FastState * state, Ensemble ensemble) {
    if (state->board.get_boardsize() != 19) {
        assert(false);
        return 0.5f;
    }

    NNPlanes planes;
    gather_features_value(state, planes);
    float result;

    if (ensemble == DIRECT) {
        result = get_value_internal(state, planes, 0);
    } else if (ensemble == RANDOM_ROTATION) {
        int rotation = Random::get_Rng()->randfix<8>();
        result = get_value_internal(state, planes, rotation);
    } else {
        assert(ensemble == AVERAGE_ALL);
        result = get_value_internal(state, planes, 0);
        //myprintf("%5.4f ", result);
        for (int r = 1; r < 8; r++) {
            float sum_res = get_value_internal(state, planes, r);
            //myprintf("%5.4f ", sum_res);
            result += sum_res;
        }
        result /= 8.0f;
    }

    //if (ensemble == AVERAGE_ALL || ensemble == DIRECT) {
    //    myprintf("==> %5.4f\n", result);
    //}

    return result;
}

Network::Netresult Network::get_scored_moves(
    FastState * state, Ensemble ensemble, int rotation) {
    Netresult result;
    if (state->board.get_boardsize() != 19) {
        return result;
    }

    NNPlanes planes;
    BoardPlane* ladder;
    gather_features_policy(state, planes, &ladder);

    if (ensemble == DIRECT) {
        assert(rotation >= 0 && rotation <= 7);
        result = get_scored_moves_internal(state, planes, rotation);
    } else if (ensemble == RANDOM_ROTATION) {
        assert(rotation == -1);
        int rand_rot = Random::get_Rng()->randfix<8>();
        result = get_scored_moves_internal(state, planes, rand_rot);
    } else {
        assert(ensemble == AVERAGE_ALL);
        result = get_scored_moves_internal(state, planes, 0);
        for (int r = 1; r < 8; r++) {
            auto sum_res = get_scored_moves_internal(state, planes, r);
            for (size_t i = 0; i < sum_res.size(); i++) {
                assert(result[i].second == sum_res[i].second);
                result[i].first += sum_res[i].first;
            }
        }
        std::for_each(result.begin(), result.end(),
                      [](scored_node & sn){ sn.first /= 8.0f; });
    }

    /* prune losing ladders completely */
    for (auto & sm : result) {
        std::pair<int, int> xy = state->board.get_xy(sm.second);
        int bitmappos = (xy.second * 19) + xy.first;
        if ((*ladder)[bitmappos]) {
            //myprintf("Ladder at %s (%d) score %f\n",
            //         state->board.move_to_text(sm.second).c_str(),
            //         sm.second,
            //         sm.first);
            sm.first = 0.0f;
        }
    }

    // if (ensemble == AVERAGE_ALL || ensemble == DIRECT) {
    //     show_heatmap(state, result, true);
    // }

    return result;
}

float Network::get_value_internal(
    FastState * state, NNPlanes & planes, int rotation) {
    assert(rotation >= 0 && rotation <= 7);
    float result;

    constexpr int channels = VALUE_CHANNELS;
    constexpr int width = 19;
    constexpr int height = 19;
    constexpr int max_channels = MAX_VALUE_CHANNELS;
    std::vector<float> orig_input_data(planes.size() * width * height);
    std::vector<float> input_data(max_channels * width * height);
    std::vector<float> output_data(max_channels * width * height);
    std::vector<float> winrate_data(256);
    std::vector<float> winrate_out(1);

    for (int c = 0; c < channels; ++c) {
        for (int h = 0; h < height; ++h) {
            for (int w = 0; w < width; ++w) {
                int vtx = rotate_nn_idx(h * 19 + w, rotation);
                orig_input_data[(c * height + h) * width + w] =
                    (float)planes[c][vtx];
            }
        }
    }
#ifdef USE_OPENCL
    std::copy(orig_input_data.begin(), orig_input_data.end(), input_data.begin());
    opencl_value_net.forward(input_data, output_data, nullptr, nullptr);
    // Sigmoid
    float winrate_sig = (1.0f + std::tanh(output_data[0])) / 2.0f;
    result = winrate_sig;
#elif defined(USE_BLAS)
    std::copy(orig_input_data.begin(), orig_input_data.end(), input_data.begin());

    convolve<5, 32, 64>(input_data, val_conv1_w, val_conv1_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 64, 64>(input_data, val_conv2_w, val_conv2_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 64, 64>(input_data, val_conv3_w, val_conv3_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 64, 64>(input_data, val_conv4_w, val_conv4_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 64, 64>(input_data, val_conv5_w, val_conv5_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 64, 64>(input_data, val_conv6_w, val_conv6_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 64, 64>(input_data, val_conv7_w, val_conv7_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 64, 64>(input_data, val_conv8_w, val_conv8_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 64, 64>(input_data, val_conv9_w, val_conv9_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 64, 64>(input_data, val_conv10_w, val_conv10_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 64, 64>(input_data, val_conv11_w, val_conv11_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 64,  1>(input_data, val_conv12_w, val_conv12_b, output_data);
    // Now get the score
    innerproduct<361, 256>(output_data, val_ip13_w, val_ip13_b, winrate_data);
    innerproduct<256, 1>(winrate_data, val_ip14_w, val_ip14_b, winrate_out);
    // Sigmoid
    float winrate_sig = (1.0f + std::tanh(winrate_out[0])) / 2.0f;
    result = winrate_sig;
 #endif
    return result;
}

Network::Netresult Network::get_scored_moves_internal(
    FastState * state, NNPlanes & planes, int rotation) {
    Netresult result;
    assert(rotation >= 0 && rotation <= 7);
    constexpr int channels = POLICY_CHANNELS;
    constexpr int width = 19;
    constexpr int height = 19;
    constexpr int max_channels = MAX_CHANNELS;
    std::vector<float> orig_input_data(planes.size() * width * height);
    std::vector<float> input_data(max_channels * width * height);
    std::vector<float> output_data(max_channels * width * height);
    std::vector<float> softmax_data(width * height);
    for (int c = 0; c < channels; ++c) {
        for (int h = 0; h < height; ++h) {
            for (int w = 0; w < width; ++w) {
                int vtx = rotate_nn_idx(h * 19 + w, rotation);
                orig_input_data[(c * height + h) * width + w] =
                    (float)planes[c][vtx];
            }
        }
    }
#ifdef USE_OPENCL
    std::copy(orig_input_data.begin(), orig_input_data.end(), input_data.begin());
    opencl_policy_net.forward(input_data, output_data, nullptr, nullptr);
    softmax(output_data, softmax_data, cfg_softmax_temp);
    std::vector<float>& outputs = softmax_data;
#elif defined(USE_BLAS)
    // XXX really only need the first 24
    std::copy(orig_input_data.begin(), orig_input_data.end(), input_data.begin());

    convolve<5,  32,  96>(input_data, conv1_w, conv1_b, output_data);
    std::swap(input_data, output_data);
    convolve<3,  96, 128>(input_data, conv2_w, conv2_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 128, 128>(input_data, conv3_w, conv3_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 128, 128>(input_data, conv4_w, conv4_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 128, 128>(input_data, conv5_w, conv5_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 128, 128>(input_data, conv6_w, conv6_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 128, 128>(input_data, conv7_w, conv7_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 128, 128>(input_data, conv8_w, conv8_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 128, 128>(input_data, conv9_w, conv9_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 128, 128>(input_data, conv10_w, conv10_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 128, 128>(input_data, conv11_w, conv11_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 128, 128>(input_data, conv12_w, conv12_b, output_data);
    std::swap(input_data, output_data);
    convolve<3, 128,   1>(input_data, conv13_w, conv13_b, output_data);
    softmax(output_data, softmax_data, cfg_softmax_temp);

    // Move scores
    std::vector<float>& outputs = softmax_data;
#endif
    for (size_t idx = 0; idx < outputs.size(); idx++) {
        int rot_idx = rev_rotate_nn_idx(idx, rotation);
        float val = outputs[rot_idx];
        int x = idx % 19;
        int y = idx / 19;
        int vtx = state->board.get_vertex(x, y);
        if (state->board.get_square(vtx) == FastBoard::EMPTY) {
            result.push_back(std::make_pair(val, vtx));
        }
    }

    return result;
}

void Network::show_heatmap(FastState * state, Netresult& result, bool topmoves) {
    auto moves = result;
    std::vector<std::string> display_map;
    std::string line;

    for (unsigned int y = 0; y < 19; y++) {
        for (unsigned int x = 0; x < 19; x++) {
            int vtx = state->board.get_vertex(x, y);

            auto item = std::find_if(moves.cbegin(), moves.cend(),
                [&vtx](scored_node const & item) {
                return item.second == vtx;
            });

            float score = 0.0f;
            // Non-empty squares won't be scored
            if (item != moves.end()) {
                score = item->first;
                assert(vtx == item->second);
            }

            line += Utils::format("%3d ", int(score * 1000));
            if (x == 18) {
                display_map.push_back(line);
                line.clear();
            }
        }
    }

    for (int i = display_map.size() - 1; i >= 0; --i) {
        myprintf("%s\n", display_map[i].c_str());
    }

    if (topmoves) {
    std::stable_sort(moves.rbegin(), moves.rend());

    float cum = 0.0f;
    size_t tried = 0;
    while (cum < 0.85f && tried < moves.size()) {
        if (moves[tried].first < 0.01f) break;
        myprintf("%1.3f (%s)\n",
                 moves[tried].first,
                 state->board.move_to_text(moves[tried].second).c_str());
        cum += moves[tried].first;
        tried++;
    }
    }
}

void Network::gather_features_policy(FastState * state, NNPlanes & planes,
                                     BoardPlane** ladder_out) {
    planes.resize(32);
    BoardPlane& empt_color   = planes[0];
    BoardPlane& move_color   = planes[1];
    BoardPlane& othr_color   = planes[2];
    BoardPlane& libs_1       = planes[3];
    BoardPlane& libs_2       = planes[4];
    BoardPlane& libs_3       = planes[5];
    BoardPlane& libs_4       = planes[6];
    BoardPlane& libs_5p      = planes[7];
    BoardPlane& libs_1_e     = planes[8];
    BoardPlane& libs_2_e     = planes[9];
    BoardPlane& libs_3_e     = planes[10];
    BoardPlane& libs_4_e     = planes[11];
    BoardPlane& libs_5p_e    = planes[12];
    BoardPlane& after_1      = planes[13];
    BoardPlane& after_2      = planes[14];
    BoardPlane& after_3      = planes[15];
    BoardPlane& after_4      = planes[16];
    BoardPlane& after_5      = planes[17];
    BoardPlane& after_6p     = planes[18];
    BoardPlane& after_1_e    = planes[19];
    BoardPlane& after_2_e    = planes[20];
    BoardPlane& after_3_e    = planes[21];
    BoardPlane& after_4_e    = planes[22];
    BoardPlane& after_5_e    = planes[23];
    BoardPlane& after_6p_e   = planes[24];
    BoardPlane& ladder       = planes[25];
    BoardPlane& ladder_win   = planes[26];
    BoardPlane& komove       = planes[27];
    BoardPlane& movehist1    = planes[28];
    BoardPlane& movehist2    = planes[29];
    BoardPlane& has_komi     = planes[30];
    BoardPlane& line_3       = planes[31];

    if (ladder_out) {
        *ladder_out = &ladder;
    }

    bool white_has_komi = true;
    if (std::fabs(state->get_komi()) <= 0.75f) {
        white_has_komi = false;
    }

    int tomove = state->get_to_move();
    // collect white, black occupation planes
    for (int j = 0; j < 19; j++) {
        for(int i = 0; i < 19; i++) {
            int vtx = state->board.get_vertex(i, j);
            FastBoard::square_t color =
                state->board.get_square(vtx);
            int idx = j * 19 + i;
            if (i == 2 || i == 16 || j == 2 || j == 16) {
                line_3[idx] = true;
            }
            if (color != FastBoard::EMPTY) {
                // White gets extra points in scoring
                if (color == FastBoard::WHITE && white_has_komi) {
                    has_komi[idx] = true;
                }
                if (color == tomove) {
                    move_color[idx] = true;
                } else {
                    othr_color[idx] = true;
                }
                int rlibs = state->board.count_rliberties(vtx);
                if (rlibs == 1) {
                    if (color == tomove) {
                        libs_1[idx] = true;
                    } else {
                        libs_1_e[idx] = true;
                    }
                } else if (rlibs == 2) {
                    if (color == tomove) {
                        libs_2[idx] = true;
                    } else {
                        libs_2_e[idx] = true;
                    }
                } else if (rlibs == 3) {
                    if (color == tomove) {
                        libs_3[idx] = true;
                    } else {
                        libs_3_e[idx] = true;
                    }
                } else if (rlibs == 4) {
                    if (color == tomove) {
                        libs_4[idx] = true;
                    } else {
                        libs_4_e[idx] = true;
                    }
                } else if (rlibs >= 5) {
                    if (color == tomove) {
                        libs_5p[idx] = true;
                    } else {
                        libs_5p_e[idx] = true;
                    }
                }
            } else {
                empt_color[idx] = true;

                std::pair<int, int> p =
                    state->board.after_liberties(tomove, vtx);
                int al = p.first;
                if (al == 1) {
                    after_1[idx] = true;
                } else if (al == 2) {
                    after_2[idx] = true;
                } else if (al == 3) {
                    after_3[idx] = true;
                } else if (al == 4) {
                    after_4[idx] = true;
                } else if (al == 5) {
                    after_5[idx] = true;
                } else if (al >= 6) {
                    after_6p[idx] = true;
                }
                int at = p.second;
                if (at == 1) {
                    after_1_e[idx] = true;
                } else if (at == 2) {
                    after_2_e[idx] = true;
                } else if (at == 3) {
                    after_3_e[idx] = true;
                } else if (at == 4) {
                    after_4_e[idx] = true;
                } else if (at == 5) {
                    after_5_e[idx] = true;
                } else if (at >= 6) {
                    after_6p_e[idx] = true;
                }

                int ae = state->board.count_pliberties(vtx);
                if (ae == 2) {
                    int ss = state->board.saving_size(tomove, vtx);
                    if (ss > 0) {
                        bool ll = state->board.check_losing_ladder(tomove, vtx);
                        ladder[idx] = ll;
                    }
                }
                if (state->board.check_winning_ladder(tomove, vtx)) {
                    ladder_win[idx] = true;
                }
            }
        }
    }

    if (state->get_last_move() > 0) {
        std::pair<int, int> lastmove = state->board.get_xy(state->get_last_move());
        int idx = lastmove.second * 19 + lastmove.first;
        movehist1[idx] = true;
        if (state->get_prevlast_move() > 0) {
            std::pair<int, int> prevlast = state->board.get_xy(state->get_prevlast_move());
            int idxp = prevlast.second * 19 + prevlast.first;
            movehist2[idxp] = true;
        }
    }

    if (state->get_komove() > 0) {
        std::pair<int, int> kosq = state->board.get_xy(state->get_komove());
        int idx = kosq.second * 19 + kosq.first;
        komove[idx] = true;
    }
}

void Network::gather_features_value(FastState * state, NNPlanes & planes) {
    planes.resize(32);
    BoardPlane& empt_color   = planes[0];
    BoardPlane& move_color   = planes[1];
    BoardPlane& othr_color   = planes[2];
    BoardPlane& libs_1       = planes[3];
    BoardPlane& libs_2       = planes[4];
    BoardPlane& libs_3       = planes[5];
    BoardPlane& libs_4       = planes[6];
    BoardPlane& libs_5       = planes[7];
    BoardPlane& libs_6p      = planes[8];
    BoardPlane& libs_1_e     = planes[9];
    BoardPlane& libs_2_e     = planes[10];
    BoardPlane& libs_3_e     = planes[11];
    BoardPlane& libs_4_e     = planes[12];
    BoardPlane& libs_5_e     = planes[13];
    BoardPlane& libs_6p_e    = planes[14];
    BoardPlane& after_1      = planes[15];
    BoardPlane& after_2      = planes[16];
    BoardPlane& after_3      = planes[17];
    BoardPlane& after_4      = planes[18];
    BoardPlane& after_5      = planes[19];
    BoardPlane& after_6p     = planes[20];
    BoardPlane& after_1_e    = planes[21];
    BoardPlane& after_2_e    = planes[22];
    BoardPlane& after_3_e    = planes[23];
    BoardPlane& after_4_e    = planes[24];
    BoardPlane& after_5_e    = planes[25];
    BoardPlane& after_6p_e   = planes[26];
    BoardPlane& ladder       = planes[27];
    BoardPlane& ladder_win   = planes[28];
    BoardPlane& has_komi     = planes[29];
    BoardPlane& line_3       = planes[30];
    BoardPlane& komove       = planes[31];

    bool white_has_komi = true;
    if (std::fabs(state->get_komi()) <= 0.75f) {
        white_has_komi = false;
    }

    int tomove = state->get_to_move();
    // collect white, black occupation planes
    for (int j = 0; j < 19; j++) {
        for(int i = 0; i < 19; i++) {
            int vtx = state->board.get_vertex(i, j);
            FastBoard::square_t color =
                state->board.get_square(vtx);
            int idx = j * 19 + i;
            if (i == 2 || i == 16 || j == 2 || j == 16) {
                line_3[idx] = true;
            }
            if (color != FastBoard::EMPTY) {
                // White gets extra points in scoring
                if (color == FastBoard::WHITE && white_has_komi) {
                    has_komi[idx] = true;
                }
                if (color == tomove) {
                    move_color[idx] = true;
                } else {
                    othr_color[idx] = true;
                }
                int rlibs = state->board.count_rliberties(vtx);
                if (rlibs == 1) {
                    if (color == tomove) {
                        libs_1[idx] = true;
                    } else {
                        libs_1_e[idx] = true;
                    }
                } else if (rlibs == 2) {
                    if (color == tomove) {
                       libs_2[idx] = true;
                    } else {
                        libs_2_e[idx] = true;
                    }
                } else if (rlibs == 3) {
                    if (color == tomove) {
                        libs_3[idx] = true;
                    } else {
                        libs_3_e[idx] = true;
                    }
                } else if (rlibs == 4) {
                    if (color == tomove) {
                        libs_4[idx] = true;
                    } else {
                        libs_4_e[idx] = true;
                    }
                } else if (rlibs == 5) {
                    if (color == tomove) {
                        libs_5[idx] = true;
                    } else {
                        libs_5_e[idx] = true;
                    }
                }  else if (rlibs >= 6) {
                    if (color == tomove) {
                        libs_6p[idx] = true;
                    } else {
                        libs_6p_e[idx] = true;
                    }
                }
            } else {
                empt_color[idx] = true;

                std::pair<int, int> p =
                    state->board.after_liberties(tomove, vtx);
                int al = p.first;
                if (al == 1) {
                    after_1[idx] = true;
                } else if (al == 2) {
                    after_2[idx] = true;
                } else if (al == 3) {
                    after_3[idx] = true;
                } else if (al == 4) {
                    after_4[idx] = true;
                } else if (al == 5) {
                    after_5[idx] = true;
                } else if (al >= 6) {
                    after_6p[idx] = true;
                }
                int at = p.second;
                if (at == 1) {
                    after_1_e[idx] = true;
                } else if (at == 2) {
                    after_2_e[idx] = true;
                } else if (at == 3) {
                    after_3_e[idx] = true;
                } else if (at == 4) {
                    after_4_e[idx] = true;
                } else if (at == 5) {
                    after_5_e[idx] = true;
                } else if (at >= 6) {
                    after_6p_e[idx] = true;
                }

                int ss = state->board.saving_size(tomove, vtx);
                int ae = state->board.count_pliberties(vtx);
                if (ae == 2) {
                    if (ss > 0) {
                        bool ll = state->board.check_losing_ladder(tomove, vtx);
                        ladder[idx] = ll;
                    }
                }
                if (state->board.check_winning_ladder(tomove, vtx)) {
                    ladder_win[idx] = true;
                }
            }
        }
    }

    if (state->get_komove() > 0) {
        std::pair<int, int> kosq = state->board.get_xy(state->get_komove());
        int idx = kosq.second * 19 + kosq.first;
        komove[idx] = true;
    }
}

void Network::gather_traindata(std::string filename, TrainVector& data) {
    std::vector<std::string> games = SGFParser::chop_all(filename);
    int gametotal = games.size();
    int gamecount = 0;

    size_t train_pos = 0;
    size_t test_pos = 0;

    myprintf("Total games in file: %d\n", gametotal);
    myprintf("Shuffling...\n");
    std::shuffle(games.begin(), games.end(), std::mt19937(std::random_device{}()));

    while (gamecount < gametotal) {
        std::unique_ptr<SGFTree> sgftree(new SGFTree);

        try {
            sgftree->load_from_string(games[gamecount]);
        } catch (...) {
        };

        SGFTree * treewalk = &(*sgftree);
        size_t counter = 0;

        size_t movecount = sgftree->count_mainline_moves();
        std::vector<int> tree_moves = sgftree->get_mainline();
        int who_won = sgftree->get_winner();
        int handicap = sgftree->get_state()->get_handicap();
        //float komi = sgftree->get_state()->get_komi();
        if (handicap) {
            goto skipnext;
        }
        // 5.5, 6.5, 7.5
        //if (std::abs(komi) > 0.75f && (std::abs(komi) < 5.25f || std::abs(komi) > 7.75f)) {
        //    goto skipnext;
        //}
        if (who_won != FastBoard::BLACK && who_won != FastBoard::WHITE) {
            goto skipnext;
        }

        while (counter < movecount) {
            assert(treewalk != NULL);
            assert(treewalk->get_state() != NULL);
            if (treewalk->get_state()->board.get_boardsize() != 19)
                break;

            KoState * state = treewalk->get_state();
            int tomove = state->get_to_move();
            int move;

            if (treewalk->get_child(0) != NULL) {
                move = treewalk->get_child(0)->get_move(tomove);
                if (move == SGFTree::EOT) {
                    break;
                }
            } else {
                break;
            }

            assert(move == tree_moves[counter]);
            int this_move = -1;

            std::vector<int> moves = state->generate_moves(tomove);
            bool moveseen = false;
            for(auto it = moves.begin(); it != moves.end(); ++it) {
                if (*it == move) {
                    if (move != FastBoard::PASS) {
                        // get x y coords for actual move
                        std::pair<int, int> xy = state->board.get_xy(move);
                        this_move = (xy.second * 19) + xy.first;
                    }
                    moveseen = true;
                }
            }

            bool has_next_moves = counter + 2 < tree_moves.size();
            if (!has_next_moves) {
                goto skipnext;
            }

            has_next_moves  = tree_moves[counter + 1] != FastBoard::PASS;
            has_next_moves &= tree_moves[counter + 2] != FastBoard::PASS;

            if (!has_next_moves) {
                goto skipnext;
            }

            //int skip = Random::get_Rng()->randfix<8>();
            if (/*skip == 0*/ 1) {
                if (moveseen && move != FastBoard::PASS && has_next_moves) {
                    TrainPosition position;
                    //position.stm_won = (tomove == who_won ? 1.0f : 0.0f);
                    //position.stm_won_tanh = (tomove == who_won ? 1.0f : -1.0f);
                    //float frac = (float)counter / (float)movecount;
                    //position.stm_score = (frac * position.stm_won)
                        //+ ((1.0f - frac) * 0.5f);
                    //position.stm_score_tanh = (frac * position.stm_won_tanh)
                        //+ ((1.0f - frac) * 0.0f);
                    //gather_features_value(state, position.planes);
                    position.moves[0] = this_move;
                    // add next 2 moves to position
                    // we do not check them for legality
                    int next_move = tree_moves[counter + 1];
                    int next_next_move = tree_moves[counter + 2];
                    std::pair<int, int> xy = state->board.get_xy(next_move);
                    position.moves[1] = (xy.second * 19) + xy.first;
                    xy = state->board.get_xy(next_next_move);
                    position.moves[2] = (xy.second * 19) + xy.first;
                    gather_features_policy(state, position.planes);
                    data.push_back(position);
                } else if (move != FastBoard::PASS) {
                    myprintf("Mainline move not found: %d\n", move);
                    goto skipnext;
                }
            }

            counter++;
            treewalk = treewalk->get_child(0);
        }

skipnext:
        gamecount++;
        if (gamecount % 100 == 0) {
            myprintf("Game %d, %d new positions, %d total\n",
                     gamecount, data.size(), train_pos + data.size());
        }
        if (gamecount % (50000) == 0) {
            train_network(data, train_pos, test_pos);
        }
    }

    train_network(data, train_pos, test_pos);

    std::cout << train_pos << " training positions." << std::endl;
    std::cout << test_pos << " testing positions." << std::endl;

    myprintf("Gathering pass done.\n");
}

int Network::rev_rotate_nn_idx(const int vertex, int symmetry) {
    static const int invert[] = {0, 1, 2, 3, 4, 6, 5, 7};
    assert(rotate_nn_idx(rotate_nn_idx(vertex, symmetry), invert[symmetry])
           == vertex);
    return rotate_nn_idx(vertex, invert[symmetry]);
}

int Network::rotate_nn_idx(const int vertex, int symmetry) {
    assert(vertex >= 0 && vertex < 19*19);
    assert(symmetry >= 0 && symmetry < 8);
    int x = vertex % 19;
    int y = vertex / 19;
    int newx;
    int newy;

    if (symmetry >= 4) {
        std::swap(x, y);
        symmetry -= 4;
    }

    if (symmetry == 0) {
        newx = x;
        newy = y;
    } else if (symmetry == 1) {
        newx = x;
        newy = 19 - y - 1;
    } else if (symmetry == 2) {
        newx = 19 - x - 1;
        newy = y;
    } else {
        assert(symmetry == 3);
        newx = 19 - x - 1;
        newy = 19 - y - 1;
    }

    int newvtx = (newy * 19) + newx;
    assert(newvtx >= 0 && newvtx < 19*19);
    return newvtx;
}

void Network::train_network(TrainVector& data,
                            size_t& total_train_pos,
                            size_t& total_test_pos) {
}

void Network::autotune_from_file(std::string filename) {
    TrainVector data;
    gather_traindata(filename, data);
}

std::string Network::get_backend() {
#if defined(USE_OPENCL)
    return opencl.get_device_name();
#else
#ifdef USE_BLAS
#endif
#endif
    return std::string("No BLAS backend active");
}
