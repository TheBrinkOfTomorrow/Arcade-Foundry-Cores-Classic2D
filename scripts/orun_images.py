#!/usr/bin/env python3
"""Writes the Out Run board's images from a MAME set: the 68000 programs
(de-interleaved, as the CPUs see them, for m68krecomp), the Z80 program and
the SegaPCM sample region as MAME loads it. Output is ROM-derived: keep it
under work/ or build/ (git-ignored).

  orun_images.py SET.zip OUTDIR
"""
import os
import sys
import zipfile

# MAME's ROM_LOAD16_BYTE layout per set: (even, odd, offset) for each CPU.
SETS = {
    'outrun': {
        'main': [('epr-10380b.133', 'epr-10382b.118', 0x00000), ('epr-10381b.132', 'epr-10383b.117', 0x20000)],
        'sub': [('epr-10327a.76', 'epr-10329a.58', 0x00000), ('epr-10328a.75', 'epr-10330a.57', 0x20000)],
        'sound': ['epr-10187.88'],
        # ROM_LOAD at n * 0x10000 with a ROM_RELOAD at + 0x8000, the rest 0xff
        'pcm': ['opr-10193.66', 'opr-10192.67', 'opr-10191.68', 'opr-10190.69', 'opr-10189.70', 'opr-10188.71'],
        # tiles: ROM_LOAD at 0x8000 steps
        'tiles': ['opr-10268.99', 'opr-10232.102', 'opr-10267.100', 'opr-10231.103', 'opr-10266.101', 'opr-10230.104'],
        # sprites: ROM_REGION32_LE, ROM_LOAD32_BYTE byte lanes 0-3 of each half
        'sprites': [['mpr-10371.9', 'mpr-10373.10', 'mpr-10375.11', 'mpr-10377.12'],
                    ['mpr-10372.13', 'mpr-10374.14', 'mpr-10376.15', 'mpr-10378.16']],
        'road': ['opr-10186.47', 'opr-10185.11'],
    },
}


def main():
    path, out = sys.argv[1], sys.argv[2]
    name = os.path.splitext(os.path.basename(path))[0]
    z = zipfile.ZipFile(path)
    os.makedirs(out, exist_ok=True)
    for cpu, parts in SETS[name].items():
        if cpu == 'sound':
            data = z.read(parts[0])
            with open(os.path.join(out, f'{name}_sound.bin'), 'wb') as f:
                f.write(data)
            print(f'{name}_sound.bin: {len(data):#x} bytes')
            continue
        if cpu in ('tiles', 'road'):
            img = b''.join(z.read(r) for r in parts)
            with open(os.path.join(out, f'{name}_{cpu}.bin'), 'wb') as f:
                f.write(img)
            print(f'{name}_{cpu}.bin: {len(img):#x} bytes')
            continue
        if cpu == 'sprites':
            img = bytearray(0x100000)
            for half, roms in enumerate(parts):
                for lane, rom in enumerate(roms):
                    data = z.read(rom)
                    img[half * 0x80000 + lane:half * 0x80000 + 4 * len(data):4] = data
            with open(os.path.join(out, f'{name}_sprites.bin'), 'wb') as f:
                f.write(img)
            print(f'{name}_sprites.bin: {len(img):#x} bytes')
            continue
        if cpu == 'pcm':
            img = bytearray(b'\xff' * 0x80000)
            for i, rom in enumerate(parts):
                data = z.read(rom)
                img[i * 0x10000:i * 0x10000 + 0x8000] = data
                img[i * 0x10000 + 0x8000:i * 0x10000 + 0x10000] = data
            with open(os.path.join(out, f'{name}_pcm.bin'), 'wb') as f:
                f.write(img)
            print(f'{name}_pcm.bin: {len(img):#x} bytes')
            continue
        img = bytearray(0x60000)
        top = 0
        for even, odd, off in parts:
            e, o = z.read(even), z.read(odd)
            for i in range(len(e)):
                img[off + 2 * i] = e[i]
                img[off + 2 * i + 1] = o[i]
            top = max(top, off + 2 * len(e))
        with open(os.path.join(out, f'{name}_{cpu}.bin'), 'wb') as f:
            f.write(img[:top])
        print(f'{name}_{cpu}.bin: {top:#x} bytes')


main()
