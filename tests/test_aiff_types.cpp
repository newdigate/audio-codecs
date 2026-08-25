// tests/test_aiff_types.cpp
#include "audio_codecs/aiff/aiff_types.h"
#include "src/aiff/aiff_common.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    using namespace audio_codecs::aiff;

    assert(static_cast<uint32_t>(AiffFormType::Aiff) == 0x41494646); // 'AIFF'
    assert(static_cast<uint32_t>(AiffFormType::Aifc) == 0x41494643); // 'AIFC'

    assert(static_cast<uint32_t>(AiffCompressionType::None) == 0x4E4F4E45); // 'NONE'
    assert(static_cast<uint32_t>(AiffCompressionType::Sowt) == 0x736F7774); // 'sowt'
    assert(static_cast<uint32_t>(AiffCompressionType::Fl32) == 0x666C3332); // 'fl32'
    assert(static_cast<uint32_t>(AiffCompressionType::ALaw) == 0x616C6177); // 'alaw'
    assert(static_cast<uint32_t>(AiffCompressionType::MuLaw) == 0x756C6177); // 'ulaw'

    AiffEncoderConfig cfg;
    assert(cfg.sample_format == AiffSampleFormat::Int16BE);
    assert(cfg.core_config.sample_rate == 44100);
    assert(cfg.core_config.channels == 2);
    assert(cfg.form_type == AiffFormType::Aiff);
    assert(cfg.compression_type == AiffCompressionType::None);

    assert(kFourCcForm == 0x464F524D); // 'FORM' Big-Endian
    assert(kFourCcAiff == 0x41494646); // 'AIFF' Big-Endian
    assert(kFourCcAifc == 0x41494643); // 'AIFC' Big-Endian
    assert(kFourCcComm == 0x434F4D4D); // 'COMM' Big-Endian
    assert(kFourCcSsnd == 0x53534E44); // 'SSND' Big-Endian
    assert(kFourCcFver == 0x46564552); // 'FVER' Big-Endian
    assert(kAifcVersion1 == 0xA2805140);

    // Endianness helper tests
    uint8_t buf4[4];
    write_be32(buf4, 0x12345678);
    assert(buf4[0] == 0x12 && buf4[1] == 0x34 && buf4[2] == 0x56 && buf4[3] == 0x78);
    assert(read_be32(buf4) == 0x12345678);

    uint8_t buf2[2];
    write_be16(buf2, 0xABCD);
    assert(buf2[0] == 0xAB && buf2[1] == 0xCD);
    assert(read_be16(buf2) == 0xABCD);

    std::cout << "AIFF types test passed!\n";
    return 0;
}
