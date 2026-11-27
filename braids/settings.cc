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

#include "braids/settings.h"

#include <cstddef>
#include <cstring>

// Replace with custom storage for version 4.1 and later
// #include "stmlib/system/storage.h"
#include "braids/engine_params.h"
#include "braids/page_storage.h"

namespace braids {

using namespace stmlib;

const SettingsData kInitSettings = {
  MACRO_OSC_SHAPE_CSAW,   // shape
  RESOLUTION_16_BIT,      // resolution
  SAMPLE_RATE_96K,        // sample_rate
  false,                  // auto_trig (Trig source)
  1,                      // trig_delay
  FM_DESTINATION_FREQ,    // meta_modulation
  PITCH_RANGE_EXTERNAL,   // pitch_range
  2,                      // pitch_octave
  PITCH_QUANTIZATION_OFF, // pitch_quantization
  0,                      // vco_drift
  2,                      // brightness
  false,                  // osc_sync
  31,                     // fine_tune = 0
  127,                    // initial_gain
  0,                      // reset_type
  false,                  // pitch_sample_hold
  false,                  // edit_wrap
  0,                      // filter_mode = OFF
  127,                    // filter_cutoff (fully open)
  0,                      // filter_resonance
  0,                      // filter_tracking = OFF
  0,                      // filter_cutoff_control = MENU
  0,                      // filter_resonance_control = MENU
  0,                      // performance_mode = OFF
  DISPLAY_VIEW_TOUR,      // display_view
  0,                      // fold = bypass
  0,                      // drive = bypass
  0,                      // ring_amount = off
  3,                      // ring_ratio = 1
  0,                      // sub_level = off
  { 0, 0 },               // padding
  50,                     // pitch_cv_offset
  15401,                  // pitch_cv_scale
  2048,                   // fm_cv_offset
  'B',                    // magic_byte
  { },                    // engine_params: see Reset()
};

Storage<0x8020000, 4> storage;

Settings settings;

void Settings::Init() {
  if (!storage.ParsimoniousLoad(&data_, &version_token_)) {
    Reset(false);
  }
  bool settings_within_range = true;
  for (int32_t i = 0; i <= SETTING_LAST_EDITABLE_SETTING; ++i) {
    const Setting setting = static_cast<Setting>(i);
    const SettingMetadata& setting_metadata = metadata(setting);
    uint8_t value = GetValue(setting);
    settings_within_range = settings_within_range && \
        value >= setting_metadata.min_value && \
        value <= setting_metadata.max_value;
  }
  for (int32_t i = 0; i < MACRO_OSC_SHAPE_LAST; ++i) {
    for (int32_t j = 0; j < kNumEngineParams; ++j) {
      settings_within_range = settings_within_range &&
          data_.engine_params[i][j] <= 127;
    }
  }
  settings_within_range = settings_within_range && data_.magic_byte == 'B';
  if (!settings_within_range) {
    Reset(false);
  }
}

void Settings::Reset(bool except_cal_data) {
  int32_t saved_pitch_cv_offset = data_.pitch_cv_offset;
  int32_t saved_pitch_cv_scale = data_.pitch_cv_scale;
  int32_t saved_fm_cv_offset = data_.fm_cv_offset;
  memcpy(&data_, &kInitSettings, sizeof(SettingsData));
  memcpy(data_.engine_params, kEngineParamDefaults,
         sizeof(data_.engine_params));
  if (except_cal_data) {
    data_.pitch_cv_offset = saved_pitch_cv_offset;
    data_.pitch_cv_scale = saved_pitch_cv_scale;
    data_.fm_cv_offset = saved_fm_cv_offset;
  }
  data_.magic_byte = 'B';
}

void Settings::Save() {
  data_.magic_byte = 'B';
  storage.ParsimoniousSave(data_, &version_token_);
}

uint8_t Settings::default_value(Setting setting) const {
  if (setting >= SETTING_ENGINE_PARAM_1 && setting <= SETTING_ENGINE_PARAM_3) {
    return kEngineParamDefaults[data_.shape][setting - SETTING_ENGINE_PARAM_1];
  }
  const uint8_t* defaults = reinterpret_cast<const uint8_t*>(&kInitSettings);
  return defaults[setting];
}

const char* Settings::setting_name(Setting setting) const {
  if (setting >= SETTING_ENGINE_PARAM_1 && setting <= SETTING_ENGINE_PARAM_3) {
    return kEngineParamNames[data_.shape][setting - SETTING_ENGINE_PARAM_1];
  }
  return metadata_[setting].name;
}

const char* const boolean_values[] = { "OFF ", "ON  " };

const char* const algo_values[] = {
    "CSAW",
    "^\x88\x8D_",
    "\x88\x8A\x8C\x8D",
    "SYNC",
    "FOLD",
    "\x8E\x8E\x8E\x8E",
    "\x88\x88x3",
    "\x8C_x3",
    "/\x3",
    "SIx3",
    "\x88\x89\x88\x89",
    "\x88\x88\x8E\x8E",
    "VOSM",
    "VOWL",
    "VFOF",
    "FM  ",
    "FBFM",
    "WTFM",
    "PLUK",
    "BOWD",
    "BLOW",
    "FLUT",
    "BELL",
    "WTBL",
    "WMAP",
    "WLIN",
    "WTx4",
    "CLKN",
    "CLOU",
    "VMRF", // vowel morph + formant shift
    "VOS2", // VOSIM with decaying pulse train
    "DSWM", // drift swarm
    "FDRN", // fold drone
    "SPEC", // spectral (additive) drone
    "TANP", // tanpura
    "ZERO", // RenderSilence
};

const char* const bits_values[] = {
    "2BIT", // 0
    "3BIT", // 1
    "4BIT", // 2
    "6BIT", // 3
    "8BIT", // 4
    "12B", // 5
    "16B", // 6
};

const char* const sample_rate_values[] = {
    "4K", // 0
    "8K", // 1
    "16K",  // 2
    "24K",  // 3
    "32K",  // 4
    "48K",  // 5
    "96K" }; // 6

  
const char* const mod_rate_values[] = {
    "0",
    "1",
    "2",
    "3",
    "4",
    "5",
    "6",
    "7",
    "8",
    "9",
    "10",
    "11",
    "12",
    "13",
    "14",
    "15",
    "16",
    "17",
    "18",
    "19",
    "20",
    "21",
    "22",
    "23",
    "24",
    "25",
    "26",
    "27",
    "28",
    "29",
    "30",
    "31",
    "32",
    "33",
    "34",
    "35",
    "36",
    "37",
    "38",
    "39",
    "40",
    "41",
    "42",
    "43",
    "44",
    "45",
    "46",
    "47",
    "48",
    "49",
    "50",
    "51",
    "52",
    "53",
    "54",
    "55",
    "56",
    "57",
    "58",
    "59",
    "60",
    "61",
    "62",
    "63",
    "64",
    "65",
    "66",
    "67",
    "68",
    "69",
    "70",
    "71",
    "72",
    "73",
    "74",
    "75",
    "76",
    "77",
    "78",
    "79",
    "80",
    "81",
    "82",
    "83",
    "84",
    "85",
    "86",
    "87",
    "88",
    "89",
    "90",
    "91",
    "92",
    "93",
    "94",
    "95",
    "96",
    "97",
    "98",
    "99",
    "100",
    "101",
    "102",
    "103",
    "104",
    "105",
    "106",
    "107",
    "108",
    "109",
    "110",
    "111",
    "112",
    "113",
    "114",
    "115",
    "116",
    "117",
    "118",
    "119",
    "120",
    "121",
    "122",
    "123",
    "124",
    "125",
    "126",
    "127",
};

const char* const bipolar_values[] = {
    "-31", // 0
    "-30",
    "-29",
    "-28",
    "-27",
    "-26",
    "-25",
    "-24",
    "-23",
    "-22",
    "-21",
    "-20",
    "-19",
    "-18",
    "-17",
    "-16",
    "-15",
    "-14",
    "-13",
    "-12",
    "-11",
    "-10",
    "-9",
    "-8",
    "-7",
    "-6",
    "-5",
    "-4",
    "-3",
    "-2",
    "-1",
    "0", // 31
    "1",
    "2",
    "3",
    "4",
    "5",
    "6",
    "7",
    "8",
    "9",
    "10",
    "11",
    "12",
    "13",
    "14",
    "15",
    "16",
    "17",
    "18",
    "19",
    "20",
    "21",
    "22",
    "23",
    "24",
    "25",
    "26",
    "27",
    "28",
    "29",
    "30",
    "31",
};

    
const char* const quantization_values[] = { 
    "OFF",  // 0
    "QTR",  // 1
    "SEMI", // 2
    "ION",  // 3
    "DOR",  
    "PHR",
    "LYD",
    "MIXO",
    "AEOL",
    "LOCR",
    "BLMJ",
    "BLMN",
    "5MAJ",
    "5MIN",
    "BHAI",
    "SHRI",
    "RUPA",
    "TODI",
    "RAGE",
    "KAAF",
    "MEGH",
    "MALK",
    "DEEP",
    "FOLK",
    "JAPN",
    "GAML",
    "WHOL", // 26
};

const char* const trig_source_values[] = { "EXT", "AUTO" };

const char* const pitch_range_values[] = {
    "EXT",
    "FREE",
    "XTND",
    "440",
    "LFO"
};

const char* const octave_values[] = { "-2", "-1", "0", "1", "2" };

const char* const trig_delay_values[] = {
    "NONE",
    "125u",
    "250u",
    "500u",
    "1ms",
    "2ms",
    "4ms"
};

const char* const brightness_values[] = {
    "LOW",
    "MED",
    "HIGH",
};

const char* const filter_mode_values[] = { "OFF", "LPF", "HPF", "BPF" };

const char* const filter_tracking_values[] = { "OFF", "HALF", "FULL" };

const char* const filter_control_values[] = { "MENU", "FM", "TIMB", "COLR" };

const char* const display_view_values[] = { "NAME", "PARM", "SCOP", "DIAL", "TOUR",
                                              "FRAC" };

const char* const reset_type_values[] = {
    "NO", // 0
    "DFLT", // 1
    "NO", // 2
    "FULL", // 3
};

const char* const meta_values[] = {
    "FREQ",  // pitch
    "META",  // engine selection
    "LEVL",  // output level
    "PAR1",  // advanced engine parameters
    "PAR2",
    "PAR3",
    "FOLD",  // output stage
    "DRIV",
    "RING",
    "SUB",
    "JITR",
    "BITS",
    "SRAT",
    "CRSH",  // BITS + SRAT
};

const char* const ring_ratio_values[] = {
    "1/4", "1/2", "3/4", "1", "5/4", "3/2", "PHI", "2",
    "5/2", "E", "3", "PI", "4", "5", "7", "8",
};

/* static */
const SettingMetadata Settings::metadata_[] = {
  { 0, MACRO_OSC_SHAPE_LAST - 1, "WAVE", algo_values },
  { 0, RESOLUTION_LAST - 1, "BITS", bits_values },
  { 0, SAMPLE_RATE_LAST - 1, "SRAT", sample_rate_values },
  { 0, 1, "TSRC", trig_source_values },
  { 0, 6, "TDLY", trig_delay_values },
  { 0, FM_DESTINATION_LAST - 1, "FMCV", meta_values },
  { 0, 4, "RANG", pitch_range_values },
  { 0, 4, "OCTV", octave_values },
  { 0, PITCH_QUANTIZATION_LAST - 1, "QNTZ", quantization_values },
  { 0, 127, "JITR", mod_rate_values },
  { 0, 2, "BRIG", brightness_values },
  { 0, 1, "SYNC", boolean_values },
  { 0, 62, "TUNE", bipolar_values },
  { 0, 127, "LEVL", mod_rate_values },
  { 0, 3, "RST ", reset_type_values },
  { 0, 1, "TS+H", boolean_values },
  { 0, 1, "WRAP", boolean_values },
  { 0, 3, "FMOD", filter_mode_values },
  { 0, 127, "FCUT", mod_rate_values },
  { 0, 127, "FRES", mod_rate_values },
  { 0, 2, "FTRK", filter_tracking_values },
  { 0, FILTER_CONTROL_LAST - 1, "FCTL", filter_control_values },
  { 0, FILTER_CONTROL_LAST - 1, "QCTL", filter_control_values },
  { 0, 1, "PERF", boolean_values },
  { 0, DISPLAY_VIEW_LAST - 1, "VIEW", display_view_values },
  { 0, 127, "FOLD", mod_rate_values },
  { 0, 127, "DRIV", mod_rate_values },
  { 0, 127, "RING", mod_rate_values },
  { 0, kNumRingRatios - 1, "RRAT", ring_ratio_values },
  { 0, 127, "SUB ", mod_rate_values },
  { 0, 127, "PAR1", mod_rate_values },  // named per engine
  { 0, 127, "PAR2", mod_rate_values },
  { 0, 127, "PAR3", mod_rate_values },
  { 0, 0, "CAL.", NULL },
  { 0, 0, "    ", NULL },  // Placeholder for CV tester
  { 0, 0, "LOAD", NULL },  // CPU load meter
{ 0, 0, "h0.4", NULL },  // Placeholder for version string
};

// Menu order: engine, its three advanced parameters, the output stage
// (sound design), the filter, then everything else.
/* static */
const Setting Settings::settings_order_[] = {
  SETTING_OSCILLATOR_SHAPE,
  SETTING_ENGINE_PARAM_1,
  SETTING_ENGINE_PARAM_2,
  SETTING_ENGINE_PARAM_3,
  SETTING_FOLD,
  SETTING_DRIVE,
  SETTING_RING_AMOUNT,
  SETTING_RING_RATIO,
  SETTING_SUB_LEVEL,
  SETTING_FILTER_MODE,
  SETTING_FILTER_CUTOFF,
  SETTING_FILTER_RESONANCE,
  SETTING_FILTER_TRACKING,
  SETTING_PERFORMANCE_MODE,
  SETTING_FILTER_CUTOFF_CONTROL,
  SETTING_FILTER_RESONANCE_CONTROL,
  SETTING_META_MODULATION,
  SETTING_INITIAL_GAIN,
  SETTING_RESOLUTION,
  SETTING_SAMPLE_RATE,
  SETTING_VCO_DRIFT,
  SETTING_PITCH_RANGE,
  SETTING_PITCH_OCTAVE,
  SETTING_PITCH_QUANTIZER,
  SETTING_FINE_TUNE,
  SETTING_TRIG_SOURCE,
  SETTING_TRIG_DELAY,
  SETTING_PITCH_SAMPLE_HOLD,
  SETTING_OSC_SYNC,
  SETTING_DISPLAY_VIEW,
  SETTING_BRIGHTNESS,
  SETTING_EDIT_WRAP,
  SETTING_RESET_TYPE,
  SETTING_CALIBRATION,
  SETTING_CV_TESTER,
  SETTING_CPU_LOAD,
  SETTING_VERSION,
};

// GetValue()/SetValue() index SettingsData by byte offset.
static_assert(offsetof(SettingsData, edit_wrap) == SETTING_EDIT_WRAP,
              "SettingsData and Setting enum out of sync");
static_assert(offsetof(SettingsData, sub_level) == SETTING_SUB_LEVEL,
              "SettingsData and Setting enum out of sync");
static_assert(SETTING_ENGINE_PARAM_1 == SETTING_SUB_LEVEL + 1,
              "Engine parameters must follow the byte-mapped settings");
static_assert(sizeof(algo_values) / sizeof(algo_values[0]) ==
              MACRO_OSC_SHAPE_LAST, "algo_values and shapes out of sync");
static_assert(sizeof(meta_values) / sizeof(meta_values[0]) ==
              FM_DESTINATION_LAST, "meta_values and FMCV out of sync");
static_assert(sizeof(ring_ratio_values) / sizeof(ring_ratio_values[0]) ==
              kNumRingRatios, "ring_ratio_values out of sync");

}  // namespace braids
