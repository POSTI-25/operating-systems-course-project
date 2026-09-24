CC ?= gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Werror -Isrc -Itest
SANFLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer
CORE_SRC = src/platform/platform_clock.c src/core/telemetry.c src/core/task_model.c src/core/ledger.c src/core/gate.c src/core/mode_generator.c src/core/utility.c src/sim/scenario.c src/sim/simulator.c
UNIT_NAMES = platform_clock telemetry task_model scenario simulator ledger gate mode_generator utility
UNIT_BINS = $(addprefix build/test_,$(UNIT_NAMES))
SAN_BINS = $(addprefix build/san_test_,$(UNIT_NAMES))

.PHONY: all test sanitize clean

all: build/sage_rt build/golden_runner $(UNIT_BINS) build/measure_self_cost

build:
	mkdir -p build

build/sage_rt: src/main.c | build
	$(CC) $(CFLAGS) $< -o $@

build/test_%: test/test_%.c $(CORE_SRC) | build
	$(CC) $(CFLAGS) $^ -o $@

build/san_test_%: test/test_%.c $(CORE_SRC) | build
	$(CC) $(CFLAGS) $(SANFLAGS) $^ -o $@

build/golden_runner: test/golden_runner.c $(CORE_SRC) | build
	$(CC) $(CFLAGS) $^ -o $@

build/san_golden_runner: test/golden_runner.c $(CORE_SRC) | build
	$(CC) $(CFLAGS) $(SANFLAGS) $^ -o $@

build/measure_self_cost: test/measure_self_cost.c $(CORE_SRC) | build
	$(CC) $(CFLAGS) $^ -o $@

test: all
	@set -e; for binary in $(UNIT_BINS); do ./$$binary; done
	@set -e; actual=$$(mktemp); trap 'rm -f "$$actual"' EXIT; ./build/golden_runner scenarios/normal_orbit.scn > "$$actual"; diff -u golden/normal_orbit.expected "$$actual"

sanitize: $(SAN_BINS) build/san_golden_runner
	@set -e; for binary in $(SAN_BINS); do ./$$binary; done
	@set -e; actual=$$(mktemp); trap 'rm -f "$$actual"' EXIT; ./build/san_golden_runner scenarios/normal_orbit.scn > "$$actual"; diff -u golden/normal_orbit.expected "$$actual"

clean:
	rm -f build/sage_rt build/golden_runner build/san_golden_runner build/measure_self_cost $(UNIT_BINS) $(SAN_BINS)
