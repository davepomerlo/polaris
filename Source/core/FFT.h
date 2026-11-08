#pragma once
// Small iterative radix-2 FFT plus a windowed power-spectrum helper. Not for the audio thread.
#include "Biquad.h"
#include <complex>
#include <vector>

namespace polaris
{
class FFT
{
public:
    explicit FFT (int order = 12) { setOrder (order); }

    void setOrder (int order)
    {
        n = 1 << order;
        twiddle.resize ((size_t) n / 2);
        for (int k = 0; k < n / 2; ++k)
            twiddle[(size_t) k] = std::polar (1.0f, (float) (-2.0 * kPi * k / n));
        rev.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; (1 << b) < n; ++b)
                if (i & (1 << b)) r |= (n >> (b + 1));
            rev[(size_t) i] = r;
        }
        window.resize ((size_t) n);
        windowSum = 0;
        for (int i = 0; i < n; ++i)
        {
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / n));
            windowSum += window[(size_t) i];
        }
        work.resize ((size_t) n);
    }

    int size() const { return n; }

    void transform (std::vector<std::complex<float>>& data) const
    {
        for (int i = 0; i < n; ++i)
            if (i < rev[(size_t) i]) std::swap (data[(size_t) i], data[(size_t) rev[(size_t) i]]);

        for (int len = 2; len <= n; len <<= 1)
        {
            const int step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < len / 2; ++j)
                {
                    const auto w = twiddle[(size_t) (j * step)];
                    const auto u = data[(size_t) (i + j)];
                    const auto v = data[(size_t) (i + j + len / 2)] * w;
                    data[(size_t) (i + j)] = u + v;
                    data[(size_t) (i + j + len / 2)] = u - v;
                }
        }
    }

    /** Hann-windowed power spectrum (n/2+1 bins), scaled so a full-scale sine reads ~1.0 (0 dB). */
    void powerSpectrum (const float* input, std::vector<float>& powerOut)
    {
        for (int i = 0; i < n; ++i) work[(size_t) i] = { input[i] * window[(size_t) i], 0.0f };
        transform (work);
        powerOut.resize ((size_t) n / 2 + 1);
        const double scale = 2.0 / windowSum;
        for (int k = 0; k <= n / 2; ++k)
        {
            const double mag = std::abs (work[(size_t) k]) * scale;
            powerOut[(size_t) k] = (float) (mag * mag);
        }
    }

private:
    int n = 4096;
    std::vector<std::complex<float>> twiddle, work;
    std::vector<int> rev;
    std::vector<float> window;
    double windowSum = 1;
};
} // namespace polaris
