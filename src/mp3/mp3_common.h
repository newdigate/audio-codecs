#pragma once
#include <cstddef>
#include <cstdint>

namespace audio_codecs::mp3 {

enum class MpegVersion : uint8_t {
    Mpeg25 = 0,
    Reserved = 1,
    Mpeg2 = 2,
    Mpeg1 = 3
};

enum class MpegLayer : uint8_t {
    Reserved = 0,
    Layer3 = 1,
    Layer2 = 2,
    Layer1 = 3
};

enum class MpegMode : uint8_t {
    Stereo = 0,
    JointStereo = 1,
    DualChannel = 2,
    SingleChannel = 3
};

enum class MpegEmphasis : uint8_t {
    None = 0,
    Microsec_50_15 = 1,
    Reserved = 2,
    CCITT_J17 = 3
};

struct FrameHeader {
    MpegVersion version{MpegVersion::Mpeg1};
    MpegLayer layer{MpegLayer::Layer3};
    bool protection_bit{true}; // 1 = no CRC, 0 = CRC present (16-bit)
    uint8_t bitrate_index{0};
    uint32_t bitrate_kbps{0};
    uint8_t sampling_frequency{0};
    uint32_t sample_rate{0};
    bool padding_bit{false};
    bool private_bit{false};
    MpegMode mode{MpegMode::Stereo};
    uint8_t mode_extension{0};
    bool intensity_stereo{false};
    bool ms_stereo{false};
    bool copyright{false};
    bool original{true};
    MpegEmphasis emphasis{MpegEmphasis::None};

    uint8_t channels{2};
    uint8_t ngr{2};             // 2 for MPEG1, 1 for MPEG2/2.5
    size_t frame_bytes{0};
    size_t side_info_bytes{0};  // 32/17 for MPEG1, 17/9 for MPEG2
};

struct GranuleChannelInfo {
    uint16_t part2_3_length{0};
    uint16_t big_values{0};
    uint8_t global_gain{0};
    uint16_t scalefac_compress{0};
    bool window_switching_flag{false};
    uint8_t block_type{0};       // 0: normal, 1: start, 2: short, 3: stop
    bool mixed_block_flag{false};
    uint8_t table_select[3]{0, 0, 0};
    uint8_t subblock_gain[3]{0, 0, 0};
    uint8_t region0_count{0};
    uint8_t region1_count{0};
    bool preflag{false};
    bool scalefac_scale{false};
    bool count1table_select{false};
};

struct SideInfo {
    uint16_t main_data_begin{0};
    uint8_t private_bits{0};
    uint8_t scfsi[2][4]{{0}}; // [ch][scfsi_band]
    GranuleChannelInfo gr[2][2]; // [gr][ch]
};

struct ScalefactorData {
    int16_t l[23]{0};       // scalefactor bands for long blocks (0..21 + sentinel)
    int16_t s[14][3]{{0}};  // scalefactor bands for short blocks (0..12, 3 windows)
};

// Parse 32-bit big-endian frame header word
bool parse_frame_header(uint32_t header_word, FrameHeader& out);

// Build 32-bit big-endian frame header word
bool build_frame_header_word(const FrameHeader& header, uint32_t& out);

} // namespace audio_codecs::mp3
