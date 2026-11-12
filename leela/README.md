# Leela (rebuilt for Gifu)

This is **Leela 0.11**, the Go engine by Gian-Carlo Pascutto (https://github.com/gcp/Leela,
commit afe9099, MIT licence, see `LICENSE`), rebuilt so it compiles with current
compilers on Windows, Linux and macOS and ships with Gifu. Its network was trained on
human games. As its author asked of forks, it keeps the name Leela.

Changes from the original:
- **No Boost:** command-line parsing and string helpers are plain C++ (`Leela.cpp`, `Utils.cpp`).
- **No BLAS library:** the CPU build uses its own matrix routines (`src/Gemm.h`).
- **No OpenCL SDK:** the Khronos headers are vendored (`opencl/`) and the system's
  OpenCL library is loaded when the program starts (`src/OpenCLLoader.cpp`,
  `src/OpenCLFunctions.inc` from `tools/gen_opencl_loader.py`).
- **Weights in a file:** the network (formerly `NN.cpp`, `NN128.cpp`, `NNValue.cpp`,
  66 MB of C++ source) is read from `weights/leela-0.11.weights` (`src/NNWeights.*`,
  converted by `tools/convert_weights.py`); `--weights FILE` picks another.
- **C++17 and CMake:** removed standard-library features replaced (`std::result_of`,
  `std::binary_function`, `std::random_shuffle`); portable timing (`std::chrono`).
- Caffe, MKL, OpenBLAS, Accelerate, wxWidgets and the training-time tuner are gone.

Two programs, as the original shipped:
- `leela`: CPU, 128-filter policy network;
- `leela-opencl`: GPU, 192-filter policy network (falls back to nothing: without an
  OpenCL driver it exits with a message; use `leela` there).

Build: part of Gifu's CMake build (`GIFU_BUILD_LEELA`, on by default); the programs and
the weights land in `<build>/leela/`.
