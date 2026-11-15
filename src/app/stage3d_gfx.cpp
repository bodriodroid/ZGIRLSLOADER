#include "stage3d_gfx.h"

#include <algorithm>
#include <cstdio>

#include "imgscale.h"
#include "vfs.h"

namespace zengine {

namespace {

std::string lowerCopy(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return s;
}

}  // namespace

bool Stage3DGfx::loadCpu(const GameData& gd, const std::string& levelName, int maxTex, Cpu& out, std::string* err) {
    out = Cpu();
    auto fail = [&](const std::string& why) {
        if (err) *err = why;
        return false;
    };
    const Vfs& vfs = gd.vfs();
    // "<level>/background.sgi.msb": every file of the stage folder is found by its folder + name (the Vfs indexes every path suffix)
    auto read = [&](const std::string& file, std::vector<uint8_t>& bytes) { return vfs.read(levelName + "/" + file, bytes); };
    std::string e;
    if (!stage3d::load(read, out.stage, &e)) return fail(e);

    // textures: "<material>.dds" wherever the data keeps it (stages/textures/ ...)
    size_t texturedTris = 0;
    const size_t totalTris = std::max<size_t>(1, out.stage.triangles());
    std::vector<std::string> have;
    for (const std::string& mat : out.stage.materials) {
        std::string real;
        std::vector<std::string> found = vfs.findByName(mat + ".dds");
        if (found.empty()) {
            out.noTexture.push_back(mat);
            continue;
        }
        // prefer a stage texture folder over e.g. a character's file of the same name
        std::sort(found.begin(), found.end(), [](const std::string& a, const std::string& b) {
            const bool sa = lowerCopy(a).find("stages") != std::string::npos, sb = lowerCopy(b).find("stages") != std::string::npos;
            if (sa != sb) return sa;
            return a.size() < b.size();
        });
        std::vector<uint8_t> data;
        DdsImage img;
        std::string de;
        if (!readFile(found[0], data) || !decodeDds(data, img, &de, maxTex) || img.rgba.empty()) {
            out.noTexture.push_back(mat);
            continue;
        }
        Cpu::Tex t;
        t.name = mat;
        t.img = std::move(img);
        out.tex.push_back(std::move(t));
        have.push_back(mat);
    }
    // Memory: a phone does not need 2048 x 2048 atlases times twenty.  Over the budget, the largest textures are halved until it fits.
    {
        constexpr size_t kBudget = 160u * 1024u * 1024u;
        auto total = [&]() {
            size_t n = 0;
            for (const Cpu::Tex& t : out.tex) n += static_cast<size_t>(t.img.width) * static_cast<size_t>(t.img.height) * 4;
            return n;
        };
        size_t bytes = total();
        while (bytes > kBudget) {
            Cpu::Tex* big = nullptr;
            for (Cpu::Tex& t : out.tex)
                if (std::max(t.img.width, t.img.height) > 256 && (!big || t.img.width * t.img.height > big->img.width * big->img.height)) big = &t;
            if (!big) break;
            int sw = 0, sh = 0;
            std::vector<uint8_t> px = shrinkRgba(big->img.rgba.data(), big->img.width, big->img.height, 2, sw, sh);
            if (px.empty()) break;
            big->img.rgba = std::move(px);
            big->img.width = sw;
            big->img.height = sh;
            bytes = total();
        }
    }
    for (const stage3d::Piece& p : out.stage.pieces)
        if (std::find(have.begin(), have.end(), p.material) != have.end()) texturedTris += p.idx.size() / 3;
    char buf[200];
    std::snprintf(buf, sizeof buf, "%zu meshes, %zu triangles, %zu/%zu textures (%zu%% of the triangles have one)", out.stage.pieces.size(), out.stage.triangles(),
                  have.size(), out.stage.materials.size(), texturedTris * 100 / totalTris);
    out.note = buf;
    if (!out.stage.havePlane) return fail("the scene has no FightPlane");
    // Mostly untextured means the data player has does not include this stage's textures: a half-drawn 3D stage is worse than the picture.
    if (texturedTris * 100 < totalTris * 60) return fail(out.note);
    return true;
}

void Stage3DGfx::reset(SDL_Renderer* r) {
    if (r)
        for (SDL_Texture* t : tex_)
            if (t) SDL_DestroyTexture(t);
    tex_.clear();
    pieceTex_.clear();
    stage_ = stage3d::Stage();
    usable_ = false;
    frame_ = stage3d::Frame();
    drawMs_ = 0;
    lastTris_ = 0;
    note_.clear();
}

bool Stage3DGfx::upload(SDL_Renderer* r, Cpu& cpu, bool smooth) {
    reset(r);
    if (!r) return false;
    stage_ = std::move(cpu.stage);
    tex_.assign(stage_.materials.size(), nullptr);
    for (Cpu::Tex& t : cpu.tex) {
        const auto it = std::find(stage_.materials.begin(), stage_.materials.end(), t.name);
        if (it == stage_.materials.end()) continue;
        const size_t slot = static_cast<size_t>(it - stage_.materials.begin());
        SDL_Texture* tx = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, t.img.width, t.img.height);
        if (tx) {
            SDL_UpdateTexture(tx, nullptr, t.img.rgba.data(), t.img.width * 4);
        } else {   // larger than the GPU allows: half, quarter size
            for (int f = 2; f <= 8 && !tx; f *= 2) {
                int sw = 0, sh = 0;
                const std::vector<uint8_t> px = shrinkRgba(t.img.rgba.data(), t.img.width, t.img.height, f, sw, sh);
                if (px.empty()) break;
                tx = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, sw, sh);
                if (tx) SDL_UpdateTexture(tx, nullptr, px.data(), sw * 4);
            }
        }
        if (!tx) continue;
        SDL_SetTextureBlendMode(tx, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tx, smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
        t.img = DdsImage{};   // the pixels live on the GPU now
        tex_[slot] = tx;
    }
    pieceTex_.assign(stage_.pieces.size(), -1);
    for (size_t i = 0; i < stage_.pieces.size(); ++i) {
        const auto it = std::find(stage_.materials.begin(), stage_.materials.end(), stage_.pieces[i].material);
        if (it == stage_.materials.end()) continue;
        const size_t slot = static_cast<size_t>(it - stage_.materials.begin());
        if (tex_[slot]) pieceTex_[i] = static_cast<int>(slot);
    }
    note_ = cpu.note;
    usable_ = stage_.havePlane && std::any_of(pieceTex_.begin(), pieceTex_.end(), [](int t) { return t >= 0; });
    return usable_;
}

int Stage3DGfx::draw(SDL_Renderer* r, const stage3d::Fit& fit, double shakeX, double shakeY, double timeSec) {
    if (!usable_ || !r) return 0;
    const Uint64 t0 = SDL_GetPerformanceCounter();
    const stage3d::Cam cam = stage3d::fitCamera(stage_, fit);
    stage3d::Options opt;
    opt.offsetX = shakeX;
    opt.offsetY = shakeY;
    opt.time = timeSec;
    stage3d::render(stage_, cam, opt, frame_);
    int drawn = 0;
    for (const stage3d::Batch& b : frame_.batches) {
        const int slot = pieceTex_[static_cast<size_t>(b.piece)];
        if (slot < 0) continue;   // no texture for this mesh in the data
        verts_.clear();
        verts_.reserve(b.tris.size() * 3);
        for (const stage3d::Tri& t : b.tris) {
            for (int k = 0; k < 3; ++k) {
                SDL_Vertex v;
                v.position.x = t.x[k];
                v.position.y = t.y[k];
                v.color = SDL_Color{t.c[k][0], t.c[k][1], t.c[k][2], t.c[k][3]};
                v.tex_coord.x = std::clamp(t.u[k], 0.0f, 1.0f);
                v.tex_coord.y = std::clamp(1.0f - t.v[k], 0.0f, 1.0f);   // the meshes store v upward, the pictures are stored top-down
                verts_.push_back(v);
            }
        }
        if (verts_.empty()) continue;
        // (the software renderer refuses a whole batch when one coordinate is a hair outside 0..1, so they are clamped here)
        if (SDL_RenderGeometry(r, tex_[static_cast<size_t>(slot)], verts_.data(), static_cast<int>(verts_.size()), nullptr, 0) != 0) {
            if (!geometryWarned_) std::printf("[play] stage 3D: SDL_RenderGeometry: %s\n", SDL_GetError());
            geometryWarned_ = true;
            continue;
        }
        drawn += static_cast<int>(b.tris.size());
    }
    const double ms = 1000.0 * static_cast<double>(SDL_GetPerformanceCounter() - t0) / static_cast<double>(SDL_GetPerformanceFrequency());
    drawMs_ = drawMs_ <= 0 ? ms : drawMs_ * 0.9 + ms * 0.1;
    lastTris_ = static_cast<size_t>(drawn);
    return drawn;
}

}  // namespace zengine
