"""Group LoadPhaseSample stacks by stream gap (see stream_force.md).

Usage: python lp_samples.py [kotor2_log.txt] [--top N] [--id ID] [--funcs FILE]

--funcs takes Ghidra's function list ("NAME at ADDR" per line, e.g. from the
GhidraMCP bridge: curl http://127.0.0.1:8080/list_functions > functions.txt).
With it, addresses are labelled with their containing function and each window
also gets a per-function inclusive ranking.

For each load in the latest run, splits the main-thread samples into windows:
  handshake   finalize enter .. server running (state12_exit)
  gap1        server running .. first stream message
  gap2        first .. second stream message
  rest        second message .. area-loaded ack
and prints, per window, the return addresses present in the most samples
(inclusive: each address counts once per sample).  Resolve them in Ghidra.
"""
import bisect
import re
import sys
from collections import Counter

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
        windows = [("handshake", fin, run), ("gap1", run, m0), ("gap2", m0, m1), ("rest", m1, ack)]
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


if __name__ == "__main__":
    main()
