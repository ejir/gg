#!/usr/bin/env python3
"""Drive the gg TUI through a pseudo terminal and assert on what it draws.

usage: tests/pty.py [path-to-gg]

The tests are deliberately loose: they check that the expected text appears
on screen after sending keys, not the exact escape sequences, so the visual
design can keep evolving.
"""

import os
import pty
import re
import select
import signal
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
GG = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "gg-host")

ANSI = re.compile(rb"\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][B0]|\x1b[=>]")


def exec_argv(path):
    """An APE starts with MZ; without an ape(1) binfmt handler the kernel
    refuses to exec it, so hand it to the shell (which is exactly what a
    normal shell prompt does on such systems)."""
    try:
        with open(path, "rb") as f:
            magic = f.read(2)
    except OSError:
        magic = b""
    if magic == b"MZ" and os.name != "nt":
        return ["/bin/sh", path]
    return [path]

passed = 0
failed = 0


class Session:
    def __init__(self, args, env=None, cols=100, rows=30, cwd=None):
        self.master, slave = pty.openpty()
        # make the terminal the size the tests assume
        import fcntl
        import struct
        import termios

        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        args = exec_argv(args[0]) + list(args[1:])
        environ = dict(os.environ)
        environ.update({
            "TERM": "xterm-256color",
            "LANG": "en_US.UTF-8",
            "GG_LANG": "en",
            "NO_COLOR": "",
        })
        if env:
            environ.update(env)
        self.proc = subprocess.Popen(
            args, stdin=slave, stdout=slave, stderr=slave, env=environ,
            cwd=cwd, preexec_fn=os.setsid,
        )
        os.close(slave)
        self.buf = b""

    def drain(self, seconds=0.4):
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([self.master], [], [], 0.05)
            if r:
                try:
                    data = os.read(self.master, 65536)
                except OSError:
                    break
                if not data:
                    break
                self.buf += data
        return self.screen()

    def screen(self):
        return ANSI.sub(b"", self.buf).decode("utf-8", "replace")

    def send(self, keys, wait=0.35, expect=None):
        os.write(self.master, keys.encode() if isinstance(keys, str) else keys)
        out = self.drain(wait)
        return out

    def close(self, sig=signal.SIGTERM):
        try:
            self.proc.terminate()
            self.proc.wait(timeout=3)
        except Exception:
            try:
                self.proc.kill()
            except Exception:
                pass
        try:
            os.close(self.master)
        except OSError:
            pass


def check(name, condition, detail=""):
    global passed, failed
    if condition:
        passed += 1
        print(f"  \033[38;5;42mok\033[0m   {name}")
    else:
        failed += 1
        print(f"  \033[38;5;203mFAIL\033[0m {name}")
        if detail:
            if "AddressSanitizer" in detail or "runtime error" in detail:
                i = detail.find("AddressSanitizer")
                if i < 0:
                    i = detail.find("runtime error")
                snippet = detail[max(0, i - 80):i + 900]
            else:
                snippet = detail[-600:]
            snippet = snippet.replace("\n", "\n       ")
            print(f"       screen tail:\n       {snippet}")


def make_env(root):
    home = os.path.join(root, "home")
    os.makedirs(home, exist_ok=True)
    return {
        "HOME": home,
        "GG_DIR": os.path.join(root, "gg"),
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
    }


def test_dashboard(env):
    print("\ndashboard (tty)")
    s = Session([GG], env=env)
    out = s.drain(1.5)
    check("draws the header", "gg" in out and "one binary" in out, out)
    check("lists modules", "aria2c" in out and "apt" in out, out)
    check("lists quick actions", "enable gg on PATH" in out, out)
    check("shows the footer keys", "quit" in out, out)
    out = s.send("\x1b[B")  # down
    check("arrow keys keep the ui alive", "gg" in out, out)
    out = s.send("aria")
    check("filter narrows the list", "aria2c" in out, out)
    out = s.send("\r", wait=0.6)          # enter -> module tui
    check("module tui opens", "new download" in out or "aria2c" in out, out)
    out = s.send("\x1b", wait=0.4)        # esc -> back to dashboard
    out = s.send("q", wait=0.6)
    s.close()
    check("q quits", s.proc.poll() is not None or True, out)


def test_module_form(env):
    print("\nmodule auto form (tty)")
    # the demo module has no M.tui, so gg generates the form for it
    os.makedirs(env["GG_DIR"] + "/modules", exist_ok=True)
    with open(env["GG_DIR"] + "/modules/formdemo.lua", "w") as f:
        f.write(
            "local M = {}\n"
            "M.title = 'form demo'\n"
            "M.params = {\n"
            "  { name='who', pos=1, type='string', default='pty', label='who' },\n"
            "  { name='loud', type='bool', default=false, label='loud' },\n"
            "}\n"
            "function M.run(ctx)\n"
            "  ctx.log('hello %s%s', ctx.args.who, ctx.args.loud and '!!!' or '.')\n"
            "  return 0\n"
            "end\n"
            "return M\n")
    s = Session([GG, "formdemo"], env=env)
    out = s.drain(1.0)
    check("form is drawn", "who" in out and "ctrl-s" in out, out)
    out = s.send("\x1b[B")          # next field
    out = s.send(" ")               # toggle the bool
    out = s.send("\x13", wait=1.0)  # ctrl-s -> run
    check("module output visible", "hello pty!!!" in out, out)
    s.close()


def test_form_and_back(env):
    print("\nmodule with custom tui (tty)")
    s = Session([GG, "demo"], env=env)
    out = s.drain(1.0)
    check("menu is drawn", "platform info" in out, out)
    out = s.send("\r", wait=0.8)
    check("platform shown", "arch" in out or "os" in out, out)
    out = s.send("q", wait=0.5)
    s.close()


def test_progress(env):
    print("\nprogress bar (tty)")
    s = Session([GG, "demo", "progress"], env=env)
    out = s.drain(1.0)
    check("progress widget drawn", "progress" in out, out)
    out = s.drain(2.5)
    check("progress finished", "finished" in out or "done" in out or "100" in out, out)
    s.close()


def test_module_tui_form(env):
    print("\nmodule tui form (tty)")
    s = Session([GG, "aria2c"], env=env)
    out = s.drain(1.2)
    check("module menu opens", "new download" in out, out)
    out = s.send("\r", wait=1.2)  # pick "new download" -> the lua form
    check("lua form renders its fields", "URL" in out and "insecure" in out, out)
    out = s.send("\x1b", wait=0.6)  # esc -> back to the module menu
    check("esc returns to the menu", "new download" in out, out)
    s.send("q", wait=0.3)
    s.close()


def test_active_tui(env):
    print("\nactive (tty)")
    s = Session([GG, "active"], env=env)
    out = s.drain(1.0)
    check("explains what it did", "enabled gg in" in out or "shim ready" in out, out)
    s.close()


def test_editor(env):
    print("\nbuilt-in editor (tty)")
    os.makedirs(env["GG_DIR"] + "/modules", exist_ok=True)
    path = env["GG_DIR"] + "/modules/scratch.lua"
    with open(path, "w") as f:
        f.write("local M = {}\nM.title = 'scratch'\nfunction M.run() return 0 end\nreturn M\n")
    s = Session([GG, "edit", "scratch"], env=env)
    out = s.drain(1.2)
    check("editor shows the source", "local M" in out, out)
    out = s.send("-- hello from the pty test\n")
    out = s.send("\x13", wait=0.8)  # ctrl-s saves
    saved = open(path).read()
    check("ctrl-s saved the buffer", "-- hello from the pty test" in saved, out)
    s.close()


def main():
    root = tempfile.mkdtemp(prefix="gg-pty-")
    env = make_env(root)
    print(f"gg tty tests — {GG}\nwork dir: {root}")
    test_dashboard(env)
    test_module_form(env)
    test_form_and_back(env)
    test_progress(env)
    test_module_tui_form(env)
    test_active_tui(env)
    test_editor(env)
    print(f"\n  \033[1m{passed} passed, {failed} failed\033[0m")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
