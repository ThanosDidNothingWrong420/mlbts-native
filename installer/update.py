"""Updating the port from the launcher: the newest setup on GitHub Releases, downloaded and unpacked beside this one.

The launcher asks GitHub's API for the latest release of the project (one small request, no data about the player sent;
nothing is checked in --selftest). When its version is newer than this setup's, the launcher offers the update:
download() fetches MLBTS-Setup-<version>-windows.zip, checks it against the SHA-256 GitHub lists for the file, and
unpacks it next to this setup folder as MLBTS-Setup-<version>/ (into %LOCALAPPDATA%\\MLBTS-Native\\setups when that
folder cannot be written). The launcher then starts the new MLBTS-Setup.exe with --update <install folder>, which
rebuilds mlb.exe with the new release; the game files, saves and settings stay.
"""
import hashlib
import json
import re
import shutil
import ssl
import urllib.request
import zipfile
from pathlib import Path

from . import layout as layouts

REPO = 'sam110411/mlbts-native'
API = f'https://api.github.com/repos/{REPO}/releases/latest'
ASSET = re.compile(r'^MLBTS-Setup-(\d+(?:\.\d+)*)-windows\.zip$')
HEADERS = {'User-Agent': 'MLBTS-Setup', 'Accept': 'application/vnd.github+json'}


class Cancelled(Exception):
    pass


def version_key(v):
    """'0.1.10' -> (0, 1, 10); '-dev' and other suffixes ignored; None when there is no number."""
    m = re.match(r'v?(\d+(?:\.\d+)*)', (v or '').strip())
    return tuple(int(x) for x in m.group(1).split('.')) if m else None


def newer(a, b):
    """Version a is newer than version b (unknown versions are never newer)."""
    ka, kb = version_key(a), version_key(b)
    return ka is not None and kb is not None and ka > kb


def latest(timeout=10):
    """The latest release's setup zip: {version, url, size, sha256 (or None), page}; None when there is none."""
    ctx = ssl.create_default_context()
    req = urllib.request.Request(API, headers=HEADERS)
    with urllib.request.urlopen(req, timeout=timeout, context=ctx) as r:
        rel = json.load(r)
    for a in rel.get('assets') or []:
        m = ASSET.match(a.get('name') or '')
        if m and a.get('browser_download_url'):
            digest = a.get('digest') or ''
            return {'version': m.group(1), 'url': a['browser_download_url'], 'size': int(a.get('size') or 0),
                    'sha256': digest[7:].lower() if digest.startswith('sha256:') else None,
                    'page': rel.get('html_url') or f'https://github.com/{REPO}/releases'}
    return None


def target_parent(setup_root):
    """Where the new setup folder goes: beside this one when that folder is writable."""
    parent = Path(setup_root).resolve().parent
    try:
        probe = parent / '.mlbts-write-test'
        probe.write_bytes(b'')
        probe.unlink()
        return parent
    except OSError:
        return layouts.settings_dir() / 'setups'


def ready(folder, version):
    """folder holds an unpacked setup of that version (its VERSION is '<version>+<commit>')."""
    try:
        return (folder / 'MLBTS-Setup.exe').is_file() and (folder / 'python' / 'pythonw.exe').is_file() and \
            version_key((folder / 'VERSION').read_text(encoding='utf-8')) == version_key(version)
    except OSError:
        return False


def download(info, setup_root, progress=None, cancel=None):
    """Download, check and unpack info's setup; returns the new setup folder. progress(text, done, total) is called
    as it goes; cancel() returning true stops it (Cancelled). Raises OSError / ValueError on failure."""
    parent = target_parent(setup_root)
    folder = parent / f'MLBTS-Setup-{info["version"]}'
    if ready(folder, info['version']):
        return folder
    parent.mkdir(parents=True, exist_ok=True)
    zpath = parent / f'MLBTS-Setup-{info["version"]}-windows.zip'
    part = zpath.with_name(zpath.name + '.part')
    h = hashlib.sha256()
    req = urllib.request.Request(info['url'], headers={'User-Agent': HEADERS['User-Agent']})
    try:
        with urllib.request.urlopen(req, timeout=60, context=ssl.create_default_context()) as r, \
                open(part, 'wb') as f:
            total = int(r.headers.get('Content-Length') or info.get('size') or 0)
            done = 0
            while True:
                if cancel and cancel():
                    raise Cancelled()
                block = r.read(1 << 20)
                if not block:
                    break
                f.write(block)
                h.update(block)
                done += len(block)
                if progress:
                    progress(f'Downloading version {info["version"]}: {done >> 20} of {total >> 20} MB', done, total)
        if total and done != total:
            raise OSError(f'the download stopped early ({done} of {total} bytes)')
        if info.get('sha256') and h.hexdigest() != info['sha256']:
            raise ValueError('the download is damaged (its checksum does not match GitHub\'s)')
        part.replace(zpath)
    except BaseException:
        part.unlink(missing_ok=True)
        raise
    stage = parent / f'.MLBTS-Setup-{info["version"]}-unpacking'
    shutil.rmtree(stage, ignore_errors=True)
    try:
        with zipfile.ZipFile(zpath) as z:
            names = z.infolist()
            top = {n.filename.split('/', 1)[0] for n in names}
            for n in names:   # never outside the staging folder
                p = Path(n.filename)
                if p.is_absolute() or '..' in p.parts or ':' in n.filename:
                    raise ValueError(f'unexpected path in the download: {n.filename}')
            for i, n in enumerate(names):
                if cancel and cancel():
                    raise Cancelled()
                z.extract(n, stage)
                if progress and i % 64 == 0:
                    progress(f'Unpacking version {info["version"]}', i, len(names))
        src = stage / top.pop() if len(top) == 1 else stage
        if not ready(src, info['version']):
            raise ValueError('the download is not a setup of that version')
        shutil.rmtree(folder, ignore_errors=True)
        src.replace(folder)
    finally:
        shutil.rmtree(stage, ignore_errors=True)
    zpath.unlink(missing_ok=True)
    return folder
