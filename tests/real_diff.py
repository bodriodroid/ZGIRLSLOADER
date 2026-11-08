#!/usr/bin/env python3
"""Diffs `sgl_probe --records` against tools/reference/sg_extract.py on REAL character files.

usage: python3 -I tests/real_diff.py [--strict] <dir with <name>.foit/.art/.art-pt> <name> <probe-output.txt>
Reads the three files as plain bytes only; nothing from the folder is executed or imported.
"""
import importlib.util, os, sys, zlib

here = os.path.dirname(os.path.abspath(__file__))
strict = "--strict" in sys.argv   # unmodified sg_extract.py (tags 0x3A/0xBA only)
sys.argv = [a for a in sys.argv if a != "--strict"]
src_text = open(os.path.join(here, "..", "tools", "reference", "sg_extract.py"), encoding="utf-8").read()
if not strict:
    # Real data has records tagged 0x7A and 0xFA with the identical layout (they all validate). The reference only
    # accepts 0x3A/0xBA, so for the default comparison its single tag test is widened to "low 6 bits == 0x3A".
    old = "tag in (TAG_COLOR, TAG_SIL)"
    assert old in src_text, "reference script changed: update the patch"
    src_text = src_text.replace(old, "(tag & 0x3F) == TAG_COLOR")
m = type(sys)("sg_extract")
exec(compile(src_text, "sg_extract.py", "exec"), m.__dict__)

d, name, probe = sys.argv[1:4]
def rd(ext):
    p = os.path.join(d, name + ext)
    return open(p, "rb").read() if os.path.exists(p) else b""
foit, art, pt = rd(".foit"), rd(".art"), rd(".art-pt")
recs = m.find_records(foit, art, pt)

ref = []
for r in recs:
    Bc, Gc, W, H = m.place(r)
    ref.append("REC %s %02x %d %d %d %d %d %d %d %d %d crc %08x %08x" % (
        r["src"], r["tag"], r["w"], r["h"], r["n"], r["A"], r["B"], r["foit_off"], r["off"], r["size"],
        # sizeNormal is not kept by the reference; recover it from the record header
        __import__("struct").unpack_from("<I", foit, r["foit_off"] + 10 + (r["w"] * r["h"] + 7) // 8 + 4)[0],
        zlib.crc32(bytes(Bc)) & 0xFFFFFFFF, zlib.crc32(bytes(Gc)) & 0xFFFFFFFF))

got = [l.rstrip("\n") for l in open(probe) if l.startswith("REC ")]
print("reference records: %d | C++ records: %d" % (len(ref), len(got)))
bad = 0
for i, (a, b) in enumerate(zip(ref, got)):
    if a != b:
        bad += 1
        if bad <= 5:
            print("MISMATCH #%d\n  ref: %s\n  c++: %s" % (i, a, b))
if len(ref) != len(got):
    bad += abs(len(ref) - len(got))
print("RESULT:", "IDENTICAL" if bad == 0 else "%d differences" % bad)
sys.exit(1 if bad else 0)
