#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "hunt_mode.h"
#include "wifi_result_utils.h"

void setUp() {}
void tearDown() {}

static WiFiResult makeResult(const char* ssid, const uint8_t bssid[6], int8_t rssi) {
  WiFiResult result = {};
  strncpy(result.ssid, ssid, sizeof(result.ssid) - 1);
  memcpy(result.bssid, bssid, sizeof(result.bssid));
  result.rssi = rssi;
  result.channel = 149;
  result.band = WIFI_BAND_5_GHZ;
  return result;
}

static const uint8_t FOX_BSSID[6] = {0xF2, 0x2F, 0xE4, 0x6B, 0xD2, 0x9E};
static const uint8_t OTHER_BSSID[6] = {0xF2, 0x22, 0x28, 0x43, 0x1E, 0x7C};

void test_parse_bssid_accepts_common_separators() {
  uint8_t out[6] = {};

  TEST_ASSERT_TRUE(huntParseBssid("F2:2F:E4:6B:D2:9E", out));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(FOX_BSSID, out, 6);

  memset(out, 0, sizeof(out));
  TEST_ASSERT_TRUE(huntParseBssid("f2-2f-e4-6b-d2-9e", out));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(FOX_BSSID, out, 6);

  memset(out, 0, sizeof(out));
  TEST_ASSERT_TRUE(huntParseBssid("F22FE46BD29E", out));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(FOX_BSSID, out, 6);
}

void test_parse_bssid_rejects_malformed_input() {
  uint8_t out[6] = {};

  // An empty target is the "no BSSID filter" case, not a valid address.
  TEST_ASSERT_FALSE(huntParseBssid("", out));
  // Too few digits.
  TEST_ASSERT_FALSE(huntParseBssid("F2:2F:E4:6B:D2", out));
  // Too many digits.
  TEST_ASSERT_FALSE(huntParseBssid("F2:2F:E4:6B:D2:9E:11", out));
  // Non-hex character; a typo here must not silently match nothing.
  TEST_ASSERT_FALSE(huntParseBssid("F2:2F:E4:6B:D2:9G", out));
  TEST_ASSERT_FALSE(huntParseBssid(nullptr, out));
}

void test_bssid_only_target_matches_regardless_of_ssid() {
  HuntTarget target = {};
  target.bssid_valid = huntParseBssid("F2:2F:E4:6B:D2:9E", target.bssid);
  TEST_ASSERT_TRUE(target.bssid_valid);
  TEST_ASSERT_TRUE(huntTargetIsConfigured(target));

  // A team spoofing the SSID on a different radio must not be chased.
  const WiFiResult spoof = makeResult("Lagos WiFi 5GHz AP Hard Fox", OTHER_BSSID, -50);
  TEST_ASSERT_FALSE(huntTargetMatches(target, spoof));

  // The real fox matches even if its SSID is truncated or unreadable.
  const WiFiResult fox = makeResult("", FOX_BSSID, -80);
  TEST_ASSERT_TRUE(huntTargetMatches(target, fox));
}

void test_ssid_substring_match_is_case_insensitive() {
  HuntTarget target = {};
  strncpy(target.ssid, "hard fox", sizeof(target.ssid) - 1);
  target.ssid_valid = true;

  TEST_ASSERT_TRUE(huntTargetMatches(
      target, makeResult("Lagos WiFi 5GHz AP Hard Fox", FOX_BSSID, -60)));
  TEST_ASSERT_TRUE(huntTargetMatches(
      target, makeResult("HARD FOX", OTHER_BSSID, -60)));
  TEST_ASSERT_FALSE(huntTargetMatches(
      target, makeResult("Lagos WiFi 5GHz AP Easy Fox", FOX_BSSID, -60)));
  // Partial tail must not count as a match.
  TEST_ASSERT_FALSE(huntTargetMatches(target, makeResult("hard fo", FOX_BSSID, -60)));
}

void test_both_filters_are_required_when_both_configured() {
  HuntTarget target = {};
  target.bssid_valid = huntParseBssid("F2:2F:E4:6B:D2:9E", target.bssid);
  strncpy(target.ssid, "Hard Fox", sizeof(target.ssid) - 1);
  target.ssid_valid = true;

  TEST_ASSERT_TRUE(huntTargetMatches(
      target, makeResult("Lagos WiFi 5GHz AP Hard Fox", FOX_BSSID, -60)));
  // Right BSSID, wrong SSID.
  TEST_ASSERT_FALSE(huntTargetMatches(target, makeResult("something else", FOX_BSSID, -60)));
  // Right SSID, wrong BSSID.
  TEST_ASSERT_FALSE(huntTargetMatches(
      target, makeResult("Lagos WiFi 5GHz AP Hard Fox", OTHER_BSSID, -60)));
}

void test_unconfigured_target_never_matches() {
  HuntTarget target = {};
  TEST_ASSERT_FALSE(huntTargetIsConfigured(target));
  TEST_ASSERT_FALSE(huntTargetMatches(target, makeResult("anything", FOX_BSSID, -60)));
}

void test_state_tracks_best_rssi_and_hit_count() {
  HuntTargetState state = {};

  huntStateNoteHit(state, -80, 1000);
  TEST_ASSERT_TRUE(state.seen);
  TEST_ASSERT_EQUAL_INT8(-80, state.best_rssi);
  TEST_ASSERT_EQUAL_INT8(-80, state.last_rssi);
  TEST_ASSERT_EQUAL_UINT32(1, state.hits);
  TEST_ASSERT_EQUAL_UINT32(1000, state.first_seen_ms);

  // Closer to the fox: best improves.
  huntStateNoteHit(state, -55, 2000);
  TEST_ASSERT_EQUAL_INT8(-55, state.best_rssi);
  TEST_ASSERT_EQUAL_INT8(-55, state.last_rssi);

  // Walked past it: last drops but best is retained as the peak.
  huntStateNoteHit(state, -70, 3000);
  TEST_ASSERT_EQUAL_INT8(-55, state.best_rssi);
  TEST_ASSERT_EQUAL_INT8(-70, state.last_rssi);
  TEST_ASSERT_EQUAL_UINT32(3, state.hits);
  TEST_ASSERT_EQUAL_UINT32(3000, state.last_seen_ms);
  TEST_ASSERT_EQUAL_UINT32(1000, state.first_seen_ms);
}

void test_bar_is_clamped_and_scaled() {
  char bar[HUNT_BAR_WIDTH + 1] = {};

  huntFormatBar(-120, bar, sizeof(bar));
  TEST_ASSERT_EQUAL_UINT32(HUNT_BAR_WIDTH, strlen(bar));
  TEST_ASSERT_EQUAL_STRING("--------------------", bar);

  huntFormatBar(-10, bar, sizeof(bar));
  TEST_ASSERT_EQUAL_STRING("####################", bar);

  // Midpoint of the -95..-30 window should be about half filled.
  huntFormatBar(-63, bar, sizeof(bar));
  size_t filled = 0;
  for (size_t i = 0; i < strlen(bar); i++) {
    if (bar[i] == '#') filled++;
  }
  TEST_ASSERT_TRUE(filled >= 8 && filled <= 12);
}

void test_elapsed_formatting_avoids_floating_point() {
  char out[16] = {};

  huntFormatElapsed(0, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("0.0s", out);

  huntFormatElapsed(2150, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("2.1s", out);

  huntFormatElapsed(45000, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("45.0s", out);
}

// The LED blinks faster the closer you are, so the period must fall as RSSI
// rises. A non-monotonic mapping would make the readout actively misleading.
void test_led_period_shortens_as_signal_rises() {
  TEST_ASSERT_EQUAL_UINT32(HUNT_LED_PERIOD_FAR_MS, huntLedPeriodMs(HUNT_BAR_RSSI_MIN));
  TEST_ASSERT_EQUAL_UINT32(HUNT_LED_PERIOD_NEAR_MS, huntLedPeriodMs(HUNT_BAR_RSSI_MAX));

  // Clamped outside the readout range rather than running away.
  TEST_ASSERT_EQUAL_UINT32(HUNT_LED_PERIOD_FAR_MS, huntLedPeriodMs(-120));
  TEST_ASSERT_EQUAL_UINT32(HUNT_LED_PERIOD_NEAR_MS, huntLedPeriodMs(-5));

  for (int rssi = -120; rssi < 0; rssi++) {
    TEST_ASSERT_TRUE(huntLedPeriodMs((int8_t)(rssi + 1)) <= huntLedPeriodMs((int8_t)rssi));
  }
}

void test_led_brightness_rises_with_signal() {
  TEST_ASSERT_EQUAL_UINT8(HUNT_LED_BRIGHT_MIN, huntLedBrightness(HUNT_BAR_RSSI_MIN));
  TEST_ASSERT_EQUAL_UINT8(HUNT_LED_BRIGHT_MAX, huntLedBrightness(HUNT_BAR_RSSI_MAX));
  TEST_ASSERT_EQUAL_UINT8(HUNT_LED_BRIGHT_MIN, huntLedBrightness(-120));
  TEST_ASSERT_EQUAL_UINT8(HUNT_LED_BRIGHT_MAX, huntLedBrightness(-5));

  for (int rssi = -120; rssi < 0; rssi++) {
    TEST_ASSERT_TRUE(huntLedBrightness((int8_t)(rssi + 1)) >= huntLedBrightness((int8_t)rssi));
  }
}

void test_led_blink_duty_cycle() {
  TEST_ASSERT_TRUE(huntLedIsOn(0, 1000, 350));
  TEST_ASSERT_TRUE(huntLedIsOn(349, 1000, 350));
  TEST_ASSERT_FALSE(huntLedIsOn(350, 1000, 350));
  TEST_ASSERT_FALSE(huntLedIsOn(999, 1000, 350));
  // Wraps with the free-running clock.
  TEST_ASSERT_TRUE(huntLedIsOn(1000, 1000, 350));
  TEST_ASSERT_TRUE(huntLedIsOn(123456000, 1000, 350));
  // A zero period must not divide by zero.
  TEST_ASSERT_FALSE(huntLedIsOn(500, 0, 350));
}

// The bar and the LED read from the same scaling helper, so a strong signal can
// never show a full bar and a slow blink at the same time.
void test_bar_and_led_agree_on_direction() {
  char weak[HUNT_BAR_WIDTH + 1] = {};
  char strong[HUNT_BAR_WIDTH + 1] = {};
  huntFormatBar(-90, weak, sizeof(weak));
  huntFormatBar(-40, strong, sizeof(strong));

  size_t weak_filled = 0, strong_filled = 0;
  for (size_t i = 0; i < HUNT_BAR_WIDTH; i++) {
    if (weak[i] == '#') weak_filled++;
    if (strong[i] == '#') strong_filled++;
  }

  TEST_ASSERT_TRUE(strong_filled > weak_filled);
  TEST_ASSERT_TRUE(huntLedPeriodMs(-40) < huntLedPeriodMs(-90));
  TEST_ASSERT_TRUE(huntLedBrightness(-40) > huntLedBrightness(-90));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_led_period_shortens_as_signal_rises);
  RUN_TEST(test_led_brightness_rises_with_signal);
  RUN_TEST(test_led_blink_duty_cycle);
  RUN_TEST(test_bar_and_led_agree_on_direction);
  RUN_TEST(test_parse_bssid_accepts_common_separators);
  RUN_TEST(test_parse_bssid_rejects_malformed_input);
  RUN_TEST(test_bssid_only_target_matches_regardless_of_ssid);
  RUN_TEST(test_ssid_substring_match_is_case_insensitive);
  RUN_TEST(test_both_filters_are_required_when_both_configured);
  RUN_TEST(test_unconfigured_target_never_matches);
  RUN_TEST(test_state_tracks_best_rssi_and_hit_count);
  RUN_TEST(test_bar_is_clamped_and_scaled);
  RUN_TEST(test_elapsed_formatting_avoids_floating_point);
  return UNITY_END();
}
