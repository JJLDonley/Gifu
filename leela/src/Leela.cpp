#include "config.h"
#include <algorithm>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include "NNWeights.h"
#include "Network.h"

#include "Zobrist.h"
#include "GTP.h"
#include "SMP.h"
#include "Random.h"
#include "Utils.h"
#include "Matcher.h"
#include "AttribScores.h"
#include "ThreadPool.h"
#include "MCPolicy.h"

using namespace Utils;

static void usage(void) {
    std::cout <<
        "Allowed options:\n"
        "  -h [ --help ]              Show commandline options.\n"
        "  -g [ --gtp ]               Enable GTP mode.\n"
        "  -t [ --threads ] N         Number of threads to use.\n"
        "  -p [ --playouts ] N        Weaken engine by limiting the number of playouts.\n"
        "                             Requires --noponder.\n"
        "  -b [ --lagbuffer ] CS      Safety margin for time usage in centiseconds.\n"
        "  -l [ --logfile ] FILE      File to log input/output to.\n"
        "  -q [ --quiet ]             Disable all diagnostic output.\n"
        "  -k [ --komiadjust ]        Adjust komi one point in my disadvantage\n"
        "                             (for territory scoring).\n"
        "  -w [ --weights ] FILE      Network weights (default: leela-0.11.weights\n"
        "                             next to the program).\n"
        "  --noponder                 Disable thinking on opponent's time.\n"
        "  --nonets                   Disable use of neural networks.\n"
        "  --nobook                   Disable use of the fuseki library.\n"
#ifdef USE_OPENCL
        "  --gpu ID                   ID of the OpenCL device(s) to use (repeatable;\n"
        "                             disables autodetection).\n"
        "  --rowtiles N               Split up the board in # tiles.\n"
#endif
        << std::endl;
}

static int int_arg(const std::string & option, const char * value) {
    char * end = nullptr;
    long v = value ? std::strtol(value, &end, 10) : 0;
    if (!value || *value == '\0' || *end != '\0') {
        myprintf("ERROR: %s needs a number\n", option.c_str());
        usage();
        exit(EXIT_FAILURE);
    }
    return static_cast<int>(v);
}

std::string cfg_weights_file;

void parse_commandline(int argc, char *argv[], bool & gtp_mode) {
    bool noponder = false;
    int playouts = -1, threads = cfg_num_threads, lagbuffer = cfg_lagbuffer_cs;
#ifdef USE_OPENCL
    int rowtiles = cfg_rowtiles;
#endif
    cfg_weights_file = Weights::default_path(argc > 0 ? argv[0] : "");
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        // --name=value is accepted as well as --name value
        const char * inline_value = nullptr;
        std::string inline_storage;
        if (starts_with(arg, "--") && arg.find('=') != std::string::npos) {
            inline_storage = arg.substr(arg.find('=') + 1);
            arg = arg.substr(0, arg.find('='));
            inline_value = inline_storage.c_str();
        }
        auto value = [&]() -> const char * {
            if (inline_value) return inline_value;
            return i + 1 < argc ? argv[++i] : nullptr;
        };
        if (arg == "-h" || arg == "--help") {
            usage();
            exit(EXIT_SUCCESS);
        } else if (arg == "-g" || arg == "--gtp") {
            gtp_mode = true;
        } else if (arg == "-t" || arg == "--threads") {
            threads = int_arg(arg, value());
        } else if (arg == "-p" || arg == "--playouts") {
            playouts = int_arg(arg, value());
        } else if (arg == "-b" || arg == "--lagbuffer") {
            lagbuffer = int_arg(arg, value());
        } else if (arg == "-l" || arg == "--logfile") {
            const char * v = value();
            if (!v) { usage(); exit(EXIT_FAILURE); }
            cfg_logfile = v;
        } else if (arg == "-w" || arg == "--weights") {
            const char * v = value();
            if (!v) { usage(); exit(EXIT_FAILURE); }
            cfg_weights_file = v;
        } else if (arg == "-q" || arg == "--quiet") {
            cfg_quiet = true;
        } else if (arg == "-k" || arg == "--komiadjust") {
            myprintf("Adjusting komi for territory scoring rules.\n");
            cfg_komi_adjust = true;
        } else if (arg == "--noponder") {
            noponder = true;
            cfg_allow_pondering = false;
        } else if (arg == "--nonets") {
            cfg_enable_nets = false;
        } else if (arg == "--nobook") {
            cfg_allow_book = false;
#ifdef USE_OPENCL
        } else if (arg == "--gpu") {
            cfg_gpus.push_back(int_arg(arg, value()));
        } else if (arg == "--rowtiles") {
            rowtiles = int_arg(arg, value());
#endif
        } else {
            std::cout << "Unrecognized argument: " << arg << std::endl;
            usage();
            exit(EXIT_FAILURE);
        }
    }

    if (!cfg_logfile.empty()) {
        myprintf("Logging to %s.\n", cfg_logfile.c_str());
        cfg_logfile_handle = fopen(cfg_logfile.c_str(), "a");
    }
    if (threads > cfg_num_threads) {
        myprintf("Clamping threads to maximum = %d\n", cfg_num_threads);
    } else if (threads != cfg_num_threads) {
        myprintf("Using %d thread(s).\n", threads);
        cfg_num_threads = threads;
    }
    if (playouts >= 0) {
        cfg_max_playouts = playouts;
        if (!noponder) {
            myprintf("Nonsensical options: Playouts are restricted but "
                     "thinking on the opponent's time is still allowed. "
                     "Add --noponder if you want a weakened engine.\n");
            exit(EXIT_FAILURE);
        }
    }
    if (lagbuffer != cfg_lagbuffer_cs) {
        myprintf("Using per-move time margin of %.2fs.\n", lagbuffer/100.0f);
        cfg_lagbuffer_cs = lagbuffer;
    }
#ifdef USE_OPENCL
    rowtiles = std::min(19, std::max(1, rowtiles));
    if (rowtiles != cfg_rowtiles) {
        myprintf("Splitting the board in %d tiles.\n", rowtiles);
        cfg_rowtiles = rowtiles;
    }
#endif
}

int main (int argc, char *argv[]) {
    bool gtp_mode = false;
    std::string input;

    // Set up engine parameters
    GTP::setup_default_parameters();
    parse_commandline(argc, argv, gtp_mode);

    // Disable IO buffering as much as possible
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
    std::cin.setf(std::ios::unitbuf);

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
#ifndef _WIN32
    setbuf(stdin, NULL);
#endif

    thread_pool.initialize(cfg_num_threads);

    // Use deterministic random numbers for hashing
    std::unique_ptr<Random> rng(new Random(5489));
    Zobrist::init_zobrist(*rng);

    // Initialize things
    AttribScores::get_attribscores();
    Matcher::get_Matcher();
    std::string weights_error;
    if (!Weights::load(cfg_weights_file, weights_error)) {
        myprintf("ERROR: %s\n", weights_error.c_str());
        exit(EXIT_FAILURE);
    }
    Network::get_Network();

    std::unique_ptr<GameState> maingame(new GameState);

    /* set board limits */
    float komi = 7.5;
    maingame->init_game(19, komi);

    if (!GTP::perform_self_test(*maingame)) {
        exit(EXIT_FAILURE);
    }

    for(;;) {
        if (!gtp_mode) {
            maingame->display_state();
            std::cout << "Leela: ";
        }

        if (std::getline(std::cin, input)) {
            Utils::log_input(input);
            GTP::execute(*maingame, input);
        } else {
            // eof or other error
            break;
        }

        // Force a flush of the logfile
        if (cfg_logfile_handle) {
            fclose(cfg_logfile_handle);
            cfg_logfile_handle = fopen(cfg_logfile.c_str(), "a");
        }
    }

    return 0;
}
