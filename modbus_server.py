"""Tiny Modbus TCP test server for the --bus modbus backend.

Requires: pip install -r requirements-dev.txt   (pymodbus 3.6.x)
Register 0 holds a raw INA219 bus-voltage word; the default 0x5DC0 reads as 12.0 V.
"""
import argparse

from pymodbus.datastore import (ModbusSequentialDataBlock, ModbusServerContext,
                                ModbusSlaveContext)
from pymodbus.server import StartTcpServer


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="127.0.0.1", help="bind address (default: localhost only)")
    ap.add_argument("--port", type=int, default=1502)
    ap.add_argument("--value", type=lambda s: int(s, 0), default=0x5DC0,
                    help="initial value of register 0 (default 0x5DC0 = 12.0 V on INA219)")
    args = ap.parse_args()

    block = ModbusSequentialDataBlock(0, [args.value] + [0] * 99)
    # zero_mode=True: client register N maps to datastore address N
    store = ModbusSlaveContext(hr=block, ir=block, co=block, di=block, zero_mode=True)
    context = ModbusServerContext(slaves=store, single=True)

    print(f"Modbus server on {args.host}:{args.port} (register 0 = {args.value:#06x})")
    StartTcpServer(context=context, address=(args.host, args.port))


if __name__ == "__main__":
    main()
