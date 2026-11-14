#include "sgscene.h"

#include "msb.h"

namespace zengine {
namespace sgscene {

namespace {
constexpr uint64_t kMaxInstances = 1u << 16;   // far above any stage; keeps a damaged count from allocating gigabytes

std::string readStr(msb::Reader& r) {
    const uint64_t n = r.u64();
    if (n > r.remaining()) throw msb::Error{msb::Error::Kind::BadLength, r.offset()};
    const std::vector<uint8_t> b = r.raw(static_cast<size_t>(n));
    return std::string(b.begin(), b.end());
}
}  // namespace

bool parse(const std::vector<uint8_t>& data, Scene& out, std::string* err) {
    auto fail = [&](const std::string& why) {
        if (err) *err = why;
        return false;
    };
    out = Scene();
    try {
        msb::Reader r(data);
        out.version = readStr(r);
        if (out.version != "2.0" && out.version != "2.1") return fail("unsupported scene version '" + out.version + "'");
        const size_t flagBytes = out.version == "2.0" ? 2 : 6;
        const uint64_t count = r.u64();
        if (count > kMaxInstances) return fail("implausible instance count");
        out.instances.reserve(static_cast<size_t>(count));
        for (uint64_t i = 0; i < count; ++i) {
            Instance in;
            in.path = readStr(r);
            in.mesh = readStr(r);
            for (float& f : in.m) f = r.f32();
            const std::vector<uint8_t> fl = r.raw(flagBytes);
            for (size_t k = 0; k < flagBytes; ++k) in.flags[k] = fl[k];
            const uint64_t anims = r.u64();
            if (anims > 64) return fail("implausible animation count");
            for (uint64_t k = 0; k < anims; ++k) {
                std::string slot = readStr(r);
                std::string name = readStr(r);
                in.anims.emplace_back(std::move(slot), std::move(name));
            }
            out.instances.push_back(std::move(in));
        }
        if (!r.eof()) return fail("bytes left after the last instance");
    } catch (const msb::Error& e) {
        return fail(std::string("truncated: ") + e.what());
    }
    return true;
}

}  // namespace sgscene
}  // namespace zengine
