#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <Esp.h>

extern "C" {
  #include <user_interface.h> // https://github.com/esp8266/Arduino actually tools/sdk/include
}

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
const uint16_t PIN_BTN_C   =  0; // D3 // 16; // D0
const uint16_t PIN_IR_LED  = 12;  // ESP8266 GPIO pin to use. 12 (D6).
const uint16_t PIN_RST_ENABLE = 3; // RXD0
const uint16_t PIN_WAKEUP_USER = 14; // TXD0

enum event {
  event_press_a = 1 << 0,
  event_press_b = 1 << 1,
  event_press_c = 1 << 2,
};

uint32_t rtcsv_wakeup = RTCSV;
uint32_t rtccv_wakeup = RTCCV;
bool user_reset;
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
IRsend irsend(PIN_IR_LED);

bool u8g2_begin(void) {
  if (user_reset) {
    return u8g2.begin();
  } else {
    return true;
  }
}

void u8g2_setFont(const uint8_t *font) {
  if (user_reset) {
    u8g2.setFont(font);
  }
}

unsigned int u8g2_drawStr(u8g2_uint_t x, u8g2_uint_t y, const char *s) {
  if (user_reset) {
    return u8g2.drawStr(x, y, s);
  } else {
    return 0;
  }
}

void u8g2_drawGlyph(u8g2_uint_t x, u8g2_uint_t y, uint16_t encoding) {
  if (user_reset) {
    u8g2.drawGlyph(x, y, encoding);
  }
}

void u8g2_setPowerSave(uint8_t is_enable) {
  if (user_reset) {
    u8g2.setPowerSave(is_enable);
  }
}

void u8g2_clearBuffer() {
  if (user_reset) {
    u8g2.clearBuffer();
  }
}

void u8g2_sendBuffer() {
  if (user_reset) {
    u8g2.sendBuffer();
  }
}

bool wakeup_user;

void setup(void) {
  pinMode(PIN_WAKEUP_USER, INPUT);
  if (digitalRead(PIN_WAKEUP_USER) == HIGH) {
    wakeup_user = false;
  } else {
    wakeup_user = true;
  }
  pinMode(PIN_BTN_A, INPUT_PULLUP);
  pinMode(PIN_BTN_B, INPUT_PULLUP);
  // pinMode(PIN_BTN_C, INPUT_PULLDOWN_16);
  pinMode(PIN_BTN_C, INPUT_PULLUP);
  pinMode(PIN_RST_ENABLE, OUTPUT);
  digitalWrite(PIN_RST_ENABLE, LOW);
}

bool poll_btn_b(void) {
  return !digitalRead(PIN_BTN_B);
}

bool poll_btn_a(void) {
  return !digitalRead(PIN_BTN_A);
}

bool poll_btn_c(void) {
  return !digitalRead(PIN_BTN_C);
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

enum screen {
  screen_menu_time,
  screen_countdown,
  screen_deadline,
  screen_sleep,
};

const char COOKIE[5] = "IrTi";
struct state {
  char cookie[4];
  uint32_t rtcsv_last;
  enum screen screen;
  uint32_t timer_dur; // configured timer duration in minutes
  int32_t timer_dur_ms; // remaining timer duration in milliseconds
};

bool cookie_check(char cookie[4]) {
  return (cookie[0] == COOKIE[0]) &&
         (cookie[1] == COOKIE[1]) &&
         (cookie[2] == COOKIE[2]) &&
         (cookie[3] == COOKIE[3]);
}

void cookie_set(char cookie[4]) {
  cookie[0] = COOKIE[0];
  cookie[1] = COOKIE[1];
  cookie[2] = COOKIE[2];
  cookie[3] = COOKIE[3];
}

void state_init(struct state *s) {
  cookie_set(s->cookie);
  s->screen = screen_menu_time;
  s->rtcsv_last = 0;
  s->timer_dur = 0;
  s->timer_dur_ms = 0;
}

void state_read(struct state *s) {
  ESP.rtcUserMemoryRead(0, (uint32_t *) s, sizeof(state));
  if (!cookie_check(s->cookie)) {
    state_init(s);
  }
}

void state_write(struct state *s) {
  ESP.rtcUserMemoryWrite(0, (uint32_t *) s, sizeof(state));
}

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
  u8g2_setFont(u8g2_font_logisoso24_tf);
  u8g2_drawStr(0, 24, line0);
  
  u8g2_setFont(u8g2_font_open_iconic_all_2x_t);
  u8g2_drawGlyph(128 / 6 * (1 + 0 * 2) - 5, 64, 116);
  u8g2_drawGlyph(128 / 6 * (1 + 1 * 2) - 5, 64, 119);
  u8g2_drawGlyph(128 / 6 * (1 + 2 * 2) - 5, 64, 120);
}

// A -> DEC
// B -> INC
// C -> OK
void menu_time(struct state *state, uint8_t key_events) {
  if (key_events & event_press_a) {
    state->timer_dur = dur_dec(state->timer_dur);
  } else if (key_events & event_press_b) {
    state->timer_dur = dur_inc(state->timer_dur);
  } else if (key_events & event_press_c) {
    state->timer_dur_ms = state->timer_dur * 60 * 1000;
    // DBG BEGIN
    if (state->timer_dur_ms == 0) {
      state->timer_dur_ms = 4 * 1000;
    }
    // DBG END
    state->screen = screen_countdown;
  }

  draw_menu_time(state->timer_dur);
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
  u8g2_setFont(u8g2_font_logisoso24_tf);
  u8g2_drawStr(0, 24, line0);
  
  sprintf(line1, "%02d.%03ds", seconds, milliseconds);
  u8g2_setFont(u8g2_font_logisoso16_tf);
  u8g2_drawStr(0, 24 + 16 + 2, line1);

  u8g2_setFont(u8g2_font_open_iconic_all_2x_t);
  u8g2_drawGlyph(128 / 6 * (1 + 0 * 2) - 5, 64, 259);
  // u8g2_drawGlyph(128 / 6 * (1 + 1 * 2) - 5, 64, 0);
  u8g2_drawGlyph(128 / 6 * (1 + 2 * 2) - 5, 64, 121);
}

// A -> DISPLAY ON/OFF
// C -> STOP
void countdown(struct state *state, uint8_t key_events) {
  static uint32_t ts0 = millis();
  uint32_t ts1;
  uint32_t elapsed;
  bool display_on = true;

  if (key_events & event_press_a) {
    display_on = !display_on;
  } else if (key_events & event_press_c) {
    state->screen = screen_menu_time;
  }

  if (state->timer_dur_ms <= 0) {
    state->screen = screen_deadline;
  }

  ts1 = millis();
  elapsed = ts1 - ts0;
  state->timer_dur_ms -= elapsed;
  ts0 = ts1;

  draw_countdown(state->timer_dur_ms);
}

void deadline(struct state *state, uint8_t key_events) {
  irsend.begin();
  u8g2_clearBuffer();
  u8g2_setFont(u8g2_font_logisoso16_tf);
  u8g2_drawStr(0, 64/2 + 16/2, "Deadline");
  u8g2_sendBuffer();
  delay(16);

  int i;
  for (i = 0; i < 3; i++) {
    irsend.sendSymphony(FanOff);
    delay(200);
  }
  state->screen = screen_sleep;
}

void sleep(struct state *state, uint8_t key_events) {
  state->screen = screen_menu_time;
}

void show_logo(void) {
  int i = 0;
  for (i = 0; i < (64 / 2 + 24 / 2); i += 2) {
    u8g2_clearBuffer();
    u8g2_setFont(u8g2_font_logisoso24_tf);
    u8g2_drawStr(0, i, "IR Timer");
    u8g2_sendBuffer();
    delay(16);
  }
  
  delay(500);
}

const uint32_t RTCSV_THRESHOLD = 7000;

void deep_sleep(struct state *state, uint32_t seconds) {
  state_write(state);
  u8g2.setPowerSave(1);
  ESP.deepSleep(seconds * 1e6, WAKE_RF_DISABLED);
}

void loop(void) {
  // WiFi.setSleepMode(WIFI_MODEM_SLEEP);
  
  // RTC Memory is 512 bytes
  // char data[4];
  // ESP.rtcUserMemoryRead(0, (uint32_t *) data, sizeof(data));

  // u8g2.clearBuffer();
  // u8g2.setFont(u8g2_font_logisoso16_tf);
  // char line[32];
  // sprintf(line, "%02x %02x %02x %02x", data[0], data[1], data[2], data[3]);
  // u8g2.drawStr(0, 24, line);
  // u8g2.sendBuffer();
  // delay(5000);
  // 
  // data[0] = 0;
  // data[1] = 1;
  // data[2] = 2;
  // data[3] = 3;
  // ESP.rtcUserMemoryWrite(0, (uint32_t *) data, sizeof(data));
  // u8g2.setPowerSave(1);

  struct state state;
  state_read(&state);
  if (state.rtcsv_last == 0) {
    user_reset = true;
    state.rtcsv_last = rtcsv_wakeup;
    // state_write(&state);
    // ESP.deepSleep(1e3, WAKE_RF_DISABLED);
  } else {
    user_reset = rtcsv_wakeup < (state.rtcsv_last - 10) ? true : false;
  }

  // DBG BEGIN
  // u8g2.clearBuffer();
  // u8g2.setFont(u8g2_font_logisoso16_tf);
  // char line[32];
  // sprintf(line, "%c %c %c %c", state.cookie[0], state.cookie[1], state.cookie[2], state.cookie[3]);
  // u8g2.drawStr(0, 16, line);
  // rst_info* rinfo = ESP.getResetInfoPtr();
  // sprintf(line, "%x %d %d", millis(), rinfo->reason, rtcsv_wakeup);
  // u8g2.drawStr(0, 16 + 16 + 2, line);
  // sprintf(line, "%d %d", rtccv_wakeup, user_reset);
  // u8g2.drawStr(0, 16 + 16 + 2 + 16 + 2, line);
  // u8g2.sendBuffer();
  // delay(5000);

  if (!user_reset) {
    state.rtcsv_last = rtcsv_wakeup;
  }
  // state_write(&state);
  // u8g2.setPowerSave(1);
  // ESP.deepSleep(600e6, WAKE_RF_DISABLED);
  // DBG END

  u8g2_begin();

  const uint32_t SLEEP_TIME = 60; // in seconds

  if (state.screen == screen_countdown) {
    if (user_reset) {
      state.timer_dur_ms -= (SLEEP_TIME/2) * 1000;
    } else {
      state.timer_dur_ms -= SLEEP_TIME * 1000;
    }
  }

  const uint32_t IDLE_SLEEP = 150; // 10 / 0.066
  const int32_t FRAME_TIME_MICROS = 66666; // ~15 fps

  uint8_t key_events;
  uint32_t idle = 0;
  uint32_t ts0 = micros();
  uint32_t ts1;
  int32_t delay_micros;
  screen screen_prev;
  while (true) {
    u8g2_clearBuffer();
    key_events = poll_btn_ev();

    screen_prev = state.screen;
    switch(state.screen) {
    case screen_menu_time:
      menu_time(&state, key_events);
      break;
    case screen_countdown:
      countdown(&state, key_events);
      break;
    case screen_deadline:
      deadline(&state, key_events);
      break;
    case screen_sleep:
      sleep(&state, key_events);
      break;
    default:
      u8g2_begin();
      u8g2_setFont(u8g2_font_logisoso16_tf);
      u8g2_drawStr(0, 64/2 + 16/2, "Bad state");
      break;
    }

    if (!user_reset) {
      if (screen_prev != state.screen) {
        continue;
      }
      deep_sleep(&state, SLEEP_TIME);
    }

    u8g2_sendBuffer();

    ts1 = micros();
    delay_micros = FRAME_TIME_MICROS - (ts1 - ts0);
    if (delay_micros < 0) {
      delay_micros = 0;
    }
    delay(delay_micros / 1000);
    ts0 = ts1;

    if (key_events == 0) {
      idle++;
      if (idle >= IDLE_SLEEP) {
        deep_sleep(&state, SLEEP_TIME);
      }
    } else {
      idle = 0;
    }
  }


  // ESP.deepSleep(5e6, WAKE_RF_DISABLED);
  
  show_logo();
  // menu_time();

  
  // enum screen screen = screen_menu_time;
  // uint32_t dur = 0;
  // while (true) {
  //   switch (screen) {
  //   case screen_menu_time:
  //     screen = menu_time(&dur);
  //     break;
  //   case screen_countdown:
  //     screen = countdown(dur);
  //     break;
  //   case screen_deadline:
  //     screen = deadline();
  //     break;
  //   case screen_sleep:
  //     screen = sleep();
  //     break;
  //   default:
  //     while (true) {
  //       u8g2.clearBuffer();
  //       u8g2.setFont(u8g2_font_logisoso16_tf);
  //       u8g2.drawStr(0, 64/2 + 16/2, "Bad state");
  //       u8g2.sendBuffer();
  //       delay(16);
  //     }
  //     break;
  //   }
  // }
}
