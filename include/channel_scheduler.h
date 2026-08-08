#pragma once

#include <stddef.h>
#include <stdint.h>

#include "wifi_result_utils.h"

struct ChannelScheduleEntry {
  uint8_t band;
  uint8_t channel;
};

struct ChannelScheduleState {
  size_t next_24g_index;
  size_t next_5g_index;
  uint8_t dispatch_phase;
  bool wrapped_24g;
  bool wrapped_5g;
};

static constexpr uint8_t CHANNEL_SCHEDULE_PHASE_24G = 0;
static constexpr uint8_t CHANNEL_SCHEDULE_PHASE_5G_A = 1;
static constexpr uint8_t CHANNEL_SCHEDULE_PHASE_5G_B = 2;

// Both channel lists and the band dispatch plan are overridable at build time.
// A wardriving build wants full coverage; a fox-hunt build wants the sweep
// narrowed to the channels one target can appear on, because a shorter sweep
// means more RSSI samples per second while walking.
//
// CHANNEL_PLAN selects a preset. Passing one integer build flag avoids having
// to smuggle brace-enclosed lists through the compiler command line; the
// underlying list macros stay available for anything the presets do not cover.
//   0 = full dual-band coverage sweep (wardriving default)
//   1 = 2.4GHz only, channels 1-11
//   2 = 5GHz only, non-DFS channels
#ifndef CHANNEL_PLAN
#define CHANNEL_PLAN 0
#endif

#if CHANNEL_PLAN == 1
// The 2.4GHz foxes pick a random channel in 1-11 on each wake, so 12-14 are
// dead weight. Ordering keeps adjacent scans at least five channels apart.
#define CHANNEL_SCHEDULE_24G_LIST \
  { 1, 6, 11, 5, 10, 4, 9, 3, 8, 2, 7 }
#define CHANNEL_SCHEDULE_PHASE_BANDS \
  { WIFI_BAND_24_GHZ }
#elif CHANNEL_PLAN == 2
// The 5GHz foxes pick from this exact non-DFS set. Dropping DFS channels also
// drops the 210ms passive dwell they would each cost.
#define CHANNEL_SCHEDULE_5G_LIST \
  { 36, 40, 44, 48, 149, 153, 157, 161, 165 }
#define CHANNEL_SCHEDULE_PHASE_BANDS \
  { WIFI_BAND_5_GHZ }
#elif CHANNEL_PLAN != 0
#error "CHANNEL_PLAN must be 0 (full), 1 (2.4GHz only), or 2 (5GHz only)"
#endif

// Keep adjacent 2.4GHz assignments at least five channel numbers apart so
// sequential 20MHz scans do not land on overlapping 2.4GHz channels.
#ifndef CHANNEL_SCHEDULE_24G_LIST
#define CHANNEL_SCHEDULE_24G_LIST \
  { 1, 6, 11, 2, 7, 12, 3, 8, 13, 4, 9, 14, 5, 10 }
#endif

#ifndef CHANNEL_SCHEDULE_5G_LIST
#define CHANNEL_SCHEDULE_5G_LIST                                          \
  { 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124,    \
    128, 132, 136, 140, 144, 149, 153, 157, 161, 165 }
#endif

// Which band each dispatch slot draws from. The default interleaves one 2.4GHz
// scan with two 5GHz scans so the larger 5GHz list still wraps in reasonable
// time. A single-band plan (for example {WIFI_BAND_5_GHZ}) skips the other band
// entirely and its wrap flag is treated as already satisfied.
#ifndef CHANNEL_SCHEDULE_PHASE_BANDS
#define CHANNEL_SCHEDULE_PHASE_BANDS \
  { WIFI_BAND_24_GHZ, WIFI_BAND_5_GHZ, WIFI_BAND_5_GHZ }
#endif

static constexpr uint8_t CHANNEL_SCHEDULE_24G[] = CHANNEL_SCHEDULE_24G_LIST;

static constexpr uint8_t CHANNEL_SCHEDULE_5G[] = CHANNEL_SCHEDULE_5G_LIST;

static constexpr uint8_t CHANNEL_SCHEDULE_PHASE_BAND[] = CHANNEL_SCHEDULE_PHASE_BANDS;

static constexpr uint8_t CHANNEL_SCHEDULE_PHASE_COUNT =
  (uint8_t)(sizeof(CHANNEL_SCHEDULE_PHASE_BAND) / sizeof(CHANNEL_SCHEDULE_PHASE_BAND[0]));

static_assert(CHANNEL_SCHEDULE_PHASE_COUNT >= 1,
              "CHANNEL_SCHEDULE_PHASE_BANDS must contain at least one phase");

static inline size_t channelSchedule24gCount() {
  return sizeof(CHANNEL_SCHEDULE_24G) / sizeof(CHANNEL_SCHEDULE_24G[0]);
}

static inline size_t channelSchedule5gCount() {
  return sizeof(CHANNEL_SCHEDULE_5G) / sizeof(CHANNEL_SCHEDULE_5G[0]);
}

static inline uint8_t channelSchedule24gAt(size_t index) {
  return CHANNEL_SCHEDULE_24G[index];
}

static inline uint8_t channelSchedule5gAt(size_t index) {
  return CHANNEL_SCHEDULE_5G[index];
}

static inline bool channelScheduleCurrentUses24g(const ChannelScheduleState& state) {
  return CHANNEL_SCHEDULE_PHASE_BAND[state.dispatch_phase] == WIFI_BAND_24_GHZ;
}

// A band that no phase draws from never wraps, so a coverage cycle must not
// wait on it.
static inline bool channelSchedulePlanUsesBand(uint8_t band) {
  for (uint8_t i = 0; i < CHANNEL_SCHEDULE_PHASE_COUNT; i++) {
    if (CHANNEL_SCHEDULE_PHASE_BAND[i] == band) {
      return true;
    }
  }
  return false;
}

static inline ChannelScheduleEntry channelScheduleCurrent(const ChannelScheduleState& state) {
  ChannelScheduleEntry entry = {};
  if (channelScheduleCurrentUses24g(state)) {
    entry.band = WIFI_BAND_24_GHZ;
    entry.channel = channelSchedule24gAt(state.next_24g_index);
    return entry;
  }

  entry.band = WIFI_BAND_5_GHZ;
  entry.channel = channelSchedule5gAt(state.next_5g_index);
  return entry;
}

static inline bool channelScheduleAdvance(ChannelScheduleState& state) {
  if (channelScheduleCurrentUses24g(state)) {
    state.next_24g_index++;
    if (state.next_24g_index >= channelSchedule24gCount()) {
      state.next_24g_index = 0;
      state.wrapped_24g = true;
    }
  } else {
    state.next_5g_index++;
    if (state.next_5g_index >= channelSchedule5gCount()) {
      state.next_5g_index = 0;
      state.wrapped_5g = true;
    }
  }

  state.dispatch_phase = (uint8_t)((state.dispatch_phase + 1u) % CHANNEL_SCHEDULE_PHASE_COUNT);

  const bool covered_24g = state.wrapped_24g || !channelSchedulePlanUsesBand(WIFI_BAND_24_GHZ);
  const bool covered_5g = state.wrapped_5g || !channelSchedulePlanUsesBand(WIFI_BAND_5_GHZ);
  if (covered_24g && covered_5g) {
    state.wrapped_24g = false;
    state.wrapped_5g = false;
    return true;
  }
  return false;
}
