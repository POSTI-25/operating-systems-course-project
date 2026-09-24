#include "scenario.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>

#define MAX_TOKENS 16
#define INITIAL_BITS 15U

static bool fail(char *error, size_t size, size_t line, const char *message)
{
    if (error != NULL && size > 0) {
        (void)snprintf(error, size, "line %zu: %s", line, message);
    }
    return false;
}

static bool decimal_within(const char *text, const char *maximum)
{
    const char *digits = text;
    size_t length;
    size_t limit = strlen(maximum);
    if (*digits == '\0') return false;
    for (const char *p = digits; *p != '\0'; ++p) {
        if (!isdigit((unsigned char)*p)) return false;
    }
    while (*digits == '0' && digits[1] != '\0') ++digits;
    length = strlen(digits);
    if (length > limit || (length == limit && strcmp(digits, maximum) > 0)) return false;
    return true;
}

static bool parse_u64(const char *text, uint64_t *value, bool mask)
{
    unsigned long long parsed;
    int consumed = 0;
    if (mask && text[0] == '0' && text[1] == 'x') {
        const char *digits = text + 2;
        if (*digits == '\0') return false;
        for (const char *p = digits; *p != '\0'; ++p) {
            if (!isxdigit((unsigned char)*p)) return false;
        }
        while (*digits == '0' && digits[1] != '\0') ++digits;
        if (strlen(digits) > 8 || sscanf(text, "%llx%n", &parsed, &consumed) != 1 ||
            text[consumed] != '\0') return false;
    } else {
        if (!decimal_within(text, "18446744073709551615") ||
            sscanf(text, "%llu%n", &parsed, &consumed) != 1 || text[consumed] != '\0')
            return false;
    }
    *value = (uint64_t)parsed;
    return true;
}

static bool parse_i64(const char *text, int64_t *value)
{
    long long parsed;
    int consumed = 0;
    bool negative = text[0] == '-';
    if (!decimal_within(text + (negative ? 1 : 0),
                        negative ? "9223372036854775808" : "9223372036854775807") ||
        sscanf(text, "%lld%n", &parsed, &consumed) != 1 || text[consumed] != '\0')
        return false;
    *value = (int64_t)parsed;
    return true;
}

static bool parse_quality(const char *token, TelemetryQuality *quality)
{
    if (strcmp(token, "OK") == 0) *quality = TELEMETRY_QUALITY_OK;
    else if (strcmp(token, "STALE") == 0) *quality = TELEMETRY_QUALITY_STALE;
    else if (strcmp(token, "MISSING") == 0) *quality = TELEMETRY_QUALITY_MISSING;
    else return false;
    return true;
}

static bool parse_channel(char **tokens, size_t count, size_t start,
                          TelemetryChannelId *id, unsigned *index,
                          TelemetryChannel *channel)
{
    size_t pos = start;
    uint64_t number;
    if (pos >= count || !telemetry_channel_from_name(tokens[pos++], id)) return false;
    *index = 0;
    if (*id == TELEMETRY_TEMP_MC) {
        if (pos >= count || !parse_u64(tokens[pos++], &number, false) ||
            number >= N_SUBSYSTEMS) return false;
        *index = (unsigned)number;
    }
    if (count != pos + 3) return false;
    if (*id == TELEMETRY_FAULT_FLAGS || *id == TELEMETRY_SENSOR_AVAIL_MASK) {
        if (!parse_u64(tokens[pos++], &number, true) || number > UINT32_MAX) return false;
        channel->value = (int64_t)number;
    } else if (!parse_i64(tokens[pos++], &channel->value)) return false;
    if (!parse_u64(tokens[pos++], &channel->timestamp_ns, false) ||
        !parse_quality(tokens[pos], &channel->quality)) return false;
    return true;
}

static bool parse_task(char **tokens, size_t count, TaskTable *table)
{
    Task task = {0};
    uint64_t v[6];
    size_t i;
    if (count != 8) return false;
    for (i = 0; i < 6; ++i) {
        if (!parse_u64(tokens[i + 1], &v[i], false)) return false;
    }
    if (v[0] > UINT32_MAX || v[4] > UINT8_MAX || v[5] > 1000) return false;
    task.task_id = (uint32_t)v[0];
    task.period_ns = v[1];
    task.deadline_ns = v[2];
    task.nominal_exec_ns = v[3];
    task.criticality_class = (uint8_t)v[4];
    task.mission_value = (uint16_t)v[5];
    if (strcmp(tokens[7], "BATTERY") == 0) task.primary_subsystem = SUBSYSTEM_BATTERY;
    else if (strcmp(tokens[7], "BUS") == 0) task.primary_subsystem = SUBSYSTEM_BUS;
    else if (strcmp(tokens[7], "PAYLOAD") == 0) task.primary_subsystem = SUBSYSTEM_PAYLOAD;
    else if (strcmp(tokens[7], "ADCS") == 0) task.primary_subsystem = SUBSYSTEM_ADCS;
    else return false;
    return task_table_add(table, &task);
}

static bool parse_mode(char **tokens, size_t count, TaskTable *table)
{
    uint64_t values[9];
    int64_t thermal;
    TaskExecutingMode mode;
    TaskModeProfile profile = {0};
    size_t i;
    if (count != 11 || !parse_u64(tokens[1], &values[0], false) ||
        values[0] > UINT32_MAX) return false;
    if (strcmp(tokens[2], "FULL") == 0) mode = TASK_MODE_FULL;
    else if (strcmp(tokens[2], "THROTTLED") == 0) mode = TASK_MODE_THROTTLED;
    else if (strcmp(tokens[2], "DEGRADED") == 0) mode = TASK_MODE_DEGRADED;
    else return false;
    if (!parse_u64(tokens[3], &values[1], false) ||
        !parse_i64(tokens[4], &thermal) || thermal < INT32_MIN || thermal > INT32_MAX) return false;
    for (i = 5; i <= 10; ++i) {
        if (!parse_u64(tokens[i], &values[i - 3], i == 9)) return false;
    }
    if (values[2] > 1000 || values[3] > 100 || values[4] > UINT32_MAX ||
        values[5] > UINT32_MAX || values[6] > UINT32_MAX || values[7] > 100) return false;
    profile.energy_cost_mJ = values[1];
    profile.thermal_effect_mC = (int32_t)thermal;
    profile.value_fraction_permille = (uint16_t)values[2];
    profile.resource_demand.cpu_pct = (uint16_t)values[3];
    profile.resource_demand.mem_KB = (uint32_t)values[4];
    profile.resource_demand.storage_KB = (uint32_t)values[5];
    profile.resource_demand.sensor_mask = (uint32_t)values[6];
    profile.resource_demand.comm_pct = (uint16_t)values[7];
    return task_table_set_mode(table, (uint32_t)values[0], mode, profile);
}

bool scenario_load_file(FILE *input, Scenario *scenario,
                        char *error, size_t error_size)
{
    char line[SAGE_SCENARIO_LINE_MAX];
    char *tokens[MAX_TOKENS];
    size_t line_no = 0;
    unsigned initial_seen = 0;
    bool name_seen = false, duration_seen = false, step_seen = false;
    bool capacity_seen = false, floor_seen = false;
    if (input == NULL || scenario == NULL) return fail(error, error_size, 0, "null argument");
    memset(scenario, 0, sizeof(*scenario));
    while (fgets(line, sizeof(line), input) != NULL) {
        size_t count = 0;
        char *token;
        ++line_no;
        if (strchr(line, '\n') == NULL && !feof(input)) return fail(error, error_size, line_no, "line too long");
        token = strtok(line, " \t\r\n");
        while (token != NULL && token[0] != '#') {
            if (count == MAX_TOKENS) return fail(error, error_size, line_no, "too many tokens");
            tokens[count++] = token;
            token = strtok(NULL, " \t\r\n");
        }
        if (count == 0) continue;
        if (strcmp(tokens[0], "SCENARIO") == 0) {
            size_t i;
            if (count != 2 || name_seen || strlen(tokens[1]) >= sizeof(scenario->name))
                return fail(error, error_size, line_no, "invalid SCENARIO");
            for (i = 0; tokens[1][i] != '\0'; ++i) {
                unsigned char c = (unsigned char)tokens[1][i];
                if (!(isalnum(c) || c == '_' || c == '-'))
                    return fail(error, error_size, line_no, "invalid SCENARIO name");
            }
            if (i == 0) return fail(error, error_size, line_no, "empty SCENARIO name");
            (void)strcpy(scenario->name, tokens[1]);
            name_seen = true;
        } else if (strcmp(tokens[0], "DURATION_NS") == 0) {
            if (count != 2 || duration_seen || !parse_u64(tokens[1], &scenario->duration_ns, false) ||
                scenario->duration_ns == 0) return fail(error, error_size, line_no, "invalid DURATION_NS");
            duration_seen = true;
        } else if (strcmp(tokens[0], "STEP_NS") == 0) {
            if (count != 2 || step_seen || !parse_u64(tokens[1], &scenario->step_ns, false) ||
                scenario->step_ns == 0) return fail(error, error_size, line_no, "invalid STEP_NS");
            step_seen = true;
        } else if (strcmp(tokens[0], "BATTERY_CAPACITY_MJ") == 0) {
            if (count != 2 || capacity_seen ||
                !parse_u64(tokens[1], &scenario->battery_capacity_mJ, false) ||
                scenario->battery_capacity_mJ == 0 ||
                scenario->battery_capacity_mJ > INT64_MAX)
                return fail(error, error_size, line_no, "invalid BATTERY_CAPACITY_MJ");
            capacity_seen = true;
        } else if (strcmp(tokens[0], "SOC_FLOOR_PERMILLE") == 0) {
            uint64_t floor;
            if (count != 2 || floor_seen || !parse_u64(tokens[1], &floor, false) ||
                floor > 1000)
                return fail(error, error_size, line_no, "invalid SOC_FLOOR_PERMILLE");
            scenario->soc_floor_permille = (uint16_t)floor;
            floor_seen = true;
        } else if (strcmp(tokens[0], "ORBIT") == 0) {
            if (count != 5 || scenario->has_orbit ||
                !parse_u64(tokens[1], &scenario->orbit_epoch_ns, false) ||
                !parse_u64(tokens[2], &scenario->orbit_period_ns, false) ||
                !parse_u64(tokens[3], &scenario->eclipse_offset_ns, false) ||
                !parse_u64(tokens[4], &scenario->eclipse_duration_ns, false) ||
                scenario->orbit_period_ns == 0 || scenario->eclipse_duration_ns == 0 ||
                scenario->eclipse_offset_ns >= scenario->orbit_period_ns ||
                scenario->eclipse_duration_ns > scenario->orbit_period_ns - scenario->eclipse_offset_ns)
                return fail(error, error_size, line_no, "invalid ORBIT");
            scenario->has_orbit = true;
        } else if (strcmp(tokens[0], "INITIAL") == 0 || strcmp(tokens[0], "UPDATE") == 0) {
            bool initial = strcmp(tokens[0], "INITIAL") == 0;
            uint64_t time = 0;
            TelemetryChannelId id;
            unsigned index;
            TelemetryChannel channel;
            TelemetryFrame validation_frame;
            unsigned bit;
            size_t start = initial ? 1 : 2;
            if ((!initial && (count < 3 || !parse_u64(tokens[1], &time, false))) ||
                !parse_channel(tokens, count, start, &id, &index, &channel))
                return fail(error, error_size, line_no, "invalid telemetry directive");
            validation_frame = scenario->initial;
            if (!telemetry_frame_set(&validation_frame, id, index, channel))
                return fail(error, error_size, line_no, "invalid telemetry value");
            if (initial) {
                bit = id == TELEMETRY_TEMP_MC ? (1U << (4U + index)) :
                    (1U << (unsigned)(id < TELEMETRY_TEMP_MC ? id : id + 3));
                if ((initial_seen & bit) != 0 || channel.timestamp_ns != 0)
                    return fail(error, error_size, line_no, "duplicate or nonzero INITIAL timestamp");
                initial_seen |= bit;
                scenario->initial = validation_frame;
            } else {
                if (scenario->update_count >= SAGE_MAX_UPDATES)
                    return fail(error, error_size, line_no, "invalid UPDATE");
                scenario->updates[scenario->update_count++] =
                    (ScenarioUpdate){ time, id, index, channel };
            }
        } else if (strcmp(tokens[0], "FAULT") == 0) {
            uint64_t time, set, clear;
            if (count != 4 || scenario->fault_count >= SAGE_MAX_FAULTS ||
                !parse_u64(tokens[1], &time, false) ||
                !parse_u64(tokens[2], &set, true) || !parse_u64(tokens[3], &clear, true) ||
                set > UINT32_MAX || clear > UINT32_MAX || (set & clear) != 0)
                return fail(error, error_size, line_no, "invalid FAULT");
            scenario->faults[scenario->fault_count++] =
                (ScenarioFault){ time, (uint32_t)set, (uint32_t)clear };
        } else if (strcmp(tokens[0], "TASK") == 0) {
            if (!parse_task(tokens, count, &scenario->tasks))
                return fail(error, error_size, line_no, "invalid TASK");
        } else if (strcmp(tokens[0], "TASK_MODE") == 0) {
            if (!parse_mode(tokens, count, &scenario->tasks))
                return fail(error, error_size, line_no, "invalid TASK_MODE");
        } else return fail(error, error_size, line_no, "unknown directive");
    }
    if (ferror(input)) return fail(error, error_size, line_no, "read error");
    if (!name_seen || !duration_seen || !step_seen || !capacity_seen || !floor_seen ||
        initial_seen != ((1U << INITIAL_BITS) - 1U) ||
        !task_table_complete(&scenario->tasks)) return fail(error, error_size, line_no, "incomplete scenario");
    for (size_t i = 0; i < scenario->update_count; ++i) {
        if (scenario->updates[i].time_ns > scenario->duration_ns ||
            (scenario->has_orbit && scenario->updates[i].id == TELEMETRY_ECLIPSE))
            return fail(error, error_size, line_no, "UPDATE conflicts with duration or ORBIT");
    }
    for (size_t i = 0; i < scenario->fault_count; ++i) {
        if (scenario->faults[i].time_ns > scenario->duration_ns)
            return fail(error, error_size, line_no, "FAULT beyond duration");
    }
    if (error != NULL && error_size > 0) error[0] = '\0';
    return true;
}
