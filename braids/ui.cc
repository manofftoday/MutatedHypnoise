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

// Font glyphs (resources/characters.py).
const uint8_t kGlyphNeedle = 0x90;  // 8 needle angles, clockwise from SW
const uint8_t kGlyphMeter = 0x98;   // 8 fill levels, bottom-up

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
  if (!controls_initialized_ || mode_ == MODE_SPLASH) {
    memcpy(control_reference_, cv_, sizeof(control_reference_));
    controls_initialized_ = true;
    return;
  }
  // TIMBRE, COLOR and FM, and the filter source each one may drive.
  static const uint8_t kChannel[] = { 0, 1, 3 };
  static const uint8_t kSource[] = {
    FILTER_CONTROL_TIMBRE, FILTER_CONTROL_COLOR, FILTER_CONTROL_FM
  };
  uint8_t cutoff_control = settings.GetValue(SETTING_FILTER_CUTOFF_CONTROL);
  uint8_t resonance_control = settings.GetValue(
      SETTING_FILTER_RESONANCE_CONTROL);
  for (uint8_t i = 0; i < 3; ++i) {
    uint8_t channel = kChannel[i];
    int16_t delta = cv_[channel] - control_reference_[channel];
    if (delta < kControlMoveThreshold && delta > -kControlMoveThreshold) {
      continue;
    }
    control_reference_[channel] = cv_[channel];
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
      continue;  // FM CV not routed to the filter: no popup.
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
  if (view == DISPLAY_VIEW_PARAM || now - encoder_time_ < kNameHoldTime) {
    return false;
  }
  if (view == DISPLAY_VIEW_SCOPE) {
    memcpy(text, scope_, kDisplayWidth);
  } else {
    text[0] = DialGlyph(feedback_[FEEDBACK_TIMBRE]);
    text[1] = DialGlyph(feedback_[FEEDBACK_COLOR]);
    text[2] = DialGlyph(feedback_[FEEDBACK_MOD1]);
    text[3] = DialGlyph(feedback_[FEEDBACK_MOD2]);
  }
  return true;
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
         }
         display_.Print(text);
       }
       break;
    
    case MODE_EDIT:
      {
        if (setting_ == SETTING_OSCILLATOR_SHAPE) {
          char text[kDisplayWidth + 1];
          text[kDisplayWidth] = '\0';
          if (RenderMainView(text, system_clock.milliseconds())) {
            display_.Print(text);
            break;
          }
        }
        uint8_t value = settings.GetValue(setting_);
        if (setting_ == SETTING_OSCILLATOR_SHAPE &&
            (settings.GetValue(SETTING_META_MODULATION) == 1 ||
             settings.GetValue(SETTING_METASEQ) )) {
          value = meta_shape_;
        }
        display_.Print(settings.metadata(setting_).strings[value]);
      }
      break;
    
    case MODE_MENU:
      {
        if (setting_ == SETTING_CV_TESTER) {
          char text[] = "    ";
          if (!blink_) {
            for (uint8_t i = 0; i < kDisplayWidth; ++i) {
              text[i] = '\x90' + (cv_[i] * 7 >> 12);
            }
          }
          display_.Print(text);
        } else {
          display_.Print(settings.metadata(setting_).name);
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
        if (setting_ == SETTING_OSCILLATOR_SHAPE) {
           settings.Save();
        }   
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
        if (settings.edit_wrap()) {
           value = settings.metadata(setting_).Wrap(value + e.data);
        } else {
           value = settings.metadata(setting_).Clip(value + e.data);
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
      (setting_ == SETTING_CV_TESTER)) {
    refresh_display_ = true;
  }
  if (queue_.idle_time() >= 50 &&
      setting_ == SETTING_OSCILLATOR_SHAPE &&
      mode_ == MODE_EDIT &&
      (settings.GetValue(SETTING_META_MODULATION) == 1 || 
       settings.GetValue(SETTING_METASEQ) )) {
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
