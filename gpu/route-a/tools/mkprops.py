#!/usr/bin/env python3
"""Write an Android 4.4 (KitKat) property area for bionic programs on plain Linux.

bionic's libc maps /dev/__properties__ at start-up and dereferences it without checking; the file
must be owned by root, not group/world writable, and start with a valid prop_area header. Android's
hardware-module loader also needs properties such as ro.hardware to pick gralloc.<board>.so.

KitKat layout (bionic/libc/bionic/system_properties.c):
  struct prop_area { u32 bytes_used, serial, magic, version, reserved[28]; char data[]; }
  struct prop_bt   { u8 namelen, reserved[3]; u32 prop, left, right, children; char name[]; }
  struct prop_info { u32 serial; char value[92]; char name[]; }
Offsets are relative to data[]; the root prop_bt (empty name) is at offset 0. Names are split at
'.'; each level is a binary search tree of siblings ordered by (length, bytes); allocations are
4-byte aligned. serial = value length << 24.

usage: mkprops.py OUT [FILE|name=value]...
  FILE: getprop output ("[name]: [value]") or build.prop ("name=value"); later values win.
"""
import os
import re
import struct
import sys

PROP_AREA_MAGIC = 0x504F5250    # "PROP"
PROP_AREA_VERSION = 0xFC6ED0AB
PA_SIZE = 128 * 1024
PROP_VALUE_MAX = 92
HDR = 4 * 4 + 28 * 4


class Area:
    def __init__(self):
        self.data = bytearray(PA_SIZE - HDR)
        self.used = 0
        self.root = self.new_bt(b"")

    def alloc(self, size):
        off = self.used
        self.used += (size + 3) & ~3
        if self.used > len(self.data):
            raise SystemExit("property area full")
        return off

    def new_bt(self, name):
        off = self.alloc(20 + len(name) + 1)
        struct.pack_into("<B3xIIII", self.data, off, len(name), 0, 0, 0, 0)
        self.data[off + 20:off + 20 + len(name)] = name
        return off

    def bt(self, off):
        namelen, prop, left, right, children = struct.unpack_from("<B3xIIII", self.data, off)
        return namelen, bytes(self.data[off + 20:off + 20 + namelen]), prop, left, right, children

    def set_field(self, off, idx, val):    # idx: 0 prop, 1 left, 2 right, 3 children
        struct.pack_into("<I", self.data, off + 4 + 4 * idx, val)

    @staticmethod
    def cmp(a, b):
        if len(a) != len(b):
            return -1 if len(a) < len(b) else 1
        return (a > b) - (a < b)

    def find_or_add_child(self, parent, name):
        _, _, _, _, _, children = self.bt(parent)
        if not children:
            child = self.new_bt(name)
            self.set_field(parent, 3, child)
            return child
        cur = children
        while True:
            _, cname, _, left, right, _ = self.bt(cur)
            c = self.cmp(name, cname)
            if c == 0:
                return cur
            idx, nxt = (1, left) if c < 0 else (2, right)
            if not nxt:
                new = self.new_bt(name)
                self.set_field(cur, idx, new)
                return new
            cur = nxt

    def add(self, name, value):
        value = value.encode()[:PROP_VALUE_MAX - 1]
        node = self.root
        for seg in name.split("."):
            node = self.find_or_add_child(node, seg.encode())
        nb = name.encode()
        off = self.alloc(4 + PROP_VALUE_MAX + len(nb) + 1)
        struct.pack_into("<I", self.data, off, len(value) << 24)
        self.data[off + 4:off + 4 + len(value)] = value
        self.data[off + 4 + PROP_VALUE_MAX:off + 4 + PROP_VALUE_MAX + len(nb)] = nb
        self.set_field(node, 0, off)

    def image(self):
        hdr = struct.pack("<IIII", self.used, 0, PROP_AREA_MAGIC, PROP_AREA_VERSION) + b"\0" * (28 * 4)
        return hdr + bytes(self.data)


props = {}
for arg in sys.argv[2:]:
    lines = open(arg, encoding="utf-8", errors="replace") if os.path.isfile(arg) else [arg]
    for line in lines:
        line = line.strip("\r\n")
        m = re.match(r"\[([^\]]+)\]: \[(.*)\]\s*$", line) or re.match(r"\s*([^#=\s][^=]*?)\s*=(.*)$", line)
        if m:
            props[m.group(1)] = m.group(2)[:PROP_VALUE_MAX - 1]
area = Area()
for name, value in props.items():
    area.add(name, value)
with open(sys.argv[1], "wb") as f:
    f.write(area.image())
