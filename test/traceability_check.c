#include "test.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool file_has(const char *path, const char *token)
{
    FILE *input = fopen(path, "r");
    char line[4096];
    bool found = false;
    if (input == NULL) return false;
    while (fgets(line, sizeof(line), input) != NULL)
        if (strstr(line, token) != NULL) found = true;
    fclose(input);
    return found;
}

static bool evidence_has(const char *id, const char *scenario, const char *token)
{
    char path[128];
    if (snprintf(path, sizeof(path), "golden/%s.decisions.expected", scenario) < 0 ||
        !file_has(path, token)) {
        fprintf(stderr, "%s missing %s in %s\n", id, token, path);
        return false;
    }
    return true;
}

static bool report_number(const char *scenario, const char *variant,
                          size_t field, uint64_t *out)
{
    FILE *input = fopen("docs/phase5_metrics.csv", "r");
    char line[4096];
    bool found = false;
    if (input == NULL || out == NULL) return false;
    while (fgets(line, sizeof(line), input) != NULL) {
        char *cell;
        char *scenario_cell = strtok(line, ",");
        char *variant_cell = strtok(NULL, ",");
        size_t index = 2;
        char *end;
        unsigned long long value;
        if (scenario_cell == NULL || variant_cell == NULL ||
            strcmp(scenario_cell, scenario) != 0 ||
            strcmp(variant_cell, variant) != 0) continue;
        while (index <= field && (cell = strtok(NULL, ",")) != NULL) {
            if (index == field) {
                value = strtoull(cell, &end, 10);
                if (end != cell && (*end == ',' || *end == '\n' || *end == '\0')) {
                    *out = (uint64_t)value;
                    found = true;
                }
                break;
            }
            ++index;
        }
        break;
    }
    fclose(input);
    return found;
}

static bool metric_less(const char *scenario, const char *left,
                        const char *right, size_t field)
{
    uint64_t left_value, right_value;
    return report_number(scenario, left, field, &left_value) &&
        report_number(scenario, right, field, &right_value) &&
        left_value < right_value;
}

static bool evidence_index(void)
{
    int number;
    char token[16];
    for (number = 1; number <= 10; ++number) {
        (void)snprintf(token, sizeof(token), "| N%d |", number);
        if (!file_has("docs/phase5_evidence.md", token)) {
            fprintf(stderr, "N%d missing from evidence index\n", number);
            return false;
        }
    }
    return true;
}

static void novelty_items_have_named_end_to_end_evidence(void)
{
    uint64_t cap;
    bool all = true;
    all &= evidence_has("N1", "low_soc_eclipse", "FULL/ELIMINATED/GATE_SOC_FLOOR") &&
        evidence_has("N1", "low_soc_eclipse", "selected=DEGRADED");
    all &= evidence_has("N2", "communication_blackout",
                        "FULL/ELIMINATED/GATE_PROTECTED_RESIDUAL") &&
        evidence_has("N2", "communication_blackout", "selected=DEGRADED");
    all &= metric_less("compound_stress", "SAGE_FULL", "FIXED_EDF", 14) &&
        metric_less("compound_stress", "SAGE_FULL", "GOMX_STYLE", 14);
    all &= evidence_has("N4", "false_alarm_restoration", "restoration=1");
    all &= evidence_has("N5", "high_thermal_load", "selected=THROTTLED") &&
        evidence_has("N5", "low_soc_eclipse", "selected=DEGRADED");
    all &= report_number("normal_orbit", "SAGE_FULL", 21, &cap) && cap > 0;
    all &= metric_less("sudden_fault", "SAGE_FULL", "GOMX_STYLE", 17);
    all &= metric_less("high_thermal_load", "SAGE_FULL", "GOMX_STYLE", 11) &&
        metric_less("low_soc_eclipse", "SAGE_FULL", "RANJBAR_STYLE", 11);
    all &= evidence_has("N9", "low_soc_eclipse", "GATE_SOC_FLOOR") &&
        file_has("PRD.md", "smooth SOC trend");
    all &= evidence_has("N10", "compound_stress", "EMERGENCY_SAFE_MODE") &&
        evidence_has("N10", "compound_stress", "GATE_PROTECTED_RESIDUAL");
    all &= evidence_index();
    ASSERT_TRUE(all);
}

int main(void)
{
    RUN_TEST(novelty_items_have_named_end_to_end_evidence);
    return test_failures != 0;
}
