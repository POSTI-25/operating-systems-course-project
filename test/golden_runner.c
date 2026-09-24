#include "sim/simulator.h"

#include <inttypes.h>
#include <stdio.h>

static const char *subsystem_name(Subsystem subsystem)
{
    switch (subsystem) {
    case SUBSYSTEM_BATTERY: return "BATTERY";
    case SUBSYSTEM_BUS: return "BUS";
    case SUBSYSTEM_PAYLOAD: return "PAYLOAD";
    case SUBSYSTEM_ADCS: return "ADCS";
    default: return NULL;
    }
}

static bool print_frame(uint64_t time, const TelemetryFrame *frame)
{
    int id;
    if (printf("FRAME t=%" PRIu64, time) < 0) return false;
    for (id = 0; id < TELEMETRY_CHANNEL_COUNT; ++id) {
        unsigned count = id == TELEMETRY_TEMP_MC ? N_SUBSYSTEMS : 1U;
        unsigned index;
        for (index = 0; index < count; ++index) {
            TelemetryChannel channel;
            const char *name = telemetry_channel_name((TelemetryChannelId)id);
            if (!telemetry_frame_get(frame, (TelemetryChannelId)id, index, &channel)) return false;
            if (printf(" %s", name) < 0) return false;
            if (id == TELEMETRY_TEMP_MC && printf("[%u]", index) < 0) return false;
            if (id == TELEMETRY_FAULT_FLAGS || id == TELEMETRY_SENSOR_AVAIL_MASK) {
                if (printf("=0x%08" PRIx32, (uint32_t)channel.value) < 0) return false;
            } else if (printf("=%" PRId64, channel.value) < 0) return false;
            if (printf("@%" PRIu64 ":%s", channel.timestamp_ns,
                       telemetry_quality_name(channel.quality)) < 0) return false;
        }
    }
    return putchar('\n') != EOF;
}

int main(int argc, char **argv)
{
    FILE *input;
    Scenario scenario;
    PlatformClock clock;
    Simulator sim;
    TelemetryFrame frame;
    uint64_t time;
    char error[128];
    SimulatorResult result;
    if (argc != 2) {
        fprintf(stderr, "usage: golden_runner <scenario.scn>\n");
        return 2;
    }
    input = fopen(argv[1], "r");
    if (input == NULL) { perror(argv[1]); return 2; }
    if (!scenario_load_file(input, &scenario, error, sizeof(error))) {
        fprintf(stderr, "%s: %s\n", argv[1], error);
        (void)fclose(input);
        return 2;
    }
    (void)fclose(input);
    for (size_t i = 0; i < scenario.tasks.count; ++i) {
        const Task *task = &scenario.tasks.items[i];
        const char *name = subsystem_name(task->primary_subsystem);
        if (name == NULL || printf("TASK id=%" PRIu32 " primary_subsystem=%s\n",
                                   task->task_id, name) < 0) return 2;
    }
    if (!platform_clock_init(&clock, PLATFORM_CLOCK_VIRTUAL, 0) ||
        !simulator_init(&sim, &scenario, &clock)) return 2;
    while ((result = simulator_next(&sim, &frame, &time)) == SIMULATOR_FRAME) {
        if (!print_frame(time, &frame)) return 2;
    }
    return result == SIMULATOR_DONE && !ferror(stdout) ? 0 : 2;
}
