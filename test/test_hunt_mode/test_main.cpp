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
  uint8_t parsed[6] = {};
  TEST_ASSERT_TRUE(huntParseBssid("F2:2F:E4:6B:D2:9E", parsed));
  TEST_ASSERT_TRUE(huntTargetAddBssid(target, parsed));
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
  uint8_t parsed[6] = {};
  TEST_ASSERT_TRUE(huntParseBssid("F2:2F:E4:6B:D2:9E", parsed));
  TEST_ASSERT_TRUE(huntTargetAddBssid(target, parsed));
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


void test_multi_target_match_reports_the_right_index() {
  HuntTarget target = {};
  uint8_t a[6] = {};
  uint8_t b[6] = {};
  TEST_ASSERT_TRUE(huntParseBssid("F2:EE:CB:62:E8:77", a));
  TEST_ASSERT_TRUE(huntParseBssid("F2:2F:E4:6B:D2:9E", b));
  TEST_ASSERT_TRUE(huntTargetAddBssid(target, a));
  TEST_ASSERT_TRUE(huntTargetAddBssid(target, b));
  TEST_ASSERT_EQUAL_UINT8(2, target.bssid_count);

  TEST_ASSERT_EQUAL_INT(0, huntTargetMatchIndex(target, makeResult("", a, -60)));
  TEST_ASSERT_EQUAL_INT(1, huntTargetMatchIndex(target, makeResult("", b, -60)));
  TEST_ASSERT_EQUAL_INT(-1, huntTargetMatchIndex(target, makeResult("", OTHER_BSSID, -60)));
}

void test_target_list_is_capped_rather_than_overflowing() {
  HuntTarget target = {};
  uint8_t mac[6] = {0xF2, 0, 0, 0, 0, 0};
  for (int i = 0; i < HUNT_MAX_TARGETS; i++) {
    mac[5] = (uint8_t)i;
    TEST_ASSERT_TRUE(huntTargetAddBssid(target, mac));
  }
  mac[5] = 0xFF;
  TEST_ASSERT_FALSE(huntTargetAddBssid(target, mac));
  TEST_ASSERT_EQUAL_UINT8(HUNT_MAX_TARGETS, target.bssid_count);
  TEST_ASSERT_EQUAL_INT(-1, huntTargetMatchIndex(target, makeResult("", mac, -60)));
}


void test_label_falls_back_to_index_when_unset() {
  HuntTarget target = {};
  uint8_t a[6] = {};
  uint8_t b[6] = {};
  TEST_ASSERT_TRUE(huntParseBssid("F2:EE:CB:62:E8:77", a));
  TEST_ASSERT_TRUE(huntParseBssid("F2:2F:E4:6B:D2:9E", b));
  TEST_ASSERT_TRUE(huntTargetAddBssid(target, a, "AP Easy 1"));
  TEST_ASSERT_TRUE(huntTargetAddBssid(target, b));

  char out[HUNT_LABEL_MAX] = {};
  huntTargetFormatLabel(target, 0, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("AP Easy 1", out);
  huntTargetFormatLabel(target, 1, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("#1", out);
  // Out of range must not read past the array.
  huntTargetFormatLabel(target, 9, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("#9", out);
}


// "F2:*" is how you catch a fox whose MAC you were never told: every RFHS fox
// uses a locally-administered address.
void test_wildcard_prefix_patterns() {
  uint8_t out[6] = {};

  TEST_ASSERT_EQUAL_UINT8(1, huntParseBssidPattern("F2:*", out));
  TEST_ASSERT_EQUAL_UINT8(0xF2, out[0]);
  TEST_ASSERT_EQUAL_UINT8(2, huntParseBssidPattern("F2:2F:*", out));
  TEST_ASSERT_EQUAL_UINT8(6, huntParseBssidPattern("F2:2F:E4:6B:D2:9E", out));

  // Half an octet, hex after the wildcard, and short-without-'*' are all typos.
  TEST_ASSERT_EQUAL_UINT8(0, huntParseBssidPattern("F2:2:*", out));
  TEST_ASSERT_EQUAL_UINT8(0, huntParseBssidPattern("F2:*:E4", out));
  TEST_ASSERT_EQUAL_UINT8(0, huntParseBssidPattern("F2:2F", out));
  TEST_ASSERT_EQUAL_UINT8(0, huntParseBssidPattern("*", out));
}

void test_wildcard_target_matches_any_fox() {
  HuntTarget target = {};
  uint8_t pat[6] = {};
  const uint8_t len = huntParseBssidPattern("F2:*", pat);
  TEST_ASSERT_EQUAL_UINT8(1, len);
  TEST_ASSERT_TRUE(huntTargetAddBssid(target, pat, "any fox", len));

  // Both real foxes match the one prefix entry.
  TEST_ASSERT_EQUAL_INT(0, huntTargetMatchIndex(target, makeResult("", FOX_BSSID, -60)));
  TEST_ASSERT_EQUAL_INT(0, huntTargetMatchIndex(target, makeResult("", OTHER_BSSID, -60)));

  // A normal vendor MAC does not.
  const uint8_t vendor[6] = {0x3C, 0x71, 0xBF, 0x01, 0x02, 0x03};
  TEST_ASSERT_EQUAL_INT(-1, huntTargetMatchIndex(target, makeResult("", vendor, -60)));

  char shown[20] = {};
  huntTargetFormatPattern(target, 0, shown, sizeof(shown));
  TEST_ASSERT_EQUAL_STRING("F2:*", shown);
}

// An exact entry listed after a prefix must still win, so a named fox reports
// its own label rather than the catch-all.
void test_exact_entries_take_priority_over_a_later_prefix() {
  HuntTarget target = {};
  uint8_t exact[6] = {};
  uint8_t pat[6] = {};
  TEST_ASSERT_TRUE(huntParseBssid("F2:2F:E4:6B:D2:9E", exact));
  TEST_ASSERT_TRUE(huntTargetAddBssid(target, exact, "5G HARD 1", 6));
  TEST_ASSERT_EQUAL_UINT8(1, huntParseBssidPattern("F2:*", pat));
  TEST_ASSERT_TRUE(huntTargetAddBssid(target, pat, "unknown fox", 1));

  TEST_ASSERT_EQUAL_INT(0, huntTargetMatchIndex(target, makeResult("", FOX_BSSID, -60)));
  TEST_ASSERT_EQUAL_INT(1, huntTargetMatchIndex(target, makeResult("", OTHER_BSSID, -60)));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_led_period_shortens_as_signal_rises);
  RUN_TEST(test_led_brightness_rises_with_signal);
  RUN_TEST(test_led_blink_duty_cycle);
  RUN_TEST(test_bar_and_led_agree_on_direction);
  RUN_TEST(test_multi_target_match_reports_the_right_index);
  RUN_TEST(test_target_list_is_capped_rather_than_overflowing);
  RUN_TEST(test_label_falls_back_to_index_when_unset);
  RUN_TEST(test_wildcard_prefix_patterns);
  RUN_TEST(test_wildcard_target_matches_any_fox);
  RUN_TEST(test_exact_entries_take_priority_over_a_later_prefix);
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
