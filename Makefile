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

HAL_SRCS = src/hal/hal_core.c src/hal/hal_i2c.c src/hal/hal_spi.c \
           src/hal/hal_mock.c src/hal/hal_can.c src/hal/hal_uart.c
ifeq ($(WITH_MODBUS),1)
CFLAGS  += -DHAVE_MODBUS
LDLIBS  += -lmodbus
HAL_SRCS += src/hal/hal_modbus.c
endif

SRCS   = src/main.c src/diag.c $(HAL_SRCS)
OBJS   = $(patsubst src/%.c,obj/%.o,$(SRCS))
TARGET = emb-bus-diag-v2
LIB    = libembdiag.so
BUILD  = build

.PHONY: all lib test py-test sim check clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Shared library for Python/ctypes (always built without sanitizers)
lib: $(LIB)

$(LIB): src/diag.c src/embdiag_api.c
	$(CC) -std=gnu11 -Wall -Wextra -O2 -Iinclude -fPIC -shared -o $@ $^ -lm

$(BUILD)/test_diag: tests/test_diag.c src/diag.c src/embdiag_api.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/test_hal: tests/test_hal.c $(HAL_SRCS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/test_uart: tests/test_uart.c $(HAL_SRCS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: $(BUILD)/test_diag $(BUILD)/test_hal $(BUILD)/test_uart $(TARGET)
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
