#define _POSIX_C_SOURCE 200809L

#include "core/utility.h"

#include <inttypes.h>
#include <stdio.h>
#include <time.h>

#define MEASURE_TASKS 8U
#define MEASURE_REPEATS 2000U

static uint64_t cpu_time_ns(const struct timespec *time)
{
    return (uint64_t)time->tv_sec * UINT64_C(1000000000) +
        (uint64_t)time->tv_nsec;
}

static void make_tasks(Task tasks[MEASURE_TASKS])
{
    size_t i;
    for (i = 0; i < MEASURE_TASKS; ++i) {
        Task *task = &tasks[i];
        *task = (Task){ .task_id = (uint32_t)i + 1U, .period_ns = 20,
            .deadline_ns = 10, .nominal_exec_ns = 2,
            .criticality_class = (uint8_t)(i < 2 ? 0U : 1U),
            .mission_value = (uint16_t)(900U - (unsigned)i * 50U),
            .primary_subsystem = i < 2 ? SUBSYSTEM_ADCS : (Subsystem)(i % N_SUBSYSTEMS) };
        task->modes[TASK_MODE_FULL] = (TaskModeProfile){ 5000, 50, 1000,
            { 15, 100, 20, UINT32_C(1) << (unsigned)task->primary_subsystem, 5 } };
        task->modes[TASK_MODE_THROTTLED] = (TaskModeProfile){ 3000, 30, 700,
            { 10, 70, 10, UINT32_C(1) << (unsigned)task->primary_subsystem, 3 } };
        task->modes[TASK_MODE_DEGRADED] = (TaskModeProfile){ 1000, 10, 400,
            { 5, 40, 5, 0, 1 } };
    }
}

static bool prepare(Ledger *ledger, TelemetryFrame *telemetry)
{
    LedgerConfig config = {0};
    config.capacity[LEDGER_CPU] = 100;
    config.capacity[LEDGER_MEMORY] = 10000;
    config.capacity[LEDGER_STORAGE] = 10000;
    config.capacity[LEDGER_SENSOR] = 0x0f;
    config.capacity[LEDGER_COMM] = 100;
    config.protected_reserve[LEDGER_CPU] = 20;
    config.protected_reserve[LEDGER_SENSOR] = 0x08;
    config.battery_capacity_mJ = 1000000;
    config.soc_floor_permille = 200;
    telemetry_frame_init(telemetry);
    telemetry->battery_soc_mV_pct.value = 800;
    telemetry->fault_flags.value = 0x08;
    return ledger_init(ledger, &config) && ledger_update_energy(ledger, 800, 0, 0);
}

static bool measure_pass(Ledger *ledger, const TelemetryFrame *telemetry,
                         const Task tasks[MEASURE_TASKS], uint64_t *sink)
{
    size_t i;
    for (i = 0; i < MEASURE_TASKS; ++i) {
        const Task *task = &tasks[i];
        Candidate candidates[SAGE_MAX_CANDIDATES];
        GateFilterResult filtered;
        GateContext gate = {0};
        UtilityContext utility = {0};
        SelectionResult selection;
        const SurvivorSet *survivors;
        LedgerDimension state;
        size_t count = mode_generator_generate(task, telemetry, candidates,
                                                SAGE_MAX_CANDIDATES);
        if (count != SAGE_MAX_CANDIDATES) return false;
        gate.telemetry = telemetry;
        gate.ledger = ledger;
        gate.safety_margin_permille = 10;
        gate.predicted_completion_time_ns = 2;
        gate.absolute_deadline_ns = 10;
        for (size_t j = 0; j < N_SUBSYSTEMS; ++j)
            gate.remaining_thermal_margin_mC[j] = 100;
        if (i == MEASURE_TASKS - 1U)
            gate.remaining_thermal_margin_mC[task->primary_subsystem] = 0;
        survivors = gate_filter(candidates, count, task, &gate, &filtered);
        if (survivors == NULL) return false;
        utility.time_remaining_to_deadline_ns = 10;
        utility.time_until_next_tick_ns = 1;
        utility.fault_flags = (uint32_t)telemetry->fault_flags.value;
        for (size_t j = 0; j < N_SUBSYSTEMS; ++j)
            utility.remaining_thermal_margin_mC[j] = gate.remaining_thermal_margin_mC[j];
        if (!ledger_query(ledger, LEDGER_ENERGY, &state)) return false;
        utility.energy_capacity_mJ = state.capacity;
        if (!ledger_query(ledger, LEDGER_CPU, &state)) return false;
        utility.free_cpu_pct = state.free;
        if (!ledger_query(ledger, LEDGER_MEMORY, &state)) return false;
        utility.free_mem_KB = state.free;
        if (!ledger_query(ledger, LEDGER_STORAGE, &state)) return false;
        utility.free_storage_KB = state.free;
        if (!ledger_query(ledger, LEDGER_COMM, &state)) return false;
        utility.free_comm_pct = state.free;
        if (!utility_select(survivors, task, &utility, &selection)) return false;
        *sink += (uint64_t)selection.outcome + (uint64_t)selection.selected.task_id;
        if (selection.outcome == SELECTION_EXECUTING &&
            !ledger_commit(ledger, task->task_id, task->criticality_class,
                           selection.selected.predicted_cost.resource_demand,
                           selection.selected.predicted_cost.energy_cost_mJ))
            return false;
    }
    return true;
}

int main(void)
{
    Task tasks[MEASURE_TASKS];
    Ledger ledger;
    TelemetryFrame telemetry;
    struct timespec start;
    struct timespec end;
    uint64_t total_ns = 0;
    uint64_t sink = 0;
    unsigned run;
    make_tasks(tasks);
    for (run = 0; run < MEASURE_REPEATS; ++run) {
        if (!prepare(&ledger, &telemetry) ||
            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start) != 0) return 1;
        if (!measure_pass(&ledger, &telemetry, tasks, &sink) ||
            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end) != 0) return 1;
        total_ns += cpu_time_ns(&end) - cpu_time_ns(&start);
    }
    printf("provisional_cpu_ns_per_pass=%" PRIu64 " tasks=%u repeats=%u sink=%" PRIu64 "\n",
           total_ns / MEASURE_REPEATS, MEASURE_TASKS, MEASURE_REPEATS, sink);
    return 0;
}
