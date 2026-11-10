"""Compare how many arguments Darkspore scripts pass to each engine function with
how many the server's sol2 binding requires (non-optional, non-variadic).
usage: arity_audit.py <serverdata> <luafunctions.cpp>"""
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

S, SRC = Path(sys.argv[1]), Path(sys.argv[2])
src = SRC.read_text("utf-8", errors="replace").replace("\r\n", "\n")


def split_params(text):
    out, depth, cur = [], 0, ""
    for ch in text:
        if ch in "<(":
            depth += 1
        elif ch in ">)":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip()); cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def classify(params):
    """-> (required_count, types) ; types exclude this_state"""
    types = []
    for p in params:
        if p.startswith("sol::this_state"):
            continue
        types.append(p)
    required = 0
    for i, p in enumerate(types):
        if p.startswith("sol::variadic_args"):
            break
        if not p.startswith("sol::optional"):
            required = i + 1
    return required, types


def matching_paren(text, start):
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return i
    return -1


# free functions usable as bindings: "auto Name(" / "type Name(" at namespace scope
free_fns = defaultdict(list)
for m in re.finditer(r"^\t{1,2}(?:auto|[\w:<>]+(?:\s*[&*])?)\s+(\w+)\((sol::this_state[^\n]*|\s*$)", src, re.M):
    start = src.index("(", m.start(1))
    end = matching_paren(src, start)
    free_fns[m.group(1)].append(classify(split_params(src[start + 1:end].replace("\n", " "))))

bindings = {}          # (ns, member) -> list of (required, types)
var_ns = {}
decl_re = re.compile(r'auto (\w+) = mState(?:\.create_named_table\("(\w+)"\)|\["(\w+)"\])')
bind_re = re.compile(r'^\t+(\w+)\["(\w+)"\] = (.*)$')
pos = 0
lines = src.split("\n")
offsets = []
o = 0
for l in lines:
    offsets.append(o); o += len(l) + 1
for idx, line in enumerate(lines):
    d = decl_re.search(line)
    if d:
        var_ns[d.group(1)] = d.group(2) or d.group(3)
    b = bind_re.match(line)
    if not b or b.group(1) not in var_ns:
        m2 = re.match(r'^\t+mState\["(\w+)"\]\["(\w+)"\] = (.*)$', line)
        if not m2:
            continue
        ns, member, rhs = m2.group(1), m2.group(2), m2.group(3)
    else:
        ns, member, rhs = var_ns[b.group(1)], b.group(2), b.group(3)
    sigs = []
    lam = re.match(r"\[[^\]]*\]\s*\(", rhs)
    if lam:
        start = offsets[idx] + line.index(rhs) + rhs.index("(", lam.start())
        end = matching_paren(src, start)
        sigs.append(classify(split_params(src[start + 1:end].replace("\n", " "))))
    else:
        for name in re.findall(r"&(?:[\w:]+::)?(\w+)", rhs if "overload" not in rhs else " ".join(lines[idx:idx + 4])):
            sigs.extend(free_fns.get(name, []))
    if sigs:
        bindings[(ns, member)] = sigs

# script calls
calls = defaultdict(Counter)   # (ns, member) -> Counter(argc)
examples = {}
for d in ("lua", "Abilities", "Modifiers", "behaviors"):
    for f in (S / d).glob("*.lua"):
        ls = f.read_text("latin-1").splitlines()
        reg_ns, reg_fn = {}, {}
        for i, line in enumerate(ls):
            m = re.match(r"\s*(R\d+_\d+) = (n[A-Z]\w*)\s*$", line)
            if m:
                reg_ns[m.group(1)] = m.group(2); reg_fn.pop(m.group(1), None); continue
            m = re.match(r"\s*(R\d+_\d+) = (R\d+_\d+)\.(\w+)\s*$", line)
            if m and m.group(2) in reg_ns:
                reg_fn[m.group(1)] = (reg_ns[m.group(2)], m.group(3))
                if m.group(1) != m.group(2):
                    pass
                else:
                    reg_ns.pop(m.group(1), None)
                continue
            c = re.search(r"(?<![\w.])(R\d+_\d+)\((.*)\)\s*(?:end)?\s*$", line)
            if c and c.group(1) in reg_fn:
                key = reg_fn[c.group(1)]
                args = c.group(2).strip()
                if "__lua51_unpack" in args:
                    fixed = [a for a in split_params(args.split("__lua51_unpack")[0]) if a]
                    argc = (len(fixed), "+")
                else:
                    argc = (len(split_params(args)) if args else 0, "")
                calls[key][argc] += 1
                examples.setdefault((key, argc), f"{f.name}:{i + 1}")
            m = re.match(r"\s*(R\d+_\d+)(?:, R\d+_\d+)* = ", line)
            if m and not re.match(r"\s*(R\d+_\d+) = \1\.", line):
                reg_fn.pop(m.group(1), None)
                if not re.match(r"\s*(R\d+_\d+) = (n[A-Z]\w*)\s*$", line):
                    reg_ns.pop(m.group(1), None)

rows = []
for key, counter in calls.items():
    if key not in bindings:
        continue
    need = min(r for r, _ in bindings[key])          # most lenient overload
    short = {a: n for a, n in counter.items() if a[1] == "" and a[0] < need}
    if short:
        total = sum(short.values())
        mn = min(a[0] for a in short)
        ex = examples[(key, min(short))]
        types = min(bindings[key], key=lambda s: s[0])[1]
        rows.append((total, f"{key[0]}.{key[1]}", need, mn, ex, types))
rows.sort(reverse=True)
print(f"bindings parsed: {len(bindings)}; script-called engine functions: {len(calls)}; "
      f"functions called with too few arguments: {len(rows)} ({sum(r[0] for r in rows)} call sites)")
for total, name, need, mn, ex, types in rows:
    print(f"{total:4d}  {name:46s} needs {need}, called with {mn}  e.g. {ex}\n        params: {', '.join(t.split(' ')[0] + ' ' + t.split(' ')[-1] for t in types)[:150]}")

# ---- type audit: literal argument kinds at call sites vs declared C++ parameter types
def param_kind(t):
    base = t.rsplit(" ", 1)[0] if " " in t else t
    if base.startswith("sol::optional") or base.startswith("sol::variadic_args") or base.startswith("sol::object"):
        return "any"
    if "std::string" in base: return "string"
    if base.startswith("sol::table"): return "table"
    if base.startswith("sol::function") or base.startswith("sol::protected_function"): return "function"
    if base == "bool": return "boolean"
    if re.match(r"(const )?(float|double|u?int\d*_t|int|unsigned|size_t|Game::\w+)", base): return "number"
    return "any"

def literal_kind(rhs):
    rhs = rhs.strip()
    if re.fullmatch(r"-?\d+(\.\d+)?(e[-+]?\d+)?", rhs): return "number"
    if rhs.startswith('"'): return "string"
    if rhs in ("true", "false"): return "boolean"
    if rhs == "nil": return "nil"
    if rhs == "{}": return "table"
    if rhs.startswith("function("): return "function"
    return None

ok_pairs = {("number", "number"), ("string", "string"), ("table", "table"), ("boolean", "boolean"), ("function", "function"),
            ("number", "string")}   # SOL_STRINGS_ARE_NUMBERS: numeric strings convert (not checked here)
mism = defaultdict(Counter); mex = {}
for d in ("lua", "Abilities", "Modifiers", "behaviors"):
    for f in (S / d).glob("*.lua"):
        ls = f.read_text("latin-1").splitlines()
        reg_ns, reg_fn, reg_lit = {}, {}, {}
        for i, line in enumerate(ls):
            if re.match(r"\s*::pc_\d+::", line): reg_lit.clear()
            m = re.match(r"\s*(R\d+_\d+) = (n[A-Z]\w*)\s*$", line)
            if m:
                reg_ns[m.group(1)] = m.group(2); reg_fn.pop(m.group(1), None); reg_lit.pop(m.group(1), None); continue
            m = re.match(r"\s*(R\d+_\d+) = (R\d+_\d+)\.(\w+)\s*$", line)
            if m and m.group(2) in reg_ns:
                reg_fn[m.group(1)] = (reg_ns[m.group(2)], m.group(3)); reg_lit.pop(m.group(1), None)
                if m.group(1) == m.group(2): reg_ns.pop(m.group(1), None)
                continue
            c = re.search(r"(?<![\w.])(R\d+_\d+)\((.*)\)\s*$", line)
            if c and c.group(1) in reg_fn and "__lua51_unpack" not in c.group(2):
                key = reg_fn[c.group(1)]
                if key in bindings and len(bindings[key]) == 1:
                    types = bindings[key][0][1]
                    for pos_i, a in enumerate(split_params(c.group(2))):
                        if pos_i >= len(types): break
                        want = param_kind(types[pos_i]); got = reg_lit.get(a)
                        if want != "any" and got and (want, got) not in ok_pairs:
                            mism[(key, pos_i, types[pos_i])][got] += 1
                            mex.setdefault((key, pos_i), f"{f.name}:{i + 1}")
            m = re.match(r"\s*(R\d+_\d+) = (.+)$", line)
            if m:
                k = literal_kind(m.group(2))
                if k: reg_lit[m.group(1)] = k
                else: reg_lit.pop(m.group(1), None)
                if not re.match(r"\s*(R\d+_\d+) = \1\.", line): reg_fn.pop(m.group(1), None)
print(f"\nliteral type mismatches: {len(mism)} parameters")
for (key, pos_i, t), kinds in sorted(mism.items(), key=lambda kv: -sum(kv[1].values())):
    print(f"{sum(kinds.values()):4d}  {key[0]}.{key[1]:34s} arg {pos_i + 1} declared '{t}' but scripts pass {dict(kinds)}  e.g. {mex[(key, pos_i)]}")
