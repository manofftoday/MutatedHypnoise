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
// User interface.

#include "braids/ui.h"
#include "braids/resources.h"

#include <cstring>

#include "stmlib/system/system_clock.h"

namespace braids {

using namespace stmlib;

const uint32_t kEncoderLongPressTime = 800;

// Display views (SETTING_DISPLAY_VIEW).
const uint32_t kPopupDuration = 1200;    // ms a parameter popup stays up
const uint32_t kNameHoldTime = 1500;     // ms the name stays after an edit
const uint32_t kAnimationPeriod = 50;    // ms between view frames
const int16_t kControlMoveThreshold = 48;  // ADC codes (of 4096), ~1.2%
const uint8_t kSplashFrames = 16;

// A gesture is a run of control movements less than kGestureGap apart.
// One that lasts longer than kMaxGesture is an automation (CV), not a
// hand: no popups for that control until it rests.
const uint32_t kGestureGap = 400;
const uint32_t kMaxGesture = 2500;

// TOUR view (screensaver): after kTourDelay without touching the encoder,
// cycle through pages: name for kTourNameTime, then a live bar.
const uint32_t kTourDelay = 4000;
const uint32_t kTourPageTime = 2600;
const uint32_t kTourNameTime = 800;
const uint8_t kTourPageEngine = 0xfe;
const uint8_t kTourPageScope = 0xfd;

// Font glyphs (resources/characters.py).
const uint8_t kGlyphNeedle = 0x90;  // 8 needle angles, clockwise from SW
const uint8_t kGlyphMeter = 0x98;   // 8 fill levels, bottom-up
const uint8_t kGlyphBar = 0xa0;     // 1..3 columns, left to right

// 0..127 -> 3-character horizontal bar (9 steps) in text[0..2].
static inline void BarGlyphs(uint8_t value, char* text) {
  int8_t steps = value * 10 >> 7;  // 0..9
  for (uint8_t i = 0; i < 3; ++i) {
    int8_t fill = steps - i * 3;
    if (fill > 3) fill = 3;
    text[i] = fill <= 0 ? ' ' : static_cast<char>(kGlyphBar + fill - 1);
  }
}

// 0..127 -> blank or one of the 8 fill levels.
static inline char MeterGlyph(uint8_t value) {
  uint8_t level = value * 9 >> 7;
  return level ? static_cast<char>(kGlyphMeter + level - 1) : ' ';
}

// 0..127 -> needle from lower-left (min) through up to lower-right (max).
static inline char DialGlyph(uint8_t value) {
  return static_cast<char>(kGlyphNeedle + (value * 7 >> 7));
}

void Ui::Init() {
  encoder_.Init();
  display_.Init();
  queue_.Init();
  sub_clock_ = 0;
  value_ = 0;
  setting_ = SETTING_OSCILLATOR_SHAPE;
  last_setting_ = setting_;
  setting_index_ = 0;
  last_setting_index_ = setting_index_;
  mode_ = MODE_SPLASH;
  just_reset_ = false;
  popup_feedback_ = -1;
  controls_initialized_ = false;
  memset(scope_, ' ', kDisplayWidth);
}

void Ui::DetectControlMovement(uint32_t now) {
  // TIMBRE, COLOR, FM and FINE, and the filter source each one may drive.
  int16_t position[4] = { cv_[0], cv_[1], cv_[3], fine_ };
  if (!controls_initialized_ || mode_ == MODE_SPLASH) {
    memcpy(control_reference_, position, sizeof(control_reference_));
    controls_initialized_ = true;
    return;
  }
  static const uint8_t kSource[] = {
    FILTER_CONTROL_TIMBRE, FILTER_CONTROL_COLOR, FILTER_CONTROL_FM, 0xff
  };
  // Performance mode: COLOR = cutoff, FM = resonance.
  bool performance = settings.GetValue(SETTING_PERFORMANCE_MODE);
  uint8_t cutoff_control = performance ? FILTER_CONTROL_COLOR :
      settings.GetValue(SETTING_FILTER_CUTOFF_CONTROL);
  uint8_t resonance_control = performance ? FILTER_CONTROL_FM :
      settings.GetValue(SETTING_FILTER_RESONANCE_CONTROL);
  for (uint8_t i = 0; i < 4; ++i) {
    int16_t delta = position[i] - control_reference_[i];
    if (delta < kControlMoveThreshold && delta > -kControlMoveThreshold) {
      continue;
    }
    control_reference_[i] = position[i];
    if (now - last_move_[i] > kGestureGap) {
      gesture_start_[i] = now;
    }
    last_move_[i] = now;
    if (now - gesture_start_[i] > kMaxGesture) {
      continue;  // moving for too long: automation, no popup
    }
    if (cutoff_control == kSource[i]) {
      popup_feedback_ = FEEDBACK_CUTOFF;
      popup_letter_ = 'F';
    } else if (resonance_control == kSource[i]) {
      popup_feedback_ = FEEDBACK_RESONANCE;
      popup_letter_ = 'Q';
    } else if (i == 0) {
      popup_feedback_ = FEEDBACK_TIMBRE;
      popup_letter_ = 'T';
    } else if (i == 1) {
      popup_feedback_ = FEEDBACK_COLOR;
      popup_letter_ = 'C';
    } else {
      continue;  // FM / FINE not routed to the filter: no popup.
    }
    popup_time_ = now;
  }
}

// Main screen for the PARM/SCOP/DIAL views. Returns false when the engine
// name should be shown instead.
bool Ui::RenderMainView(char* text, uint32_t now) {
  uint8_t view = settings.GetValue(SETTING_DISPLAY_VIEW);
  if (view == DISPLAY_VIEW_NAME) {
    return false;
  }
  if (popup_feedback_ >= 0) {
    if (now - popup_time_ < kPopupDuration) {
      // Letter, level glyph, value 00..99 (e.g. F, 5/8 full, 63).
      uint8_t value = feedback_[popup_feedback_];
      uint8_t percent = value * 100 >> 7;
      text[0] = popup_letter_;
      text[1] = MeterGlyph(value);
      text[2] = '0' + percent / 10;
      text[3] = '0' + percent % 10;
      return true;
    }
    popup_feedback_ = -1;
  }
  if (view == DISPLAY_VIEW_FRACTAL) {
    if (now - encoder_time_ < kTourDelay) {
      return false;
    }
    fractal_frame_ = true;
    return true;
  }
  if (view == DISPLAY_VIEW_TOUR) {
    uint32_t idle = now - encoder_time_;
    if (idle < kTourDelay) {
      return false;
    }
    RenderTour(text, idle - kTourDelay);
    return true;
  }
  if (view == DISPLAY_VIEW_PARAM || now - encoder_time_ < kNameHoldTime) {
    return false;
  }
  if (view == DISPLAY_VIEW_SCOPE) {
    memcpy(text, scope_, kDisplayWidth);
  } else {
    text[0] = DialGlyph(feedback_[FEEDBACK_TIMBRE]);
    text[1] = DialGlyph(feedback_[FEEDBACK_COLOR]);
    text[2] = DialGlyph(feedback_[FEEDBACK_CUTOFF]);
    text[3] = DialGlyph(feedback_[FEEDBACK_RESONANCE]);
  }
  return true;
}

// FRAC screensaver: a fractal plasma drawn on the 56 segments themselves.
// Each segment has a position (half units: x 0..4 per digit, digits 6
// apart, y 0..8). Its value is a sum of travelling sine waves at three
// octaves (1, 1/2, 1/4 amplitude: fBm-like, self-similar) plus rings
// spreading from a wandering centre; the right half mirrors the left
// (kaleidoscope). A segment is lit when the value crosses a threshold
// that falls as the output gets louder. TIMBRE sets the speed, COLOR the
// zoom.
static const uint16_t kSegmentBit[14] = {
  0x8000, 0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200,  // a b c d e f g
  0x0100, 0x0080, 0x0040, 0x0020, 0x0010, 0x0008, 0x0004   // h j k l m n p
};
static const uint8_t kSegmentX[14] = { 2, 4, 4, 2, 0, 0, 1, 2, 3, 3, 3, 2, 1, 1 };
static const uint8_t kSegmentY[14] = { 0, 2, 6, 8, 6, 2, 2, 2, 2, 4, 6, 6, 6, 4 };

void Ui::RenderFractal(uint16_t* segments) {
  uint8_t t = fractal_time_ >> 2;
  uint8_t zoom = 6 + (feedback_[FEEDBACK_COLOR] >> 3);  // 6..21 per unit
  // Wandering centre of the rings (Lissajous, two incommensurate rates).
  int32_t cx = 11 + (wav_sine[static_cast<uint8_t>(fractal_time_ >> 3)] >> 12);
  int32_t cy = 4 + (wav_sine[static_cast<uint8_t>(
      (fractal_time_ * 3) >> 4)] >> 13);
  // Threshold: 8 levels of output loudness open more segments.
  int32_t threshold = 24000 - (fractal_level_ * 3000 >> 8);
  for (uint8_t digit = 0; digit < kDisplayWidth; ++digit) {
    uint16_t word = 0;
    for (uint8_t s = 0; s < 14; ++s) {
      int32_t x = digit * 6 + kSegmentX[s];  // 0..22
      int32_t y = kSegmentY[s];
      if (x > 11) x = 22 - x;  // kaleidoscope: mirror around the centre
      int32_t value = 0;
      for (uint8_t o = 0; o < 3; ++o) {
        uint8_t scale = zoom << o;
        uint8_t along = x * scale + t * (o + 1);
        uint8_t across = y * scale + (x * scale >> 1) - t * (3 - o);
        value += (wav_sine[along] + wav_sine[across]) >> (o + 1);
      }
      int32_t dx = x - cx;
      int32_t dy = y - cy;
      uint8_t ring = (dx * dx + dy * dy) * 3 - (t << 1);
      value += wav_sine[ring] >> 1;
      if (value > threshold) {
        word |= kSegmentBit[s];
      }
    }
    segments[digit] = word;
  }
}

// Screensaver: engine, TIMBRE, COLOR, the engine's advanced parameters,
// the filter (when on), the output stage (what is in use) and the scope.
void Ui::RenderTour(char* text, uint32_t elapsed) {
  uint8_t pages[16];
  uint8_t num_pages = 0;
  pages[num_pages++] = kTourPageEngine;
  pages[num_pages++] = FEEDBACK_TIMBRE;
  pages[num_pages++] = FEEDBACK_COLOR;
  for (uint8_t i = 0; i < 3; ++i) {
    if (settings.setting_name(
            static_cast<Setting>(SETTING_ENGINE_PARAM_1 + i))[0] != '-') {
      pages[num_pages++] = FEEDBACK_PARAM_1 + i;
    }
  }
  if (settings.GetValue(SETTING_FILTER_MODE)) {
    pages[num_pages++] = FEEDBACK_CUTOFF;
    pages[num_pages++] = FEEDBACK_RESONANCE;
  }
  for (uint8_t i = FEEDBACK_FOLD; i <= FEEDBACK_SUB; ++i) {
    if (feedback_[i]) {
      pages[num_pages++] = i;
    }
  }
  pages[num_pages++] = kTourPageScope;

  uint8_t page = pages[(elapsed / kTourPageTime) % num_pages];
  bool show_name = (elapsed % kTourPageTime) < kTourNameTime;
  if (page == kTourPageEngine) {
    memcpy(text, settings.metadata(SETTING_OSCILLATOR_SHAPE).strings[
        meta_shape_], kDisplayWidth);
    return;
  } else if (page == kTourPageScope) {
    memcpy(text, scope_, kDisplayWidth);
    return;
  }
  static const char* const kNames[] = {
    "TIMB", "COLR", "FCUT", "FRES", NULL, NULL, NULL,
    "FOLD", "DRIV", "RING", "SUB "
  };
  const char* name = kNames[page];
  if (!name) {
    name = settings.setting_name(static_cast<Setting>(
        SETTING_ENGINE_PARAM_1 + page - FEEDBACK_PARAM_1));
  }
  if (show_name) {
    memcpy(text, name, kDisplayWidth);
  } else {
    // First letter + live bar, e.g. "F" followed by 2 2/3 full blocks.
    text[0] = page == FEEDBACK_RESONANCE ? 'Q' : name[0];
    BarGlyphs(feedback_[page], text + 1);
  }
}

void Ui::Poll() {
  system_clock.Tick();  // Tick global ms counter.
  ++sub_clock_;
  encoder_.Debounce();
  if (encoder_.just_pressed()) {
    encoder_press_time_ = system_clock.milliseconds();
    inhibit_further_switch_events_ = false;
  } 
  if (!inhibit_further_switch_events_) {
    if (encoder_.pressed()) {
      uint32_t duration = system_clock.milliseconds() - encoder_press_time_;
      if (duration >= kEncoderLongPressTime) {
        queue_.AddEvent(CONTROL_ENCODER_LONG_CLICK, 0, 0);
        inhibit_further_switch_events_ = true;
      }
    } else if (encoder_.released()) {
      queue_.AddEvent(CONTROL_ENCODER_CLICK, 0, 0);
    }
  }
  int32_t increment = encoder_.increment();
  if (increment != 0) {
    queue_.AddEvent(CONTROL_ENCODER, 0, increment);
  }
  
  if ((sub_clock_ & 1) == 0) {
    display_.Refresh();
  }
}

void Ui::FlushEvents() {
  queue_.Flush();
}

void Ui::RefreshDisplay() {
  switch (mode_) {
     case MODE_SPLASH:
       {
         // Rotating needles with a phase offset per digit, then the name.
         char text[] = "HYPN";
         if (splash_frame_ < kSplashFrames - 6) {
           for (uint8_t i = 0; i < kDisplayWidth; ++i) {
             text[i] = static_cast<char>(
                 kGlyphNeedle + ((splash_frame_ + i * 2) & 7));
           }
         } else if (splash_frame_ >= kSplashFrames - 3) {
           memcpy(text, settings.metadata(SETTING_VERSION).name,
                  kDisplayWidth);
         }
         display_.Print(text);
       }
       break;
    
    case MODE_EDIT:
      {
        if (setting_ == SETTING_OSCILLATOR_SHAPE) {
          char text[kDisplayWidth + 1];
          text[kDisplayWidth] = '\0';
          fractal_frame_ = false;
          if (RenderMainView(text, system_clock.milliseconds())) {
            if (fractal_frame_) {
              uint16_t segments[kDisplayWidth];
              RenderFractal(segments);
              display_.PrintSegments(segments);
            } else {
              display_.Print(text);
            }
            break;
          }
        }
        uint8_t value = settings.GetValue(setting_);
        if (setting_ == SETTING_OSCILLATOR_SHAPE &&
            settings.GetValue(SETTING_META_MODULATION) ==
                FM_DESTINATION_META) {
          value = meta_shape_;
        }
        const SettingMetadata& metadata = settings.metadata(setting_);
        if (metadata.min_value == 0 && metadata.max_value == 127) {
          // 0..127 values: level glyph + number, e.g. [glyph] 64.
          char text[kDisplayWidth + 1] = "    ";
          text[0] = MeterGlyph(value);
          text[1] = value >= 100 ? '1' : ' ';
          text[2] = value >= 10 ? '0' + (value / 10) % 10 : ' ';
          text[3] = '0' + value % 10;
          display_.Print(text);
        } else {
          display_.Print(metadata.strings[value]);
        }
      }
      break;
    
    case MODE_MENU:
      {
        if (setting_ == SETTING_CPU_LOAD) {
          // Cycles every 1.5 s: A = average, P = peak, O = engine peak,
          // in % of the real-time budget, e.g. "A" + glyph + "52".
          // "OV" = over 99 % (the block took longer than real time).
          static const char kLetters[] = { 'A', 'P', 'O' };
          uint8_t page = (system_clock.milliseconds() / 1500) % 3;
          uint8_t load = cpu_display_[page];
          char text[kDisplayWidth + 1] = "    ";
          text[0] = kLetters[page];
          text[1] = MeterGlyph(load > 99 ? 127 : load * 127 / 99);
          if (load > 99) {
            text[2] = 'O';
            text[3] = 'V';
          } else {
            text[2] = '0' + load / 10;
            text[3] = '0' + load % 10;
          }
          display_.Print(text);
        } else if (setting_ == SETTING_CV_TESTER) {
          char text[] = "    ";
          if (!blink_) {
            for (uint8_t i = 0; i < kDisplayWidth; ++i) {
              text[i] = '\x90' + (cv_[i] * 7 >> 12);
            }
          }
          display_.Print(text);
        } else {
          display_.Print(settings.setting_name(setting_));
        }
      }
      break;
      
    case MODE_CALIBRATION_STEP_1:
      display_.Print(">C2 ");
      break;

    case MODE_CALIBRATION_STEP_2:
      display_.Print(">C4 ");
      break;
      
    default:
      break;
  }
}

void Ui::OnLongClick() {
  switch (mode_) {

    case MODE_MENU:
      if (setting_ == SETTING_CALIBRATION) {
        mode_ = MODE_CALIBRATION_STEP_1;
      } else if (setting_ == SETTING_RESET_TYPE) {
        if (settings.GetValue(SETTING_RESET_TYPE) == 1) {
           settings.Reset(true);
           just_reset_ = true;
        } else if (settings.GetValue(SETTING_RESET_TYPE) == 3) {
           settings.Reset(false);
           just_reset_ = true;
        }
        if (just_reset_) {
           settings.Save();
           just_reset_ = false;
           last_setting_ = SETTING_OSCILLATOR_SHAPE;
           last_setting_index_ = 0;
           mode_ = MODE_SPLASH;
        }
      } else {
        // Back to the main screen: keep every edit.
        settings.Save();
        // short-cut   
        last_setting_ = setting_;
        last_setting_index_ = setting_index_;
        setting_ = SETTING_OSCILLATOR_SHAPE;
        mode_ = MODE_EDIT;
        setting_index_ = 0;
      }
      break;

    case MODE_EDIT:
      if (setting_ == SETTING_OSCILLATOR_SHAPE) {
         setting_ = last_setting_;
         setting_index_ = last_setting_index_;
         mode_ = MODE_MENU;
      } else {
         // Long press while editing a value: back to its default.
         settings.ResetValue(setting_);
      }
      break;
    
    default:
      break;
  }
}

void Ui::OnClick() {
  switch (mode_) {
    case MODE_EDIT:
      mode_ = MODE_MENU;
      if (setting_ == SETTING_OSCILLATOR_SHAPE) {
        // From the main screen, go straight to the engine's first
        // advanced parameter.
        setting_index_ = 1;
        setting_ = settings.setting_at_index(setting_index_);
      }
      break;

    case MODE_MENU:
      if (setting_ <= SETTING_LAST_EDITABLE_SETTING) {
        mode_ = MODE_EDIT;
        if (setting_ == SETTING_OSCILLATOR_SHAPE) {
          settings.Save();
        }
      } 
      else if (setting_ == SETTING_VERSION) {
        mode_ = MODE_SPLASH;
      }
      break;
      
    case MODE_CALIBRATION_STEP_1:
      dac_code_c2_ = cv_[2];
      mode_ = MODE_CALIBRATION_STEP_2;
      break;
      
    case MODE_CALIBRATION_STEP_2:
      settings.Calibrate(dac_code_c2_, cv_[2], cv_[3]);
      mode_ = MODE_MENU;
      break;
      
    default:
      break;
  }
}

void Ui::OnIncrement(const Event& e) {
  switch (mode_) {

    case MODE_EDIT:
      {
        int16_t value = settings.GetValue(setting_);
        int32_t step = e.data;
        uint32_t now = system_clock.milliseconds();
        const SettingMetadata& metadata = settings.metadata(setting_);
        if (metadata.max_value - metadata.min_value >= 100 &&
            now - increment_time_ < 40) {
          step *= 4;  // fast turn: bigger steps
        }
        increment_time_ = now;
        if (settings.edit_wrap()) {
           value = settings.metadata(setting_).Wrap(value + step);
        } else {
           value = settings.metadata(setting_).Clip(value + step);
        }
        settings.SetValue(setting_, value);
        display_.set_brightness(settings.GetValue(SETTING_BRIGHTNESS) + 1);
      }
      break;
      
    case MODE_MENU:
      {
        setting_index_ += e.data;
        // allow menu to be an Ouroborus
        if (setting_index_ < 0) {
          // setting_index_ = 0; // pre-Ouroboros
          setting_index_ = SETTING_LAST - 1;
        } else if (setting_index_ >= SETTING_LAST) {
          // setting_index_ = SETTING_LAST - 1; // pre-Ouroboros
          setting_index_ = 0;
        }
        setting_ = settings.setting_at_index(setting_index_);
      }
      break;
      
    default:
      break;
  }
}

void Ui::DoEvents() {
  bool refresh_display_ = false;
  uint32_t now = system_clock.milliseconds();
  while (queue_.available()) {
    Event e = queue_.PullEvent();
    encoder_time_ = now;
    if (e.control_type == CONTROL_ENCODER_CLICK) {
      OnClick();
    } else if (e.control_type == CONTROL_ENCODER_LONG_CLICK) {
      OnLongClick();
    } else if (e.control_type == CONTROL_ENCODER) {
      OnIncrement(e);
    }
    refresh_display_ = true;
  }
  if (queue_.idle_time() > 1000) {
    refresh_display_ = true;
  }
  if (queue_.idle_time() >= 50 && mode_ == MODE_SPLASH) {
     ++splash_frame_;
     if (splash_frame_ == kSplashFrames) {
       splash_frame_ = 0;
       mode_ = MODE_EDIT;
       setting_ = SETTING_OSCILLATOR_SHAPE;
       setting_index_ = 0;
     }
     refresh_display_ = true;
  }
  if (queue_.idle_time() >= 50 &&
      (setting_ == SETTING_CV_TESTER || setting_ == SETTING_CPU_LOAD)) {
    refresh_display_ = true;
  }
  if (now - cpu_time_ >= 500) {
    cpu_time_ = now;
    cpu_display_[0] = cpu_count_ ? cpu_sum_ / cpu_count_ : 0;
    cpu_display_[1] = cpu_peak_;
    cpu_display_[2] = cpu_oscillator_peak_;
    cpu_peak_ = 0;
    cpu_oscillator_peak_ = 0;
    cpu_sum_ = 0;
    cpu_count_ = 0;
  }
  if (queue_.idle_time() >= 50 &&
      setting_ == SETTING_OSCILLATOR_SHAPE &&
      mode_ == MODE_EDIT &&
      settings.GetValue(SETTING_META_MODULATION) == FM_DESTINATION_META) {
    refresh_display_ = true;
  }
  DetectControlMovement(now);
  if (mode_ == MODE_EDIT && setting_ == SETTING_OSCILLATOR_SHAPE &&
      settings.GetValue(SETTING_DISPLAY_VIEW) != DISPLAY_VIEW_NAME &&
      now - animation_time_ >= kAnimationPeriod) {
    animation_time_ = now;
    // Scope: scroll left, push the peak level since the last frame,
    // 6 dB per step (blank below -48 dBFS).
    uint8_t level = 0;
    for (uint16_t peak = level_peak_ >> 7; peak; peak >>= 1) {
      ++level;
    }
    memmove(scope_, scope_ + 1, kDisplayWidth - 1);
    scope_[kDisplayWidth - 1] = level
        ? static_cast<char>(kGlyphMeter + level - 1) : ' ';
    // FRAC: fast attack, slow release on the level; speed from TIMBRE.
    int32_t target = level << 8;
    if (target > fractal_level_) {
      fractal_level_ += (target - fractal_level_) >> 1;
    } else {
      fractal_level_ -= (fractal_level_ - target) >> 4;
    }
    fractal_time_ += 1 + (feedback_[FEEDBACK_TIMBRE] >> 4);
    level_peak_ = 0;
    refresh_display_ = true;
  }
  if (refresh_display_) {
    queue_.Touch();
    RefreshDisplay();
    blink_ = false;
  }
}

}  // namespace braids
