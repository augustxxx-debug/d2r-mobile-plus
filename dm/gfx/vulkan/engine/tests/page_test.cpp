// 宿主页大小换算（xbg_page.h）在 4 KB 与 16 KB 页上都对；xbg_game.cpp 的 evguard / wtrack 段里不再有写死的 4 KB。
// Host page arithmetic (xbg_page.h) holds for 4 KB and 16 KB pages; the evguard / wtrack sections of xbg_game.cpp no longer hard-code 4 KB.
#include "../xbg_page.h"
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>

int main(int argc, char **argv) {
    int checks = 0; auto require = [&](bool ok, const char *why) { ++checks; if (!ok) { std::cerr << "FAIL " << why << "\n"; std::exit(1); } };
    for (size_t z : {size_t(4096), size_t(16384)}) {
        xbg_page p = xbg_page_of(z);
        require((size_t(1) << p.shift) == z, "shift matches size");
        const uintptr_t a = 0x7000001234 + 5 * 4096;   // 4 KB 对齐但不一定 16 KB 对齐 | 4 KB aligned, not necessarily 16 KB aligned
        require(xbg_page_floor(a, p) % z == 0 && xbg_page_floor(a, p) <= a && a - xbg_page_floor(a, p) < z, "floor");
        require(xbg_page_ceil(a + 1, p) % z == 0 && xbg_page_ceil(a + 1, p) >= a + 1 && xbg_page_ceil(a + 1, p) - (a + 1) < z, "ceil");
        require(xbg_page_ceil(xbg_page_floor(a, p), p) == xbg_page_floor(a, p), "ceil of an aligned address is itself");
        // wtrack 的页数：[floor(addr), ceil(addr + bytes)) / size | wtrack page count
        const uintptr_t addr = 0x7100003000; const size_t bytes = 3 << 20;
        size_t n = (xbg_page_ceil(addr + bytes, p) - xbg_page_floor(addr, p)) >> p.shift;
        require(n * z >= bytes && n * z < bytes + 2 * z, "tracked region page count");
    }
    xbg_page h = xbg_page_host(); require(h.size >= 4096 && !(h.size & (h.size - 1)), "host page size is a power of two >= 4 KB");
    if (argc > 1) {   // 源码检查：evguard / wtrack 两段 | source check of the evguard / wtrack sections
        std::ifstream f(argv[1]); std::stringstream ss; ss << f.rdbuf(); std::string s = ss.str();
        for (const char *ns : {"namespace evguard {", "namespace wtrack {"}) {
            size_t b = s.find(ns); require(b != std::string::npos, "section present");
            size_t e = s.find("\n}", b); std::string sec = s.substr(b, e - b);
            require(!std::regex_search(sec, std::regex(">>12|<<12|4095|,4096,|\\+=4096")), "no hard-coded 4 KB in the section");
        }
    }
    std::cout << "page OK (" << checks << " checks, host page " << h.size << ")\n";
    return 0;
}
