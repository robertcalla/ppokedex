#include "bytes.h"

#include <cstdio>
#include <sys/stat.h>

namespace pkx {

bool readFile(const std::string& path, Bytes& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < 0) {
        std::fclose(f);
        return false;
    }
    out.resize(size_t(size));
    size_t got = size ? std::fread(out.data(), 1, out.size(), f) : 0;
    std::fclose(f);
    return got == out.size();
}

bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

} // namespace pkx
