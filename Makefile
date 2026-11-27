# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 walidgali
# emb-bus-diag-v2
CC      ?= gcc
CFLAGS  ?= -O2
CFLAGS  += -std=gnu11 -Wall -Wextra -Iinclude
LDLIBS  := -lm

# libmodbus is optional: auto-detected, override with WITH_MODBUS=0/1
WITH_MODBUS ?= $(shell pkg-config --exists libmodbus 2>/dev/null && echo 1 || echo 0)
# SAN=1 builds with AddressSanitizer + UBSan (for C tests / CLI only)
SAN ?= 0

ifeq ($(SAN),1)
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer -g
endif

HAL_LIBS :=
HAL_SRCS = src/hal/hal_core.c src/hal/hal_i2c.c src/hal/hal_spi.c \
           src/hal/hal_mock.c src/hal/hal_can.c src/hal/hal_uart.c
ifeq ($(WITH_MODBUS),1)
CFLAGS  += -DHAVE_MODBUS
LDLIBS  += -lmodbus
HAL_LIBS += -lmodbus
HAL_SRCS += src/hal/hal_modbus.c
endif

CORE_SRC = core/embdiag_core.c
# libmosquitto is optional too (cloud / MQTT): auto-detected, override with WITH_MQTT=0/1
WITH_MQTT ?= $(shell pkg-config --exists libmosquitto 2>/dev/null && echo 1 || echo 0)
CLOUD_SRCS = src/cloud/cloud_payload.c
ifeq ($(WITH_MQTT),1)
LDLIBS    += -lmosquitto
CLOUD_SRCS += src/cloud/mqtt_client.c
else
CLOUD_SRCS += src/cloud/cloud_stub.c
endif

SRCS   = src/main.c src/diag.c $(CORE_SRC) $(CLOUD_SRCS) $(HAL_SRCS)
OBJS   = $(patsubst %.c,obj/%.o,$(SRCS))
TARGET = emb-bus-diag-v2
LIB    = libembdiag.so
BUILD  = build

.PHONY: all lib test py-test sim core-check cloud-test check clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Shared library for Python/ctypes (always built without sanitizers)
lib: $(LIB)

$(LIB): $(CORE_SRC) src/embdiag_api.c
	$(CC) -std=gnu11 -Wall -Wextra -O2 -Iinclude -fPIC -shared -o $@ $^ -lm

$(BUILD)/test_diag: tests/test_diag.c src/diag.c $(CORE_SRC) src/embdiag_api.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# The portable core is tested on its own, with libm and with the built-in sqrt
$(BUILD)/test_core: tests/test_core.c $(CORE_SRC)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/test_core_nolibm: tests/test_core.c $(CORE_SRC)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -DEMBDIAG_NO_LIBM -o $@ $^ $(LDLIBS)

# Portability guard: the core must compile as strict C99 and, without libm,
# leave NO undefined symbols (no libc, no heap, no OS calls).
core-check:
	@mkdir -p $(BUILD)
	@for opt in -O0 -O2 -O3; do \
	  $(CC) -std=c99 -Wall -Wextra -Wpedantic -ffreestanding $$opt -DEMBDIAG_NO_LIBM -Iinclude \
	    -c $(CORE_SRC) -o $(BUILD)/core_check.o || exit 1; \
	  if [ -n "$$(nm -u $(BUILD)/core_check.o)" ]; then \
	    echo "FAIL: core has undefined symbols at $$opt:"; nm -u $(BUILD)/core_check.o; exit 1; fi; \
	done
	@echo "core-check: strict C99, no undefined symbols (-O0/-O2/-O3)"

# Cloud client tested against a fake libmosquitto (no broker, no network needed)
FAKE_MQTT = tests/sim/fake_mosquitto.c
$(BUILD)/test_cloud: tests/test_cloud.c src/cloud/cloud_payload.c src/cloud/mqtt_client.c $(FAKE_MQTT) src/diag.c $(CORE_SRC)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Itests/sim/fake -o $@ $^ -lm

$(BUILD)/cli_fakecloud: src/main.c src/diag.c $(CORE_SRC) src/cloud/cloud_payload.c src/cloud/mqtt_client.c $(FAKE_MQTT) $(HAL_SRCS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Itests/sim/fake -o $@ $^ -lm $(HAL_LIBS)

cloud-test: $(BUILD)/test_cloud $(BUILD)/cli_fakecloud
	./$(BUILD)/test_cloud
	sh tests/cloud_smoke.sh ./$(BUILD)/cli_fakecloud

$(BUILD)/test_hal: tests/test_hal.c $(HAL_SRCS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/test_uart: tests/test_uart.c $(HAL_SRCS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: $(BUILD)/test_core $(BUILD)/test_core_nolibm $(BUILD)/test_diag $(BUILD)/test_hal $(BUILD)/test_uart $(TARGET) core-check cloud-test
	./$(BUILD)/test_core
	./$(BUILD)/test_core_nolibm
	./$(BUILD)/test_diag
	./$(BUILD)/test_hal
	./$(BUILD)/test_uart
	sh tests/smoke.sh ./$(TARGET)

py-test: $(LIB) $(TARGET)
	python3 -m unittest discover -s tests -p 'test_*.py' -v

# Hardware simulation: runs the real CLI under an LD_PRELOAD system-call simulator
sim: $(TARGET) $(BUILD)/libhwsim.so
	sh tests/sim/run_sim.sh ./$(TARGET) ./$(BUILD)/libhwsim.so

$(BUILD)/libhwsim.so: tests/sim/hwsim.c
	@mkdir -p $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -O2 -fPIC -shared -Wno-nonnull-compare -Wno-maybe-uninitialized -o $@ $< -ldl

check: test py-test sim

clean:
	rm -rf obj $(BUILD) $(TARGET) $(LIB) report.json
