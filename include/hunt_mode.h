#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "spi_protocol_shared.h"

// Fox-hunt mode.
//
// The stock firmware is a wardriver: it deduplicates every BSSID for the whole
// session and writes one CSV row per network. That is exactly wrong for a fox
// hunt, where the target is a single known BSSID and the useful signal is how
// its RSSI changes as you walk. Hunt mode keeps the wardriving path intact and
// adds a parallel path for one target: dedupe is bypassed so every sighting is
// reported, and each sighting prints a live line on the USB console.
//
// All knobs are build flags (see platformio.ini hunt_* environments).

#ifndef HUNT_MODE_ENABLED
#define HUNT_MODE_ENABLED 0
#endif

// Target BSSID as a string, e.g. "F2:22:28:43:1E:7C". Separators may be ':',
// '-', or omitted. An empty string disables BSSID matching.
#ifndef HUNT_TARGET_BSSID
#define HUNT_TARGET_BSSID ""
#endif

// Case-insensitive SSID substring, e.g. "Hard Fox". Empty disables SSID
// matching. Prefer matching on BSSID alone: an SSID is trivially spoofed by
// another team, a BSSID sighting is what you actually want to chase.
#ifndef HUNT_TARGET_SSID
#define HUNT_TARGET_SSID ""
#endif

// Report every sighting of the target instead of only the first one. This is
// the whole point of hunt mode; leave it on.
#ifndef HUNT_BYPASS_DEDUPE
#define HUNT_BYPASS_DEDUPE 1
#endif

// Print a console line per target sighting.
#ifndef HUNT_LIVE_PRINT
#define HUNT_LIVE_PRINT 1
#endif

// Print a "no contact" heartbeat when the target has been silent this long.
// The hard AP foxes sleep 45s between 30s transmit windows, so silence is
// normal and worth distinguishing from "walked out of range". 0 disables.
#ifndef HUNT_IDLE_NOTICE_MS
#define HUNT_IDLE_NOTICE_MS 10000
#endif

// RSSI range mapped onto the signal bar, in dBm.
#ifndef HUNT_BAR_RSSI_MIN
#define HUNT_BAR_RSSI_MIN (-95)
#endif
#ifndef HUNT_BAR_RSSI_MAX
#define HUNT_BAR_RSSI_MAX (-30)
#endif
#ifndef HUNT_BAR_WIDTH
#define HUNT_BAR_WIDTH 20
#endif

struct HuntTarget {
  uint8_t bssid[6];
  bool bssid_valid;
  char ssid[33];
  bool ssid_valid;
};

struct HuntTargetState {
  uint32_t hits;
  uint32_t first_seen_ms;
  uint32_t last_seen_ms;
  uint32_t last_idle_notice_ms;
  int8_t last_rssi;
  int8_t best_rssi;
  bool seen;
};

static inline int huntHexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return (c - 'a') + 10;
  if (c >= 'A' && c <= 'F') return (c - 'A') + 10;
  return -1;
}

// Parse "F2:22:28:43:1E:7C", "f2-22-28-43-1e-7c" or "F222284 31E7C"-style input
// into six bytes. Returns false unless exactly twelve hex digits are present.
static inline bool huntParseBssid(const char* text, uint8_t out[6]) {
  if (text == nullptr) {
    return false;
  }

  uint8_t nibbles[12] = {};
  size_t count = 0;
  for (const char* p = text; *p != '\0'; p++) {
    if (*p == ':' || *p == '-' || *p == '.' || *p == ' ') {
      continue;
    }
    const int nibble = huntHexNibble(*p);
    if (nibble < 0) {
      return false;
    }
    if (count >= sizeof(nibbles)) {
      return false;
    }
    nibbles[count++] = (uint8_t)nibble;
  }

  if (count != sizeof(nibbles)) {
    return false;
  }
  for (size_t i = 0; i < 6; i++) {
    out[i] = (uint8_t)((nibbles[i * 2] << 4) | nibbles[(i * 2) + 1]);
  }
  return true;
}

static inline char huntLowerAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static inline bool huntSsidContains(const char* haystack, const char* needle) {
  if (needle == nullptr || needle[0] == '\0') {
    return true;
  }
  if (haystack == nullptr) {
    return false;
  }

  for (size_t start = 0; haystack[start] != '\0'; start++) {
    size_t i = 0;
    while (needle[i] != '\0' &&
           huntLowerAscii(haystack[start + i]) == huntLowerAscii(needle[i])) {
      i++;
    }
    if (needle[i] == '\0') {
      return true;
    }
  }
  return false;
}

static inline HuntTarget huntTargetFromConfig() {
  HuntTarget target = {};
  target.bssid_valid = huntParseBssid(HUNT_TARGET_BSSID, target.bssid);
  strncpy(target.ssid, HUNT_TARGET_SSID, sizeof(target.ssid) - 1);
  target.ssid_valid = (target.ssid[0] != '\0');
  return target;
}

static inline bool huntTargetIsConfigured(const HuntTarget& target) {
  return target.bssid_valid || target.ssid_valid;
}

// Both configured filters must match. With only a BSSID set this is a pure
// BSSID match, which is the recommended configuration.
static inline bool huntTargetMatches(const HuntTarget& target, const WiFiResult& result) {
  if (!huntTargetIsConfigured(target)) {
    return false;
  }
  if (target.bssid_valid && memcmp(target.bssid, result.bssid, sizeof(target.bssid)) != 0) {
    return false;
  }
  if (target.ssid_valid && !huntSsidContains(result.ssid, target.ssid)) {
    return false;
  }
  return true;
}

static inline void huntStateNoteHit(HuntTargetState& state, int8_t rssi, uint32_t now_ms) {
  if (!state.seen) {
    state.seen = true;
    state.first_seen_ms = now_ms;
    state.best_rssi = rssi;
  } else if (rssi > state.best_rssi) {
    state.best_rssi = rssi;
  }
  state.last_rssi = rssi;
  state.last_seen_ms = now_ms;
  state.last_idle_notice_ms = now_ms;
  if (state.hits < 0xFFFFFFFFu) {
    state.hits++;
  }
}

// Render an RSSI as a fixed-width bar so signal changes are readable at a
// glance while walking, rather than having to compare numbers.
static inline void huntFormatBar(int8_t rssi, char* out, size_t out_len) {
  if (out == nullptr || out_len == 0) {
    return;
  }

  size_t width = HUNT_BAR_WIDTH;
  if (width > out_len - 1) {
    width = out_len - 1;
  }

  int32_t span = (int32_t)HUNT_BAR_RSSI_MAX - (int32_t)HUNT_BAR_RSSI_MIN;
  if (span <= 0) {
    span = 1;
  }
  int32_t level = ((int32_t)rssi - (int32_t)HUNT_BAR_RSSI_MIN) * (int32_t)width / span;
  if (level < 0) {
    level = 0;
  }
  if (level > (int32_t)width) {
    level = (int32_t)width;
  }

  for (size_t i = 0; i < width; i++) {
    out[i] = ((int32_t)i < level) ? '#' : '-';
  }
  out[width] = '\0';
}

// Elapsed milliseconds rendered as seconds with one decimal, without dragging
// in floating-point printf on the controller.
static inline void huntFormatElapsed(uint32_t elapsed_ms, char* out, size_t out_len) {
  if (out == nullptr || out_len == 0) {
    return;
  }
  snprintf(out, out_len, "%lu.%lus",
           (unsigned long)(elapsed_ms / 1000u),
           (unsigned long)((elapsed_ms % 1000u) / 100u));
}
