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

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_rising_edge_found);
    RUN_TEST(test_falling_edge_found);
    RUN_TEST(test_no_edge_returns_minus_one);
    RUN_TEST(test_exact_threshold_counts_as_crossing);
    RUN_TEST(test_search_len_zero_finds_nothing);
    return UNITY_END();
}
