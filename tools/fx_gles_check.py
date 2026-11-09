#!/usr/bin/env python3
"""fx_gles_check.py - renders the effect programs (src/fx/fx_glsl.cpp) with a real GLES driver and compares every pixel with the C++ reference.

    zdroid_pafx gen-check <dir> [--core <Core folder>]
    python3 tools/fx_gles_check.py <dir> [--tol 2] [--dump <png dir>]

Needs libEGL + libGLESv2 (Mesa's llvmpipe is fine: EGL_PLATFORM=surfaceless is set here) and numpy.  No window, no GPU required.
Exit status 0 = every case within tolerance (8-bit steps).
"""
import ctypes as C
import os
import sys

import numpy as np

os.environ.setdefault("EGL_PLATFORM", "surfaceless")
egl = C.CDLL("libEGL.so.1")
gl = C.CDLL("libGLESv2.so.2")

GL_FRAGMENT_SHADER, GL_VERTEX_SHADER = 0x8B30, 0x8B31
GL_COMPILE_STATUS, GL_LINK_STATUS, GL_INFO_LOG_LENGTH = 0x8B81, 0x8B82, 0x8B84
GL_ARRAY_BUFFER, GL_STATIC_DRAW, GL_FLOAT, GL_TRIANGLE_STRIP = 0x8892, 0x88E4, 0x1406, 0x5
GL_TEXTURE_2D, GL_TEXTURE0, GL_RGBA, GL_UNSIGNED_BYTE = 0x0DE1, 0x84C0, 0x1908, 0x1401
GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER, GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T = 0x2801, 0x2800, 0x2802, 0x2803
GL_NEAREST, GL_CLAMP_TO_EDGE, GL_COLOR_BUFFER_BIT, GL_BLEND = 0x2600, 0x812F, 0x4000, 0x0BE2
GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_COMPLETE = 0x8D40, 0x8CE0, 0x8CD5

egl.eglGetDisplay.restype = C.c_void_p
egl.eglGetDisplay.argtypes = [C.c_void_p]
egl.eglInitialize.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p]
egl.eglChooseConfig.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.c_int, C.c_void_p]
egl.eglCreateContext.restype = C.c_void_p
egl.eglCreateContext.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p]
egl.eglMakeCurrent.argtypes = [C.c_void_p] * 4
gl.glGetString.restype = C.c_char_p
gl.glCreateShader.restype = C.c_uint
gl.glCreateProgram.restype = C.c_uint
gl.glGetAttribLocation.restype = C.c_int
gl.glGetUniformLocation.restype = C.c_int
gl.glGetUniformLocation.argtypes = [C.c_uint, C.c_char_p]
gl.glGetAttribLocation.argtypes = [C.c_uint, C.c_char_p]


def make_context():
    d = egl.eglGetDisplay(None)
    maj, mnr = C.c_int(), C.c_int()
    if not egl.eglInitialize(C.c_void_p(d), C.byref(maj), C.byref(mnr)):
        sys.exit("eglInitialize failed")
    egl.eglBindAPI(0x30A0)
    attrs = (C.c_int * 5)(0x3033, 0x1, 0x3040, 0x4, 0x3038)
    cfg, n = C.c_void_p(), C.c_int()
    if not egl.eglChooseConfig(C.c_void_p(d), attrs, C.byref(cfg), 1, C.byref(n)) or n.value < 1:
        sys.exit("no EGL config")
    ctx = egl.eglCreateContext(C.c_void_p(d), cfg, None, (C.c_int * 3)(0x3098, 2, 0x3038))
    if not egl.eglMakeCurrent(C.c_void_p(d), None, None, C.c_void_p(ctx)):
        sys.exit("eglMakeCurrent failed")
    return gl.glGetString(0x1F01).decode(), gl.glGetString(0x1F02).decode()


def compile_shader(kind, src):
    s = gl.glCreateShader(kind)
    b = src.encode()
    ptr = C.c_char_p(b)
    gl.glShaderSource(s, 1, C.byref(ptr), None)
    gl.glCompileShader(s)
    ok = C.c_int()
    gl.glGetShaderiv(s, GL_COMPILE_STATUS, C.byref(ok))
    if not ok.value:
        n = C.c_int()
        gl.glGetShaderiv(s, GL_INFO_LOG_LENGTH, C.byref(n))
        log = C.create_string_buffer(max(n.value, 1))
        gl.glGetShaderInfoLog(s, n, None, log)
        raise RuntimeError("shader compile failed:\n" + log.value.decode())
    return s


def link(vs, fs):
    p = gl.glCreateProgram()
    gl.glAttachShader(p, vs)
    gl.glAttachShader(p, fs)
    gl.glLinkProgram(p)
    ok = C.c_int()
    gl.glGetProgramiv(p, GL_LINK_STATUS, C.byref(ok))
    if not ok.value:
        log = C.create_string_buffer(2048)
        gl.glGetProgramInfoLog(p, 2048, None, log)
        raise RuntimeError("link failed:\n" + log.value.decode())
    return p


def parse_cases(path):
    cases, cur = [], None
    for line in open(path):
        w = line.split()
        if not w:
            continue
        if w[0] == "case":
            cur = {"name": w[1], "prog": w[2], "w": int(w[3]), "h": int(w[4]), "tex": [], "uni": [], "z": (0.0, 0.0), "color": (1.0, 1.0, 1.0, 1.0)}
        elif w[0] == "tex":
            cur["tex"].append(w[1:])
        elif w[0] == "uniform":
            cur["uni"].append((w[1], w[2], [float(x) for x in w[3:]]))
        elif w[0] == "z":
            cur["z"] = (float(w[1]), float(w[2]))
        elif w[0] == "color":
            cur["color"] = tuple(float(x) for x in w[1:5])
        elif w[0] == "ref":
            cur["ref"] = w[1]
        elif w[0] == "end":
            cases.append(cur)
    return cases


def run_case(d, c):
    W, H = c["w"], c["h"]
    vs = compile_shader(GL_VERTEX_SHADER, open(os.path.join(d, "programs", c["prog"] + ".vert")).read())
    fs = compile_shader(GL_FRAGMENT_SHADER, open(os.path.join(d, "programs", c["prog"] + ".frag")).read())
    prog = link(vs, fs)

    fbo, rbo = C.c_uint(), C.c_uint()
    gl.glGenFramebuffers(1, C.byref(fbo))
    gl.glBindFramebuffer(GL_FRAMEBUFFER, fbo)
    tgt = C.c_uint()
    gl.glGenTextures(1, C.byref(tgt))
    gl.glBindTexture(GL_TEXTURE_2D, tgt)
    gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, None)
    gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tgt, 0)
    if gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE:
        raise RuntimeError("framebuffer incomplete")
    gl.glViewport(0, 0, W, H)
    gl.glDisable(GL_BLEND)
    gl.glClearColor(C.c_float(0), C.c_float(0), C.c_float(0), C.c_float(0))   # ctypes needs explicit floats or the registers hold junk
    gl.glClear(GL_COLOR_BUFFER_BIT)
    gl.glUseProgram(prog)

    # full-screen quad: x, y, z(height), u, v  - v grows with y, row 0 is the bottom row of the read-back
    z0, z1 = c["z"]
    quad = np.array([-1, -1, z0, 0, 0, 1, -1, z0, 1, 0, -1, 1, z1, 0, 1, 1, 1, z1, 1, 1], dtype=np.float32)
    vbo = C.c_uint()
    gl.glGenBuffers(1, C.byref(vbo))
    gl.glBindBuffer(GL_ARRAY_BUFFER, vbo)
    gl.glBufferData(GL_ARRAY_BUFFER, quad.nbytes, quad.ctypes.data_as(C.c_void_p), GL_STATIC_DRAW)
    pos, uv, col = (gl.glGetAttribLocation(prog, n) for n in (b"aPos", b"aUV", b"aColor"))
    gl.glEnableVertexAttribArray(pos)
    gl.glVertexAttribPointer(pos, 3, GL_FLOAT, 0, 20, C.c_void_p(0))
    gl.glEnableVertexAttribArray(uv)
    gl.glVertexAttribPointer(uv, 2, GL_FLOAT, 0, 20, C.c_void_p(12))
    if col >= 0:
        gl.glDisableVertexAttribArray(col)
        gl.glVertexAttrib4f(col, C.c_float(c["color"][0]), C.c_float(c["color"][1]), C.c_float(c["color"][2]), C.c_float(c["color"][3]))

    ident = np.eye(4, dtype=np.float32)
    loc = gl.glGetUniformLocation(prog, b"uMvp")
    gl.glUniformMatrix4fv(loc, 1, 0, ident.ctypes.data_as(C.c_void_p))

    unit = 0
    for t in c["tex"]:
        name, file = t[0], t[1]
        data = np.fromfile(os.path.join(d, file), dtype=np.uint8)
        tw, th = (int(t[3]), int(t[4])) if len(t) >= 5 else (W, H)
        assert data.size == tw * th * 4, (file, data.size, tw, th)
        tex = C.c_uint()
        gl.glActiveTexture(GL_TEXTURE0 + unit)
        gl.glGenTextures(1, C.byref(tex))
        gl.glBindTexture(GL_TEXTURE_2D, tex)
        for p in (GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER):
            gl.glTexParameteri(GL_TEXTURE_2D, p, GL_NEAREST)
        for p in (GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T):
            gl.glTexParameteri(GL_TEXTURE_2D, p, GL_CLAMP_TO_EDGE)
        gl.glPixelStorei(0x0CF5, 1)
        gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.ctypes.data_as(C.c_void_p))
        gl.glUniform1i(gl.glGetUniformLocation(prog, name.encode()), unit)
        unit += 1

    missing = []
    for typ, name, vals in c["uni"]:
        l = gl.glGetUniformLocation(prog, name.encode())
        if l < 0:
            missing.append(name)   # optimised away is fine: the program does not use it
            continue
        arr = np.array(vals, dtype=np.float32)
        p = arr.ctypes.data_as(C.c_void_p)
        n = {"f1": 1, "f2": 2, "f3": 3, "f4": 4}[typ]
        fn = {"f1": gl.glUniform1fv, "f2": gl.glUniform2fv, "f3": gl.glUniform3fv, "f4": gl.glUniform4fv}[typ]
        fn(l, len(vals) // n, p)

    gl.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4)
    out = np.zeros(W * H * 4, dtype=np.uint8)
    gl.glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, out.ctypes.data_as(C.c_void_p))
    err = gl.glGetError()
    if err:
        raise RuntimeError("GL error 0x%x" % err)
    return out.reshape(H, W, 4), missing


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    d = sys.argv[1]
    tol = int(sys.argv[sys.argv.index("--tol") + 1]) if "--tol" in sys.argv else 2
    dump = sys.argv[sys.argv.index("--dump") + 1] if "--dump" in sys.argv else None
    renderer, version = make_context()
    print("GL: %s / %s" % (renderer, version))
    bad = 0
    for c in parse_cases(os.path.join(d, "cases.txt")):
        got, missing = run_case(d, c)
        ref = np.fromfile(os.path.join(d, c["ref"]), dtype=np.uint8).reshape(c["h"], c["w"], 4)
        diff = np.abs(got.astype(int) - ref.astype(int))
        mx, mean = int(diff.max()), float(diff.mean())
        n_over = int((diff.max(axis=2) > tol).sum())
        ok = mx <= tol
        bad += 0 if ok else 1
        print("%-34s max %2d  mean %.3f  pixels over tol %4d  %s%s" % (c["name"], mx, mean, n_over, "ok" if ok else "FAIL", ("  (unused: %s)" % ",".join(missing)) if missing else ""))
        if dump:
            os.makedirs(dump, exist_ok=True)
            try:
                from PIL import Image
                Image.fromarray(np.concatenate([got[::-1], ref[::-1]], axis=1)).resize((c["w"] * 4, c["h"] * 2)).save(os.path.join(dump, c["name"] + ".png"))
            except ImportError:
                pass
    print("%d case(s) failed" % bad if bad else "all cases match within %d step(s)" % tol)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
