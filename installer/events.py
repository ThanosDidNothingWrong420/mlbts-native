"""Progress events: one JSON object per line on stdout for the window and tests, and readable lines in a log file.

Every event has "type" and "t" (seconds since the start). Types:

    start     release, dest, log, steps ([{id, title}] in order)
    step      step, state: started | done (seconds) | skipped (reason)
    progress  step, text, and done/total/unit (bytes, chunks, objects) when the amount is known
    warning   text
    error     text, step (absent outside a step)
    end       result: ok | failed | cancelled, seconds

After TotK Recomp installer/events.py @ v0.4.0 (MIT), with the English text in the events (one language for now).
"""
import json
import sys
import threading
import time
from pathlib import Path

PROGRESS_EVERY = 0.4   # seconds between two progress events of one step, unless it is the last


class Reporter:
    def __init__(self, out=None, log_path=None):
        self.out = out or sys.stdout
        self.t0 = time.monotonic()
        self.lock = threading.Lock()
        self.log_file = None
        self.last_progress = {}
        self.log_path = log_path
        if log_path:
            Path(log_path).parent.mkdir(parents=True, exist_ok=True)
            self.log_file = open(log_path, 'a', encoding='utf-8', buffering=1)

    def elapsed(self):
        return time.monotonic() - self.t0

    def log(self, text):
        """A line for the log file only."""
        if self.log_file:
            with self.lock:
                self.log_file.write(f'[{self.elapsed():8.1f}] {text}\n')

    def emit(self, type, **fields):
        event = {'type': type, 't': round(self.elapsed(), 2), **fields}
        with self.lock:
            self.out.write(json.dumps(event, ensure_ascii=False) + '\n')
            self.out.flush()
        if type == 'progress':
            if 'total' in fields:
                self.log(f'{fields.get("text", "")} {fields.get("done")}/{fields["total"]} {fields.get("unit", "")}')
            else:
                self.log(fields.get('text', ''))
        else:
            self.log(json.dumps(event, ensure_ascii=False))
        return event

    def step(self, step, state, **fields):
        return self.emit('step', step=step, state=state, **fields)

    def progress(self, step, text, done=None, total=None, unit=None, force=False):
        """At most one progress event per PROGRESS_EVERY per step, except the first, the last and forced ones."""
        now = time.monotonic()
        last = self.last_progress.get(step)
        final = total is not None and done is not None and done >= total
        if last is not None and not final and not force and now - last < PROGRESS_EVERY:
            return None
        self.last_progress[step] = now
        fields = {'step': step, 'text': text}
        if done is not None and total:
            fields.update(done=done, total=total, unit=unit or '')
        return self.emit('progress', **fields)

    def warning(self, text):
        return self.emit('warning', text=text)

    def error(self, text, step=None):
        return self.emit('error', text=text, **({'step': step} if step else {}))

    def close(self):
        if self.log_file:
            self.log_file.close()
            self.log_file = None
