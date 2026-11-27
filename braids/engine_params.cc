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
// Names and defaults of the advanced engine parameters. Order must match
// MacroOscillatorShape. See macro_oscillator.cc / digital_oscillator.cc for
// what each one does.

#include "braids/engine_params.h"

namespace braids {

const char kEngineParamNames[MACRO_OSC_SHAPE_LAST][kNumEngineParams][5] = {
  { "DRIV", "UNIS", "SHFT" },  // CSAW
  { "TONE", "FUZZ", "OCT " },  // MORPH
  { "SQLV", "OCT ", "PWOF" },  // SAW_SQUARE
  { "RANG", "PW 1", "PW 2" },  // SQUARE_SYNC
  { "BIAS", "TRAK", "OCT2" },  // SINE_TRIANGLE (FOLD)
  { "DTUN", "MIX ", "OCT2" },  // BUZZ
  { "OCT ", "DRFT", "LEV1" },  // TRIPLE_SAW
  { "OCT ", "DRFT", "LEV1" },  // TRIPLE_SQUARE
  { "OCT ", "DRFT", "LEV1" },  // TRIPLE_TRIANGLE
  { "OCT ", "DRFT", "LEV1" },  // TRIPLE_SINE
  { "HPF ", "DRIV", "VOIC" },  // SAW_SWARM
  { "DAMP", "MIX ", "SAT " },  // SAW_COMB
  { "F1LV", "F2LV", "OCT " },  // VOSIM
  { "CONS", "NOIS", "DRIV" },  // VOWEL
  { "DECY", "BRIT", "SHFT" },  // VOWEL_FOF
  { "FDBK", "DTUN", "MODW" },  // FM
  { "FDBK", "DTUN", "ATTN" },  // FEEDBACK_FM
  { "CHAO", "DTUN", "INDX" },  // CHAOTIC_FEEDBACK_FM
  { "DAMP", "VOIC", "NOIS" },  // PLUCKED
  { "PRES", "BODY", "DAMP" },  // BOWED
  { "BRTH", "REED", "REFL" },  // BLOWN
  { "BRTH", "JET ", "DAMP" },  // FLUTED
  { "INHR", "BRIT", "ODD " },  // STRUCK_BELL
  { "WARP", "SYNC", "QUAN" },  // WAVETABLES
  { "WARP", "SYNC", "QUAN" },  // WAVE_MAP
  { "WARP", "SYNC", "QUAN" },  // WAVE_LINE
  { "WARP", "SYNC", "QUAN" },  // WAVE_PARAPHONIC
  { "SLEW", "RAND", "RATE" },  // CLOCKED_NOISE
  { "DENS", "WAVE", "SCAT" },  // GRANULAR_CLOUD
  { "VOIC", "WIND", "BRTH" },  // VOWEL_MORPH
  { "CHRP", "PULS", "OCT " },  // VOSIM_PLUS
  { "RATE", "WAVE", "VOIC" },  // DRIFT_SWARM
  { "RATE", "DPTH", "BRST" },  // FOLD_DRONE
  { "SHIM", "SRAT", "WIDT" },  // SPECTRAL_DRONE
  { "TUNE", "DCAY", "DTUN" },  // TANPURA
  { "BEAT", "DCAY", "SIZE" },  // SINGING_BOWL
  { "VOWL", "GLID", "BRTH" },  // OVERTONE_CHANT
  { "BLOM", "DCAY", "FDBK" },  // GONG
  { "----", "----", "----" },  // SILENCE
};

const uint8_t kEngineParamDefaults[MACRO_OSC_SHAPE_LAST][kNumEngineParams] = {
  { 64, 0, 64 },     // CSAW
  { 64, 64, 64 },    // MORPH
  { 64, 64, 0 },     // SAW_SQUARE
  { 64, 0, 0 },      // SQUARE_SYNC
  { 64, 64, 64 },    // SINE_TRIANGLE
  { 64, 64, 64 },    // BUZZ
  { 64, 0, 64 },     // TRIPLE_SAW
  { 64, 0, 64 },     // TRIPLE_SQUARE
  { 64, 0, 64 },     // TRIPLE_TRIANGLE
  { 64, 0, 64 },     // TRIPLE_SINE
  { 64, 127, 127 },  // SAW_SWARM
  { 0, 64, 0 },      // SAW_COMB
  { 64, 64, 64 },    // VOSIM
  { 64, 0, 64 },     // VOWEL
  { 64, 64, 64 },    // VOWEL_FOF
  { 0, 64, 0 },      // FM
  { 64, 64, 64 },    // FEEDBACK_FM
  { 64, 64, 64 },    // CHAOTIC_FEEDBACK_FM
  { 64, 127, 64 },   // PLUCKED
  { 64, 64, 64 },    // BOWED
  { 64, 64, 64 },    // BLOWN
  { 64, 64, 64 },    // FLUTED
  { 64, 64, 64 },    // STRUCK_BELL
  { 0, 0, 0 },       // WAVETABLES
  { 0, 0, 0 },       // WAVE_MAP
  { 0, 0, 0 },       // WAVE_LINE
  { 0, 0, 0 },       // WAVE_PARAPHONIC
  { 0, 0, 64 },      // CLOCKED_NOISE
  { 64, 0, 0 },      // GRANULAR_CLOUD
  { 32, 64, 0 },     // VOWEL_MORPH
  { 0, 127, 64 },    // VOSIM_PLUS
  { 64, 0, 127 },    // DRIFT_SWARM
  { 64, 0, 64 },     // FOLD_DRONE: no breathing LFO by default
  { 0, 64, 64 },     // SPECTRAL_DRONE: no shimmer by default
  { 0, 64, 16 },     // TANPURA: Pa, ~2.7 s decay, Sa detune
  { 32, 96, 64 },    // SINGING_BOWL: slow beating, long decay
  { 48, 64, 8 },     // OVERTONE_CHANT: "mmm", medium glide, a little breath
  { 64, 96, 0 },     // GONG: medium bloom, long decay, no feedback
  { 0, 0, 0 },       // SILENCE
};

}  // namespace braids
