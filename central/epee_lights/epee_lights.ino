// Favero epee CENTRAL (bench board on COM3)
//
// Parses real Favero DATA-LINE frames (10 bytes, 2400 8N1, ~42 ms)
// from USB Serial (bench emulator) or Serial2/GPIO16 (real box /
// USB-TTL), drives local LEDs, and relays lamp state to side units
// over ESP-NOW (ch 6):
//   GPIO25 -> 220R -> red LED (left hit)   [anode leg; cathode->GND]
//   GPIO26 -> 220R -> green LED (right hit)
//   GPIO2  built-in blue = link/pairing status (see below)
//
// ESP-NOW protocol v2 (shared with epee_side):
//   LAMP        heartbeat 100 ms from boot + burst x3 on change.
//               Lamp is gated: stale Favero input relays lamp=0.
//   PAIR_BEACON broadcast 10 Hz while the pairing window is open.
//   PAIR_ACK    side -> central on adopting; central saves roster.
//   HELLO       side -> central every 5 s when paired (presence).
//   TEST        confirmation pattern after the window closes:
//               left 1 s, right 1 s, end marker. Sides mirror it
//               unfiltered, pause 1 s, show their assigned colour
//               for 2 s.
//
// Pairing window: press the board's BOOT button (GPIO0). Press
// again to close early. Window auto-closes after 20 s; closing
// runs the TEST sequence. Roster (up to 4 side units) in NVS.
//
// Built-in LED: fast blink = pairing window open; solid = a paired
// side unit HELLO'd within 5 s; off = idle/no side unit seen.
//
// Epee only: white/card bits parsed but ignored.
// Target: Arduino-ESP32 core >= 3.0, ESP32 Dev Module.

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>

#define PIN_RED 25
#define PIN_GREEN 26
#define PIN_BUILTIN 2
#define PIN_PAIR_BTN 0  // BOOT button
#define FAVERO_RX_PIN 16
#define FAVERO_TX_PIN 17
#define FAILSAFE_MS 2000
#define WIFI_CHANNEL 6
#define BOX_ID 1
#define PAIR_WINDOW_MS 20000

// ---------------- Favero parser (HUNTING/LOCKED) -------------------
struct FaveroFrame {
  uint8_t score_r, score_l, sec, min, lamp, matches, cards;
};

static bool frame_ok(const uint8_t *f) {
  uint16_t sum = 0;
  for (int i = 0; i < 9; i++) sum += f[i];
  return (sum & 0xFF) == f[9];
}

static uint8_t bcd(uint8_t b) { return ((b >> 4) & 0x0F) * 10 + (b & 0x0F); }

struct Parser {
  uint8_t buf[64];
  size_t len = 0;
  bool locked = false;
  unsigned long ckerr = 0;

  bool feed(uint8_t b, FaveroFrame &out) {
    if (len >= sizeof(buf)) { len = 0; locked = false; }
    buf[len++] = b;
    while (true) {
      if (!locked) {
        size_t idx = 0;
        while (idx < len && buf[idx] != 0xFF) idx++;
        if (idx == len) { len = 0; return false; }
        if (idx > 0) { memmove(buf, buf + idx, len - idx); len -= idx; }
        if (len < 10) return false;
        if (frame_ok(buf)) { locked = true; break; }
        ckerr++;
        memmove(buf, buf + 1, len - 1); len -= 1;
      } else {
        if (len < 10) return false;
        if (buf[0] != 0xFF) { locked = false; continue; }
        if (frame_ok(buf)) break;
        ckerr++;
        locked = false;
        memmove(buf, buf + 1, len - 1); len -= 1;
      }
    }
    out.score_r = bcd(buf[1]);
    out.score_l = bcd(buf[2]);
    out.sec = bcd(buf[3]);
    out.min = bcd(buf[4]);
    out.lamp = buf[5];
    out.matches = buf[6];
    out.cards = buf[8];
    memmove(buf, buf + 10, len - 10);
    len -= 10;
    return true;
  }
};

static Parser usb_parser;
static Parser uart_parser;
static uint8_t cur_lamp = 0;
static uint8_t cur_score_l = 0, cur_score_r = 0;
static uint32_t last_frame_ms = 0;
static bool have_frame = false;

// ---------------- ESP-NOW protocol v2 ------------------------------
enum PktType : uint8_t {
  PKT_LAMP = 0,
  PKT_PAIR_BEACON = 1,
  PKT_PAIR_ACK = 2,
  PKT_HELLO = 3,
  PKT_TEST = 4,
};

struct __attribute__((packed)) Packet {
  uint16_t magic;    // 0x4656 "FV"
  uint8_t type;      // PktType
  uint8_t box_id;
  uint8_t seq;
  uint8_t lamp;      // LAMP/TEST: Favero lamp byte
  uint8_t cards;
  uint8_t score_l;
  uint8_t score_r;
  uint8_t side;      // ACK/HELLO: 0=LEFT, 1=RIGHT
};

static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static Packet tx{};
static uint8_t tx_seq = 0;
static uint32_t last_send_ms = 0;

static void send_raw(uint8_t type, uint8_t lamp) {
  tx.magic = 0x4656;
  tx.type = type;
  tx.box_id = BOX_ID;
  tx.seq = tx_seq++;
  tx.lamp = lamp;
  tx.cards = 0;
  tx.score_l = cur_score_l;
  tx.score_r = cur_score_r;
  tx.side = 0;
  esp_now_send(kBroadcast, (const uint8_t *)&tx, sizeof(tx));
}

static uint8_t live_lamp() {
  return (have_frame && millis() - last_frame_ms <= FAILSAFE_MS)
             ? cur_lamp : 0;
}

static void send_lamp(int bursts) {
  for (int i = 0; i < bursts; i++) {
    send_raw(PKT_LAMP, live_lamp());
    if (i + 1 < bursts) delay(5);
  }
  last_send_ms = millis();
}

// ---------------- roster (paired side units, NVS) ------------------
static Preferences prefs;
static uint8_t roster_mac[4][6];
static uint8_t roster_side[4];
static int roster_n = 0;
static uint32_t last_hello_ms = 0;

static void roster_save() {
  prefs.putUChar("n", (uint8_t)roster_n);
  for (int i = 0; i < roster_n; i++) {
    char k[4];
    snprintf(k, sizeof(k), "m%d", i);
    prefs.putBytes(k, roster_mac[i], 6);
    snprintf(k, sizeof(k), "s%d", i);
    prefs.putUChar(k, roster_side[i]);
  }
}

static void roster_load() {
  roster_n = prefs.getUChar("n", 0);
  if (roster_n > 4) roster_n = 4;
  for (int i = 0; i < roster_n; i++) {
    char k[4];
    snprintf(k, sizeof(k), "m%d", i);
    prefs.getBytes(k, roster_mac[i], 6);
    snprintf(k, sizeof(k), "s%d", i);
    roster_side[i] = prefs.getUChar(k, 0);
  }
}

static int roster_find(const uint8_t *mac) {
  for (int i = 0; i < roster_n; i++)
    if (memcmp(roster_mac[i], mac, 6) == 0) return i;
  return -1;
}

static void roster_add(const uint8_t *mac, uint8_t sd) {
  int i = roster_find(mac);
  if (i < 0) {
    if (roster_n >= 4) return;
    i = roster_n++;
    memcpy(roster_mac[i], mac, 6);
  }
  roster_side[i] = sd;
  roster_save();
}

// ---------------- pairing window + test sequence -----------------
static bool pair_open = false;
static uint32_t pair_until = 0;
static uint32_t last_beacon_ms = 0;
static int test_phase = -1;  // -1 idle, 0 left, 1 right, 2 end mark
static uint32_t test_until = 0;

static void start_test() {
  test_phase = 0;
  test_until = millis() + 1000;
}

static void open_window() {
  pair_open = true;
  pair_until = millis() + PAIR_WINDOW_MS;
}

static void close_window() {
  pair_open = false;
  start_test();
}

static void on_recv(const esp_now_recv_info_t *info,
                    const uint8_t *data, int len) {
  if (len != (int)sizeof(Packet)) return;
  const Packet *p = (const Packet *)data;
  if (p->magic != 0x4656 || p->box_id != BOX_ID) return;
  if (p->type == PKT_PAIR_ACK && pair_open) {
    roster_add(info->src_addr, p->side);
  } else if (p->type == PKT_HELLO) {
    if (roster_find(info->src_addr) >= 0) last_hello_ms = millis();
  }
}

static void apply_frame(const FaveroFrame &f) {
  bool changed = !have_frame || f.lamp != cur_lamp ||
                 f.score_l != cur_score_l || f.score_r != cur_score_r;
  cur_lamp = f.lamp;
  cur_score_l = f.score_l;
  cur_score_r = f.score_r;
  last_frame_ms = millis();
  have_frame = true;
  if (changed) send_lamp(3);
}

void setup() {
  pinMode(PIN_RED, OUTPUT);
  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_BUILTIN, OUTPUT);
  pinMode(PIN_PAIR_BTN, INPUT_PULLUP);

  // Lamp test: built-in 3 blinks, then red, green, both.
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_BUILTIN, HIGH); delay(180);
    digitalWrite(PIN_BUILTIN, LOW); delay(180);
  }
  digitalWrite(PIN_RED, HIGH); delay(400);
  digitalWrite(PIN_RED, LOW);
  digitalWrite(PIN_GREEN, HIGH); delay(400);
  digitalWrite(PIN_GREEN, LOW);
  digitalWrite(PIN_RED, HIGH); digitalWrite(PIN_GREEN, HIGH); delay(400);
  digitalWrite(PIN_RED, LOW); digitalWrite(PIN_GREEN, LOW);

  Serial.begin(2400, SERIAL_8N1);  // USB = bench Favero input
  Serial2.begin(2400, SERIAL_8N1, FAVERO_RX_PIN, FAVERO_TX_PIN);

  prefs.begin("epee-central", false);
  roster_load();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() == ESP_OK) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, kBroadcast, 6);
    peer.channel = WIFI_CHANNEL;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
    esp_now_register_recv_cb(on_recv);
  }
}

void loop() {
  FaveroFrame f;
  while (Serial.available() > 0) {
    if (usb_parser.feed((uint8_t)Serial.read(), f)) apply_frame(f);
  }
  while (Serial2.available() > 0) {
    if (uart_parser.feed((uint8_t)Serial2.read(), f)) apply_frame(f);
  }

  // BOOT button: toggle pairing window (debounced edge).
  static bool btn_stable = HIGH, btn_last = HIGH;
  static uint32_t btn_ms = 0;
  bool btn = digitalRead(PIN_PAIR_BTN);
  if (btn != btn_last) { btn_last = btn; btn_ms = millis(); }
  if (millis() - btn_ms > 30 && btn != btn_stable) {
    btn_stable = btn;
    if (btn_stable == LOW) {
      if (pair_open) close_window(); else open_window();
    }
  }
  if (pair_open && millis() >= pair_until) close_window();
  if (pair_open && millis() - last_beacon_ms >= 100) {
    last_beacon_ms = millis();
    send_raw(PKT_PAIR_BEACON, 0);
  }

  // Test sequence state machine.
  uint8_t test_lamp = 0;
  bool testing = test_phase >= 0;
  if (testing) {
    if (millis() >= test_until) {
      test_phase++;
      test_until = millis() + (test_phase == 2 ? 60 : 1000);
      if (test_phase > 2) { test_phase = -1; testing = false; }
    }
    if (testing) {
      test_lamp = test_phase == 0 ? 0x04 : test_phase == 1 ? 0x08 : 0x00;
      static uint32_t last_test_ms = 0;
      if (millis() - last_test_ms >= 100) {
        last_test_ms = millis();
        send_raw(PKT_TEST, test_lamp);
      }
    }
  }

  // Heartbeat from boot (lamp gated by stream freshness).
  if (!testing && millis() - last_send_ms >= 100) send_lamp(1);

  // Local display: test pattern mirrors to local LEDs too.
  uint8_t lamp = testing ? test_lamp : live_lamp();
  digitalWrite(PIN_RED, (lamp & 0x04) ? HIGH : LOW);
  digitalWrite(PIN_GREEN, (lamp & 0x08) ? HIGH : LOW);

  // Built-in: window = fast blink; recent HELLO = solid; else off.
  bool builtin;
  if (pair_open) builtin = (millis() / 150) % 2;
  else builtin = last_hello_ms && millis() - last_hello_ms <= 5000;
  digitalWrite(PIN_BUILTIN, builtin ? HIGH : LOW);
  delay(2);
}
