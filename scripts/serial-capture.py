#!/usr/bin/env python3
# Exper Easypad 8 — serial console capture tool (persistent version)
#
# Moved into repository from old /tmp/exper-capture2.py (lost on reboot).
# Features:
#   - raw termios, default 115200
#   - raw byte recording (<log>.raw) + line-based decoded log (<log>)
#   - auto reopen when port is lost (EIO/EOF)
#   - auto baud trial: readability <0.55 -> next baud,
#     >0.85 -> locks and stops changing (can be fixed with --baud)
#   - event lines: PORT OPEN / ### BAUD CHANGED / ### LOCKED
#   - active log path is written to logs/lastlog.txt
#
# Usage:
#   python3 scripts/serial-capture.py /dev/ttyUSB0 [--baud 115200]
#
# In background:
#   setsid nohup python3 scripts/serial-capture.py /dev/ttyUSB0 \
#     >> logs/capture.out 2>&1 < /dev/null &
#   pgrep -f serial-capture.py   # liveness check
#   cat logs/lastlog.txt         # active log path

import argparse
import os
import sys
import termios
import time

BAUDS = [115200, 9600, 19200, 38400, 57600,
         230400, 460800, 576000, 921600, 1500000]

LOGDIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "logs")


def ts():
    return time.strftime("%Y-%m-%d %H:%M:%S")


def open_port(dev, baud):
    fd = os.open(dev, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    speed = getattr(termios, f"B{baud}", None)
    if speed is None:
        raise ValueError(f"unsupported baud: {baud}")
    attrs[0] = attrs[1] = attrs[3] = 0          # iflag, oflag, lflag: raw
    attrs[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
    attrs[4] = speed
    attrs[5] = speed
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIFLUSH)
    return fd


def readability(buf):
    """Ratio of printable ASCII bytes at the end of the buffer (0..1)."""
    if not buf:
        return 0.0
    sample = buf[-4096:]
    ok = sum(1 for b in sample if 0x20 <= b < 0x7F or b in (0x0A, 0x0D, 0x09))
    return ok / len(sample)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("device", help="e.g. /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=None,
                    help="fixed baud (disables auto trial)")
    args = ap.parse_args()

    os.makedirs(LOGDIR, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    log_path = os.path.join(LOGDIR, f"boot-{stamp}.log")
    raw_path = log_path + ".raw"
    with open(os.path.join(LOGDIR, "lastlog.txt"), "w") as f:
        f.write(log_path + "\n")

    log = open(log_path, "ab", buffering=0)
    raw = open(raw_path, "ab", buffering=0)

    def ev(msg):
        line = f"\r\n### {ts()} {msg}\r\n".encode()
        log.write(line)
        print(line.decode(errors="replace"), end="")
        sys.stdout.flush()

    baud_idx = 0
    baud = args.baud or BAUDS[0]
    locked = args.baud is not None
    window = bytearray()

    ev(f"PORT OPEN {args.device} @ {baud} (log: {log_path})")

    while True:
        try:
            fd = open_port(args.device, baud)
        except (OSError, ValueError) as e:
            time.sleep(1.0)
            continue
        ev(f"PORT OPEN {args.device} @ {baud}")
        try:
            while True:
                try:
                    data = os.read(fd, 4096)
                except BlockingIOError:
                    time.sleep(0.01)
                    continue
                if not data:
                    raise OSError(EIO := 5)
                raw.write(data)
                if not locked:
                    window.extend(data)
                    r = readability(window)
                    if r > 0.85 and len(window) > 512:
                        locked = True
                        ev(f"LOCKED @ {baud} (readability {r:.2f})")
                    elif r < 0.55 and len(window) > 512:
                        window.clear()
                        baud_idx += 1
                        if baud_idx >= len(BAUDS):
                            baud_idx = 0
                        new_baud = BAUDS[baud_idx]
                        ev(f"BAUD CHANGED {baud} -> {new_baud} (readability {r:.2f})")
                        termios.tcsetattr(
                            fd, termios.TCSANOW,
                            (*termios.tcgetattr(fd)[:4],
                             getattr(termios, f"B{new_baud}"),
                             getattr(termios, f"B{new_baud}")))
                        baud = new_baud
                        window.clear()
                lines = data.replace(b"\r\n", b"\n").replace(b"\r", b"\n").split(b"\n")
                stamp = time.strftime("%H:%M:%S")
                for line in lines[:-1]:
                    log.write(f"[{stamp}] ".encode() + line + b"\n")
                    try:
                        print(f"[{stamp}] " + line.decode("utf-8", errors="replace"))
                    except Exception:
                        pass
                if lines[-1]:
                    log.write(f"[{stamp}] ".encode() + lines[-1])
                    try:
                        print(f"[{stamp}] " + lines[-1].decode("utf-8", errors="replace"), end="")
                    except Exception:
                        pass
                sys.stdout.flush()
        except (OSError, ValueError) as e:
            ev(f"PORT CLOSED ({e}); reopening in 1 sec")
            try:
                os.close(fd)
            except Exception:
                pass
            time.sleep(1.0)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
