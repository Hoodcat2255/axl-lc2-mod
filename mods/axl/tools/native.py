"""Build and locate the small C helpers (unecm, fixsec) for the host with zig cc (Linux and Windows)."""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, 'bin')


def path(name):
    return os.path.join(BIN, name + ('.exe' if os.name == 'nt' else ''))


def build(name):
    exe = path(name)
    src = os.path.join(HERE, name + '.c')
    if not os.path.exists(exe) or os.path.getmtime(exe) < os.path.getmtime(src):
        os.makedirs(BIN, exist_ok=True)
        subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-O2', '-o', exe, src], check=True)
    return exe
