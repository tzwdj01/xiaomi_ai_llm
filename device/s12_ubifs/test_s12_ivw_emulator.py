"""Offline S12 callback experiment. No device access and no real audio.

Usage: python test_s12_ivw_emulator.py /path/to/local/mipns-xiaomi
Requires unicorn==2.1.4 and pyelftools. The firmware is not distributed here.
"""
from pathlib import Path
import hashlib
import sys

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import (UC_ARM64_REG_PC, UC_ARM64_REG_SP, UC_ARM64_REG_LR,
                                UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2,
                                UC_ARM64_REG_X3)

if len(sys.argv) != 2:
    raise SystemExit('usage: python test_s12_ivw_emulator.py /path/to/mipns-xiaomi')
binary = Path(sys.argv[1])
if hashlib.md5(binary.read_bytes()).hexdigest() != '6cd03f9c1a2f517677898015e49e58b7':
    raise SystemExit('firmware does not match the locally studied build')

loads = []
with binary.open('rb') as source:
    for segment in ELFFile(source).iter_segments():
        if segment['p_type'] == 'PT_LOAD':
            loads.append((segment['p_vaddr'], segment.data()))

def simulate(result, length, skip_audio=0):
    machine = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    machine.mem_map(0x400000, 0x200000)
    for address, data in loads:
        machine.mem_write(address, data)
    machine.mem_map(0x10000000, 0x100000)
    machine.mem_map(0x20000000, 0x1000)
    machine.mem_write(0x462898, b'\0' * 4)  # disable firmware debug branch
    machine.mem_write(0x452a70, b'\0')       # start with buffer gate closed
    for register, value in ((UC_ARM64_REG_X0, result), (UC_ARM64_REG_X1, 0x10010000),
                            (UC_ARM64_REG_X2, length), (UC_ARM64_REG_SP, 0x100f0000),
                            (UC_ARM64_REG_LR, 0x20000000)):
        machine.reg_write(register, value)
    writes = []
    def hook(emu, address, size, user_data):
        del size, user_data
        if address == 0x404d00:  # logger, intentionally stubbed
            emu.reg_write(UC_ARM64_REG_PC, emu.reg_read(UC_ARM64_REG_LR))
        elif address == 0x412d08:  # discard predicate, fixed per scenario
            emu.reg_write(UC_ARM64_REG_X0, skip_audio)
            emu.reg_write(UC_ARM64_REG_PC, emu.reg_read(UC_ARM64_REG_LR))
        elif address == 0x4165fc:  # side effect, intentionally stubbed
            emu.reg_write(UC_ARM64_REG_PC, emu.reg_read(UC_ARM64_REG_LR))
        elif address == 0x41413c:  # buffer write: record length, not PCM
            writes.append((emu.reg_read(UC_ARM64_REG_X2), emu.reg_read(UC_ARM64_REG_X3)))
            emu.reg_write(UC_ARM64_REG_PC, emu.reg_read(UC_ARM64_REG_LR))
    machine.hook_add(UC_HOOK_CODE, hook)
    machine.emu_start(0x40d564, 0x20000000, count=100000)
    if machine.reg_read(UC_ARM64_REG_PC) != 0x20000000:
        raise RuntimeError('callback failed to return')
    return bytes(machine.mem_read(0x452a70, 1))[0], writes

assert simulate(1, 0) == (1, [])
assert simulate(0x101, 0) == (0, [])
assert simulate(1, 8000) == (1, [(4000, 0), (4000, 1)])
assert simulate(1, 8000, skip_audio=1) == (1, [])
print('S12_IVW_EMULATION_OK empty_opens_gate flagged_event_ignored synthetic_chunks=2')
