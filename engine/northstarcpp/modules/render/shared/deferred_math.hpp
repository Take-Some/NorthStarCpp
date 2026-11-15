#pragma once
// CPU-side inverse VP for depth-based world position reconstruction.
// The input is a row-major matrix, matching material HLSL mul(row, matrix).
#include <array>
#include <cmath>
#include <stdexcept>

namespace northstarcpp::render {
inline std::array<float, 16> inverse_view_projection(const float* value) {
    if (!value) throw std::invalid_argument("Missing view-projection matrix");
    double rows[4][8]{};
    for (int y = 0; y != 4; ++y) {
        for (int x = 0; x != 4; ++x)
            rows[y][x] = static_cast<double>(value[y * 4 + x]);
        rows[y][4 + y] = 1.;
    }
    for (int pivot = 0; pivot != 4; ++pivot) {
        int best = pivot;
        for (int y = pivot + 1; y != 4; ++y)
            if (std::abs(rows[y][pivot]) > std::abs(rows[best][pivot])) best = y;
        if (!std::isfinite(rows[best][pivot]) || std::abs(rows[best][pivot]) < 1.e-12)
            throw std::invalid_argument("Singular view-projection matrix");
        if (best != pivot) for (int x = 0; x != 8; ++x) {
            double tmp = rows[pivot][x];
            rows[pivot][x] = rows[best][x];
            rows[best][x] = tmp;
        }
        double const scale = rows[pivot][pivot];
        for (int x = 0; x != 8; ++x) rows[pivot][x] /= scale;
        for (int y = 0; y != 4; ++y) {
            if (y == pivot) continue;
            const double factor = rows[y][pivot];
            for (int x = 0; x != 8; ++x)
                rows[y][x] -= factor * rows[pivot][x];
        }
    }
    std::array<float, 16> output{};
    for (int y = 0; y != 4; ++y)
        for (int x = 0; x != 4; ++x) {
            const double result = rows[y][x + 4];
            if (!std::isfinite(result))
                throw std::invalid_argument("Non-finite inverse view-projection");
            output[static_cast<std::size_t>(y * 4 + x)] = static_cast<float>(result);
        }
    return output;
}
}
