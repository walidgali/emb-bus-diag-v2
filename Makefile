CC = gcc
CFLAGS = -std=gnu11 -Wall -Wextra -O2 -Iinclude
LDFLAGS = -lm -lmodbus -lmosquitto

TARGET = emb-bus-diag-v2

all: $(TARGET)

$(TARGET): src/main.c src/diag.c src/cloud/mqtt_client.c src/hal/hal_core.c src/hal/hal_i2c.c src/hal/hal_spi.c src/hal/hal_mock.c src/hal/hal_can.c src/hal/hal_modbus.c src/hal/hal_uart.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(TARGET)
