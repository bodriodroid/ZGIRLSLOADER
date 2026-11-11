// fx_gl.cpp - see fx_gl.h.
#include "fx_gl.h"

#include "fx_gl_headers.h"

#include <algorithm>
#include <cstring>

namespace zengine {

// ---------------------------------------------------------------------------
// GL entry points (loaded through SDL, so this file links against no GL library)
// ---------------------------------------------------------------------------

#define ZD_FX_GL_FUNCS(X)                                                  \
    X(PFNGLACTIVETEXTUREPROC, ActiveTexture)                               \
    X(PFNGLATTACHSHADERPROC, AttachShader)                                 \
    X(PFNGLBINDATTRIBLOCATIONPROC, BindAttribLocation)                     \
    X(PFNGLBINDBUFFERPROC, BindBuffer)                                     \
    X(PFNGLBINDTEXTUREPROC, BindTexture)                                   \
    X(PFNGLBLENDEQUATIONSEPARATEPROC, BlendEquationSeparate)               \
    X(PFNGLBLENDFUNCSEPARATEPROC, BlendFuncSeparate)                       \
    X(PFNGLBUFFERDATAPROC, BufferData)                                     \
    X(PFNGLCOMPILESHADERPROC, CompileShader)                               \
    X(PFNGLCREATEPROGRAMPROC, CreateProgram)                               \
    X(PFNGLCREATESHADERPROC, CreateShader)                                 \
    X(PFNGLDELETEBUFFERSPROC, DeleteBuffers)                               \
    X(PFNGLDELETEPROGRAMPROC, DeleteProgram)                               \
    X(PFNGLDELETESHADERPROC, DeleteShader)                                 \
    X(PFNGLDELETETEXTURESPROC, DeleteTextures)                             \
    X(PFNGLDISABLEPROC, Disable)                                           \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, DisableVertexAttribArray)         \
    X(PFNGLDRAWARRAYSPROC, DrawArrays)                                     \
    X(PFNGLENABLEPROC, Enable)                                             \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray)           \
    X(PFNGLGENBUFFERSPROC, GenBuffers)                                     \
    X(PFNGLGENTEXTURESPROC, GenTextures)                                   \
    X(PFNGLGETERRORPROC, GetError)                                         \
    X(PFNGLGETINTEGERVPROC, GetIntegerv)                                   \
    X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog)                       \
    X(PFNGLGETPROGRAMIVPROC, GetProgramiv)                                 \
    X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog)                         \
    X(PFNGLGETSHADERIVPROC, GetShaderiv)                                   \
    X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation)                     \
    X(PFNGLGETVERTEXATTRIBIVPROC, GetVertexAttribiv)                       \
    X(PFNGLISENABLEDPROC, IsEnabled)                                       \
    X(PFNGLLINKPROGRAMPROC, LinkProgram)                                   \
    X(PFNGLSHADERSOURCEPROC, ShaderSource)                                 \
    X(PFNGLTEXIMAGE2DPROC, TexImage2D)                                     \
    X(PFNGLTEXPARAMETERIPROC, TexParameteri)                               \
    X(PFNGLUNIFORM1FPROC, Uniform1f)                                       \
    X(PFNGLUNIFORM1IPROC, Uniform1i)                                       \
    X(PFNGLUNIFORM2FPROC, Uniform2f)                                       \
    X(PFNGLUNIFORM3FPROC, Uniform3f)                                       \
    X(PFNGLUNIFORMMATRIX4FVPROC, UniformMatrix4fv)                         \
    X(PFNGLUSEPROGRAMPROC, UseProgram)                                     \
    X(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer)

struct FxRenderer::Gl {
#define ZD_X(T, N) T N = nullptr;
    ZD_FX_GL_FUNCS(ZD_X)
#undef ZD_X
    bool load() {
        bool ok = true;
#define ZD_X(T, N)                                                    \
    N = reinterpret_cast<T>(SDL_GL_GetProcAddress("gl" #N));          \
    ok = ok && N != nullptr;
        ZD_FX_GL_FUNCS(ZD_X)
#undef ZD_X
        return ok;
    }
};

namespace {

constexpr int kAttrib = 5;   // SDL's renderer uses attribute slots 0..4; every one of them is saved

struct Vertex {
    float x, y, z, u, v, r, g, b, a;
};

}  // namespace

// ---------------------------------------------------------------------------
// FxRenderer: setup
// ---------------------------------------------------------------------------

FxRenderer::FxRenderer() = default;

FxRenderer::~FxRenderer() { shutdown(); }

bool FxRenderer::build(const fx::FxPack& pack, const char* name, Program& out, std::string* err) {
    const fx::FxProgramSource* src = pack.program(name);
    if (!src) {
        if (err) *err = std::string("the effects pack has no program '") + name + "'";
        return false;
    }
    Gl& gl = *gl_;
    auto compile = [&](GLenum type, const std::string& text, const char* what) -> GLuint {
        GLuint sh = gl.CreateShader(type);
        if (!sh) return 0;
        const char* p = text.c_str();
        gl.ShaderSource(sh, 1, &p, nullptr);
        gl.CompileShader(sh);
        GLint ok = 0;
        gl.GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512] = {};
            gl.GetShaderInfoLog(sh, sizeof(log) - 1, nullptr, log);
            if (err) *err = std::string(name) + " " + what + " shader: " + log;
            gl.DeleteShader(sh);
            return 0;
        }
        return sh;
    };
    const GLuint vs = compile(GL_VERTEX_SHADER, src->vertex, "vertex");
    if (!vs) return false;
    const GLuint fs = compile(GL_FRAGMENT_SHADER, src->fragment, "fragment");
    if (!fs) {
        gl.DeleteShader(vs);
        return false;
    }
    const GLuint prog = gl.CreateProgram();
    gl.AttachShader(prog, vs);
    gl.AttachShader(prog, fs);
    gl.BindAttribLocation(prog, 0, "aPos");
    gl.BindAttribLocation(prog, 1, "aUV");
    gl.BindAttribLocation(prog, 2, "aColor");
    gl.LinkProgram(prog);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);
    GLint ok = 0;
    gl.GetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = {};
        gl.GetProgramInfoLog(prog, sizeof(log) - 1, nullptr, log);
        if (err) *err = std::string(name) + " link: " + log;
        gl.DeleteProgram(prog);
        return false;
    }
    out = Program{};
    out.id = prog;
    auto loc = [&](const char* n) { return gl.GetUniformLocation(prog, n); };
    out.uMvp = loc("uMvp");
    out.uScreen = loc("uScreen");
    out.uFlashLow = loc("uFlashLow");
    out.uFlashHigh = loc("uFlashHigh");
    out.uFlashLuma = loc("uFlashLuma");
    out.uFlashCurve = loc("uFlashCurve");
    out.uShadowTint = loc("uShadowTint");
    out.uShadowLuma = loc("uShadowLuma");
    out.uIndex = loc("uIndex");
    out.uPalette = loc("uPalette");
    out.uPaletteSize = loc("uPaletteSize");
    out.uAlpha = loc("uAlpha");
    out.uLine = loc("uLine");
    out.uHue = loc("uHue");
    out.uFloor = loc("uFloor");
    out.uOutlineA = loc("uOutlineA");
    out.uOutlineB = loc("uOutlineB");
    out.uOutlineMix = loc("uOutlineMix");
    out.uTexSize = loc("uTexSize");
    return true;
}

bool FxRenderer::init(SDL_Renderer* r, const fx::FxPack& pack, std::string* err) {
    shutdown();
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        shutdown();
        return false;
    };
    if (!r) return fail("no renderer");
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(r, &info) != 0) return fail(SDL_GetError());
    if (!info.name || std::strcmp(info.name, "opengles2") != 0)
        return fail(std::string("the renderer is '") + (info.name ? info.name : "?") + "', GPU effects need 'opengles2'");
    if (!SDL_GL_GetCurrentContext()) return fail("no current GL context");
    r_ = r;
    ctx_ = SDL_GL_GetCurrentContext();
    gl_ = new Gl;
    if (!gl_->load()) return fail("a GL entry point is missing");
    SDL_RenderFlush(r);   // whatever SDL has queued runs before the first GL call of ours
    while (gl_->GetError() != GL_NO_ERROR) {}
    GLint mt = 0;
    gl_->GetIntegerv(GL_MAX_TEXTURE_SIZE, &mt);
    maxTex_ = mt;
    std::string e;
    if (!build(pack, "sprite_palette", progSprite_, &e) || !build(pack, "floor_shadow", progShadow_, &e)) return fail(e);
    std::string ignore;
    build(pack, "super_outline", progOutline_, &ignore);   // optional
    build(pack, "sprite_palette_smooth", progSmooth_, &ignore);
    gl_->GenBuffers(1, &vbo_);
    if (!vbo_) return fail("no vertex buffer");
    tuning_ = pack.tuning();
    while (gl_->GetError() != GL_NO_ERROR) {}
    ready_ = true;
    return true;
}

void FxRenderer::shutdown() {
    if (gl_ && SDL_GL_GetCurrentContext()) {
        for (Program* p : {&progSprite_, &progSmooth_, &progShadow_, &progOutline_})
            if (p->id) gl_->DeleteProgram(p->id);
        if (vbo_) gl_->DeleteBuffers(1, &vbo_);
    }
    progSprite_ = progSmooth_ = progShadow_ = progOutline_ = Program{};
    vbo_ = 0;
    delete gl_;
    gl_ = nullptr;
    r_ = nullptr;
    ctx_ = nullptr;
    ready_ = false;
    maxTex_ = 0;
}

// ---------------------------------------------------------------------------
// FxRenderer: textures
// ---------------------------------------------------------------------------

std::vector<uint8_t> FxRenderer::packIndex(const uint8_t* band, const uint8_t* grad, int w, int h) {
    std::vector<uint8_t> out;
    if (!band || !grad || w <= 0 || h <= 0) return out;
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    out.resize(n * 4);
    for (size_t i = 0; i < n; ++i) {
        out[i * 4 + 0] = 0;
        out[i * 4 + 1] = grad[i];
        out[i * 4 + 2] = band[i];
        out[i * 4 + 3] = 255;
    }
    return out;
}

unsigned FxRenderer::makeTexture(int w, int h, const uint8_t* rgba) {
    if (!ready_ || !rgba || w <= 0 || h <= 0 || w > maxTex_ || h > maxTex_) return 0;
    Gl& gl = *gl_;
    while (gl.GetError() != GL_NO_ERROR) {}
    GLint active = GL_TEXTURE0, bound0 = 0;
    gl.GetIntegerv(GL_ACTIVE_TEXTURE, &active);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &bound0);
    GLuint tex = 0;
    gl.GenTextures(1, &tex);
    gl.BindTexture(GL_TEXTURE_2D, tex);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);   // rows of w*4 bytes are always 4-aligned
    const bool bad = gl.GetError() != GL_NO_ERROR;
    gl.BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(bound0));
    gl.ActiveTexture(static_cast<GLenum>(active));
    if (bad) {
        gl.DeleteTextures(1, &tex);
        return 0;
    }
    return tex;
}

FxIndexTex FxRenderer::createIndex(const uint8_t* packed, int w, int h) {
    FxIndexTex t;
    t.gl = makeTexture(w, h, packed);
    if (t.gl) {
        t.w = w;
        t.h = h;
    }
    return t;
}

FxPaletteTex FxRenderer::createPalette(const Palette& pal) {
    FxPaletteTex t;
    if (pal.tones <= 0 || pal.bands <= 0 || pal.rgb.size() < static_cast<size_t>(pal.tones) * pal.bands * 3) return t;
    std::vector<uint8_t> rgba(static_cast<size_t>(pal.tones) * pal.bands * 4);
    for (size_t i = 0, n = static_cast<size_t>(pal.tones) * pal.bands; i < n; ++i) {
        rgba[i * 4 + 0] = pal.rgb[i * 3 + 0];
        rgba[i * 4 + 1] = pal.rgb[i * 3 + 1];
        rgba[i * 4 + 2] = pal.rgb[i * 3 + 2];
        rgba[i * 4 + 3] = 255;
    }
    t.gl = makeTexture(pal.tones, pal.bands, rgba.data());
    if (t.gl) {
        t.tones = pal.tones;
        t.bands = pal.bands;
    }
    return t;
}

void FxRenderer::destroy(FxIndexTex& t) {
    if (t.gl && ready_ && SDL_GL_GetCurrentContext()) {
        const GLuint id = t.gl;
        gl_->DeleteTextures(1, &id);
    }
    t = FxIndexTex{};
}

void FxRenderer::destroy(FxPaletteTex& t) {
    if (t.gl && ready_ && SDL_GL_GetCurrentContext()) {
        const GLuint id = t.gl;
        gl_->DeleteTextures(1, &id);
    }
    t = FxPaletteTex{};
}

// ---------------------------------------------------------------------------
// Pass
// ---------------------------------------------------------------------------

struct FxRenderer::Pass::Saved {
    GLint program = 0, arrayBuffer = 0, activeTex = GL_TEXTURE0, tex0 = 0, tex1 = 0;
    GLint blend = 0, srcRgb = 0, dstRgb = 0, srcA = 0, dstA = 0, eqRgb = 0, eqA = 0;
    GLint attrib[kAttrib] = {};
    GLint vp[4] = {};
};

FxRenderer::Pass::Pass(FxRenderer& fx) : fx_(fx) {
    if (!fx.ready_ || !SDL_GL_GetCurrentContext() || SDL_GL_GetCurrentContext() != fx.ctx_) return;
    SDL_Renderer* r = fx.r_;
    Gl& gl = *fx.gl_;

    // Make SDL apply its pending viewport, clip rectangle and render target and run everything it has queued: a draw that does
    // nothing (alpha 0) is enough, SDL sets its state before it draws.  The point goes inside the clip rectangle when there is one.
    Uint8 cr = 0, cg = 0, cb = 0, ca = 0;
    SDL_BlendMode bm = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawColor(r, &cr, &cg, &cb, &ca);
    SDL_GetRenderDrawBlendMode(r, &bm);
    int px = 0, py = 0;
    if (SDL_RenderIsClipEnabled(r)) {
        SDL_Rect clip;
        SDL_RenderGetClipRect(r, &clip);
        px = clip.x;
        py = clip.y;
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderDrawPoint(r, px, py);
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    SDL_SetRenderDrawBlendMode(r, bm);
    SDL_RenderFlush(r);

    saved_ = new Saved;
    Saved& s = *saved_;
    gl.GetIntegerv(GL_CURRENT_PROGRAM, &s.program);
    gl.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &s.arrayBuffer);
    gl.GetIntegerv(GL_ACTIVE_TEXTURE, &s.activeTex);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &s.tex0);
    gl.ActiveTexture(GL_TEXTURE1);
    gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &s.tex1);
    gl.ActiveTexture(static_cast<GLenum>(s.activeTex));
    gl.GetIntegerv(GL_BLEND_SRC_RGB, &s.srcRgb);
    gl.GetIntegerv(GL_BLEND_DST_RGB, &s.dstRgb);
    gl.GetIntegerv(GL_BLEND_SRC_ALPHA, &s.srcA);
    gl.GetIntegerv(GL_BLEND_DST_ALPHA, &s.dstA);
    gl.GetIntegerv(GL_BLEND_EQUATION_RGB, &s.eqRgb);
    gl.GetIntegerv(GL_BLEND_EQUATION_ALPHA, &s.eqA);
    s.blend = gl.IsEnabled(GL_BLEND) ? 1 : 0;
    for (int i = 0; i < kAttrib; ++i) gl.GetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &s.attrib[i]);
    gl.GetIntegerv(GL_VIEWPORT, s.vp);

    vpW_ = static_cast<float>(s.vp[2]);
    vpH_ = static_cast<float>(s.vp[3]);
    target_ = SDL_GetRenderTarget(r) != nullptr;
    SDL_RenderGetScale(r, &sx_, &sy_);
    ok_ = vpW_ > 0 && vpH_ > 0;
}

FxRenderer::Pass::~Pass() {
    if (!saved_) return;
    if (!fx_.gl_ || !fx_.ready_) {   // shutdown() ran while the pass was open: the context's objects are gone, nothing to put back
        delete saved_;
        saved_ = nullptr;
        return;
    }
    Gl& gl = *fx_.gl_;
    const Saved& s = *saved_;
    while (gl.GetError() != GL_NO_ERROR) {}   // an error of ours must not be reported as SDL's
    gl.UseProgram(static_cast<GLuint>(s.program));
    gl.BindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(s.arrayBuffer));
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(s.tex0));
    gl.ActiveTexture(GL_TEXTURE1);
    gl.BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(s.tex1));
    gl.ActiveTexture(static_cast<GLenum>(s.activeTex));
    gl.BlendFuncSeparate(static_cast<GLenum>(s.srcRgb), static_cast<GLenum>(s.dstRgb), static_cast<GLenum>(s.srcA), static_cast<GLenum>(s.dstA));
    gl.BlendEquationSeparate(static_cast<GLenum>(s.eqRgb), static_cast<GLenum>(s.eqA));
    if (s.blend)
        gl.Enable(GL_BLEND);
    else
        gl.Disable(GL_BLEND);
    for (int i = 0; i < kAttrib; ++i) {
        if (s.attrib[i])
            gl.EnableVertexAttribArray(static_cast<GLuint>(i));
        else
            gl.DisableVertexAttribArray(static_cast<GLuint>(i));
    }
    delete saved_;
    saved_ = nullptr;
}

// program, projection, textures and blending for one draw
bool FxRenderer::Pass::prepare(const Program& pr, const FxIndexTex& img, const FxPaletteTex* pal) {
    if (!ok_ || !pr.id || !img.valid() || (pal && !pal->valid())) return false;
    Gl& gl = *fx_.gl_;
    gl.UseProgram(pr.id);
    // SDL's own projection: vertices are scaled by the renderer's scale, the window's y runs down, a render target's y runs up
    const float a = 2.0f * sx_ / vpW_;
    const float b = (target_ ? 2.0f : -2.0f) * sy_ / vpH_;
    const float m[16] = {a, 0, 0, 0, 0, b, 0, 0, 0, 0, 1, 0, -1.0f, target_ ? -1.0f : 1.0f, 0, 1};
    if (pr.uMvp >= 0) gl.UniformMatrix4fv(pr.uMvp, 1, GL_FALSE, m);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, img.gl);
    if (pr.uIndex >= 0) gl.Uniform1i(pr.uIndex, 0);
    if (pal) {
        gl.ActiveTexture(GL_TEXTURE1);
        gl.BindTexture(GL_TEXTURE_2D, pal->gl);
        if (pr.uPalette >= 0) gl.Uniform1i(pr.uPalette, 1);
        if (pr.uPaletteSize >= 0) gl.Uniform2f(pr.uPaletteSize, static_cast<float>(pal->tones), static_cast<float>(pal->bands));
    }
    gl.Enable(GL_BLEND);
    gl.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);   // SDL's "blend" mode
    gl.BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    return true;
}

void FxRenderer::Pass::setScreen(const Program& pr, const fx::ScreenFx& s) {
    Gl& gl = *fx_.gl_;
    if (pr.uScreen >= 0) gl.Uniform2f(pr.uScreen, s.shadowT, s.flashT);
    if (pr.uFlashLow >= 0) gl.Uniform3f(pr.uFlashLow, s.look.low[0], s.look.low[1], s.look.low[2]);
    if (pr.uFlashHigh >= 0) gl.Uniform3f(pr.uFlashHigh, s.look.high[0], s.look.high[1], s.look.high[2]);
    if (pr.uFlashLuma >= 0) gl.Uniform3f(pr.uFlashLuma, s.lumaCoeff[0], s.lumaCoeff[1], s.lumaCoeff[2]);
    if (pr.uFlashCurve >= 0) gl.Uniform3f(pr.uFlashCurve, s.contrast, s.brightness, s.saturation);
    if (pr.uShadowTint >= 0) gl.Uniform3f(pr.uShadowTint, s.shadowTint[0], s.shadowTint[1], s.shadowTint[2]);
    if (pr.uShadowLuma >= 0) gl.Uniform3f(pr.uShadowLuma, s.shadowLuma[0], s.shadowLuma[1], s.shadowLuma[2]);
}

void FxRenderer::Pass::emit(const float xy[4][2], const float z[4], const float uv[4][2]) {
    Gl& gl = *fx_.gl_;
    Vertex v[4];
    for (int i = 0; i < 4; ++i) v[i] = Vertex{xy[i][0], xy[i][1], z[i], uv[i][0], uv[i][1], 1.0f, 1.0f, 1.0f, 1.0f};
    gl.BindBuffer(GL_ARRAY_BUFFER, fx_.vbo_);
    gl.BufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
    const GLsizei stride = sizeof(Vertex);
    auto off = [](size_t o) { return reinterpret_cast<const void*>(static_cast<uintptr_t>(o)); };
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, off(0));
    gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, off(12));
    gl.VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, off(20));
    for (GLuint i = 0; i < 3; ++i) gl.EnableVertexAttribArray(i);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

namespace {
// strip order: top-left, top-right, bottom-left, bottom-right
void rectQuad(const SDL_FRect& d, bool flipH, float xy[4][2], float uv[4][2]) {
    const float x0 = d.x, x1 = d.x + d.w, y0 = d.y, y1 = d.y + d.h;
    const float u0 = flipH ? 1.0f : 0.0f, u1 = flipH ? 0.0f : 1.0f;
    const float px[4] = {x0, x1, x0, x1}, py[4] = {y0, y0, y1, y1}, pu[4] = {u0, u1, u0, u1}, pv[4] = {0, 0, 1, 1};
    for (int i = 0; i < 4; ++i) {
        xy[i][0] = px[i];
        xy[i][1] = py[i];
        uv[i][0] = pu[i];
        uv[i][1] = pv[i];
    }
}
}  // namespace

void FxRenderer::Pass::sprite(const FxIndexTex& img, const FxPaletteTex& pal, const SDL_FRect& dst, bool flipH, const fx::SpriteParams& p, bool smooth) {
    const Program& pr = smooth && fx_.progSmooth_.id ? fx_.progSmooth_ : fx_.progSprite_;
    if (!prepare(pr, img, &pal)) return;
    Gl& gl = *fx_.gl_;
    if (pr.uTexSize >= 0) gl.Uniform2f(pr.uTexSize, static_cast<float>(img.w), static_cast<float>(img.h));
    setScreen(pr, p.screen);
    if (pr.uAlpha >= 0) gl.Uniform1f(pr.uAlpha, p.alpha);
    if (pr.uLine >= 0) gl.Uniform3f(pr.uLine, p.line[0], p.line[1], p.line[2]);
    if (pr.uHue >= 0) gl.Uniform3f(pr.uHue, p.hueCos, p.hueSin, p.hueAmount);
    float xy[4][2], uv[4][2];
    const float z[4] = {0, 0, 0, 0};
    rectQuad(dst, flipH, xy, uv);
    emit(xy, z, uv);
}

void FxRenderer::Pass::outline(const FxIndexTex& img, const SDL_FRect& dst, bool flipH, float alpha, const float a[3], const float b[3], float mix) {
    const Program& pr = fx_.progOutline_;
    if (!prepare(pr, img, nullptr)) return;
    Gl& gl = *fx_.gl_;
    if (pr.uAlpha >= 0) gl.Uniform1f(pr.uAlpha, alpha);
    if (pr.uOutlineA >= 0) gl.Uniform3f(pr.uOutlineA, a[0], a[1], a[2]);
    if (pr.uOutlineB >= 0) gl.Uniform3f(pr.uOutlineB, b[0], b[1], b[2]);
    if (pr.uOutlineMix >= 0) gl.Uniform1f(pr.uOutlineMix, mix);
    float xy[4][2], uv[4][2];
    const float z[4] = {0, 0, 0, 0};
    rectQuad(dst, flipH, xy, uv);
    emit(xy, z, uv);
}

void FxRenderer::Pass::shadow(const FxIndexTex& img, const FxShadowQuad& q, float fadeDist, float maxAlpha) {
    const Program& pr = fx_.progShadow_;
    if (!prepare(pr, img, nullptr)) return;
    Gl& gl = *fx_.gl_;
    // the game's value is signed (the sign says which side of the floor casts); heights above the floor are positive here
    if (pr.uFloor >= 0) gl.Uniform2f(pr.uFloor, -std::max(fadeDist, 1e-3f), maxAlpha);
    float xy[4][2], uv[4][2];
    for (int i = 0; i < 4; ++i) {
        xy[i][0] = q.x[i];
        xy[i][1] = q.y[i];
        uv[i][0] = q.u[i];
        uv[i][1] = q.v[i];
    }
    emit(xy, q.z, uv);
}

}  // namespace zengine
