// Favero epee SIDE UNIT - Seeed XIAO ESP32-S3 port, protocol v2
//
// Port of epee_side.ino (DevKit bench board) to the XIAO receiver
// build. The boards that arrived 2026-10-22 are the ESP32-S3
// variant of the XIAO (chip-verified: S3 rev v0.2, 8 MB PSRAM),
// not the C3 originally planned - user decision was to keep them.
// Protocol v2 packet format, pairing ritual, failsafe and status
// scheme are UNCHANGED - centrals running the DevKit firmware
// interoperate as-is.
//
// Wiring (XIAO pin -> GPIO, Seeed XIAO ESP32S3 pin map):
//   D1 = GPIO2  -> 220R -> RGB R leg
//   D3 = GPIO4  -> 220R -> RGB G leg
//   D4 = GPIO5  -> 220R -> RGB B leg
//   RGB common (longest) leg -> GND (common cathode;
//   common-anode part: leg -> 3V3, set RGB_COMMON_ANODE 1)
//   D8 = GPIO7  -> 12 mm tact button -> GND (internal pull-up,
//   diagonal pins). PAIRING ONLY on this build (see below).
//   (Button moved D5 -> D8 in the soldered build 2026-11-08;
//   the button is also the future auto-sleep wake pin.)
//   D10 = GPIO9 -> SS12D00 side-select slide switch: middle pin
//   -> D10, one outer pin -> GND, other outer pin open. Internal
//   pull-up reads it: grounded = LEFT (red), open = RIGHT (green).
//   If the built unit's knob direction feels backwards, flip
//   SIDE_LOW_IS_LEFT to 0 - one line, no rewiring.
//   D2 = GPIO3 deliberately unused (ESP32-S3 strapping pin).
//   The S3 HAS a user LED (amber, GPIO21) - it MIRRORS the RGB
//   blue status channel, DevKit-style: off = link established,
//   fast blink = pairing/learn mode, slow blink = link lost
//   after being up, solid = not linked (dark = healthy, any
//   light = trouble). Handy before the RGB is even wired.
//   Antenna: the kit flex antenna MUST be snapped onto the
//   u.FL connector. Despite the module's onboard trace, ESP-NOW
//   reception without the flex antenna was too weak to pair
//   (verified 2026-10-22: LEARN timed out; with the antenna
//   attached, pairing and link passed immediately).
//   Power: 503450 LiPo on the B+/B- pads (onboard charging);
//   charge with the power switch ON (OFF disconnects the cell).
//
// SIDE SELECTION IS THE SWITCH NOW (2026-10-17 decision):
// side is read from the physical switch at boot and live
// (debounced) - it is NOT stored in NVS and the button no longer
// cycles it. The button's only job is pairing: long press
// (>=3 s) enters learn mode. A switch flip reports the new side
// to the central in the next HELLO and blinks the new colour 3x
// as feedback, same as the old button cycle did.
//
// Pairing (LINKING_DESIGN.md): learn mode listens 20 s for the
// central's PAIR_BEACON; adopts only a strong one (RSSI >= -55,
// ~within 1 m, 3 beacons from the same MAC) so gym neighbours
// can't cross-pair. Saves {central MAC, box_id} in NVS, replies
// PAIR_ACK x5. After that it accepts LAMP/TEST packets ONLY from
// the saved central MAC - other sets are ignored completely.
// Unpaired units show no hits at all (solid blue = not linked).
//
// Confirmation choreography (unchanged from the DevKit build):
// - Pairing: when the central's window closes it broadcasts TEST:
//   left 1 s, right 1 s, end mark. This unit MIRRORS the test
//   (shows both colours as they come), then 1 s dark, then its
//   ASSIGNED colour solid for 2 s.
// - Boot: once the link first comes up, assigned colour 2 s.
// HELLO is broadcast every 5 s so the central knows we're here.
// Failsafe: link down >750 ms -> hit colour off (never a stale
// hit); the blue channel reports the lost link instead.
// USB serial (115200, native USB) logs boot/pairing/link/lamp
// events; the port appears after boot, so early boot lines can
// be missed on a cold plug-in - reset after opening the monitor
// if you need them.
// Target: Arduino-ESP32 core >= 3.0, board "XIAO_ESP32S3".
// Pin map (soldered build 2026-11-08, by D-pad — the user reads the
// silkscreen D-numbers, so translate via the XIAO_ESP32S3 Arduino
// variant, which is NOT sequential: D6=GPIO43, D7=GPIO44, D8=GPIO7,
// D9=GPIO8, D10=GPIO9. Never take a "GPIO n" from him at face value
// without checking which pad he means. R/G/B verified 2026-10-22;
// button/side corrected 2026-11-08 after the D7/D9 pads proved to
// be GPIO44/GPIO8 (the earlier GPIO7/GPIO9 build listened on the
// wrong pads and the button hold did nothing). RGB G/B legs are
// soldered swapped on the board, compensated here (G on D4, B on D3).
// POWER (redesigned 2026-11-08): the battery is wired STRAIGHT to
// B+/B- so USB charging works while the board is "off" — the PWR
// switch no longer cuts power, it is a sleep signal on D10/GPIO9
// (middle -> D10, one outer -> GND, other nc; LOW = OFF = deep
// sleep, HIGH = ON; a broken wire reads ON = awake, fail-safe).
// Wake: switch ON (ext1, GPIO9 is an RTC pin) or a 60 s timer tick
// in the auto-sleep case; the pairing button (GPIO44) can NOT wake
// from deep sleep (not an RTC pin). Auto-sleep: 15 min with no
// central packets (paired only, never while learning/testing).

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <Preferences.h>

#define WIFI_CHANNEL 6
#define PIN_R 2        // D1 = GPIO2
#define PIN_G 5        // D4 = GPIO5 (G/B swapped 2026-11-08: legs
#define PIN_B 4        // D3 = GPIO4  soldered swapped on the board)
#define PIN_BUTTON 44  // D7 pad = GPIO44 (soldered build 2026-11-08)
#define PIN_SIDE 8     // D9 pad = GPIO8
#define PIN_PWR 9      // D10 pad = GPIO9 (PWR switch SIGNAL, 2026-11-08)
#define PWR_LOW_IS_OFF 1  // switch grounded (LOW) = OFF / deep sleep
#define AUTO_SLEEP_MS (15UL * 60UL * 1000UL)  // no packets -> sleep
#define AUTO_SLEEP_CHECK_MS 60000UL           // timer wake interval
#define PIN_BUILTIN 21 // amber user LED (mirrors blue status)
// The XIAO S3 user LED is wired VCC -> LED -> GPIO21: it lights
// when the pin is LOW (verified empirically 2026-10-22: the
// first S3 build drove it HIGH and the LED stayed dark while
// the console proved the app was running).
#define BUILTIN_ON LOW
#define BUILTIN_OFF HIGH
#define SIDE_LOW_IS_LEFT 1  // switch grounded (LOW) = LEFT/red
#define RGB_COMMON_ANODE 0
#define FAILSAFE_MS 750
#define LEARN_MS 20000
#define RSSI_MIN (-55)
#define BEACONS_NEEDED 3

enum PktType : uint8_t {
  PKT_LAMP = 0,
  PKT_PAIR_BEACON = 1,
  PKT_PAIR_ACK = 2,
  PKT_HELLO = 3,
  PKT_TEST = 4,
};

struct __attribute__((packed)) Packet {
  uint16_t magic;
  uint8_t type;
  uint8_t box_id;
  uint8_t seq;
  uint8_t lamp;
  uint8_t cards;
  uint8_t score_l;
  uint8_t score_r;
  uint8_t side;
};

static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static Preferences prefs;
static uint8_t side = 0;  // 0=LEFT(red) 1=RIGHT(green), from switch
static bool paired = false;
static uint8_t central_mac[6] = {0};
static uint8_t central_box = 0;

static volatile uint8_t cur_lamp = 0;
static volatile uint32_t last_pkt_ms = 0;
static volatile bool have_pkt = false;
static volatile bool link_was_up = false;

// learn mode
static bool learning = false;
static uint32_t learn_until = 0;
static uint8_t cand_mac[6] = {0};
static int cand_count = 0;

// test / show choreography
static bool test_active = false;
static uint8_t test_lamp = 0;
static uint32_t dark_until = 0;      // post-test 1 s break
static uint32_t show_until = 0;      // assigned colour 2 s show
static bool boot_show_done = false;
static uint32_t feedback_until = 0;  // side-change blink

static uint8_t read_side_switch() {
  bool low = digitalRead(PIN_SIDE) == LOW;
#if SIDE_LOW_IS_LEFT
  return low ? 0 : 1;
#else
  return low ? 1 : 0;
#endif
}

static void set_rgb(bool r, bool g, bool b) {
#if RGB_COMMON_ANODE
  digitalWrite(PIN_R, r ? LOW : HIGH);
  digitalWrite(PIN_G, g ? LOW : HIGH);
  digitalWrite(PIN_B, b ? LOW : HIGH);
#else
  digitalWrite(PIN_R, r ? HIGH : LOW);
  digitalWrite(PIN_G, g ? HIGH : LOW);
  digitalWrite(PIN_B, b ? HIGH : LOW);
#endif
}

// --- power: the switch is a sleep signal, not a power cut -----------
// The battery stays wired straight to B+/B- so the charger IC keeps
// charging from USB while the board is "off". OFF = deep sleep; wake
// on switch ON (ext1 — PIN_PWR/GPIO9 is an RTC pin) or a 60 s timer
// tick (auto-sleep case only). The pairing button (D7/GPIO44) can NOT
// wake from deep sleep — it is not an RTC pin — so it stays
// pairing-only while awake. Deep sleep is tens-to-hundreds of uA
// (months on the 1000 mAh cell), not true zero like the old series
// switch was; the XIAO's onboard RST button still resets a wedged
// board. Fail-safe: a broken switch wire reads HIGH (pull-up) = ON,
// so the board stays awake rather than sleeping forever.
static bool pwr_is_off() {
#if PWR_LOW_IS_OFF
  return digitalRead(PIN_PWR) == LOW;
#else
  return digitalRead(PIN_PWR) == HIGH;
#endif
}

static void go_sleep(bool timer_wake) {
  set_rgb(false, false, false);
  digitalWrite(PIN_BUILTIN, BUILTIN_OFF);
  Serial.flush();
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);
  esp_sleep_enable_ext1_wakeup(1ULL << PIN_PWR, ESP_EXT1_WAKEUP_ANY_HIGH);
  if (timer_wake) esp_sleep_enable_timer_wakeup(AUTO_SLEEP_CHECK_MS * 1000ULL);
  esp_deep_sleep_start();
}

static esp_sleep_wakeup_cause_t wake_cause = ESP_SLEEP_WAKEUP_UNDEFINED;

static void send_packet(uint8_t type, uint8_t lamp) {
  Packet p{};
  p.magic = 0x4656;
  p.type = type;
  p.box_id = paired ? central_box : 0;
  p.lamp = lamp;
  p.side = side;
  esp_now_send(kBroadcast, (const uint8_t *)&p, sizeof(p));
}

static void save_pairing() {
  prefs.putBool("paired", true);
  prefs.putBytes("cmac", central_mac, 6);
  prefs.putUChar("cbox", central_box);
}

static void adopt(const uint8_t *mac, uint8_t box) {
  memcpy(central_mac, mac, 6);
  central_box = box;
  paired = true;
  save_pairing();
  learning = false;
  for (int i = 0; i < 5; i++) { send_packet(PKT_PAIR_ACK, 0); delay(30); }
  Serial.printf("PAIRED to central %02X:%02X:%02X:%02X:%02X:%02X box %u\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], box);
}

static void on_recv(const esp_now_recv_info_t *info,
                    const uint8_t *data, int len) {
  if (len != (int)sizeof(Packet)) return;
  const Packet *p = (const Packet *)data;
  if (p->magic != 0x4656) return;

  if (learning && p->type == PKT_PAIR_BEACON) {
    int rssi = info->rx_ctrl ? info->rx_ctrl->rssi : -99;
    if (rssi < RSSI_MIN) return;
    if (memcmp(cand_mac, info->src_addr, 6) != 0) {
      memcpy(cand_mac, info->src_addr, 6);
      cand_count = 1;
    } else {
      cand_count++;
    }
    if (cand_count >= BEACONS_NEEDED) adopt(info->src_addr, p->box_id);
    return;
  }

  if (!paired) return;
  if (memcmp(info->src_addr, central_mac, 6) != 0) return;  // not ours

  if (p->type == PKT_LAMP) {
    cur_lamp = p->lamp;
    last_pkt_ms = millis();
    have_pkt = true;
  } else if (p->type == PKT_TEST) {
    last_pkt_ms = millis();
    have_pkt = true;
    if (p->lamp == 0x00 && test_active) {
      // end mark: 1 s dark break, then assigned colour 2 s
      test_active = false;
      dark_until = millis() + 1000;
      show_until = millis() + 3000;
    } else if (p->lamp != 0x00) {
      test_active = true;
      test_lamp = p->lamp;
    }
  }
}

void setup() {
  wake_cause = esp_sleep_get_wakeup_cause();
  Serial.begin(115200);
  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_SIDE, INPUT_PULLUP);
  pinMode(PIN_PWR, INPUT_PULLUP);
  pinMode(PIN_BUILTIN, OUTPUT);
  digitalWrite(PIN_BUILTIN, BUILTIN_OFF);
  set_rgb(false, false, false);

  // PWR switch OFF at boot: stay dark, sleep at once. Charging still
  // works on USB — the charger IC is independent of the ESP32.
  if (pwr_is_off()) {
    Serial.println("PWR switch OFF at boot - deep sleep");
    go_sleep(false);
  }

  side = read_side_switch();

  prefs.begin("epee-side", false);
  paired = prefs.getBool("paired", false);
  if (paired) {
    prefs.getBytes("cmac", central_mac, 6);
    central_box = prefs.getUChar("cbox", 0);
  }

  // RGB lamp test: R, G, B, then current side's colour x2.
  set_rgb(true, false, false); delay(300);
  set_rgb(false, true, false); delay(300);
  set_rgb(false, false, true); delay(300);
  set_rgb(false, false, false);
  for (int i = 0; i < 2; i++) {
    set_rgb(side == 0, side == 1, false); delay(200);
    set_rgb(false, false, false); delay(150);
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (true) delay(1000);
  }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, kBroadcast, 6);
  peer.channel = WIFI_CHANNEL;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
  esp_now_register_recv_cb(on_recv);

  if (paired) {
    Serial.printf("side unit (XIAO S3) ready: side=%s (switch), "
                  "paired to %02X:%02X:%02X:%02X:%02X:%02X box %u\n",
                  side ? "RIGHT" : "LEFT", central_mac[0], central_mac[1],
                  central_mac[2], central_mac[3], central_mac[4],
                  central_mac[5], central_box);
    send_packet(PKT_HELLO, 0);
  } else {
    Serial.println("side unit (XIAO S3) ready: UNPAIRED - "
                   "long-press button to pair");
  }

  // Amber self-test: 3 blinks so a bare board visibly proves it
  // is alive before the steady status state takes over.
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_BUILTIN, BUILTIN_ON); delay(150);
    digitalWrite(PIN_BUILTIN, BUILTIN_OFF); delay(150);
  }
}

void loop() {
  uint32_t now = millis();

  // --- side switch: read live, debounced; a flip is the only ---
  // --- way side changes now (no NVS, no button cycle) -----------
  static bool sw_init = false;
  static uint8_t sw_stable = 0, sw_last = 0;
  static uint32_t sw_change_ms = 0;
  uint8_t sw_raw = read_side_switch();
  if (!sw_init) {
    sw_init = true;
    sw_stable = sw_last = sw_raw;
    side = sw_raw;
  }
  if (sw_raw != sw_last) { sw_last = sw_raw; sw_change_ms = now; }
  if (now - sw_change_ms > 40 && sw_raw != sw_stable) {
    sw_stable = sw_raw;
    side = sw_raw;
    feedback_until = now + 900;
    Serial.printf("side switch -> %s\n",
                  side ? "RIGHT (green)" : "LEFT (red)");
    if (paired) send_packet(PKT_HELLO, 0);
  }

  // --- PWR switch: OFF (grounded) = deep sleep, ON = wake -----------
  static bool pwr_init = false;
  static bool pwr_stable = HIGH, pwr_last = HIGH;
  static uint32_t pwr_change_ms = 0;
  bool pwr_raw = digitalRead(PIN_PWR);
  if (!pwr_init) { pwr_init = true; pwr_stable = pwr_last = pwr_raw; }
  if (pwr_raw != pwr_last) { pwr_last = pwr_raw; pwr_change_ms = now; }
  if (now - pwr_change_ms > 40 && pwr_raw != pwr_stable) {
    pwr_stable = pwr_raw;
    if (pwr_is_off()) {
      Serial.println("PWR switch OFF - deep sleep (wake on switch ON)");
      go_sleep(false);
    } else {
      Serial.println("PWR switch ON");
    }
  }

  // --- timer wake (auto-sleep tick): no central in 5 s -> sleep again
  if (wake_cause == ESP_SLEEP_WAKEUP_TIMER && !have_pkt && now > 5000) {
    Serial.println("timer wake, no central - back to sleep");
    go_sleep(true);
  }

  // --- auto-sleep: paired, switch ON, no central packets for 15 min -
  if (paired && !learning && !test_active && have_pkt &&
      (now - last_pkt_ms > AUTO_SLEEP_MS)) {
    Serial.println("no central packets 15 min - deep sleep");
    go_sleep(true);
  }

  // --- button: pairing only. Short press does nothing; ---------
  // --- long press (>=3 s) enters learn mode ----------------------
  static bool btn_stable = HIGH, btn_last = HIGH;
  static uint32_t btn_change_ms = 0, btn_press_ms = 0;
  static bool long_fired = false;
  bool btn = digitalRead(PIN_BUTTON);
  if (btn != btn_last) { btn_last = btn; btn_change_ms = now; }
  if (now - btn_change_ms > 30 && btn != btn_stable) {
    btn_stable = btn;
    if (btn_stable == LOW) { btn_press_ms = now; long_fired = false; }
  }
  if (btn_stable == LOW && !long_fired && now - btn_press_ms >= 3000) {
    long_fired = true;
    learning = true;
    learn_until = now + LEARN_MS;
    cand_count = 0;
    Serial.println("LEARN mode: listening for a pairing beacon (20 s)");
  }
  if (learning && now >= learn_until) {
    learning = false;
    Serial.println("LEARN timeout - no central adopted");
  }

  // --- HELLO every 5 s when paired --------------------------------
  static uint32_t last_hello_ms = 0;
  if (paired && now - last_hello_ms >= 5000) {
    last_hello_ms = now;
    send_packet(PKT_HELLO, 0);
  }

  // --- link state ---------------------------------------------------
  bool link_up = paired && have_pkt && (now - last_pkt_ms <= FAILSAFE_MS);
  if (link_up) link_was_up = true;
  if (link_up && !boot_show_done) {
    boot_show_done = true;
    show_until = now + 2000;  // assigned colour on boot link
    Serial.println("link UP - showing assigned colour 2 s");
  }

  // --- status blue: off = linked, solid = not linked, slow blink
  // = link lost, fast blink = learn mode. Hits always win when up.
  bool status_fast = learning;
  bool status_slow = paired && !link_up && link_was_up;
  bool status_solid = !paired || (paired && !link_up && !link_was_up);

  // --- RGB output ---------------------------------------------------
  if (learning) {
    bool on = (now / 150) % 2 == 0;  // fast blue blink
    set_rgb(false, false, on);
  } else if (test_active) {
    set_rgb(test_lamp & 0x04, test_lamp & 0x08, false);  // mirror, both
  } else if (now < dark_until) {
    set_rgb(false, false, false);  // 1 s break after test
  } else if (now < show_until || now < feedback_until) {
    if (now < show_until) {
      set_rgb(side == 0, side == 1, false);  // assigned colour solid
    } else {
      bool on = (now / 150) % 2 == 0;        // side-change feedback
      set_rgb(on && side == 0, on && side == 1, false);
    }
  } else if (status_solid) {
    set_rgb(false, false, true);   // not linked: solid blue
  } else if (status_slow) {
    bool on = (now / 500) % 2 == 0;  // link lost: slow blue blink
    set_rgb(false, false, on);
  } else {
    bool hit = side == 0 ? (cur_lamp & 0x04) : (cur_lamp & 0x08);
    set_rgb(hit && side == 0, hit && side == 1, false);
  }

  // --- built-in amber LED mirrors the blue status channel --------
  bool builtin;
  if (status_fast) builtin = (now / 150) % 2 == 0;
  else if (status_slow) builtin = (now / 500) % 2 == 0;
  else if (status_solid) builtin = true;
  else builtin = false;  // linked: dark
  digitalWrite(PIN_BUILTIN, builtin ? BUILTIN_ON : BUILTIN_OFF);

  // --- console log on lamp/link change ------------------------------
  static uint8_t logged_lamp = 0xFF;
  static bool logged_link = false, logged_paired = false;
  if (paired != logged_paired) {
    logged_paired = paired;
    if (paired) Serial.println("state: PAIRED");
  }
  if (link_up != logged_link || (link_up && cur_lamp != logged_lamp)) {
    logged_link = link_up;
    if (link_up) {
      logged_lamp = cur_lamp;
      Serial.printf("lamp=0x%02X left_hit=%d right_hit=%d showing=%s\n",
                    cur_lamp, !!(cur_lamp & 0x04), !!(cur_lamp & 0x08),
                    side ? "RIGHT" : "LEFT");
    } else if (have_pkt) {
      logged_lamp = 0xFF;
      Serial.println("link lost (failsafe)");
    }
  }
  delay(5);
}
