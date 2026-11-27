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
// Settings

#ifndef BRAIDS_SETTINGS_H_
#define BRAIDS_SETTINGS_H_

#include "stmlib/stmlib.h"

namespace braids {

enum MacroOscillatorShape {
  MACRO_OSC_SHAPE_CSAW,
  MACRO_OSC_SHAPE_MORPH,
  MACRO_OSC_SHAPE_SAW_SQUARE,
  MACRO_OSC_SHAPE_SQUARE_SYNC,
  MACRO_OSC_SHAPE_SINE_TRIANGLE,
  MACRO_OSC_SHAPE_BUZZ,

  MACRO_OSC_SHAPE_TRIPLE_SAW,
  MACRO_OSC_SHAPE_TRIPLE_SQUARE,
  MACRO_OSC_SHAPE_TRIPLE_TRIANGLE,
  MACRO_OSC_SHAPE_TRIPLE_SINE,

  // Digital block: from here on, order must match DigitalOscillatorShape.
  MACRO_OSC_SHAPE_SAW_SWARM,
  MACRO_OSC_SHAPE_SAW_COMB,
  MACRO_OSC_SHAPE_VOSIM,
  MACRO_OSC_SHAPE_VOWEL,
  MACRO_OSC_SHAPE_VOWEL_FOF,

  MACRO_OSC_SHAPE_FM,
  MACRO_OSC_SHAPE_FEEDBACK_FM,
  MACRO_OSC_SHAPE_CHAOTIC_FEEDBACK_FM,

  MACRO_OSC_SHAPE_PLUCKED,
  MACRO_OSC_SHAPE_BOWED,
  MACRO_OSC_SHAPE_BLOWN,
  MACRO_OSC_SHAPE_FLUTED,
  MACRO_OSC_SHAPE_STRUCK_BELL,

  MACRO_OSC_SHAPE_WAVETABLES,
  MACRO_OSC_SHAPE_WAVE_MAP,
  MACRO_OSC_SHAPE_WAVE_LINE,
  MACRO_OSC_SHAPE_WAVE_PARAPHONIC,

  MACRO_OSC_SHAPE_CLOCKED_NOISE,
  MACRO_OSC_SHAPE_GRANULAR_CLOUD,
  MACRO_OSC_SHAPE_VOWEL_MORPH,
  MACRO_OSC_SHAPE_VOSIM_PLUS,
  MACRO_OSC_SHAPE_DRIFT_SWARM,
  MACRO_OSC_SHAPE_FOLD_DRONE,
  MACRO_OSC_SHAPE_SPECTRAL_DRONE,
  MACRO_OSC_SHAPE_TANPURA,
  MACRO_OSC_SHAPE_SILENCE,

  MACRO_OSC_SHAPE_LAST,
  MACRO_OSC_SHAPE_LAST_ACCESSIBLE_FROM_META = MACRO_OSC_SHAPE_TANPURA
};

enum Resolution {
  RESOLUTION_2_BIT,
  RESOLUTION_3_BIT,
  RESOLUTION_4_BIT,
  RESOLUTION_6_BIT,
  RESOLUTION_8_BIT,
  RESOLUTION_12_BIT,
  RESOLUTION_16_BIT,
  RESOLUTION_LAST
};

enum SampleRate {
  SAMPLE_RATE_4K,
  SAMPLE_RATE_8K,
  SAMPLE_RATE_16K,
  SAMPLE_RATE_24k,
  SAMPLE_RATE_32k,
  SAMPLE_RATE_48K,
  SAMPLE_RATE_96K,
  SAMPLE_RATE_LAST
};


enum PitchQuantization {
  PITCH_QUANTIZATION_OFF,
  PITCH_QUANTIZATION_QUARTER_TONE,
  PITCH_QUANTIZATION_SEMITONE,
  PITCH_QUANTIZATION_IONIAN,
  PITCH_QUANTIZATION_DORIAN,
  PITCH_QUANTIZATION_PHRYGIAN,
  PITCH_QUANTIZATION_LYDIAN,
  PITCH_QUANTIZATION_MIXOLYDIAN,
  PITCH_QUANTIZATION_AEOLIAN,
  PITCH_QUANTIZATION_LOCRIAN,
  PITCH_QUANTIZATION_BLUES_MAJOR,
  PITCH_QUANTIZATION_BLUES_MINOR,
  PITCH_QUANTIZATION_PENTATONIC_MAJOR,
  PITCH_QUANTIZATION_PENTATONIC_MINOR,
  PITCH_QUANTIZATION_BHAIRAV,
  PITCH_QUANTIZATION_SHRI,
  PITCH_QUANTIZATION_RUPAVATI,
  PITCH_QUANTIZATION_TODI,
  PITCH_QUANTIZATION_RAGESHRI,
  PITCH_QUANTIZATION_KAAFI,
  PITCH_QUANTIZATION_MEGH,
  PITCH_QUANTIZATION_MALKAUNS,
  PITCH_QUANTIZATION_DEEPAK,
  PITCH_QUANTIZATION_FOLK,
  PITCH_QUANTIZATION_JAPANESE,
  PITCH_QUANTIZATION_GAMELAN,
  PITCH_QUANTIZATION_WHOLE_TONE,
  PITCH_QUANTIZATION_LAST
};

enum PitchRange {
  PITCH_RANGE_EXTERNAL,
  PITCH_RANGE_FREE,
  PITCH_RANGE_EXTENDED,
  PITCH_RANGE_440,
  PITCH_RANGE_LFO  // This setting is hidden by default! But now unhidden!!
};

// Physical control that drives the output filter cutoff or resonance.
enum FilterControlSource {
  FILTER_CONTROL_MENU,    // FCUT / FRES menu value only
  FILTER_CONTROL_FM,      // FM CV (+ FM attenuverter) offsets the menu value
  FILTER_CONTROL_TIMBRE,  // TIMBRE knob + CV, absolute; engine timbre centered
  FILTER_CONTROL_COLOR,   // COLOR knob + CV, absolute; engine color centered
  FILTER_CONTROL_LAST
};

enum DisplayView {
  DISPLAY_VIEW_NAME,    // engine name only (original behavior)
  DISPLAY_VIEW_PARAM,   // engine name + parameter popups
  DISPLAY_VIEW_SCOPE,   // scrolling output level + popups
  DISPLAY_VIEW_DIALS,   // needles: TIMBRE, COLOR, CUTOFF, RESONANCE + popups
  DISPLAY_VIEW_TOUR,    // screensaver touring the live parameters + popups
  DISPLAY_VIEW_FRACTAL, // screensaver: psychedelic fractal plasma + popups
DISPLAY_VIEW_LAST
};

// FMCV destinations.
enum FmDestination {
  FM_DESTINATION_FREQ,    // pitch (original Braids behavior)
  FM_DESTINATION_META,    // engine selection
  FM_DESTINATION_LEVEL,   // output level
  FM_DESTINATION_PARAM_1, // advanced engine parameters
  FM_DESTINATION_PARAM_2,
  FM_DESTINATION_PARAM_3,
  FM_DESTINATION_FOLD,    // output stage
  FM_DESTINATION_DRIVE,
  FM_DESTINATION_RING,
  FM_DESTINATION_SUB,
  FM_DESTINATION_JITTER,
  FM_DESTINATION_BITS,
  FM_DESTINATION_SAMPLE_RATE,
  FM_DESTINATION_CRUSH,   // bits + sample rate
  FM_DESTINATION_LAST
};

const uint8_t kNumRingRatios = 16;

// Settings are byte-indexed: the byte-mapped part of this enum must stay
// in the same order as SettingsData (checked in settings.cc).
enum Setting {
  SETTING_OSCILLATOR_SHAPE,
  SETTING_RESOLUTION,
  SETTING_SAMPLE_RATE,
  SETTING_TRIG_SOURCE,
  SETTING_TRIG_DELAY,
  SETTING_META_MODULATION,
  SETTING_PITCH_RANGE,
  SETTING_PITCH_OCTAVE,
  SETTING_PITCH_QUANTIZER,
  SETTING_VCO_DRIFT,
  SETTING_BRIGHTNESS,
  SETTING_OSC_SYNC,
  SETTING_FINE_TUNE,
  SETTING_INITIAL_GAIN,
  SETTING_RESET_TYPE,
  SETTING_PITCH_SAMPLE_HOLD,
  SETTING_EDIT_WRAP,
  SETTING_FILTER_MODE,
  SETTING_FILTER_CUTOFF,
  SETTING_FILTER_RESONANCE,
  SETTING_FILTER_TRACKING,
  SETTING_FILTER_CUTOFF_CONTROL,
  SETTING_FILTER_RESONANCE_CONTROL,
  SETTING_PERFORMANCE_MODE,
  SETTING_DISPLAY_VIEW,
  SETTING_FOLD,
  SETTING_DRIVE,
  SETTING_RING_AMOUNT,
  SETTING_RING_RATIO,
  SETTING_SUB_LEVEL,
  // Not byte-mapped: stored per engine (SettingsData::engine_params).
  SETTING_ENGINE_PARAM_1,
  SETTING_ENGINE_PARAM_2,
  SETTING_ENGINE_PARAM_3,
  SETTING_LAST_EDITABLE_SETTING = SETTING_ENGINE_PARAM_3,

  // Not settings per se, but used for menu display!
  SETTING_CALIBRATION,
  SETTING_CV_TESTER,
  SETTING_CPU_LOAD,
  SETTING_VERSION,
  SETTING_LAST,
};

struct SettingsData {
  uint8_t shape;
  uint8_t resolution;
  uint8_t sample_rate;
  uint8_t auto_trig;
  uint8_t trig_delay;
  uint8_t meta_modulation;   // FmDestination
  uint8_t pitch_range;
  uint8_t pitch_octave;
  uint8_t pitch_quantization;
  uint8_t vco_drift;
  uint8_t brightness;
  uint8_t osc_sync;
  uint8_t fine_tune;         // 0..62, 31 = 0
  uint8_t initial_gain;
  uint8_t reset_type;
  uint8_t pitch_sample_hold;
  uint8_t edit_wrap;
  uint8_t filter_mode;       // OutputFilterMode
  uint8_t filter_cutoff;     // 0..127 -> MIDI 12..139
  uint8_t filter_resonance;  // 0..127
  uint8_t filter_tracking;   // 0 = OFF, 1 = HALF, 2 = FULL
  uint8_t filter_cutoff_control;     // FilterControlSource
  uint8_t filter_resonance_control;  // FilterControlSource
  uint8_t performance_mode;  // COLOR = cutoff, FM = resonance
  uint8_t display_view;      // DisplayView
  uint8_t fold;              // output stage, 0 = bypass
  uint8_t drive;
  uint8_t ring_amount;
  uint8_t ring_ratio;
  uint8_t sub_level;
  uint8_t padding[2];
  int32_t pitch_cv_offset;
  int32_t pitch_cv_scale;
  int32_t fm_cv_offset;
  char magic_byte;
  uint8_t engine_params[MACRO_OSC_SHAPE_LAST][3];
};

struct SettingMetadata {
  uint8_t min_value;
  uint8_t max_value;
  const char name[5];
  const char* const* strings;

  int16_t Clip(int16_t value) const {
    if (value > max_value) {
      value = max_value;
    } else if (value < min_value) {
      value = min_value;
    }
    return value;
  }

  int16_t Wrap(int16_t value) const {
    if (value > max_value) {
      value = min_value;
    } else if (value < min_value) {
      value = max_value;
    }
    return value;
  }

};

class Settings {
 public:
  Settings() { }
  ~Settings() { }

  void Init();
  void Save();
  void Reset(bool except_cal_data);

  void SetValue(Setting setting, uint8_t value) {
    if (setting >= SETTING_ENGINE_PARAM_1 &&
        setting <= SETTING_ENGINE_PARAM_3) {
      data_.engine_params[data_.shape][setting - SETTING_ENGINE_PARAM_1] =
          value;
      return;
    }
    uint8_t* data = static_cast<uint8_t*>(static_cast<void*>(&data_));
    data[setting] = value;
  }

  uint8_t GetValue(Setting setting) const {
    if (setting >= SETTING_ENGINE_PARAM_1 &&
        setting <= SETTING_ENGINE_PARAM_3) {
      return data_.engine_params[data_.shape][
          setting - SETTING_ENGINE_PARAM_1];
    }
    const uint8_t* data = static_cast<const uint8_t*>(
        static_cast<const void*>(&data_));
    return data[setting];
  }

  // Name shown in the menu: the advanced parameters are named after what
  // they do in the current engine.
  const char* setting_name(Setting setting) const;

  // Default value of an editable setting (per engine for the advanced
  // parameters), and a reset of one setting to it.
  uint8_t default_value(Setting setting) const;
  void ResetValue(Setting setting) {
    SetValue(setting, default_value(setting));
  }

  inline const uint8_t* engine_params(MacroOscillatorShape shape) const {
    return data_.engine_params[shape];
  }

  inline MacroOscillatorShape shape() const {
    return static_cast<MacroOscillatorShape>(data_.shape);
  }

  inline Resolution resolution() const {
    return static_cast<Resolution>(data_.resolution);
  }

  inline SampleRate sample_rate() const {
    return static_cast<SampleRate>(data_.sample_rate);
  }

  inline PitchQuantization pitch_quantization() const {
    return static_cast<PitchQuantization>(data_.pitch_quantization);
  }

  inline uint16_t vco_drift() const {
    return data_.vco_drift;
  }

  inline uint8_t trig_delay() const {
    return data_.trig_delay;
  }

  inline bool osc_sync() const {
    return data_.osc_sync;
  }

  inline int32_t fine_tune() const {
    return static_cast<int32_t>(data_.fine_tune) - 31;
  }

  inline int32_t initial_gain() const {
    return (data_.initial_gain * 516);
  }

  inline bool pitch_sample_hold() const {
    return data_.pitch_sample_hold;
  }

  inline bool edit_wrap() const {
    return data_.edit_wrap;
  }

  inline const SettingsData& data() const { return data_; }
  inline SettingsData* mutable_data() { return &data_; }

  void Calibrate(
      int32_t adc_code_c2,
      int32_t adc_code_c4,
      int32_t adc_code_fm) {
    if (adc_code_c4 != adc_code_c2) {
      int32_t scale = (24 * 128 * 4096L) / (adc_code_c4 - adc_code_c2);
      data_.pitch_cv_scale = scale;
      data_.pitch_cv_offset = (60 << 7) -
          (scale * ((adc_code_c2 + adc_code_c4) >> 1) >> 12);
      data_.fm_cv_offset = adc_code_fm;
    }
    Save();
  }

  inline int32_t adc_to_pitch(int32_t pitch_adc_code) const {
    if (data_.pitch_range == PITCH_RANGE_EXTERNAL ||
        data_.pitch_range == PITCH_RANGE_LFO) {
      pitch_adc_code = pitch_adc_code * data_.pitch_cv_scale >> 12;
      pitch_adc_code += data_.pitch_cv_offset;
    } else if (data_.pitch_range == PITCH_RANGE_FREE) {
      pitch_adc_code = (pitch_adc_code - 1638);
      pitch_adc_code = pitch_adc_code * data_.pitch_cv_scale >> 12;
      pitch_adc_code += 60 << 7;
    } else if (data_.pitch_range == PITCH_RANGE_440) {
      pitch_adc_code = 69 << 7;
    } else {
      pitch_adc_code = (pitch_adc_code - 1638) * 9 >> 1;
      pitch_adc_code += 60 << 7;
    }
    return pitch_adc_code;
  }

  inline int32_t pitch_transposition() const {
    int32_t t = data_.pitch_range == PITCH_RANGE_LFO ? -36 << 7 : 0;
    t += (static_cast<int32_t>(data_.pitch_octave) - 2) * 12 * 128;
    return t;
  }

  inline int32_t adc_to_fm(int32_t fm_adc_code) const {
    fm_adc_code -= data_.fm_cv_offset;
    fm_adc_code = fm_adc_code * 7680 >> 12;
    if (data_.pitch_range == PITCH_RANGE_440) {
      fm_adc_code = 0;
    }
    return fm_adc_code;
  }

  static const SettingMetadata& metadata(Setting setting) {
    return metadata_[setting];
  }

  static const Setting setting_at_index(int16_t index) {
    return settings_order_[index];
  }

 private:

  SettingsData data_;

  uint16_t version_token_;

  static const SettingMetadata metadata_[SETTING_LAST];
  static const Setting settings_order_[SETTING_LAST];

  DISALLOW_COPY_AND_ASSIGN(Settings);
};

extern Settings settings;

}  // namespace braids

#endif  // BRAIDS_SETTINGS_H_
