#!/usr/bin/env python3
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "logging"))
from ugripper_logger import ugripper_log

import pymodbus
from pymodbus.client import ModbusSerialClient


GPIO_NUM = 496
GPIO_PATH = Path(f"/sys/class/gpio/gpio{GPIO_NUM}")
POWER_BOARD_PORT = "/dev/ttyS0"
POWER_BOARD_BAUDRATE = 9600
POWER_BOARD_SLAVE_ID = 1
POWER_REGISTER = 0x10
POWER_BITS = tuple(range(4))
POWER_VERIFY_MASK = sum(1 << bit_index for bit_index in POWER_BITS)
POWER_GROUP_ATTEMPTS = 3
POWER_BIT_ATTEMPTS = 3
POWER_VERIFY_DELAY_SEC = 0.1
POWER_RETRY_DELAY_SEC = 0.2
POWER_STAGGER_DELAY_SEC = 0.01


def set_gpio_value(value: int) -> bool:
    value_path = GPIO_PATH / "value"
    try:
        value_path.write_text(f"{value}\n", encoding="ascii")
        ugripper_log("INFO", f"gpio {GPIO_NUM} set to {value}", component="usb_restore")
        return True
    except Exception as exc:
        ugripper_log("WARN", f"warning: failed to set gpio {GPIO_NUM} to {value}: {exc}", component="usb_restore")
        return False


def open_modbus_client() -> ModbusSerialClient:
    return ModbusSerialClient(
        port=POWER_BOARD_PORT,
        baudrate=POWER_BOARD_BAUDRATE,
        bytesize=8,
        parity="N",
        stopbits=1,
        timeout=1,
    )


def read_power_register(client: ModbusSerialClient) -> int | None:
    try:
        read_result = client.read_holding_registers(
            POWER_REGISTER,
            count=1,
            device_id=POWER_BOARD_SLAVE_ID,
        )
    except Exception as exc:
        ugripper_log("WARN", f"warning: failed to read power register: {exc}", component="usb_restore")
        return None

    if read_result is None or read_result.isError():
        ugripper_log("WARN", f"warning: failed to read power register: {read_result}", component="usb_restore")
        return None
    return int(read_result.registers[0])


def write_power_bit(bit_index: int, bit_value: int) -> bool:
    if bit_index < 0 or bit_index > 15:
        ugripper_log("WARN", f"warning: invalid power bit index: {bit_index}", component="usb_restore")
        return False

    client = open_modbus_client()
    try:
        if not client.connect():
            ugripper_log("WARN", f"warning: failed to open {POWER_BOARD_PORT}", component="usb_restore")
            return False

        current = read_power_register(client)
        if current is None:
            return False

        bit_mask = 1 << bit_index
        if bit_value:
            new_value = current | bit_mask
        else:
            new_value = current & ~bit_mask

        if new_value != current:
            try:
                write_result = client.write_register(
                    POWER_REGISTER,
                    new_value,
                    device_id=POWER_BOARD_SLAVE_ID,
                )
            except Exception as exc:
                ugripper_log("WARN", f"warning: failed to write power bit{bit_index}: {exc}", component="usb_restore")
                return False
            if write_result is None or write_result.isError():
                ugripper_log("WARN", f"warning: failed to write power bit{bit_index}: {write_result}", component="usb_restore")
                return False

        time.sleep(POWER_VERIFY_DELAY_SEC)
        verify = read_power_register(client)
        if verify is None:
            return False
        if bool(verify & bit_mask) != bool(bit_value):
            ugripper_log("WARN",
                f"warning: power bit{bit_index} verify mismatch: "
                f"expected={bit_value}, "
                f"got={1 if verify & bit_mask else 0}, "
                f"register=0x{verify:04X}", component="usb_restore"
            )
            return False

        ugripper_log("INFO",
            f"power bit{bit_index} -> {bit_value}, "
            f"register 0x{current:04X} -> 0x{new_value:04X}, "
            f"verify=0x{verify:04X}", component="usb_restore"
        )
        return True
    except Exception as exc:
        ugripper_log("WARN", f"warning: power bit{bit_index} restore failed: {exc}", component="usb_restore")
        return False
    finally:
        client.close()


def set_power_bit(bit_index: int, bit_value: int) -> bool:
    for attempt in range(1, POWER_BIT_ATTEMPTS + 1):
        if write_power_bit(bit_index, bit_value):
            return True
        if attempt < POWER_BIT_ATTEMPTS:
            ugripper_log("WARN",
                f"warning: retry power bit{bit_index} value={bit_value} "
                f"attempt={attempt + 1}/{POWER_BIT_ATTEMPTS}", component="usb_restore"
            )
            time.sleep(POWER_RETRY_DELAY_SEC)
    return False


def verify_power_bits(bit_value: int) -> bool:
    client = open_modbus_client()
    try:
        if not client.connect():
            ugripper_log("WARN", f"warning: failed to open {POWER_BOARD_PORT}", component="usb_restore")
            return False
        register_value = read_power_register(client)
    finally:
        client.close()

    if register_value is None:
        return False
    expected = POWER_VERIFY_MASK if bit_value else 0
    actual = register_value & POWER_VERIFY_MASK
    if actual != expected:
        ugripper_log("WARN",
            "warning: power bits group verify mismatch: "
            f"expected=0x{expected:04X}, got=0x{actual:04X}, "
            f"register=0x{register_value:04X}", component="usb_restore"
        )
        return False

    ugripper_log("INFO",
        f"power bits group verify ok: mask=0x{POWER_VERIFY_MASK:04X}, "
        f"value={bit_value}, register=0x{register_value:04X}", component="usb_restore"
    )
    return True


def set_power_bits(bit_value: int, stagger: bool = False) -> bool:
    for attempt in range(1, POWER_GROUP_ATTEMPTS + 1):
        bits_ok = True
        for bit_index in POWER_BITS:
            bits_ok = set_power_bit(bit_index, bit_value) and bits_ok
            if stagger and bit_index != POWER_BITS[-1]:
                time.sleep(POWER_STAGGER_DELAY_SEC)

        if bits_ok and verify_power_bits(bit_value):
            return True

        if attempt < POWER_GROUP_ATTEMPTS:
            ugripper_log("WARN",
                f"warning: retry power bits group value={bit_value} "
                f"attempt={attempt + 1}/{POWER_GROUP_ATTEMPTS}", component="usb_restore"
            )
            time.sleep(POWER_RETRY_DELAY_SEC)
    return False


def main() -> int:
    ugripper_log("INFO", f"pymodbus version: {pymodbus.__version__}", component="usb_restore")
    ok = True

    ok = set_gpio_value(1) and ok
    time.sleep(0.5)
    ok = set_gpio_value(1) and ok
    ok = set_power_bits(0) and ok
    time.sleep(5)

    ok = set_gpio_value(0) and ok
    time.sleep(1)
    ok = set_power_bits(1, stagger=True) and ok

    if not ok:
        ugripper_log(
            "ERROR",
            "USB power restore did not complete successfully",
            component="usb_restore",
            error_code="UGR-80-04",
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
