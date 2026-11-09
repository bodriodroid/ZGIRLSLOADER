// fuzz_formats.cpp - feeds arbitrary bytes to every parser that reads files the user (or a mod) provides.
//
// Two ways to run it:
//   * libFuzzer (clang):  cmake -DCMAKE_CXX_COMPILER=clang++ -DZDROID_FUZZ=ON ... && ./zdroid_fuzz corpus/ -max_total_time=60
//   * plain smoke (default, runs in ctest): a deterministic stream of random and mutated inputs; any crash / sanitizer report fails it.
// Nothing here needs game data: the seeds below are tiny hand-written files.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "dds.h"
#include "foit.h"
#include "gbs.h"
#include "imgscale.h"
#include "level.h"
#include "menu_data.h"
#include "palmod.h"
#include "savini.h"
#include "fx_tuning.h"
#include "pafx.h"

using namespace zengine;

static void feed(const uint8_t* data, size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    std::string err;
    {   // text formats
        (void)parseLevel(text, "fuzz");
        (void)parseMenuIni(text);
        SavIni ini;
        ini.setAutosave(false);
        ini.parse(text);
        (void)ini.toText();
    }
    {   // binary formats
        foit::Character ch;
        (void)foit::parse(data, size, ch, &err);
        Gbs g;
        (void)parseGbs(data, size, g, &err);
        palmod::PalFile pf;
        (void)palmod::parse(data, size, pf, &err);
        DdsImage img;
        (void)decodeDds(std::vector<uint8_t>(data, data + size), img, &err);
    }
    {   // PAFX: arbitrary bytes are refused cleanly (the tag cannot match), tuning text never crashes the readers, and a pack built
        // from arbitrary entries opens and reads back exactly what went in
        fx::AeadKey key{};
        key[0] = 1;
        fx::PafxArchive a;
        (void)a.open(data, size, key, &err);
        fx::FxTuning t;
        (void)fx::tuningFromIni(std::string(text), t);
        (void)fx::extractTuning(std::string(text), std::string(text), std::string(text), t);
        if (size >= 2) {
            const size_t nameLen = 1 + data[0] % std::min<size_t>(size, 12);
            const std::string name(reinterpret_cast<const char*>(data) + 1, std::min(nameLen, size - 1));
            fx::PafxWriter w;
            w.add(name, fx::PafxKind::Raw, std::vector<uint8_t>(data, data + size));
            std::array<uint8_t, 16> id{};
            for (size_t i = 0; i < id.size(); ++i) id[i] = data[i % size];
            std::vector<uint8_t> blob;
            if (w.build(key, id, blob)) {
                fx::PafxArchive b;
                std::vector<uint8_t> back;
                if (!b.open(blob.data(), blob.size(), key, &err) || !b.read(name, back, &err) || back.size() != size) __builtin_trap();
            }
        }
    }
    if (size >= 4) {   // image maths on whatever shape the bytes give
        const int w = 1 + data[0] % 9, h = 1 + data[1] % 9;
        std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
        for (size_t i = 0; i < px.size(); ++i) px[i] = data[(2 + i) % size];
        int ow = 0, oh = 0;
        (void)shrinkRgba(px.data(), w, h, 1 + data[2] % 5, ow, oh);
        (void)upscaleCubic2x(px.data(), w, h);
        sharpenRgba(px.data(), w, h, 0.5f);
        bleedTransparent(px.data(), w, h, 2);
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > (1u << 20)) return 0;
    feed(data, size);
    return 0;
}

#ifndef ZDROID_LIBFUZZER
// plain driver: seeds + random bytes + single-byte mutations of the seeds
#include <cstdio>
int main(int argc, char** argv) {
    const int iterations = argc > 1 ? std::atoi(argv[1]) : 6000;
    static const char* seeds[] = {
        "StageSize: 3750 2000\nStart1: 1675\nStart2: 2075\nShadowDist: 400\nLight: Amb 85 96 122\nCAMERA 66\n2D stages/textures/2D/stage_x.dds\n",
        "TopText: Settings\nOption:\nType: Setting\nText: Rounds\nChoice: 1\nDefaultChoice: 3\nChoice: 5\nKeyword: OPT_INT_NUM_ROUNDS\n",
        "[options]\nZD_DEV_RES=Native\n[progression]\nStoryFilia=C\n",
        "DDS \x7c\x00\x00\x00\x07\x10\x08\x00\x04\x00\x00\x00\x04\x00\x00\x00",
        "\x00\x01\x02,,,:::\n",
    };
    uint32_t rs = 0xC0FFEEu;
    auto next = [&]() { rs = rs * 1664525u + 1013904223u; return rs >> 8; };
    for (const char* s : seeds) feed(reinterpret_cast<const uint8_t*>(s), std::string(s).size());
    for (int i = 0; i < iterations; ++i) {
        std::vector<uint8_t> buf;
        if (i % 3 == 0) {   // pure noise
            buf.resize(next() % 600);
            for (uint8_t& b : buf) b = static_cast<uint8_t>(next());
        } else {            // a seed with a few bytes flipped / cut
            const std::string s = seeds[next() % (sizeof(seeds) / sizeof(seeds[0]))];
            buf.assign(s.begin(), s.end());
            for (int k = 0, n = 1 + static_cast<int>(next() % 4); k < n && !buf.empty(); ++k) buf[next() % buf.size()] = static_cast<uint8_t>(next());
            if (next() % 4 == 0 && !buf.empty()) buf.resize(next() % buf.size());
        }
        feed(buf.data(), buf.size());
    }
    std::printf("fuzz smoke: %d inputs, no crash\n", iterations);
    return 0;
}
#endif
