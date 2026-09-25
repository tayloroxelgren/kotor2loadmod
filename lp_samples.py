"""Group LoadPhaseSample stacks by stream gap (see stream_force.md).

Usage: python lp_samples.py [kotor2_log.txt] [--top N] [--id ID] [--funcs FILE]

--funcs takes Ghidra's function list ("NAME at ADDR" per line, e.g. from the
GhidraMCP bridge: curl http://127.0.0.1:8080/list_functions > functions.txt).
With it, addresses are labelled with their containing function and each window
also gets a per-function inclusive ranking.

For each load in the latest run, splits the main-thread samples into windows:
  work        window armed (click) .. finalize enter
  handshake   finalize enter .. server running (state12_exit)
  gap1        server running .. first stream message
  gap2        first .. second stream message
  rest        second message .. area-loaded ack
and prints, per window, the return addresses present in the most samples
(inclusive: each address counts once per sample).  Resolve them in Ghidra.
--summary prints only a per-window category table in ms (samples x the mean
sample interval), averaged over the run's loads -- the A/B view.
"""
import bisect
import re
import sys
from collections import Counter, defaultdict

# Categories by a return address on the stack, first match wins.  Addresses
# are return sites in swkotor2.exe (Steam build), resolved in Ghidra 2026-09-25.
CATEGORIES = [
    ("cpu_mipmaps", lambda st: 0x4340BC in st),       # Texture_UploadToGL -> gluBuild2DMipmaps
    ("texture_upload", lambda st: 0x4267DB in st),    # FUN_004265e0 -> Texture_UploadToGL
    ("texture_queue", lambda st: 0x42785E in st),     # TexturePoolCleanupAndRefresh, other
    ("present_wait", lambda st: st[:1] == [0x409FCF]),  # LoadingScreenUpdateFrame SwapBuffers
    ("other", lambda st: True),
]
WINDOWS = ["work", "handshake", "gap1", "gap2", "rest"]


def category(st):
    for name, test in CATEGORIES:
        if test(st):
            return name
    return "other"


DEFAULT_LOG = r"D:\SteamLibrary\steamapps\common\Knights of the Old Republic II\kotor2_log.txt"


def kv(line):
    return dict(re.findall(r"(\w+)=(\S+)", line))


def load_funcs(path):
    funcs = []
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"(\S+) at ([0-9a-fA-F]+)", line.strip())
        if m:
            funcs.append((int(m.group(2), 16), m.group(1)))
    funcs.sort()
    return [a for a, _ in funcs], [n for _, n in funcs]


def main():
    args = sys.argv[1:]
    top = 20
    only_id = None
    if "--top" in args:
        i = args.index("--top")
        top = int(args[i + 1])
        del args[i:i + 2]
    if "--id" in args:
        i = args.index("--id")
        only_id = args[i + 1]
        del args[i:i + 2]
    starts, names = [], []
    summary = "--summary" in args
    if summary:
        args.remove("--summary")
    if "--funcs" in args:
        i = args.index("--funcs")
        starts, names = load_funcs(args[i + 1])
        del args[i:i + 2]

    def fn(addr):
        if not starts:
            return ""
        k = bisect.bisect_right(starts, addr) - 1
        return names[k] if k >= 0 else "?"

    path = args[0] if args else DEFAULT_LOG
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    start = max((i for i, l in enumerate(lines) if "ProfilerRunStart:" in l), default=0)
    lines = lines[start:]

    loads = {}
    for l in lines:
        if " - LoadPhaseEvent: " in l:
            d = kv(l)
            ev = loads.setdefault(d["id"], {"events": {}, "msgs": [], "samples": []})["events"]
            ev.setdefault(d["kind"], int(d["at_us"]))
        elif " - LoadPhaseMsg: " in l:
            d = kv(l)
            loads.setdefault(d["id"], {"events": {}, "msgs": [], "samples": []})["msgs"].append(
                (int(d["at_us"]), int(d["phase"])))
        elif " - LoadPhaseSample: " in l:
            d = kv(l)
            st = [int(x, 16) for x in d.get("st", "").split(",") if x]
            loads.setdefault(d["id"], {"events": {}, "msgs": [], "samples": []})["samples"].append(
                (int(d["at_us"]), int(d["eip"], 16), d.get("m", "?"), st))

    table = defaultdict(float)  # (window, category) -> ms summed over loads
    wlen = defaultdict(float)   # window -> ms summed over loads
    nloads = 0
    for lid, ld in loads.items():
        if only_id and lid != only_id:
            continue
        if not ld["samples"]:
            continue
        ev = ld["events"]
        stream = [t for t, ph in ld["msgs"] if ph == 2]
        fin = ev.get("finalize_enter", 0)
        run = ev.get("state12_exit", fin)
        m0 = stream[0] if stream else run
        m1 = stream[1] if len(stream) > 1 else m0
        ack = ev.get("area_loaded", 10 ** 12)
        windows = [("work", -10 ** 12, fin), ("handshake", fin, run), ("gap1", run, m0),
                   ("gap2", m0, m1), ("rest", m1, ack)]
        ts = [s[0] for s in ld["samples"]]
        interval = (ts[-1] - ts[0]) / max(len(ts) - 1, 1)  # us per sample
        nloads += 1
        for name, a, b in windows:
            lo = max(a, ts[0])
            wlen[name] += max(min(b, ack) - lo, 0) / 1000
            for s in ld["samples"]:
                if a <= s[0] < b:
                    table[(name, category(s[3]))] += interval / 1000
        if summary:
            continue
        mc = (ev.get("modchunk_enter"), ev.get("modchunk_exit"))
        print(f"=== load id={lid} samples={len(ld['samples'])} finalize={fin} running={run} "
              f"msg0={m0} msg1={m1} ack={ack} modchunk={mc[0]}..{mc[1]}")
        for name, a, b in windows:
            sm = [s for s in ld["samples"] if a <= s[0] < b]
            if not sm:
                print(f"--- {name} [{a}..{b}] {(b - a) / 1000:.0f} ms: no samples")
                continue
            incl = Counter()
            leaf = Counter()
            fincl = Counter()
            methods = Counter(s[2] for s in sm)
            for _, eip, _, st in sm:
                for addr in set(st):
                    incl[addr] += 1
                for f in {fn(a) for a in st}:
                    fincl[f] += 1
                leaf[st[0] if st else eip] += 1
            print(f"--- {name} [{a}..{b}] {(b - a) / 1000:.0f} ms, {len(sm)} samples, "
                  f"stack methods {dict(methods)}")
            print("    inclusive (addr: samples %):")
            for addr, n in incl.most_common(top):
                print(f"      {addr:08x}: {n:4d} {100 * n / len(sm):5.1f}%  {fn(addr)}")
            print("    innermost game frame:")
            for addr, n in leaf.most_common(8):
                print(f"      {addr:08x}: {n:4d} {100 * n / len(sm):5.1f}%  {fn(addr)}")
            if starts:
                print("    inclusive by function:")
                for f, n in fincl.most_common(top):
                    print(f"      {n:4d} {100 * n / len(sm):5.1f}%  {f}")

    if nloads:
        cats = [c for c, _ in CATEGORIES]
        print(f"=== mean ms per load over {nloads} load(s)")
        print(f"{'window':<10}{'length':>8}" + "".join(f"{c:>16}" for c in cats))
        for w in WINDOWS:
            print(f"{w:<10}{wlen[w] / nloads:8.0f}" +
                  "".join(f"{table[(w, c)] / nloads:16.0f}" for c in cats))


if __name__ == "__main__":
    main()
