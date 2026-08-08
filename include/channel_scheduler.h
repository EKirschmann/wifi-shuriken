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

// A sweep plan: the channel list for each band, plus the sequence of bands the
// dispatcher walks. A band that appears in no phase is never scanned and its
// list is ignored.
struct ChannelPlan {
  const uint8_t* list_24g;
  size_t count_24g;
  const uint8_t* list_5g;
  size_t count_5g;
  const uint8_t* phase_bands;
  size_t phase_count;
};

static constexpr uint8_t CHANNEL_SCHEDULE_PHASE_24G = 0;
static constexpr uint8_t CHANNEL_SCHEDULE_PHASE_5G_A = 1;
static constexpr uint8_t CHANNEL_SCHEDULE_PHASE_5G_B = 2;

// Keep adjacent 2.4GHz assignments at least five channel numbers apart so
// sequential 20MHz scans do not land on overlapping 2.4GHz channels.
static constexpr uint8_t CHANNEL_LIST_24G_FULL[] = {
  1, 6, 11, 2, 7, 12, 3, 8, 13, 4, 9, 14, 5, 10
};

static constexpr uint8_t CHANNEL_LIST_5G_FULL[] = {
  36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124,
  128, 132, 136, 140, 144, 149, 153, 157, 161, 165
};

// The 2.4GHz foxes pick a random channel in 1-11 on each wake, so 12-14 are
// dead weight. Same five-channel spacing rule as the full list.
static constexpr uint8_t CHANNEL_LIST_24G_FOX[] = {
  1, 6, 11, 5, 10, 4, 9, 3, 8, 2, 7
};

// The 5GHz foxes pick from this exact non-DFS set. Dropping DFS channels also
// drops the 210ms passive dwell each of them would cost.
static constexpr uint8_t CHANNEL_LIST_5G_FOX[] = {
  36, 40, 44, 48, 149, 153, 157, 161, 165
};

// Interleaving one 2.4GHz scan with two 5GHz scans keeps the larger 5GHz list
// wrapping in reasonable time during a full coverage sweep.
static constexpr uint8_t CHANNEL_PHASES_MIXED[] = {
  WIFI_BAND_24_GHZ, WIFI_BAND_5_GHZ, WIFI_BAND_5_GHZ
};
static constexpr uint8_t CHANNEL_PHASES_24G_ONLY[] = { WIFI_BAND_24_GHZ };
static constexpr uint8_t CHANNEL_PHASES_5G_ONLY[] = { WIFI_BAND_5_GHZ };

// Full dual-band coverage sweep. This is what wardriving builds want.
static inline ChannelPlan channelPlanFull() {
  ChannelPlan plan = {};
  plan.list_24g = CHANNEL_LIST_24G_FULL;
  plan.count_24g = sizeof(CHANNEL_LIST_24G_FULL) / sizeof(CHANNEL_LIST_24G_FULL[0]);
  plan.list_5g = CHANNEL_LIST_5G_FULL;
  plan.count_5g = sizeof(CHANNEL_LIST_5G_FULL) / sizeof(CHANNEL_LIST_5G_FULL[0]);
  plan.phase_bands = CHANNEL_PHASES_MIXED;
  plan.phase_count = sizeof(CHANNEL_PHASES_MIXED) / sizeof(CHANNEL_PHASES_MIXED[0]);
  return plan;
}

// 2.4GHz fox sweep: channels 1-11 only, no 5GHz phases.
static inline ChannelPlan channelPlan24gFox() {
  ChannelPlan plan = channelPlanFull();
  plan.list_24g = CHANNEL_LIST_24G_FOX;
  plan.count_24g = sizeof(CHANNEL_LIST_24G_FOX) / sizeof(CHANNEL_LIST_24G_FOX[0]);
  plan.phase_bands = CHANNEL_PHASES_24G_ONLY;
  plan.phase_count = sizeof(CHANNEL_PHASES_24G_ONLY) / sizeof(CHANNEL_PHASES_24G_ONLY[0]);
  return plan;
}

// 5GHz fox sweep: the nine non-DFS fox channels only, no 2.4GHz phases.
static inline ChannelPlan channelPlan5gFox() {
  ChannelPlan plan = channelPlanFull();
  plan.list_5g = CHANNEL_LIST_5G_FOX;
  plan.count_5g = sizeof(CHANNEL_LIST_5G_FOX) / sizeof(CHANNEL_LIST_5G_FOX[0]);
  plan.phase_bands = CHANNEL_PHASES_5G_ONLY;
  plan.phase_count = sizeof(CHANNEL_PHASES_5G_ONLY) / sizeof(CHANNEL_PHASES_5G_ONLY[0]);
  return plan;
}

// CHANNEL_PLAN picks the plan a build starts with. Hunt builds can still be
// re-pointed at runtime from the SD card, so this is a default rather than a
// hard restriction.
//   0 = full dual-band coverage sweep (wardriving default)
//   1 = 2.4GHz fox sweep
//   2 = 5GHz fox sweep
#ifndef CHANNEL_PLAN
#define CHANNEL_PLAN 0
#endif

#if CHANNEL_PLAN != 0 && CHANNEL_PLAN != 1 && CHANNEL_PLAN != 2
#error "CHANNEL_PLAN must be 0 (full), 1 (2.4GHz fox), or 2 (5GHz fox)"
#endif

static inline ChannelPlan channelPlanDefault() {
#if CHANNEL_PLAN == 1
  return channelPlan24gFox();
#elif CHANNEL_PLAN == 2
  return channelPlan5gFox();
#else
  return channelPlanFull();
#endif
}

static inline bool channelPlanUsesBand(const ChannelPlan& plan, uint8_t band) {
  for (size_t i = 0; i < plan.phase_count; i++) {
    if (plan.phase_bands[i] == band) {
      return true;
    }
  }
  return false;
}

static inline ChannelScheduleEntry channelScheduleCurrent(const ChannelPlan& plan,
                                                          const ChannelScheduleState& state) {
  ChannelScheduleEntry entry = {};
  if (plan.phase_count == 0) {
    return entry;
  }

  if (plan.phase_bands[state.dispatch_phase] == WIFI_BAND_24_GHZ) {
    entry.band = WIFI_BAND_24_GHZ;
    entry.channel = plan.list_24g[state.next_24g_index];
    return entry;
  }

  entry.band = WIFI_BAND_5_GHZ;
  entry.channel = plan.list_5g[state.next_5g_index];
  return entry;
}

// Advances to the next dispatch slot. Returns true when a full coverage cycle
// has completed, meaning every band the plan actually uses has wrapped.
static inline bool channelScheduleAdvance(const ChannelPlan& plan,
                                          ChannelScheduleState& state) {
  if (plan.phase_count == 0) {
    return true;
  }

  if (plan.phase_bands[state.dispatch_phase] == WIFI_BAND_24_GHZ) {
    state.next_24g_index++;
    if (state.next_24g_index >= plan.count_24g) {
      state.next_24g_index = 0;
      state.wrapped_24g = true;
    }
  } else {
    state.next_5g_index++;
    if (state.next_5g_index >= plan.count_5g) {
      state.next_5g_index = 0;
      state.wrapped_5g = true;
    }
  }

  state.dispatch_phase = (uint8_t)((state.dispatch_phase + 1u) % plan.phase_count);

  // A band that no phase draws from never wraps, so a cycle must not wait on it.
  const bool covered_24g = state.wrapped_24g || !channelPlanUsesBand(plan, WIFI_BAND_24_GHZ);
  const bool covered_5g = state.wrapped_5g || !channelPlanUsesBand(plan, WIFI_BAND_5_GHZ);
  if (covered_24g && covered_5g) {
    state.wrapped_24g = false;
    state.wrapped_5g = false;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Convenience wrappers bound to the build's default plan.
// ---------------------------------------------------------------------------

static constexpr uint8_t CHANNEL_SCHEDULE_PHASE_COUNT =
#if CHANNEL_PLAN == 1
  (uint8_t)(sizeof(CHANNEL_PHASES_24G_ONLY) / sizeof(CHANNEL_PHASES_24G_ONLY[0]));
#elif CHANNEL_PLAN == 2
  (uint8_t)(sizeof(CHANNEL_PHASES_5G_ONLY) / sizeof(CHANNEL_PHASES_5G_ONLY[0]));
#else
  (uint8_t)(sizeof(CHANNEL_PHASES_MIXED) / sizeof(CHANNEL_PHASES_MIXED[0]));
#endif

static inline size_t channelSchedule24gCount() {
  return channelPlanDefault().count_24g;
}

static inline size_t channelSchedule5gCount() {
  return channelPlanDefault().count_5g;
}

static inline uint8_t channelSchedule24gAt(size_t index) {
  return channelPlanDefault().list_24g[index];
}

static inline uint8_t channelSchedule5gAt(size_t index) {
  return channelPlanDefault().list_5g[index];
}

static inline bool channelSchedulePlanUsesBand(uint8_t band) {
  return channelPlanUsesBand(channelPlanDefault(), band);
}

static inline bool channelScheduleCurrentUses24g(const ChannelScheduleState& state) {
  const ChannelPlan plan = channelPlanDefault();
  return plan.phase_bands[state.dispatch_phase] == WIFI_BAND_24_GHZ;
}

static inline ChannelScheduleEntry channelScheduleCurrent(const ChannelScheduleState& state) {
  return channelScheduleCurrent(channelPlanDefault(), state);
}

static inline bool channelScheduleAdvance(ChannelScheduleState& state) {
  return channelScheduleAdvance(channelPlanDefault(), state);
}
