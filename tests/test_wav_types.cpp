#include "audio_codecs/wav/wav_types.h"
#include "src/wav/wav_common.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    using namespace audio_codecs::wav;

    // WavFormat enum values
    assert(static_cast<uint16_t>(WavFormat::Pcm) == 0x0001);
    assert(static_cast<uint16_t>(WavFormat::IeeeFloat) == 0x0003);
    assert(static_cast<uint16_t>(WavFormat::ALaw) == 0x0006);
    assert(static_cast<uint16_t>(WavFormat::MuLaw) == 0x0007);
    assert(static_cast<uint16_t>(WavFormat::Extensible) == 0xFFFE);

    // WavSampleFormat enum check
    auto s1 = WavSampleFormat::Uint8;
    auto s2 = WavSampleFormat::Int16LE;
    auto s3 = WavSampleFormat::Int24LE;
    auto s4 = WavSampleFormat::Int32LE;
    auto s5 = WavSampleFormat::Float32LE;
    auto s6 = WavSampleFormat::ALaw8;
    auto s7 = WavSampleFormat::MuLaw8;
    (void)s1; (void)s2; (void)s3; (void)s4; (void)s5; (void)s6; (void)s7;

    // SpeakerMask bit positions
    assert(SpeakerMask::FrontLeft == 0x00000001);
    assert(SpeakerMask::FrontRight == 0x00000002);
    assert(SpeakerMask::FrontCenter == 0x00000004);
    assert(SpeakerMask::LowFrequency == 0x00000008);
    assert(SpeakerMask::BackLeft == 0x00000010);
    assert(SpeakerMask::BackRight == 0x00000020);
    assert(SpeakerMask::FrontLeftOfCenter == 0x00000040);
    assert(SpeakerMask::FrontRightOfCenter == 0x00000080);
    assert(SpeakerMask::BackCenter == 0x00000100);
    assert(SpeakerMask::SideLeft == 0x00000200);
    assert(SpeakerMask::SideRight == 0x00000400);
    assert(SpeakerMask::StereoMask == 0x00000003);
    assert(SpeakerMask::Surround51Mask == 0x0000003F);

    // WavEncoderConfig default values
    WavEncoderConfig cfg;
    assert(cfg.sample_format == WavSampleFormat::Int16LE);
    assert(cfg.core_config.sample_rate == 44100);
    assert(cfg.core_config.channels == 2);
    assert(cfg.channel_mask == 0);
    assert(!cfg.use_extensible);

    // FourCC constants in little-endian order
    assert(kFourCcRiff == 0x46464952); // "RIFF" in Little-Endian
    assert(kFourCcRf64 == 0x34364652); // "RF64" in Little-Endian
    assert(kFourCcWave == 0x45564157); // "WAVE" in Little-Endian
    assert(kFourCcFmt  == 0x20746D66); // "fmt " in Little-Endian
    assert(kFourCcData == 0x61746164); // "data" in Little-Endian
    assert(kFourCcFact == 0x74636166); // "fact" in Little-Endian
    assert(kFourCcList == 0x5453494C); // "LIST" in Little-Endian
    assert(kFourCcBext == 0x74786562); // "bext" in Little-Endian
    assert(kFourCcJunk == 0x4B4E554A); // "JUNK" in Little-Endian
    assert(kFourCcPad  == 0x20444150); // "PAD " in Little-Endian

    // Subformat GUIDs
    assert(kGuidPcm[0] == 0x01 && kGuidPcm[15] == 0x71);
    assert(kGuidIeeeFloat[0] == 0x03 && kGuidIeeeFloat[15] == 0x71);

    const uint8_t expected_pcm_guid[16] = {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
        0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
    };
    assert(std::memcmp(kGuidPcm, expected_pcm_guid, 16) == 0);

    const uint8_t expected_float_guid[16] = {
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
        0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
    };
    assert(std::memcmp(kGuidIeeeFloat, expected_float_guid, 16) == 0);

    std::cout << "WAV types test passed!\n";
    return 0;
}
