#!/usr/bin/env python3
"""ggshot — run gg inside a pty and print a plain text screenshot.

usage: tools/ggshot.py <cols> <rows> <keys> -- <gg args...>

`keys` is a comma separated list of key names (enter, esc, down, ctrl-c, text,
delay:0.5).  Handy while hacking on the TUI:

    tools/ggshot.py 100 30 'text:aria2c,enter,delay:1' -- gg
"""
import fcntl
import os
import pty
import select
import struct
import subprocess
import sys
import tempfile
import termios
import time

KEYS = {
    "enter": "\r", "esc": "\x1b", "tab": "\t", "backspace": "\x7f",
    "up": "\x1b[A", "down": "\x1b[B", "left": "\x1b[D", "right": "\x1b[C",
    "space": " ", "ctrl-c": "\x03", "ctrl-s": "\x13",
}


class Screen:
    def __init__(self, cols, rows):
        self.cols, self.rows = cols, rows
        self.grid = [[" "] * cols for _ in range(rows)]
        self.r = self.c = 0
        self.saved = (0, 0)

    def put(self, ch):
        if ch == "\n":
            self.r = min(self.rows - 1, self.r + 1)
            return
        if ch == "\r":
            self.c = 0
            return
        if ch == "\b":
            self.c = max(0, self.c - 1)
            return
        if self.c >= self.cols:
            self.c = 0
            self.r = min(self.rows - 1, self.r + 1)
        self.grid[self.r][self.c] = ch
        self.c += 1

    def feed(self, data):
        i = 0
        text = data.decode("utf-8", "replace")
        while i < len(text):
            ch = text[i]
            if ch == "\x1b":
                j = i + 1
                if j < len(text) and text[j] == "[":
                    j += 1
                    while j < len(text) and not ("\x40" <= text[j] <= "\x7e"):
                        j += 1
                    seq = text[i + 2:j]
                    final = text[j] if j < len(text) else ""
                    self.csi(seq, final)
                    i = j + 1
                    continue
                if j < len(text) and text[j] == "]":
                    while j < len(text) and text[j] != "\x07":
                        j += 1
                    i = j + 1
                    continue
                i = j + 1
                continue
            self.put(ch)
            i += 1

    def csi(self, seq, final):
        parts = seq.split(";") if seq else []
        nums = []
        for p in parts:
            try:
                nums.append(int(p))
            except ValueError:
                nums.append(0)
        if final == "H" or final == "f":
            self.r = max(0, (nums[0] if nums else 1) - 1)
            self.c = max(0, (nums[1] if len(nums) > 1 else 1) - 1)
        elif final == "J":
            if seq in ("2", ""):
                self.grid = [[" "] * self.cols for _ in range(self.rows)]
            elif seq == "0":
                for c in range(self.c, self.cols):
                    self.grid[self.r][c] = " "
                for r in range(self.r + 1, self.rows):
                    for c in range(self.cols):
                        self.grid[r][c] = " "
        elif final == "K":
            if seq in ("", "0"):
                for c in range(self.c, self.cols):
                    self.grid[self.r][c] = " "
            elif seq == "2":
                for c in range(self.cols):
                    self.grid[self.r][c] = " "
        elif final == "A":
            self.r = max(0, self.r - max(1, nums[0] if nums else 1))
        elif final == "B":
            self.r = min(self.rows - 1, self.r + max(1, nums[0] if nums else 1))
        elif final == "C":
            self.c = min(self.cols - 1, self.c + max(1, nums[0] if nums else 1))
        elif final == "D":
            self.c = max(0, self.c - max(1, nums[0] if nums else 1))

    def dump(self):
        out = []
        for row in self.grid:
            line = "".join(row).rstrip()
            out.append(line)
        while out and not out[-1]:
            out.pop()
        return "\n".join(out)


def main():
    if "--" not in sys.argv:
        print(__doc__)
        return 2
    head = sys.argv[1:sys.argv.index("--")]
    cols, rows = int(head[0]), int(head[1])
    keys = head[2] if len(head) > 2 else ""
    args = sys.argv[sys.argv.index("--") + 1:]
    root = tempfile.mkdtemp(prefix="ggshot-")
    os.makedirs(os.path.join(root, "home"), exist_ok=True)
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    env = dict(os.environ)
    env.update({
        "HOME": os.path.join(root, "home"),
        "GG_DIR": os.environ.get("GG_DIR", os.path.join(root, "gg")),
        "TERM": "xterm-256color",
        "LANG": env.get("LANG", "en_US.UTF-8"),
    })
    try:
        with open(args[0], "rb") as fh:
            if fh.read(2) == b"MZ" and os.name != "nt":
                args = ["/bin/sh"] + args   # APE without a binfmt handler
    except OSError:
        pass
    proc = subprocess.Popen(args, stdin=slave, stdout=slave, stderr=slave,
                            env=env, preexec_fn=os.setsid)
    os.close(slave)
    screen = Screen(cols, rows)

    def pump(seconds):
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([master], [], [], 0.05)
            if r:
                try:
                    data = os.read(master, 65536)
                except OSError:
                    return
                if not data:
                    return
                screen.feed(data)

    pump(1.0)
    for item in [k for k in keys.split(",") if k]:
        if item.startswith("delay:"):
            pump(float(item[6:]))
            continue
        if item.startswith("text:"):
            os.write(master, item[5:].encode())
        else:
            os.write(master, KEYS[item].encode())
        pump(0.45)
    pump(0.4)
    print(screen.dump())
    proc.terminate()
    return 0


if __name__ == "__main__":
    sys.exit(main())
