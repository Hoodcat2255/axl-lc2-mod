"""End-to-end scenarios of the Axl mod on Legacy Collection 2: the game driven by the DLL's test harness.

usage: lc2_e2e.py [NAME ...] [--keep]
  NAME: mods/axl/tools/e2e/NAME.txt (all scenarios there when none given; start.txt is not one)
Each scenario runs alone (one game at a time) through lc2_test.py, with e2e/start.txt (collection menu -> X6, the
opening skipped, play from frame 7990) in front of it; 'include NAME' lines take e2e/NAME.inc. A scenario is the harness's 'seq' language (axl_test.c):
its 'expect' / 'wait' lines log 'e2e PASS' / 'e2e FAIL', 'end' logs 'e2e end pass P fail F'.
Prints one line per scenario (PASS, or FAIL with the failing lines); exit 0 only when every scenario reached its end
with no failure. A scenario that never logs its end (a crash, the timeout) fails. Logs and snaps stay in
work/lc2/test/e2e_<name>*.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lc2_fmt as F  # noqa: E402

DIR = os.path.join(HERE, 'e2e')
OUT = os.path.join(F.BUILT, 'test')
TIMEOUT = 3600   # seconds per scenario (intro_boss about 5 minutes, ending about 15)


def scenarios(names):
    if names:
        return names
    return sorted(f[:-4] for f in os.listdir(DIR) if f.endswith('.txt') and f != 'start.txt')


def expand(text, depth=0):
    """'include NAME' lines -> the lines of e2e/NAME.inc (OSError: a missing one)"""
    out = []
    for line in text.splitlines():
        w = line.split()
        if len(w) == 2 and w[0] == 'include':
            if depth > 4:
                raise OSError('include nested too deep: %s' % w[1])
            out.append(expand(open(os.path.join(DIR, w[1] + '.inc')).read(), depth + 1))
        else:
            out.append(line)
    return '\n'.join(out) + '\n'


def run(name):
    """(ok, lines): the scenario's verdict and the lines that explain it"""
    src = os.path.join(DIR, name + '.txt')
    if not os.path.exists(src):
        return False, ['no scenario %s' % src]
    os.makedirs(OUT, exist_ok=True)
    script = os.path.join(OUT, 'e2e_%s.txt' % name)
    try:
        body = expand(open(src).read())
    except OSError as e:
        return False, ['include: %s' % e]
    with open(script, 'w') as f:
        f.write(open(os.path.join(DIR, 'start.txt')).read() + body)
    tag = 'e2e_' + name
    log = os.path.join(OUT, tag + '.log')
    if os.path.exists(log):
        os.remove(log)   # (a run that never starts the game must not be judged on the last run's log)
    r = subprocess.run([sys.executable, '-I', os.path.join(HERE, 'lc2_test.py'), script, '--timeout', str(TIMEOUT),
                        '--tag', tag], capture_output=True, text=True)
    notes = [ln for ln in r.stdout.splitlines() if ln.startswith('NOTE')]
    if r.returncode or not os.path.exists(log):
        return False, ['lc2_test.py failed (%d): %s' % (r.returncode, (r.stderr or r.stdout)[-300:])] + notes
    text = open(log, errors='replace').read()
    fails = re.findall(r'^\[\s*\d+\] (e2e FAIL.*|seq: bad step.*|e2e: the scenario ran out.*)$', text, re.M)
    end = re.search(r'e2e end pass (\d+) fail (\d+)', text)
    if not end:
        fails.append('no "e2e end" in the log (crash or timeout)')
    return not fails, fails + notes


def main():
    names = scenarios([a for a in sys.argv[1:] if not a.startswith('-')])
    bad = 0
    for n in names:
        ok, lines = run(n)
        bad += not ok
        print('%s %s' % ('PASS' if ok else 'FAIL', n))
        for ln in lines:
            print('    ' + ln)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
