// zdroid_gfs - lists or unpacks the game's .gfs packages (core/gfs.h).
//   zdroid_gfs list <file.gfs>
//   zdroid_gfs unpack <file.gfs> <out dir>        (an existing file is overwritten)
#include <cstdio>
#include <cstring>
#include <string>

#include "gfs.h"

int main(int argc, char** argv) {
    using namespace zengine;
    if (argc >= 3 && std::strcmp(argv[1], "list") == 0) {
        gfs::Index idx;
        std::string err;
        if (!gfs::readIndex(argv[2], idx, &err)) {
            std::fprintf(stderr, "%s: %s\n", argv[2], err.c_str());
            return 1;
        }
        for (const gfs::Entry& e : idx.entries) std::printf("%12llu  %s\n", static_cast<unsigned long long>(e.size), e.path.c_str());
        std::printf("%zu files, format %s\n", idx.entries.size(), idx.version.c_str());
        return 0;
    }
    if (argc >= 4 && std::strcmp(argv[1], "unpack") == 0) {
        gfs::ExtractStats st;
        std::string err;
        const bool ok = gfs::extract(argv[2], argv[3], st, [](size_t d, size_t t) {
            if (d % 200 == 0 || d == t) std::printf("\r%zu / %zu", d, t), std::fflush(stdout);
            return true;
        }, true, &err);
        std::printf("\n");
        if (!ok) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        for (const std::string& p : st.problems) std::fprintf(stderr, "  ! %s\n", p.c_str());
        std::printf("%zu files written (%llu bytes), %zu problems\n", st.files, static_cast<unsigned long long>(st.bytes), st.problems.size());
        return st.problems.empty() ? 0 : 2;
    }
    std::fprintf(stderr, "usage: zdroid_gfs list <file.gfs> | unpack <file.gfs> <out dir>\n");
    return 1;
}
