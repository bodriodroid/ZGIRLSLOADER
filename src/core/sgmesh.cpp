#include "sgmesh.h"

#include <cstring>

#include "msb.h"

namespace zengine {
namespace sgmesh {

namespace {
constexpr const char* kStaticDecl = "float p[3],n[3],uv[2]; uchar4 c;";
constexpr const char* kSkinnedDecl = "float p[3],n[3],uv[2]; uchar4 c; uchar4 j[4],jw[4];";
constexpr uint64_t kMaxVertices = 1u << 20;   // far above anything in a stage; keeps a damaged count from allocating gigabytes

// A string of these files: an 8-byte big-endian length, then the bytes (msb::Reader::str() is the 3-byte form of the other assets).
std::string readStr(msb::Reader& r) {
    const uint64_t n = r.u64();
    if (n > r.remaining()) throw msb::Error{msb::Error::Kind::BadLength, r.offset()};
    const std::vector<uint8_t> b = r.raw(static_cast<size_t>(n));
    return std::string(b.begin(), b.end());
}
}  // namespace

bool parse(const std::vector<uint8_t>& data, Mesh& out, std::string* err) {
    auto fail = [&](const std::string& why) {
        if (err) *err = why;
        return false;
    };
    out = Mesh();
    try {
        msb::Reader r(data);
        out.version = readStr(r);
        if (out.version != "2.0") return fail("unsupported mesh version '" + out.version + "'");
        out.material = readStr(r);
        for (float& f : out.xf) f = r.f32();
        out.decl = readStr(r);
        if (out.decl == kSkinnedDecl) out.skinned = true;
        else if (out.decl != kStaticDecl) return fail("unknown vertex declaration '" + out.decl + "'");
        const uint64_t stride = r.u64(), vcount = r.u64(), tris = r.u64(), jcount = r.u64();
        if (stride != (out.skinned ? 44u : 36u)) return fail("vertex stride does not match the declaration");
        if (vcount > kMaxVertices || tris > kMaxVertices || jcount > 1024) return fail("implausible counts");
        if (!out.skinned && jcount != 0) return fail("a static mesh with joints");
        if (r.remaining() < vcount * stride + tris * 6 + 24) return fail("file shorter than its counts");
        const std::vector<uint8_t> vb = r.raw(static_cast<size_t>(vcount * stride));
        auto be32 = [](const uint8_t* q) {
            const uint32_t bits = (uint32_t(q[0]) << 24) | (uint32_t(q[1]) << 16) | (uint32_t(q[2]) << 8) | q[3];
            float f;
            std::memcpy(&f, &bits, 4);
            return f;
        };
        out.verts.resize(static_cast<size_t>(vcount));
        for (size_t k = 0; k < out.verts.size(); ++k) {
            const uint8_t* q = vb.data() + k * static_cast<size_t>(stride);
            Vertex& v = out.verts[k];
            for (int i = 0; i < 3; ++i) v.p[i] = be32(q + 4 * i);
            for (int i = 0; i < 3; ++i) v.n[i] = be32(q + 12 + 4 * i);
            for (int i = 0; i < 2; ++i) v.uv[i] = be32(q + 24 + 4 * i);
            std::memcpy(v.c, q + 32, 4);
            if (out.skinned) {
                std::memcpy(v.j, q + 36, 4);
                std::memcpy(v.jw, q + 40, 4);
            }
        }
        const std::vector<uint8_t> ib = r.raw(static_cast<size_t>(tris * 6));
        out.idx.resize(static_cast<size_t>(tris * 3));
        for (size_t k = 0; k < out.idx.size(); ++k) {
            out.idx[k] = static_cast<uint16_t>((ib[2 * k] << 8) | ib[2 * k + 1]);
            if (out.idx[k] >= vcount) return fail("an index points past the vertex list");
        }
        for (float& f : out.bbMin) f = r.f32();
        for (float& f : out.bbMax) f = r.f32();
        out.joints.resize(static_cast<size_t>(jcount));
        for (Joint& j : out.joints) j.path = readStr(r);
        for (Joint& j : out.joints)
            for (float& f : j.invBind) f = r.f32();
        if (!r.eof()) return fail("bytes left after the last joint");
        if (out.skinned)
            for (const Vertex& v : out.verts)
                for (int k = 0; k < 4; ++k)
                    if (v.jw[k] && v.j[k] >= out.joints.size()) return fail("a vertex weights a joint that does not exist");
    } catch (const msb::Error& e) {
        return fail(std::string("truncated: ") + e.what());
    }
    return true;
}

}  // namespace sgmesh
}  // namespace zengine
