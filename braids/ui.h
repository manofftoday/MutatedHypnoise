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

#ifndef BRAIDS_UI_H_
#define BRAIDS_UI_H_

#include "stmlib/stmlib.h"

#include <cstring>

#include "stmlib/ui/event_queue.h"

#include "braids/drivers/display.h"
#include "braids/drivers/encoder.h"
#include "braids/settings.h"

namespace braids {

enum UiMode {
  MODE_SPLASH,
  MODE_EDIT,
  MODE_MENU,
  MODE_CALIBRATION_STEP_1,
  MODE_CALIBRATION_STEP_2,
};

class Ui {
 public:
  Ui() { }
  ~Ui() { }
  
  void Init();
  void Poll();
  void DoEvents();
  void FlushEvents();
  void Print(const char* text) {
    display_.Print(text);
  }
/*
//   void PrintDebugInteger(uint16_t x) {
//     char buffer[5];
//     buffer[4] = '\0';
//     for (uint16_t i = 0; i < 4; ++i) {
//       buffer[3 - i] = '0' + (x % 10);
//       x /= 10;
//     }
//     Print(buffer);
//   }
*/  
  inline void UpdateCv(
      int16_t cv_param,
      int16_t cv_color,
      int16_t cv_pitch,
      int16_t cv_fm) {
    cv_[0] = cv_param;
    cv_[1] = cv_color;
    cv_[2] = cv_pitch;
    cv_[3] = cv_fm;
  }
  
  // Following method is retained because it is responsible for causing the CV tester
  // to blink on each trigger received. But we don't need to increment marquee_step 
  // it no longer exists.
  inline void StepMarquee() {
    // marquee_step_++;
    blink_ = true;
  }

  enum Feedback {
    FEEDBACK_TIMBRE,
    FEEDBACK_COLOR,
    FEEDBACK_CUTOFF,
    FEEDBACK_RESONANCE,
    FEEDBACK_PARAM_1,
    FEEDBACK_PARAM_2,
    FEEDBACK_PARAM_3,
    FEEDBACK_FOLD,
    FEEDBACK_DRIVE,
    FEEDBACK_RING,
    FEEDBACK_SUB,
    FEEDBACK_LAST
  };

  // Live values for the display views and popups, called once per block.
  //   values: FEEDBACK_LAST live values (incl. CV), 0..127 each.
  //   level: peak absolute output level of the block (0..32767).
  //   fine: FINE knob position, 0..4095 (same scale as the ADC codes).
  inline void UpdateFeedback(const uint8_t* values, uint16_t level,
                             int16_t fine) {
    memcpy(feedback_, values, FEEDBACK_LAST);
    fine_ = fine;
    if (level > level_peak_) {
      level_peak_ = level;
    }
  }

  // Overrides oscillator shape display when in meta mode.
  inline void set_meta_shape(MacroOscillatorShape shape) {
    meta_shape_ = shape;
  }
  
 private:
  void OnIncrement(const stmlib::Event& event);
  void OnClick();
  void OnLongClick();
  void RefreshDisplay();
  void DetectControlMovement(uint32_t now);
  bool RenderMainView(char* text, uint32_t now);
  void RenderTour(char* text, uint32_t elapsed);

  stmlib::EventQueue<16> queue_;
  
  uint32_t encoder_press_time_;
  bool inhibit_further_switch_events_;
  
  int16_t value_;
  uint8_t sub_clock_;
  
  UiMode mode_;
  int16_t setting_index_;
  int16_t last_setting_index_;
  Setting setting_;
  Setting last_setting_;
  
  Display display_;
  Encoder encoder_;
  
  int16_t dac_code_c2_;
  int16_t cv_[4];

  uint8_t splash_frame_;
  bool blink_;
  bool just_reset_;
  
  MacroOscillatorShape meta_shape_;

  uint8_t feedback_[FEEDBACK_LAST];  // 0..127
  uint16_t level_peak_;

  int16_t fine_;                   // FINE knob, 0..4095
  // TIMBRE, COLOR, FM, FINE positions at the last detected move.
  int16_t control_reference_[4];
  // Gesture tracking, to tell a hand on a knob from a CV automation.
  uint32_t gesture_start_[4];
  uint32_t last_move_[4];
  int8_t popup_feedback_;          // Feedback index, -1 = no popup
  char popup_letter_;
  uint32_t popup_time_;
  uint32_t encoder_time_;
  uint32_t animation_time_;
  char scope_[kDisplayWidth];
  bool controls_initialized_;

  DISALLOW_COPY_AND_ASSIGN(Ui);
};

}  // namespace braids

#endif // BRAIDS_UI_H_
