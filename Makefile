CC ?= gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Werror -Isrc -Itest
SANFLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer
CORE_SRC = src/platform/platform_clock.c src/platform/platform_wait.c src/platform/platform_ipc.c src/core/telemetry.c src/core/task_model.c src/core/ledger.c src/core/gate.c src/core/mode_generator.c src/core/utility.c src/core/prediction.c src/core/governor.c src/sim/scenario.c src/sim/simulator.c
UNIT_NAMES = platform_clock telemetry task_model scenario simulator ledger gate mode_generator utility prediction governor platform_ipc governor_run
UNIT_BINS = $(addprefix build/test_,$(UNIT_NAMES))
SAN_BINS = $(addprefix build/san_test_,$(UNIT_NAMES))

.PHONY: all test sanitize clean

all: build/sage_rt build/golden_runner build/prediction_ab $(UNIT_BINS) build/measure_self_cost

build:
	mkdir -p build

build/sage_rt: src/main.c $(CORE_SRC) | build
	$(CC) $(CFLAGS) $^ -o $@

build/test_%: test/test_%.c $(CORE_SRC) | build
	$(CC) $(CFLAGS) $^ -o $@

build/san_test_%: test/test_%.c $(CORE_SRC) | build
	$(CC) $(CFLAGS) $(SANFLAGS) $^ -o $@

build/golden_runner: test/golden_runner.c $(CORE_SRC) | build
	$(CC) $(CFLAGS) $^ -o $@

build/san_golden_runner: test/golden_runner.c $(CORE_SRC) | build
	$(CC) $(CFLAGS) $(SANFLAGS) $^ -o $@

build/san_sage_rt: src/main.c $(CORE_SRC) | build
	$(CC) $(CFLAGS) $(SANFLAGS) $^ -o $@

build/san_prediction_ab: test/prediction_ab.c $(CORE_SRC) | build
	$(CC) $(CFLAGS) $(SANFLAGS) $^ -o $@

build/measure_self_cost: test/measure_self_cost.c $(CORE_SRC) | build
	$(CC) $(CFLAGS) $^ -o $@

build/prediction_ab: test/prediction_ab.c $(CORE_SRC) | build
	$(CC) $(CFLAGS) $^ -o $@

test: all
	@set -e; for binary in $(UNIT_BINS); do ./$$binary; done
	@set -e; actual=$$(mktemp); trap 'rm -f "$$actual"' EXIT; ./build/golden_runner scenarios/normal_orbit.scn > "$$actual"; diff -u golden/normal_orbit.expected "$$actual"
	@./build/prediction_ab
	@./build/prediction_ab scenarios/prediction_trend.scn
	@./build/sage_rt scenarios/normal_orbit.scn --tick-ms 1 | grep '^SUMMARY '

sanitize: $(SAN_BINS) build/san_golden_runner build/san_prediction_ab build/san_sage_rt
	@set -e; for binary in $(SAN_BINS); do ./$$binary; done
	@set -e; actual=$$(mktemp); trap 'rm -f "$$actual"' EXIT; ./build/san_golden_runner scenarios/normal_orbit.scn > "$$actual"; diff -u golden/normal_orbit.expected "$$actual"
	@./build/san_prediction_ab scenarios/prediction_trend.scn
	@./build/san_sage_rt scenarios/normal_orbit.scn --tick-ms 1 | grep '^SUMMARY '

clean:
	rm -f build/sage_rt build/san_sage_rt build/golden_runner build/san_golden_runner build/measure_self_cost build/prediction_ab build/san_prediction_ab $(UNIT_BINS) $(SAN_BINS)
