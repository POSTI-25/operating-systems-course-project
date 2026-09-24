#include "telemetry.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static TelemetryChannel *channel_at(TelemetryFrame *frame, TelemetryChannelId id,
                                    unsigned index)
{
    if (frame == NULL || (id != TELEMETRY_TEMP_MC && index != 0)) return NULL;
    switch (id) {
    case TELEMETRY_BATTERY_SOC_MV_PCT: return &frame->battery_soc_mV_pct;
    case TELEMETRY_CHARGE_RATE_MA: return &frame->charge_rate_mA;
    case TELEMETRY_SOLAR_INPUT_MW: return &frame->solar_input_mW;
    case TELEMETRY_ECLIPSE: return &frame->eclipse;
    case TELEMETRY_TEMP_MC: return index < N_SUBSYSTEMS ? &frame->temp_mC[index] : NULL;
    case TELEMETRY_CURRENT_DRAW_MA: return &frame->current_draw_mA;
    case TELEMETRY_FAULT_FLAGS: return &frame->fault_flags;
    case TELEMETRY_COMM_WINDOW_OPEN: return &frame->comm_window_open;
    case TELEMETRY_CPU_AVAIL_PCT: return &frame->cpu_avail_pct;
    case TELEMETRY_MEM_AVAIL_KB: return &frame->mem_avail_KB;
    case TELEMETRY_STORAGE_AVAIL_KB: return &frame->storage_avail_KB;
    case TELEMETRY_SENSOR_AVAIL_MASK: return &frame->sensor_avail_mask;
    default: return NULL;
    }
}

static bool valid_value(TelemetryChannelId id, int64_t value)
{
    switch (id) {
    case TELEMETRY_ECLIPSE:
    case TELEMETRY_COMM_WINDOW_OPEN: return value == 0 || value == 1;
    case TELEMETRY_CPU_AVAIL_PCT: return value >= 0 && value <= 100;
    case TELEMETRY_FAULT_FLAGS:
    case TELEMETRY_SENSOR_AVAIL_MASK: return value >= 0 && value <= UINT32_MAX;
    case TELEMETRY_BATTERY_SOC_MV_PCT: return value >= 0 && value <= 1000;
    case TELEMETRY_SOLAR_INPUT_MW:
    case TELEMETRY_MEM_AVAIL_KB:
    case TELEMETRY_STORAGE_AVAIL_KB: return value >= 0;
    default: return true;
    }
}

void telemetry_frame_init(TelemetryFrame *frame)
{
    if (frame != NULL) memset(frame, 0, sizeof(*frame));
}

bool telemetry_frame_set(TelemetryFrame *frame, TelemetryChannelId id,
                         unsigned index, TelemetryChannel channel)
{
    TelemetryChannel *slot = channel_at(frame, id, index);
    if (slot == NULL || channel.quality > TELEMETRY_QUALITY_MISSING ||
        !valid_value(id, channel.value)) return false;
    *slot = channel;
    return true;
}

bool telemetry_frame_get(const TelemetryFrame *frame, TelemetryChannelId id,
                         unsigned index, TelemetryChannel *channel)
{
    TelemetryChannel *slot;
    if (channel == NULL) return false;
    slot = channel_at((TelemetryFrame *)frame, id, index);
    if (slot == NULL) return false;
    *channel = *slot;
    return true;
}

const char *telemetry_channel_name(TelemetryChannelId id)
{
    static const char *const names[TELEMETRY_CHANNEL_COUNT] = {
        "BATTERY_SOC_MV_PCT", "CHARGE_RATE_MA", "SOLAR_INPUT_MW", "ECLIPSE",
        "TEMP_MC", "CURRENT_DRAW_MA", "FAULT_FLAGS", "COMM_WINDOW_OPEN",
        "CPU_AVAIL_PCT", "MEM_AVAIL_KB", "STORAGE_AVAIL_KB", "SENSOR_AVAIL_MASK"
    };
    if (id < 0 || id >= TELEMETRY_CHANNEL_COUNT) return NULL;
    return names[id];
}

bool telemetry_channel_from_name(const char *name, TelemetryChannelId *id)
{
    int i;
    if (name == NULL || id == NULL) return false;
    for (i = 0; i < TELEMETRY_CHANNEL_COUNT; ++i) {
        if (strcmp(name, telemetry_channel_name((TelemetryChannelId)i)) == 0) {
            *id = (TelemetryChannelId)i;
            return true;
        }
    }
    return false;
}

const char *telemetry_quality_name(TelemetryQuality quality)
{
    switch (quality) {
    case TELEMETRY_QUALITY_OK: return "OK";
    case TELEMETRY_QUALITY_STALE: return "STALE";
    case TELEMETRY_QUALITY_MISSING: return "MISSING";
    default: return NULL;
    }
}
