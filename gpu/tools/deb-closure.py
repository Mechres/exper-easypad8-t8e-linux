#!/usr/bin/env python3
"""Resolve the runtime dependency closure of Debian packages from a Packages index.

usage: deb-closure.py PACKAGES_FILE [-n] [-x skip,skip...] pkg [pkg...]
  -n  only the named packages, no dependencies
Prints one "name version filename" line per package to fetch. Packages we
provide ourselves on the tablet (glibc, libstdc++, libdrm, Mesa's EGL/GLES/GBM)
and non-library infrastructure are skipped.
"""
import re
import sys

SKIP = {
    # our /opt/pvr runtime: toolchain glibc 2.39 + libstdc++, Debian libdrm2, our Mesa
    "libc6", "libgcc-s1", "libstdc++6", "libdrm2", "libdrm-common",
    "libegl1", "libgles2", "libgbm1", "libgl1", "libglvnd0", "libegl-mesa0",
    "libglapi-mesa", "libglx0", "libglx-mesa0", "mesa-libgallium",
    # infrastructure / data-only or not needed for running a compositor
    "adduser", "debconf", "dpkg", "passwd", "init-system-helpers", "base-files",
    "libc-bin", "sensible-utils", "ucf", "lsb-base", "perl-base", "tzdata",
    "fontconfig-config", "libglib2.0-data", "shared-mime-info", "xkb-data",
    "libwacom-common", "libinput-bin", "dbus", "dbus-bin", "dbus-daemon",
    "dbus-session-bus-common", "dbus-system-bus-common", "colord-data",
}


def parse(path):
    pkgs, provides = {}, {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for block in f.read().split("\n\n"):
            fields, key = {}, None
            for line in block.splitlines():
                if line.startswith(" ") and key:
                    fields[key] += " " + line.strip()
                elif ":" in line:
                    key, _, val = line.partition(":")
                    fields[key] = val.strip()
            name = fields.get("Package")
            if not name:
                continue
            pkgs[name] = fields
            for p in fields.get("Provides", "").split(","):
                p = re.sub(r"\(.*?\)", "", p).strip()
                if p:
                    provides.setdefault(p, name)
    return pkgs, provides


def deps(fields):
    out = []
    for key in ("Pre-Depends", "Depends"):
        for alt in fields.get(key, "").split(","):
            alt = alt.strip()
            if not alt:
                continue
            first = re.sub(r"\(.*?\)|\[.*?\]|:any", "", alt.split("|")[0]).strip()
            out.append(first)
    return out


def main():
    args = sys.argv[2:]
    nodeps = bool(args) and args[0] == "-n"
    if nodeps:
        args = args[1:]
    if args and args[0] == "-x":
        SKIP.update(args[1].split(","))
        args = args[2:]
    pkgs, provides = parse(sys.argv[1])
    todo, seen = list(args), set()
    while todo:
        name = todo.pop()
        if name not in pkgs and name in provides:
            name = provides[name]
        if name in seen or (name in SKIP and not nodeps) or name not in pkgs:
            continue
        seen.add(name)
        if not nodeps:
            todo.extend(deps(pkgs[name]))
    for name in sorted(seen):
        f = pkgs[name]
        print(name, f["Version"], f["Filename"])


if __name__ == "__main__":
    main()
