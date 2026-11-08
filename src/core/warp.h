// warp.h - re::warp skeleton / animation assets (*.sgs.msb, *.sga.msb).
//
// Layout recovered from SkullGirls.exe + real Skullgirls 2nd Encore data
// (docs/BINARY_RE.md).  Everything is MSB (big-endian, 8-byte-widened integers).
//
//   .sgs.msb (sg::read_skeleton -> re::warp::skeleton)
//       str   format version, e.g. "2.0"
//       u64   joint count
//       per joint:
//           str   joint name
//           i32   parent joint index, -1 for the root
//           f32x16 bind matrix (float44, row-major, big-endian)
//           ... trailing per-joint state attributes (present in some builds)
//
//   .sga.msb (sg::read_animation -> sg::anm)
//       str   format version, e.g. "3.0"
//       ...   header scalars
//       per track:
//           str   track name (e.g. "uv_track")
//           f32[] keyframe payload (big-endian)
//
// The trailing-attribute bytes differ between builds, so parsing is deliberately
// forgiving: joints are located by scanning for length-prefixed strings and each
// joint's matrix is clipped to whatever room is left before the next one.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "msb.h"

namespace zengine {
namespace warp {

struct Joint {
    std::string name;
    int32_t parent = -1;
    std::size_t offset = 0;      // file offset of the name's length prefix
    std::size_t payloadBytes = 0;  // bytes between this joint's name and the next
    std::vector<float> matrix;      // up to 16 floats (float44 bind matrix, row-major)
    std::vector<uint8_t> extra;     // undecoded build-specific trailing bytes
};

struct Skeleton {
    std::string version;
    uint64_t declaredJoints = 0;
    std::vector<Joint> joints;
    std::size_t fileSize = 0;
    bool ok = false;
    std::string error;
};

struct Track {
    std::string name;
    std::size_t offset = 0;
    std::vector<float> keys;
};

struct Animation {
    std::string version;
    std::vector<Track> tracks;
    std::size_t fileSize = 0;
    bool ok = false;
    std::string error;
};

// Locate every plausible length-prefixed string in an msb blob.  Used both by the
// parsers and by the inspection tool.
struct StringHit {
    std::size_t offset;  // offset of the 8-byte length prefix
    std::size_t data;    // offset of the first character
    std::string value;
};
std::vector<StringHit> scanStrings(const std::vector<uint8_t>& d, std::size_t maxLen = 96);

Skeleton readSkeleton(const std::vector<uint8_t>& d);
Animation readAnimation(const std::vector<uint8_t>& d);

// Mesh sidecar (*.sgm.msb) shares the same primitives; this extracts the header
// strings and the vertex/face counts so the viewer can report on them.
struct MeshInfo {
    std::string version;
    std::vector<std::string> names;
    std::size_t fileSize = 0;
    bool ok = false;
    std::string error;
};
MeshInfo readMeshInfo(const std::vector<uint8_t>& d);

}  // namespace warp
}  // namespace zengine
