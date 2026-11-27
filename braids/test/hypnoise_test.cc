// Copyright 2026 MutatedHypnoise contributors. MIT license (see braids.cc).
//
// -----------------------------------------------------------------------------
//
// Desktop tests for the MutatedHypnoise additions.
//
//   1. Every MacroOscillatorShape renders without crashing and produces
//      sound (except ZERO), which catches mismatches between the macro
//      shape enum and the digital oscillator fn_table_.
//   2. Parameter corners of the new engines, advanced parameters.
//   3. Listening file: hypnoise_engines.wav (each new engine, 8 s).
//
// Build and run from the repository root:
//   make -f braids/test/makefile hypnoise_test && ./hypnoise_test

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "braids/engine_params.h"
#include "braids/macro_oscillator.h"
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
  MACRO_OSC_SHAPE_TANPURA,
  MACRO_OSC_SHAPE_SINGING_BOWL,
  MACRO_OSC_SHAPE_OVERTONE_CHANT,
  MACRO_OSC_SHAPE_GONG,
};
static const char* const kNewEngineNames[] = {
  "VMRF", "VOS2", "DSWM", "FDRN", "SPEC", "TANP", "BOWL", "HOOM", "GONG" };
static const size_t kNumNewEngines = 9;

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
      if (!effective) {
        // Some parameters only act when another one is up (e.g. a rate
        // when its depth is 0 by default): retry with the others at 64.
        uint8_t context[3] = { 64, 64, 64 };
        const int16_t kPitch = 40 << 7;
        RenderEngine(static_cast<MacroOscillatorShape>(shape), context,
                     kPitch, reference, kBlocks);
        context[p] = 127;
        RenderEngine(static_cast<MacroOscillatorShape>(shape), context,
                     kPitch, modified, kBlocks);
        effective = memcmp(reference, modified, sizeof(reference)) != 0;
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

  printf("\nWrote hypnoise_engines.wav\n");
}

int main(void) {
  TestAllEngines();
  TestNewEngines();
  TestAdvancedParameters();
  RenderListeningFiles();
  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
         failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
