#ifndef SAGE_LEDGER_H
#define SAGE_LEDGER_H

#include "task_model.h"

#include <stdbool.h>
#include <stdint.h>

#define SAGE_PROVISIONAL_SELF_COST_CPU_PCT 0U
#define SAGE_PROVISIONAL_SELF_COST_ENERGY_MJ 0U

typedef enum {
    LEDGER_CPU = 0,
    LEDGER_ENERGY = 1,
    LEDGER_MEMORY = 2,
    LEDGER_STORAGE = 3,
    LEDGER_SENSOR = 4,
    LEDGER_COMM = 5,
    LEDGER_DIMENSION_COUNT = 6
} LedgerDimensionId;

typedef struct {
    int64_t capacity;
    int64_t protected_reserve;
    int64_t committed;
    int64_t free;
} LedgerDimension;

typedef struct {
    int64_t capacity[LEDGER_DIMENSION_COUNT];
    int64_t protected_reserve[LEDGER_DIMENSION_COUNT];
    uint64_t battery_capacity_mJ;
    uint16_t soc_floor_permille;
} LedgerConfig;

typedef struct {
    bool in_use;
    bool rate_active;
    bool stock_held;
    uint32_t task_id;
    ResourceDemand demand;
    uint64_t energy_cost_mJ;
} LedgerReservation;

typedef struct {
    LedgerConfig config;
    LedgerReservation reservations[SAGE_MAX_TASKS];
    int64_t memory_committed;
    int64_t storage_committed;
} Ledger;

bool ledger_init(Ledger *ledger, const LedgerConfig *config);
bool ledger_set_rate_capacities(Ledger *ledger, uint16_t cpu_pct, uint16_t comm_pct);
bool ledger_update_energy(Ledger *ledger, uint16_t soc_permille,
                          uint64_t predicted_harvest_mJ,
                          uint64_t other_planned_draws_mJ);
bool ledger_query(const Ledger *ledger, LedgerDimensionId id, LedgerDimension *out);
bool ledger_energy_config(const Ledger *ledger, uint64_t *battery_capacity_mJ,
                          uint16_t *soc_floor_permille);
bool ledger_can_reserve(const Ledger *ledger, uint8_t criticality_class,
                        ResourceDemand demand, uint64_t energy_cost_mJ);
bool ledger_commit(Ledger *ledger, uint32_t task_id, uint8_t criticality_class,
                   ResourceDemand demand, uint64_t energy_cost_mJ);
bool ledger_release_rate(Ledger *ledger, uint32_t task_id);
bool ledger_release_stock(Ledger *ledger, uint32_t task_id);

#endif
