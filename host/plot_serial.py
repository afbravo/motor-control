#!/usr/bin/env python3
"""Plot numeric key/value messages arriving over a serial port.

Expected input examples:
    angle: 123.456789 degrees
    speed: 42.0 rpm
    duty: 45.0,52.5,47.5

The first word is used as the series name. Any later numeric value on the
line is plotted, so additional telemetry fields can be added incrementally.
"""

from __future__ import annotations

import argparse
import re
import time
from collections import defaultdict, deque

import matplotlib.pyplot as plt
import serial


NUMBER = r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?"

VALUE_RE = re.compile(
    r"^\s*(?P<name>[A-Za-z_][\w-]*)\s*:\s*"
    rf"(?P<value>{NUMBER})"
)

DUTY_RE = re.compile(
    rf"^\s*duty\s*:\s*({NUMBER})\s*,\s*"
    rf"({NUMBER})\s*,\s*({NUMBER})"
)


def parse_line(line: str) -> list[tuple[str, float]]:
    duty_match = DUTY_RE.match(line)
    if duty_match is not None:
        return [
            ("duty_a", float(duty_match.group(1))),
            ("duty_b", float(duty_match.group(2))),
            ("duty_c", float(duty_match.group(3))),
        ]

    match = VALUE_RE.match(line)
    if match is None:
        return []
    return [(match.group("name"), float(match.group("value")))]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="serial device, e.g. /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--seconds",
        type=float,
        default=30.0,
        help="seconds of history to show (default: 30)",
    )
    parser.add_argument(
        "--show",
        nargs="+",
        help="only plot these series, e.g. --show angle speed",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    allowed = set(args.show) if args.show else None
    history: dict[str, deque[tuple[float, float]]] = defaultdict(deque)

    with serial.Serial(args.port, args.baud, timeout=0.1) as port:
        # Opening a USB serial adapter can reset some boards. Give it time to
        # finish booting before consuming data.
        time.sleep(0.5)
        port.reset_input_buffer()

        plt.ion()
        figure, axis = plt.subplots()
        axis.set_xlabel("Time (s)")
        axis.set_ylabel("Value")
        axis.grid(True)

        start = time.monotonic()
        lines = {}

        while plt.fignum_exists(figure.number):
            raw = port.readline()
            if raw:
                text = raw.decode("utf-8", errors="replace").strip()
                for name, value in parse_line(text):
                    if allowed is None or name in allowed:
                        now = time.monotonic() - start
                        history[name].append((now, value))

            now = time.monotonic() - start
            cutoff = now - args.seconds
            for name, samples in history.items():
                while samples and samples[0][0] < cutoff:
                    samples.popleft()

                if name not in lines:
                    (line,) = axis.plot([], [], label=name)
                    lines[name] = line
                    axis.legend()

                lines[name].set_data(
                    [sample[0] for sample in samples],
                    [sample[1] for sample in samples],
                )

            axis.set_xlim(max(0.0, now - args.seconds), max(args.seconds, now))
            axis.relim()
            axis.autoscale_view(scalex=False, scaley=True)
            figure.canvas.draw_idle()
            figure.canvas.flush_events()
            plt.pause(0.01)


if __name__ == "__main__":
    main()
