// Copyright 2026 MutatedHypnoise contributors. MIT license (see braids.cc).
//
// -----------------------------------------------------------------------------
//
// Desktop tests for the MutatedHypnoise additions.
//
//   1. OutputFilter: measured gain at the cutoff for LP/HP/BP, stability under
//      maximum resonance and fast modulation, decay to silence (no limit
//      cycles), bounded output.
//   2. Every MacroOscillatorShape renders without crashing and produces
//      sound (except ZERO), which catches mismatches between the macro
//      shape enum and the digital oscillator fn_table_.
//   3. Listening files: filter_sweep.wav (LP, HP, BP sweeps over CSAW) and
//      filter_stabs.wav (envelope-style cutoff stabs, hypnotic techno style).
//
// Build and run from the repository root:
//   make -f braids/test/makefile hypnoise_test && ./hypnoise_test

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "braids/macro_oscillator.h"
#include "braids/output_filter.h"

using namespace braids;

const uint32_t kSampleRate = 96000;
const size_t kBlockSize = 24;

static int failures = 0;

static void Check(bool condition, const char* what, double value) {
  printf("%s %-48s %10.4f\n", condition ? "PASS" : "FAIL", what, value);
  if (!condition) ++failures;
}

static void WriteWavHeader(FILE* fp, uint32_t num_samples) {
  uint32_t l;
  uint16_t s;
  fwrite("RIFF", 4, 1, fp);
  l = 36 + num_samples * 2; fwrite(&l, 4, 1, fp);
  fwrite("WAVE", 4, 1, fp);
  fwrite("fmt ", 4, 1, fp);
  l = 16; fwrite(&l, 4, 1, fp);
  s = 1; fwrite(&s, 2, 1, fp);  // PCM
  s = 1; fwrite(&s, 2, 1, fp);  // mono
  l = kSampleRate; fwrite(&l, 4, 1, fp);
  l = kSampleRate * 2; fwrite(&l, 4, 1, fp);
  s = 2; fwrite(&s, 2, 1, fp);
  s = 16; fwrite(&s, 2, 1, fp);
  fwrite("data", 4, 1, fp);
  l = num_samples * 2; fwrite(&l, 4, 1, fp);
}

// Pitch units (semitone << 7) for a frequency in Hz.
static int32_t PitchFromHz(double hz) {
  return static_cast<int32_t>(
      (69.0 + 12.0 * log2(hz / 440.0)) * 128.0 + 0.5);
}

// Feeds a sine at `hz` through a settled filter, returns RMS gain.
static double MeasureGain(OutputFilterMode mode, int32_t cutoff,
                          int32_t resonance, double hz) {
  OutputFilter f;
  f.Init();
  const double amplitude = 8000.0;
  double phase = 0.0;
  double in_energy = 0.0, out_energy = 0.0;
  const size_t kSettleBlocks = 400;   // 100 ms: smoothing + crossfade
  const size_t kMeasureBlocks = 800;  // 200 ms
  for (size_t b = 0; b < kSettleBlocks + kMeasureBlocks; ++b) {
    f.Configure(mode, cutoff, resonance);
    for (size_t i = 0; i < kBlockSize; ++i) {
      int16_t x = static_cast<int16_t>(amplitude * sin(phase));
      phase += 2.0 * M_PI * hz / kSampleRate;
      int16_t y = f.Process(x);
      if (b >= kSettleBlocks) {
        in_energy += double(x) * x;
        out_energy += double(y) * y;
      }
    }
  }
  return sqrt(out_energy / in_energy);
}

static void TestFilterResponse() {
  printf("\n-- OutputFilter frequency response (Q = 0.5, fc = 1 kHz) --\n");
  const int32_t fc = PitchFromHz(1000.0);
  // TPT SVF with k = 2: |LP(fc)| = |HP(fc)| = 1/k = 0.5, normalized BP = 1.
  double lp = MeasureGain(OUTPUT_FILTER_LP, fc, 0, 1000.0);
  double hp = MeasureGain(OUTPUT_FILTER_HP, fc, 0, 1000.0);
  double bp = MeasureGain(OUTPUT_FILTER_BP, fc, 0, 1000.0);
  Check(fabs(lp - 0.5) < 0.03, "LP gain at fc ~ 0.5", lp);
  Check(fabs(hp - 0.5) < 0.03, "HP gain at fc ~ 0.5", hp);
  Check(fabs(bp - 1.0) < 0.05, "BP gain at fc ~ 1.0", bp);

  double lp_pass = MeasureGain(OUTPUT_FILTER_LP, fc, 0, 100.0);
  double lp_stop = MeasureGain(OUTPUT_FILTER_LP, fc, 0, 10000.0);
  double hp_pass = MeasureGain(OUTPUT_FILTER_HP, fc, 0, 10000.0);
  double hp_stop = MeasureGain(OUTPUT_FILTER_HP, fc, 0, 100.0);
  Check(lp_pass > 0.9, "LP passes 100 Hz", lp_pass);
  Check(lp_stop < 0.02, "LP rejects 10 kHz (12 dB/oct)", lp_stop);
  Check(hp_pass > 0.9, "HP passes 10 kHz", hp_pass);
  Check(hp_stop < 0.02, "HP rejects 100 Hz", hp_stop);

  // Resonance: peak at fc must rise well above the Q = 0.5 case.
  double lp_res = MeasureGain(OUTPUT_FILTER_LP, fc, 127 << 8, 1000.0);
  Check(lp_res > 2.0, "LP max resonance peak > 2x", lp_res);

  // Extremes of the cutoff range stay well-behaved.
  double low = MeasureGain(OUTPUT_FILTER_LP, PitchFromHz(20.0), 0, 20.0);
  double high = MeasureGain(OUTPUT_FILTER_HP, 140 << 7, 0, 1000.0);
  Check(fabs(low - 0.5) < 0.05, "LP gain at 20 Hz cutoff ~ 0.5", low);
  Check(high < 0.01, "HP at top of range rejects 1 kHz", high);
}

static void TestFilterStability() {
  printf("\n-- OutputFilter stability --\n");
  const OutputFilterMode modes[] = {
    OUTPUT_FILTER_LP, OUTPUT_FILTER_HP, OUTPUT_FILTER_BP };
  for (size_t m = 0; m < 3; ++m) {
    OutputFilter f;
    f.Init();
    uint32_t rng = 12345;
    int32_t peak = 0;
    // 2 s of full-scale noise, max resonance, cutoff jumping every block
    // across the whole range (worst case for zipper and instability).
    for (size_t b = 0; b < 2 * kSampleRate / kBlockSize; ++b) {
      rng = rng * 1664525 + 1013904223;
      int32_t cutoff = 12 << 7;
      cutoff += static_cast<int32_t>((rng >> 8) % (128 << 7));
      f.Configure(modes[m], cutoff, 127 << 8);
      for (size_t i = 0; i < kBlockSize; ++i) {
        rng = rng * 1664525 + 1013904223;
        int16_t x = static_cast<int16_t>(rng >> 16);
        int32_t y = f.Process(x);
        if (abs(y) > peak) peak = abs(y);
      }
    }
    // Then silence: the output must decay to zero (no limit cycles).
    int32_t tail = 0;
    for (size_t b = 0; b < kSampleRate / kBlockSize; ++b) {
      f.Configure(modes[m], 60 << 7, 127 << 8);
      for (size_t i = 0; i < kBlockSize; ++i) {
        int32_t y = f.Process(0);
        if (b > (kSampleRate / kBlockSize) / 2 && abs(y) > tail) {
          tail = abs(y);
        }
      }
    }
    const char* names[] = { "LP", "HP", "BP" };
    char label[64];
    snprintf(label, sizeof(label), "%s bounded under noise + modulation", names[m]);
    Check(peak <= 32767, label, peak);
    snprintf(label, sizeof(label), "%s decays to silence (max |y| tail)", names[m]);
    Check(tail <= 2, label, tail);
  }

  // OFF -> LP -> OFF must not jump: max step between samples during the
  // crossfade stays small for a constant (DC) input.
  OutputFilter f;
  f.Init();
  int32_t previous = 0, max_step = 0;
  for (size_t b = 0; b < 2000; ++b) {
    OutputFilterMode mode = (b >= 500 && b < 1500) ? OUTPUT_FILTER_HP
                                                    : OUTPUT_FILTER_OFF;
    f.Configure(mode, 100 << 7, 0);
    for (size_t i = 0; i < kBlockSize; ++i) {
      int32_t y = f.Process(10000);
      if (b > 10 && abs(y - previous) > max_step) max_step = abs(y - previous);
      previous = y;
    }
  }
  Check(max_step < 200, "mode change crossfade: max sample step", max_step);
}

static void TestAllEngines() {
  printf("\n-- Every engine renders --\n");
  for (int shape = 0; shape < MACRO_OSC_SHAPE_LAST; ++shape) {
    static MacroOscillator osc;
    osc.Init();
    osc.set_shape(static_cast<MacroOscillatorShape>(shape));
    double energy = 0.0;
    for (size_t b = 0; b < kSampleRate / 4 / kBlockSize; ++b) {
      int16_t buffer[kBlockSize];
      uint8_t sync[kBlockSize];
      memset(sync, 0, sizeof(sync));
      if (b % 400 == 0) osc.Strike();
      osc.set_parameters(16384, 16384);
      osc.set_pitch(48 << 7);
      osc.Render(sync, buffer, kBlockSize);
      for (size_t i = 0; i < kBlockSize; ++i) {
        energy += double(buffer[i]) * buffer[i];
      }
    }
    double rms = sqrt(energy / (kSampleRate / 4));
    char label[64];
    snprintf(label, sizeof(label), "shape %2d renders %s", shape,
             shape == MACRO_OSC_SHAPE_SILENCE ? "silence" : "sound");
    Check(shape == MACRO_OSC_SHAPE_SILENCE ? rms == 0.0 : rms > 10.0,
          label, rms);
  }
}

static void RenderListeningFiles() {
  static MacroOscillator osc;
  OutputFilter f;

  // filter_sweep.wav: 3 x 4 s, exponential sweep 30 Hz -> 15 kHz -> 30 Hz,
  // resonance 100/127, over a CSAW at C2.
  FILE* fp = fopen("filter_sweep.wav", "wb");
  const uint32_t kSweepBlocks = 4 * kSampleRate / kBlockSize;
  WriteWavHeader(fp, 3 * kSweepBlocks * kBlockSize);
  const OutputFilterMode modes[] = {
    OUTPUT_FILTER_LP, OUTPUT_FILTER_HP, OUTPUT_FILTER_BP };
  for (size_t m = 0; m < 3; ++m) {
    osc.Init();
    osc.set_shape(MACRO_OSC_SHAPE_CSAW);
    f.Init();
    for (uint32_t b = 0; b < kSweepBlocks; ++b) {
      double t = double(b) / kSweepBlocks;
      double tri = t < 0.5 ? 2.0 * t : 2.0 - 2.0 * t;
      int32_t cutoff = PitchFromHz(30.0 * pow(500.0, tri));
      f.Configure(modes[m], cutoff, 100 << 8);
      int16_t buffer[kBlockSize];
      uint8_t sync[kBlockSize];
      memset(sync, 0, sizeof(sync));
      osc.set_parameters(8000, 16384);
      osc.set_pitch(36 << 7);
      osc.Render(sync, buffer, kBlockSize);
      for (size_t i = 0; i < kBlockSize; ++i) buffer[i] = f.Process(buffer[i]);
      fwrite(buffer, sizeof(int16_t), kBlockSize, fp);
    }
  }
  fclose(fp);

  // filter_stabs.wav: 8 s at 130 BPM, 16th-note exponential-decay
  // envelope on the LP cutoff (what FEN1 + TRIG do on the module).
  fp = fopen("filter_stabs.wav", "wb");
  const uint32_t kStabBlocks = 8 * kSampleRate / kBlockSize;
  WriteWavHeader(fp, kStabBlocks * kBlockSize);
  osc.Init();
  osc.set_shape(MACRO_OSC_SHAPE_SAW_SWARM);
  f.Init();
  const uint32_t kSixteenth = kSampleRate * 60 / 130 / 4 / kBlockSize;
  double env = 0.0;
  for (uint32_t b = 0; b < kStabBlocks; ++b) {
    if (b % kSixteenth == 0) env = 1.0;
    env *= 0.9965;
    int32_t cutoff = (40 << 7) + static_cast<int32_t>(env * (60 << 7));
    f.Configure(OUTPUT_FILTER_LP, cutoff, 110 << 8);
    int16_t buffer[kBlockSize];
    uint8_t sync[kBlockSize];
    memset(sync, 0, sizeof(sync));
    osc.set_parameters(20000, 12000);
    osc.set_pitch(43 << 7);
    osc.Render(sync, buffer, kBlockSize);
    for (size_t i = 0; i < kBlockSize; ++i) buffer[i] = f.Process(buffer[i]);
    fwrite(buffer, sizeof(int16_t), kBlockSize, fp);
  }
  fclose(fp);
  printf("\nWrote filter_sweep.wav and filter_stabs.wav\n");
}

int main(void) {
  TestFilterResponse();
  TestFilterStability();
  TestAllEngines();
  RenderListeningFiles();
  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
         failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
