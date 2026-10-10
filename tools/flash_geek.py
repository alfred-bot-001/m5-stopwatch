"""Flash only the backed-up GEEK-988C, using a verified Geek build snapshot."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time

import serial
from serial.tools.list_ports import comports
from elftools.elf.elffile import ELFFile

from archive_build import archive_build


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'receiver/build-geek'
PROJECT = 'stopwatch_receiver'
MAC = 'd4:05:92:78:98:8c'
DEVICE_ID = 'D4059278988C'
APP_SERIAL = DEVICE_ID + '-VIBE1'
BACKUP = ROOT.parent / 'backups/receiver-d4059278988c/original-20261010.bin'
FLASH_SIZE = 16 * 1024 * 1024


def require(condition, message):
    # Unlike assert, these guards remain active under python -O.
    if not condition:
        raise RuntimeError(message)


def verify_backup():
    metadata = json.loads(BACKUP.with_suffix('.json').read_text())
    require(metadata.get('mac', '').lower() == MAC, 'Backup MAC does not match GEEK-988C')
    require(BACKUP.stat().st_size == FLASH_SIZE, 'The original backup must be exactly 16 MiB')
    digest = hashlib.sha256(BACKUP.read_bytes()).hexdigest()
    require(digest == metadata.get('sha256', '').lower(), 'Original backup SHA-256 mismatch')


def cache_value(cache, key):
    matches = re.findall(r'^' + re.escape(key) + r':[^=\n]+=(.*)$', cache, re.MULTILINE)
    require(len(matches) == 1, f'Expected one {key} in the Geek CMake cache')
    return matches[0]


def verify_build_config():
    cache = (BUILD / 'CMakeCache.txt').read_text()
    require(cache_value(cache, 'VIBE_RECEIVER_BOARD') == 'geek', 'Build is not configured for Geek')
    config_path = Path(cache_value(cache, 'SDKCONFIG'))
    require(config_path.is_absolute(), 'SDKCONFIG must identify an absolute configuration path')
    config = config_path.read_text().splitlines()
    require('CONFIG_SPIRAM_MODE_QUAD=y' in config, 'Geek requires Quad PSRAM mode')
    require('CONFIG_SPIRAM_MODE_OCT=y' not in config, 'Octal PSRAM mode is invalid for this Geek')
    require('CONFIG_IDF_TARGET="esp32s3"' in config, 'Build target is not ESP32-S3')


def symbol_data(elf, name):
    symbols = elf.get_section_by_name('.symtab')
    require(symbols is not None, 'ELF has no symbol table')
    matches = symbols.get_symbol_by_name(name) or []
    require(len(matches) == 1, f'ELF must contain exactly one {name} symbol')
    symbol = matches[0]
    require(isinstance(symbol['st_shndx'], int), f'ELF symbol {name} has no data section')
    section = elf.get_section(symbol['st_shndx'])
    offset = symbol['st_value'] - section['sh_addr']
    require(offset >= 0 and offset + symbol['st_size'] <= section['sh_size'],
            f'ELF symbol {name} is outside its section')
    return section.data()[offset:offset + symbol['st_size']]


def verify_snapshot(snapshot):
    elf_path = snapshot / f'{PROJECT}.elf'
    elf_bytes = elf_path.read_bytes()
    with elf_path.open('rb') as source:
        elf = ELFFile(source)
        descriptor = symbol_data(elf, 'device')
    require(len(descriptor) == 18 and descriptor[:2] == b'\x12\x01',
            'ELF does not contain a valid USB device descriptor')
    require(int.from_bytes(descriptor[8:10], 'little') == 0xcafe
            and int.from_bytes(descriptor[10:12], 'little') == 0x4022,
            'ELF USB identity is not the Geek receiver CAFE:4022')
    require(APP_SERIAL.encode() + b'\0' in elf_bytes, 'ELF is missing the Geek USB serial string')
    require(b'VIBE/1 ARM-BOOT ' + DEVICE_ID.encode() + b'\0' in elf_bytes,
            'ELF is missing the protected Geek maintenance identity')

    application = (snapshot / f'{PROJECT}.bin').read_bytes()
    require(len(application) > 0xd0 and application[0] == 0xe9,
            'Application is not an ESP image')
    # ESP image header (24), first segment header (8), then esp_app_desc_t.
    require(application[0x20:0x24] == b'\x32\x54\xcd\xab', 'Application descriptor is missing')
    require(application[0xb0:0xd0] == hashlib.sha256(elf_bytes).digest(),
            'Application binary does not match the archived ELF')
    require(len(application) <= 4 * 1024 * 1024, 'Application exceeds the 4 MiB app partition')
    bootloader = (snapshot / 'bootloader.bin').read_bytes()
    partition = (snapshot / 'partition-table.bin').read_bytes()
    require(0 < len(bootloader) <= 0x8000 and bootloader[0] == 0xe9,
            'Bootloader is invalid or overlaps the partition table')
    require(0 < len(partition) <= 0x1000, 'Partition table would overwrite NVS')
    entries = {}
    for offset in range(0, len(partition) - 31, 32):
        entry = partition[offset:offset + 32]
        if entry[:2] != b'\xaa\x50':
            break
        label = entry[12:28].split(b'\0', 1)[0].decode('ascii')
        entries[label] = (entry[2], entry[3], int.from_bytes(entry[4:8], 'little'),
                          int.from_bytes(entry[8:12], 'little'))
    require(entries.get('factory') == (0, 0, 0x10000, 4 * 1024 * 1024),
            'Application partition does not match the planned write address')
    require(entries.get('nvs') == (1, 2, 0x9000, 0x5000), 'Unexpected Geek NVS partition layout')


def matching_ports(kind):
    if kind == 'app':
        return [p for p in comports() if p.vid == 0xcafe and p.pid == 0x4022
                and p.serial_number == APP_SERIAL]
    return [p for p in comports() if p.vid == 0x303a and p.pid == 0x1001
            and (p.serial_number or '').lower() == MAC]


def one_port(kind, required=True):
    matches = matching_ports(kind)
    require(len(matches) <= 1, f'Multiple devices claim the Geek {kind} identity')
    require(bool(matches) or not required, f'GEEK-988C {kind} USB port not found')
    return matches[0] if matches else None


def wait_rom(timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        port = one_port('rom', required=False)
        if port:
            return port
        time.sleep(0.25)
    raise RuntimeError('Verified Geek ROM USB port did not appear; no firmware was written')


def request_boot(app):
    # TinyUSB exposes diagnostics only with DTR asserted. In this application
    # only the protected command requests reset; RTS remains deasserted.
    conn = serial.Serial(port=None, baudrate=115200, timeout=0.5, write_timeout=2)
    conn.dtr = True
    conn.rts = False
    conn.port = app.device
    with conn:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if conn.readline().startswith(b'VIBE RX v=1 '):
                break
        else:
            raise RuntimeError('Unknown running Geek firmware; refusing to request download mode')
        # Verify USB identity again before writing the maintenance commands.
        current = one_port('app')
        require(current.device == app.device and current.location == app.location,
                'Geek USB connection changed before download request')
        conn.write(('\nVIBE/1 ARM-BOOT ' + DEVICE_ID + '\nVIBE/1 CONFIRM-BOOT '
                    + DEVICE_ID + '\n').encode())
        conn.flush()


def esptool_base(port, before):
    return [sys.executable, '-m', 'esptool', '--chip', 'esp32s3', '--port', port.device,
            '--baud', '115200', '--before', before]


def read_mac(port, before):
    result = subprocess.run(esptool_base(port, before) +
                            ['--after', 'no-reset', '--no-stub', 'read-mac'],
                            check=True, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=40)
    print(result.stdout, end='', flush=True)
    addresses = re.findall(r'^MAC:\s*([0-9a-f:]{17})\s*$', result.stdout,
                           re.MULTILINE | re.IGNORECASE)
    require(bool(addresses) and {address.lower() for address in addresses} == {MAC},
            'Connected chip MAC does not match GEEK-988C; flash aborted')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    verify_backup()
    verify_build_config()
    snapshot = archive_build(ROOT, BUILD, PROJECT)
    verify_snapshot(snapshot)
    print(f'Backup and Geek firmware verified. Flashing archived build: {snapshot}', flush=True)

    app = one_port('app', required=False)
    rom = one_port('rom', required=False)
    require(not (app and rom), 'Both Geek app and ROM identities are present; refusing ambiguous target')
    if app:
        request_boot(app)
        rom = wait_rom()
        before = 'no-reset'
    else:
        require(rom is not None, 'GEEK-988C is not connected; other ESP32 devices will not be touched')
        # The original factory app also uses USB Serial/JTAG. Explicit USB reset
        # enters the ROM loader, unlike an assumption based on the VID/PID alone.
        before = 'usb-reset'
    initial_location = rom.location
    read_mac(rom, before)
    rom = wait_rom()
    require(rom.location == initial_location, 'Geek moved to another USB location during identification')
    read_mac(rom, 'no-reset')
    current = one_port('rom')
    require(current.device == rom.device and current.location == initial_location,
            'Geek USB connection changed immediately before flashing')
    # Revalidate the immutable snapshot after USB identification. Esptool verifies
    # each written region; the original NVS and all other sectors are left intact.
    verify_snapshot(snapshot)
    subprocess.run(esptool_base(current, 'no-reset') + [
        '--after', 'watchdog-reset', 'write-flash', '--flash-mode', 'dio',
        '--flash-size', '16MB', '--flash-freq', '80m',
        '0x0', str(snapshot / 'bootloader.bin'),
        '0x8000', str(snapshot / 'partition-table.bin'),
        '0x10000', str(snapshot / f'{PROJECT}.bin')], check=True)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError, subprocess.SubprocessError) as error:
        sys.exit(f'Geek flash stopped: {error}')
