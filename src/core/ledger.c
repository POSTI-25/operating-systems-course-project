#include "ledger.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static bool checked_add(int64_t left, int64_t right, int64_t *out)
{
    if ((right > 0 && left > INT64_MAX - right) ||
        (right < 0 && left < INT64_MIN - right)) return false;
    *out = left + right;
    return true;
}

static bool checked_sub(int64_t left, int64_t right, int64_t *out)
{
    if (right == INT64_MIN) return false;
    return checked_add(left, -right, out);
}

bool ledger_init(Ledger *ledger, const LedgerConfig *config)
{
    int id;
    if (ledger == NULL || config == NULL || config->battery_capacity_mJ == 0 ||
        config->battery_capacity_mJ > INT64_MAX || config->soc_floor_permille > 1000 ||
        config->capacity[LEDGER_ENERGY] != 0 ||
        config->capacity[LEDGER_CPU] > 100 || config->capacity[LEDGER_COMM] > 100 ||
        config->capacity[LEDGER_SENSOR] > UINT32_MAX) return false;
    for (id = 0; id < LEDGER_DIMENSION_COUNT; ++id) {
        if (config->capacity[id] < 0 || config->protected_reserve[id] < 0 ||
            (id != LEDGER_SENSOR && id != LEDGER_ENERGY &&
             config->protected_reserve[id] > config->capacity[id]))
            return false;
    }
    if (config->protected_reserve[LEDGER_SENSOR] > UINT32_MAX ||
        ((uint32_t)config->protected_reserve[LEDGER_SENSOR] &
         ~(uint32_t)config->capacity[LEDGER_SENSOR]) != 0U) return false;
    memset(ledger, 0, sizeof(*ledger));
    ledger->config = *config;
    return true;
}

bool ledger_set_rate_capacities(Ledger *ledger, uint16_t cpu_pct, uint16_t comm_pct)
{
    if (ledger == NULL || cpu_pct > 100 || comm_pct > 100) return false;
    ledger->config.capacity[LEDGER_CPU] = (int64_t)cpu_pct;
    ledger->config.capacity[LEDGER_COMM] = (int64_t)comm_pct;
    return true;
}

bool ledger_set_telemetry_capacities(Ledger *ledger, uint16_t cpu_pct,
                                     uint64_t mem_KB, uint64_t storage_KB,
                                     uint32_t sensor_mask, uint16_t comm_pct)
{
    if (ledger == NULL || cpu_pct > 100 || comm_pct > 100 ||
        mem_KB > INT64_MAX || storage_KB > INT64_MAX) return false;
    ledger->config.capacity[LEDGER_CPU] = (int64_t)cpu_pct;
    ledger->config.capacity[LEDGER_MEMORY] = (int64_t)mem_KB;
    ledger->config.capacity[LEDGER_STORAGE] = (int64_t)storage_KB;
    ledger->config.capacity[LEDGER_SENSOR] = (int64_t)sensor_mask;
    ledger->config.capacity[LEDGER_COMM] = (int64_t)comm_pct;
    return true;
}

bool ledger_set_self_cost(Ledger *ledger, uint16_t cpu_pct,
                          uint64_t energy_mJ)
{
    int64_t previous_cpu;
    int64_t previous_energy;
    LedgerDimension state;
    if (ledger == NULL || cpu_pct > 100 || energy_mJ > INT64_MAX)
        return false;
    previous_cpu = ledger->self_cpu_pct;
    previous_energy = ledger->self_energy_mJ;
    ledger->self_cpu_pct = (int64_t)cpu_pct;
    ledger->self_energy_mJ = (int64_t)energy_mJ;
    if (!ledger_query(ledger, LEDGER_CPU, &state) ||
        !ledger_query(ledger, LEDGER_ENERGY, &state)) {
        ledger->self_cpu_pct = previous_cpu;
        ledger->self_energy_mJ = previous_energy;
        return false;
    }
    return true;
}

bool ledger_update_energy(Ledger *ledger, uint16_t soc_permille,
                          uint64_t predicted_harvest_mJ,
                          uint64_t other_planned_draws_mJ)
{
    int64_t delta;
    int64_t physical;
    int64_t available;
    int64_t with_harvest;
    int64_t budget;
    size_t i;
    if (ledger == NULL || soc_permille > 1000 ||
        predicted_harvest_mJ > INT64_MAX || other_planned_draws_mJ > INT64_MAX)
        return false;
    delta = (int64_t)soc_permille - (int64_t)ledger->config.soc_floor_permille;
    physical = (int64_t)ledger->config.battery_capacity_mJ;
    available = (physical / 1000) * delta + ((physical % 1000) * delta) / 1000;
    if (!checked_add(available, (int64_t)predicted_harvest_mJ, &with_harvest) ||
        !checked_sub(with_harvest, (int64_t)other_planned_draws_mJ, &budget))
        return false;
    ledger->config.capacity[LEDGER_ENERGY] = budget;
    for (i = 0; i < SAGE_MAX_TASKS; ++i) {
        LedgerReservation *entry = &ledger->reservations[i];
        if (entry->in_use && !entry->rate_active) {
            entry->energy_pending = false;
            if (!entry->stock_held) entry->in_use = false;
        }
    }
    return true;
}

bool ledger_query(const Ledger *ledger, LedgerDimensionId id, LedgerDimension *out)
{
    size_t i;
    int64_t committed = 0;
    int64_t after_reserve;
    if (ledger == NULL || out == NULL || id < LEDGER_CPU ||
        id >= LEDGER_DIMENSION_COUNT) return false;
    if (id == LEDGER_CPU) committed = ledger->self_cpu_pct;
    else if (id == LEDGER_ENERGY) committed = ledger->self_energy_mJ;
    out->capacity = ledger->config.capacity[id];
    out->protected_reserve = ledger->config.protected_reserve[id];
    if (id == LEDGER_MEMORY) committed = ledger->memory_committed;
    else if (id == LEDGER_STORAGE) committed = ledger->storage_committed;
    else {
        for (i = 0; i < SAGE_MAX_TASKS; ++i) {
            const LedgerReservation *entry = &ledger->reservations[i];
            int64_t demand = 0;
            if (!entry->in_use ||
                (id == LEDGER_ENERGY ? !entry->energy_pending : !entry->rate_active))
                continue;
            switch (id) {
            case LEDGER_CPU: demand = (int64_t)entry->demand.cpu_pct; break;
            case LEDGER_ENERGY:
                if (entry->energy_cost_mJ > INT64_MAX) return false;
                demand = (int64_t)entry->energy_cost_mJ;
                break;
            case LEDGER_SENSOR:
                committed |= (int64_t)entry->demand.sensor_mask;
                continue;
            case LEDGER_COMM: demand = (int64_t)entry->demand.comm_pct; break;
            default: break;
            }
            if (!checked_add(committed, demand, &committed)) return false;
        }
    }
    out->committed = committed;
    if (id == LEDGER_SENSOR) {
        out->free = (int64_t)((uint32_t)out->capacity &
            ~(uint32_t)out->protected_reserve & ~(uint32_t)committed);
        return true;
    }
    if (!checked_sub(out->capacity, out->protected_reserve, &after_reserve) ||
        !checked_sub(after_reserve, committed, &out->free)) return false;
    return true;
}

bool ledger_energy_config(const Ledger *ledger, uint64_t *battery_capacity_mJ,
                          uint16_t *soc_floor_permille)
{
    if (ledger == NULL || battery_capacity_mJ == NULL || soc_floor_permille == NULL)
        return false;
    *battery_capacity_mJ = ledger->config.battery_capacity_mJ;
    *soc_floor_permille = ledger->config.soc_floor_permille;
    return true;
}

bool ledger_can_reserve(const Ledger *ledger, uint8_t criticality_class,
                        ResourceDemand demand, uint64_t energy_cost_mJ)
{
    return ledger_can_reserve_masked(ledger, criticality_class, demand,
                                     energy_cost_mJ, LEDGER_MASK_ALL);
}

bool ledger_can_reserve_masked(const Ledger *ledger, uint8_t criticality_class,
    ResourceDemand demand, uint64_t energy_cost_mJ, uint8_t dimension_mask)
{
    const uint64_t requested[LEDGER_DIMENSION_COUNT] = {
        demand.cpu_pct, energy_cost_mJ, demand.mem_KB, demand.storage_KB,
        demand.sensor_mask, demand.comm_pct
    };
    int id;
    if (ledger == NULL || demand.cpu_pct > 100 || demand.comm_pct > 100 ||
        (dimension_mask & ~LEDGER_MASK_ALL) != 0U) return false;
    for (id = 0; id < LEDGER_DIMENSION_COUNT; ++id) {
        LedgerDimension state;
        int64_t allowed;
        if (!ledger_query(ledger, (LedgerDimensionId)id, &state)) return false;
        if (id == LEDGER_SENSOR) {
            uint32_t available = criticality_class == 0 ?
                (uint32_t)state.capacity & ~(uint32_t)state.committed :
                (uint32_t)state.free;
            if ((dimension_mask & (1U << (unsigned)id)) != 0U &&
                ((uint32_t)requested[id] & available) != (uint32_t)requested[id])
                return false;
            continue;
        }
        if (requested[id] > INT64_MAX) return false;
        if (!checked_add(state.committed, (int64_t)requested[id], &allowed) ||
            !checked_sub(state.capacity, state.protected_reserve, &allowed) ||
            !checked_sub(allowed, state.committed, &allowed) ||
            !checked_sub(allowed, (int64_t)requested[id], &allowed)) return false;
        if ((dimension_mask & (1U << (unsigned)id)) == 0U) continue;
        allowed = criticality_class == 0 ?
            state.capacity - state.committed : state.free;
        if ((int64_t)requested[id] > allowed) return false;
    }
    return true;
}

bool ledger_commit(Ledger *ledger, uint32_t task_id, uint8_t criticality_class,
                   ResourceDemand demand, uint64_t energy_cost_mJ)
{
    return ledger_commit_masked(ledger, task_id, criticality_class, demand,
                                 energy_cost_mJ, LEDGER_MASK_ALL);
}

bool ledger_commit_masked(Ledger *ledger, uint32_t task_id,
    uint8_t criticality_class, ResourceDemand demand, uint64_t energy_cost_mJ,
    uint8_t dimension_mask)
{
    size_t i;
    size_t vacant = SAGE_MAX_TASKS;
    if (ledger == NULL || !ledger_can_reserve_masked(ledger, criticality_class,
                         demand, energy_cost_mJ, dimension_mask)) return false;
    for (i = 0; i < SAGE_MAX_TASKS; ++i) {
        if (ledger->reservations[i].in_use &&
            ledger->reservations[i].task_id == task_id) return false;
        if (!ledger->reservations[i].in_use && vacant == SAGE_MAX_TASKS) vacant = i;
    }
    if (vacant == SAGE_MAX_TASKS) return false;
    ledger->reservations[vacant] = (LedgerReservation){
        .in_use = true, .rate_active = true,
        .stock_held = demand.mem_KB != 0 || demand.storage_KB != 0,
        .energy_pending = energy_cost_mJ != 0,
        .task_id = task_id, .demand = demand, .energy_cost_mJ = energy_cost_mJ
    };
    ledger->memory_committed += (int64_t)demand.mem_KB;
    ledger->storage_committed += (int64_t)demand.storage_KB;
    return true;
}

static LedgerReservation *find_reservation(Ledger *ledger, uint32_t task_id)
{
    size_t i;
    if (ledger == NULL) return NULL;
    for (i = 0; i < SAGE_MAX_TASKS; ++i) {
        if (ledger->reservations[i].in_use &&
            ledger->reservations[i].task_id == task_id) return &ledger->reservations[i];
    }
    return NULL;
}

bool ledger_release_rate(Ledger *ledger, uint32_t task_id)
{
    LedgerReservation *entry = find_reservation(ledger, task_id);
    if (entry == NULL || !entry->rate_active) return false;
    entry->rate_active = false;
    if (!entry->stock_held && !entry->energy_pending) entry->in_use = false;
    return true;
}

bool ledger_release_stock(Ledger *ledger, uint32_t task_id)
{
    LedgerReservation *entry = find_reservation(ledger, task_id);
    if (entry == NULL || !entry->stock_held) return false;
    ledger->memory_committed -= (int64_t)entry->demand.mem_KB;
    ledger->storage_committed -= (int64_t)entry->demand.storage_KB;
    entry->stock_held = false;
    if (!entry->rate_active && !entry->energy_pending) entry->in_use = false;
    return true;
}

bool ledger_cancel_tentative(Ledger *ledger, uint32_t task_id)
{
    LedgerReservation *entry = find_reservation(ledger, task_id);
    if (entry == NULL) return false;
    if (entry->stock_held) {
        ledger->memory_committed -= (int64_t)entry->demand.mem_KB;
        ledger->storage_committed -= (int64_t)entry->demand.storage_KB;
    }
    *entry = (LedgerReservation){0};
    return true;
}

bool ledger_has_reservation(const Ledger *ledger, uint32_t task_id)
{
    size_t i;
    if (ledger == NULL) return false;
    for (i = 0; i < SAGE_MAX_TASKS; ++i)
        if (ledger->reservations[i].in_use &&
            ledger->reservations[i].task_id == task_id) return true;
    return false;
}
