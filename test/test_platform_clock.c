#include "test.h"
#include "platform/platform_clock.h"

#include <stdint.h>

static void virtual_time_moves_only_when_advanced(void)
{
    PlatformClock clock;
    uint64_t now = 0;
    ASSERT_TRUE(platform_clock_init(&clock, PLATFORM_CLOCK_VIRTUAL, 7));
    ASSERT_TRUE(platform_clock_now_ns(&clock, &now));
    ASSERT_EQ_INT(7, now);
    ASSERT_TRUE(platform_clock_advance_ns(&clock, 5));
    ASSERT_TRUE(platform_clock_now_ns(&clock, &now));
    ASSERT_EQ_INT(12, now);
}

static void virtual_overflow_is_rejected(void)
{
    PlatformClock clock;
    uint64_t now = 0;
    ASSERT_TRUE(platform_clock_init(&clock, PLATFORM_CLOCK_VIRTUAL, UINT64_MAX));
    ASSERT_TRUE(!platform_clock_advance_ns(&clock, 1));
    ASSERT_TRUE(platform_clock_now_ns(&clock, &now));
    ASSERT_TRUE(now == UINT64_MAX);
}

static void real_time_is_monotonic_and_not_advanceable(void)
{
    PlatformClock clock;
    uint64_t first = 0;
    uint64_t second = 0;
    ASSERT_TRUE(platform_clock_init(&clock, PLATFORM_CLOCK_REAL, 123));
    ASSERT_TRUE(platform_clock_now_ns(&clock, &first));
    ASSERT_TRUE(!platform_clock_advance_ns(&clock, 1));
    ASSERT_TRUE(platform_clock_now_ns(&clock, &second));
    ASSERT_TRUE(second >= first);
}

int main(void)
{
    RUN_TEST(virtual_time_moves_only_when_advanced);
    RUN_TEST(virtual_overflow_is_rejected);
    RUN_TEST(real_time_is_monotonic_and_not_advanceable);
    return test_failures != 0;
}
