// Copyright 2012 Olivier Gillet, 2015 Tim Churches
//
// Author: Olivier Gillet (ol.gillet@gmail.com)
// Modifications: Tim Churches (tim.churches@gmail.com)
// Modifications may be determined by examining the differences between the last commit 
// by Olivier Gillet (pichenettes) and the HEAD commit at 
// https://github.com/timchurches/Mutated-Mutables/tree/master/braids 
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
// Macro-oscillator.

#include "braids/macro_oscillator.h"

#include <algorithm>

#include "stmlib/utils/dsp.h"

#include "braids/parameter_interpolation.h"
#include "braids/resources.h"

#include "stmlib/utils/random.h"

namespace braids {

using namespace stmlib;

// Advanced engine parameters (extra_[], 0..127, see engine_params.cc).
// Each default reproduces the original engine exactly.

// "OCT" parameters: 0..42 = -1 octave, 43..85 = 0, 86..127 = +1 octave.
static inline int32_t OctaveShift(uint8_t value) {
  return value < 43 ? -(12 << 7) : (value < 86 ? 0 : (12 << 7));
}

void MacroOscillator::Render(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  RenderFn fn = fn_table_[shape_];
  (this->*fn)(sync, buffer, size);
}

// CSAW. DRIV: gain into the overdrive (64 = 3x). UNIS: second CSAW detuned
// up to +1 semitone (0 = off). SHFT: depth of the COLOR DC shift.
void MacroOscillator::RenderCSaw(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  int16_t notch = std::max(parameter_[0] >> 9, 3);
  int16_t shift = (parameter_[1] - 16384) >> 1;
  if (extra_[2] != 64) {
    shift = shift * extra_[2] >> 6;
  }
  analog_oscillator_[0].set_pitch(pitch_);
  analog_oscillator_[0].set_shape(OSC_SHAPE_CSAW);
  analog_oscillator_[0].set_parameter(notch);
  analog_oscillator_[0].set_aux_parameter(shift);
  analog_oscillator_[0].Render(sync, buffer, NULL, size);
  if (extra_[1]) {
    analog_oscillator_[1].set_pitch(pitch_ + extra_[1]);
    analog_oscillator_[1].set_shape(OSC_SHAPE_CSAW);
    analog_oscillator_[1].set_parameter(notch);
    analog_oscillator_[1].set_aux_parameter(shift);
    analog_oscillator_[1].Render(sync, temp_buffer_, NULL, size);
    for (uint8_t i = 0; i < size; ++i) {
      buffer[i] = (buffer[i] + temp_buffer_[i]) >> 1;
    }
  }
  int32_t drive = extra_[0] * 3;  // 192 = 3.0 (>> 6)
  while (size--) {
    int32_t signal_amplified = *buffer * drive >> 6;
    signal_amplified = (signal_amplified + (shift >> 1) + 2048) >> 1;
    if (drive > 192) {
      CLIP(signal_amplified)  // the original (DRIV = 64) does not clip
    }
    *buffer = Interpolate88(ws_moderate_overdrive, signal_amplified + 32768);
    buffer++;
  }
}

// MORPH. TONE: offset of the low-pass. FUZZ: depth of the fuzz blend.
// OCT: octave (64 = original, one octave up).
void MacroOscillator::RenderMorph(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  uint8_t half_size = size >> 1;
    for (uint8_t i = 0; i < half_size; ++i) {
      sync_buffer_[i] = sync[i << 1] | sync[(i << 1) + 1];
  }

  int32_t transposition = (12 << 7) + OctaveShift(extra_[2]);
  analog_oscillator_[0].set_pitch(pitch_ + transposition);
  analog_oscillator_[1].set_pitch(pitch_ + transposition);

  uint16_t balance;
  if (parameter_[0] <= 10922) {
    analog_oscillator_[0].set_parameter(0);
    analog_oscillator_[1].set_parameter(0);
    analog_oscillator_[0].set_shape(OSC_SHAPE_TRIANGLE);
    analog_oscillator_[1].set_shape(OSC_SHAPE_SAW);
    balance = parameter_[0] * 6;
  } else if (parameter_[0] <= 21845) {
    analog_oscillator_[0].set_parameter(0);
    analog_oscillator_[1].set_parameter(0);
    analog_oscillator_[0].set_shape(OSC_SHAPE_SQUARE);
    analog_oscillator_[1].set_shape(OSC_SHAPE_SAW);
    balance = 65535 - (parameter_[0] - 10923) * 6;
  } else {
    analog_oscillator_[0].set_parameter((parameter_[0] - 21846) * 3);
    analog_oscillator_[1].set_parameter(0);
    analog_oscillator_[0].set_shape(OSC_SHAPE_SQUARE);
    analog_oscillator_[1].set_shape(OSC_SHAPE_SINE);
    balance = 0;
  }

  int16_t* shape_1 = temp_buffer_;
  int16_t* shape_2 = temp_buffer_ + half_size;
  analog_oscillator_[0].Render(sync_buffer_, shape_1, NULL, half_size);
  analog_oscillator_[1].Render(sync_buffer_, shape_2, NULL, half_size);

  int32_t lp_cutoff = pitch_ - (parameter_[1] >> 1) + 128 * 128 +
      (static_cast<int32_t>(extra_[0]) - 64) * 64;
  if (lp_cutoff < 0) {
    lp_cutoff = 0;
  } else if (lp_cutoff > 32767) {
    lp_cutoff = 32767;
  }
  int32_t f = Interpolate824(lut_svf_cutoff, lp_cutoff << 17);
  int32_t lp_state = lp_state_;
  uint16_t fuzz = parameter_[1] << 1;
  if (extra_[1] != 64) {
    int32_t b = static_cast<int32_t>(fuzz) * extra_[1] >> 6;
    fuzz = b > 65535 ? 65535 : b;
  }

  while (size) {
    int16_t sample = Mix(*shape_1, *shape_2, balance);
    int32_t shifted_sample = sample;
    shifted_sample += (parameter_[1] >> 2) + (parameter_[0] >> 4);

    lp_state += (sample - lp_state) * f >> 15;
    CLIP(lp_state)
    shifted_sample = lp_state + 32768;

    int16_t fuzzed = Interpolate88(ws_moderate_overdrive, shifted_sample);
    buffer[1] = Mix(sample, fuzzed, fuzz);
    buffer[0] = (buffer[1] + previous_sample_) >> 1;
    previous_sample_ = buffer[1];
    buffer += 2;
    size -= 2;
    ++shape_1;
    ++shape_2;
  }
  lp_state_ = lp_state;
}

// SAW/SQUARE. SQLV: square level. OCT: octave (64 = original, +1 octave).
// PWOF: extra pulse width on the square.
void MacroOscillator::RenderSawSquare(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  uint8_t half_size = size >> 1;
  for (uint8_t i = 0; i < half_size; ++i) {
    sync_buffer_[i] = sync[i << 1] | sync[(i << 1) + 1];
  }

  int32_t square_pw = parameter_[0] + (extra_[2] << 8);
  if (square_pw > 32767) {
    square_pw = 32767;
  }
  int32_t transposition = (12 << 7) + OctaveShift(extra_[1]);
  analog_oscillator_[0].set_parameter(parameter_[0]);
  analog_oscillator_[1].set_parameter(square_pw);
  analog_oscillator_[0].set_pitch(pitch_ + transposition);
  analog_oscillator_[1].set_pitch(pitch_ + transposition);

  analog_oscillator_[0].set_shape(OSC_SHAPE_SAW);
  analog_oscillator_[1].set_shape(OSC_SHAPE_SQUARE);

  int16_t* saw_buffer = temp_buffer_;
  int16_t* square_buffer = temp_buffer_ + half_size;

  analog_oscillator_[0].Render(sync_buffer_, saw_buffer, NULL, half_size);
  analog_oscillator_[1].Render(sync_buffer_, square_buffer, NULL, half_size);

  int32_t square_level = 148 * extra_[0] >> 6;

  BEGIN_INTERPOLATE_PARAMETER_1

  size_t i = 0;
  while (size--) {
    INTERPOLATE_PARAMETER_1
    uint16_t balance = parameter_1 << 1;
    int32_t attenuated_square = static_cast<int32_t>(
        square_buffer[i >> 1]) * square_level >> 8;
    CLIP(attenuated_square)
    *buffer++ = Mix(saw_buffer[i >> 1], attenuated_square, balance);
    ++i;
  }

  END_INTERPOLATE_PARAMETER_1
}

#define SEMI * 128

const int16_t intervals[65] = {
  -24 SEMI, -24 SEMI, -24 SEMI + 4,
  -23 SEMI, -22 SEMI, -21 SEMI, -20 SEMI, -19 SEMI, -18 SEMI,
  -17 SEMI - 4, -17 SEMI,
  -16 SEMI, -15 SEMI, -14 SEMI, -13 SEMI,
  -12 SEMI - 4, -12 SEMI,
  -11 SEMI, -10 SEMI, -9 SEMI, -8 SEMI,
  -7 SEMI - 4, -7 SEMI,
  -6 SEMI, -5 SEMI, -4 SEMI, -3 SEMI, -2 SEMI, -1 SEMI,
  -24, -8, -4, 0, 4, 8, 24,
  1 SEMI, 2 SEMI, 3 SEMI, 4 SEMI, 5 SEMI, 6 SEMI,
  7 SEMI, 7 SEMI + 4,
  8 SEMI, 9 SEMI, 10 SEMI, 11 SEMI,
  12 SEMI, 12 SEMI + 4,
  13 SEMI, 14 SEMI, 15 SEMI, 16 SEMI,
  17 SEMI, 17 SEMI + 4,
  18 SEMI, 19 SEMI, 20 SEMI, 21 SEMI, 22 SEMI, 23 SEMI,
  24 SEMI - 4, 24 SEMI, 24 SEMI
};

// TRIPLE_*. OCT: octave of the chord. DRFT: each voice wanders on its own
// slow random walk (up to ~1 semitone). LEV1: level of the root voice.
void MacroOscillator::ConfigureTriple(
    AnalogOscillatorShape shape,
    int32_t transposition) {
  analog_oscillator_[0].set_parameter(0);
  analog_oscillator_[1].set_parameter(0);
  analog_oscillator_[2].set_parameter(0);

  transposition += OctaveShift(extra_[0]);
  int32_t drift[3] = { 0, 0, 0 };
  if (extra_[1]) {
    for (uint8_t i = 0; i < 3; ++i) {
      // ~0.5 s correlation time at the 4 kHz block rate.
      drift_[i] += (Random::GetSample() >> 5) - (drift_[i] >> 11);
      drift[i] = (drift_[i] >> 8) * extra_[1] >> 6;
    }
  }

  analog_oscillator_[0].set_pitch(pitch_ + transposition + drift[0]);
  for (uint8_t i = 0; i < 2; ++i) {
    int16_t detune_1 = intervals[parameter_[i] >> 9];
    int16_t detune_2 = intervals[((parameter_[i] >> 8) + 1) >> 1];
    uint16_t xfade = parameter_[i] << 8;
    int16_t detune = detune_1 + ((detune_2 - detune_1) * xfade >> 16);
    analog_oscillator_[i + 1].set_pitch(
        pitch_ + transposition + detune + drift[i + 1]);
  }

  analog_oscillator_[0].set_shape(shape);
  analog_oscillator_[1].set_shape(shape);
  analog_oscillator_[2].set_shape(shape);
}

void MacroOscillator::RenderTripleSawSquare(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  ConfigureTriple(shape_ == MACRO_OSC_SHAPE_TRIPLE_SAW
      ? OSC_SHAPE_SAW : OSC_SHAPE_SQUARE, 12 << 7);

  // Use half the sample rate.
  uint8_t half_size = size >> 1;

  // Downsample the sync buffer.
  for (uint8_t i = 0; i < half_size; ++i) {
    sync_buffer_[i] = sync[i << 1] | sync[(i << 1) + 1];
  }
  int16_t* voice_1_buffer = buffer + half_size;
  int16_t* voice_2_buffer = temp_buffer_;
  int16_t* voice_3_buffer = temp_buffer_ + half_size;

  analog_oscillator_[0].Render(sync_buffer_, voice_1_buffer, NULL, half_size);
  analog_oscillator_[1].Render(sync_buffer_, voice_2_buffer, NULL, half_size);
  analog_oscillator_[2].Render(sync_buffer_, voice_3_buffer, NULL, half_size);

  int32_t root_level = 4 * extra_[2];  // 256 = 4 / 8 (>> 9)
  for (uint8_t i = 0; i < size; i += 2) {
    int32_t sample = 0;
    sample += static_cast<int32_t>(voice_1_buffer[i >> 1]) * root_level >> 9;
    sample += static_cast<int32_t>(voice_2_buffer[i >> 1]) * 5 >> 3;
    sample += static_cast<int32_t>(voice_3_buffer[i >> 1]) * 5 >> 3;
    CLIP(sample);
    buffer[i] = sample;
    buffer[i + 1] = sample;
  }
}

void MacroOscillator::RenderTripleSineTriangle(const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  ConfigureTriple(shape_ == MACRO_OSC_SHAPE_TRIPLE_TRIANGLE ?
      OSC_SHAPE_TRIANGLE : OSC_SHAPE_SINE, 0);
  std::fill(&buffer[0], &buffer[size], 0);
  for (uint8_t i = 0; i < 3; ++i) {
    analog_oscillator_[i].Render(sync, temp_buffer_, NULL, size);
    int32_t level = i == 0 ? 21 * extra_[2] : 21 << 6;  // >> 12
    for (uint8_t j = 0; j < size; ++j) {
      buffer[j] += temp_buffer_[j] * level >> 12;
    }
  }
}

// SQUARE SYNC. RANG: range of the slave detune. PW 1 / PW 2: pulse width
// of the master / slave.
void MacroOscillator::RenderSquareSync(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  analog_oscillator_[0].set_parameter(extra_[1] << 8);
  analog_oscillator_[0].set_shape(OSC_SHAPE_SQUARE);
  analog_oscillator_[0].set_pitch(pitch_);

  analog_oscillator_[1].set_parameter(extra_[2] << 8);
  analog_oscillator_[1].set_shape(OSC_SHAPE_SQUARE);
  analog_oscillator_[1].set_pitch(
      pitch_ + ((parameter_[0] >> 2) * extra_[0] >> 6));

  analog_oscillator_[0].Render(sync, buffer, sync_buffer_, size);
  analog_oscillator_[1].Render(sync_buffer_, temp_buffer_, NULL, size);

  BEGIN_INTERPOLATE_PARAMETER_1

  int16_t* temp_buffer = temp_buffer_;
  while (size--) {
    INTERPOLATE_PARAMETER_1
    uint16_t balance = parameter_1 << 1;

    *buffer = Mix(*buffer, *temp_buffer, balance);
    buffer++;
    temp_buffer++;
  }

  END_INTERPOLATE_PARAMETER_1
}

// FOLD. BIAS: offset before the folders (even harmonics, 64 = none).
// TRAK: how much the folding is reduced at high pitches (anti-aliasing).
// OCT2: octave of the triangle folder.
void MacroOscillator::RenderSineTriangle(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  int32_t tracking = extra_[1];
  int32_t attenuation_sine =
      32767 - (6 * (pitch_ - (92 << 7)) * tracking >> 6);
  int32_t attenuation_tri =
      32767 - (7 * (pitch_ - (80 << 7)) * tracking >> 6);
  if (attenuation_tri < 0) attenuation_tri = 0;
  if (attenuation_sine < 0) attenuation_sine = 0;
  if (attenuation_tri > 32767) attenuation_tri = 32767;
  if (attenuation_sine > 32767) attenuation_sine = 32767;

  int32_t timbre = parameter_[0];
  int16_t bias = (static_cast<int32_t>(extra_[0]) - 64) << 8;

  analog_oscillator_[0].set_parameter(timbre * attenuation_sine >> 15);
  analog_oscillator_[1].set_parameter(timbre * attenuation_tri >> 15);
  analog_oscillator_[0].set_aux_parameter(bias);
  analog_oscillator_[1].set_aux_parameter(bias);
  analog_oscillator_[0].set_pitch(pitch_);
  analog_oscillator_[1].set_pitch(pitch_ + OctaveShift(extra_[2]));

  analog_oscillator_[0].set_shape(OSC_SHAPE_SINE_FOLD);
  analog_oscillator_[1].set_shape(OSC_SHAPE_TRIANGLE_FOLD);

  analog_oscillator_[0].Render(sync, buffer, NULL, size);
  analog_oscillator_[1].Render(sync, temp_buffer_, NULL, size);

  int16_t* temp_buffer = temp_buffer_;

  BEGIN_INTERPOLATE_PARAMETER_1

  while (size--) {
    INTERPOLATE_PARAMETER_1
    uint16_t balance = parameter_1 << 1;

    *buffer = Mix(*buffer, *temp_buffer, balance);
    buffer++;
    temp_buffer++;
  }

  END_INTERPOLATE_PARAMETER_1
}

// BUZZ. DTUN: range of the second buzz detune (COLOR). MIX: balance
// between the two. OCT2: octave of the second one.
void MacroOscillator::RenderBuzz(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  analog_oscillator_[0].set_parameter(parameter_[0]);
  analog_oscillator_[0].set_shape(OSC_SHAPE_BUZZ);
  analog_oscillator_[0].set_pitch(pitch_);

  analog_oscillator_[1].set_parameter(parameter_[0]);
  analog_oscillator_[1].set_shape(OSC_SHAPE_BUZZ);
  analog_oscillator_[1].set_pitch(pitch_ + OctaveShift(extra_[2]) +
      (static_cast<int32_t>(parameter_[1]) * extra_[0] >> 14));

  analog_oscillator_[0].Render(sync, buffer, NULL, size);
  analog_oscillator_[1].Render(sync, temp_buffer_, NULL, size);
  int16_t* temp_buffer = temp_buffer_;
  if (extra_[1] == 64) {
    while (size--) {
      *buffer >>= 1;
      *buffer += *temp_buffer >> 1;
      buffer++;
      temp_buffer++;
    }
  } else {
    int32_t mix = extra_[1];
    while (size--) {
      *buffer = (*buffer * (128 - mix) + *temp_buffer * mix) >> 7;
      buffer++;
      temp_buffer++;
    }
  }
}

void MacroOscillator::RenderDigital(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  digital_oscillator_.set_parameters(parameter_[0], parameter_[1]);
  digital_oscillator_.set_extras(extra_);
  digital_oscillator_.set_pitch(pitch_);
  digital_oscillator_.set_shape(static_cast<DigitalOscillatorShape>(
      shape_ - MACRO_OSC_SHAPE_SAW_SWARM));
  digital_oscillator_.Render(sync, buffer, size);
}

void MacroOscillator::RenderSawComb(
  const uint8_t* sync,
  int16_t* buffer,
  uint8_t size) {
  analog_oscillator_[0].set_parameter(0);
  analog_oscillator_[0].set_pitch(pitch_);
  analog_oscillator_[0].set_shape(OSC_SHAPE_SAW);
  analog_oscillator_[0].Render(sync, buffer, NULL, size);

  digital_oscillator_.set_parameters(parameter_[0], parameter_[1]);
  digital_oscillator_.set_extras(extra_);
  digital_oscillator_.set_pitch(pitch_);
  digital_oscillator_.set_shape(OSC_SHAPE_COMB_FILTER);
  digital_oscillator_.Render(sync, buffer, size);
}

/* static */
MacroOscillator::RenderFn MacroOscillator::fn_table_[] = {
  &MacroOscillator::RenderCSaw,
  &MacroOscillator::RenderMorph,
  &MacroOscillator::RenderSawSquare,
  &MacroOscillator::RenderSquareSync,
  &MacroOscillator::RenderSineTriangle,
  &MacroOscillator::RenderBuzz,
  &MacroOscillator::RenderTripleSawSquare,
  &MacroOscillator::RenderTripleSawSquare,
  &MacroOscillator::RenderTripleSineTriangle,
  &MacroOscillator::RenderTripleSineTriangle,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderSawComb,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,
  &MacroOscillator::RenderDigital,  // VMRF
  &MacroOscillator::RenderDigital,  // VOS2
  &MacroOscillator::RenderDigital,  // DSWM
  &MacroOscillator::RenderDigital,  // FDRN
  &MacroOscillator::RenderDigital,  // SPEC
  &MacroOscillator::RenderDigital,  // TANP
  &MacroOscillator::RenderDigital,  // ZERO
};

static_assert(MACRO_OSC_SHAPE_SILENCE - MACRO_OSC_SHAPE_SAW_SWARM ==
              OSC_SHAPE_SILENCE,
              "Digital block of MacroOscillatorShape out of sync");

}  // namespace braids
