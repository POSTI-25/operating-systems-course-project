#include "test.h"
#include "core/ledger.h"

static LedgerConfig config(void)
{
    LedgerConfig value = {0};
    value.capacity[LEDGER_CPU] = 100;
    value.capacity[LEDGER_MEMORY] = 1000;
    value.capacity[LEDGER_STORAGE] = 1000;
    value.capacity[LEDGER_SENSOR] = 0x0f;
    value.capacity[LEDGER_COMM] = 100;
    value.protected_reserve[LEDGER_CPU] = 20;
    value.protected_reserve[LEDGER_MEMORY] = 100;
    value.protected_reserve[LEDGER_STORAGE] = 100;
    value.protected_reserve[LEDGER_SENSOR] = 0x08;
    value.protected_reserve[LEDGER_COMM] = 10;
    value.battery_capacity_mJ = 100000;
    value.soc_floor_permille = 200;
    return value;
}

static void commit_itself_protects_class_zero_pool(void)
{
    Ledger ledger;
    LedgerConfig setup = config();
    ResourceDemand demand = { .cpu_pct = 81 };
    ASSERT_TRUE(ledger_init(&ledger, &setup));
    ASSERT_TRUE(ledger_update_energy(&ledger, 800, 0, 0));
    ASSERT_TRUE(!ledger_commit(&ledger, 1, 1, demand, 0));
    ASSERT_TRUE(ledger_commit(&ledger, 1, 0, demand, 0));
    ASSERT_TRUE(!ledger_commit(&ledger, 2, 0, demand, 0));
}

static void rate_and_sensor_release_but_stock_persists(void)
{
    Ledger ledger;
    LedgerConfig setup = config();
    LedgerDimension state;
    ResourceDemand demand = { .cpu_pct = 10, .mem_KB = 200,
        .storage_KB = 50, .sensor_mask = 0x01U, .comm_pct = 5 };
    ASSERT_TRUE(ledger_init(&ledger, &setup));
    ASSERT_TRUE(ledger_update_energy(&ledger, 800, 0, 0));
    ASSERT_TRUE(ledger_commit(&ledger, 7, 1, demand, 1000));
    ASSERT_TRUE(ledger_query(&ledger, LEDGER_CPU, &state));
    ASSERT_EQ_INT(10, state.committed);
    ASSERT_TRUE(ledger_query(&ledger, LEDGER_SENSOR, &state));
    ASSERT_EQ_INT(1, state.committed);
    ASSERT_TRUE(!ledger_commit(&ledger, 8, 1, demand, 1000));
    ASSERT_TRUE(ledger_release_rate(&ledger, 7));
    ASSERT_TRUE(ledger_query(&ledger, LEDGER_CPU, &state));
    ASSERT_EQ_INT(0, state.committed);
    ASSERT_TRUE(ledger_query(&ledger, LEDGER_MEMORY, &state));
    ASSERT_EQ_INT(200, state.committed);
    ASSERT_TRUE(ledger_release_stock(&ledger, 7));
    ASSERT_TRUE(ledger_query(&ledger, LEDGER_MEMORY, &state));
    ASSERT_EQ_INT(0, state.committed);
}

static void energy_budget_includes_active_task_draws(void)
{
    Ledger ledger;
    LedgerConfig setup = config();
    LedgerDimension state;
    ResourceDemand zero = {0};
    ASSERT_TRUE(ledger_init(&ledger, &setup));
    ASSERT_TRUE(ledger_update_energy(&ledger, 800, 5000, 10000));
    ASSERT_TRUE(ledger_query(&ledger, LEDGER_ENERGY, &state));
    ASSERT_EQ_INT(55000, state.capacity);
    ASSERT_TRUE(ledger_commit(&ledger, 1, 1, zero, 30000));
    ASSERT_TRUE(!ledger_commit(&ledger, 2, 1, zero, 30000));
    ASSERT_TRUE(ledger_query(&ledger, LEDGER_ENERGY, &state));
    ASSERT_EQ_INT(30000, state.committed);
    ASSERT_EQ_INT(25000, state.free);
    ASSERT_TRUE(ledger_release_rate(&ledger, 1));
    ASSERT_TRUE(ledger_query(&ledger, LEDGER_ENERGY, &state));
    ASSERT_EQ_INT(0, state.committed);
}

static void sensor_protected_bits_need_class_zero(void)
{
    Ledger ledger;
    LedgerConfig setup = config();
    ResourceDemand sensor = { .sensor_mask = 0x08U };
    ASSERT_TRUE(ledger_init(&ledger, &setup));
    ASSERT_TRUE(ledger_update_energy(&ledger, 800, 0, 0));
    ASSERT_TRUE(!ledger_commit(&ledger, 1, 1, sensor, 0));
    ASSERT_TRUE(ledger_commit(&ledger, 1, 0, sensor, 0));
    ASSERT_TRUE(!ledger_commit(&ledger, 2, 0, sensor, 0));
}

static void energy_protected_pool_is_available_only_to_class_zero(void)
{
    Ledger ledger;
    LedgerConfig setup = config();
    ResourceDemand zero = {0};
    setup.protected_reserve[LEDGER_ENERGY] = 10000;
    ASSERT_TRUE(ledger_init(&ledger, &setup));
    ASSERT_TRUE(ledger_update_energy(&ledger, 800, 0, 0));
    ASSERT_TRUE(!ledger_commit(&ledger, 1, 1, zero, 55000));
    ASSERT_TRUE(ledger_commit(&ledger, 1, 0, zero, 55000));
}

int main(void)
{
    RUN_TEST(commit_itself_protects_class_zero_pool);
    RUN_TEST(rate_and_sensor_release_but_stock_persists);
    RUN_TEST(energy_budget_includes_active_task_draws);
    RUN_TEST(sensor_protected_bits_need_class_zero);
    RUN_TEST(energy_protected_pool_is_available_only_to_class_zero);
    return test_failures != 0;
}
