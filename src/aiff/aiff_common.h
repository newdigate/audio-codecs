#pragma once
#include <cstdint>
#include <cstddef>

namespace audio_codecs::aiff {

constexpr uint32_t kFourCcForm = 0x464F524D; // 'FORM'
constexpr uint32_t kFourCcAiff = 0x41494646; // 'AIFF'
constexpr uint32_t kFourCcAifc = 0x41494643; // 'AIFC'
constexpr uint32_t kFourCcComm = 0x434F4D4D; // 'COMM'
constexpr uint32_t kFourCcSsnd = 0x53534E44; // 'SSND'
constexpr uint32_t kFourCcFver = 0x46564552; // 'FVER'
constexpr uint32_t kFourCcMark = 0x4D41524B; // 'MARK'
constexpr uint32_t kFourCcInst = 0x494E5354; // 'INST'
constexpr uint32_t kFourCcComt = 0x434F4D54; // 'COMT'
constexpr uint32_t kFourCcAnno = 0x414E4E4F; // 'ANNO'
constexpr uint32_t kFourCcAuth = 0x41555448; // 'AUTH'
constexpr uint32_t kFourCcCopy = 0x28632920; // '(c) '
constexpr uint32_t kFourCcName = 0x4E414D45; // 'NAME'
constexpr uint32_t kFourCcAppl = 0x4150504C; // 'APPL'
constexpr uint32_t kFourCcId3U = 0x49443320; // 'ID3 '
constexpr uint32_t kFourCcId3L = 0x69643320; // 'id3 '

constexpr uint32_t kAifcVersion1 = 0xA2805140; // May 23, 1990 14:40:00 UTC

inline uint32_t read_be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           static_cast<uint32_t>(p[3]);
}

inline uint16_t read_be16(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) |
           static_cast<uint16_t>(p[1]);
}

inline void write_be32(uint8_t* p, uint32_t val) {
    p[0] = static_cast<uint8_t>((val >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((val >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(val & 0xFF);
}

inline void write_be16(uint8_t* p, uint16_t val) {
    p[0] = static_cast<uint8_t>((val >> 8) & 0xFF);
    p[1] = static_cast<uint8_t>(val & 0xFF);
}

} // namespace audio_codecs::aiff
