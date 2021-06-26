#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

enum code {
  LightOn  = 0xC08,
  LightOff = 0xC20,
  FanOff   = 0xC10,
  FanHi    = 0xC01,
  FanMed   = 0xC04,
  FanLow   = 0xC43,
};

const uint16_t PIN_BTN_A   =  2; // D4
const uint16_t PIN_BTN_B   = 13; // D7 
const uint16_t PIN_BTN_C   = 16; // D0
const uint16_t PIN_IR_LED  = 12 ;  // ESP8266 GPIO pin to use. 12 (D6).

enum event {
  event_press_a = 1 << 0,
  event_press_b = 1 << 1,
  event_press_c = 1 << 2,
};

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
IRsend irsend(PIN_IR_LED);

void setup(void) {
  pinMode(PIN_BTN_B, INPUT_PULLUP);
  pinMode(PIN_BTN_A, INPUT_PULLUP);
  pinMode(PIN_BTN_C, INPUT_PULLDOWN_16);
  u8g2.begin();
  irsend.begin();
}

bool poll_btn_b(void) {
  return !digitalRead(PIN_BTN_B);
}

bool poll_btn_a(void) {
  return !digitalRead(PIN_BTN_A);
}

bool poll_btn_c(void) {
  return digitalRead(PIN_BTN_C);
}

uint8_t poll_btn_ev(void) {
  static bool btn_b = false;
  static bool btn_a = false;
  static bool btn_c = false;

  uint8_t events = 0;
  if (poll_btn_b()) {
    if (!btn_b) {
      events |= event_press_b;
    }
    btn_b = true;
  } else {
    btn_b = false;
  }
  if (poll_btn_a()) {
    if (!btn_a) {
      events |= event_press_a;
    }
    btn_a = true;
  } else {
    btn_a = false;
  }
  if (poll_btn_c()) {
    if (!btn_c) {
      events |= event_press_c;
    }
    btn_c = true;
  } else {
    btn_c = false;
  }

  return events;
}

enum state {
  state_menu_time,
  state_countdown,
  state_deadline,
  state_sleep,
};

const uint32_t DUR_MAX = 10 * 60;

uint32_t dur_inc(uint32_t dur) {
  if (dur < 10) {
    return dur + 1;
  } else if (dur < 30) {
    return dur + 5;
  } else if (dur < 2 * 60) {
    return dur + 15;
  } else if (dur < 6 * 60) {
    return dur + 30;
  } else if (dur < DUR_MAX) {
    return dur + 60;
  } else {
    return dur;
  }
}

uint32_t dur_dec(uint32_t dur) {
  if (dur > 6 * 60) {
    return dur - 60;
  } else if (dur > 2 * 60) {
    return dur - 30;
  } else if (dur > 30) {
    return dur - 15;
  } else if (dur > 10) {
    return dur - 5;
  } else if (dur > 0) {
    return dur - 1;
  } else {
    return dur;
  }
}

// dur in minutes
void draw_menu_time(uint32_t dur) {
  char line0[32];
  uint32_t minutes = dur % 60;
  uint32_t hours = dur / 60;

  sprintf(line0, "%02dh %02dm", hours, minutes);
  u8g2.setFont(u8g2_font_logisoso24_tf);
  u8g2.drawStr(0, 24, line0);
  
  u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
  u8g2.drawGlyph(128 / 6 * (1 + 0 * 2) - 5, 64, 116);
  u8g2.drawGlyph(128 / 6 * (1 + 1 * 2) - 5, 64, 119);
  u8g2.drawGlyph(128 / 6 * (1 + 2 * 2) - 5, 64, 120);
}

// A -> DEC
// B -> INC
// C -> OK
enum state menu_time(uint32_t *dur);
enum state menu_time(uint32_t *dur) {
  bool ok = false;
  uint8_t events = 0;
  u8g2.setPowerSave(0);
  while (!ok) {
    events = poll_btn_ev();

    if (events & event_press_a) {
      *dur = dur_dec(*dur); 
    } else if (events & event_press_b) {
      *dur = dur_inc(*dur);
    } else if (events & event_press_c) {
      return state_countdown;
    }

    u8g2.clearBuffer();
    draw_menu_time(*dur);
    // draw_countdown(dur * 60 * 1000);
    u8g2.sendBuffer();
    delay(16);
  }
}


// t in milliseconds
void draw_countdown(uint32_t t) {
  char line0[32];
  char line1[32];
  uint32_t milliseconds = t % 1000;
  t /= 1000;
  uint32_t seconds = t % 60;
  t /= 60;
  uint32_t minutes = t % 60;
  uint32_t hours = t / 60;
  
  sprintf(line0, "%02dh %02dm", hours, minutes);
  u8g2.setFont(u8g2_font_logisoso24_tf);
  u8g2.drawStr(0, 24, line0);
  
  sprintf(line1, "%02d.%03ds", seconds, milliseconds);
  u8g2.setFont(u8g2_font_logisoso16_tf);
  u8g2.drawStr(0, 24 + 16 + 2, line1);

  u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
  u8g2.drawGlyph(128 / 6 * (1 + 0 * 2) - 5, 64, 259);
  // u8g2.drawGlyph(128 / 6 * (1 + 1 * 2) - 5, 64, 0);
  u8g2.drawGlyph(128 / 6 * (1 + 2 * 2) - 5, 64, 121);
}

// A -> DISPLAY ON/OFF
// C -> STOP
enum state countdown(uint32_t dur);
enum state countdown(uint32_t dur) {
  uint32_t dur_ms = dur * 60 * 1000;
  uint32_t ts0 = millis();
  uint32_t ts1 = 0;
  uint32_t elapsed = 0;
  uint8_t events = 0;
  bool end = false;
  bool display_on = true;

  // DBG BEGIN
  if (dur_ms == 0) {
    dur_ms = 4 * 1000;
  }
  // DBG END

  while (!end) {
    events = poll_btn_ev();

    if (events & event_press_a) {
      if (display_on) {
        u8g2.setPowerSave(1);
      } else {
        u8g2.setPowerSave(0);
      }
      display_on = !display_on;
    } else if (events & event_press_c) {
      return state_menu_time;
    }

    if (dur_ms == 0) {
      return state_deadline;
    }

    ts1 = millis();
    elapsed = ts1 - ts0;
    if (elapsed > dur_ms) {
      dur_ms = 0;
    } else {
      dur_ms -= elapsed;
    }
    ts0 = ts1;

    u8g2.clearBuffer();
    // draw_menu_time(dur);
    draw_countdown(dur_ms);
    u8g2.sendBuffer();
    delay(16);
  }
}

enum state deadline();
enum state deadline() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_logisoso16_tf);
  u8g2.drawStr(0, 64/2 + 16/2, "Deadline");
  u8g2.sendBuffer();
  delay(16);

  irsend.sendSymphony(LightOff);
  delay(200);
  irsend.sendSymphony(LightOff);
  delay(200);
  irsend.sendSymphony(LightOff);
  delay(200);
  return state_sleep;
}

enum state sleep();
enum state sleep() {
  return state_menu_time;
}

void loop(void) {\
  /*
  // RTC Memory is 512 bytes
  char data[4];
  ESP.rtcUserMemoryRead(0, (uint32_t *) data, sizeof(data));

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_logisoso16_tf);
  char line[32];
  sprintf(line, "%02x %02x %02x %02x", data[0], data[1], data[2], data[3]);
  u8g2.drawStr(0, 24, line);
  u8g2.sendBuffer();
  delay(1000);
  
  data[0] = 0;
  data[1] = 1;
  data[2] = 2;
  data[3] = 3;
  ESP.rtcUserMemoryWrite(0, (uint32_t *) data, sizeof(data));

  ESP.deepSleep(1e6, WAKE_RF_DISABLED);
  */
  int i = 0;
  for (i = 0; i < (64 / 2 + 24 / 2); i += 2) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_logisoso24_tf);
    u8g2.drawStr(0, i, "IR Timer");
    u8g2.sendBuffer();
    delay(16);
  }
  
  delay(500);
  // menu_time();

  
  enum state state = state_menu_time;
  uint32_t dur = 0;
  while (true) {
    switch (state) {
    case state_menu_time:
      state = menu_time(&dur);
      break;
    case state_countdown:
      state = countdown(dur);
      break;
    case state_deadline:
      state = deadline();
      break;
    case state_sleep:
      state = sleep();
      break;
    default:
      while (true) {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_logisoso16_tf);
        u8g2.drawStr(0, 64/2 + 16/2, "Bad state");
        u8g2.sendBuffer();
        delay(16);
      }
      break;
    }
  }
}
