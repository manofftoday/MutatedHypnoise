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

#include "braids/engine_params.h"
#include "braids/macro_oscillator.h"
#include "braids/output_filter.h"
#include "stmlib/utils/random.h"

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

// The block version of OutputFilter::Process (fast path used by the
// firmware) must match the per-sample version exactly, across mode
// changes, crossfades and cutoff/resonance jumps.
static void TestFilterBlockEquivalence() {
  printf("\n-- Filter: block processing == per-sample processing --\n");
  OutputFilter a, b;
  a.Init();
  b.Init();
  srand(7);
  int mismatches = 0;
  for (int block = 0; block < 20000; ++block) {
    OutputFilterMode mode = static_cast<OutputFilterMode>((block / 700) % 4);
    int32_t cutoff = (12 << 7) + rand() % (128 << 7);
    int32_t resonance = (block / 1500) % 2 ? 32767 : rand() % 32768;
    a.Configure(mode, cutoff, resonance);
    b.Configure(mode, cutoff, resonance);
    int16_t x[kBlockSize], y[kBlockSize];
    for (size_t i = 0; i < kBlockSize; ++i) {
      x[i] = y[i] = static_cast<int16_t>((rand() % 65536) - 32768);
    }
    for (size_t i = 0; i < kBlockSize; ++i) {
      x[i] = a.Process(x[i]);
    }
    b.Process(y, kBlockSize);
    for (size_t i = 0; i < kBlockSize; ++i) {
      if (x[i] != y[i]) ++mismatches;
    }
  }
  Check(mismatches == 0, "block == per-sample (mismatching samples)",
        mismatches);
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
      osc.set_extras(kEngineParamDefaults[shape]);
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

static const MacroOscillatorShape kNewEngines[] = {
  MACRO_OSC_SHAPE_VOWEL_MORPH,
  MACRO_OSC_SHAPE_VOSIM_PLUS,
  MACRO_OSC_SHAPE_DRIFT_SWARM,
  MACRO_OSC_SHAPE_FOLD_DRONE,
  MACRO_OSC_SHAPE_SPECTRAL_DRONE,
};
static const char* const kNewEngineNames[] = {
  "VMRF", "VOS2", "DSWM", "FDRN", "SPEC" };
static const size_t kNumNewEngines = 5;

// Corners of TIMBRE/COLOR at a low and a high pitch, with triggers: no
// sustained clipping, no large DC offset, audible output.
static void TestNewEngines() {
  printf("\n-- New engines: parameter corners --\n");
  const int16_t kValues[] = { 0, 16384, 32767 };
  const int16_t kPitches[] = { 24 << 7, 60 << 7, 96 << 7 };
  for (size_t e = 0; e < kNumNewEngines; ++e) {
    double worst_clip = 0.0, worst_dc = 0.0, min_rms = 1e9;
    for (size_t p = 0; p < 3; ++p) {
      for (size_t t = 0; t < 3; ++t) {
        for (size_t c = 0; c < 3; ++c) {
          static MacroOscillator osc;
          osc.Init();
          osc.set_shape(kNewEngines[e]);
          double sum = 0.0, energy = 0.0;
          size_t clipped = 0, n = 0;
          for (size_t b = 0; b < kSampleRate / 2 / kBlockSize; ++b) {
            int16_t buffer[kBlockSize];
            uint8_t sync[kBlockSize];
            memset(sync, 0, sizeof(sync));
            if (b % 500 == 0) osc.Strike();
            osc.set_parameters(kValues[t], kValues[c]);
            osc.set_extras(kEngineParamDefaults[kNewEngines[e]]);
            osc.set_pitch(kPitches[p]);
            osc.Render(sync, buffer, kBlockSize);
            if (b < 100) continue;  // let smoothing / DC trackers settle
            for (size_t i = 0; i < kBlockSize; ++i, ++n) {
              sum += buffer[i];
              energy += double(buffer[i]) * buffer[i];
              if (buffer[i] >= 32767 || buffer[i] <= -32767) ++clipped;
            }
          }
          double rms = sqrt(energy / n);
          double dc = fabs(sum / n);
          double clip = double(clipped) / n;
          if (clip > worst_clip) worst_clip = clip;
          if (dc / 32768.0 > worst_dc) worst_dc = dc / 32768.0;
          if (rms < min_rms) min_rms = rms;
        }
      }
    }
    char label[64];
    snprintf(label, sizeof(label), "%s clipped sample ratio < 2%%",
             kNewEngineNames[e]);
    Check(worst_clip < 0.02, label, worst_clip);
    snprintf(label, sizeof(label), "%s DC offset < 10%% FS", kNewEngineNames[e]);
    Check(worst_dc < 0.10, label, worst_dc);
    snprintf(label, sizeof(label), "%s quietest corner rms > 300",
             kNewEngineNames[e]);
    Check(min_rms > 300.0, label, min_rms);
  }
}

// Renders one engine (fixed seed) and returns its output; used to check
// that each advanced parameter does something and never kills the sound.
static void RenderEngine(MacroOscillatorShape shape, const uint8_t* extras,
                         int16_t pitch, int16_t* out, size_t num_blocks) {
  static MacroOscillator osc;
  stmlib::Random::Seed(4321);
  osc.Init();
  osc.set_shape(shape);
  for (size_t b = 0; b < num_blocks; ++b) {
    uint8_t sync[kBlockSize];
    memset(sync, 0, sizeof(sync));
    if (b % 250 == 0) osc.Strike();
    osc.set_parameters(12000, 20000);
    osc.set_extras(extras);
    osc.set_pitch(pitch);
    osc.Render(sync, out + b * kBlockSize, kBlockSize);
  }
}

static void TestAdvancedParameters() {
  printf("\n-- Advanced parameters: extremes change the sound, never mute --\n");
  const size_t kBlocks = 1000;
  static int16_t reference[kBlocks * kBlockSize];
  static int16_t modified[kBlocks * kBlockSize];
  int tested = 0, ineffective = 0, muted = 0;
  for (int shape = 0; shape < MACRO_OSC_SHAPE_SILENCE; ++shape) {
    for (int p = 0; p < kNumEngineParams; ++p) {
      bool effective = false;
      for (int pitch_index = 0; pitch_index < 3; ++pitch_index) {
        const int16_t kPitches[] = { 40 << 7, 72 << 7, 96 << 7 };
        int16_t pitch = kPitches[pitch_index];
        RenderEngine(static_cast<MacroOscillatorShape>(shape),
                     kEngineParamDefaults[shape], pitch, reference, kBlocks);
        const uint8_t extremes[] = { 0, 127 };
        for (int x = 0; x < 2; ++x) {
          uint8_t extras[3];
          memcpy(extras, kEngineParamDefaults[shape], 3);
          extras[p] = extremes[x];
          RenderEngine(static_cast<MacroOscillatorShape>(shape), extras,
                       pitch, modified, kBlocks);
          double energy = 0.0;
          for (size_t i = 0; i < kBlocks * kBlockSize; ++i) {
            if (modified[i] != reference[i]) effective = true;
            energy += double(modified[i]) * modified[i];
          }
          double rms = sqrt(energy / (kBlocks * kBlockSize));
          if (rms < 30.0) {
            ++muted;
            printf("  quiet: shape %d %s=%d pitch %d rms %.1f\n", shape,
                   kEngineParamNames[shape][p], extremes[x], pitch >> 7, rms);
          }
        }
      }
      ++tested;
      if (!effective) {
        ++ineffective;
        printf("  no effect: shape %d %s\n", shape,
               kEngineParamNames[shape][p]);
      }
    }
  }
  char label[64];
  snprintf(label, sizeof(label), "%d parameters: all change the sound", tested);
  Check(ineffective == 0, label, ineffective);
  Check(muted == 0, "no parameter extreme mutes an engine", muted);
}

static void RenderListeningFiles() {
  static MacroOscillator osc;
  OutputFilter f;

  // hypnoise_engines.wav: each new engine for 8 s at C2. 0-4 s: TIMBRE
  // sweeps 0 -> max (COLOR center); 4-8 s: COLOR sweeps 0 -> max, with a
  // trigger every 0.5 s.
  FILE* fp = fopen("hypnoise_engines.wav", "wb");
  const uint32_t kEngineBlocks = 8 * kSampleRate / kBlockSize;
  WriteWavHeader(fp, kNumNewEngines * kEngineBlocks * kBlockSize);
  for (size_t e = 0; e < kNumNewEngines; ++e) {
    osc.Init();
    osc.set_shape(kNewEngines[e]);
    for (uint32_t b = 0; b < kEngineBlocks; ++b) {
      uint32_t half = kEngineBlocks / 2;
      int16_t timbre = b < half ? int16_t(32767.0 * b / half) : 16384;
      int16_t color = b < half ? 16384 : int16_t(32767.0 * (b - half) / half);
      if (b >= half && (b - half) % (kSampleRate / 2 / kBlockSize) == 0) {
        osc.Strike();
      }
      int16_t buffer[kBlockSize];
      uint8_t sync[kBlockSize];
      memset(sync, 0, sizeof(sync));
      osc.set_parameters(timbre, color);
      osc.set_extras(kEngineParamDefaults[kNewEngines[e]]);
      osc.set_pitch(36 << 7);
      osc.Render(sync, buffer, kBlockSize);
      fwrite(buffer, sizeof(int16_t), kBlockSize, fp);
    }
  }
  fclose(fp);

  // filter_sweep.wav: 3 x 4 s, exponential sweep 30 Hz -> 15 kHz -> 30 Hz,
  // resonance 100/127, over a CSAW at C2.
  fp = fopen("filter_sweep.wav", "wb");
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
      osc.set_extras(kEngineParamDefaults[MACRO_OSC_SHAPE_CSAW]);
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
    osc.set_extras(kEngineParamDefaults[MACRO_OSC_SHAPE_SAW_SWARM]);
    osc.set_pitch(43 << 7);
    osc.Render(sync, buffer, kBlockSize);
    for (size_t i = 0; i < kBlockSize; ++i) buffer[i] = f.Process(buffer[i]);
    fwrite(buffer, sizeof(int16_t), kBlockSize, fp);
  }
  fclose(fp);
  printf("\nWrote hypnoise_engines.wav, filter_sweep.wav, filter_stabs.wav\n");
}

int main(void) {
  TestFilterResponse();
  TestFilterStability();
  TestFilterBlockEquivalence();
  TestAllEngines();
  TestNewEngines();
  TestAdvancedParameters();
  RenderListeningFiles();
  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
         failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
