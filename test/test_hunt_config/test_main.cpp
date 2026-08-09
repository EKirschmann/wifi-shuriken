#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unity.h>

#include "hunt_config.h"

void setUp() {}
void tearDown() {}

static const uint8_t FOX_5G[6] = {0xF2, 0x2F, 0xE4, 0x6B, 0xD2, 0x9E};

static bool parse(const char* text, HuntConfig& cfg) {
  return huntConfigParseText(text, strlen(text), cfg);
}

void test_parses_a_typical_config_file() {
  HuntConfig cfg = {};
  const char* text =
      "# WiFi-Shuriken hunt target\n"
      "bssid = F2:2F:E4:6B:D2:9E\n"
      "band  = 5\n";

  TEST_ASSERT_TRUE(parse(text, cfg));
  TEST_ASSERT_TRUE(cfg.has_bssid);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(FOX_5G, cfg.bssid[0], 6);
  TEST_ASSERT_EQUAL_UINT8(HUNT_BAND_5GHZ, cfg.band);
  TEST_ASSERT_EQUAL_UINT16(2, cfg.keys_ok);
  TEST_ASSERT_EQUAL_UINT16(0, cfg.keys_bad);
}

void test_tolerates_crlf_blank_lines_and_comment_styles() {
  HuntConfig cfg = {};
  const char* text =
      "\r\n"
      "# hash comment\r\n"
      "; semicolon comment\r\n"
      "   \r\n"
      "\tbssid\t=\tF2:2F:E4:6B:D2:9E\t\r\n"
      "band = 5 # trailing comment\r\n";

  TEST_ASSERT_TRUE(parse(text, cfg));
  TEST_ASSERT_TRUE(cfg.has_bssid);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(FOX_5G, cfg.bssid[0], 6);
  TEST_ASSERT_EQUAL_UINT8(HUNT_BAND_5GHZ, cfg.band);
  TEST_ASSERT_EQUAL_UINT16(0, cfg.keys_bad);
}

void test_keys_and_band_values_are_case_insensitive() {
  HuntConfig cfg = {};
  TEST_ASSERT_TRUE(parse("BSSID=f2:2f:e4:6b:d2:9e\nBAND=ALL\n", cfg));
  TEST_ASSERT_TRUE(cfg.has_bssid);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(FOX_5G, cfg.bssid[0], 6);
  TEST_ASSERT_EQUAL_UINT8(HUNT_BAND_ALL, cfg.band);
}

void test_band_aliases() {
  struct Case { const char* value; uint8_t expected; };
  const Case cases[] = {
    {"2.4", HUNT_BAND_24GHZ}, {"24", HUNT_BAND_24GHZ}, {"2", HUNT_BAND_24GHZ},
    {"2.4GHz", HUNT_BAND_24GHZ}, {"2g", HUNT_BAND_24GHZ},
    {"5", HUNT_BAND_5GHZ}, {"5g", HUNT_BAND_5GHZ}, {"5GHZ", HUNT_BAND_5GHZ},
    {"all", HUNT_BAND_ALL}, {"both", HUNT_BAND_ALL}, {"full", HUNT_BAND_ALL},
    {"fox", HUNT_BAND_FOX}, {"FOX", HUNT_BAND_FOX}, {"foxall", HUNT_BAND_FOX},
    {"hunt", HUNT_BAND_FOX},
    {"default", HUNT_BAND_KEEP_DEFAULT}, {"auto", HUNT_BAND_KEEP_DEFAULT},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    char line[64] = {};
    snprintf(line, sizeof(line), "band = %s\n", cases[i].value);
    HuntConfig cfg = {};
    TEST_ASSERT_TRUE(parse(line, cfg));
    TEST_ASSERT_EQUAL_UINT8(cases[i].expected, cfg.band);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.keys_bad);
  }
}

void test_bssid_aliases_and_separator_forms() {
  HuntConfig cfg = {};
  TEST_ASSERT_TRUE(parse("mac = f2-2f-e4-6b-d2-9e\n", cfg));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(FOX_5G, cfg.bssid[0], 6);

  HuntConfig bare = {};
  TEST_ASSERT_TRUE(parse("target = F22FE46BD29E\n", bare));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(FOX_5G, bare.bssid[0], 6);
}

void test_ssid_is_captured_verbatim_including_spaces() {
  HuntConfig cfg = {};
  TEST_ASSERT_TRUE(parse("ssid = Lagos WiFi 5GHz AP Hard Fox\n", cfg));
  TEST_ASSERT_TRUE(cfg.ssid_seen);
  TEST_ASSERT_TRUE(cfg.has_ssid);
  TEST_ASSERT_EQUAL_STRING("Lagos WiFi 5GHz AP Hard Fox", cfg.ssid);
}

// A blank value must be distinguishable from an absent key, otherwise there is
// no way to clear a filter that the firmware was built with.
void test_blank_values_clear_rather_than_error() {
  HuntConfig cfg = {};
  TEST_ASSERT_TRUE(parse("bssid =\nssid =\n", cfg));
  TEST_ASSERT_TRUE(cfg.bssid_seen);
  TEST_ASSERT_FALSE(cfg.has_bssid);
  TEST_ASSERT_TRUE(cfg.ssid_seen);
  TEST_ASSERT_FALSE(cfg.has_ssid);
  TEST_ASSERT_EQUAL_UINT16(0, cfg.keys_bad);

  HuntConfig absent = {};
  TEST_ASSERT_TRUE(parse("band = 5\n", absent));
  TEST_ASSERT_FALSE(absent.bssid_seen);
  TEST_ASSERT_FALSE(absent.ssid_seen);
}

void test_malformed_entries_are_counted_but_do_not_abort_the_file() {
  HuntConfig cfg = {};
  const char* text =
      "this line has no equals sign\n"
      "bssid = ZZ:ZZ:ZZ:ZZ:ZZ:ZZ\n"
      "unknown_key = 5\n"
      "band = purple\n"
      "ssid = Hard Fox\n";

  // The good line still lands.
  TEST_ASSERT_TRUE(parse(text, cfg));
  TEST_ASSERT_TRUE(cfg.has_ssid);
  TEST_ASSERT_EQUAL_STRING("Hard Fox", cfg.ssid);
  // A bad BSSID must not be treated as a target.
  TEST_ASSERT_FALSE(cfg.has_bssid);
  TEST_ASSERT_EQUAL_UINT16(1, cfg.keys_ok);
  TEST_ASSERT_EQUAL_UINT16(4, cfg.keys_bad);
}

void test_file_with_nothing_usable_reports_failure() {
  HuntConfig cfg = {};
  TEST_ASSERT_FALSE(parse("# only a comment\n\n   \n", cfg));
  TEST_ASSERT_EQUAL_UINT16(0, cfg.keys_ok);

  HuntConfig noise = {};
  TEST_ASSERT_FALSE(parse("garbage\nmore garbage\n", noise));
  TEST_ASSERT_EQUAL_UINT16(2, noise.keys_bad);

  HuntConfig empty = {};
  TEST_ASSERT_FALSE(parse("", empty));
}

void test_final_line_without_newline_is_parsed() {
  HuntConfig cfg = {};
  TEST_ASSERT_TRUE(parse("band = 2.4", cfg));
  TEST_ASSERT_EQUAL_UINT8(HUNT_BAND_24GHZ, cfg.band);
}

void test_overlong_ssid_is_truncated_not_overflowed() {
  HuntConfig cfg = {};
  char line[128] = {};
  snprintf(line, sizeof(line),
           "ssid = 0123456789012345678901234567890123456789\n");
  TEST_ASSERT_TRUE(parse(line, cfg));
  TEST_ASSERT_TRUE(cfg.has_ssid);
  TEST_ASSERT_EQUAL_UINT32(32, strlen(cfg.ssid));
}

void test_band_maps_onto_the_expected_sweep_plan() {
  HuntConfig cfg = {};
  cfg.band = HUNT_BAND_5GHZ;
  ChannelPlan plan = huntConfigChannelPlan(cfg);
  TEST_ASSERT_TRUE(channelPlanUsesBand(plan, WIFI_BAND_5_GHZ));
  TEST_ASSERT_FALSE(channelPlanUsesBand(plan, WIFI_BAND_24_GHZ));
  TEST_ASSERT_EQUAL_UINT32(9, plan.count_5g);

  cfg.band = HUNT_BAND_24GHZ;
  plan = huntConfigChannelPlan(cfg);
  TEST_ASSERT_TRUE(channelPlanUsesBand(plan, WIFI_BAND_24_GHZ));
  TEST_ASSERT_FALSE(channelPlanUsesBand(plan, WIFI_BAND_5_GHZ));
  TEST_ASSERT_EQUAL_UINT32(11, plan.count_24g);

  cfg.band = HUNT_BAND_ALL;
  plan = huntConfigChannelPlan(cfg);
  TEST_ASSERT_TRUE(channelPlanUsesBand(plan, WIFI_BAND_24_GHZ));
  TEST_ASSERT_TRUE(channelPlanUsesBand(plan, WIFI_BAND_5_GHZ));
  TEST_ASSERT_EQUAL_UINT32(14, plan.count_24g);
  TEST_ASSERT_EQUAL_UINT32(25, plan.count_5g);
}


// Several foxes are usually live at once, so repeating the key has to add a
// target rather than replace the previous one.
void test_multiple_bssid_lines_accumulate_targets() {
  HuntConfig cfg = {};
  const char* text =
      "band  = all\n"
      "bssid = F2:EE:CB:62:E8:77\n"
      "bssid = F2:1D:D5:EA:5D:16\n"
      "mac   = F2:2F:E4:6B:D2:9E\n";

  TEST_ASSERT_TRUE(parse(text, cfg));
  TEST_ASSERT_EQUAL_UINT8(3, cfg.bssid_count);
  TEST_ASSERT_TRUE(cfg.has_bssid);

  const uint8_t first[6]  = {0xF2, 0xEE, 0xCB, 0x62, 0xE8, 0x77};
  const uint8_t second[6] = {0xF2, 0x1D, 0xD5, 0xEA, 0x5D, 0x16};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(first, cfg.bssid[0], 6);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(second, cfg.bssid[1], 6);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(FOX_5G, cfg.bssid[2], 6);
  TEST_ASSERT_EQUAL_UINT8(HUNT_BAND_ALL, cfg.band);
  TEST_ASSERT_EQUAL_UINT16(0, cfg.keys_bad);
}

void test_blank_bssid_clears_the_whole_list() {
  HuntConfig cfg = {};
  TEST_ASSERT_TRUE(parse("bssid = F2:EE:CB:62:E8:77\n"
                         "bssid = F2:1D:D5:EA:5D:16\n"
                         "bssid =\n", cfg));
  TEST_ASSERT_EQUAL_UINT8(0, cfg.bssid_count);
  TEST_ASSERT_FALSE(cfg.has_bssid);
  TEST_ASSERT_TRUE(cfg.bssid_seen);
}

// Overflow must be reported. Silently hunting a subset of the list would look
// exactly like the extra foxes being out of range.
void test_targets_beyond_the_limit_are_reported() {
  HuntConfig cfg = {};
  char text[2048] = {};
  size_t n = 0;
  for (int i = 0; i < HUNT_MAX_TARGETS + 2; i++) {
    n += (size_t)snprintf(text + n, sizeof(text) - n,
                          "bssid = F2:00:00:00:00:%02X\n", i);
  }

  TEST_ASSERT_TRUE(parse(text, cfg));
  TEST_ASSERT_EQUAL_UINT8(HUNT_MAX_TARGETS, cfg.bssid_count);
  TEST_ASSERT_EQUAL_UINT16(2, cfg.bssid_dropped);
  TEST_ASSERT_EQUAL_UINT16(2, cfg.keys_bad);
}


// The comment on a bssid line is the fox's name on the console. Losing it
// would put the hunter back to memorising MAC addresses.
void test_bssid_trailing_comment_becomes_the_label() {
  HuntConfig cfg = {};
  const char* text =
      "bssid = F2:C8:7F:C2:94:C4   # AP HARD 2 (450)\n"
      "bssid = F2:2F:E4:6B:D2:9E ; 5G HARD 1 (550)\n"
      "bssid = F2:EE:CB:62:E8:77\n";

  TEST_ASSERT_TRUE(parse(text, cfg));
  TEST_ASSERT_EQUAL_UINT8(3, cfg.bssid_count);
  TEST_ASSERT_EQUAL_STRING("AP HARD 2 (450)", cfg.label[0]);
  TEST_ASSERT_EQUAL_STRING("5G HARD 1 (550)", cfg.label[1]);
  // No comment leaves an empty label; callers fall back to the index.
  TEST_ASSERT_EQUAL_STRING("", cfg.label[2]);
  TEST_ASSERT_EQUAL_UINT16(0, cfg.keys_bad);
}

void test_overlong_label_is_truncated_not_overflowed() {
  HuntConfig cfg = {};
  TEST_ASSERT_TRUE(parse("bssid = F2:2F:E4:6B:D2:9E # "
                         "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n", cfg));
  TEST_ASSERT_EQUAL_UINT32(HUNT_LABEL_MAX - 1, strlen(cfg.label[0]));
}


void test_wildcard_bssid_from_config() {
  HuntConfig cfg = {};
  const char* text =
      "bssid = F2:2F:E4:6B:D2:9E  # 5G HARD 1\n"
      "bssid = F2:*               # any other fox\n";
  TEST_ASSERT_TRUE(parse(text, cfg));
  TEST_ASSERT_EQUAL_UINT8(2, cfg.bssid_count);
  TEST_ASSERT_EQUAL_UINT8(6, cfg.prefix_len[0]);
  TEST_ASSERT_EQUAL_UINT8(1, cfg.prefix_len[1]);
  TEST_ASSERT_EQUAL_UINT8(0xF2, cfg.bssid[1][0]);
  TEST_ASSERT_EQUAL_STRING("any other fox", cfg.label[1]);
  TEST_ASSERT_EQUAL_UINT16(0, cfg.keys_bad);
}


// The fox plan must keep both bands but drop every channel no fox can use:
// 2.4GHz 12-14 and all of DFS. Losing a usable channel here would make a fox
// invisible; keeping a useless one just wastes dwell.
void test_fox_plan_covers_exactly_the_fox_channels() {
  HuntConfig cfg = {};
  cfg.band = HUNT_BAND_FOX;
  const ChannelPlan plan = huntConfigChannelPlan(cfg);

  TEST_ASSERT_TRUE(channelPlanUsesBand(plan, WIFI_BAND_24_GHZ));
  TEST_ASSERT_TRUE(channelPlanUsesBand(plan, WIFI_BAND_5_GHZ));
  TEST_ASSERT_EQUAL_UINT32(11, plan.count_24g);
  TEST_ASSERT_EQUAL_UINT32(9, plan.count_5g);

  for (size_t i = 0; i < plan.count_24g; i++) {
    TEST_ASSERT_TRUE(plan.list_24g[i] >= 1 && plan.list_24g[i] <= 11);
  }
  for (size_t i = 0; i < plan.count_5g; i++) {
    const uint8_t ch = plan.list_5g[i];
    TEST_ASSERT_FALSE(ch >= 52 && ch <= 144);   // no DFS: 210ms dwell for nothing
  }

  // A cycle must still complete and visit every channel in both lists.
  ChannelScheduleState state = {};
  uint8_t seen24[16] = {};
  uint8_t seen5[200] = {};
  bool done = false;
  uint32_t n = 0;
  while (!done && n < 256) {
    const ChannelScheduleEntry e = channelScheduleCurrent(plan, state);
    if (e.band == WIFI_BAND_24_GHZ) seen24[e.channel]++; else seen5[e.channel]++;
    done = channelScheduleAdvance(plan, state);
    n++;
  }
  TEST_ASSERT_TRUE(done);
  for (size_t i = 0; i < plan.count_24g; i++) TEST_ASSERT_TRUE(seen24[plan.list_24g[i]] >= 1);
  for (size_t i = 0; i < plan.count_5g; i++) TEST_ASSERT_TRUE(seen5[plan.list_5g[i]] >= 1);

  // And it must be materially cheaper than full coverage, which is the point.
  TEST_ASSERT_TRUE(n < 30);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_parses_a_typical_config_file);
  RUN_TEST(test_tolerates_crlf_blank_lines_and_comment_styles);
  RUN_TEST(test_keys_and_band_values_are_case_insensitive);
  RUN_TEST(test_band_aliases);
  RUN_TEST(test_bssid_aliases_and_separator_forms);
  RUN_TEST(test_ssid_is_captured_verbatim_including_spaces);
  RUN_TEST(test_blank_values_clear_rather_than_error);
  RUN_TEST(test_malformed_entries_are_counted_but_do_not_abort_the_file);
  RUN_TEST(test_file_with_nothing_usable_reports_failure);
  RUN_TEST(test_final_line_without_newline_is_parsed);
  RUN_TEST(test_overlong_ssid_is_truncated_not_overflowed);
  RUN_TEST(test_band_maps_onto_the_expected_sweep_plan);
  RUN_TEST(test_multiple_bssid_lines_accumulate_targets);
  RUN_TEST(test_blank_bssid_clears_the_whole_list);
  RUN_TEST(test_targets_beyond_the_limit_are_reported);
  RUN_TEST(test_bssid_trailing_comment_becomes_the_label);
  RUN_TEST(test_overlong_label_is_truncated_not_overflowed);
  RUN_TEST(test_wildcard_bssid_from_config);
  RUN_TEST(test_fox_plan_covers_exactly_the_fox_channels);
  return UNITY_END();
}
