// Copyright 2026 MutatedHypnoise contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Output filter: TPT / zero-delay-feedback state variable filter
// (Zavalishin, "The Art of VA Filter Design"; Simper, "Linear trapezoidal
// integrated SVF"), in fixed point for the Cortex-M3 (no FPU).
//
//   g  = tan(pi fc / fs)                    lut_tpt_g, Q28, g <= 1
//   k  = 1 / Q, from kMinDamping to 2       Q28
//   a1 = 1 / (1 + g (g + k))                Q30, once per block
//   a2 = g a1,  a3 = g a2                   Q30
//
//   v3 = x - ic2
//   v1 = a1 ic1 + a2 v3                     band-pass
//   v2 = ic2 + a2 ic1 + a3 v3               low-pass
//   hp = x - k v1 - v2                      high-pass
//   ic1 = 2 v1 - ic1,  ic2 = 2 v2 - ic2
//
// Audio runs internally as 16-bit samples << 8 (24-bit headroom domain).
// Anti-artifact measures:
//   - cutoff/resonance smoothed per block in the pitch (log) domain;
//   - coefficients recomputed per block (4 kHz), which the TPT structure
//     tolerates without zipper noise;
//   - input attenuated as resonance rises, states clamped as a safety net,
//     soft knee limiter on the output;
//   - ~2 ms crossfade when the mode changes (incl. OFF <-> ON).

#ifndef BRAIDS_OUTPUT_FILTER_H_
#define BRAIDS_OUTPUT_FILTER_H_

#include "stmlib/stmlib.h"

#include "braids/resources.h"

namespace braids {

enum OutputFilterMode {
  OUTPUT_FILTER_OFF,
  OUTPUT_FILTER_LP,
  OUTPUT_FILTER_HP,
  OUTPUT_FILTER_BP,
  OUTPUT_FILTER_LAST
};

// Cutoff in Braids pitch units (semitone << 7, MIDI note 60 = 60 << 7).
const int32_t kOutputFilterMinCutoff = 12 << 7;   // 16.35 Hz
const int32_t kOutputFilterMaxCutoff = 140 << 7;  // clamped to 24 kHz by LUT

class OutputFilter {
 public:
  OutputFilter() { }
  ~OutputFilter() { }

  void Init() {
    ic1_ = ic2_ = 0;
    mode_ = target_mode_ = OUTPUT_FILTER_OFF;
    wet_ = 0;
    cutoff_ = 60 << (7 + 8);
    resonance_ = 0;
    ComputeCoefficients();
  }

  // Call once per block, before Process().
  //   cutoff:    pitch units, any range (clamped here).
  //   resonance: 0 (Q = 0.5) .. 32767 (Q ~ 30).
  void Configure(OutputFilterMode mode, int32_t cutoff, int32_t resonance) {
    target_mode_ = mode;
    if (cutoff < kOutputFilterMinCutoff) cutoff = kOutputFilterMinCutoff;
    if (cutoff > kOutputFilterMaxCutoff) cutoff = kOutputFilterMaxCutoff;
    if (resonance < 0) resonance = 0;
    if (resonance > 32767) resonance = 32767;

    // One-pole smoothing, ~1 ms at the 4 kHz block rate. cutoff_ keeps
    // 8 extra fractional bits so it settles exactly on the target.
    cutoff_ += ((cutoff << 8) - cutoff_) >> 2;
    resonance_ += (resonance - resonance_) >> 3;
    ComputeCoefficients();
  }

  inline bool active() const {
    return mode_ != OUTPUT_FILTER_OFF || target_mode_ != OUTPUT_FILTER_OFF;
  }

  // in/out: 16-bit audio sample.
  inline int16_t Process(int16_t in) {
    // Mode change: fade to dry, swap mode and clear state, fade back in.
    if (mode_ != target_mode_) {
      if (wet_ > 0 && mode_ != OUTPUT_FILTER_OFF) {
        wet_ -= kFadeStep;
        if (wet_ < 0) wet_ = 0;
      } else {
        mode_ = target_mode_;
        ic1_ = ic2_ = 0;
        wet_ = 0;
      }
    } else if (mode_ != OUTPUT_FILTER_OFF && wet_ < kUnity) {
      wet_ += kFadeStep;
      if (wet_ > kUnity) wet_ = kUnity;
    }
    if (mode_ == OUTPUT_FILTER_OFF) {
      return in;
    }

    int32_t x = static_cast<int32_t>(in) * input_gain_ >> 7;  // << 8 * Q15
    int32_t v3 = x - ic2_;
    int32_t v1 = static_cast<int32_t>(
        (static_cast<int64_t>(a1_) * ic1_ +
         static_cast<int64_t>(a2_) * v3) >> 30);
    int32_t v2 = ic2_ + static_cast<int32_t>(
        (static_cast<int64_t>(a2_) * ic1_ +
         static_cast<int64_t>(a3_) * v3) >> 30);
    ic1_ = ClampState(2 * v1 - ic1_);
    ic2_ = ClampState(2 * v2 - ic2_);

    int32_t y;
    if (mode_ == OUTPUT_FILTER_LP) {
      y = v2;
    } else if (mode_ == OUTPUT_FILTER_HP) {
      y = x - MulQ28(k_, v1) - v2;
    } else {
      y = MulQ28(k_bp_, v1);
    }
    y = SoftLimit(y >> 8);
    if (wet_ < kUnity) {
      y = in + ((y - in) * (wet_ >> 1) >> 14);  // >> 1 avoids overflow
    }
    return static_cast<int16_t>(y);
  }

 private:
  static const int32_t kUnity = 32768;
  static const int32_t kFadeStep = 171;           // 32768 / 192 = 2 ms @ 96k
  static const int32_t kOneQ28 = 1 << 28;
  static const int32_t kMaxDamping = 2 << 28;     // Q = 0.5
  static const int32_t kMinDamping = 8947849;     // 1/30 in Q28, Q = 30
  static const int32_t kMinBpGain = 1 << 26;      // 0.25 in Q28
  static const int32_t kStateLimit = 1 << 28;     // 32x full scale (<< 8)

  static inline int32_t MulQ28(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<int64_t>(a) * b >> 28);
  }

  static inline int32_t ClampState(int32_t x) {
    if (x > kStateLimit) return kStateLimit;
    if (x < -kStateLimit) return -kStateLimit;
    return x;
  }

  // Linear up to |x| = 16384, then a quadratic knee that reaches 32767
  // with zero slope at |x| = 49152; hard clip beyond.
  static inline int32_t SoftLimit(int32_t x) {
    const int32_t kThreshold = 16384;
    const int32_t kKnee = 32768;
    int32_t sign = x < 0 ? -1 : 1;
    int32_t a = x * sign;
    if (a > kThreshold) {
      int32_t e = a - kThreshold;
      if (e >= kKnee) {
        a = 32767;
      } else {
        a = kThreshold + e - ((e * e) >> 16);
        if (a > 32767) a = 32767;
      }
    }
    return a * sign;
  }

  void ComputeCoefficients() {
    // g from the LUT: 257 entries, half-semitone steps (64 pitch units).
    int32_t index = (cutoff_ >> 8) - kOutputFilterMinCutoff;
    int32_t i = index >> 6;
    int32_t frac = index & 63;
    if (i >= 256) {
      i = 255;
      frac = 64;
    }
    int32_t ga = lut_tpt_g[i];
    int32_t gb = lut_tpt_g[i + 1];
    int32_t g = ga + static_cast<int32_t>(
        static_cast<int64_t>(gb - ga) * frac >> 6);

    // k = kMin + (2 - kMin) * (1 - r)^2  -> exponential-feeling Q curve.
    int32_t inv = 32767 - resonance_;
    int32_t inv2 = inv * inv >> 15;  // Q15
    k_ = kMinDamping + static_cast<int32_t>(
        static_cast<int64_t>(kMaxDamping - kMinDamping) * inv2 >> 15);
    k_bp_ = k_ < kMinBpGain ? kMinBpGain : k_;

    // a1 = 1 / (1 + g (g + k)): D in Q28 (1..4), a1 in Q30.
    // Only 32-bit division (native UDIV on the M3): a 64-bit division would
    // pull __udivmoddi4 from libgcc, whose .ARM.exidx section the stmlib
    // linker script cannot place. D >> 14 is Q14, so the quotient
    // 2^32 / (D * 2^14) is a1 in Q18 (relative error < 2^-14).
    uint32_t d = kOneQ28 + static_cast<uint32_t>(
        static_cast<int64_t>(g) * (g + k_) >> 28);
    a1_ = static_cast<int32_t>((0xffffffffUL / (d >> 14)) << 12);
    a2_ = static_cast<int32_t>(static_cast<int64_t>(g) * a1_ >> 28);
    a3_ = static_cast<int32_t>(static_cast<int64_t>(g) * a2_ >> 28);

    // Input gain compensation (1 + k) / 3, Q15: 1.0 at Q = 0.5,
    // ~0.34 at max resonance. Keeps resonant peaks in the limiter's knee.
    // (1 + k) in Q15 is at most 98304; * 21845 (1/3 in Q16) fits 32 bits.
    uint32_t one_plus_k = static_cast<uint32_t>(kOneQ28 + k_) >> 13;
    input_gain_ = static_cast<int32_t>((one_plus_k * 21845UL) >> 16);
  }

  int32_t ic1_;
  int32_t ic2_;
  int32_t a1_;
  int32_t a2_;
  int32_t a3_;
  int32_t k_;
  int32_t k_bp_;
  int32_t input_gain_;

  int32_t cutoff_;     // pitch units << 8
  int32_t resonance_;  // 0..32767

  int32_t wet_;        // crossfade, 0..32768
  OutputFilterMode mode_;
  OutputFilterMode target_mode_;

  DISALLOW_COPY_AND_ASSIGN(OutputFilter);
};

}  // namespace braids

#endif  // BRAIDS_OUTPUT_FILTER_H_
