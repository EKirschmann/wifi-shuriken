#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "channel_scheduler.h"
#include "hunt_mode.h"

// Runtime hunt configuration read from a small text file on the SD card.
//
// The target BSSID and the channel plan are otherwise fixed at build time,
// which means switching foxes requires a reflash. The three RF Village targets
// sit on different bands, so that would mean rebuilding mid-hunt. This lets the
// card carry the target instead: edit one line, power-cycle, hunt something
// else.
//
// The parser is deliberately free of any SD or Arduino dependency so it can be
// exercised natively. Reading the file is the caller's job.
//
// Format (all keys optional, '#' or ';' begins a comment):
//
//   # WiFi-Shuriken hunt target
//   bssid = F2:2F:E4:6B:D2:9E
//   ssid  =
//   band  = 5
//
// band accepts: 2.4 | 24 | 2 -> 2.4GHz sweep
//               5 | 5g       -> 5GHz sweep
//               all | both   -> full dual-band sweep
//               default      -> keep the plan the firmware was built with

#ifndef HUNT_CONFIG_PATH
#define HUNT_CONFIG_PATH "/hunt.txt"
#endif

// Longest single line handled. The file is read a line at a time rather than
// slurped into one buffer: a documented config easily runs past a kilobyte of
// comments with the actual settings at the bottom, and a whole-file buffer
// would silently parse only the header and report "no usable settings".
#ifndef HUNT_CONFIG_MAX_LINE
#define HUNT_CONFIG_MAX_LINE 192
#endif

enum HuntBandSelect : uint8_t {
  HUNT_BAND_KEEP_DEFAULT = 0,
  HUNT_BAND_24GHZ = 1,
  HUNT_BAND_5GHZ = 2,
  HUNT_BAND_ALL = 3,
};

struct HuntConfig {
  // *_seen records that the key appeared at all, which is what distinguishes
  // "not mentioned, keep the built-in value" from "explicitly blank, clear the
  // filter". has_* records that a usable value was set.
  bool bssid_seen;
  bool has_bssid;
  // Repeating the bssid key adds targets rather than replacing, so one card can
  // carry every fox that is live.
  uint8_t bssid[HUNT_MAX_TARGETS][6];
  uint8_t bssid_count;
  uint16_t bssid_dropped;
  bool ssid_seen;
  bool has_ssid;
  char ssid[33];
  uint8_t band;
  uint16_t keys_ok;
  uint16_t keys_bad;
};

static inline void huntConfigInit(HuntConfig& cfg) {
  memset(&cfg, 0, sizeof(cfg));
  cfg.band = HUNT_BAND_KEEP_DEFAULT;
}

static inline size_t huntCfgTrimStart(const char* s, size_t begin, size_t end) {
  while (begin < end && (s[begin] == ' ' || s[begin] == '\t')) {
    begin++;
  }
  return begin;
}

static inline size_t huntCfgTrimEnd(const char* s, size_t begin, size_t end) {
  while (end > begin &&
         (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) {
    end--;
  }
  return end;
}

static inline bool huntCfgEquals(const char* s, size_t begin, size_t end, const char* literal) {
  size_t i = 0;
  while (literal[i] != '\0') {
    if (begin + i >= end) {
      return false;
    }
    if (huntLowerAscii(s[begin + i]) != huntLowerAscii(literal[i])) {
      return false;
    }
    i++;
  }
  return (begin + i) == end;
}

static inline void huntCfgCopy(const char* s, size_t begin, size_t end,
                               char* out, size_t out_len) {
  size_t i = 0;
  while ((begin + i) < end && (i + 1) < out_len) {
    out[i] = s[begin + i];
    i++;
  }
  out[i] = '\0';
}

static inline bool huntCfgParseBand(const char* s, size_t begin, size_t end, uint8_t& out) {
  if (huntCfgEquals(s, begin, end, "2.4") || huntCfgEquals(s, begin, end, "24") ||
      huntCfgEquals(s, begin, end, "2") || huntCfgEquals(s, begin, end, "2.4ghz") ||
      huntCfgEquals(s, begin, end, "24ghz") || huntCfgEquals(s, begin, end, "2g")) {
    out = HUNT_BAND_24GHZ;
    return true;
  }
  if (huntCfgEquals(s, begin, end, "5") || huntCfgEquals(s, begin, end, "5g") ||
      huntCfgEquals(s, begin, end, "5ghz")) {
    out = HUNT_BAND_5GHZ;
    return true;
  }
  if (huntCfgEquals(s, begin, end, "all") || huntCfgEquals(s, begin, end, "both") ||
      huntCfgEquals(s, begin, end, "full") || huntCfgEquals(s, begin, end, "dual")) {
    out = HUNT_BAND_ALL;
    return true;
  }
  if (huntCfgEquals(s, begin, end, "default") || huntCfgEquals(s, begin, end, "auto")) {
    out = HUNT_BAND_KEEP_DEFAULT;
    return true;
  }
  return false;
}

// Parses one already-delimited line. Returns false only for a line that looked
// like a setting but could not be understood; blank and comment lines return
// true without recording a key.
static inline bool huntConfigParseLine(const char* text, size_t begin, size_t end,
                                       HuntConfig& cfg) {
  // Strip comments before trimming so "bssid = X # note" works.
  for (size_t i = begin; i < end; i++) {
    if (text[i] == '#' || text[i] == ';') {
      end = i;
      break;
    }
  }

  begin = huntCfgTrimStart(text, begin, end);
  end = huntCfgTrimEnd(text, begin, end);
  if (begin >= end) {
    return true;
  }

  size_t sep = end;
  for (size_t i = begin; i < end; i++) {
    if (text[i] == '=') {
      sep = i;
      break;
    }
  }
  if (sep == end) {
    cfg.keys_bad++;
    return false;
  }

  const size_t key_begin = begin;
  const size_t key_end = huntCfgTrimEnd(text, key_begin, sep);
  const size_t val_begin = huntCfgTrimStart(text, sep + 1, end);
  const size_t val_end = huntCfgTrimEnd(text, val_begin, end);
  const bool value_empty = (val_begin >= val_end);

  if (huntCfgEquals(text, key_begin, key_end, "bssid") ||
      huntCfgEquals(text, key_begin, key_end, "mac") ||
      huntCfgEquals(text, key_begin, key_end, "target")) {
    if (value_empty) {
      // An explicitly blank value clears the whole list rather than being an
      // error, which is how a BSSID baked in at build time gets dropped.
      cfg.bssid_seen = true;
      cfg.has_bssid = false;
      cfg.bssid_count = 0;
      cfg.keys_ok++;
      return true;
    }
    char raw[32] = {};
    huntCfgCopy(text, val_begin, val_end, raw, sizeof(raw));
    uint8_t parsed[6] = {};
    if (!huntParseBssid(raw, parsed)) {
      cfg.keys_bad++;
      return false;
    }
    if (cfg.bssid_count >= HUNT_MAX_TARGETS) {
      // Report rather than silently hunting a subset of the list.
      cfg.bssid_dropped++;
      cfg.keys_bad++;
      return false;
    }
    memcpy(cfg.bssid[cfg.bssid_count], parsed, sizeof(parsed));
    cfg.bssid_count++;
    cfg.bssid_seen = true;
    cfg.has_bssid = true;
    cfg.keys_ok++;
    return true;
  }

  if (huntCfgEquals(text, key_begin, key_end, "ssid")) {
    if (value_empty) {
      cfg.has_ssid = false;
      cfg.ssid[0] = '\0';
    } else {
      huntCfgCopy(text, val_begin, val_end, cfg.ssid, sizeof(cfg.ssid));
      cfg.has_ssid = (cfg.ssid[0] != '\0');
    }
    cfg.ssid_seen = true;
    cfg.keys_ok++;
    return true;
  }

  if (huntCfgEquals(text, key_begin, key_end, "band") ||
      huntCfgEquals(text, key_begin, key_end, "plan")) {
    uint8_t band = HUNT_BAND_KEEP_DEFAULT;
    if (value_empty || !huntCfgParseBand(text, val_begin, val_end, band)) {
      cfg.keys_bad++;
      return false;
    }
    cfg.band = band;
    cfg.keys_ok++;
    return true;
  }

  cfg.keys_bad++;
  return false;
}

// Parses a whole file. Returns true if at least one setting was understood, so
// a file of pure noise is reported as a failure rather than silently ignored.
static inline bool huntConfigParseText(const char* text, size_t len, HuntConfig& cfg) {
  huntConfigInit(cfg);
  if (text == nullptr) {
    return false;
  }

  size_t line_begin = 0;
  for (size_t i = 0; i <= len; i++) {
    if (i == len || text[i] == '\n') {
      huntConfigParseLine(text, line_begin, i, cfg);
      line_begin = i + 1;
    }
  }
  return cfg.keys_ok > 0;
}

// Maps the configured band onto a sweep plan, falling back to whatever the
// firmware was built with.
static inline ChannelPlan huntConfigChannelPlan(const HuntConfig& cfg) {
  switch (cfg.band) {
    case HUNT_BAND_24GHZ:
      return channelPlan24gFox();
    case HUNT_BAND_5GHZ:
      return channelPlan5gFox();
    case HUNT_BAND_ALL:
      return channelPlanFull();
    default:
      return channelPlanDefault();
  }
}

static inline const char* huntConfigBandName(uint8_t band) {
  switch (band) {
    case HUNT_BAND_24GHZ: return "2.4GHz";
    case HUNT_BAND_5GHZ: return "5GHz";
    case HUNT_BAND_ALL: return "all";
    default: return "built-in default";
  }
}
