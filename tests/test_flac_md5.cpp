// tests/test_flac_md5.cpp
#include "src/flac/md5.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    using namespace audio_codecs::flac;

    Md5Context md5;
    md5.init();
    const char* str = "The quick brown fox jumps over the lazy dog";
    md5.update(reinterpret_cast<const uint8_t*>(str), std::strlen(str));
    uint8_t digest[16];
    md5.finish(digest);

    // Expected MD5: 9e107d9d372bb6826bd81d3542a419d6
    assert(digest[0] == 0x9e && digest[1] == 0x10);
    assert(digest[2] == 0x7d && digest[3] == 0x9d);
    assert(digest[4] == 0x37 && digest[5] == 0x2b);
    assert(digest[6] == 0xb6 && digest[7] == 0x82);
    assert(digest[8] == 0x6b && digest[9] == 0xd8);
    assert(digest[10] == 0x1d && digest[11] == 0x35);
    assert(digest[12] == 0x42 && digest[13] == 0xa4);
    assert(digest[14] == 0x19 && digest[15] == 0xd6);

    std::cout << "FLAC MD5 tests passed!\n";
    return 0;
}
