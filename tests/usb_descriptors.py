"""Validate the actual compiled USB descriptors against the S3 endpoint budget."""
import sys
from elftools.elf.elffile import ELFFile

with open(sys.argv[1], 'rb') as source:
    elf = ELFFile(source)
    symbols = elf.get_section_by_name('.symtab')

    def data(name):
        symbol, = symbols.get_symbol_by_name(name)
        section = elf.get_section(symbol['st_shndx'])
        offset = symbol['st_value'] - section['sh_addr']
        return section.data()[offset:offset + symbol['st_size']]

    configuration = data('configuration')
    hid = data('hid_descriptor')

assert int.from_bytes(configuration[2:4], 'little') == len(configuration)
interfaces = set()
endpoints = {}
hid_endpoints = []
offset = 0
interface_class = None
while offset < len(configuration):
    size, kind = configuration[offset:offset + 2]
    assert size >= 2 and offset + size <= len(configuration)
    item = configuration[offset:offset + size]
    if kind == 4:  # interface
        interfaces.add(item[2])
        interface_class = item[5]
    elif kind == 5:  # endpoint
        address = item[2]
        assert address not in endpoints, 'Duplicate endpoint allocation'
        endpoints[address] = int.from_bytes(item[4:6], 'little')
        if interface_class == 3:
            hid_endpoints.append(address)
    offset += size
assert len(interfaces) == configuration[4]
assert 1 + sum(bool(address & 0x80) for address in endpoints) <= 5, 'S3 IN endpoint budget includes EP0'
assert len(hid_endpoints) == 1, 'Keyboard and mouse must share one HID endpoint'

# Parse HID short items to calculate input report sizes from the binary.
offset = 0
report_id = bits = count = 0
input_bits = {}
while offset < len(hid):
    prefix = hid[offset]
    assert prefix != 0xfe, 'Unexpected HID long item'
    length = (0, 1, 2, 4)[prefix & 3]
    value = int.from_bytes(hid[offset + 1:offset + 1 + length], 'little')
    tag = prefix & 0xfc
    if tag == 0x84:
        report_id = value
    elif tag == 0x74:
        bits = value
    elif tag == 0x94:
        count = value
    elif tag == 0x80:
        input_bits[report_id] = input_bits.get(report_id, 0) + bits * count
    offset += length + 1
assert input_bits == {1: 64, 2: 40}, input_bits
assert endpoints[hid_endpoints[0]] >= 1 + max(input_bits.values()) // 8
print('PASS compiled descriptor lengths, interface count, S3 endpoint budget, keyboard/mouse report sizes')
