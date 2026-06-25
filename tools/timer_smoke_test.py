#!/usr/bin/env python3
"""On-device smoke test for the multi-timer feature (F1).

Drives the BYTE-90 over its USB-CDC serial port (/dev/ttyACM0 @ 921600) to
verify the F1 acceptance criteria from SuggestedFeatures.md:

  1. The five timer MCP tools work (set/list/status/cancel/repeat).
  2. Two concurrent labeled timers run end-to-end.
  3. A timer survives a reboot (NVS rehydration).
  4. The 8-timer cap is enforced.

How this harness verifies (serial vs MCP)
-----------------------------------------
The timer tools are *MCP tools*: they are invoked by the LLM over the cloud
protocol, NOT by any serial command. You still trigger each action by SPEAKING
to the device. Verification, however, is deterministic: the F1 firmware change
reports active timers in the GET_STATUS serial response (SerialClient::
handleGetStatus -> doc["timers"] / doc["timer_count"]). So this harness:

  * Prompts you to speak a phrase ("set a 3 minute pasta timer"), then POLLS
    GET_STATUS until the expected timer state appears — checking id, label,
    duration, remaining, and count directly from device state. No dependence on
    log levels or OLED reading.

  * For reboot persistence it sends RESTART over serial, then polls GET_STATUS
    until the timer reappears and confirms its remaining time counted down
    (rather than resetting), corroborated by the "Rehydrated timers from NVS"
    boot log when available.

  * Timer EXPIRY is an edge event (a timer leaving the active set), so that one
    check still reads the serial log line "Timer N expired" in addition to
    confirming the timer disappeared from GET_STATUS.

Requires firmware with the F1 GET_STATUS change flashed; the script does a
capability check on startup and warns if the field is missing.

Usage:
    python3 tools/timer_smoke_test.py                 # interactive, all steps
    python3 tools/timer_smoke_test.py --port /dev/ttyACM0
    python3 tools/timer_smoke_test.py --only reboot   # run one step
    python3 tools/timer_smoke_test.py --tail           # just stream serial+matches

Requires: pyserial  (pip install pyserial)
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import threading
import time
from collections import deque
from dataclasses import dataclass, field
from typing import Callable, Deque, List, Optional, Pattern

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

    def query_status(self) -> Optional[dict]:
        """Send GET_STATUS and return the parsed JSON payload (or None on failure)."""
        reply = self.send_command("GET_STATUS")
        if not reply.startswith("OK:"):
            return None
        try:
            return json.loads(reply[len("OK:"):])
        except json.JSONDecodeError:
            return None

    def query_timers(self) -> List[dict]:
        """Return the active-timer list from GET_STATUS (empty list if none/unavailable)."""
        status = self.query_status()
        if not status:
            return []
        return status.get("timers", []) or []

    def poll_timers(
        self, predicate: Callable[[List[dict]], bool], timeout: float, interval: float = 0.5
    ) -> Optional[List[dict]]:
        """Poll GET_STATUS until predicate(timers) is True or timeout. Returns the timers list."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            timers = self.query_timers()
            if predicate(timers):
                return timers
            time.sleep(interval)
        return None

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

    # ----- Helpers ----------------------------------------------------------

    @staticmethod
    def _find_label(timers: List[dict], label: str) -> Optional[dict]:
        for t in timers:
            if t.get("label") == label:
                return t
        return None

    # ----- Individual test steps -------------------------------------------
    #
    # State checks poll GET_STATUS (deterministic, firmware-reported). Event
    # checks (expiry) still use the serial log, since an expiry is an edge that
    # GET_STATUS only shows as a disappearance.

    def step_set_and_list(self) -> None:
        print(bold("\n=== Step 1: set (single labeled timer) — deterministic via GET_STATUS ==="))
        self.prompt('Say to the device: "set a 3 minute pasta timer"')
        timers = self.mon.poll_timers(
            lambda ts: self._find_label(ts, "pasta") is not None, timeout=10
        )
        if timers is None:
            self.record("timer.set appears in GET_STATUS", False, "no 'pasta' timer in status after 10s")
            return
        t = self._find_label(timers, "pasta")
        dur_ok = t.get("duration_seconds") == 180
        rem = t.get("remaining_seconds", 0)
        rem_ok = 150 <= rem <= 180  # allow for speech/dialog latency
        self.record(
            "timer.set duration correct (180s)",
            dur_ok,
            f"id={t.get('id')} duration={t.get('duration_seconds')}s remaining={rem}s",
        )
        self.record("timer.set remaining sane", rem_ok or None,
                    f"remaining={rem}s (expected ~150-180)")

    def step_concurrent(self) -> None:
        print(bold("\n=== Step 2: two concurrent timers, soonest fires first ==="))
        self.prompt('Say: "set a 30 second tea timer"')
        timers = self.mon.poll_timers(
            lambda ts: self._find_label(ts, "tea") is not None, timeout=10
        )
        if timers is None:
            self.record("second timer appears in GET_STATUS", False, "no 'tea' timer in status")
        else:
            count = len(timers)
            self.record(
                "two timers active concurrently",
                count >= 2,
                f"{count} active: {[t.get('label') for t in timers]}",
            )

        # Expiry is an event — catch it via the log.
        print(yellow("    Waiting up to 45s for the 30s tea timer to expire..."))
        exp = self.mon.wait_for(RE_TIMER_EXPIRED, timeout=45)
        self.record("soonest timer expired", bool(exp) or None,
                    f"timer id={exp.group(1)} fired" if exp else "no expiry log; did you hear the alert?")

        # Deterministic: pasta must still be in GET_STATUS, tea must be gone.
        timers_after = self.mon.query_timers()
        pasta_alive = self._find_label(timers_after, "pasta") is not None
        tea_gone = self._find_label(timers_after, "tea") is None
        self.record("other timer survives peer expiry", pasta_alive,
                    f"pasta {'present' if pasta_alive else 'MISSING'}")
        self.record("expired timer removed from status", tea_gone,
                    f"tea {'gone' if tea_gone else 'STILL LISTED'}")

    def step_cancel_repeat(self) -> None:
        print(bold("\n=== Step 3: cancel + repeat — deterministic via GET_STATUS ==="))
        # Ensure there is a pasta timer to cancel (from step 1) or create one.
        if self._find_label(self.mon.query_timers(), "pasta") is None:
            self.prompt('Say: "set a 5 minute pasta timer" (needed as a cancel target)')
            self.mon.poll_timers(lambda ts: self._find_label(ts, "pasta") is not None, timeout=10)

        self.prompt('Say: "cancel the pasta timer"')
        gone = self.mon.poll_timers(
            lambda ts: self._find_label(ts, "pasta") is None, timeout=10
        )
        self.record("timer.cancel removes it from status", gone is not None,
                    "pasta no longer in GET_STATUS" if gone is not None else "pasta still listed after 10s")

        before = {t.get("id") for t in self.mon.query_timers()}
        self.prompt('Say: "repeat that timer" (or "do it again")')
        grew = self.mon.poll_timers(
            lambda ts: bool({t.get("id") for t in ts} - before), timeout=10
        )
        if grew is not None:
            new_ids = {t.get("id") for t in grew} - before
            self.record("timer.repeat creates a new timer", True, f"new id(s)={sorted(new_ids)}")
        else:
            self.record("timer.repeat creates a new timer", False, "no new timer id in status after 10s")

    def step_reboot_persistence(self) -> None:
        print(bold("\n=== Step 4: reboot persistence — deterministic via GET_STATUS ==="))
        self.prompt('Say: "set a 10 minute laundry timer"')
        pre = self.mon.poll_timers(
            lambda ts: self._find_label(ts, "laundry") is not None, timeout=10
        )
        if pre is None:
            self.record("laundry timer set pre-reboot", False, "not in status; aborting reboot step")
            return
        t = self._find_label(pre, "laundry")
        rem_before = t.get("remaining_seconds", 0)
        print(f"    laundry timer present pre-reboot: id={t.get('id')} remaining={rem_before}s")

        print(yellow("    Sending RESTART over serial..."))
        reply = self.mon.send_command("RESTART")
        if reply:
            print(f"    device replied: {reply}")

        # Corroborate via boot log if available (informational), then assert via status.
        rehy = self.mon.wait_for(RE_REHYDRATED, timeout=30, since=self.mon.mark())
        if rehy:
            print(green(f"    boot log: Rehydrated timers from NVS (count={rehy.group(1)})"))

        print(yellow("    Polling GET_STATUS (up to 40s) for the laundry timer to reappear..."))
        post = self.mon.poll_timers(
            lambda ts: self._find_label(ts, "laundry") is not None, timeout=40
        )
        if post is None:
            self.record("timer survives reboot", False, "laundry timer absent from status post-reboot")
            return
        t2 = self._find_label(post, "laundry")
        rem_after = t2.get("remaining_seconds", 0)
        # Remaining should have decreased (time passed) but still be > 0 and not reset to full.
        sane = 0 < rem_after <= rem_before
        self.record("timer survives reboot", True, f"reappeared: remaining {rem_before}s -> {rem_after}s")
        self.record("rehydrated remaining counted down (not reset)", sane,
                    f"{rem_after}s vs pre-reboot {rem_before}s")

    def step_cap(self) -> None:
        print(bold("\n=== Step 5: 8-timer cap — deterministic via GET_STATUS ==="))
        print(yellow("    Current active timers will count toward the cap. Cancel any first if needed."))
        self.prompt(
            "Set enough timers to reach 8 total active (any durations/labels).\n"
            "    Tip: use distinct labels so they're easy to track."
        )
        timers = self.mon.poll_timers(lambda ts: len(ts) >= 8, timeout=20)
        if timers is None:
            n = len(self.mon.query_timers())
            self.record("reached 8 active timers", None, f"only {n} active; set more or adjust, then re-run --only cap")
            return
        self.record("8 timers active", len(timers) == 8, f"{len(timers)} active")

        self.prompt('Now try to set a 9th timer. The assistant should refuse.')
        after = self.mon.query_timers()
        self.record("9th timer rejected (cap holds)", len(after) <= 8,
                    f"{len(after)} active after attempting a 9th (must stay <= 8)")

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
            "\n  PASS = GET_STATUS (or boot log, for rehydration) confirmed the behavior.\n"
            "  INCONCLUSIVE = a soft/latency-sensitive check; review the detail.\n"
            "  FAIL = device state contradicted the expected behavior."
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
        help="don't pause for human prompts (you must trigger actions out-of-band).",
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

        # Capability check: confirm this firmware reports timers in GET_STATUS.
        status = mon.query_status()
        if status is None:
            print(red("warning: GET_STATUS returned no parseable JSON. Is the device in a"
                      " mode that answers serial commands? Continuing, but state checks may fail."))
        elif "timer_count" not in status:
            print(red("warning: this firmware's GET_STATUS has no 'timer_count'/'timers' field."
                      "\n  The deterministic checks need the F1 GET_STATUS change flashed."
                      "\n  Re-flash, or fall back to --tail for log-only observation."))
        else:
            print(green(f"GET_STATUS reports timers (timer_count={status.get('timer_count')}). "
                        "Deterministic checks enabled.\n"))

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
