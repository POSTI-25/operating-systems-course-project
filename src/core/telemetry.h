#ifndef SAGE_TELEMETRY_H
#define SAGE_TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SUBSYSTEM_BATTERY = 0,
    SUBSYSTEM_BUS = 1,
    SUBSYSTEM_PAYLOAD = 2,
    SUBSYSTEM_ADCS = 3,
    N_SUBSYSTEMS = 4
} Subsystem;

typedef enum {
    TELEMETRY_QUALITY_OK = 0,
    TELEMETRY_QUALITY_STALE = 1,
    TELEMETRY_QUALITY_MISSING = 2
} TelemetryQuality;

typedef enum {
    TELEMETRY_BATTERY_SOC_MV_PCT = 0,
    TELEMETRY_CHARGE_RATE_MA,
    TELEMETRY_SOLAR_INPUT_MW,
    TELEMETRY_ECLIPSE,
    TELEMETRY_TEMP_MC,
    TELEMETRY_CURRENT_DRAW_MA,
    TELEMETRY_FAULT_FLAGS,
    TELEMETRY_COMM_WINDOW_OPEN,
    TELEMETRY_CPU_AVAIL_PCT,
    TELEMETRY_MEM_AVAIL_KB,
    TELEMETRY_STORAGE_AVAIL_KB,
    TELEMETRY_SENSOR_AVAIL_MASK,
    TELEMETRY_CHANNEL_COUNT
} TelemetryChannelId;

typedef struct {
    int64_t value;
    uint64_t timestamp_ns;
    TelemetryQuality quality;
} TelemetryChannel;

typedef struct {
    TelemetryChannel battery_soc_mV_pct;
    TelemetryChannel charge_rate_mA;
    TelemetryChannel solar_input_mW;
    TelemetryChannel eclipse;
    TelemetryChannel temp_mC[N_SUBSYSTEMS];
    TelemetryChannel current_draw_mA;
    TelemetryChannel fault_flags;
    TelemetryChannel comm_window_open;
    TelemetryChannel cpu_avail_pct;
    TelemetryChannel mem_avail_KB;
    TelemetryChannel storage_avail_KB;
    TelemetryChannel sensor_avail_mask;
} TelemetryFrame;

void telemetry_frame_init(TelemetryFrame *frame);
bool telemetry_frame_set(TelemetryFrame *frame, TelemetryChannelId id,
                         unsigned index, TelemetryChannel channel);
bool telemetry_frame_get(const TelemetryFrame *frame, TelemetryChannelId id,
                         unsigned index, TelemetryChannel *channel);
const char *telemetry_channel_name(TelemetryChannelId id);
bool telemetry_channel_from_name(const char *name, TelemetryChannelId *id);
const char *telemetry_quality_name(TelemetryQuality quality);

#endif
