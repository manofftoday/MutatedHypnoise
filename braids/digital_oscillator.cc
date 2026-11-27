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
// Oscillator - digital style waveforms.

#include "braids/digital_oscillator.h"

#include <algorithm>
#include <cstdio>

#include "stmlib/utils/dsp.h"
#include "stmlib/utils/random.h"

#include "braids/parameter_interpolation.h"
#include "braids/resources.h"

namespace braids {
  
using namespace stmlib;

static const uint16_t kHighestNote = 140 * 128;
static const uint16_t kPitchTableStart = 128 * 128;
static const uint16_t kOctave = 12 * 128;


// Advanced engine parameters: extra_[0..2], 0..127 (engine_params.cc).
// Every default value reproduces the original engine exactly.

// "OCT" parameters: 0..42 = -1 octave, 43..85 = 0, 86..127 = +1 octave.
static inline int32_t OctaveShift(uint8_t value) {
  return value < 43 ? -(12 << 7) : (value < 86 ? 0 : (12 << 7));
}

// Wavetable engines: WARP (phase distortion), SYNC (internal hard sync,
// x1..x8) and QUAN (phase quantization: stepped, lo-fi playback).
void DigitalOscillator::ConfigurePhaseWarp() {
  warp_enabled_ = extra_[0] || extra_[1] || extra_[2];
  warp_sync_ = 256 + (extra_[1] * 1792 >> 7);
  // Knee from 1/2 (no warp) down to ~1/40 of the period.
  warp_knee_ = extra_[0] ? 32768 - (extra_[0] * 248) : 0;
  if (warp_knee_) {
    warp_slope_a_ = 0x80000000UL / warp_knee_;
    warp_slope_b_ = 0x80000000UL / (65536 - warp_knee_);
  }
  warp_mask_ = extra_[2] ? 0xffffffffUL << (25 + (extra_[2] * 6 >> 7)) :
      0xffffffffUL;
}

uint32_t DigitalOscillator::ComputePhaseIncrement(int16_t midi_pitch) {
  if (midi_pitch >= kPitchTableStart) {
    midi_pitch = kPitchTableStart - 1;
  }
  
  int32_t ref_pitch = midi_pitch;
  ref_pitch -= kPitchTableStart;
  
  size_t num_shifts = 0;
  while (ref_pitch < 0) {
    ref_pitch += kOctave;
    ++num_shifts;
  }
  
  uint32_t a = lut_oscillator_increments[ref_pitch >> 4];
  uint32_t b = lut_oscillator_increments[(ref_pitch >> 4) + 1];
  uint32_t phase_increment = a + \
      (static_cast<int32_t>(b - a) * (ref_pitch & 0xf) >> 4);
  phase_increment >>= num_shifts;
  return phase_increment;
}

uint32_t DigitalOscillator::ComputeDelay(int16_t midi_pitch) {
  if (midi_pitch >= kHighestNote - kOctave) {
    midi_pitch = kHighestNote - kOctave;
  }
  
  int32_t ref_pitch = midi_pitch;
  ref_pitch -= kPitchTableStart;
  
  size_t num_shifts = 0;
  while (ref_pitch < 0) {
    ref_pitch += kOctave;
    ++num_shifts;
  }
  
  uint32_t a = lut_oscillator_delays[ref_pitch >> 4];
  uint32_t b = lut_oscillator_delays[(ref_pitch >> 4) + 1];
  uint32_t delay = a + (static_cast<int32_t>(b - a) * (ref_pitch & 0xf) >> 4);  
  delay >>= 12 - num_shifts;
  return delay;
}

void DigitalOscillator::Render(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {

  // Quantize parameter for FM.
  if (shape_ >= OSC_SHAPE_FM &&
      shape_ <= OSC_SHAPE_CHAOTIC_FEEDBACK_FM) {
    uint16_t integral = parameter_[1] >> 8;
    uint16_t fractional = parameter_[1] & 255;
    int16_t a = lut_fm_frequency_quantizer[integral];
    int16_t b = lut_fm_frequency_quantizer[integral + 1];
    parameter_[1] = a + ((b - a) * fractional >> 8);
  }    
  
  RenderFn fn = fn_table_[shape_];

  if (shape_ >= OSC_SHAPE_WAVETABLES &&
      shape_ <= OSC_SHAPE_WAVE_PARAPHONIC) {
    ConfigurePhaseWarp();
  }

  if (shape_ != previous_shape_) {
    Init();
    previous_shape_ = shape_;
    init_ = true;
  }
  
  phase_increment_ = ComputePhaseIncrement(pitch_);
  delay_ = ComputeDelay(pitch_);
  
  if (pitch_ > kHighestNote) {
    pitch_ = kHighestNote;
  } else if (pitch_ < 0) {
    pitch_ = 0;
  }

  (this->*fn)(sync, buffer, size);
}

void DigitalOscillator::RenderSawSwarm(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  int32_t detune = parameter_[0] + 1024;
  detune = (detune * detune) >> 9;
  uint32_t increments[7];
  for (int16_t i = 0; i < 7; ++i) {
    int32_t saw_detune = detune * (i - 3);
    int32_t detune_integral = saw_detune >> 16;
    int32_t detune_fractional = saw_detune & 0xffff;
    int32_t increment_a = ComputePhaseIncrement(pitch_ + detune_integral);
    int32_t increment_b = ComputePhaseIncrement(pitch_ + detune_integral + 1);
    increments[i] = increment_a + \
        (((increment_b - increment_a) * detune_fractional) >> 16);
  }
  if (strike_) {
    for (uint8_t i = 0; i < 6; ++i) {
      state_.saw.phase[i] = Random::GetWord();
    }
    strike_ = false;
  }
  int32_t hp_cutoff = pitch_ + (static_cast<int32_t>(extra_[0]) - 64) * 96;
  if (parameter_[1] < 10922) {
    hp_cutoff += ((parameter_[1] - 10922) * 24) >> 5;
  } else {
    hp_cutoff += ((parameter_[1] - 10922) * 12) >> 5;
  }
  if (hp_cutoff < 0) {
    hp_cutoff = 0;
  } else if (hp_cutoff > 32767) {
    hp_cutoff = 32767;
  }
  
  int32_t f = Interpolate824(lut_svf_cutoff, hp_cutoff << 17);
  int32_t damp = lut_svf_damp[0];
  uint8_t num_voices = 1 + (extra_[2] * 6 + 63) / 127;
  int32_t bp = state_.saw.bp;
  int32_t lp = state_.saw.lp;

  while (size--) {
    if (*sync++) {
      for (uint8_t i = 0; i < 6; ++i) {
        state_.saw.phase[i] = 0;
      }
    }
    int32_t notch, hp, sample;
    
    phase_ += increments[0];
    state_.saw.phase[0] += increments[1];
    state_.saw.phase[1] += increments[2];
    state_.saw.phase[2] += increments[3];
    state_.saw.phase[3] += increments[4];
    state_.saw.phase[4] += increments[5];
    state_.saw.phase[5] += increments[6];
    
    // Compute a sample.
    sample = -28672;
    sample += phase_ >> 19;
    sample += state_.saw.phase[0] >> 19;
    sample += state_.saw.phase[1] >> 19;
    sample += state_.saw.phase[2] >> 19;
    sample += state_.saw.phase[3] >> 19;
    sample += state_.saw.phase[4] >> 19;
    sample += state_.saw.phase[5] >> 19;
    if (num_voices < 7) {
      // VOIC: drop the outer voices, keep the level.
      for (uint8_t i = num_voices - 1; i < 6; ++i) {
        sample -= (state_.saw.phase[i] >> 19) - 4096;
      }
      sample = sample * 7 / num_voices;
      CLIP(sample)
    }
    if (extra_[1] == 127) {
      sample = Interpolate88(ws_moderate_overdrive, sample + 32768);
    } else {
      // DRIV: blend clean -> overdriven.
      sample = Mix(sample,
                   Interpolate88(ws_moderate_overdrive, sample + 32768),
                   extra_[1] << 9);
    }
    
    notch = sample - (bp * damp >> 15);
    lp += f * bp >> 15;
    CLIP(lp)
    hp = notch - lp;
    bp += f * hp >> 15;
    
    int32_t result = hp;
    CLIP(result)
    *buffer++ = result;
  }
  state_.saw.lp = lp;
  state_.saw.bp = bp;
}

void DigitalOscillator::RenderComb(
    const uint8_t* sync,
     int16_t* buffer,
     uint8_t size) {
  // Filter the delay time to avoid clicks/glitches.
  int32_t pitch = pitch_ + ((parameter_[0] - 16384) >> 1);
  int32_t filtered_pitch = state_.ffm.previous_sample;
  filtered_pitch = (15 * filtered_pitch + pitch) >> 4;
  state_.ffm.previous_sample = filtered_pitch;
  
  int16_t* dl = delay_lines_.comb;
  uint32_t delay = ComputeDelay(filtered_pitch);
  if (delay > (kCombDelayLength << 16)) {
    delay = kCombDelayLength << 16;
  }
  uint32_t delay_integral = delay >> 16;
  int32_t delay_fractional = delay & 0xffff;

  // Warp the resonance curve to have a more precise adjustment in the extrema.
  int16_t resonance = (parameter_[1] << 1) - 32768;
  resonance = Interpolate88(ws_moderate_overdrive, resonance + 32768);
  
  uint32_t delay_ptr = phase_;
  delay_ptr =  delay_ptr % kCombDelayLength;
  bool damping = extra_[0] != 0;
  int32_t damping_coefficient = 32767 - (extra_[0] << 8);
  int32_t lp = static_cast<int32_t>(state_.ffm.modulator_phase);
  while (size--) {
    int32_t in = *buffer;
    uint32_t offset = delay_ptr + 2 * kCombDelayLength - delay_integral;
    int32_t a = dl[offset % kCombDelayLength];
    int32_t b = dl[(offset - 1) % kCombDelayLength];
    int32_t delayed_sample = a + (((b - a) * (delay_fractional >> 1)) >> 15);
    if (damping) {
      // DAMP: one-pole low-pass in the loop.
      lp += (delayed_sample - lp) * damping_coefficient >> 15;
      delayed_sample = lp;
    }
    int32_t feedback = (delayed_sample * resonance >> 15) + (in >> 1);
    CLIP(feedback)
    if (extra_[2]) {
      // SAT: saturation in the loop.
      feedback = Mix(feedback,
                     Interpolate88(ws_moderate_overdrive, feedback + 32768),
                     extra_[2] << 9);
    }
    dl[delay_ptr] = feedback;
    int32_t out = extra_[1] == 64
        ? (in + (delayed_sample << 1)) >> 1
        : (in * (128 - extra_[1]) + delayed_sample * extra_[1] * 2) >> 7;
    CLIP(out)
    *buffer++ = out;
    delay_ptr = (delay_ptr + 1) % kCombDelayLength;
  }
  phase_ = delay_ptr;
  state_.ffm.modulator_phase = static_cast<uint32_t>(lp);
}

void DigitalOscillator::RenderVosim(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  int32_t octave = OctaveShift(extra_[2]);
  for (size_t i = 0; i < 2; ++i) {
    state_.vow.formant_increment[i] = ComputePhaseIncrement(
        (parameter_[i] >> 1) + octave);
  }
  int32_t level_1 = extra_[0];
  int32_t level_2 = extra_[1];
  while (size--) {
    phase_ += phase_increment_;
    if (*sync++) {
      phase_ = 0;
    }
    int32_t sample = 16384 + 8192;
    state_.vow.formant_phase[0] += state_.vow.formant_increment[0];
    sample += Interpolate824(wav_sine, state_.vow.formant_phase[0]) *
        level_1 >> 7;

    state_.vow.formant_phase[1] += state_.vow.formant_increment[1];
    sample += Interpolate824(wav_sine, state_.vow.formant_phase[1]) *
        level_2 >> 8;
    
    sample = sample * (Interpolate824(lut_bell, phase_) >> 1) >> 15;
    if (phase_ < phase_increment_) {
      state_.vow.formant_phase[0] = 0;
      state_.vow.formant_phase[1] = 0;
      sample = 0;
    }
    sample -= 16384 + 8192;
    CLIP(sample)
    *buffer++ = sample;
  }
}

struct PhonemeDefinition {
  uint8_t formant_frequency[3];
  uint8_t formant_amplitude[3];
};

const PhonemeDefinition vowels_data[9] = {
    { { 27,  40,  89 }, { 15,  13,  1 } },
    { { 18,  51,  62 }, { 13,  12,  6 } },
    { { 15,  69,  93 }, { 14,  12,  7 } },
    { { 10,  84, 110 }, { 13,  10,  8 } },
    { { 23,  44,  87 }, { 15,  12,  1 } },
    { { 13,  29,  80 }, { 13,   8,  0 } },
    { {  6,  46,  81 }, { 12,   3,  0 } },
    { {  9,  51,  95 }, { 15,   3,  0 } },
    { {  6,  73,  99 }, {  7,   3,  14 } }
};

const PhonemeDefinition consonant_data[8] = {
    { { 6, 54, 121 }, { 9,  9,  0 } },
    { { 18, 50, 51 }, { 12,  10,  5 } },
    { { 11, 24, 70 }, { 13,  8,  0 } },
    { { 15, 69, 74 }, { 14,  12,  7 } },
    { { 16, 37, 111 }, { 14,  8,  1 } },
    { { 18, 51, 62 }, { 14,  12,  6 } },
    { { 6, 26, 81 }, { 5,  5,  5 } },
    { { 6, 73, 99 }, { 7,  10,  14 } },
};


void DigitalOscillator::RenderVowel(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  size_t vowel_index = parameter_[0] >> 12;
  uint16_t balance = parameter_[0] & 0x0fff;
  uint16_t formant_shift = (200 + (parameter_[1] >> 6));
  if (strike_) {
    strike_ = false;
    state_.vow.consonant_frames = 160 * extra_[0] >> 6;
    uint16_t index = (Random::GetSample() + 1) & 7;
    for (uint8_t i = 0; i < 3; ++i) {
      state_.vow.formant_increment[i] = \
          static_cast<uint32_t>(consonant_data[index].formant_frequency[i]) * \
          0x1000 * formant_shift;
      state_.vow.formant_amplitude[i] = consonant_data[index].formant_amplitude[i];
    }
    state_.vow.noise = index >= 6 ? 4095 : 0;
  }
  
  if (state_.vow.consonant_frames) {
    --state_.vow.consonant_frames;
  } else {
    for (size_t i = 0; i < 3; ++i) {
      state_.vow.formant_increment[i] = 
          (vowels_data[vowel_index].formant_frequency[i] * (0x1000 - balance) + \
           vowels_data[vowel_index + 1].formant_frequency[i] * balance) * \
           formant_shift;
      state_.vow.formant_amplitude[i] =
          (vowels_data[vowel_index].formant_amplitude[i] * (0x1000 - balance) + \
           vowels_data[vowel_index + 1].formant_amplitude[i] * balance) >> 12;
    }
    state_.vow.noise = 0;
  }
  int32_t noise = state_.vow.noise;
  
  while (size--) {
    phase_ += phase_increment_;
    size_t phaselet;
    int16_t sample = 0;
    state_.vow.formant_phase[0] += state_.vow.formant_increment[0];
    phaselet = (state_.vow.formant_phase[0] >> 24) & 0xf0;
    sample += wav_formant_sine[phaselet | state_.vow.formant_amplitude[0]];

    state_.vow.formant_phase[1] += state_.vow.formant_increment[1];
    phaselet = (state_.vow.formant_phase[1] >> 24) & 0xf0;
    sample += wav_formant_sine[phaselet | state_.vow.formant_amplitude[1]];
    
    state_.vow.formant_phase[2] += state_.vow.formant_increment[2];
    phaselet = (state_.vow.formant_phase[2] >> 24) & 0xf0;
    sample += wav_formant_square[phaselet | state_.vow.formant_amplitude[2]];
    
    sample *= 255 - (phase_ >> 24);
    int32_t phase_noise = Random::GetSample() * noise;
    if ((phase_ + phase_noise) < phase_increment_) {
      state_.vow.formant_phase[0] = 0;
      state_.vow.formant_phase[1] = 0;
      state_.vow.formant_phase[2] = 0;
      sample = 0;
    }
    if (extra_[1] || extra_[2] != 64) {
      int32_t s = sample;
      if (extra_[1]) {
        s += Random::GetSample() * extra_[1] >> 8;  // NOIS: breath
      }
      s = s * (16 + (extra_[2] * 3 >> 2)) >> 6;  // DRIV: 0.25x..1.7x
      CLIP(s)
      sample = s;
    }
    sample = Interpolate88(ws_moderate_overdrive, sample + 32768);
    *buffer++ = sample;
  }
}

const int16_t formant_f_data[kNumFormants][kNumFormants][kNumFormants] = {
  // bass
  {
    { 9519, 10738, 12448, 12636, 12892 }, // a
    { 8620, 11720, 12591, 12932, 13158 }, // e
    { 7579, 11891, 12768, 13122, 13323 }, // i
    { 8620, 10013, 12591, 12768, 13010 }, // o
    { 8324, 9519, 12591, 12831, 13048 } // u
  },
  // tenor
  {
    { 9696, 10821, 12810, 13010, 13263 }, // a
    { 8620, 11827, 12768, 13228, 13477 }, // e
    { 7908, 12038, 12932, 13263, 13452 }, // i
    { 8620, 10156, 12768, 12932, 13085 }, // o
    { 8324, 9519, 12852, 13010, 13296 } // u
  },
  // countertenor
  {
    { 9730, 10902, 12892, 13085, 13330 }, // a
    { 8832, 11953, 12852, 13085, 13296 }, // e
    { 7749, 12014, 13010, 13330, 13483 }, // i
    { 8781, 10211, 12852, 13085, 13296 }, // o
    { 8448, 9627, 12892, 13085, 13363 } // u
  },
  // alto
  {
    { 10156, 10960, 12932, 13427, 14195 }, // a
    { 8620, 11692, 12852, 13296, 14195 }, // e
    { 8324, 11827, 12852, 13550, 14195 }, // i
    { 8881, 10156, 12956, 13427, 14195 }, // o
    { 8160, 9860, 12708, 13427, 14195 } // u
  },
  // soprano
  {
    { 10156, 10960, 13010, 13667, 14195 }, // a
    { 8324, 12187, 12932, 13489, 14195 }, // e
    { 7749, 12337, 13048, 13667, 14195 }, // i
    { 8881, 10156, 12956, 13609, 14195 }, // o
    { 8160, 9860, 12852, 13609, 14195 } // u
  }
};

const int16_t formant_a_data[kNumFormants][kNumFormants][kNumFormants] = {
  // bass
  {
    { 16384, 7318, 5813, 5813, 1638 }, // a
    { 16384, 4115, 5813, 4115, 2062 }, // e
    { 16384, 518, 2596, 1301, 652 }, // i
    { 16384, 4617, 1460, 1638, 163 }, // o
    { 16384, 1638, 411, 652, 259 } // u
  },
  // tenor
  {
    { 16384, 8211, 7318, 6522, 1301 }, // a
    { 16384, 3269, 4115, 3269, 1638 }, // e
    { 16384, 2913, 2062, 1638, 518 }, // i
    { 16384, 5181, 4115, 4115, 821 }, // o
    { 16384, 1638, 2314, 3269, 821 } // u
  },
  // countertenor
  {
    { 16384, 8211, 1159, 1033, 206 }, // a
    { 16384, 3269, 2062, 1638, 1638 }, // e
    { 16384, 1033, 1033, 259, 259 }, // i
    { 16384, 5181, 821, 1301, 326 }, // o
    { 16384, 1638, 1159, 518, 326 } // u
  },
  // alto
  {
    { 16384, 10337, 1638, 259, 16 }, // a
    { 16384, 1033, 518, 291, 16 }, // e
    { 16384, 1638, 518, 259, 16 }, // i
    { 16384, 5813, 2596, 652, 29 }, // o
    { 16384, 4115, 518, 163, 10 } // u
  },
  // soprano
  {
    { 16384, 8211, 411, 1638, 51 }, // a
    { 16384, 1638, 2913, 163, 25 }, // e
    { 16384, 4115, 821, 821, 103 }, // i
    { 16384, 4617, 1301, 1301, 51 }, // o
    { 16384, 2596, 291, 163, 16 } // u
  }
};

int16_t DigitalOscillator::InterpolateFormantParameter(
    const int16_t table[][kNumFormants][kNumFormants],
    int16_t x,
    int16_t y,
    uint8_t formant) {
  uint16_t x_index = x >> 13;
  uint16_t x_mix = x << 3;
  uint16_t y_index = y >> 13;
  uint16_t y_mix = y << 3;
  int16_t a = table[x_index][y_index][formant];
  int16_t b = table[x_index + 1][y_index][formant];
  int16_t c = table[x_index][y_index + 1][formant];
  int16_t d = table[x_index + 1][y_index + 1][formant];
  a = a + ((b - a) * x_mix >> 16);
  c = c + ((d - c) * x_mix >> 16);
  return a + ((c - a) * y_mix >> 16);
}

void DigitalOscillator::RenderVowelFof(
  const uint8_t* sync,
  int16_t* buffer,
  uint8_t size) {
  
  // This thing is running at SR / 2.
  phase_increment_ <<= 1;
  int32_t formant_shift = (static_cast<int32_t>(extra_[2]) - 64) * 24;
  
  int16_t previous_sample = state_.fof.prevous_sample;
  while (size) {
    phase_ += phase_increment_;
    int32_t sample = 0;
    
    for (uint8_t i = 0; i < kNumOverlappingFof; ++i) {
      if (state_.fof.envelope_phase[i] < 0x01000000) {
        int32_t s;
        int32_t fof_set_sample = 0;
        state_.fof.fof[i][0].phase += state_.fof.fof[i][0].phase_increment;
        s = Interpolate824(wav_sine, state_.fof.fof[i][0].phase);
        fof_set_sample += s * state_.fof.fof[i][0].amplitude >> 16;
        
        state_.fof.fof[i][1].phase += state_.fof.fof[i][1].phase_increment;
        s = wav_sine[state_.fof.fof[i][1].phase >> 24];
        fof_set_sample += s * state_.fof.fof[i][1].amplitude >> 16;

        state_.fof.fof[i][2].phase += state_.fof.fof[i][2].phase_increment;
        s = wav_sine[state_.fof.fof[i][2].phase >> 24];
        fof_set_sample += s * state_.fof.fof[i][2].amplitude >> 16;

        state_.fof.fof[i][3].phase += state_.fof.fof[i][3].phase_increment;
        s = wav_sine[state_.fof.fof[i][3].phase >> 24];
        fof_set_sample += s * state_.fof.fof[i][3].amplitude >> 16;

        // Prone to too much aliasing!
        state_.fof.fof[i][4].phase += state_.fof.fof[i][4].phase_increment;
        s = wav_sine[state_.fof.fof[i][4].phase >> 24];
        fof_set_sample += s * state_.fof.fof[i][4].amplitude >> 16;

        sample += fof_set_sample * \
            lut_fof_envelope[state_.fof.envelope_phase[i] >> 14] >> 16;
        state_.fof.envelope_phase[i] += state_.fof.envelope_phase_increment[i];
      }
    }
    
    // Overlap a new set of grains.
    if (phase_ < phase_increment_) {
      uint8_t i = state_.fof.lru_fof;
      for (uint8_t j = 0; j < kNumFormants; ++j) {
        state_.fof.fof[i][j].phase_increment = ComputePhaseIncrement(
            InterpolateFormantParameter(
                formant_f_data,
                parameter_[1],
                parameter_[0],
                j) + formant_shift) << 1;
        int32_t amplitude = InterpolateFormantParameter(
            formant_a_data,
            parameter_[1],
            parameter_[0],
            j);
        if (j) {
          amplitude = amplitude * extra_[1] >> 6;  // BRIT
          if (amplitude > 65535) amplitude = 65535;
        }
        state_.fof.fof[i][j].amplitude = amplitude;
        state_.fof.fof[i][j].phase = 0;
      }
      state_.fof.envelope_phase[i] = 0;
      state_.fof.envelope_phase_increment[i] = (extra_[0] + 32) << 8;
      // Make sure that the envelope duration does not exceed N periods.
      // If this happens, this would cause a discontinuity as we only have
      // N overlapping FOFs.
      uint32_t period = phase_increment_ >> 8;
      uint32_t limit = period / kNumOverlappingFof;
      if (state_.fof.envelope_phase_increment[i] <= limit) {
        state_.fof.envelope_phase_increment[i] = limit - 1;
      }
      state_.fof.lru_fof = (i + 1) % kNumOverlappingFof;
    }
    sample = Interpolate88(ws_moderate_overdrive, sample + 32768);
    *buffer++ = (previous_sample + sample) >> 1;
    *buffer++ = sample;
    previous_sample = sample;
    size -= 2;
  }
  state_.fof.prevous_sample = previous_sample;
}


void DigitalOscillator::RenderFm(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  uint32_t modulator_phase = state_.modulator_phase;
  uint32_t modulator_phase_increment = ComputePhaseIncrement(
      (12 << 7) + pitch_ + ((parameter_[1] - 16384) >> 1) +
      (static_cast<int32_t>(extra_[1]) - 64) * 2) >> 1;
  int32_t feedback = extra_[0];
  int32_t shape_gain = 64 + extra_[2];
  int32_t modulator = state_.ffm.previous_sample;

  BEGIN_INTERPOLATE_PARAMETER_0

  while (size--) {
    INTERPOLATE_PARAMETER_0

    phase_ += phase_increment_;
    if (*sync++) {
      phase_ = modulator_phase = 0;
    }
    modulator_phase += modulator_phase_increment;

    if (feedback || extra_[2]) {
      // FDBK: modulator self-feedback. MODW: modulator sine -> square.
      modulator = Interpolate824(
          wav_sine, modulator_phase +
          (static_cast<uint32_t>(modulator * feedback) << 9));
      modulator = modulator * shape_gain >> 6;
      CLIP(modulator)
    } else {
      modulator = Interpolate824(wav_sine, modulator_phase);
    }
    uint32_t pm = (modulator * parameter_0) << 2;
    *buffer++ = Interpolate824(wav_sine, phase_ + pm);
  }
  
  END_INTERPOLATE_PARAMETER_0
  
  previous_parameter_[0] = parameter_[0];
  state_.modulator_phase = modulator_phase;
  state_.ffm.previous_sample = modulator;
}

void DigitalOscillator::RenderFeedbackFm(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  int16_t previous_sample = state_.ffm.previous_sample;
  uint32_t modulator_phase = state_.ffm.modulator_phase;

  int32_t attenuation = pitch_ - (72 << 7) + ((parameter_[1] - 16384) >> 1);
  attenuation = 32767 - (attenuation * 4 * extra_[2] >> 6);
  if (attenuation < 0) attenuation = 0;
  if (attenuation > 32767) attenuation = 32767;
  int32_t feedback = extra_[0] << 8;  // 64 = original (<< 14)

  uint32_t modulator_phase_increment = ComputePhaseIncrement(
      (12 << 7) + pitch_ + ((parameter_[1] - 16384) >> 1) +
      (static_cast<int32_t>(extra_[1]) - 64) * 2) >> 1;
  
  BEGIN_INTERPOLATE_PARAMETER_0    
  
  while (size--) {
    INTERPOLATE_PARAMETER_0
    
    phase_ += phase_increment_;
    if (*sync++) {
      phase_ = modulator_phase = 0;
    }
    
    modulator_phase += modulator_phase_increment;

    int32_t pm;
    int32_t p = parameter_0 * attenuation >> 15;
    pm = previous_sample * feedback;
    pm = (
        Interpolate824(wav_sine, modulator_phase + pm) * p) << 1;
    previous_sample = Interpolate824(wav_sine, phase_ + pm);
    *buffer++ = previous_sample;
  }
  
  END_INTERPOLATE_PARAMETER_0
  
  state_.ffm.previous_sample = previous_sample;
  state_.ffm.modulator_phase = modulator_phase;
}

void DigitalOscillator::RenderChaoticFeedbackFm(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  uint32_t modulator_phase_increment = ComputePhaseIncrement(
      (12 << 7) + pitch_ + ((parameter_[1] - 16384) >> 1) +
      (static_cast<int32_t>(extra_[1]) - 64) * 2) >> 1;
  int16_t previous_sample = state_.ffm.previous_sample;
  int32_t chaos = extra_[0];
  int32_t index_gain = extra_[2];
  uint32_t modulator_phase = state_.ffm.modulator_phase;
  
  BEGIN_INTERPOLATE_PARAMETER_0
  
  while (size--) {
    INTERPOLATE_PARAMETER_0
    
    phase_ += phase_increment_;
    if (*sync++) {
      phase_ = modulator_phase = 0;
    }
    
    int32_t index = parameter_0 * index_gain >> 6;
    uint32_t pm = static_cast<uint32_t>(
        Interpolate824(wav_sine, modulator_phase) * index) << 1;
    previous_sample = Interpolate824(wav_sine, phase_ + pm);
    *buffer++ = previous_sample;
    modulator_phase += (modulator_phase_increment >> 8) * \
        (129 + (previous_sample * chaos >> 15));
  }
  
  END_INTERPOLATE_PARAMETER_0
  
  state_.ffm.previous_sample = previous_sample;
  state_.ffm.modulator_phase = modulator_phase;
}


static int16_t kBellPartials[] = {
  -1284, -1283, -184, -183, 385, 1175, 1536, 2233, 2434, 2934, 3110
};

static int16_t kBellPartialAmplitudes[] = {
  8192, 5488, 8192, 14745, 21872, 13680, 11960, 10895, 10895, 6144, 10895
};

static uint16_t kBellPartialDecayLong[] = {
  65533, 65533, 65533, 65532, 65531, 65531, 65530, 65529, 65527, 65523, 65519
};

static uint16_t kBellPartialDecayShort[] = {
  65308, 65283, 65186, 65123, 64839, 64889, 64632, 64409, 64038, 63302, 62575
};

void DigitalOscillator::RenderStruckBell(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  
  // To save some CPU cycles, do not refresh the frequency of all partials at
  // the same time. This create a kind of "arpeggiation" with high frequency
  // CV though...
  size_t first_partial = state_.add.current_partial;
  size_t last_partial = std::min(
      state_.add.current_partial + 3,
      kNumBellPartials);
  state_.add.current_partial = (first_partial + 3) % kNumBellPartials;
  
  if (strike_) {
    for (size_t i = 0; i < kNumBellPartials; ++i) {
      int32_t amplitude = kBellPartialAmplitudes[i];
      if (i >= 5) amplitude = amplitude * extra_[1] >> 6;  // BRIT
      if (i & 1) amplitude = amplitude * extra_[2] >> 6;   // ODD
      if (amplitude > 32767) amplitude = 32767;
      state_.add.partial_amplitude[i] = amplitude;
      state_.add.partial_phase[i] = (1L << 30);
    }
    strike_ = false;
    first_partial = 0;
    last_partial = kNumBellPartials;
  }
  
  for (size_t i = first_partial; i < last_partial; ++i) {
    int16_t partial_pitch = pitch_ + (kBellPartials[i] * extra_[0] >> 6);
    if (i & 1) {
      partial_pitch += parameter_[1] >> 7;
    } else {
      partial_pitch -= parameter_[1] >> 7;
    }
    state_.add.partial_phase_increment[i] = \
        ComputePhaseIncrement(partial_pitch) << 1;
  }
  
  // Allow a "droning" bell with no energy loss when the parameter is set to
  // its maximum value
  if (parameter_[0] < 32000) {
    for (size_t i = 0; i < kNumBellPartials; ++i) {
      int32_t decay_long = kBellPartialDecayLong[i];
      int32_t decay_short = kBellPartialDecayShort[i];
      int16_t balance = (32767 - parameter_[0]) >> 8;
      balance = balance * balance >> 7;
      int32_t decay = decay_long - ((decay_long - decay_short) * balance >> 7);
      state_.add.partial_amplitude[i] = \
          state_.add.partial_amplitude[i] * decay >> 16;
    }
  }
  
  int16_t previous_sample = state_.add.previous_sample;
  while (size--) {
    int32_t out = 0;
    for (size_t i = 0; i < kNumBellPartials; ++i) {
      state_.add.partial_phase[i] += state_.add.partial_phase_increment[i];
      int32_t partial = Interpolate824(wav_sine, state_.add.partial_phase[i]);
      out += partial * state_.add.partial_amplitude[i] >> 17;
    }
    CLIP(out)
    *buffer++ = (out + previous_sample) >> 1;
    *buffer++ = out; size--;
    previous_sample = out;
  }
  state_.add.previous_sample = previous_sample;
}

void DigitalOscillator::RenderPlucked(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  phase_increment_ <<= 1;
  if (strike_) {
    ++active_voice_;
    if (active_voice_ >= 1 + (extra_[1] * 2 + 63) / 127) {  // VOIC
      active_voice_ = 0;
    }
    // Find the optimal oversampling rate.
    PluckState* p = &state_.plk[active_voice_];
    int32_t increment = phase_increment_;
    p->shift = 0;
    while (increment > (2 << 22)) {
      increment >>= 1;
      ++p->shift;
    }
    p->size = 1024 >> p->shift;
    p->mask = p->size - 1;
    p->write_ptr = 0;
    p->max_phase_increment = phase_increment_ << 1;
    p->phase_increment = phase_increment_;
    int32_t width = parameter_[1];
    width = (3 * width) >> 1;
    p->initialization_ptr = p->size * (8192 + width) >> 16;
    strike_ = false;
  }
  
  PluckState* current_string = &state_.plk[active_voice_];
  
  // Update the phase increment of the latest note, but do not transpose too
  // high above the original pitch.
  current_string->phase_increment = std::min(
      phase_increment_,
      current_string->max_phase_increment);
  
  // Compute loss and stretching factors.
  uint32_t update_probability = parameter_[0] < 16384
      ? 65535
      : 131072 - (parameter_[0] >> 3) * 31;
  int16_t loss = 4096 - (phase_increment_ >> 14);
  if (loss < 256) {
    loss = 256;
  }
  if (parameter_[0] < 16384) {
    loss = loss * (16384 - parameter_[0]) >> 14;
  } else {
    loss = 0;
  }
  loss = loss * extra_[0] >> 6;  // DAMP
  int32_t noise_weight = extra_[2] * 3 >> 1;  // NOIS, 96 = original
  if (noise_weight > 128) noise_weight = 128;
  
  int16_t previous_sample = state_.plk[0].previous_sample;

  while (size) {
    int32_t sample = 0;
    for (uint8_t i = 0; i < kNumPluckVoices; ++i) {
      PluckState* p = &state_.plk[i];
      int16_t* dl = delay_lines_.ks + i * 1025;
      // Initialization: Just use a white noise sample and fill the delay
      // line.
      if (p->initialization_ptr) {
        --p->initialization_ptr;
        int32_t excitation_sample = (dl[p->initialization_ptr] * \
            (128 - noise_weight) + Random::GetSample() * noise_weight) >> 7;
        dl[p->initialization_ptr] = excitation_sample;
        sample += excitation_sample;
      } else {
        p->phase += p->phase_increment;
        size_t read_ptr = ((p->phase >> (22 + p->shift)) + 2) & p->mask;
        size_t write_ptr = p->write_ptr;
        uint8_t num_loops = 0;
        while (write_ptr != read_ptr) {
          ++num_loops;
          size_t next = (write_ptr + 1) & p->mask;
          int32_t a = dl[write_ptr];
          int32_t b = dl[next];
          uint32_t probability = Random::GetWord();
          if ((probability & 0xffff) <= update_probability) {
            int32_t sum = (a + b);
            sum = sum < 0 ? -(-sum >> 1) : (sum >> 1);
            if (loss) {
              sum = sum * (32768 - loss) >> 15;
            }
            dl[write_ptr] = sum;
          }
          if (write_ptr == 0) {
            dl[p->size] = dl[0];
          }
          write_ptr = next;
        }
        p->write_ptr = write_ptr;
        sample += Interpolate1022(dl, p->phase >> p->shift);
      }
    }
    CLIP(sample);
    *buffer++ = (previous_sample + sample) >> 1;
    *buffer++ = sample;
    previous_sample = sample;
    size -= 2;
  }
  state_.plk[0].previous_sample = previous_sample;
}

static const int32_t kBridgeLPGain = 14008;
static const int32_t kBridgeLPPole1 = 18022;
static const int32_t kBiquadGain = 6553;
static const int32_t kBiquadPole1 = 6948;
static const int32_t kBiquadPole2 = -2959;

void DigitalOscillator::RenderBowed(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  int8_t* dl_b = delay_lines_.bowed.bridge;
  int8_t* dl_n = delay_lines_.bowed.neck;
  
  if (strike_) {
    memset(dl_b, 0, sizeof(delay_lines_.bowed.bridge));
    memset(dl_n, 0, sizeof(delay_lines_.bowed.neck));
    memset(&state_, 0, sizeof(state_));
    strike_ = false;
  }
  int16_t parameter_0 = 172 - (parameter_[0]  >> 8) +
      (static_cast<int16_t>(extra_[0]) - 64);  // PRES
  if (parameter_0 < 1) parameter_0 = 1;
  int32_t damping = (static_cast<int32_t>(extra_[2]) - 64) * 96;  // DAMP
  int32_t bridge_lp_gain = kBridgeLPGain - damping;
  int32_t bridge_lp_pole = kBridgeLPPole1 + damping;
  int32_t body_gain = kBiquadGain * (16 + (extra_[1] * 3 >> 2)) >> 6;  // BODY
  int16_t parameter_1 = 6 + (parameter_[1]  >> 9);

  uint16_t delay_ptr = state_.phy.delay_ptr;
  uint16_t excitation_ptr = state_.phy.excitation_ptr;
  int32_t lp_state = state_.phy.lp_state;

  int32_t biquad_y0 = state_.phy.filter_state[0];
  int32_t biquad_y1 = state_.phy.filter_state[1];
  // Setup delay times and interpolation coefficients.
  uint32_t delay = (delay_ >> 1) - (2 << 16);  // Compensation for 1-pole delay
  uint32_t bridge_delay = (delay >> 8) * parameter_1;
  // Transpose one octave up when the note is too low to fit in the delays.
  while ((delay - bridge_delay) > ((kWGNeckLength - 1) << 16)
         || bridge_delay > ((kWGBridgeLength - 1) << 16)) {
    delay >>= 1;
    bridge_delay >>= 1;
  }
  uint16_t bridge_delay_integral = bridge_delay >> 16;
  uint16_t bridge_delay_fractional = bridge_delay & 0xffff;
  uint32_t neck_delay = delay - bridge_delay;
  uint32_t neck_delay_integral = neck_delay >> 16;
  uint16_t neck_delay_fractional = neck_delay & 0xffff;
  int16_t previous_sample = state_.phy.previous_sample;
  // Rendered at half the sample rate (for avoiding big rounding error in
  // coefficients of body IIR filter).
  while (size) {
    phase_ += phase_increment_;
    
    int32_t new_velocity, friction;
    uint16_t bridge_delay_ptr = delay_ptr + 2 * kWGBridgeLength \
        - bridge_delay_integral;
    uint16_t neck_delay_ptr = delay_ptr + 2 * kWGNeckLength \
        - neck_delay_integral;
    int16_t bridge_dl_a = dl_b[bridge_delay_ptr % kWGBridgeLength];
    int16_t bridge_dl_b = dl_b[(bridge_delay_ptr - 1) % kWGBridgeLength];
    int16_t nut_dl_a = dl_n[neck_delay_ptr % kWGNeckLength];
    int16_t nut_dl_b = dl_n[(neck_delay_ptr - 1) % kWGNeckLength];
    int32_t bridge_value = Mix(
        bridge_dl_a, bridge_dl_b, bridge_delay_fractional) << 8;
    int32_t nut_value = Mix(nut_dl_a, nut_dl_b, neck_delay_fractional) << 8;
    lp_state = (bridge_value * bridge_lp_gain +
                lp_state * bridge_lp_pole) >> 15;
    int32_t bridge_reflection = -lp_state;
    int32_t nut_reflection = -nut_value;
    int32_t string_velocity = bridge_reflection + nut_reflection;
    int32_t bow_velocity = lut_bowing_envelope[excitation_ptr >> 1];
    bow_velocity += lut_bowing_envelope[(excitation_ptr + 1) >> 1];
    bow_velocity >>= 1;
    int32_t velocity_delta = bow_velocity - string_velocity;
    
    friction = velocity_delta * parameter_0 >> 5;
    if (friction < 0) {
      friction = -friction;
    }
    if (friction >= (1 << 17)) {
      friction = (1 << 17) - 1;
    }
    //friction = Interpolate824(lut_bowing_friction, friction << 15);
    friction = lut_bowing_friction[friction >> 9];
    new_velocity = friction * velocity_delta >> 15;
    dl_n[delay_ptr % kWGNeckLength] = (bridge_reflection + new_velocity) >> 8;
    dl_b[delay_ptr % kWGBridgeLength] = (nut_reflection + new_velocity) >> 8;
    ++delay_ptr;
    
    int32_t temp = bridge_value * body_gain >> 15;
    temp += biquad_y0 * kBiquadPole1 >> 12;
    temp += biquad_y1 * kBiquadPole2 >> 12;
    int32_t out = temp - biquad_y1;
    biquad_y1 = biquad_y0;
    biquad_y0 = temp;

    CLIP(out)
    *buffer++ = (out + previous_sample) >> 1;
    *buffer++ = out;
    previous_sample = out;
    ++excitation_ptr;
    size -= 2;
  }
  if ((excitation_ptr >> 1) >= LUT_BOWING_ENVELOPE_SIZE - 32) {
    excitation_ptr = (LUT_BOWING_ENVELOPE_SIZE - 32) << 1;
  }
  state_.phy.delay_ptr = delay_ptr % kWGNeckLength;
  state_.phy.excitation_ptr = excitation_ptr;
  state_.phy.lp_state = lp_state;
  state_.phy.filter_state[0] = biquad_y0;
  state_.phy.filter_state[1] = biquad_y1;
  state_.phy.previous_sample = previous_sample;
}

static const uint16_t kBreathPressure = 26214;
static const int16_t kReflectionCoefficient = -3891;
static const int16_t kReedSlope = -1229;
static const int16_t kReedOffset = 22938;

void DigitalOscillator::RenderBlown(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  uint16_t delay_ptr = state_.phy.delay_ptr;
  int32_t lp_state = state_.phy.lp_state;
  
  int16_t* dl = delay_lines_.bore;
  if (strike_) {
    memset(dl, 0, sizeof(delay_lines_.bore));
    strike_ = false;
  }

  uint32_t delay = (delay_ >> 1) - (1 << 16);
  while (delay > ((kWGBoreLength - 1) << 16)) {
    delay >>= 1;
  }
  uint16_t bore_delay_integral = delay >> 16;
  uint16_t bore_delay_fractional = delay & 0xffff;
  uint16_t parameter = 28000 - (parameter_[0] >> 1);
  int16_t filter_state = state_.phy.filter_state[0];
  int16_t normalized_pitch = (pitch_ - 8192 + (parameter_[1] >> 1)) >> 7;
  if (normalized_pitch < 0) {
    normalized_pitch = 0;
  } else if (normalized_pitch > 127) {
    normalized_pitch = 127;
  }
  uint16_t filter_coefficient = lut_flute_body_filter[normalized_pitch];
  int32_t breath = kBreathPressure * (32 + (extra_[0] >> 1)) >> 6;  // BRTH
  if (breath > 32767) breath = 32767;
  int32_t reed_slope = kReedSlope * extra_[1] >> 6;  // REED
  int32_t reflection = kReflectionCoefficient * (48 + (extra_[2] >> 2)) >> 6;
  if (reflection < -4000) reflection = -4000;  // REFL, keep |r| < 1
  while (size--) {
    phase_ += phase_increment_;

    int32_t breath_pressure = Random::GetSample() * parameter >> 15;
    breath_pressure = breath_pressure * breath >> 15;
    breath_pressure += breath;
    
    uint16_t bore_delay_ptr = delay_ptr + 2 * kWGBoreLength \
        - bore_delay_integral;
    int16_t dl_a = dl[bore_delay_ptr % kWGBoreLength];
    int16_t dl_b = dl[(bore_delay_ptr - 1) % kWGBoreLength];
    int32_t dl_value = Mix(dl_a, dl_b, bore_delay_fractional);
    
    int32_t pressure_delta = (dl_value >> 1) + lp_state;
    lp_state = dl_value >> 1;
    
    pressure_delta = reflection * pressure_delta >> 12;
    pressure_delta -= breath_pressure;
    int32_t reed = (pressure_delta * reed_slope >> 12) + kReedOffset;
    CLIP(reed)
    int32_t out = pressure_delta * reed >> 15;
    out += breath_pressure;
    CLIP(out)
    dl[delay_ptr++ % kWGBoreLength] = out;
    filter_state = (filter_coefficient * out + \
        (4096 - filter_coefficient) * filter_state) >> 12;
    *buffer++ = filter_state;
  }
  state_.phy.filter_state[0] = filter_state;
  state_.phy.delay_ptr = delay_ptr % kWGBoreLength;
  state_.phy.lp_state = lp_state;
}

static const uint16_t kRandomPressure = 0.22 * 4096;
static const uint16_t kDCBlockingPole = 0.99 * 4096;

void DigitalOscillator::RenderFluted(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  uint16_t delay_ptr = state_.phy.delay_ptr;
  uint16_t excitation_ptr = state_.phy.excitation_ptr;

  int32_t lp_state = state_.phy.lp_state;
  int32_t dc_blocking_x0 = state_.phy.filter_state[0];
  int32_t dc_blocking_y0 = state_.phy.filter_state[1];

  int8_t* dl_b = delay_lines_.fluted.bore;
  int8_t* dl_j = delay_lines_.fluted.jet;
  
  if (strike_) {
    excitation_ptr = 0;
    memset(dl_b, 0, sizeof(delay_lines_.fluted.bore));
    memset(dl_j, 0, sizeof(delay_lines_.fluted.jet));
    lp_state = 0;
    strike_ = false;
  }

  // Setup delay times and interpolation coefficients.
  uint32_t bore_delay = (delay_ << 1) - (2 << 16);
  int32_t jet_ratio = 48 + (parameter_[1] >> 10) +
      ((static_cast<int32_t>(extra_[1]) - 64) >> 2);  // JET
  if (jet_ratio < 8) jet_ratio = 8;
  uint32_t jet_delay = (bore_delay >> 8) * jet_ratio;
  bore_delay -= jet_delay;
  while (bore_delay > ((kWGFBoreLength - 1) << 16)
         || jet_delay > ((kWGJetLength - 1) << 16)) {
    bore_delay >>= 1;
    jet_delay >>= 1;
  }
  uint16_t bore_delay_integral = bore_delay >> 16;
  uint16_t bore_delay_fractional = bore_delay & 0xffff;
  uint32_t jet_delay_integral = jet_delay >> 16;
  uint16_t jet_delay_fractional = jet_delay & 0xffff;
  
  uint16_t breath_intensity = (2100 - (parameter_[0] >> 4)) * extra_[0] >> 6;
  int32_t body_index = pitch_ >> 7;
  if (extra_[2] != 64) {  // DAMP
    body_index += (static_cast<int32_t>(extra_[2]) - 64) >> 1;
    if (body_index < 0) body_index = 0;
    if (body_index > 127) body_index = 127;
  }
  uint16_t filter_coefficient = lut_flute_body_filter[body_index];
  while (size--) {
    phase_ += phase_increment_;
    
    uint16_t bore_delay_ptr = delay_ptr + 2 * kWGFBoreLength \
        - bore_delay_integral;
    uint16_t jet_delay_ptr = delay_ptr + 2 * kWGJetLength \
        - jet_delay_integral;
    int16_t bore_dl_a = dl_b[bore_delay_ptr % kWGFBoreLength];
    int16_t bore_dl_b = dl_b[(bore_delay_ptr - 1) % kWGFBoreLength];
    int16_t jet_dl_a = dl_j[jet_delay_ptr % kWGJetLength];
    int16_t jet_dl_b = dl_j[(jet_delay_ptr - 1) % kWGJetLength];
    int32_t bore_value = Mix(bore_dl_a, bore_dl_b, bore_delay_fractional) << 9;
    int32_t jet_value = Mix(jet_dl_a, jet_dl_b, jet_delay_fractional) << 9;
        
    int32_t breath_pressure = lut_blowing_envelope[excitation_ptr];
    breath_pressure <<= 1;
    int32_t random_pressure = Random::GetSample() * breath_intensity >> 12;
    random_pressure = random_pressure * breath_pressure >> 15;
    breath_pressure += random_pressure;
    
    lp_state = (-filter_coefficient * bore_value + \
        (4096 - filter_coefficient) * lp_state) >> 12;
    int32_t reflection = lp_state;
    dc_blocking_y0 = (kDCBlockingPole * dc_blocking_y0 >> 12);
    dc_blocking_y0 += reflection - dc_blocking_x0;
    dc_blocking_x0 = reflection;
    reflection = dc_blocking_y0;
    
    int32_t pressure_delta = breath_pressure - (reflection >> 1);
    dl_j[delay_ptr % kWGJetLength] = pressure_delta >> 9;
    
    pressure_delta = jet_value;
    int32_t jet_table_index = pressure_delta;
    if (jet_table_index < 0) {
      jet_table_index = 0;
    }
    if (jet_table_index > 65535) {
      jet_table_index = 65535;
    }
    pressure_delta = static_cast<int16_t>(
        lut_blowing_jet[jet_table_index >> 8]) + (reflection >> 1);
    dl_b[delay_ptr % kWGFBoreLength] = pressure_delta >> 9;
    ++delay_ptr;
    
    int32_t out = bore_value >> 1;
    CLIP(out)
    *buffer++ = out;
    if (size & 3) {
      ++excitation_ptr;
    }
  }
  if (excitation_ptr >= LUT_BLOWING_ENVELOPE_SIZE - 32) {
    excitation_ptr = LUT_BLOWING_ENVELOPE_SIZE - 32;
  }
  state_.phy.delay_ptr = delay_ptr;
  state_.phy.excitation_ptr = excitation_ptr;
  state_.phy.lp_state = lp_state;
  state_.phy.filter_state[0] = dc_blocking_x0;
  state_.phy.filter_state[1] = dc_blocking_y0;
}

struct WavetableDefinition {
  uint8_t num_steps;
  uint8_t wave_index[17];
};

const WavetableDefinition wavetable_definitions[] = {
// 01 male
{ 16 , { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15 } },
// 02 female
{ 16 , { 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 31 } },
// 03 choir
{ 16 , { 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 47 } },
// 04 space_voice
{ 16 , { 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 63 } },
// 05 tampura
{ 16 , { 64, 65, 66, 67, 68, 68, 69, 70, 71, 72, 73, 73, 74, 75, 75, 76, 76 } },
// 06 shamus
{ 16 , { 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 92 } },
// 07 swept_string
{ 16 , { 93, 94, 95, 96, 97, 98, 99, 100, 101,
         102, 103, 104, 105, 106, 107, 108, 108 } },
// 08 bowed
{ 16 , { 109, 110, 111, 112, 113, 114, 115, 116,
         117, 118, 119, 120, 121, 122, 123, 124, 124 } },
// 09 cello
{ 16 , { 125, 126, 127, 128, 129, 130, 131, 132,
         132, 132, 132, 132, 132, 132, 132, 132, 132 } },
// 10 vibes
{ 16 , { 133, 134, 135, 136, 137, 138, 139, 140,
         141, 142, 143, 144, 144, 144, 145, 145, 145 } },
// 11 slap
{ 16 , { 146, 147, 148, 149, 150, 151, 151, 151,
         152, 152, 152, 152, 153, 153, 153, 153, 153 } },
// 12 piano
{ 8 , { 154, 154, 154, 154, 154, 154, 155, 156, 156 } },
// 13 organ!
{ 16 , { 176, 157, 158, 159, 160, 161, 162, 163,
         164, 165, 166, 167, 168, 169, 170, 171, 171 } },
// 14 waves!
{ 16 , { 172, 173, 174, 175, 176, 177, 178, 179,
         180, 181, 182, 183, 184, 185, 186, 187, 187 } },
// 15 digital
{ 16 , { 176, 188, 189, 190, 191, 192, 193, 194,
         195, 196, 197, 198, 199, 200, 201, 202, 202 } },
// 16 drone 1
{ 16 , { 203, 205, 204, 205, 212, 206, 207, 208,
         208, 209, 210, 210, 211, 211, 212, 212, 212 } },
// 17 drone 2
{ 8 , { 213, 213, 213, 214, 215, 216, 217, 218, 219 } },
// 18 metallic
{ 16 , { 220, 221, 222, 223, 224, 225, 226, 227,
         228, 229, 230, 231, 232, 233, 234, 235, 235 } },
// 19 fantasy
{ 16 , { 236, 237, 238, 239, 240, 241, 242, 243,
         244, 245, 246, 247, 248, 249, 250, 251, 251 } },
// 20 bell
{ 4 , { 252, 253, 254, 255, 254 } },
};

void DigitalOscillator::RenderWavetables(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  // Add some hysteresis to the second parameter to prevent a single DAC bit
  // error to cause a sharp and glitchy wavetable transition.
  if ((parameter_[1] > previous_parameter_[1] + 64) ||
      (parameter_[1] < previous_parameter_[1] - 64)) {
    previous_parameter_[1] = parameter_[1];
  }
      
  uint32_t wavetable_index = static_cast<uint32_t>(previous_parameter_[1]) * 20;
  wavetable_index >>= 15;
  
  uint32_t wave_pointer;
  const uint8_t* wave[2];
  const WavetableDefinition& wt = wavetable_definitions[wavetable_index];
  
  wave_pointer = (parameter_[0] << 1) * wt.num_steps;
  for (uint8_t i = 0; i < 2; ++i) {
    size_t wave_index = wt.wave_index[(wave_pointer >> 16) + i];
    wave[i] = wt_waves + wave_index * 129;
  }

  uint32_t phase_increment = phase_increment_ >> 1;
  while (size--) {
    int16_t sample;
    // 2x naive oversampling.
    phase_ += phase_increment;
    if (*sync++) {
      phase_ = 0;
    }
    
    sample = Crossfade(
        wave[0], wave[1], WarpPhase(phase_) >> 1, wave_pointer) >> 1;
    phase_ += phase_increment;
    sample += Crossfade(
        wave[0], wave[1], WarpPhase(phase_) >> 1, wave_pointer) >> 1;
    *buffer++ = sample;
  }
}

void DigitalOscillator::RenderWaveMap(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  
  // The grid is 16x16; so there are 15 interpolation squares.
  uint16_t p[2];
  uint16_t wave_xfade[2];
  uint16_t wave_coordinate[2];

  p[0] = parameter_[0] * 15 >> 4;
  p[1] = parameter_[1] * 15 >> 4;
  wave_xfade[0] = p[0] << 5;
  wave_xfade[1] = p[1] << 5;
  wave_coordinate[0] = p[0] >> 11;
  wave_coordinate[1] = p[1] >> 11;

  const uint8_t* wave[2][2];
  
  for (uint8_t i = 0; i < 2; ++i) {
    for (uint8_t j = 0; j < 2; ++j) {
      uint16_t wave_index = \
          (wave_coordinate[0] + i) * 16 + (wave_coordinate[1] + j);
      wave[i][j] = wt_waves + wt_map[wave_index] * 129;
    }
  }

  uint32_t phase_increment = phase_increment_ >> 1;
  while (size--) {
    int16_t sample;
    // 2x naive oversampling.
    phase_ += phase_increment;
    if (*sync++) {
      phase_ = 0;
    }
    
    uint32_t p = WarpPhase(phase_) >> 1;
    sample = Mix(
        Crossfade(wave[0][0], wave[0][1], p, wave_xfade[1]),
        Crossfade(wave[1][0], wave[1][1], p, wave_xfade[1]),
        wave_xfade[0]) >> 1;
    phase_ += phase_increment;
    p = WarpPhase(phase_) >> 1;
    sample += Mix(
        Crossfade(wave[0][0], wave[0][1], p, wave_xfade[1]),
        Crossfade(wave[1][0], wave[1][1], p, wave_xfade[1]),
        wave_xfade[0]) >> 1;
    *buffer++ = sample;
  }
}

const uint8_t wave_line[] = {
  187, 179, 154, 155, 135, 134, 137, 19, 24, 3, 8, 66, 79, 25, 180, 174, 64,
  127, 198, 15, 10, 7, 11, 0, 191, 192, 115, 238, 237, 236, 241, 47, 70, 76,
  235, 26, 133, 208, 34, 175, 183, 146, 147, 148, 150, 151, 152, 153, 117,
  138, 32, 33, 35, 125, 199, 201, 30, 31, 193, 27, 29, 21, 18, 182
};


const uint8_t mini_wave_line[] = {
  174, 157, 161, 171, 188, 189, 191, 192, 193, 196, 198, 201, 234, 232,
  229, 226, 224, 1, 2, 3, 4, 5, 8, 12, 32, 36, 42, 47, 252, 254, 141, 139,
  135
};

void DigitalOscillator::RenderWaveLine(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  smoothed_parameter_ = (3 * smoothed_parameter_ + (parameter_[0] << 1)) >> 2;

  uint16_t scan = smoothed_parameter_;
  const uint8_t* wave_0 = wt_waves + wave_line[previous_parameter_[0] >> 9] * 129;
  const uint8_t* wave_1 = wt_waves + wave_line[scan >> 10] * 129;
  const uint8_t* wave_2 = wt_waves + wave_line[(scan >> 10) + 1] * 129;

  uint16_t smooth_xfade = scan << 6;
  uint16_t rough_xfade = 0;
  uint16_t rough_xfade_increment = 32768 / size;
  uint32_t balance = parameter_[1] << 3;

  uint32_t phase = phase_;
  uint32_t phase_increment = phase_increment_ >> 1;
  
  int16_t rough, smooth;
  
  if (parameter_[1] < 8192) {
    while (size--) {
      int32_t sample = 0;
      
      rough = Crossfade(wave_0, wave_1, (WarpPhase(phase) >> 1) & 0xfe000000, rough_xfade);
      smooth = Crossfade(wave_0, wave_1, WarpPhase(phase) >> 1, rough_xfade);
      sample += Mix(rough, smooth, balance);
      phase += phase_increment;
      rough_xfade += rough_xfade_increment;
      
      rough = Crossfade(wave_0, wave_1, (WarpPhase(phase) >> 1) & 0xfe000000, rough_xfade);
      smooth = Crossfade(wave_0, wave_1, WarpPhase(phase) >> 1, rough_xfade);
      sample += Mix(rough, smooth, balance);
      phase += phase_increment;
      rough_xfade += rough_xfade_increment;
      
      *buffer++ = sample >> 1;
    }
  } else if (parameter_[1] < 16384) {
    while (size--) {
      int32_t sample = 0;
      
      rough = Crossfade(wave_0, wave_1, WarpPhase(phase) >> 1, rough_xfade);
      smooth = Crossfade(wave_1, wave_2, WarpPhase(phase) >> 1, smooth_xfade);
      sample += Mix(rough, smooth, balance);
      phase += phase_increment;
      rough_xfade += rough_xfade_increment;
      
      rough = Crossfade(wave_0, wave_1, WarpPhase(phase) >> 1, rough_xfade);
      smooth = Crossfade(wave_1, wave_2, WarpPhase(phase) >> 1, smooth_xfade);
      sample += Mix(rough, smooth, balance);
      phase += phase_increment;
      rough_xfade += rough_xfade_increment;

      *buffer++ = sample >> 1;
    }
  } else if (parameter_[1] < 24576) {
    while (size--) {
      int32_t sample = 0;
      
      smooth = Crossfade(wave_1, wave_2, WarpPhase(phase) >> 1, smooth_xfade);
      rough = Crossfade(wave_1, wave_2, (WarpPhase(phase) >> 1) & 0xfe000000, smooth_xfade);
      sample += Mix(smooth, rough, balance);
      phase += phase_increment;

      smooth = Crossfade(wave_1, wave_2, WarpPhase(phase) >> 1, smooth_xfade);
      rough = Crossfade(wave_1, wave_2, (WarpPhase(phase) >> 1) & 0xfe000000, smooth_xfade);
      sample += Mix(smooth, rough, balance);
      phase += phase_increment;

      *buffer++ = sample >> 1;
    }
  } else {
    while (size--) {
      int32_t sample = 0;
      smooth = Crossfade(wave_1, wave_2, (WarpPhase(phase) >> 1) & 0xfe000000, smooth_xfade);
      rough = Crossfade(wave_1, wave_2, (WarpPhase(phase) >> 1) & 0xf8000000, smooth_xfade);
      sample += Mix(smooth, rough, balance);
      phase += phase_increment;

      smooth = Crossfade(wave_1, wave_2, (WarpPhase(phase) >> 1) & 0xfe000000, smooth_xfade);
      rough = Crossfade(wave_1, wave_2, (WarpPhase(phase) >> 1) & 0xf8000000, smooth_xfade);
      sample += Mix(smooth, rough, balance);
      phase += phase_increment;

      *buffer++ = sample >> 1;
    }
  }
  phase_ = phase;
  previous_parameter_[0] = smoothed_parameter_ >> 1;
}

#define SEMI * 128

const uint16_t chords[17][3] = {
  { 4, 8, 12 },  // Fat
  { 16, 32, 48 },  // Superfat
  { 4, 7 SEMI, 4 + 7 SEMI },  // Fat power
  { 4, 12 SEMI, 4 + 12 SEMI },  // Fat octave
  { 12 SEMI, 24 SEMI, 36 SEMI},  // Octaves
  { 12 SEMI, 7 SEMI, 19 SEMI },  // Power
  { 4 SEMI,  7 SEMI , 12 SEMI },  // Major
  { 4 SEMI,  7 SEMI , 11 SEMI },  // Major7
  { 3 SEMI,  7 SEMI , 10 SEMI },  // Minor7
  { 3 SEMI,  7 SEMI , 12 SEMI },  // Minor
  { 2 SEMI,  7 SEMI , 12 SEMI },  // Sus2
  { 4 SEMI,  7 SEMI , 12 SEMI },  // Sus4
  { 3 SEMI,  7 SEMI , 14 SEMI },  // Minor9
  { 4 SEMI,  7 SEMI , 14 SEMI },  // Major9
  { 3 SEMI,  7 SEMI , 16 SEMI },  // Minor11
  { 4 SEMI,  7 SEMI , 16 SEMI },  // Major11
  { 4 SEMI,  7 SEMI , 16 SEMI },  // Major11
};

void DigitalOscillator::RenderWaveParaphonic(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  if (strike_) {
    for (uint8_t i = 0; i < 4; ++i) {
      state_.saw.phase[i] = Random::GetWord();
    }
    strike_ = false;
  }
  
  // Do not use an array here to allow these to be kept in arbitrary registers.
  uint32_t phase_0, phase_1, phase_2, phase_3;
  uint32_t phase_increment[3];
  uint32_t phase_increment_0;

  phase_increment_0 = phase_increment_;
  phase_0 = state_.saw.phase[0];
  phase_1 = state_.saw.phase[1];
  phase_2 = state_.saw.phase[2];
  phase_3 = state_.saw.phase[3];
  
  for (uint8_t i = 0; i < 3; ++i) {
    uint16_t detune_1 = chords[parameter_[1] >> 11][i];
    uint16_t detune_2 = chords[((parameter_[1] >> 10) + 1) >> 1][i];
    uint16_t detune_xfade = parameter_[1] << 6;
    uint16_t detune = detune_1 + ((detune_2 - detune_1) * detune_xfade >> 16);
    phase_increment[i] = ComputePhaseIncrement(pitch_ + detune);
  }

  const uint8_t* wave_1 = wt_waves + mini_wave_line[parameter_[0] >> 10] * 129;
  const uint8_t* wave_2 = wt_waves + mini_wave_line[(parameter_[0] >> 10) + 1] * 129;
  uint16_t wave_xfade = parameter_[0] << 6;
  
  while (size) {
    int32_t sample = 0;
    
    phase_0 += phase_increment_0;
    phase_1 += phase_increment[0];
    phase_2 += phase_increment[1];
    phase_3 += phase_increment[2];

    sample += Crossfade(wave_1, wave_2, WarpPhase(phase_0) >> 1, wave_xfade);
    sample += Crossfade(wave_1, wave_2, WarpPhase(phase_1) >> 1, wave_xfade);
    sample += Crossfade(wave_1, wave_2, WarpPhase(phase_2) >> 1, wave_xfade);
    sample += Crossfade(wave_1, wave_2, WarpPhase(phase_3) >> 1, wave_xfade);
    *buffer++ = sample >> 2;

    phase_0 += phase_increment_0;
    phase_1 += phase_increment[0];
    phase_2 += phase_increment[1];
    phase_3 += phase_increment[2];
    
    sample = 0;
    sample += Crossfade(wave_1, wave_2, WarpPhase(phase_0) >> 1, wave_xfade);
    sample += Crossfade(wave_1, wave_2, WarpPhase(phase_1) >> 1, wave_xfade);
    sample += Crossfade(wave_1, wave_2, WarpPhase(phase_2) >> 1, wave_xfade);
    sample += Crossfade(wave_1, wave_2, WarpPhase(phase_3) >> 1, wave_xfade);
    *buffer++ = sample >> 2;
    size -= 2;
  }
  
  state_.saw.phase[0] = phase_0;
  state_.saw.phase[1] = phase_1;
  state_.saw.phase[2] = phase_2;
  state_.saw.phase[3] = phase_3;

}

/*
// void DigitalOscillator::RenderFilteredNoise(
//     const uint8_t* sync,
//     int16_t* buffer,
//     uint8_t size) {
//   int32_t f = Interpolate824(lut_svf_cutoff, pitch_ << 17);
//   int32_t damp = Interpolate824(lut_svf_damp, parameter_[0] << 17);
//   int32_t scale = Interpolate824(lut_svf_scale, parameter_[0] << 17);
//   int32_t bp = state_.svf.bp;
//   int32_t lp = state_.svf.lp;
//   int32_t bp_gain, lp_gain, hp_gain;
//   
//   // Morph between LP, BP, HP.
//   if (parameter_[1] < 16384) {
//     bp_gain = parameter_[1];
//     lp_gain = 16384 - bp_gain;
//     hp_gain = 0;
//   } else {
//     bp_gain = 32767 - parameter_[1];
//     hp_gain = parameter_[1] - 16384;
//     lp_gain = 0;
//   }
//   
//   int32_t gain_correction = f > scale ? scale * 32767 / f : 32767;
//   while (size--) {
//     int32_t notch, hp, in;
//     
//     in = Random::GetSample() >> 1;
//     notch = in - (bp * damp >> 15);
//     lp += f * bp >> 15;
//     CLIP(lp)
//     hp = notch - lp;
//     bp += f * hp >> 15;
//     
//     int32_t result = 0;
//     result += (lp_gain * lp) >> 14;
//     result += (bp_gain * bp) >> 14;
//     result += (hp_gain * hp) >> 14;
//     CLIP(result)
//     result = result * gain_correction >> 15;
//     *buffer++ = Interpolate88(ws_moderate_overdrive, result + 32768);
//   }
//   state_.svf.lp = lp;
//   state_.svf.bp = bp;
// }
*/

/*
// void DigitalOscillator::RenderTwinPeaksNoise(
//     const uint8_t* sync,
//     int16_t* buffer,
//     uint8_t size) {
//   int32_t sample;
//   int32_t y10, y20;
//   int32_t y11 = state_.pno.filter_state[0][0];
//   int32_t y12 = state_.pno.filter_state[0][1];
//   int32_t y21 = state_.pno.filter_state[1][0];
//   int32_t y22 = state_.pno.filter_state[1][1];
//   uint32_t q = 65240 + (parameter_[0] >> 7);
//   int32_t q_squared = q * q >> 17;
//   int16_t p1 = pitch_;
// 
//   CONSTRAIN(p1, 0, 16383)
//   int32_t c1 = Interpolate824(lut_resonator_coefficient, p1 << 17);
//   int32_t s1 = Interpolate824(lut_resonator_scale, p1 << 17);
//   
//   int16_t p2 = pitch_ + ((parameter_[1] - 16384) >> 1);
//   CONSTRAIN(p2, 0, 16383)
//   int32_t c2 = Interpolate824(lut_resonator_coefficient, p2 << 17);
//   int32_t s2 = Interpolate824(lut_resonator_scale, p2 << 17);
// 
//   c1 = c1 * q >> 16;
//   c2 = c2 * q >> 16;
// 
//   int32_t makeup_gain = 8191 - (parameter_[0] >> 2);
//   
//   while (size) {    
//     sample = Random::GetSample() >> 1;
//     
//     if (sample > 0) {
//       y10 = sample * s1 >> 16;
//       y20 = sample * s2 >> 16;
//     } else {
//       y10 = -((-sample) * s1 >> 16);
//       y20 = -((-sample) * s2 >> 16);
//     }
//     
//     y10 += y11 * c1 >> 15;
//     y10 -= y12 * q_squared >> 15;
//     CLIP(y10)
//     y12 = y11;
//     y11 = y10;
//     
//     y20 += y21 * c2 >> 15;
//     y20 -= y22 * q_squared >> 15;
//     CLIP(y20)
//     y22 = y21;
//     y21 = y20;
//     
//     y10 += y20;
//     y10 += (y10 * makeup_gain >> 13);
//     CLIP(y10)
//     sample = y10;
//     sample = Interpolate88(ws_moderate_overdrive, sample + 32768);
//     
//     *buffer++ = sample;
//     *buffer++ = sample;
//     size -= 2;
//   }
//   
//   state_.pno.filter_state[0][0] = y11;
//   state_.pno.filter_state[0][1] = y12;
//   state_.pno.filter_state[1][0] = y21;
//   state_.pno.filter_state[1][1] = y22;
// }
*/

void DigitalOscillator::RenderClockedNoise(
     const uint8_t* sync,
     int16_t* buffer,
     uint8_t size) {
   ClockedNoiseState* state = &state_.clk;
   
   if ((parameter_[1] > previous_parameter_[1] + 64) ||
       (parameter_[1] < previous_parameter_[1] - 64)) {
     previous_parameter_[1] = parameter_[1];
   }
   if ((parameter_[0] > previous_parameter_[0] + 16) ||
       (parameter_[0] < previous_parameter_[0] - 16)) {
     previous_parameter_[0] = parameter_[0];
   }
   
   
   if (strike_) {
     state->seed = Random::GetWord();
     strike_ = false;
   }
   
   // Shift the range of the Coarse knob to reach higher clock rates, close
   // to the sample rate.
   uint32_t phase = phase_;
   uint32_t phase_increment = phase_increment_;
   uint8_t num_shifts = extra_[2] * 6 >> 7;  // RATE, 3 = original
   for (uint8_t i = 0; i < num_shifts; ++i) {
     if (phase_increment < (1UL << 31)) {
       phase_increment <<= 1;
     }
   }
   int32_t slew = extra_[0] ? 32767 - (extra_[0] << 8) : 0;  // SLEW
   
   // Compute the period of the random generator.
   state->cycle_phase_increment = ComputePhaseIncrement(
       previous_parameter_[0] - 16384) << 1;
   
   // Compute the number of quantization steps
   uint32_t num_steps = 1 + (previous_parameter_[1] >> 10);
   if (num_steps == 1) {
     num_steps = 2;
   }
   uint32_t quantizer_divider = 65536 / num_steps;
   while (size--) {
     phase += phase_increment;
     if (*sync++) {
       phase = 0;
     }
     
     // Clock.
     if (phase < phase_increment) {
       state->rng_state = state->rng_state * 1664525L + 1013904223L;
       // RAND: chance to break out of the loop with a new random value.
       if (extra_[1] && (Random::GetWord() >> 25) < extra_[1]) {
         state->rng_state = Random::GetWord();
       }
       state->cycle_phase += state->cycle_phase_increment;
       // Enforce period
       if (state->cycle_phase < state->cycle_phase_increment) {
         state->rng_state = state->seed;
         // Make the period an integer.
         state->cycle_phase = state->cycle_phase_increment;
       }
       uint16_t sample = state->rng_state;
       sample -= sample % quantizer_divider;
       sample += quantizer_divider >> 1;
       state->sample = sample;
       // Make the clock rate an exact divisor of the sample rate.
       phase = phase_increment;
     }
     if (slew) {
       state->slewed += (state->sample - state->slewed) * slew >> 15;
       *buffer++ = state->slewed;
     } else {
       *buffer++ = state->sample;
     }
   }
   phase_ = phase;
}

void DigitalOscillator::RenderGranularCloud(
     const uint8_t* sync,
     int16_t* buffer,
     uint8_t size) {
   
   uint32_t density = 512 + extra_[0] * 248;  // DENS, 0x4000 = original
   for (size_t i = 0; i < 4; ++i) {
     Grain* g = &state_.grain[i];
     // If a grain has reached the end of its envelope, reset it.
     if (g->envelope_phase > (1 << 24) ||
         g->envelope_phase_increment == 0) {
       g->envelope_phase_increment = 0;
       if ((Random::GetWord() & 0xffff) < density) {
         g->envelope_phase_increment = \
             lut_granular_envelope_rate[parameter_[0] >> 7] << 3;
         g->envelope_phase = 0;
         g->phase_increment = phase_increment_;
         int32_t pitch_mod = Random::GetSample() * parameter_[1] >> 16;
         int32_t phi = phase_increment_ >> 8;
         if (pitch_mod < 0) {
           g->phase_increment += phi * (pitch_mod >> 8);
         } else {
           g->phase_increment += phi * (pitch_mod >> 7);
         }
         // SCAT: octave scattering.
         if (extra_[2] && (Random::GetWord() >> 25) < extra_[2]) {
           if (Random::GetWord() & 1) {
             g->phase_increment <<= 1;
           } else {
             g->phase_increment >>= 1;
           }
         }
       }
     }
   }
   
   // TODO(pichenettes): Check if it's possible to interpolate envelope
   // increment too!
   while (size--) {
     int32_t sample = 0;
     state_.grain[0].phase += state_.grain[0].phase_increment;
     state_.grain[0].envelope_phase += state_.grain[0].envelope_phase_increment;
     sample += Interpolate824(wav_sine, state_.grain[0].phase) * \
         lut_granular_envelope[state_.grain[0].envelope_phase >> 16] >> 17;
 
     state_.grain[1].phase += state_.grain[1].phase_increment;
     state_.grain[1].envelope_phase += state_.grain[1].envelope_phase_increment;
     sample += Interpolate824(wav_sine, state_.grain[1].phase) * \
         lut_granular_envelope[state_.grain[1].envelope_phase >> 16] >> 17;
 
     state_.grain[2].phase += state_.grain[2].phase_increment;
     state_.grain[2].envelope_phase += state_.grain[2].envelope_phase_increment;
     sample += Interpolate824(wav_sine, state_.grain[2].phase) * \
         lut_granular_envelope[state_.grain[2].envelope_phase >> 16] >> 17;
 
     state_.grain[3].phase += state_.grain[3].phase_increment;
     state_.grain[3].envelope_phase += state_.grain[3].envelope_phase_increment;
     sample += Interpolate824(wav_sine, state_.grain[3].phase) * \
         lut_granular_envelope[state_.grain[3].envelope_phase >> 16] >> 17;
     
     if (sample < -32768) {
       sample = -32768;
     }
     if (sample > 32767) {
       sample = 32767;
     }
     if (extra_[1]) {
       // WAVE: sine grains -> saw grains.
       int32_t saw = 0;
       for (uint8_t g = 0; g < 4; ++g) {
         saw += (static_cast<int32_t>(state_.grain[g].phase >> 16) - 32768) *
             lut_granular_envelope[state_.grain[g].envelope_phase >> 16] >> 17;
       }
       CLIP(saw)
       sample = Mix(static_cast<int16_t>(sample), static_cast<int16_t>(saw),
                    extra_[1] << 9);
     }
     *buffer++ = sample;
   }
}

/*
// static const uint16_t kParticleNoiseDecay = 64763;
// static const int32_t kResonanceSquared = 32768 * 0.996 * 0.996;
// static const int32_t kResonanceFactor = 32768 * 0.996;
// 
// void DigitalOscillator::RenderParticleNoise(
//     const uint8_t* sync,
//     int16_t* buffer,
//     uint8_t size) {
//   uint16_t amplitude = state_.pno.amplitude;
//   uint32_t density = 1024 + parameter_[0];
//   int32_t sample;
//   
//   int32_t y10, y20, y30;
//   int32_t y11 = state_.pno.filter_state[0][0];
//   int32_t y12 = state_.pno.filter_state[0][1];
//   int32_t s1 = state_.pno.filter_scale[0];
//   int32_t c1 = state_.pno.filter_coefficient[0];
//   int32_t y21 = state_.pno.filter_state[1][0];
//   int32_t y22 = state_.pno.filter_state[1][1];
//   int32_t s2 = state_.pno.filter_scale[1];
//   int32_t c2 = state_.pno.filter_coefficient[1];
//   int32_t y31 = state_.pno.filter_state[2][0];
//   int32_t y32 = state_.pno.filter_state[2][1];
//   int32_t s3 = state_.pno.filter_scale[2];
//   int32_t c3 = state_.pno.filter_coefficient[2];
// 
//   while (size) {
//     uint32_t noise = Random::GetWord();
//     if ((noise & 0x7fffff) < density) {
//       amplitude = 65535;
//       int16_t noise_a = (noise & 0x0fff) - 0x800;
//       int16_t noise_b = ((noise >> 15) & 0x1fff) - 0x1000;
//       int16_t p1 = pitch_ + (3 * noise_a * parameter_[1] >> 17) + 0x600;
// 
//       CONSTRAIN(p1, 0, 16383)
//       c1 = Interpolate824(lut_resonator_coefficient, p1 << 17);
//       s1 = Interpolate824(lut_resonator_scale, p1 << 17);
// 
//       int16_t p2 = pitch_ + (noise_a * parameter_[1] >> 15) + 0x980;
//       CONSTRAIN(p2, 0, 16383)
//       c2 = Interpolate824(lut_resonator_coefficient, p2 << 17);
//       s2 = Interpolate824(lut_resonator_scale, p2 << 17);
// 
//       int16_t p3 = pitch_ + (noise_b * parameter_[1] >> 16) + 0x790;
//       CONSTRAIN(p3, 0, 16383)
//       c3 = Interpolate824(lut_resonator_coefficient, p3 << 17);
//       s3 = Interpolate824(lut_resonator_scale, p3 << 17);
//       
//       c1 = c1 * kResonanceFactor >> 15;
//       c2 = c2 * kResonanceFactor >> 15;
//       c3 = c3 * kResonanceFactor >> 15;
//     }
//     sample = (static_cast<int16_t>(noise) * amplitude) >> 16;
//     amplitude = (amplitude * kParticleNoiseDecay) >> 16;
//     
//     if (sample > 0) {
//       y10 = sample * s1 >> 16;
//       y20 = sample * s2 >> 16;
//       y30 = sample * s3 >> 16;
//     } else {
//       y10 = -((-sample) * s1 >> 16);
//       y20 = -((-sample) * s2 >> 16);
//       y30 = -((-sample) * s3 >> 16);
//     }
//     
//     y10 += y11 * c1 >> 15;
//     y10 -= y12 * kResonanceSquared >> 15;
//     CLIP(y10);
//     y12 = y11;
//     y11 = y10;
//     
//     y20 += y21 * c2 >> 15;
//     y20 -= y22 * kResonanceSquared >> 15;
//     CLIP(y20);
//     y22 = y21;
//     y21 = y20;
//     
//     y30 += y31 * c3 >> 15;
//     y30 -= y32 * kResonanceSquared >> 15;
//     CLIP(y30);
//     y32 = y31;
//     y31 = y30;
//     
//     y10 += y20 + y30;
//     CLIP(y10)
//     *buffer++ = y10;
//     *buffer++ = y10;
//     size -= 2;
//   }
//   
//   state_.pno.amplitude = amplitude;
//   state_.pno.filter_state[0][0] = y11;
//   state_.pno.filter_state[0][1] = y12;
//   state_.pno.filter_scale[0] = s1;
//   state_.pno.filter_coefficient[0] = c1;
//   state_.pno.filter_state[1][0] = y21;
//   state_.pno.filter_state[1][1] = y22;
//   state_.pno.filter_scale[1] = s2;
//   state_.pno.filter_coefficient[1] = c2;
//   state_.pno.filter_state[2][0] = y31;
//   state_.pno.filter_state[2][1] = y32;
//   state_.pno.filter_scale[2] = s3;
//   state_.pno.filter_coefficient[2] = c3;
// }
*/

// -----------------------------------------------------------------------------
//
// MutatedHypnoise engines.
//
// Note: wav_sine holds -cos (it starts at -32767): a phase of 1 << 30
// (quarter cycle) is the zero crossing going up, i.e. sin(0).

static const uint32_t kSinePhaseOffset = 1UL << 30;

// VMRF - vowel morph with formant shift. Three sine formants restarted at
// every pitch period under a decaying (squared) window.
//   TIMBRE: continuous a-e-i-o-u morph (tenor formant data).
//   COLOR:  shifts all formants by up to +/-1 octave, independently from the
//           pitch (center = natural vowel; down = giant, up = tiny).
void DigitalOscillator::RenderVowelMorph(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  VowelMorphState& v = state_.vmf;
  int32_t shift = (parameter_[1] - 16384) * 3 >> 5;  // +/-1536 = 1 octave
  int16_t voice = extra_[0] << 8;  // VOIC: bass .. soprano (32 = tenor)
  for (uint8_t i = 0; i < 3; ++i) {
    int32_t f = InterpolateFormantParameter(
        formant_f_data, voice, parameter_[0], i) + shift;
    v.formant_increment[i] = ComputePhaseIncrement(f);
    v.formant_amplitude[i] = InterpolateFormantParameter(
        formant_a_data, voice, parameter_[0], i);
  }
  while (size--) {
    phase_ += phase_increment_;
    if (*sync++) {
      phase_ = 0;
    }
    if (phase_ < phase_increment_) {
      // Restart each formant at its upward zero crossing (no click).
      v.formant_phase[0] = v.formant_phase[1] = v.formant_phase[2] =
          kSinePhaseOffset;
    }
    uint32_t window = 65535 - (phase_ >> 16);
    if (extra_[1] == 64) {
      window = window * window >> 17;  // 0..32767
    } else {
      // WIND: linear (0) .. squared (64) .. cubed (127) window.
      uint32_t squared = window * window >> 16;
      uint32_t a = extra_[1] < 64 ? window : squared;
      uint32_t b = extra_[1] < 64 ? squared : squared * window >> 16;
      uint32_t x = extra_[1] < 64 ? extra_[1] << 10 : (extra_[1] - 64) << 10;
      window = (a + ((static_cast<int32_t>(b - a) >> 1) *
                     static_cast<int32_t>(x) >> 15)) >> 1;
    }

    v.formant_phase[0] += v.formant_increment[0];
    v.formant_phase[1] += v.formant_increment[1];
    v.formant_phase[2] += v.formant_increment[2];
    int32_t sample = Interpolate824(wav_sine, v.formant_phase[0]) *
        v.formant_amplitude[0] >> 15;
    sample += wav_sine[v.formant_phase[1] >> 24] *
        v.formant_amplitude[1] >> 15;
    sample += wav_sine[v.formant_phase[2] >> 24] *
        v.formant_amplitude[2] >> 15;
    if (extra_[2]) {
      sample += Random::GetSample() * extra_[2] >> 7;  // BRTH
    }
    sample = sample * static_cast<int32_t>(window) >> 14;
    // Partial formant cycles under the window are not zero-mean.
    v.dc += (sample - v.dc) >> 10;  // ~15 Hz
    sample -= v.dc;
    CLIP(sample)
    *buffer++ = Interpolate88(ws_moderate_overdrive, sample + 32768);
  }
}

// VOS2 - VOSIM (Kaegi & Tempelaars): a train of sin^2 pulses at the formant
// frequency, each one attenuated by a constant ratio, restarted at every
// pitch period.
//   TIMBRE: formant (pulse) frequency, MIDI 48..108.
//   COLOR:  pulse decay, from short buzzy bursts to long resonant ringing.
void DigitalOscillator::RenderVosimPlus(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  VosimPlusState& v = state_.vsp;
  uint32_t base_increment = ComputePhaseIncrement(
      (48 << 7) + (parameter_[0] * 60 >> 8) + OctaveShift(extra_[2]));
  uint32_t pulse_increment = extra_[0] ? v.pulse_increment : base_increment;
  int32_t chirp = 32768 - (extra_[0] << 5);  // CHRP: per-pulse ratio, Q15
  uint8_t max_pulses = extra_[1] == 127 ? 255 : 1 + (extra_[1] >> 3);
  int32_t decay = 16384 + (parameter_[1] >> 1);
  if (decay > 32440) {
    decay = 32440;  // 0.99
  }
  uint32_t pulse_phase = v.pulse_phase;
  int32_t amplitude = v.amplitude;
  int32_t dc = v.dc;
  while (size--) {
    phase_ += phase_increment_;
    if (*sync++) {
      phase_ = 0;
    }
    if (phase_ < phase_increment_) {
      pulse_phase = 0;
      amplitude = 32767;
      pulse_increment = base_increment;
      v.pulse_count = 0;
    }
    pulse_phase += pulse_increment;
    if (pulse_phase < pulse_increment) {
      amplitude = amplitude * decay >> 15;
      // PULS: silence after N pulses (classic VOSIM). CHRP: each pulse a
      // bit lower than the previous one.
      if (extra_[1] != 127 && ++v.pulse_count >= max_pulses) {
        amplitude = 0;
      }
      if (extra_[0]) {
        pulse_increment = (pulse_increment >> 15) * chirp;
      }
    }
    // sin^2(pi t) = (1 - cos(2 pi t)) / 2, and wav_sine is -cos.
    int32_t pulse = (32767 + wav_sine[pulse_phase >> 24]) >> 1;
    int32_t sample = pulse * amplitude >> 15;
    dc += (sample - dc) >> 10;  // DC tracker, ~15 Hz
    sample -= dc;
    CLIP(sample)
    *buffer++ = sample;
  }
  v.pulse_phase = pulse_phase;
  v.pulse_increment = pulse_increment;
  v.amplitude = amplitude;
  v.dc = dc;
}

// DSWM - drift swarm. Seven saws spread around the pitch, each one
// wandering on its own slow random walk.
//   TIMBRE: spread, up to +/-3 semitones between the outer voices.
//   COLOR:  drift depth, up to +/-4 semitones per voice.
//   TRIG:   new drift targets for all voices ("bloom").
void DigitalOscillator::RenderDriftSwarm(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  DriftSwarmState& d = state_.dsw;
  if (init_) {
    for (uint8_t i = 0; i < kNumDriftVoices; ++i) {
      d.phase[i] = Random::GetWord();
    }
    init_ = false;
  }
  if (strike_) {
    for (uint8_t i = 0; i < kNumDriftVoices; ++i) {
      d.target[i] = Random::GetSample();
    }
    strike_ = false;
  }
  // One voice gets a new target every 128 ms (each one every ~0.9 s) at
  // RATE = 64; RATE scales that by 1/4..2.
  uint8_t rate_shift = 13 - (extra_[0] >> 5);
  uint16_t counter = ++d.counter;
  if ((counter & ((1 << (rate_shift - 2)) - 1)) == 0) {
    d.target[(counter >> (rate_shift - 2)) % kNumDriftVoices] =
        Random::GetSample();
  }
  uint8_t num_voices = 1 + (extra_[2] * 6 + 63) / 127;  // VOIC
  int32_t spread = parameter_[0] * parameter_[0] >> 23;  // 0..127 per voice
  int32_t depth = parameter_[1] >> 7;
  depth = depth * depth >> 7;  // 0..508

  uint32_t increment[kNumDriftVoices];
  for (uint8_t i = 0; i < kNumDriftVoices; ++i) {
    // ~0.5 s glide to the target (at the 4 kHz block rate).
    d.drift[i] += ((static_cast<int32_t>(d.target[i]) << 8) - d.drift[i]) >>
        rate_shift;
    int32_t pitch = pitch_ +
        (spread * (2 * static_cast<int32_t>(i) - (num_voices - 1)) >> 1) +
        ((d.drift[i] >> 8) * depth >> 15);
    increment[i] = ComputePhaseIncrement(pitch);
  }
  while (size--) {
    if (*sync++) {
      memset(d.phase, 0, sizeof(d.phase));
    }
    int32_t sample = 0;
    int32_t triangle = 0;
    for (uint8_t i = 0; i < num_voices; ++i) {
      d.phase[i] += increment[i];
      sample += d.phase[i] >> 20;
      if (extra_[1]) {
        uint32_t p = d.phase[i];
        triangle += (p & 0x80000000UL ? ~p : p) >> 19;
      }
    }
    sample = (sample - (num_voices << 11)) << 1;
    if (extra_[1]) {
      // WAVE: saws -> triangles.
      triangle = (triangle - (num_voices << 11)) << 1;
      CLIP(sample)
      CLIP(triangle)
      sample = Mix(static_cast<int16_t>(sample), static_cast<int16_t>(triangle),
                   extra_[1] << 9);
    }
    if (num_voices < kNumDriftVoices) {
      sample = sample * kNumDriftVoices / num_voices;
    }
    CLIP(sample)
    *buffer++ = Interpolate88(ws_moderate_overdrive, sample + 32768);
  }
}

// FDRN - fold drone. Two sines (root and root + interval) summed into a
// sine wavefolder.
//   TIMBRE: fold depth (gentle saturation -> dense folded harmonics), with a
//           slow internal "breathing" LFO.
//   COLOR:  interval of the second sine, 0..19 semitones (slightly detuned,
//           so the unison beats).
//   TRIG:   a decaying fold burst ("ping"), for rhythmic patterns.
void DigitalOscillator::RenderFoldDrone(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  FoldDroneState& f = state_.fdr;
  uint32_t increment_b = ComputePhaseIncrement(
      pitch_ + 6 + (parameter_[1] * 19 >> 8));
  if (strike_) {
    f.burst = 32767;
    strike_ = false;
  }
  f.burst -= f.burst >> (7 + (extra_[2] >> 5));  // BRST: ~130 ms at 64
  f.lfo_phase += 53687 * extra_[0] >> 6;  // RATE: ~0.05 Hz at 64
  int32_t lfo = wav_sine[f.lfo_phase >> 24];
  // Drive in Q12: 1 (sin(pi/2 x)) to 8, +/-12.5% breathing, + up to 4 burst.
  int32_t drive = 4096 + (parameter_[0] * 7 >> 3);
  drive += ((drive >> 3) * extra_[1] >> 6) * lfo >> 15;  // DPTH
  drive += f.burst >> 1;
  while (size--) {
    phase_ += phase_increment_;
    f.phase[1] += increment_b;
    if (*sync++) {
      phase_ = f.phase[1] = 0;
    }
    int32_t sum = (Interpolate824(wav_sine, phase_) +
                   Interpolate824(wav_sine, f.phase[1])) >> 1;
    // |sum| * drive < 2^31; drive = 1 maps full scale to a quarter cycle.
    // + a quarter cycle: sin() (odd, no DC) instead of -cos().
    uint32_t fold_phase = static_cast<uint32_t>(sum * drive) << 3;
    *buffer++ = Interpolate824(wav_sine, fold_phase + kSinePhaseOffset);
  }
}

// SPEC - spectral drone. Eight additive partials.
//   TIMBRE: moves a spectral peak from the fundamental to the 8th partial.
//   COLOR:  compresses (below center) or stretches (above) the partial
//           series by up to 12.5%: bell-like, inharmonic spectra.
//   Each partial shimmers with its own very slow LFO.
//   TRIG:   moves the shimmer LFOs to a new random place ("spectral bloom").
static const int16_t kPartialPitch[kNumSpectralPartials] = {
  // 12 * 128 * log2(n)
  0, 1536, 2435, 3072, 3567, 3971, 4312, 4608
};

void DigitalOscillator::RenderSpectralDrone(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  SpectralDroneState& s = state_.spc;
  if (strike_) {
    s.shimmer_phase = Random::GetWord();
    strike_ = false;
  }
  s.shimmer_phase += 26843 * extra_[1] >> 6;  // SRAT: ~0.025 Hz at 64
  int32_t width = 64 + extra_[2] * 7;  // WIDT: 512 (2 partials) at 64
  int32_t shimmer_base = 32768 - (extra_[0] << 6);  // SHIM
  int32_t stretch = (parameter_[1] - 16384) >> 3;  // Q14, +/-0.125
  int32_t center = parameter_[0] * 7 >> 7;         // partial index, Q8
  for (uint8_t i = 0; i < kNumSpectralPartials; ++i) {
    int32_t pitch = pitch_ + kPartialPitch[i] +
        (kPartialPitch[i] * stretch >> 14);
    s.increment[i] = ComputePhaseIncrement(pitch);
    int32_t distance = (static_cast<int32_t>(i) << 8) - center;
    if (distance < 0) {
      distance = -distance;
    }
    int32_t target = distance < width ? (width - distance) * 24576 / width : 0;
    target += 6144 >> i;  // 1/2^n floor keeps a body under the peak
    int32_t lfo = wav_sine[(s.shimmer_phase * (i + 3)) >> 24];
    target = target * (shimmer_base + (lfo * extra_[0] >> 9)) >> 15;
    if (pitch > (127 << 7)) {
      target = 0;  // above ~12.5 kHz: dropped (no aliasing)
    }
    s.amplitude[i] += (target - s.amplitude[i]) >> 4;
  }
  while (size--) {
    if (*sync++) {
      memset(s.phase, 0, sizeof(s.phase));
    }
    int32_t sample = 0;
    for (uint8_t i = 0; i < kNumSpectralPartials; ++i) {
      s.phase[i] += s.increment[i];
      sample += wav_sine[s.phase[i] >> 24] * s.amplitude[i] >> 15;
    }
    sample >>= 1;
    CLIP(sample)
    *buffer++ = sample;
  }
}

// TANP - tanpura (ported from the MAYA branch). Four strings plucked in an
// endless cycle: first string (TUNE: Pa, Ma, Ni or Sa), Sa, Sa (DTUN:
// detuned against the other Sa) and the low Sa.
//   TIMBRE: pluck rate (one string every ~2 s .. ~0.25 s) and organic
//           detune of the strings.
//   COLOR:  jawari, the bridge buzz: a wavefolder whose depth follows the
//           decay of each string (bright attack, mellowing tail).
//   TRIG:   plucks the next string at once. DCAY: string decay time.
static const int16_t kTanpuraFirstString[4] = { -640, -896, -128, 0 };

// Sawtooth with a polyBLEP correction around the reset.
static inline int32_t BlepSaw(uint32_t phase, uint32_t increment) {
  int32_t saw = static_cast<int32_t>(phase >> 16) - 32768;
  uint32_t scale = increment >> 16;
  if (scale == 0) {
    return saw;
  }
  if (phase < increment) {
    int32_t x = phase / scale;
    saw -= ((x << 1) - (x * (x >> 1) >> 15) - 65536) >> 1;
  } else if (phase > ~increment) {
    int32_t x = (~phase) / scale;
    saw -= ((x * (x >> 1) >> 15) - (x << 1) + 65536) >> 1;
  }
  return saw;
}

void DigitalOscillator::RenderTanpura(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  TanpuraState& t = state_.tnp;
  if (init_) {
    // Start with the four strings sounding.
    for (uint8_t k = 0; k < 4; ++k) {
      t.attack[k] = 1;
      t.phase[k] = Random::GetWord();
    }
    init_ = false;
    strike_ = false;
  }
  int32_t rate = parameter_[0];
  int32_t jawari = parameter_[1];

  uint32_t cycle_increment = 536870 + rate * 118;
  t.cycle_phase += cycle_increment;
  if (t.cycle_phase < cycle_increment || strike_) {
    if (strike_) {
      t.cycle_phase = 0;
      strike_ = false;
    }
    t.attack[t.next_string] = 1;
    t.next_string = (t.next_string + 1) & 3;
  }

  int32_t offset[4] = {
    kTanpuraFirstString[extra_[0] >> 5], 0, extra_[2] >> 2, -1536
  };
  int32_t decay = 1 + ((127 - extra_[1]) * 12 >> 7);
  uint32_t increment[4];
  int32_t level[4];
  int32_t fold_gain[4];
  int32_t total = 0;
  for (uint8_t k = 0; k < 4; ++k) {
    int32_t* envelope = &t.envelope[k];
    if (t.attack[k]) {
      *envelope += ((32767 << 8) - *envelope) >> 2;
      if (*envelope > (30000 << 8)) {
        t.attack[k] = 0;
      }
    } else {
      *envelope -= (*envelope >> 16) * decay;
    }
    int32_t envelope_level = *envelope >> 8;
    // Organic detune: slow random walk, up to ~12 cents with TIMBRE.
    if ((Random::GetWord() >> 16) <= 64) {
      t.drift_target[k] = Random::GetSample();
    }
    t.drift[k] += ((static_cast<int32_t>(t.drift_target[k]) << 8) -
                   t.drift[k]) >> 11;
    int32_t detune = (t.drift[k] >> 8) * (rate >> 11) >> 15;
    int32_t pitch = pitch_ + offset[k] + detune;
    if (pitch < 0) pitch = 0;
    increment[k] = ComputePhaseIncrement(pitch);
    level[k] = envelope_level * 24576 >> 15;
    total += level[k];
    // Jawari: fold depth 1x .. 8x, following the envelope.
    fold_gain[k] = 4096 + ((jawari * envelope_level >> 15) * 7 >> 3);
  }
  total >>= 1;
  int32_t gain = total > 26000 ? (26000 << 15) / total : 32768;

  while (size--) {
    int32_t sum = 0;
    for (uint8_t k = 0; k < 4; ++k) {
      t.phase[k] += increment[k];
      int32_t x = BlepSaw(t.phase[k], increment[k]);
      CLIP(x)
      uint32_t fold_phase = static_cast<uint32_t>(x * fold_gain[k]) << 3;
      int32_t y = Interpolate824(wav_sine, fold_phase + kSinePhaseOffset);
      sum += y * level[k] >> 16;  // >> 1: four strings
    }
    sum = sum * gain >> 15;
    CLIP(sum)
    *buffer++ = Interpolate88(ws_moderate_overdrive, sum + 32768);
  }
  (void)sync;
}

void DigitalOscillator::RenderSilence(
    const uint8_t* sync,
    int16_t* buffer,
    uint8_t size) {
  while (size--) {
    *buffer++ = 0;
  }
}

/* static */
DigitalOscillator::RenderFn DigitalOscillator::fn_table_[] = {
  &DigitalOscillator::RenderSawSwarm,
  &DigitalOscillator::RenderComb,
  &DigitalOscillator::RenderVosim,
  &DigitalOscillator::RenderVowel,
  &DigitalOscillator::RenderVowelFof,
  &DigitalOscillator::RenderFm,
  &DigitalOscillator::RenderFeedbackFm,
  &DigitalOscillator::RenderChaoticFeedbackFm,
  &DigitalOscillator::RenderPlucked,
  &DigitalOscillator::RenderBowed,
  &DigitalOscillator::RenderBlown,
  &DigitalOscillator::RenderFluted,
  &DigitalOscillator::RenderStruckBell,
  &DigitalOscillator::RenderWavetables,
  &DigitalOscillator::RenderWaveMap,
  &DigitalOscillator::RenderWaveLine,
  &DigitalOscillator::RenderWaveParaphonic,
  &DigitalOscillator::RenderClockedNoise,
  &DigitalOscillator::RenderGranularCloud,
  &DigitalOscillator::RenderVowelMorph,
  &DigitalOscillator::RenderVosimPlus,
  &DigitalOscillator::RenderDriftSwarm,
  &DigitalOscillator::RenderFoldDrone,
  &DigitalOscillator::RenderSpectralDrone,
  &DigitalOscillator::RenderTanpura,
  &DigitalOscillator::RenderSilence,
};

}  // namespace braids
