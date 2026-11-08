// zdroid_dds - decode a single .dds into a .png.
//
//   zdroid_dds <file.dds> [<file.png>]   [--max 256]
//
// Pick the largest mip by default; --max N descends the chain while the level is
// still at least N pixels on its longest side (useful for previews).  This is the
// only path that exercises src/core/dds.cpp end-to-end from a real file, and it
// is also how the project produces DDS preview screenshots for the docs.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "art.h"
#include "dds.h"
#include "vfs.h"

using namespace zengine;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: zdroid_dds <file.dds> [<file.png>] [--max N]\n");
        return 2;
    }
    const std::string in = argv[1];
    std::string out = in + ".png";
    int maxSize = 0;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--max" && i + 1 < argc) maxSize = std::atoi(argv[++i]);
        else if (a[0] != '-') out = a;
    }

    std::vector<uint8_t> d;
    if (!readFile(in, d)) {
        std::printf("cannot read %s\n", in.c_str());
        return 1;
    }
    DdsImage img;
    std::string err;
    if (!decodeDds(d, img, &err, maxSize)) {
        std::printf("decode failed: %s\n", err.c_str());
        return 1;
    }
    std::printf("%s -> %s  %s %dx%d (mip downscaled to <= %d)\n", in.c_str(), out.c_str(),
                img.formatName(), img.width, img.height, maxSize);
    const std::vector<uint8_t> png = encodePngRgba(img.rgba.data(), img.width, img.height);
    if (FILE* fp = std::fopen(out.c_str(), "wb")) {
        std::fwrite(png.data(), 1, png.size(), fp);
        std::fclose(fp);
    } else {
        std::printf("cannot write %s\n", out.c_str());
        return 1;
    }
    return 0;
}
