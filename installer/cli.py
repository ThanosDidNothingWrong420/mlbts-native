"""Command line: python -m installer --dest DIR [--base NSP --update NSP --keys prod.keys] [--titlekeys FILE]
                                     [--root DIR] [--dlss] [--shortcut] [--import-save]

A first install needs the game files and the keys. On a folder with mlbts-install.json (resume after a cancel or a
failure, or update with a newer release) only --dest is needed, unless the extraction must run again. Prints JSON
events on stdout (events.py). Exit code: 0 done, 1 failed, 2 bad command line, 3 cancelled (Ctrl+C or Ctrl+Break).
"""
import argparse
import signal
import sys
import time
from pathlib import Path

from . import layout as layouts
from .events import Reporter
from .steps import Context, all_steps, run_steps

EXIT = {'ok': 0, 'failed': 1, 'cancelled': 3}


class Parser(argparse.ArgumentParser):
    def error(self, message):
        Reporter().error(f'Bad command line: {message}')
        sys.exit(2)


def cancel(signum, frame):
    raise KeyboardInterrupt


def main(argv=None):
    ap = Parser(prog='python -m installer', description=__doc__.splitlines()[0])
    ap.add_argument('--dest', type=Path, required=True, help='install folder')
    ap.add_argument('--base', type=Path, help='the base game NSP')
    ap.add_argument('--update', type=Path, help='the update NSP: 1.0.20 or 1.0.15')
    ap.add_argument('--keys', type=Path, help='prod.keys')
    ap.add_argument('--titlekeys', type=Path, help='title.keys (only when the NSPs lack their tickets)')
    ap.add_argument('--root', type=Path, help='the setup folder or a repository (default: the folder above installer/)')
    ap.add_argument('--dlss', action='store_true', help="download NVIDIA's nvngx_dlss.dll next to the game")
    ap.add_argument('--shortcut', action='store_true', help='desktop and Start menu shortcuts')
    ap.add_argument('--import-save', action='store_true', help="copy the game's Ryujinx save (mlb.exe --import-save)")
    args = ap.parse_args(argv)

    # The window cancels with Ctrl+Break to the setup's process group, which reaches the tools it runs too; Python's
    # default for it ends the process without recording anything
    for name in ('SIGBREAK', 'SIGTERM'):
        if hasattr(signal, name):
            signal.signal(getattr(signal, name), cancel)
    layout = layouts.find(args.root)
    dest = args.dest.resolve()
    log = layout.temp.parent / 'logs' / time.strftime('setup-%Y%m%d-%H%M%S.log')   # release: logs/ of the setup folder
    rep = Reporter(log_path=log)
    t0 = time.monotonic()
    ctx = Context(layout, dest, rep, args)
    steps = all_steps()
    rep.emit('start', release=layout.release, dev=layout.dev, dest=str(dest), log=str(log),
             steps=[{'id': s.id, 'title': s.title} for s in steps])
    result = run_steps(ctx, steps)
    rep.emit('end', result=result, seconds=round(time.monotonic() - t0, 1))
    rep.close()
    return EXIT[result]
