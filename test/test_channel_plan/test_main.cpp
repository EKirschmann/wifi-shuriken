#include <stddef.h>
#include <stdint.h>

#include <unity.h>

#include "channel_scheduler.h"

// Plan-aware companion to test_channel_scheduler. That suite pins the default
// wardriving sweep; this one asserts the properties every preset must hold, and
// the per-preset shape of the narrowed fox-hunt sweeps.

void setUp() {}
void tearDown() {}

struct CycleSummary {
  uint32_t dispatches;
  bool completed;
  uint8_t hits_24g[16];
  uint8_t hits_5g[200];
};

static CycleSummary runOneCycle() {
  CycleSummary summary = {};
  ChannelScheduleState state = {};

  while (!summary.completed && summary.dispatches < 256) {
    const ChannelScheduleEntry entry = channelScheduleCurrent(state);
    if (entry.band == WIFI_BAND_24_GHZ) {
      TEST_ASSERT_TRUE(entry.channel < 16);
      summary.hits_24g[entry.channel]++;
    } else {
      TEST_ASSERT_EQUAL_UINT8(WIFI_BAND_5_GHZ, entry.band);
      TEST_ASSERT_TRUE(entry.channel < 200);
      summary.hits_5g[entry.channel]++;
    }
    summary.completed = channelScheduleAdvance(state);
    summary.dispatches++;
  }

  return summary;
}

// A cycle that never completes would stall sweep timing and, in hunt mode,
// leave channels permanently unvisited.
void test_cycle_completes_for_configured_plan() {
  const CycleSummary summary = runOneCycle();
  TEST_ASSERT_TRUE(summary.completed);
  TEST_ASSERT_TRUE(summary.dispatches > 0);
}

// Every channel in a band the plan actually dispatches must be visited.
void test_cycle_covers_every_scheduled_channel() {
  const CycleSummary summary = runOneCycle();

  if (channelSchedulePlanUsesBand(WIFI_BAND_24_GHZ)) {
    for (size_t i = 0; i < channelSchedule24gCount(); i++) {
      TEST_ASSERT_TRUE(summary.hits_24g[channelSchedule24gAt(i)] >= 1);
    }
  }
  if (channelSchedulePlanUsesBand(WIFI_BAND_5_GHZ)) {
    for (size_t i = 0; i < channelSchedule5gCount(); i++) {
      TEST_ASSERT_TRUE(summary.hits_5g[channelSchedule5gAt(i)] >= 1);
    }
  }
}

// A band no phase draws from must not be scanned at all, and must not hold the
// coverage cycle open waiting for a wrap that can never happen.
void test_unscheduled_bands_are_never_dispatched() {
  const CycleSummary summary = runOneCycle();

  if (!channelSchedulePlanUsesBand(WIFI_BAND_24_GHZ)) {
    for (size_t i = 0; i < sizeof(summary.hits_24g); i++) {
      TEST_ASSERT_EQUAL_UINT8(0, summary.hits_24g[i]);
    }
  }
  if (!channelSchedulePlanUsesBand(WIFI_BAND_5_GHZ)) {
    for (size_t i = 0; i < sizeof(summary.hits_5g); i++) {
      TEST_ASSERT_EQUAL_UINT8(0, summary.hits_5g[i]);
    }
  }
}

// Cycles must be repeatable: the second cycle has to complete too, or the
// sweep would run once and then stall.
void test_consecutive_cycles_complete() {
  ChannelScheduleState state = {};
  for (int cycle = 0; cycle < 3; cycle++) {
    bool completed = false;
    uint32_t dispatches = 0;
    while (!completed && dispatches < 256) {
      completed = channelScheduleAdvance(state);
      dispatches++;
    }
    TEST_ASSERT_TRUE(completed);
  }
}

#if CHANNEL_PLAN == 1
void test_plan_24g_shape() {
  TEST_ASSERT_EQUAL_UINT8(1, CHANNEL_SCHEDULE_PHASE_COUNT);
  TEST_ASSERT_TRUE(channelSchedulePlanUsesBand(WIFI_BAND_24_GHZ));
  TEST_ASSERT_FALSE(channelSchedulePlanUsesBand(WIFI_BAND_5_GHZ));

  // Channels 1-11 exactly, since the 2.4GHz foxes never select 12-14.
  TEST_ASSERT_EQUAL_UINT32(11, channelSchedule24gCount());
  uint8_t seen[16] = {};
  for (size_t i = 0; i < channelSchedule24gCount(); i++) {
    const uint8_t channel = channelSchedule24gAt(i);
    TEST_ASSERT_TRUE(channel >= 1 && channel <= 11);
    seen[channel]++;
  }
  for (uint8_t channel = 1; channel <= 11; channel++) {
    TEST_ASSERT_EQUAL_UINT8(1, seen[channel]);
  }

  // Adjacent scans stay at least five channels apart to avoid overlap.
  for (size_t i = 0; (i + 1u) < channelSchedule24gCount(); i++) {
    const int delta = (int)channelSchedule24gAt(i + 1u) - (int)channelSchedule24gAt(i);
    TEST_ASSERT_TRUE((delta >= 5) || (delta <= -5));
  }

  const CycleSummary summary = runOneCycle();
  TEST_ASSERT_EQUAL_UINT32(11, summary.dispatches);
}
#endif

#if CHANNEL_PLAN == 2
void test_plan_5g_shape() {
  TEST_ASSERT_EQUAL_UINT8(1, CHANNEL_SCHEDULE_PHASE_COUNT);
  TEST_ASSERT_TRUE(channelSchedulePlanUsesBand(WIFI_BAND_5_GHZ));
  TEST_ASSERT_FALSE(channelSchedulePlanUsesBand(WIFI_BAND_24_GHZ));

  // Exactly the nine non-DFS channels the 5GHz foxes choose from. Including a
  // DFS channel here would cost a 210ms passive dwell for nothing.
  const uint8_t expected[] = {36, 40, 44, 48, 149, 153, 157, 161, 165};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expected), channelSchedule5gCount());
  for (size_t i = 0; i < channelSchedule5gCount(); i++) {
    TEST_ASSERT_EQUAL_UINT8(expected[i], channelSchedule5gAt(i));
    const uint8_t channel = channelSchedule5gAt(i);
    TEST_ASSERT_FALSE(channel >= 52 && channel <= 144);
  }

  const CycleSummary summary = runOneCycle();
  TEST_ASSERT_EQUAL_UINT32(9, summary.dispatches);
}
#endif

#if CHANNEL_PLAN == 0
void test_plan_default_is_unchanged() {
  // The default build must remain the full dual-band wardriving sweep.
  TEST_ASSERT_EQUAL_UINT8(3, CHANNEL_SCHEDULE_PHASE_COUNT);
  TEST_ASSERT_TRUE(channelSchedulePlanUsesBand(WIFI_BAND_24_GHZ));
  TEST_ASSERT_TRUE(channelSchedulePlanUsesBand(WIFI_BAND_5_GHZ));
  TEST_ASSERT_EQUAL_UINT32(14, channelSchedule24gCount());
  TEST_ASSERT_EQUAL_UINT32(25, channelSchedule5gCount());

  const CycleSummary summary = runOneCycle();
  TEST_ASSERT_EQUAL_UINT32(40, summary.dispatches);
}
#endif

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_cycle_completes_for_configured_plan);
  RUN_TEST(test_cycle_covers_every_scheduled_channel);
  RUN_TEST(test_unscheduled_bands_are_never_dispatched);
  RUN_TEST(test_consecutive_cycles_complete);
#if CHANNEL_PLAN == 0
  RUN_TEST(test_plan_default_is_unchanged);
#endif
#if CHANNEL_PLAN == 1
  RUN_TEST(test_plan_24g_shape);
#endif
#if CHANNEL_PLAN == 2
  RUN_TEST(test_plan_5g_shape);
#endif
  return UNITY_END();
}
