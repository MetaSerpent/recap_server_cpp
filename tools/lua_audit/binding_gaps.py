"""Find engine members used by transpiled Darkspore scripts that are neither
bound by the server (C++) nor defined by the scripts themselves.

Works on transpiler output (registers R<depth>_<n>, linear statements)."""
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

S = Path(sys.argv[1])
SRC = Path(sys.argv[2])
DIRS = ("lua", "Abilities", "Modifiers", "behaviors")

assign_re = re.compile(r"^\s*(R\d+_\d+) = (.+)$")
field_set_re = re.compile(r"^\s*(R\d+_\d+)\.([A-Za-z_]\w*) = ")
global_set_re = re.compile(r"^\s*(n[A-Z]\w*) = (R\d+_\d+)\s*$")
field_get_re = re.compile(r"^(R\d+_\d+)\.([A-Za-z_]\w*)$")

used = defaultdict(Counter)
defined = defaultdict(set)
for d in DIRS:
    for f in (S / d).glob("*.lua"):
        reg_ns = {}        # register -> namespace global it currently holds
        reg_fields = {}    # register -> fields assigned while it holds a fresh table
        reg_str = {}       # register -> string constant it currently holds
        for line in f.read_text("latin-1").splitlines():
            b = re.match(r'^\s*(R\d+_\d+)\[(R\d+_\d+)\] = ', line)   # t[k] = v with constant key
            if b and b.group(2) in reg_str:
                reg, key = b.group(1), reg_str[b.group(2)]
                if reg in reg_ns: defined[reg_ns[reg]].add(key)
                if reg in reg_fields: reg_fields[reg].add(key)
                continue
            sc = re.match(r'^\s*(R\d+_\d+) = "([A-Za-z_]\w*)"\s*$', line)
            if sc:
                reg_str[sc.group(1)] = sc.group(2)
            m = field_set_re.match(line)
            if m:
                reg, fld = m.groups()
                if reg in reg_ns:
                    defined[reg_ns[reg]].add(fld)
                if reg in reg_fields:
                    reg_fields[reg].add(fld)
                continue
            m = global_set_re.match(line)
            if m:
                ns, reg = m.groups()
                defined[ns] |= reg_fields.get(reg, set())
                continue
            m = assign_re.match(line)
            if m:
                reg, rhs = m.groups()
                rhs = rhs.strip()
                g = field_get_re.match(rhs)
                if g and g.group(1) in reg_ns:
                    used[reg_ns[g.group(1)]][g.group(2)] += 1
                reg_ns.pop(reg, None)
                reg_fields.pop(reg, None)
                if re.fullmatch(r"n[A-Z]\w*", rhs):
                    reg_ns[reg] = rhs
                elif rhs == "{}":
                    reg_fields[reg] = set()

src = "".join(p.read_text("utf-8", errors="replace") for p in SRC.glob("game/*.cpp"))
bound = set(re.findall(r'\["([A-Za-z_]\w*)"\]\s*=', src)) | set(re.findall(r'set_function\("([A-Za-z_]\w*)"', src))
bound |= set(re.findall(r'\{\s*"([A-Za-z_]\w*)"\s*,', src))  # enum initializer lists

missing = sorted(((n, ns, mem) for ns, ms in used.items() for mem, n in ms.items()
                  if mem not in bound and mem not in defined[ns]), reverse=True)
print(f"namespaces used: {len(used)}; members used: {sum(len(v) for v in used.values())}; "
      f"missing: {len(missing)} ({sum(n for n, _, _ in missing)} call sites)")
for n, ns, mem in missing[: int(sys.argv[3]) if len(sys.argv) > 3 else 80]:
    print(f"{n:5d} {ns}.{mem}")

if len(sys.argv) > 4 and sys.argv[4] == "nearmiss":
    lower = {}
    for b in bound: lower.setdefault(b.lower(), set()).add(b)
    print("\ncase-insensitive near misses (script name -> server name):")
    for n, ns, mem in missing:
        hits = lower.get(mem.lower())
        if hits: print(f"{n:5d} {ns}.{mem} -> {sorted(hits)}")
