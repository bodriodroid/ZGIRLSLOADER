#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
sg_extract.py - Extractor de sprites de Skullgirls 2nd Encore
               (.art + .art-pt + .foit  ->  PNG 32 bits sin perdida / PCX para Mugen)

Uso en Termux:
  python /storage/emulated/0/sg_dump/sg_extract.py filia
  python /storage/emulated/0/sg_dump/sg_extract.py all
  python /storage/emulated/0/sg_dump/sg_extract.py filia --todas          (PNG de todas las paletas)
  python /storage/emulated/0/sg_dump/sg_extract.py filia --formato ambos  (PNG + PCX)
  python /storage/emulated/0/sg_dump/sg_extract.py filia --pal ruta/filia_1p.dds

Busca dentro de sg_dump/ (incluido unpacked/):
  <personaje>.foit, <personaje>.art, <personaje>.art-pt
Paletas: sg_dump/unpacked/sprites/sprites/<nombre>_<N>p.dds  (ej. boss_marie_1p.dds, boss_marie_2p.dds)
  Los guiones bajos no importan: boss_marie_1p.dds sirve para 'bossmarie'. Otra carpeta: --pals ruta

Crea sg_dump/extract/<personaje>/:
  1p/sprites/   frames del .art     PNG RGBA 32 bits, SIN perdida (bordes suaves, todos los colores)
  1p/extras/    frames del .art-pt
  1p/siluetas/  capas TAG 0xBA (ej. espiritu del Skull Heart)
  2p/, 3p/ ...  solo con --todas
  pcx/          solo con --formato pcx|ambos: PCX 8 bits (indice 0 transparente) + .act por paleta
  frames.csv    tamano, bloques y A/B (pivote probable) de cada frame

Solo usa la biblioteca estandar de Python.
"""
import os, re, sys, struct, zlib, time, argparse, colorsys
from collections import Counter

BASE = "/storage/emulated/0/sg_dump"
POP = bytes(bin(x).count("1") for x in range(256))
TAG_COLOR, TAG_SIL = 0x3A, 0xBA
SKIP_DIRS = {"extract", "build", "analysis", "analysis_v2", "SKGSTEAM", "mods"}
_RUN = re.compile(rb"(.)\1{0,62}", re.S)
_RUNANY = re.compile(rb"(.)\1*", re.S)


# ---------------------------------------------------------------- RLE (.art / .art-pt)
def rle_decode(src, start, end, limit):
    """00-7F: c+1 literales | 80-FC: repite 1 byte (c&7F)+1 | FD n V | FE u16be V | FF u24be V"""
    out = bytearray(); i = start
    try:
        while i < end:
            c = src[i]
            if c < 0x80:
                out += src[i + 1:i + 2 + c]; i += 2 + c
            elif c < 0xFD:
                out += src[i + 1:i + 2] * ((c & 0x7F) + 1); i += 2
            elif c == 0xFD:
                out += src[i + 2:i + 3] * src[i + 1]; i += 3
            elif c == 0xFE:
                out += src[i + 3:i + 4] * ((src[i + 1] << 8) | src[i + 2]); i += 4
            else:
                out += src[i + 4:i + 5] * ((src[i + 1] << 16) | (src[i + 2] << 8) | src[i + 3]); i += 5
            if len(out) > limit:
                return None
    except IndexError:
        return None
    return out if i == end else None


# ---------------------------------------------------------------- registros del .foit
def find_records(foit, art, artpt):
    """[u32 A][u32 B] 05|04 00 41 u16 celdas u16 ancho u16 alto u16 nBloques TAG mascara
       u32 offset u32 tam_normal u32 tam_grad u32 tam_band   (05 = .art, 04 = .art-pt)"""
    recs = []; nf = len(foit)
    i = foit.find(b"\x41", 10)
    while i != -1:
        nxt = i + 1
        pre = foit[i - 2]
        if foit[i - 1] == 0 and pre in (4, 5) and i + 10 <= nf:
            cells, w, h, n = struct.unpack_from("<HHHH", foit, i + 1)
            tag = foit[i + 9]
            if tag in (TAG_COLOR, TAG_SIL) and w and h and cells == w * h and 0 < n <= cells:
                ml = (cells + 7) // 8; e = i + 10 + ml
                if e + 16 <= nf:
                    mask = foit[i + 10:e]
                    if sum(POP[x] for x in mask) == n:
                        off, a, b, c = struct.unpack_from("<IIII", foit, e)
                        src = art if pre == 5 else artpt
                        P = n * 256
                        if src and a and b and c and off + a + b + c <= len(src):
                            pl = []
                            for x, y in ((off, off + a), (off + a, off + a + b), (off + a + b, off + a + b + c)):
                                d = rle_decode(src, x, y, P)
                                if d is None or len(d) != P:
                                    pl = None; break
                                pl.append(d)
                            if pl:
                                A, B = struct.unpack_from("<II", foit, i - 10)
                                recs.append(dict(src="art" if pre == 5 else "art-pt", tag=tag, w=w, h=h, n=n,
                                                 mask=mask, A=A, B=B, foit_off=i, off=off, size=a + b + c,
                                                 G=pl[1], Bd=pl[2]))
                                nxt = e + 16
        i = foit.find(b"\x41", nxt)
    return recs


# ---------------------------------------------------------------- paletas
def read_dds(d):
    """Paleta .dds sin comprimir (16 tonos x N bandas). Devuelve filas[banda][tono] = (r,g,b)."""
    if d[:4] != b"DDS ":
        raise ValueError("no es un archivo DDS")
    h, w = struct.unpack_from("<II", d, 12)
    flags, fourcc, bits, rm, gm, bm = struct.unpack_from("<I4sIIII", d, 80)
    if flags & 4 and fourcc.strip(b"\0"):
        raise ValueError("DDS comprimido (%r), no soportado" % fourcc)
    if bits != 32 or not (rm and gm and bm):
        rm, gm, bm = 0xFF0000, 0xFF00, 0xFF
    sh = lambda m: (m & -m).bit_length() - 1
    sr, sg, sb = sh(rm), sh(gm), sh(bm)
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            p = struct.unpack_from("<I", d, 128 + (y * w + x) * 4)[0]
            row.append(((p & rm) >> sr, (p & gm) >> sg, (p & bm) >> sb))
        rows.append(row)
    return rows


def fallback_pal():
    rows = []
    for b in range(32):
        hue = (b * 0.137) % 1.0
        rows.append([tuple(int(255 * v) for v in colorsys.hsv_to_rgb(hue, 0.55 if b else 0, 0.25 + 0.75 * t / 15))
                     for t in range(16)])
    return rows


def key_rgb(pal, band, g):
    """grad: line = g>>5 (oscurecido 0..7), tono = (g&31)/2 interpolado; band = fila de la paleta."""
    line = g >> 5; t = (g & 31) / 2.0
    wc = len(pal[0]); t0 = int(t); fr = t - t0
    t1 = min(t0 + 1, wc - 1); t0 = min(t0, wc - 1)
    row = pal[min(band, len(pal) - 1)]; k = 1 - line / 7.0
    return tuple((row[t0][c] * (1 - fr) + row[t1][c] * fr) * k for c in range(3))


def is_transparent(band, g):
    return band == 0 and (g >> 5) == 0


# ---------------------------------------------------------------- armado del frame
def place(r):
    """Coloca los bloques 16x16 de la tira en las celdas marcadas de la mascara (LSB primero, por filas).
       Devuelve los planos band y grad ya armados (sin perdida)."""
    w, h, n = r["w"], r["h"], r["n"]; W = w * 16; H = h * 16; sw = n * 16
    Bc = bytearray(W * H); Gc = bytearray(W * H); G = r["G"]; Bd = r["Bd"]; mask = r["mask"]; k = 0
    for j in range(w * h):
        if not (mask[j >> 3] >> (j & 7)) & 1:
            continue
        cy, cx = divmod(j, w); base = cy * 16 * W + cx * 16; s0 = k * 16
        for row in range(16):
            s = row * sw + s0; p = base + row * W
            Bc[p:p + 16] = Bd[s:s + 16]; Gc[p:p + 16] = G[s:s + 16]
        k += 1
    return Bc, Gc, W, H


# ---------------------------------------------------------------- PNG 32 bits (calidad completa)
def rgba_tables(pal):
    """Por banda: tablas R,G,B,A (256 valores de grad cada una).
       band 0 = borde exterior antialias: negro con alpha = line/7. Resto: color de paleta opaco."""
    nb = len(pal); out = []
    A0 = bytes((255 * (g >> 5) + 3) // 7 for g in range(256)); Z = bytes(256)
    out.append((Z, Z, Z, A0))
    full = bytes([255]) * 256
    for b in range(1, nb):
        cs = [key_rgb(pal, b, g) for g in range(256)]
        out.append(tuple(bytes(min(255, int(c[ch] + 0.5)) for c in cs) for ch in range(3)) + (full,))
    while len(out) < 256:
        out.append(out[-1])
    return out


def to_rgba(Bc, Gc, W, H, T4):
    out = bytearray(W * H * 4)
    R = bytearray(W); Gr = bytearray(W); Bl = bytearray(W); Al = bytearray(W)
    for y in range(H):
        o = y * W; brow = Bc[o:o + W]; grow = Gc[o:o + W]
        if brow.count(0) == W and max(grow) < 32:
            continue
        for m in _RUNANY.finditer(brow):
            s, e = m.span(); t = T4[brow[s]]; seg = grow[s:e]
            R[s:e] = seg.translate(t[0]); Gr[s:e] = seg.translate(t[1])
            Bl[s:e] = seg.translate(t[2]); Al[s:e] = seg.translate(t[3])
        q = o * 4; e4 = q + 4 * W
        out[q:e4:4] = R; out[q + 1:e4:4] = Gr; out[q + 2:e4:4] = Bl; out[q + 3:e4:4] = Al
    return out


def _chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)


def png_rgba(px, W, H):
    s = W * 4
    raw = b"".join(b"\x00" + bytes(px[y * s:(y + 1) * s]) for y in range(H))
    return (b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0))
            + _chunk(b"IDAT", zlib.compress(raw, 6)) + _chunk(b"IEND", b""))


# ---------------------------------------------------------------- ZOOM por capas (requiere numpy)
def _keys(d, np):
    d = np.abs(d)
    return np.where(d < 1, 1.5 * d ** 3 - 2.5 * d ** 2 + 1,
                    np.where(d < 2, -0.5 * d ** 3 + 2.5 * d ** 2 - 4 * d + 2, 0)).astype(np.float32)


def _up(a, s, np):
    """Bicubico separable de 4 taps (rapido y con poca memoria)."""
    for axis in (0, 1):
        n = a.shape[axis]; m = n * s
        x = (np.arange(m) + 0.5) / s - 0.5; i0 = np.floor(x).astype(np.int64); t = (x - i0).astype(np.float32)
        out = None
        for k in range(-1, 3):
            w = _keys(t - k, np); w = w[:, None] if axis == 0 else w[None, :]
            v = np.take(a, np.clip(i0 + k, 0, n - 1), axis=axis) * w
            out = v if out is None else out + v
        a = out
    return a


def _sharp(v, k, np):
    return np.clip((v - 0.5) * k + 0.5, 0, 1) if k > 1 else np.clip(v, 0, 1)


def zoom_frame(r, pal, s):
    """Escala el frame separando las capas del juego: el color base se interpola suave y el
       lineart (line) y el contorno se reconstruyen nitidos a la nueva resolucion.
       Devuelve (bytes RGBA, W, H)."""
    import numpy as np
    Bc, Gc, W, H = place(r)
    Bn = np.frombuffer(bytes(Bc), np.uint8).reshape(H, W); G = np.frombuffer(bytes(Gc), np.uint8).reshape(H, W)
    P = np.array(pal, np.float32); line = (G >> 5).astype(np.float32) / 7.0
    tt = (G & 31).astype(np.float32) / 2.0; t0 = np.floor(tt).astype(np.int64); fr = (tt - t0)[..., None]
    t1 = np.minimum(t0 + 1, P.shape[1] - 1); t0 = np.minimum(t0, P.shape[1] - 1); row = np.minimum(Bn, len(P) - 1)
    base = P[row, t0] * (1 - fr) + P[row, t1] * fr
    inside = (Bn != 0).astype(np.float32)
    alpha = np.where(Bn != 0, 1.0, line).astype(np.float32)
    ink = (np.where(Bn != 0, line, 1.0) * (alpha > 0)).astype(np.float32)
    ka = min(6.0, max(1.0, 0.4 * s)); ki = min(3.0, max(1.0, 0.2 * s))
    a_raw = _up(alpha, s, np); A = _sharp(a_raw, ka, np)
    I = _sharp(_up(ink * alpha, s, np) / np.maximum(a_raw, 1e-3), ki, np); del a_raw
    iw = np.maximum(_up(inside, s, np), 1e-3)
    out = np.empty((H * s, W * s, 4), np.uint8)
    for c in range(3):
        ch = _up(base[..., c] * inside, s, np) / iw
        out[..., c] = np.clip(ch * (1 - I), 0, 255).astype(np.uint8); del ch
    out[..., 3] = (A * 255 + 0.5).astype(np.uint8)
    return out.tobytes(), W * s, H * s


# ---------------------------------------------------------------- PCX 8 bits (Mugen clasico)
def nearest(c, pal):
    r, g, b = c; best = 0; bd = 1e30
    for i, (pr, pg, pb) in enumerate(pal):
        d = 2 * (r - pr) ** 2 + 4 * (g - pg) ** 2 + 3 * (b - pb) ** 2
        if d < bd:
            bd = d; best = i
    return best


def median_cut(pts, k):
    def stats(box):
        W = sum(p[3] for p in box) or 1e-9
        m = [sum(p[c] * p[3] for p in box) / W for c in range(3)]
        var = [sum(p[3] * (p[c] - m[c]) ** 2 for p in box) for c in range(3)]
        return W, m, var
    boxes = [pts]; st = [stats(pts)]
    while len(boxes) < k:
        cand = [j for j in range(len(boxes)) if len(boxes[j]) > 1]
        if not cand:
            break
        j = max(cand, key=lambda q: sum(st[q][2]))
        box = boxes[j]; ax = max(range(3), key=lambda c: st[j][2][c])
        box.sort(key=lambda p: p[ax])
        half = st[j][0] / 2; acc = 0; cut = 1
        for q, p in enumerate(box):
            acc += p[3]
            if acc >= half:
                cut = q + 1; break
        cut = min(max(cut, 1), len(box) - 1)
        b1, b2 = box[:cut], box[cut:]
        boxes[j] = b1; st[j] = stats(b1); boxes.append(b2); st.append(stats(b2))
    return [tuple(int(round(v)) for v in s[1]) for s in st]


def build_palette(cnt, pal):
    """Paleta compartida de 256 colores (0 = transparente) + LUT (banda,grad) -> indice."""
    keys = [k for k in cnt if not is_transparent(*k)]
    col = {k: tuple(int(round(v)) for v in key_rgb(pal, *k)) for k in keys}
    uni = Counter()
    for k in keys:
        uni[col[k]] += cnt[k]
    if len(uni) <= 255:
        palette = list(uni)
    else:
        palette = median_cut([[r, g, b, w ** 0.5] for (r, g, b), w in uni.items()], 255)
        for _ in range(2):  # refinamiento k-means
            acc = [[0.0, 0.0, 0.0, 0.0] for _ in palette]
            for rgb, w in uni.items():
                a = acc[nearest(rgb, palette)]; ww = w ** 0.5
                a[0] += rgb[0] * ww; a[1] += rgb[1] * ww; a[2] += rgb[2] * ww; a[3] += ww
            palette = [tuple(int(round(a[c] / a[3])) for c in range(3)) if a[3] else palette[j]
                       for j, a in enumerate(acc)]
    idx_rgb = {rgb: nearest(rgb, palette) + 1 for rgb in uni}
    lut = bytearray(65536); kidx = {}
    for k in keys:
        kidx[k] = idx_rgb[col[k]]; lut[(k[0] << 8) | k[1]] = kidx[k]
    full = [(0, 0, 0)] + palette
    full += [(0, 0, 0)] * (256 - len(full))
    return lut, full, kidx


def remap_palette(cnt, kidx, pal):
    """Misma asignacion de indices, colores de otra paleta (.dds 2p, 3p...)."""
    acc = [[0.0, 0.0, 0.0, 0.0] for _ in range(256)]
    for k, i in kidx.items():
        w = cnt[k]; c = key_rgb(pal, *k); a = acc[i]
        a[0] += c[0] * w; a[1] += c[1] * w; a[2] += c[2] * w; a[3] += w
    return [tuple(int(round(a[c] / a[3])) for c in range(3)) if a[3] else (0, 0, 0) for a in acc]


def act_bytes(pal):
    return bytes(c for rgb in reversed(pal) for c in rgb)  # Mugen lee los .act invertidos


def make_tables(lut):
    return [bytes(lut[b << 8:(b << 8) + 256]) for b in range(256)]


def render(r, lut, tabs):
    """Frame en indices de paleta (para PCX)."""
    Bc, Gc, W, H = place(r)
    idx = bytearray(W * H)
    for y in range(H):
        o = y * W; brow = Bc[o:o + W]
        for m in _RUNANY.finditer(brow):
            s, e = m.span()
            idx[o + s:o + e] = Gc[o + s:o + e].translate(tabs[brow[s]])
    return idx, W, H


def pcx_bytes(idx, W, H, pal):
    bpl = W + (W & 1)
    hdr = struct.pack("<BBBBHHHHHH", 10, 5, 1, 8, 0, 0, W - 1, H - 1, 72, 72)
    hdr += bytes(48) + b"\x00\x01" + struct.pack("<HHHH", bpl, 1, W, H)
    hdr += bytes(128 - len(hdr))
    body = bytearray(hdr); pad = b"\x00" * (bpl - W)
    for y in range(H):
        line = bytes(idx[y * W:(y + 1) * W]) + pad
        for m in _RUN.finditer(line):
            s = m.group(); v = s[0]; c = len(s)
            if c > 1 or v >= 0xC0:
                body.append(0xC0 | c); body.append(v)
            else:
                body.append(v)
    body.append(0x0C)
    body += bytes(c for rgb in pal for c in rgb)
    return bytes(body)


# ---------------------------------------------------------------- extraccion de un personaje
def extract_char(name, foit, art, artpt, pals, write, fmt="png", all_pals=False, log=print, zoom=1):
    t0 = time.time()
    recs = find_records(foit, art, artpt)
    if not recs:
        log("  x sin registros de imagen en el .foit"); return None
    cat = lambda r: "extras" if r["src"] == "art-pt" else ("siluetas" if r["tag"] == TAG_SIL else "sprites")
    nums = Counter(cat(r) for r in recs)
    cov_a = sum(r["size"] for r in recs if r["src"] == "art") / max(len(art), 1)
    cov_p = sum(r["size"] for r in recs if r["src"] == "art-pt") / max(len(artpt or b"x"), 1)
    log("  frames: sprites %d | extras %d | siluetas %d   (cubre .art %.1f%%, .art-pt %.1f%%)"
        % (nums["sprites"], nums["extras"], nums["siluetas"], cov_a * 100, cov_p * 100))
    if not pals:
        log("  ! sin paleta .dds: uso paleta de prueba (usa --pal ruta.dds)")
        pals = [("prueba", fallback_pal())]
    seq = Counter(); items = []
    csv = ["frame,origen,tag,foit_off,ancho,alto,bloques,A,B"]
    for r in recs:
        c = cat(r); fname = "%s/%s_%04d" % (c, name, seq[c]); seq[c] += 1; items.append((r, fname))
        csv.append("%s,%s,0x%02X,0x%X,%d,%d,%d,%d,%d" % (fname, r["src"], r["tag"], r["foit_off"],
                                                          r["w"] * 16, r["h"] * 16, r["n"], r["A"], r["B"]))
    write("frames.csv", ("\n".join(csv) + "\n").encode())
    if fmt in ("png", "ambos"):
        for label, p in (pals if all_pals else pals[:1]):
            T4 = rgba_tables(p); tp = time.time()
            for r, fname in items:
                Bc, Gc, W, H = place(r)
                write("%s/%s.png" % (label, fname), png_rgba(to_rgba(Bc, Gc, W, H, T4), W, H))
            log("  PNG 32 bits paleta %s: %d frames en %.1f s" % (label, len(items), time.time() - tp))
            if zoom > 1:
                tp = time.time()
                for j, (r, fname) in enumerate(items):
                    px, W, H = zoom_frame(r, p, zoom)
                    write("%s_%dx/%s.png" % (label, zoom, fname), png_rgba(px, W, H))
                    if j % 20 == 19:
                        log("    zoom %dx: %d/%d" % (zoom, j + 1, len(items)))
                log("  PNG %dx paleta %s: %d frames en %.1f s" % (zoom, label, len(items), time.time() - tp))
    if fmt in ("pcx", "ambos"):
        cnt = Counter()
        for r in recs:
            cnt.update(zip(r["Bd"], r["G"]))
        lut, palette, kidx = build_palette(cnt, pals[0][1]); tabs = make_tables(lut)
        for r, fname in items:
            idx, W, H = render(r, lut, tabs)
            write("pcx/%s.pcx" % fname, pcx_bytes(idx, W, H, palette))
        write("pcx/%s_%s.act" % (name, pals[0][0]), act_bytes(palette))
        for label, p in pals[1:]:
            write("pcx/%s_%s.act" % (name, label), act_bytes(remap_palette(cnt, kidx, p)))
        log("  PCX 8 bits: %d frames + %d .act (%d colores -> 255)" % (len(items), len(pals), len(kidx)))
    log("  ok en %.1f s" % (time.time() - t0))
    return dict(frames=len(recs), cats=dict(nums), cov_art=cov_a, cov_pt=cov_p)


# ---------------------------------------------------------------- CLI (Termux)
def norm(s):
    return re.sub(r"[^a-z0-9]", "", s.lower())


def index_files(root):
    idx = {}
    for dp, dn, fn in os.walk(root):
        dn[:] = [d for d in dn if d not in SKIP_DIRS]
        for f in fn:
            idx.setdefault(f.lower(), []).append(os.path.join(dp, f))
    return idx


def pick(idx, fname):
    paths = idx.get(fname.lower()) or []
    paths.sort(key=lambda p: (0 if "characters" in p else 1, len(p)))
    return paths[0] if paths else None


def pal_label(path, name):
    stem = norm(os.path.basename(path).rsplit(".", 1)[0]); n = norm(name)
    return stem[len(n):] if stem.startswith(n) and stem != n else stem


def find_pals(idx, name, pal_dir=None):
    """Paletas <nombre>_<N>p.dds (boss_marie_1p.dds sirve para 'bossmarie'). Prioriza pal_dir."""
    n = norm(name); best = {}
    pd = os.path.abspath(pal_dir) if pal_dir else None
    for fname, paths in idx.items():
        if not fname.endswith(".dds"):
            continue
        stem = norm(fname[:-4])
        if stem.startswith(n) and re.fullmatch(r"p?\d{1,2}p?", stem[len(n):]):
            label = re.sub(r"\D", "", stem[len(n):]) + "p"
            for p in paths:
                pri = 0 if pd and os.path.abspath(p).startswith(pd) else 1
                if label not in best or pri < best[label][0]:
                    best[label] = (pri, p)
    return sorted(((l, v[1]) for l, v in best.items()), key=lambda t: int(t[0][:-1]))


def main():
    ap = argparse.ArgumentParser(description="Extractor .art + .art-pt -> PNG/PCX (Skullgirls 2nd Encore)")
    ap.add_argument("personaje", help="nombre interno (ej. bossmarie, annie, filia) o 'all'")
    ap.add_argument("--pal", action="append", default=[], help="paleta .dds (repetible; la primera es la base)")
    ap.add_argument("--base", default=BASE, help="carpeta sg_dump")
    ap.add_argument("--out", default=None, help="carpeta de salida (por defecto sg_dump/extract)")
    ap.add_argument("--pals", default=None, help="carpeta de paletas (por defecto sg_dump/unpacked/sprites/sprites)")
    ap.add_argument("--formato", choices=["png", "pcx", "ambos"], default="png",
                    help="png = 32 bits sin perdida (defecto); pcx = 8 bits Mugen clasico; ambos")
    ap.add_argument("--todas", action="store_true", help="PNG con todas las paletas encontradas (1p, 2p, ...)")
    ap.add_argument("--zoom", type=int, choices=[1, 2, 4, 8, 16], default=1,
                    help="ademas, PNG ampliados por capas (lineart nitido). Requiere numpy: pkg install python-numpy")
    a = ap.parse_args()
    if a.zoom > 1:
        try:
            import numpy  # noqa
        except ImportError:
            print("--zoom necesita numpy. En Termux:  pkg install python-numpy"); sys.exit(1)
        if a.formato == "pcx":
            a.formato = "ambos"
    out_root = a.out or os.path.join(a.base, "extract")
    pal_dir = a.pals or os.path.join(a.base, "unpacked", "sprites", "sprites")
    print("Indexando %s ..." % a.base)
    idx = index_files(a.base)
    if os.path.isdir(pal_dir) and not os.path.abspath(pal_dir).startswith(os.path.abspath(a.base)):
        for k, v in index_files(pal_dir).items():
            idx.setdefault(k, []).extend(v)
    print("Paletas en: %s %s" % (pal_dir, "" if os.path.isdir(pal_dir) else "(NO EXISTE)"))
    names = sorted({f[:-5] for f in idx if f.endswith(".foit")}) if a.personaje.lower() == "all" else [a.personaje.lower()]
    ok = 0
    for name in names:
        foit_p, art_p, pt_p = pick(idx, name + ".foit"), pick(idx, name + ".art"), pick(idx, name + ".art-pt")
        print("\n== %s" % name)
        if not foit_p or not art_p:
            print("  x falta %s" % ("el .foit" if not foit_p else "el .art")); continue
        print("  foit: %s\n  art:  %s\n  pt:   %s" % (foit_p, art_p, pt_p or "(no hay)"))
        pals = []
        try:
            if a.pal:
                pals = [(pal_label(p, name), read_dds(open(p, "rb").read())) for p in a.pal]
            else:
                for label, p in find_pals(idx, name, pal_dir):
                    try:
                        pals.append((label, read_dds(open(p, "rb").read()))); print("  paleta %s: %s" % (label, p))
                    except ValueError as e:
                        print("  ! %s: %s" % (p, e))
                if not pals:
                    cand = sorted(f for f in idx if f.endswith(".dds") and norm(f).startswith(norm(name)[:4]))[:8]
                    print("  ! no encontre %s_1p.dds en %s" % (name, pal_dir))
                    if cand:
                        print("    parecidos: %s  (usa --pal ruta)" % ", ".join(cand))
        except (OSError, ValueError) as e:
            print("  x paleta: %s" % e); continue
        od = os.path.join(out_root, name)

        def write(rel, data, od=od):
            path = os.path.join(od, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as fh:
                fh.write(data)
        foit = open(foit_p, "rb").read(); art = open(art_p, "rb").read()
        artpt = open(pt_p, "rb").read() if pt_p else None
        if extract_char(name, foit, art, artpt, pals, write, fmt=a.formato, all_pals=a.todas, zoom=a.zoom):
            ok += 1; print("  -> %s" % od)
    print("\nListo: %d de %d personaje(s)." % (ok, len(names)))


if __name__ == "__main__":
    main()