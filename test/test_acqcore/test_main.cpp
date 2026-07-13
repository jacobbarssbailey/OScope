// Host-side unit tests for lib/AcqCore (pure acquisition logic).
#include <unity.h>
#include <AcqCore.h>

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
    return UNITY_END();
}
