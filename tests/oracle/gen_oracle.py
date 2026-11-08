#!/usr/bin/env python3
"""Oracle generator for the C++ art decoder.

Builds synthetic <char>.foit / .art / .art-pt / palette .dds files in the format that tools/reference/sg_extract.py
reads, then runs sg_extract's OWN functions (find_records, place, rgba_tables, to_rgba, read_dds) over them and
writes the results next to the inputs. tests/test_main.cpp loads the same inputs with the C++ code and compares
byte for byte.

This proves the C++ port matches the reference implementation. It does not prove the reference matches the real
game files -- no real .art/.foit is available in this repo.

usage: python3 -I gen_oracle.py <out_dir>
"""
import importlib.util
import os
import random
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REF = os.path.join(HERE, "..", "..", "tools", "reference", "sg_extract.py")
spec = importlib.util.spec_from_file_location("sg_extract", REF)
sg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sg)


# ------------------------------------------------------------------ RLE encoder (inverse of sg.rle_decode)
def rle_encode(data, rng):
    """Uses every opcode: literals 00-7F, short repeat 80-FC (<=125), FD (<=255), FE (<=65535), FF (u24)."""
    out = bytearray()
    i, n = 0, len(data)
    lit = bytearray()

    def flush():
        nonlocal lit
        while lit:
            chunk, lit = lit[:128], lit[128:]
            out.append(len(chunk) - 1)
            out.extend(chunk)

    while i < n:
        v = data[i]
        j = i
        while j < n and data[j] == v:
            j += 1
        run = j - i
        if run < 3:
            lit.extend(data[i:j])
            i = j
            continue
        flush()
        while run > 0:
            choices = []
            if run <= 125:
                choices.append("short")
            if run <= 255:
                choices.append("fd")
            if run <= 65535:
                choices.append("fe")
            choices.append("ff")
            kind = rng.choice(choices)
            if kind == "short":
                take = run
                out += bytes([0x80 | (take - 1), v])
            elif kind == "fd":
                take = run
                out += bytes([0xFD, take, v])
            elif kind == "fe":
                take = min(run, 65535)
                out += bytes([0xFE, take >> 8, take & 255, v])
            else:
                take = min(run, 0xFFFFFF)
                out += bytes([0xFF, take >> 16, (take >> 8) & 255, take & 255, v])
            run -= take
        i = j
    flush()
    return bytes(out)


def structured_plane(P, rng, maxval):
    """Bytes with long runs and noise so every RLE opcode gets used."""
    out = bytearray()
    while len(out) < P:
        mode = rng.random()
        if mode < 0.45:
            out += bytes([rng.randrange(maxval + 1)]) * rng.choice([1, 2, 5, 20, 100, 130, 300, 700])
        elif mode < 0.8:
            out += bytes(rng.randrange(maxval + 1) for _ in range(rng.randrange(1, 40)))
        else:
            out += bytes([rng.randrange(maxval + 1)]) * rng.randrange(1, 9)
    return bytes(out[:P])


# ------------------------------------------------------------------ container builders
class Pack:
    def __init__(self):
        self.art = bytearray()
        self.pt = bytearray()
        self.foit = bytearray(rng_noise(10, random.Random(99)))  # first 10 bytes are never a record start

    def add_record(self, rng, src, tag, w, h, mask_cells, band_max, corrupt=None):
        cells = w * h
        n = len(mask_cells)
        P = n * 256
        planes = [rle_encode(structured_plane(P, rng, 255), rng),
                  rle_encode(structured_plane(P, rng, 255), rng),
                  rle_encode(structured_plane(P, rng, band_max), rng)]
        if corrupt == "overflow":      # grad plane decodes to more than P bytes -> must be rejected
            planes[1] = rle_encode(bytes([7]) * (P + 5), rng)
        elif corrupt == "short":       # band plane decodes to fewer bytes -> must be rejected
            planes[2] = rle_encode(bytes([3]) * (P - 1), rng)
        blob = self.art if src == "art" else self.pt
        off = len(blob)
        for p in planes:
            blob += p
        a, b, c = (len(p) for p in planes)
        if corrupt == "beyond":        # sizes point past the end of the data file -> rejected
            c += 100000
        mask = bytearray((cells + 7) // 8)
        for j in mask_cells:
            mask[j >> 3] |= 1 << (j & 7)
        if corrupt == "popcount":      # mask disagrees with n -> rejected
            mask[0] ^= 0x01 if mask[0] & 1 == 0 else 0x01
            if sum(bin(x).count("1") for x in mask) == n:
                mask[-1] ^= 0x80
        if corrupt == "tag":
            tag = 0x3B
        rng_ab = random.Random(off * 31 + n)
        self.foit += struct.pack("<II", rng_ab.randrange(1 << 32), rng_ab.randrange(1 << 32))
        self.foit += bytes([5 if src == "art" else 4, 0, 0x41])
        self.foit += struct.pack("<HHHH", cells, w, h, n) + bytes([tag]) + bytes(mask)
        self.foit += struct.pack("<IIII", off, a, b, c)

    def add_noise(self, rng, n):
        self.foit += rng_noise(n, rng, with_41=True)


def rng_noise(n, rng, with_41=False):
    b = bytearray(rng.randrange(256) for _ in range(n))
    if with_41:
        for k in range(0, n, 7):
            b[k] = 0x41
    return bytes(b)


def write_dds(path, rows):
    h, w = len(rows), len(rows[0])
    hdr = bytearray(128)
    hdr[0:4] = b"DDS "
    struct.pack_into("<IIIII", hdr, 4, 124, 0x100F, h, w, w * 4)
    struct.pack_into("<I", hdr, 76, 32)          # pixel format size
    struct.pack_into("<I", hdr, 80, 0x41)        # RGB | ALPHAPIXELS, no fourcc
    struct.pack_into("<IIII", hdr, 88, 32, 0x00FF0000, 0x0000FF00, 0x000000FF)
    struct.pack_into("<I", hdr, 104, 0xFF000000)
    body = bytearray()
    for row in rows:
        for (r, g, b) in row:
            body += bytes([b, g, r, 255])
    open(path, "wb").write(bytes(hdr) + bytes(body))


def build_case(outdir, seed, bands, band_max, n_art, n_pt, bad):
    rng = random.Random(seed)
    os.makedirs(outdir, exist_ok=True)
    pk = Pack()
    kinds = ["art"] * n_art + ["art-pt"] * n_pt
    rng.shuffle(kinds)
    bad_kinds = ["overflow", "short", "beyond", "popcount", "tag"][:bad]
    items = [(k, None) for k in kinds] + [(rng.choice(["art", "art-pt"]), bk) for bk in bad_kinds]
    rng.shuffle(items)
    for src, corrupt in items:
        w, h = rng.randrange(1, 7), rng.randrange(1, 7)
        cells = w * h
        n = rng.randrange(1, cells + 1)
        mask_cells = sorted(rng.sample(range(cells), n))
        tag = rng.choice([0x3A, 0x3A, 0xBA])
        pk.add_record(rng, src, tag, w, h, mask_cells, band_max, corrupt)
        pk.add_noise(rng, rng.randrange(0, 25))
    open(os.path.join(outdir, "chr.foit"), "wb").write(bytes(pk.foit))
    open(os.path.join(outdir, "chr.art"), "wb").write(bytes(pk.art))
    open(os.path.join(outdir, "chr.art-pt"), "wb").write(bytes(pk.pt))

    pal_rows = [[tuple(rng.randrange(256) for _ in range(3)) for _ in range(16)] for _ in range(bands)]
    write_dds(os.path.join(outdir, "pal.dds"), pal_rows)

    foit, art, artpt = bytes(pk.foit), bytes(pk.art), (bytes(pk.pt) or None)
    recs = sg.find_records(foit, art, artpt)
    pal = sg.read_dds(open(os.path.join(outdir, "pal.dds"), "rb").read())
    T4 = sg.rgba_tables(pal)
    with open(os.path.join(outdir, "meta.txt"), "w") as m:
        m.write("palette %d %d\n" % (len(pal[0]), len(pal)))
        m.write("records %d\n" % len(recs))
        cov_a = sum(r["size"] for r in recs if r["src"] == "art") / max(len(art), 1)
        cov_p = sum(r["size"] for r in recs if r["src"] == "art-pt") / max(len(artpt or b"x"), 1)
        m.write("coverage %.9f %.9f\n" % (cov_a, cov_p))
        for i, r in enumerate(recs):
            m.write("rec %d %s %d %d %d %d %d %d %d %d %d\n" % (
                i, r["src"], r["tag"], r["w"], r["h"], r["n"], r["A"], r["B"], r["foit_off"], r["off"], r["size"]))
            Bc, Gc, W, H = sg.place(r)
            open(os.path.join(outdir, "frame_%d.band" % i), "wb").write(bytes(Bc))
            open(os.path.join(outdir, "frame_%d.grad" % i), "wb").write(bytes(Gc))
            open(os.path.join(outdir, "frame_%d.rgba" % i), "wb").write(bytes(sg.to_rgba(Bc, Gc, W, H, T4)))
    return len(recs)


def main():
    out = sys.argv[1]
    cases = [  # seed, palette bands, max band value in data, art frames, art-pt frames, corrupted records
        (1, 32, 31, 12, 4, 5),
        (2, 32, 31, 30, 0, 0),
        (3, 8, 31, 10, 6, 3),    # data band values exceed palette rows -> clamping path
        (4, 32, 255, 8, 8, 5),   # every band index incl. > palette bands (replicated last band)
        (5, 1, 3, 6, 2, 2),      # single-row palette
        (6, 32, 31, 40, 20, 5),
    ]
    total = 0
    for seed, bands, bmax, na, npt, bad in cases:
        total += build_case(os.path.join(out, "case_%d" % seed), seed, bands, bmax, na, npt, bad)
    fb = sg.fallback_pal()  # preview palette used when no .dds is found
    open(os.path.join(out, "fallback.bin"), "wb").write(bytes(c for row in fb for rgb in row for c in rgb))
    open(os.path.join(out, "cases.txt"), "w").write("\n".join("case_%d" % c[0] for c in cases) + "\n")
    print("generated %d cases, %d reference-accepted records" % (len(cases), total))


if __name__ == "__main__":
    main()
