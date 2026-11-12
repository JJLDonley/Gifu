// Gemm.h - the matrix products the CPU build needs (it used OpenBLAS / MKL /
// Accelerate before). Row-major, no transposes, C = A x B (C is overwritten).
// Written so that compilers vectorise the inner loop over the 361 board points.
#ifndef GEMM_H_INCLUDED
#define GEMM_H_INCLUDED

#include <algorithm>
#include <cstring>

namespace Gemm {

// C[m x n] = A[m x k] * B[k x n]. B is walked in blocks of rows that stay in cache.
inline void sgemm(int m, int n, int k, const float * a, const float * b, float * c) {
    constexpr int KB = 64;   // 64 rows of a 19x19 plane: ~92 KB of B per block
    std::memset(c, 0, sizeof(float) * size_t(m) * size_t(n));
    for (int k0 = 0; k0 < k; k0 += KB) {
        const int k1 = std::min(k, k0 + KB);
        for (int i = 0; i < m; ++i) {
            float * __restrict ci = c + size_t(i) * n;
            const float * ai = a + size_t(i) * k;
            for (int p = k0; p < k1; ++p) {
                const float x = ai[p];
                if (x == 0.0f) continue;
                const float * __restrict bp = b + size_t(p) * n;
                for (int j = 0; j < n; ++j) ci[j] += x * bp[j];
            }
        }
    }
}

// y[m] = A[m x k] * x[k]
inline void sgemv(int m, int k, const float * a, const float * x, float * y) {
    for (int i = 0; i < m; ++i) {
        const float * __restrict ai = a + size_t(i) * k;
        float sum = 0.0f;
        for (int p = 0; p < k; ++p) sum += ai[p] * x[p];
        y[i] = sum;
    }
}

}

#endif
