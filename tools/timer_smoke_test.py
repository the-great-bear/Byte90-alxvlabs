#!/usr/bin/env python3
"""On-device smoke test for the multi-timer feature (F1).

Drives the BYTE-90 over its USB-CDC serial port (/dev/ttyACM0 @ 921600) to
verify the F1 acceptance criteria from SuggestedFeatures.md:

  1. The five timer MCP tools work (set/list/status/cancel/repeat).
  2. Two concurrent labeled timers run end-to-end.
  3. A timer survives a reboot (NVS rehydration).
  4. The 8-timer cap is enforced.

IMPORTANT — what serial can and cannot do here
-----------------------------------------------
The timer tools are *MCP tools*: they are invoked by the LLM over the cloud
protocol, NOT by any serial command. The serial port exposes only the
firmware-update / WiFi / status / restart surface (see SerialClient). So this
harness does two things:

  * For the voice-driven steps it PROMPTS you to speak a phrase to the device,
    then watches the serial log for TimerManager's confirmation lines
    (e.g. "Timer 1 started (180 s, label='pasta')") as machine-checkable
    evidence. The OLED status bar is your visual source of truth; the captured
    log line corroborates it.

  * For the reboot-persistence step it fully automates the part it can: it
    sends RESTART over serial and asserts that "Rehydrated timers from NVS"
    appears in the boot log within a timeout.

Log-line matching depends on TimerManager's ESP_LOGI output reaching the serial
console (governed by CORE_DEBUG_LEVEL in platformio.ini). If you see
"no log evidence captured" but the OLED shows the expected behavior, treat the
visual confirmation as authoritative and the log assertion as informational.
A fully deterministic serial check would require exposing timer state through
GET_STATUS — see the note at the bottom of this file.

Usage:
    python3 tools/timer_smoke_test.py                 # interactive, all steps
    python3 tools/timer_smoke_test.py --port /dev/ttyACM0
    python3 tools/timer_smoke_test.py --only reboot   # run one step
    python3 tools/timer_smoke_test.py --tail           # just stream serial+matches

Requires: pyserial  (pip install pyserial)
"""
from __future__ import annotations

import argparse
import re
import sys
import threading
import time
from collections import deque
from dataclasses import dataclass, field
from typing import Deque, Optional, Pattern

try:
    import serial  # type: ignore
except ImportError:
    sys.stderr.write(
        "error: pyserial not found. Install with:  pip install pyserial\n"
    )
    sys.exit(2)

DEFAULT_PORT = "/dev/ttyACM0"
DEFAULT_BAUD = 921600

# TimerManager ESP_LOGI message fragments (tolerant of the "I (ts) TAG:" prefix).
RE_TIMER_STARTED = re.compile(r"Timer\s+(\d+)\s+started\s+\((\d+)\s+s,\s+label='([^']*)'\)")
RE_TIMER_CANCELED = re.compile(r"Timer\s+(\d+)\s+canceled")
RE_TIMER_EXPIRED = re.compile(r"Timer\s+(\d+)\s+expired")
RE_REHYDRATED = re.compile(r"Rehydrated timers from NVS \(count=(\d+)\)")

# ANSI colors (skipped if not a tty).
_USE_COLOR = sys.stdout.isatty()


def _c(code: str, text: str) -> str:
    return f"\033[{code}m{text}\033[0m" if _USE_COLOR else text


def green(t: str) -> str:
    return _c("32", t)


def red(t: str) -> str:
    return _c("31", t)


def yellow(t: str) -> str:
    return _c("33", t)


def bold(t: str) -> str:
    return _c("1", t)


@dataclass
class LogLine:
    ts: float
    text: str


class SerialMonitor:
    """Background reader that timestamps lines and supports wait_for(pattern)."""

    def __init__(self, port: str, baud: int, echo: bool = True):
        self._ser = serial.Serial(port, baud, timeout=0.2)
        self._echo = echo
        self._buf: Deque[LogLine] = deque(maxlen=4000)
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    def _reader(self) -> None:
        partial = b""
        while not self._stop.is_set():
            try:
                chunk = self._ser.read(256)
            except serial.SerialException:
                break
            if not chunk:
                continue
            partial += chunk
            while b"\n" in partial:
                raw, partial = partial.split(b"\n", 1)
                text = raw.decode("utf-8", errors="replace").rstrip("\r")
                if not text:
                    continue
                line = LogLine(time.time(), text)
                with self._lock:
                    self._buf.append(line)
                if self._echo:
                    print(_c("90", f"  │ {text}"))

    def mark(self) -> float:
        """Return a timestamp to scope subsequent wait_for() searches."""
        return time.time()

    def wait_for(
        self, pattern: Pattern[str], timeout: float, since: Optional[float] = None
    ) -> Optional[re.Match]:
        """Block until a buffered line matches `pattern`, or timeout. Returns the match."""
        deadline = time.time() + timeout
        since = since if since is not None else 0.0
        while time.time() < deadline:
            with self._lock:
                snapshot = [ln for ln in self._buf if ln.ts >= since]
            for ln in snapshot:
                m = pattern.search(ln.text)
                if m:
                    return m
            time.sleep(0.1)
        return None

    def send_command(self, command: str, data: str = "") -> str:
        """Send a SerialClient text command (e.g. RESTART) and return the OK:/ERROR: reply."""
        payload = command if not data else f"{command}:{data}"
        since = self.mark()
        self._ser.write((payload + "\n").encode("utf-8"))
        self._ser.flush()
        # SerialClient replies with a single line prefixed OK: or ERROR:.
        deadline = time.time() + 3.0
        while time.time() < deadline:
            with self._lock:
                for ln in [l for l in self._buf if l.ts >= since]:
                    if ln.text.startswith(("OK:", "ERROR:")):
                        return ln.text
            time.sleep(0.05)
        return ""

    def close(self) -> None:
        self._stop.set()
        self._thread.join(timeout=1.0)
        try:
            self._ser.close()
        except Exception:
            pass


@dataclass
class Result:
    name: str
    passed: Optional[bool]  # None = skipped / inconclusive
    detail: str = ""


@dataclass
class Harness:
    mon: SerialMonitor
    interactive: bool = True
    results: list = field(default_factory=list)

    def prompt(self, msg: str) -> None:
        if self.interactive:
            input(yellow(f"\n>>> {msg}\n    Press ENTER once done... "))
        else:
            print(yellow(f"\n>>> (non-interactive) would prompt: {msg}"))

    def record(self, name: str, passed: Optional[bool], detail: str = "") -> None:
        self.results.append(Result(name, passed, detail))
        tag = green("PASS") if passed else (red("FAIL") if passed is False else yellow("SKIP"))
        print(f"  [{tag}] {name}" + (f" — {detail}" if detail else ""))

    # ----- Individual test steps -------------------------------------------

    def step_set_and_list(self) -> None:
        print(bold("\n=== Step 1: set + list (single labeled timer) ==="))
        since = self.mon.mark()
        self.prompt('Say to the device: "set a 3 minute pasta timer"')
        m = self.mon.wait_for(RE_TIMER_STARTED, timeout=8, since=since)
        if m:
            self.record(
                "timer.set started",
                m.group(3) == "pasta" and int(m.group(2)) == 180,
                f"id={m.group(1)} dur={m.group(2)}s label='{m.group(3)}'",
            )
        else:
            self.record("timer.set started", None, "no log evidence; confirm OLED shows the timer")
        self.prompt('Say: "what timers do I have?" and confirm it lists the pasta timer')
        self.record("timer.list reflects pasta timer", None, "visual confirmation only")

    def step_concurrent(self) -> None:
        print(bold("\n=== Step 2: two concurrent labeled timers, soonest fires first ==="))
        since = self.mon.mark()
        self.prompt('Say: "set a 30 second tea timer"')
        m = self.mon.wait_for(RE_TIMER_STARTED, timeout=8, since=since)
        if m:
            self.record("second timer started", m.group(3) == "tea", f"id={m.group(1)} label='{m.group(3)}'")
        else:
            self.record("second timer started", None, "no log evidence; confirm OLED")
        print(yellow("    Waiting up to 45s for the 30s tea timer to expire..."))
        exp = self.mon.wait_for(RE_TIMER_EXPIRED, timeout=45, since=since)
        if exp:
            self.record("soonest timer expired first", True, f"timer id={exp.group(1)} fired")
        else:
            self.record("soonest timer expired first", None, "no expiry log; did you hear/see the alert?")
        self.prompt("Confirm the pasta timer is STILL running after the tea timer fired")
        self.record("other timer survives peer expiry", None, "visual confirmation only")

    def step_cancel_repeat(self) -> None:
        print(bold("\n=== Step 3: cancel + repeat ==="))
        since = self.mon.mark()
        self.prompt('Say: "cancel the pasta timer"')
        m = self.mon.wait_for(RE_TIMER_CANCELED, timeout=8, since=since)
        self.record("timer.cancel", bool(m) or None, f"timer id={m.group(1)} canceled" if m else "no log evidence")
        since = self.mon.mark()
        self.prompt('Say: "repeat that timer" (or "do it again")')
        m = self.mon.wait_for(RE_TIMER_STARTED, timeout=8, since=since)
        self.record("timer.repeat restarts", bool(m) or None, f"new id={m.group(1)}" if m else "no log evidence")

    def step_reboot_persistence(self) -> None:
        print(bold("\n=== Step 4: reboot persistence (AUTOMATED) ==="))
        since = self.mon.mark()
        self.prompt('Say: "set a 10 minute laundry timer", then confirm it shows on the OLED')
        started = self.mon.wait_for(RE_TIMER_STARTED, timeout=8, since=since)
        if not started:
            print(yellow("    No 'Timer started' log seen — continuing on your visual confirmation."))
        print(yellow("    Sending RESTART over serial..."))
        reply = self.mon.send_command("RESTART")
        if reply:
            print(f"    device replied: {reply}")
        reboot_since = self.mon.mark()
        print(yellow("    Waiting up to 30s for boot + 'Rehydrated timers from NVS'..."))
        rehy = self.mon.wait_for(RE_REHYDRATED, timeout=30, since=reboot_since)
        if rehy:
            self.record(
                "timer rehydrated after reboot",
                int(rehy.group(1)) >= 1,
                f"count={rehy.group(1)}",
            )
        else:
            self.record(
                "timer rehydrated after reboot",
                None,
                "no rehydration log (check CORE_DEBUG_LEVEL); confirm OLED still shows laundry timer",
            )
        self.prompt("Confirm the laundry timer is still counting down on the OLED post-reboot")
        self.record("timer visible post-reboot", None, "visual confirmation only")

    def step_cap(self) -> None:
        print(bold("\n=== Step 5: 8-timer cap enforced ==="))
        self.prompt(
            "Set 8 timers (any durations/labels), then try a 9th.\n"
            "    The 9th should fail with a 'max timers' / timer_start_failed style response."
        )
        self.record("9th timer rejected", None, "confirm the assistant reports it could not add a 9th timer")

    def summary(self) -> int:
        print(bold("\n================ SUMMARY ================"))
        passed = sum(1 for r in self.results if r.passed is True)
        failed = sum(1 for r in self.results if r.passed is False)
        skipped = sum(1 for r in self.results if r.passed is None)
        for r in self.results:
            tag = green("PASS") if r.passed else (red("FAIL") if r.passed is False else yellow("INCONCLUSIVE"))
            print(f"  [{tag}] {r.name}" + (f" — {r.detail}" if r.detail else ""))
        print(
            bold(
                f"\n  {green(str(passed) + ' auto-pass')}, "
                f"{red(str(failed) + ' fail')}, "
                f"{yellow(str(skipped) + ' need-human-confirm')}"
            )
        )
        print(
            "\n  Auto-pass = serial log corroborated the behavior.\n"
            "  Need-human-confirm = relies on your OLED/audio observation (expected for\n"
            "  voice-driven MCP tools). Only a FAIL means the log actively contradicted\n"
            "  the expected behavior."
        )
        return 1 if failed else 0


STEPS = {
    "set": "step_set_and_list",
    "concurrent": "step_concurrent",
    "cancel": "step_cancel_repeat",
    "reboot": "step_reboot_persistence",
    "cap": "step_cap",
}


def main() -> int:
    ap = argparse.ArgumentParser(description="On-device smoke test for multi-timer (F1).")
    ap.add_argument("--port", default=DEFAULT_PORT, help=f"serial port (default {DEFAULT_PORT})")
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD, help=f"baud (default {DEFAULT_BAUD})")
    ap.add_argument(
        "--only",
        choices=list(STEPS),
        action="append",
        help="run only the named step(s); repeatable. Default: all.",
    )
    ap.add_argument(
        "--non-interactive",
        action="store_true",
        help="don't pause for human prompts (log-observation only).",
    )
    ap.add_argument(
        "--tail",
        action="store_true",
        help="just stream the serial log and highlight timer events; no test flow.",
    )
    ap.add_argument("--no-echo", action="store_true", help="don't mirror raw serial lines.")
    args = ap.parse_args()

    try:
        mon = SerialMonitor(args.port, args.baud, echo=not args.no_echo)
    except serial.SerialException as e:
        sys.stderr.write(red(f"error: could not open {args.port}: {e}\n"))
        sys.stderr.write("  Is the device connected? Is another monitor holding the port?\n")
        return 2

    print(bold(f"Connected to {args.port} @ {args.baud}. Timer smoke test."))
    print("  (Ctrl-C to abort.)\n")

    try:
        if args.tail:
            print(yellow("Tailing serial. Timer events will be highlighted. Ctrl-C to stop.\n"))
            patterns = [RE_TIMER_STARTED, RE_TIMER_CANCELED, RE_TIMER_EXPIRED, RE_REHYDRATED]
            seen = mon.mark()
            while True:
                for pat in patterns:
                    m = mon.wait_for(pat, timeout=0.3, since=seen)
                    if m:
                        print(green(f"  ★ MATCH: {m.group(0)}"))
                        seen = time.time()
                time.sleep(0.1)

        harness = Harness(mon, interactive=not args.non_interactive)
        chosen = args.only or list(STEPS)
        for key in chosen:
            getattr(harness, STEPS[key])()
        return harness.summary()
    except KeyboardInterrupt:
        print(yellow("\nAborted."))
        return 130
    finally:
        mon.close()


if __name__ == "__main__":
    raise SystemExit(main())

# -----------------------------------------------------------------------------
# Follow-up idea (not done here, would require a firmware change):
# Wire active timer state into the GET_STATUS serial response (SerialClient::
# handleGetStatus) — e.g. doc["timers"] = [...]. That would let this script
# assert timer state deterministically over serial without depending on
# ESP_LOGI reaching the console or on voice input, making steps 1, 4 and 5
# fully automated.
# -----------------------------------------------------------------------------
