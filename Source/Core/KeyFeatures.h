#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

// Key features v2 (v1.11): peak-based harmonic pitch-class profiles (HPCP, after Gómez 2006) with tuning estimation.
// - Large FFT frames (~0.34 s at 48 kHz, 2.9 Hz bins) so bass notes a semitone apart separate.
// - Spectral peaks with parabolic interpolation give exact frequencies; a global tuning offset (samples are often
//   a few cents to a quarter-tone off A440) is estimated from all peaks and removed before mapping to pitch classes.
// - Each peak also votes for the fundamentals it could be a harmonic of (2nd..4th, decaying), so overtones reinforce
//   their note instead of pulling the profile toward the fifth.
// - Frames are normalised before summing (a quiet chord counts as much as a loud one); bass (< 220 Hz) is kept apart.
namespace neuro
{

struct KeyFeatures
{
    std::array<float, 12> hpcp {};  // harmonic-weighted, all registers (sums to 1)
    std::array<float, 12> bass {};  // peaks 30-220 Hz only (sums to 1)
    std::array<float, 12> hpcpStart {}, bassStart {}; // the first quarter (loops usually start on the tonic chord)
    float tuningCents = 0.0f;       // estimated offset from A440, -50 .. +50
    int frames = 0;
};

namespace keyfeat
{
    inline void fft (std::vector<std::complex<double>>& a)
    {
        const auto n = a.size();

        for (size_t i = 1, j = 0; i < n; ++i)
        {
            auto bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }

        for (size_t len = 2; len <= n; len <<= 1)
        {
            const auto ang = -2.0 * 3.14159265358979323846 / (double) len;
            const std::complex<double> wl (std::cos (ang), std::sin (ang));

            for (size_t i = 0; i < n; i += len)
            {
                std::complex<double> w (1.0, 0.0);

                for (size_t j = 0; j < len / 2; ++j)
                {
                    const auto u = a[i + j], v = a[i + j + len / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
    }

    struct Peak { float midi, mag; };
}

inline KeyFeatures computeKeyFeatures (const float* const* channels, int numChannels, int64_t numSamples, double sampleRate,
                                       double maxSeconds = 90.0)
{
    KeyFeatures out;

    if (channels == nullptr || numChannels <= 0 || sampleRate <= 0.0 || numSamples <= 0)
        return out;

    const auto frame = (size_t) std::pow (2.0, std::round (std::log2 (0.34 * sampleRate))); // 16384 at 44.1/48 kHz
    const auto hop = frame / 2;
    const auto n = (int64_t) std::min<double> ((double) numSamples, maxSeconds * sampleRate);
    std::vector<double> window (frame);
    for (size_t i = 0; i < frame; ++i)
        window[i] = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (double) i / (double) (frame - 1));

    std::vector<std::vector<keyfeat::Peak>> framePeaks;
    std::vector<std::complex<double>> buf (frame);
    std::vector<double> mag (frame / 2);
    const auto binHz = sampleRate / (double) frame;
    const auto lo = (size_t) std::ceil (30.0 / binHz), hi = std::min (frame / 2 - 2, (size_t) (5000.0 / binHz));

    for (int64_t start = 0; start == 0 || start + (int64_t) frame <= n; start += (int64_t) hop)
    {
        double energy = 0.0;

        for (size_t i = 0; i < frame; ++i)
        {
            double s = 0.0;
            if (start + (int64_t) i < n)
                for (int ch = 0; ch < numChannels; ++ch)
                    s += channels[ch][start + (int64_t) i];
            s = s / numChannels * window[i];
            energy += s * s;
            buf[i] = { s, 0.0 };
        }

        if (energy < 1.0e-7 * (double) frame)
            continue;

        keyfeat::fft (buf);
        double peakMax = 0.0;

        for (size_t k = 0; k < frame / 2; ++k)
        {
            mag[k] = std::abs (buf[k]);
            if (k >= lo && k <= hi) peakMax = std::max (peakMax, mag[k]);
        }

        std::vector<keyfeat::Peak> peaks;
        const auto floor = peakMax * 0.01; // -40 dB under the frame's strongest peak

        for (size_t k = lo; k <= hi; ++k)
            if (mag[k] > floor && mag[k] > mag[k - 1] && mag[k] >= mag[k + 1])
            {
                // Parabolic interpolation on log magnitude.
                const auto a = std::log (mag[k - 1] + 1e-12), b = std::log (mag[k] + 1e-12), c = std::log (mag[k + 1] + 1e-12);
                const auto d = 0.5 * (a - c) / (a - 2.0 * b + c);
                const auto hz = ((double) k + d) * binHz;
                peaks.push_back ({ (float) (69.0 + 12.0 * std::log2 (hz / 440.0)), (float) (mag[k] / peakMax) });
            }

        if (! peaks.empty())
            framePeaks.push_back (std::move (peaks));
    }

    if (framePeaks.empty())
        return out;

    // Tuning: the circular mean of every peak's deviation from the equal-tempered grid, weighted by magnitude.
    double sx = 0.0, sy = 0.0;
    for (const auto& fp : framePeaks)
        for (const auto& p : fp)
        {
            const auto dev = (double) p.midi - std::round ((double) p.midi);
            sx += p.mag * std::cos (2.0 * 3.14159265358979323846 * dev);
            sy += p.mag * std::sin (2.0 * 3.14159265358979323846 * dev);
        }
    const auto tuning = std::atan2 (sy, sx) / (2.0 * 3.14159265358979323846); // semitones, -0.5 .. 0.5
    out.tuningCents = (float) (tuning * 100.0);

    static constexpr double harmonic[4] { 0.0, 12.0, 19.0195500, 24.0 }; // semitones of harmonics 1..4
    static constexpr double harmonicWeight[4] { 1.0, 0.6, 0.36, 0.22 };
    std::array<double, 12> hpcp {}, bass {}, hpcpStart {}, bassStart {};
    const auto startFrames = std::max<size_t> (2, framePeaks.size() / 4);
    size_t frameIndex = 0;

    for (const auto& fp : framePeaks)
    {
        std::array<double, 12> f {}, fb {};

        for (const auto& p : fp)
        {
            const auto m = (double) p.midi - tuning;
            const auto w = (double) p.mag * (double) p.mag; // energy

            for (int h = 0; h < 4; ++h)
            {
                const auto fundamental = m - harmonic[h];
                if (fundamental < 24.0) // below C1: not a note
                    continue;
                const auto nearest = std::round (fundamental);
                const auto d = std::abs (fundamental - nearest);
                if (d > 0.5) continue;
                const auto shape = std::cos (3.14159265358979323846 * d) * std::cos (3.14159265358979323846 * d); // cos^2 over +-0.5 st
                f[(size_t) (((int) nearest % 12 + 12) % 12)] += w * shape * harmonicWeight[h];
            }

            if (m < 69.0 - 12.0 * std::log2 (440.0 / 220.0) + 0.5) // below 220 Hz
            {
                const auto nearest = std::round (m);
                const auto d = std::abs (m - nearest);
                fb[(size_t) (((int) nearest % 12 + 12) % 12)] += w * std::cos (3.14159265358979323846 * d) * std::cos (3.14159265358979323846 * d);
            }
        }

        const auto fmax = *std::max_element (f.begin(), f.end());
        const auto bmax = *std::max_element (fb.begin(), fb.end());
        for (size_t i = 0; i < 12; ++i)
        {
            if (fmax > 0.0) hpcp[i] += f[i] / fmax;
            if (bmax > 0.0) bass[i] += fb[i] / bmax;
            if (frameIndex < startFrames && fmax > 0.0) hpcpStart[i] += f[i] / fmax;
            if (frameIndex < startFrames && bmax > 0.0) bassStart[i] += fb[i] / bmax;
        }
        ++frameIndex;
        ++out.frames;
    }

    auto normalise = [] (const std::array<double, 12>& in, std::array<float, 12>& o)
    {
        double t = 0.0;
        for (auto v : in) t += v;
        for (size_t i = 0; i < 12; ++i) o[i] = t > 0.0 ? (float) (in[i] / t) : 0.0f;
    };
    normalise (hpcpStart, out.hpcpStart);
    normalise (bassStart, out.bassStart);
    double ht = 0.0, bt = 0.0;
    for (size_t i = 0; i < 12; ++i) { ht += hpcp[i]; bt += bass[i]; }
    for (size_t i = 0; i < 12; ++i)
    {
        out.hpcp[i] = ht > 0.0 ? (float) (hpcp[i] / ht) : 0.0f;
        out.bass[i] = bt > 0.0 ? (float) (bass[i] / bt) : 0.0f;
    }

    return out;
}

} // namespace neuro
