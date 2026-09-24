#include "test.h"
#include "platform/platform_ipc.h"

static void forked_simulator_sends_frames_then_fault_event(void)
{
    static Scenario scenario;
    FILE *input = fopen("scenarios/normal_orbit.scn", "r");
    char error[128];
    SimulatorPipe stream;
    PlatformIpcMessage message;
    size_t frames = 0;
    size_t faults = 0;
    uint64_t last_frame_time = 0;
    ASSERT_TRUE(input != NULL);
    ASSERT_TRUE(scenario_load_file(input, &scenario, error, sizeof(error)));
    fclose(input);
    scenario.fault_count = 1;
    scenario.faults[0].time_ns = 3;
    scenario.faults[0].set_mask = 0x08U;
    ASSERT_TRUE(platform_ipc_spawn(&stream, &scenario, 0));
    for (;;) {
        ASSERT_TRUE(platform_ipc_read_message(&stream, &message));
        if (message.type == PLATFORM_IPC_DONE) break;
        if (message.type == PLATFORM_IPC_FRAME) {
            last_frame_time = message.sim_time_ns;
            ++frames;
        } else if (message.type == PLATFORM_IPC_FAULT_ESCALATION) {
            ASSERT_EQ_INT(3, message.sim_time_ns);
            ASSERT_EQ_INT(3, last_frame_time);
            ASSERT_EQ_INT(0x08, message.fault_set_mask);
            ++faults;
        } else ASSERT_TRUE(false);
    }
    ASSERT_TRUE(frames > 4);
    ASSERT_EQ_INT(1, faults);
    ASSERT_TRUE(platform_ipc_close(&stream));
}

int main(void)
{
    RUN_TEST(forked_simulator_sends_frames_then_fault_event);
    return test_failures != 0;
}
