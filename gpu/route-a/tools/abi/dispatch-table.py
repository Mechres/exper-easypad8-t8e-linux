#!/usr/bin/env python3
"""Recover the bridge dispatch table (ioctl index -> handler name) of a stock pvrsrvkm.ko.

Every registration is `mov r0, #index; ldr r1..r3, [pc, #x]; bl _SetDispatchTableEntry`, where the
literal-pool words carry REL relocations to the handler function and to name strings.
usage: dispatch-table.py OBJDUMP pvrsrvkm.ko
"""
import re
import subprocess
import sys

objdump, ko = sys.argv[1], sys.argv[2]
dis = subprocess.run([objdump, "-dr", ko], capture_output=True, text=True).stdout

# REL relocations per address: addr -> symbol
relocs = {}
for m in re.finditer(r"^\s+([0-9a-f]+): R_ARM_ABS32\s+(\S+)", dis, re.M):
    relocs[int(m.group(1), 16)] = m.group(2)
# literal words (the addend for section-relative relocations): addr -> value
words = {}
for m in re.finditer(r"^\s+([0-9a-f]+):\s+([0-9a-f]{8})\s+\.word\s", dis, re.M):
    words[int(m.group(1), 16)] = int(m.group(2), 16)

# .rodata.str1.1 contents, to resolve section+offset strings
sec = subprocess.run([objdump, "-s", "-j", ".rodata.str1.1", ko], capture_output=True, text=True).stdout
blob = bytearray()
for line in sec.splitlines():
    m = re.match(r"^ ([0-9a-f]{4,8}) ((?:[0-9a-f]{2,8} ){1,4})", line)
    if m:
        blob += bytes.fromhex(m.group(2).replace(" ", ""))


def resolve(addr):
    sym = relocs.get(addr)
    if sym is None:
        return "?"
    if sym.startswith(".rodata.str"):
        off = words.get(addr, 0)
        end = blob.find(b"\0", off)
        return '"' + blob[off:end].decode(errors="replace") + '"'
    return sym


lines = dis.splitlines()
table = []
for i, line in enumerate(lines):
    if "bl\t" not in line or "<_SetDispatchTableEntry>" not in line:
        continue
    idx, args = None, {}
    for back in lines[max(0, i - 12):i][::-1]:
        m = re.search(r"\tmovw?\tr0, #(\d+)", back)
        if m and idx is None:
            idx = int(m.group(1))
        m = re.search(r"\tldr\t(r[123]), \[pc, #\d+\]\s+@ ([0-9a-f]+)", back)
        if m and m.group(1) not in args:
            args[m.group(1)] = resolve(int(m.group(2), 16))
    table.append((idx, args.get("r2", "?"), args.get("r1", "?"), args.get("r3", "?")))

for idx, fn, a1, a3 in sorted(table, key=lambda t: (t[0] is None, t[0] or 0)):
    print(f"{idx if idx is not None else '?':>4}  {fn:<48} {a1}  {a3}")
print(f"# {len(table)} entries", file=sys.stderr)
