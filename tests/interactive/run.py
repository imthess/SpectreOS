#!/usr/bin/env python3
"""
End-to-end test: boots the real kernel in QEMU and drives it through
the PS/2 keyboard (QEMU monitor `sendkey`), reading the serial log.

Covers, on a FRESH disk image:
  boot 1: signup validation (short password, mismatched confirm),
          entropy collection (no RDRAND under default QEMU CPU),
          account + identity creation, shell commands, file write/read,
          real network traffic (ARP + ICMP echo through the RTL8139),
          disk-encryption self-test, in-kernel `selftest` suite
  boot 2: same disk, new boot -> wrong user / wrong password rejected,
          correct login accepted, data persisted, no signup prompt

Needs: qemu-system-i386, python3, built iso (make iso). No display.
"""
import os, random, shutil, socket, subprocess, sys, tempfile, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ISO = os.path.join(ROOT, "iso", "spectreos.iso")
LOGIN_TIMEOUT = 240          # PBKDF2 under TCG emulation is slow
rng = random.Random(1234)

SPECIAL = {" ": "spc", "-": "minus", ".": "dot", "_": "shift-minus",
           "\n": "ret"}

def key_name(ch):
    if ch in SPECIAL: return SPECIAL[ch]
    if ch.isdigit() or ch.islower(): return ch
    if ch.isupper(): return "shift-" + ch.lower()
    raise ValueError("no key mapping for %r" % ch)

class Machine:
    def __init__(self, disk, tag):
        self.dir = tempfile.mkdtemp(prefix="spectre-")
        self.log_path = os.path.join(self.dir, "serial.log")
        self.sock_path = os.path.join(self.dir, "mon.sock")
        open(self.log_path, "w").close()
        self.proc = subprocess.Popen(
            ["qemu-system-i386", "-cdrom", ISO,
             "-drive", "file=%s,format=raw,if=ide,index=0,media=disk" % disk,
             "-net", "nic,model=rtl8139", "-net", "user",
             "-display", "none",
             "-serial", "file:" + self.log_path,
             "-monitor", "unix:%s,server,nowait" % self.sock_path],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(100):
            if os.path.exists(self.sock_path): break
            time.sleep(0.1)
        self.mon = socket.socket(socket.AF_UNIX)
        self.mon.connect(self.sock_path)
        self.mon.settimeout(2)
        try: self.mon.recv(4096)
        except Exception: pass
        self.tag = tag
        self.cursor = 0   # everything before this was already matched

    def text(self):
        with open(self.log_path, "rb") as f:
            return f.read().decode("latin-1").replace("\r", "")

    def wait_for(self, needle, timeout=30):
        """Wait until `needle` appears AFTER the last matched position."""
        start = time.time()
        while time.time() - start < timeout:
            t = self.text()
            i = t.find(needle, self.cursor)
            if i >= 0:
                self.cursor = i + len(needle)
                return time.time() - start
            if self.proc.poll() is not None:
                break
            time.sleep(0.1)
        fail("[%s] timed out waiting for %r\n--- serial tail ---\n%s"
             % (self.tag, needle, self.text()[-800:]))

    def absent(self, needle):
        return needle not in self.text()[self.cursor:]

    def press(self, name):
        self.mon.sendall(("sendkey %s\n" % name).encode())
        try: self.mon.recv(4096)
        except Exception: pass

    def type(self, s, jitter=False):
        for ch in s:
            self.press(key_name(ch))
            # human-ish, irregular spacing (also feeds the entropy pool)
            time.sleep(rng.uniform(0.06, 0.22) if jitter else 0.09)

    def line(self, s):
        self.type(s); self.press("ret"); time.sleep(0.15)

    def stop(self):
        self.proc.kill(); self.proc.wait()
        shutil.rmtree(self.dir, ignore_errors=True)

failures = []
def fail(msg):
    print("FAIL: " + msg); sys.stdout.flush()
    cleanup(); sys.exit(1)

machines = []
def cleanup():
    for m in machines:
        try: m.stop()
        except Exception: pass

def ok(msg): print("  PASS  " + msg); sys.stdout.flush()

def expect(m, needle, what, timeout=30):
    dt = m.wait_for(needle, timeout)
    ok("%s (%.1fs)" % (what, dt))

def main():
    if not shutil.which("qemu-system-i386"):
        print("SKIP: qemu-system-i386 not installed"); return 0
    if not os.path.exists(ISO):
        fail("missing %s - run `make iso` first" % ISO)

    disk = tempfile.mktemp(prefix="spectre-disk-", suffix=".img")
    with open(disk, "wb") as f: f.truncate(64 * 1024 * 1024)

    print("== boot 1: fresh disk, account creation ==")
    m = Machine(disk, "boot1"); machines.append(m)
    expect(m, "Choose a username:", "first-boot account prompt", 60)
    m.line("alice")
    expect(m, "Choose a password:", "password prompt")
    m.line("short")
    expect(m, "at least 8", "7-or-fewer character password rejected")
    m.line("correct-horse")
    expect(m, "Confirm password:", "confirm prompt")
    m.line("different-one")
    expect(m, "did not match", "mismatched confirmation rejected")
    expect(m, "Choose a username:", "signup restarts after mismatch")
    m.line("alice")
    expect(m, "Choose a password:", "password prompt again")
    m.line("correct-horse")
    expect(m, "Confirm password:", "confirm prompt again")
    m.line("correct-horse")

    # No RDRAND under QEMU's default CPU model -> must ask for entropy.
    expect(m, "Entropy:", "entropy collection requested (no RDRAND)", 30)
    typed = 0
    while "Entropy: 100%" not in m.text()[m.cursor:] and typed < 600:
        m.type(rng.choice("abcdefghijklmnopqrstuvwxyz"), jitter=True)
        time.sleep(0.05)          # let the serial log catch up before polling
        typed += 1
    ok("entropy gathered after %d random keys" % typed)
    expect(m, "Account created.", "account created", LOGIN_TIMEOUT)
    expect(m, "Identity fingerprint:", "identity generated only after seeding")
    expect(m, "Welcome, alice.", "session started")
    expect(m, "spectre>", "shell prompt", 30)
    for _ in range(8):            # clear any key still in flight
        m.press("backspace"); time.sleep(0.05)

    m.line("whoami")
    expect(m, "alice", "whoami reports the user")
    m.line("write note.txt hello-spectre")
    expect(m, "Write successful.", "file written")
    m.line("cat note.txt")
    expect(m, "hello-spectre", "file read back")
    m.line("ls")
    expect(m, "note.txt", "file listed")

    m.line("netinfo")
    expect(m, "10.0.2.15", "NIC configured (MAC/IP/gateway shown)")
    m.line("ping 10.0.2.2")
    expect(m, "reply received", "ARP + ICMP echo round-trip to the gateway via RTL8139", 40)

    m.line("crypttest")
    expect(m, "Result: PASS", "disk-encryption self-test passes", 60)

    m.line("selftest")
    expect(m, "selftest:", "in-kernel regression suite ran", 120)
    t = m.text()[m.cursor:]
    m.wait_for("failed", 5)
    report = m.text()
    summary = report[report.rindex("selftest:"):].split("\n")[0]
    bad = [l for l in report.split("\n") if l.startswith("  FAIL")]
    if bad or " 0 failed" not in summary:
        fail("kernel selftest reported failures: %s\n%s" % (summary, "\n".join(bad)))
    ok("kernel selftest: " + summary.replace("selftest: ", ""))
    m.stop(); machines.remove(m)

    print("== boot 2: same disk, login ==")
    m = Machine(disk, "boot2"); machines.append(m)
    expect(m, "SpectreOS login", "login (not signup) on second boot", 60)
    if "No account exists" in m.text():
        fail("second boot offered signup - account was not persisted")
    expect(m, "Username:", "username prompt")
    m.line("mallory")
    expect(m, "Password:", "password prompt")
    m.line("correct-horse")
    expect(m, "Access denied.", "unknown user rejected", LOGIN_TIMEOUT)
    expect(m, "Username:", "back at username prompt")
    m.line("alice")
    expect(m, "Password:", "password prompt")
    m.line("wrong-password")
    expect(m, "Access denied.", "wrong password rejected", LOGIN_TIMEOUT)
    expect(m, "Username:", "back at username prompt")
    m.line("alice")
    expect(m, "Password:", "password prompt")
    m.line("correct-horse")
    expect(m, "Welcome back, alice.", "correct password accepted", LOGIN_TIMEOUT)
    expect(m, "spectre>", "shell prompt", 30)
    m.line("cat note.txt")
    expect(m, "hello-spectre", "data persisted across reboot")
    m.stop(); machines.remove(m)
    os.unlink(disk)
    print("\ninteractive tests: all passed")
    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    finally:
        cleanup()
