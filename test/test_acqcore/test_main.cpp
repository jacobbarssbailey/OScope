// Host-side unit tests for lib/AcqCore (pure acquisition logic).
#include <unity.h>
#include <AcqCore.h>
#include <cmath>

void setUp() {}
void tearDown() {}

static void test_rising_edge_found() {
    // 100,200,300 with thr=250 rising: crossing is at index 2 (200<250, 300>=250).
    const uint16_t s[] = {100, 200, 300, 400};
    TEST_ASSERT_EQUAL_INT(2, AcqCore::findTrigger(s, 3, 250, true));
}

static void test_falling_edge_found() {
    const uint16_t s[] = {400, 300, 200, 100};
    TEST_ASSERT_EQUAL_INT(2, AcqCore::findTrigger(s, 3, 250, false));
}

static void test_no_edge_returns_minus_one() {
    const uint16_t s[] = {100, 110, 120, 130};
    TEST_ASSERT_EQUAL_INT(-1, AcqCore::findTrigger(s, 3, 250, true));
}

static void test_exact_threshold_counts_as_crossing() {
    // rising uses >= thr on the right sample.
    const uint16_t s[] = {249, 250};
    TEST_ASSERT_EQUAL_INT(1, AcqCore::findTrigger(s, 1, 250, true));
}

static void test_search_len_zero_finds_nothing() {
    const uint16_t s[] = {0, 1023};
    TEST_ASSERT_EQUAL_INT(-1, AcqCore::findTrigger(s, 0, 512, true));
}

static void test_checksum_is_sum_mod_16bit() {
    const uint16_t s[] = {1, 2, 3};
    TEST_ASSERT_EQUAL_UINT16(6, AcqCore::checksum(s, 3));
}

static void test_checksum_detects_single_sample_change() {
    uint16_t s[] = {10, 20, 30, 40};
    const uint16_t before = AcqCore::checksum(s, 4);
    s[2] = 31;
    TEST_ASSERT_NOT_EQUAL(before, AcqCore::checksum(s, 4));
}

static void test_checksum_empty_is_zero() {
    TEST_ASSERT_EQUAL_UINT16(0, AcqCore::checksum(nullptr, 0));
}

// ---- CellStats (characterization cell accumulator) -----------------------

// Fold `n` identical seconds into a cell, so the tests below read as
// "the cell ran n seconds at this rate".
static void feed(AcqCore::CellStats& c, uint32_t n, uint32_t frames,
                 uint32_t tears, uint32_t miss, uint32_t wait,
                 uint32_t over, uint32_t gapUs) {
    for (uint32_t i = 0; i < n; ++i) c.addSecond(frames, tears, miss, wait, over, gapUs);
}

static void test_restart_stores_cell_identity_and_clears_totals() {
    AcqCore::CellStats c;
    feed(c, 3, 30, 2, 5, 100, 1, 40000);
    c.restart(500, 1);
    TEST_ASSERT_EQUAL_UINT32(500, c.timebase);
    TEST_ASSERT_EQUAL_UINT8(1, c.mode);
    TEST_ASSERT_EQUAL_UINT32(0, c.secs);
    TEST_ASSERT_EQUAL_UINT32(0, c.frames);
    TEST_ASSERT_EQUAL_UINT32(0, c.tears);
    TEST_ASSERT_EQUAL_UINT32(0, c.gapMaxUs);
}

static void test_add_second_accumulates_totals() {
    AcqCore::CellStats c;
    feed(c, 4, 28, 7, 140, 1300, 0, 51000);
    TEST_ASSERT_EQUAL_UINT32(4, c.secs);
    TEST_ASSERT_EQUAL_UINT32(112, c.frames);
    TEST_ASSERT_EQUAL_UINT32(28, c.tears);
    TEST_ASSERT_EQUAL_UINT32(560, c.misses);
    TEST_ASSERT_EQUAL_UINT32(5200, c.waits);
}

static void test_peaks_keep_worst_second_not_last() {
    AcqCore::CellStats c;
    c.addSecond(28, 10, 178, 1585, 0, 81000);   // the bad second
    c.addSecond(28, 3, 120, 900, 0, 51000);     // a calmer one after it
    TEST_ASSERT_EQUAL_UINT32(10, c.tearPeak);
    TEST_ASSERT_EQUAL_UINT32(178, c.missPeak);
    TEST_ASSERT_EQUAL_UINT32(1585, c.waitPeak);
}

static void test_gap_max_keeps_worst_gap_across_cell() {
    AcqCore::CellStats c;
    c.addSecond(32, 0, 0, 0, 0, 31000);
    c.addSecond(28, 0, 0, 0, 0, 81000);
    c.addSecond(32, 0, 0, 0, 0, 31000);
    TEST_ASSERT_EQUAL_UINT32(81000, c.gapMaxUs);
}

static void test_fps_tenths_averages_over_the_cell() {
    AcqCore::CellStats c;
    c.addSecond(28, 0, 0, 0, 0, 0);
    c.addSecond(29, 0, 0, 0, 0, 0);
    // 57 frames / 2 s = 28.5 fps
    TEST_ASSERT_EQUAL_UINT32(285, c.fpsTenths());
}

static void test_fps_tenths_is_zero_before_any_second() {
    AcqCore::CellStats c;
    TEST_ASSERT_EQUAL_UINT32(0, c.fpsTenths());
}

static void test_pass_requires_zero_tears_and_zero_overruns() {
    AcqCore::CellStats clean;
    feed(clean, 150, 32, 0, 0, 0, 0, 31000);
    TEST_ASSERT_TRUE(clean.pass());

    AcqCore::CellStats torn;
    feed(torn, 150, 32, 1, 0, 0, 0, 31000);
    TEST_ASSERT_FALSE(torn.pass());

    AcqCore::CellStats overrun;
    feed(overrun, 150, 32, 0, 0, 0, 1, 31000);
    TEST_ASSERT_FALSE(overrun.pass());
}

static void test_trigger_misses_alone_do_not_fail_a_cell() {
    // miss is expected at short timebases (buffer shorter than a period); the
    // acceptance criteria are tears and overruns only.
    AcqCore::CellStats c;
    feed(c, 150, 28, 0, 150, 1300, 0, 51000);
    TEST_ASSERT_TRUE(c.pass());
}

// ---- cellReport (when to print the aggregate line) -----------------------

static void test_report_is_progress_at_each_checkpoint() {
    TEST_ASSERT_EQUAL_INT((int)AcqCore::CellReport::Progress,
                          (int)AcqCore::cellReport(30, 150, 30));
    TEST_ASSERT_EQUAL_INT((int)AcqCore::CellReport::Progress,
                          (int)AcqCore::cellReport(120, 150, 30));
}

static void test_report_is_done_exactly_at_target() {
    TEST_ASSERT_EQUAL_INT((int)AcqCore::CellReport::Done,
                          (int)AcqCore::cellReport(150, 150, 30));
}

static void test_report_is_silent_between_checkpoints_and_after_target() {
    TEST_ASSERT_EQUAL_INT((int)AcqCore::CellReport::None,
                          (int)AcqCore::cellReport(29, 150, 30));
    TEST_ASSERT_EQUAL_INT((int)AcqCore::CellReport::None,
                          (int)AcqCore::cellReport(180, 150, 30));
}

static void test_report_is_silent_before_the_first_second() {
    TEST_ASSERT_EQUAL_INT((int)AcqCore::CellReport::None,
                          (int)AcqCore::cellReport(0, 150, 30));
}

static void test_partial_is_worth_reporting_only_when_a_real_dwell_was_cut_short() {
    // knob passing through an intermediate timebase: silent
    TEST_ASSERT_FALSE(AcqCore::cellPartialWorth(2, 5, 150));
    // an aborted cell: worth a line so the log says why it is missing
    TEST_ASSERT_TRUE(AcqCore::cellPartialWorth(40, 5, 150));
    // a completed cell already printed DONE
    TEST_ASSERT_FALSE(AcqCore::cellPartialWorth(150, 5, 150));
}

static void test_window_is_stale_when_reporting_was_interrupted() {
    // A normal 1 Hz window, and a little jitter past it, are not stale.
    TEST_ASSERT_FALSE(AcqCore::diagWindowStale(1000, 2000));
    TEST_ASSERT_FALSE(AcqCore::diagWindowStale(1400, 2000));
    // A trip to the settings menu, or a stopped trace, leaves a long gap: those
    // deltas span many seconds and must not be recorded as one second.
    TEST_ASSERT_TRUE(AcqCore::diagWindowStale(20000, 2000));
}

static void test_skew_peak_keeps_the_worst_instantaneous_skew() {
    AcqCore::CellStats c;
    c.noteSkew(1);
    c.noteSkew(7);
    c.noteSkew(0);
    TEST_ASSERT_EQUAL_UINT32(7, c.skewPeak);
}

static void test_excess_skew_fails_a_cell_that_is_otherwise_clean() {
    // Phase 2 turned up cells with zero tears but ~80 samples of A/B skew, which
    // violates the design spec's "within ~1 sample" criterion.  A clean-on-tears
    // cell must not report PASS in that state.
    AcqCore::CellStats c;
    feed(c, 150, 32, 0, 0, 0, 0, 31000);
    c.noteSkew(80);
    TEST_ASSERT_FALSE(c.pass());
}

static void test_a_sample_or_two_of_skew_is_within_tolerance() {
    AcqCore::CellStats c;
    feed(c, 150, 32, 0, 0, 0, 0, 31000);
    c.noteSkew(2);
    TEST_ASSERT_TRUE(c.pass());
}

static void test_skew_peak_clears_on_restart() {
    AcqCore::CellStats c;
    c.noteSkew(7);
    c.restart(500, 1);
    TEST_ASSERT_EQUAL_UINT32(0, c.skewPeak);
}

// ---- Ring cursor protocol ------------------------------------------------

static void test_ring_index_wraps_power_of_two() {
    TEST_ASSERT_EQUAL_UINT32(0,    AcqCore::ringIndex(0, 4096));
    TEST_ASSERT_EQUAL_UINT32(4095, AcqCore::ringIndex(4095, 4096));
    TEST_ASSERT_EQUAL_UINT32(0,    AcqCore::ringIndex(4096, 4096));
    TEST_ASSERT_EQUAL_UINT32(5,    AcqCore::ringIndex(4096ULL * 1000 + 5, 4096));
}

static void test_safe_watermark_trails_by_guard() {
    TEST_ASSERT_EQUAL_UINT64(968, AcqCore::safeWatermark(1000, 32));
}

static void test_safe_watermark_clamps_to_zero() {
    TEST_ASSERT_EQUAL_UINT64(0, AcqCore::safeWatermark(10, 32));
}

static void test_newest_window_when_enough_data() {
    uint64_t base = 0;
    TEST_ASSERT_TRUE(AcqCore::newestWindow(1000, 480, &base));
    TEST_ASSERT_EQUAL_UINT64(520, base);
}

static void test_newest_window_insufficient_data() {
    uint64_t base = 99;
    TEST_ASSERT_FALSE(AcqCore::newestWindow(479, 480, &base));
}

// ---- YIN pitch detection --------------------------------------------------

static void test_yin_detects_sine_period() {
    // A 64-sample-period sine over a 1024-sample window; YIN should recover the
    // period to well under a sample.
    const int n = 1024;
    static float x[1024];
    static float diff[513];
    const float period = 64.0f;
    for (int i = 0; i < n; ++i)
        x[i] = 100.0f * sinf(2.0f * (float)M_PI * i / period);
    const float est = AcqCore::yinPeriod(x, n, diff, 0.15f);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, period, est);
}

static void test_yin_ignores_dc_offset() {
    // The same sine on a large DC pedestal — the difference function cancels DC,
    // so the estimate must be unchanged.
    const int n = 1024;
    static float x[1024];
    static float diff[513];
    const float period = 100.0f;
    for (int i = 0; i < n; ++i)
        x[i] = 500.0f + 80.0f * sinf(2.0f * (float)M_PI * i / period);
    const float est = AcqCore::yinPeriod(x, n, diff, 0.15f);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, period, est);
}

static void test_yin_returns_negative_on_silence() {
    const int n = 1024;
    static float x[1024] = {0};
    static float diff[513];
    TEST_ASSERT_TRUE(AcqCore::yinPeriod(x, n, diff, 0.15f) < 0.0f);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_rising_edge_found);
    RUN_TEST(test_falling_edge_found);
    RUN_TEST(test_no_edge_returns_minus_one);
    RUN_TEST(test_exact_threshold_counts_as_crossing);
    RUN_TEST(test_search_len_zero_finds_nothing);
    RUN_TEST(test_checksum_is_sum_mod_16bit);
    RUN_TEST(test_checksum_detects_single_sample_change);
    RUN_TEST(test_checksum_empty_is_zero);
    RUN_TEST(test_restart_stores_cell_identity_and_clears_totals);
    RUN_TEST(test_add_second_accumulates_totals);
    RUN_TEST(test_peaks_keep_worst_second_not_last);
    RUN_TEST(test_gap_max_keeps_worst_gap_across_cell);
    RUN_TEST(test_fps_tenths_averages_over_the_cell);
    RUN_TEST(test_fps_tenths_is_zero_before_any_second);
    RUN_TEST(test_pass_requires_zero_tears_and_zero_overruns);
    RUN_TEST(test_trigger_misses_alone_do_not_fail_a_cell);
    RUN_TEST(test_report_is_progress_at_each_checkpoint);
    RUN_TEST(test_report_is_done_exactly_at_target);
    RUN_TEST(test_report_is_silent_between_checkpoints_and_after_target);
    RUN_TEST(test_report_is_silent_before_the_first_second);
    RUN_TEST(test_partial_is_worth_reporting_only_when_a_real_dwell_was_cut_short);
    RUN_TEST(test_window_is_stale_when_reporting_was_interrupted);
    RUN_TEST(test_skew_peak_keeps_the_worst_instantaneous_skew);
    RUN_TEST(test_excess_skew_fails_a_cell_that_is_otherwise_clean);
    RUN_TEST(test_a_sample_or_two_of_skew_is_within_tolerance);
    RUN_TEST(test_skew_peak_clears_on_restart);
    RUN_TEST(test_ring_index_wraps_power_of_two);
    RUN_TEST(test_safe_watermark_trails_by_guard);
    RUN_TEST(test_safe_watermark_clamps_to_zero);
    RUN_TEST(test_newest_window_when_enough_data);
    RUN_TEST(test_newest_window_insufficient_data);
    RUN_TEST(test_yin_detects_sine_period);
    RUN_TEST(test_yin_ignores_dc_offset);
    RUN_TEST(test_yin_returns_negative_on_silence);
    return UNITY_END();
}
