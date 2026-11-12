"""Minimal S-expression reader/writer for KiCad files."""
import re

_TOK = re.compile(r'\s*(?:(\()|(\))|"((?:[^"\\]|\\.)*)"|([^\s()"]+))', re.S)
_UNESC = re.compile(r'\\(.)')


class Sym(str):
    """Unquoted atom (keyword or number) as opposed to a quoted string."""


def parse(text):
    stack, cur = [], []
    for m in _TOK.finditer(text):
        lp, rp, s, atom = m.groups()
        if lp:
            stack.append(cur)
            cur = []
        elif rp:
            done, cur = cur, stack.pop()
            cur.append(done)
        elif s is not None:
            cur.append(_UNESC.sub(lambda k: "\n" if k.group(1) == "n" else k.group(1), s))
        elif atom is not None:
            cur.append(Sym(atom))
    return cur[0]


def dump(x, indent=0):
    pad = "\t" * indent
    if not isinstance(x, list):
        return pad + _atom(x)
    if all(not isinstance(i, list) for i in x):
        return pad + "(" + " ".join(_atom(i) for i in x) + ")"
    out = pad + "("
    first = True
    for i in x:
        if isinstance(i, list):
            out += "\n" + dump(i, indent + 1)
        else:
            out += ("" if first else " ") + _atom(i)
        first = False
    return out + "\n" + pad + ")"


def _atom(a):
    if isinstance(a, Sym):
        return str(a)
    if isinstance(a, bool):
        return "yes" if a else "no"
    if isinstance(a, (int, float)):
        return fmt(a)
    s = str(a).replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    return '"' + s + '"'


def fmt(v):
    s = ("%.4f" % v).rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def find(node, key):
    return [c for c in node if isinstance(c, list) and c and c[0] == key]


def find1(node, key):
    r = find(node, key)
    return r[0] if r else None
