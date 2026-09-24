#include "test.h"
#include "core/task_model.h"

static void table_keeps_all_three_profiles(void)
{
    TaskTable table;
    Task task = { .task_id = 9, .period_ns = 10, .deadline_ns = 8,
                  .nominal_exec_ns = 2, .criticality_class = 0, .mission_value = 800 };
    TaskModeProfile profile = { .energy_cost_mJ = 42, .thermal_effect_mC = -3,
        .value_fraction_permille = 700,
        .resource_demand = { 20, 100, 200, 4, 5 } };
    const Task *got;
    task_table_init(&table);
    ASSERT_TRUE(task_table_add(&table, &task));
    ASSERT_TRUE(!task_table_complete(&table));
    ASSERT_TRUE(task_table_set_mode(&table, 9, TASK_MODE_FULL, profile));
    ASSERT_TRUE(!task_table_set_mode(&table, 9, TASK_MODE_FULL, profile));
    ASSERT_TRUE(task_table_set_mode(&table, 9, TASK_MODE_THROTTLED, profile));
    ASSERT_TRUE(task_table_set_mode(&table, 9, TASK_MODE_DEGRADED, profile));
    ASSERT_TRUE(task_table_complete(&table));
    got = task_table_find(&table, 9);
    ASSERT_TRUE(got != NULL);
    ASSERT_EQ_INT(42, got->modes[TASK_MODE_FULL].energy_cost_mJ);
    ASSERT_EQ_INT(-3, got->modes[TASK_MODE_DEGRADED].thermal_effect_mC);
    ASSERT_EQ_INT(200, got->modes[TASK_MODE_THROTTLED].resource_demand.storage_KB);
}

static void invalid_tasks_and_profiles_are_rejected(void)
{
    TaskTable table;
    Task task = { .task_id = 1, .period_ns = 1, .deadline_ns = 1,
                  .nominal_exec_ns = 1, .mission_value = 1000 };
    TaskModeProfile profile = { .value_fraction_permille = 1000 };
    task_table_init(&table);
    ASSERT_TRUE(task_table_add(&table, &task));
    ASSERT_TRUE(!task_table_add(&table, &task));
    task.period_ns = 0;
    ASSERT_TRUE(!task_table_add(&table, &task));
    profile.resource_demand.cpu_pct = 101;
    ASSERT_TRUE(!task_table_set_mode(&table, 1, TASK_MODE_FULL, profile));
    profile.resource_demand.cpu_pct = 0;
    ASSERT_TRUE(!task_table_set_mode(&table, 2, TASK_MODE_FULL, profile));
}

int main(void)
{
    RUN_TEST(table_keeps_all_three_profiles);
    RUN_TEST(invalid_tasks_and_profiles_are_rejected);
    return test_failures != 0;
}
