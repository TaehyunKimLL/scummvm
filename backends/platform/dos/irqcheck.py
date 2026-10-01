#!/usr/bin/env python3
"""Checks the interrupt handler code ranges of a DJGPP build of ScummVM.

    irqcheck.py scummvm.exe            (exit status 1 on a problem)

Under CWSDPMI only the interrupt handlers' own code and data are locked
(backends/platform/dos/dos-irq.h): each source file with handlers puts
them between the labels _dosIrqBegin_<tag> and _dosIrqEnd_<tag>, and that
range is locked at run time. A page fault in that code is fatal when the
interrupt came in real mode, so the code in a range may not reach anything
outside it. The install-time check sees only the entry points; this one
disassembles each range (objdump) and fails on:

- a range that is missing, or empty, or does not hold its handlers;
- a call or jump that leaves the range (libc's memcpy, a libgcc division,
  an out-of-line helper, a .cold part), unless allowed below;
- an indirect call or jump (a switch's jump table, a function pointer),
  unless allowed below;
- a memory operand at an address below etext (.rodata goes with .text in
  a DJGPP link: a constant table, a jump table) or in data the range is not
  known to lock (its symbol not in ALLOWED_DATA).

Immediates below etext are constants (the timer's ms scale, say), not
addresses, and are not checked; an immediate that names code would only
matter as the target of an indirect call, which is.
"""
import bisect
import os
import re
import subprocess
import sys

# tag: (handlers that must lie inside, data symbols it may touch (regexes on
# the mangled names), indirect exits allowed (regexes on objdump's text))
RANGES = {
    # IRQ0: the BIOS chain through the old vector (in g_isr), and the
    # virtual DefaultTimerManager::handler() call, made only after the
    # check that the interrupt came in protected mode (dos-timer.cpp).
    "timer": (["__ZL8timerIsrmm"], [r"g_isr"],
              [r"^lcall \*0x[0-9a-f]+ \(indirect\) -> __ZL5g_isr", r"^call \*0x[0-9a-f]+\(%e[a-z]x\) \(indirect\)$"]),
    "uart": (["__ZN3GUIL7uartIsrEv", "__ZN3GUIL9drainFifoEv"],
             [r"g_ring", r"g_ringHead", r"g_ringTail", r"g_overruns", r"g_isrBase", r"g_irqs"], []),
    "dos": (["__ZL10int1cProbeP11__dpmi_regs", "__ZL10sbIrqCountv"],
            [r"g_int1cCalls", r"g_int1cMasked", r"g_sbIrqs"], []),
    "sb": (["_SoundBlasterIRQHandler"], [r"^_isr_", r"^_soundblaster_irq$"], []),
    "kbd": (["_KeyboardIRQHandler"], [r"^_keyevents_"], []),
}


def tool(name):
    prefix = os.environ.get("DJGPP_PREFIX", os.path.expanduser("~/opt/djgpp"))
    return os.path.join(prefix, "bin", "i586-pc-msdosdjgpp-" + name)


def main(exe):
    syms = []
    out = subprocess.run([tool("nm"), "-n", exe], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3:
            syms.append((int(p[0], 16), p[1], p[2]))
    syms.sort()
    # Section symbols (".text.dosirq_a") share addresses with the labels;
    # name addresses by real symbols only.
    named = [s for s in syms if not s[2].startswith(".")]
    addrs = [a for a, _, _ in named]

    def name(a):
        i = bisect.bisect_right(addrs, a) - 1
        return named[i][2] if i >= 0 else "?"

    ranges = {}
    for a, _, n in syms:
        m = re.match(r"_dosIrq(Begin|End)_(\w+)$", n)
        if m:
            ranges.setdefault(m.group(2), {})[m.group(1)] = a
    etexts = [a for a, _, n in syms if n in ("etext", "_etext")]
    if not etexts:
        print("irqcheck: no etext in %s" % exe)
        return 1
    etext = etexts[0]
    problems = []
    for tag, (handlers, data_ok, exits_ok) in sorted(RANGES.items()):
        r = ranges.get(tag, {})
        if "Begin" not in r or "End" not in r:
            problems.append("%s: no range (labels _dosIrqBegin_%s/_dosIrqEnd_%s missing)" % (tag, tag, tag))
            continue
        b, e = r["Begin"], r["End"]
        if not b < e:
            problems.append("%s: empty range [%#x, %#x]" % (tag, b, e))
            continue
        where = {n: a for a, _, n in syms}
        for h in handlers:
            if h not in where:
                problems.append("%s: handler %s not in the EXE" % (tag, h))
            elif not b < where[h] < e:
                problems.append("%s: handler %s at %#x is outside [%#x, %#x]" % (tag, h, where[h], b, e))
        dis = subprocess.run([tool("objdump"), "-d", "--no-show-raw-insn", "--start-address=%#x" % b,
                              "--stop-address=%#x" % (e + 1), exe], capture_output=True, text=True,
                             check=True).stdout
        exits = []
        for line in dis.splitlines():
            m = re.match(r"\s*([0-9a-f]+):\s+(\S+)\s*(.*)", line)
            if not m:
                continue
            at, op, args = int(m.group(1), 16), m.group(2), m.group(3).split("<")[0].strip()
            if op.startswith(("call", "j", "lcall", "ljmp")):
                if "*" in args:
                    text = "%s %s (indirect)" % (op, args)
                    mem = re.match(r"\*0x([0-9a-f]+)$", args)
                    if mem:
                        text += " -> " + name(int(mem.group(1), 16))
                    exits.append((at, text))
                    continue
                t = re.match(r"(?:0x)?([0-9a-f]+)$", args)
                if t:
                    ta = int(t.group(1), 16)
                    if not b <= ta <= e:
                        problems.append("%s: %#x: %s leaves the range to %s" % (tag, at, op, name(ta)))
                continue
            # Memory operands with an absolute address: "0x5c...(" or a bare
            # "0x5c..." that is not an immediate ($). lea computes, it reads
            # nothing (the timer's ms scale is an lea).
            if op.startswith("lea"):
                continue
            for x in re.findall(r"(?<![$\w])0x([0-9a-f]+)", args):
                v = int(x, 16)
                if v < 0x1000:
                    continue  # a displacement off a register
                if v < etext:
                    problems.append("%s: %#x: reads %#x (%s) below etext: code or .rodata" % (tag, at, v, name(v)))
                    continue
                n = name(v)
                if not any(re.search(p, n) for p in data_ok):
                    problems.append("%s: %#x: touches %s, not known to be locked" % (tag, at, n))
        for at, text in exits:
            if not any(re.search(p, text) for p in exits_ok):
                problems.append("%s: %#x: %s" % (tag, at, text))
        if len(exits) > len(exits_ok):
            problems.append("%s: %d indirect exits, %d allowed" % (tag, len(exits), len(exits_ok)))
        print("irqcheck: %s [%#x, %#x] %d bytes, %d indirect exit(s)" % (tag, b, e, e - b, len(exits)))
    extra = sorted(set(ranges) - set(RANGES))
    for tag in extra:
        problems.append("%s: a range irqcheck.py does not know; add it to RANGES" % tag)
    for p in problems:
        print("irqcheck: FAIL " + p)
    return 1 if problems else 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__.strip())
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
