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

// Main screen: engine name; a popup when TIMBRE or COLOR is turned by hand;
// after kOmDelay without touching the encoder, a static Om. Nothing is
// animated, so the display costs (almost) nothing.
const uint32_t kPopupDuration = 1200;    // ms a parameter popup stays up
const uint32_t kOmDelay = 4000;          // ms before the Om appears
const int16_t kControlMoveThreshold = 48;  // ADC codes (of 4096), ~1.2%
const uint8_t kSplashFrames = 16;

// A gesture is a run of control movements less than kGestureGap apart.
// One that lasts longer than kMaxGesture is an automation (CV), not a
// hand: no popups for that control until it rests.
const uint32_t kGestureGap = 400;
const uint32_t kMaxGesture = 2500;

// Font glyphs (resources/characters.py).
const uint8_t kGlyphNeedle = 0x90;  // 8 needle angles, clockwise from SW
const uint8_t kGlyphMeter = 0x98;   // 8 fill levels, bottom-up

// Om drawn on the two middle digits (14-segment masks, see MASKS in
// resources/characters.py): a "3" (a b c d k p), then the tail curling
// from the middle (p k c d) under the crescent (g j, a V).
static const uint16_t kOm[kDisplayWidth] = { 0x0000, 0xF044, 0x32C4, 0x0000 };

// 0..127 -> blank or one of the 8 fill levels.
static inline char MeterGlyph(uint8_t value) {
  uint8_t level = value * 9 >> 7;
  return level ? static_cast<char>(kGlyphMeter + level - 1) : ' ';
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
  popup_control_ = -1;
  controls_initialized_ = false;
}

bool Ui::DetectControlMovement(uint32_t now) {
  // TIMBRE and COLOR (knob + CV).
  if (!controls_initialized_ || mode_ == MODE_SPLASH) {
    control_reference_[0] = cv_[0];
    control_reference_[1] = cv_[1];
    controls_initialized_ = true;
    return false;
  }
  bool popup = false;
  for (uint8_t i = 0; i < 2; ++i) {
    int16_t delta = cv_[i] - control_reference_[i];
    if (delta < kControlMoveThreshold && delta > -kControlMoveThreshold) {
      continue;
    }
    control_reference_[i] = cv_[i];
    if (now - last_move_[i] > kGestureGap) {
      gesture_start_[i] = now;
    }
    last_move_[i] = now;
    if (now - gesture_start_[i] > kMaxGesture) {
      continue;  // moving for too long: automation, no popup
    }
    popup_control_ = i;
    popup_time_ = now;
    popup = true;
  }
  return popup;
}

void Ui::RenderMainScreen(uint32_t now) {
  if (popup_control_ >= 0 && now - popup_time_ < kPopupDuration) {
    // Letter, level glyph, value 00..99 (e.g. T, 5/8 full, 63).
    uint8_t value = cv_[popup_control_] >> 5;
    uint8_t percent = value * 100 >> 7;
    char text[kDisplayWidth + 1];
    text[0] = popup_control_ ? 'C' : 'T';
    text[1] = MeterGlyph(value);
    text[2] = '0' + percent / 10;
    text[3] = '0' + percent % 10;
    display_.Print(text);
    return;
  }
  popup_control_ = -1;
  if (now - encoder_time_ >= kOmDelay) {
    display_.PrintSegments(kOm);
  } else {
    display_.Print(settings.metadata(SETTING_OSCILLATOR_SHAPE).strings[
        settings.GetValue(SETTING_META_MODULATION) == FM_DESTINATION_META
            ? meta_shape_ : settings.GetValue(SETTING_OSCILLATOR_SHAPE)]);
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
          RenderMainScreen(system_clock.milliseconds());
          break;
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
  bool main_screen = mode_ == MODE_EDIT && setting_ == SETTING_OSCILLATOR_SHAPE;
  if (DetectControlMovement(now) && main_screen) {
    refresh_display_ = true;
  }
  if (main_screen && popup_control_ >= 0 && queue_.idle_time() >= 50) {
    refresh_display_ = true;  // live popup value, then back to the name
  }
  if (refresh_display_) {
    queue_.Touch();
    RefreshDisplay();
    blink_ = false;
  }
}

}  // namespace braids
