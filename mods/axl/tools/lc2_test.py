"""Run Legacy Collection 2 once with the Axl DLL's test harness and collect its output.

usage: lc2_test.py SCRIPT [--shots T1,T2,...] [--timeout S] [--tag NAME]
  SCRIPT: an axl_test.txt (see mods/axl/lc2/axl_test.c); copied next to RXC2.exe for this run, removed afterwards.
  `snap F` lines in the script are grabbed as soon as the DLL logs them -> <tag>_fF.png.
  --shots: seconds after launch to grab the left monitor (ffmpeg x11grab) -> work/lc2/test/<tag>_<t>.png
Output: work/lc2/test/<tag>.log (axl_lc2.log), <tag>_pdump.bin when the script dumps.
The game is started through Steam (steam -applaunch 743900: Steam's runtime has the codecs the X6 movies need;
`proton run` outside it crashes on the Capcom logo). The DLL loads through the prefix override that
lc2_install.py install sets. The script should end with `quit F`; --timeout kills the game otherwise.
"""
import argparse
import os
import re
import shutil
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lc2_install as I  # noqa: E402
import lc2_fmt as F  # noqa: E402

OUT = os.path.join(F.BUILT, 'test')
# the game's save (Steam cloud folder): a scenario must not save over the user's data. It is copied before the run
# (work/lc2/save_backup/savedata_<time>.dat, one per run, kept) and put back when the run changed it - also when the
# run stops on an error or Ctrl-C (the finally below)
SAVE = F.C.LC2_SAVE   # (axl_config)


def save_digest():
    if not os.path.exists(SAVE):
        return None
    import hashlib
    return hashlib.sha1(open(SAVE, 'rb').read()).hexdigest()
# GNOME on Wayland: the Xwayland root is black, so the game's own X window is grabbed


def running():
    return subprocess.run(['pgrep', '-x', 'RXC2.exe'], capture_output=True).returncode == 0


def game_window():
    out = subprocess.run(['xwininfo', '-root', '-tree'], capture_output=True, text=True).stdout
    best = None
    for line in out.splitlines():
        if 'steam_app_743900' in line.lower():
            m = re.match(r'\s*(0x[0-9a-f]+) .*?(\d+)x(\d+)\+', line)
            if m and (best is None or int(m.group(2)) * int(m.group(3)) > best[1]):
                best = (m.group(1), int(m.group(2)) * int(m.group(3)))
    return best[0] if best else None


def grab(path):
    w = game_window()
    if not w:
        return
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'x11grab', '-window_id', w, '-i', ':0',
                    '-frames:v', '1', path], check=False)


def monitor_source():
    """the default output's monitor (PulseAudio / PipeWire): the game's sound"""
    out = subprocess.run(['ffmpeg', '-hide_banner', '-sources', 'pulse'], capture_output=True, text=True).stdout
    m = re.search(r'^\*\s*(\S+\.monitor)', out, re.M) or re.search(r'^\s*(\S+\.monitor)', out, re.M)   # (the
    # starred one, the default: the first was HDMI's while the game played on a headset - silent, 2026-11-12)
    return m.group(1) if m else None


def record(path, secs):
    """the game window (and its sound) for secs seconds into path (runs in the background)"""
    w = game_window()
    if not w:
        return None
    mon = monitor_source()
    audio = ['-f', 'pulse', '-i', mon] if mon else []
    return subprocess.Popen(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'x11grab', '-framerate', '60',
                             '-window_id', w, '-i', ':0'] + audio + ['-t', str(secs), '-pix_fmt', 'yuv420p', path])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('script')
    ap.add_argument('--shots', default='')
    ap.add_argument('--timeout', type=float, default=180)
    ap.add_argument('--tag', default='run')
    ap.add_argument('--rec', default='', help='SNAP:SECS - the snap at frame SNAP records SECS seconds instead')
    ap.add_argument('--video', type=float, default=0, help='SECS: record the game window from when it appears -> <tag>.mkv')
    a = ap.parse_args()
    if running():
        sys.exit('RXC2.exe is already running')
    os.makedirs(OUT, exist_ok=True)
    for f in ('axl_lc2.log', 'axl_pdump.bin', 'axl_tdump.bin'):
        p = os.path.join(I.GAME_DIR, f)
        if os.path.exists(p):
            os.remove(p)
    test_txt = os.path.join(I.GAME_DIR, 'axl_test.txt')
    shutil.copyfile(a.script, test_txt)
    log_path = os.path.join(I.GAME_DIR, 'axl_lc2.log')
    done_snaps = set()

    def new_snaps():
        if not os.path.exists(log_path):
            return []
        out = []
        for line in open(log_path, errors='replace'):
            m = re.search(r'\] snap (\d+)', line)
            if m and int(m.group(1)) not in done_snaps:
                done_snaps.add(int(m.group(1)))
                out.append(int(m.group(1)))
        return out

    save0 = save_digest()
    save_dir = os.path.join(F.BUILT, 'save_backup')
    save_copy = os.path.join(save_dir, 'savedata_%s.dat' % time.strftime('%Y%m%d_%H%M%S'))
    if save0:
        os.makedirs(save_dir, exist_ok=True)
        shutil.copyfile(SAVE, save_copy)
    t0 = time.time()
    # not waited for: with Steam closed this command becomes the Steam client and runs on (it stays after the test)
    subprocess.Popen(['steam', '-applaunch', '743900'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                     start_new_session=True)
    shots = sorted(float(s) for s in a.shots.split(',') if s)
    rec = dict(r.split(':') for r in a.rec.split(',') if r)
    recs = []
    seen = False
    try:
        while time.time() - t0 < a.timeout:
            if shots and time.time() - t0 >= shots[0]:
                grab(os.path.join(OUT, '%s_%03d.png' % (a.tag, int(shots[0]))))
                shots.pop(0)
            for n in new_snaps():
                if str(n) in rec:
                    recs.append(record(os.path.join(OUT, '%s_rec%05d.mp4' % (a.tag, n)), rec[str(n)]))
                    continue
                grab(os.path.join(OUT, '%s_f%05d.png' % (a.tag, n)))
            up = running()
            if a.video and up and not seen:
                time.sleep(5)   # (the window first)
                recs.append(record(os.path.join(OUT, a.tag + '.mkv'), a.video))   # (mkv: readable if cut short)
                open(os.path.join(OUT, a.tag + '.video_start'), 'w').write('%.3f\n' % time.time())
            seen = seen or up
            if (seen and not up) or (not seen and time.time() - t0 > 180):   # (Steam itself may start first)
                break
            time.sleep(0.05)
    finally:
        for r in recs:   # (the game gone: SIGINT lets ffmpeg finish the file - a kill leaves an mp4 unreadable)
            if r:
                if a.video or not running():
                    r.send_signal(signal.SIGINT)
                try:
                    r.wait(timeout=60)
                except subprocess.TimeoutExpired:
                    r.kill()
        for _ in range(1 if seen else 10):   # (not seen: Steam may still start it - without the harness)
            if running():
                subprocess.run(['pkill', '-x', 'RXC2.exe'])
                time.sleep(3)
            elif not seen:
                time.sleep(3)
        if os.path.exists(test_txt):
            os.remove(test_txt)
        if save_digest() != save0:
            if save0:
                shutil.copyfile(save_copy, SAVE)   # (copyfile: a new mtime, the restored file is the newest)
                print('NOTE savedata.dat changed during the run: put back from %s' % save_copy)
            else:
                print('NOTE savedata.dat appeared during the run (none before): left as it is (%s)' % SAVE)
    for f, dst in (('axl_lc2.log', a.tag + '.log'), ('axl_pdump.bin', a.tag + '_pdump.bin'),
                   ('axl_tdump.bin', a.tag + '_tdump.bin')):
        p = os.path.join(I.GAME_DIR, f)
        if os.path.exists(p):
            shutil.move(p, os.path.join(OUT, dst))
    print('ran %.0f s; output in %s' % (time.time() - t0, OUT))
    if not seen:
        sys.exit('the game never started')


if __name__ == '__main__':
    main()
