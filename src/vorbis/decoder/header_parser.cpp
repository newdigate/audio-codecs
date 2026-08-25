#include "src/vorbis/decoder/header_parser.h"
#include "src/vorbis/vorbis_common.h"
#include <cstring>

namespace audio_codecs::vorbis {

namespace {

const uint8_t kVorbisMagic[6] = {'v', 'o', 'r', 'b', 'i', 's'};

} // namespace

bool is_vorbis_header(const uint8_t* in_packet, size_t in_bytes, uint8_t expected_type) {
    if (!in_packet || in_bytes < 7) return false;
    if (in_packet[0] != expected_type) return false;
    return std::memcmp(in_packet + 1, kVorbisMagic, 6) == 0;
}

bool parse_vorbis_id_header(const uint8_t* in_packet, size_t in_bytes, VorbisInfo& info) {
    if (!is_vorbis_header(in_packet, in_bytes, VORBIS_PACKET_ID) || in_bytes < 30) {
        return false;
    }

    VorbisBitReader reader(in_packet, in_bytes);
    reader.skip_bits(7 * 8); // Skip 0x01 + "vorbis"

    uint32_t version = 0;
    if (!reader.read_bits(32, version) || version != 0) return false;

    uint32_t channels = 0, sample_rate = 0;
    if (!reader.read_bits(8, channels) || channels == 0 || channels > VORBIS_MAX_CHANNELS) return false;
    if (!reader.read_bits(32, sample_rate) || sample_rate == 0) return false;

    uint32_t br_max = 0, br_nom = 0, br_min = 0;
    if (!reader.read_bits(32, br_max) || !reader.read_bits(32, br_nom) || !reader.read_bits(32, br_min)) {
        return false;
    }

    uint32_t bs0_exp = 0, bs1_exp = 0;
    if (!reader.read_bits(4, bs0_exp) || !reader.read_bits(4, bs1_exp)) return false;
    if (bs0_exp < 6 || bs0_exp > 13 || bs1_exp < bs0_exp || bs1_exp > 13) return false;

    uint32_t framing = 0;
    if (!reader.read_bits(1, framing) || framing != 1) return false;

    info.channels = static_cast<uint8_t>(channels);
    info.sample_rate = sample_rate;
    info.bitrate_maximum = static_cast<int32_t>(br_max);
    info.bitrate_nominal = static_cast<int32_t>(br_nom);
    info.bitrate_minimum = static_cast<int32_t>(br_min);
    info.blocksize_0 = 1U << bs0_exp;
    info.blocksize_1 = 1U << bs1_exp;

    return true;
}

bool parse_vorbis_comment_header(const uint8_t* in_packet, size_t in_bytes, VorbisComment& comment) {
    if (!is_vorbis_header(in_packet, in_bytes, VORBIS_PACKET_COMMENT) || in_bytes < 15) {
        return false;
    }

    VorbisBitReader reader(in_packet, in_bytes);
    reader.skip_bits(7 * 8); // Skip 0x03 + "vorbis"

    uint32_t vendor_len = 0;
    if (!reader.read_bits(32, vendor_len)) return false;
    if (vendor_len > 1024) vendor_len = 1024;

    comment.vendor.resize(vendor_len);
    for (size_t i = 0; i < vendor_len; ++i) {
        uint32_t c = 0;
        if (!reader.read_bits(8, c)) return false;
        comment.vendor[i] = static_cast<char>(c);
    }

    uint32_t comment_count = 0;
    if (!reader.read_bits(32, comment_count)) return false;
    if (comment_count > 256) comment_count = 256;

    comment.comments.clear();
    for (size_t i = 0; i < comment_count; ++i) {
        uint32_t len = 0;
        if (!reader.read_bits(32, len)) return false;
        if (len > 4096) len = 4096;
        std::string tag(len, '\0');
        for (size_t j = 0; j < len; ++j) {
            uint32_t c = 0;
            if (!reader.read_bits(8, c)) return false;
            tag[j] = static_cast<char>(c);
        }
        comment.comments.push_back(tag);
    }

    uint32_t framing = 0;
    if (!reader.read_bits(1, framing) || framing != 1) return false;

    return true;
}

bool parse_vorbis_setup_header(const uint8_t* in_packet, size_t in_bytes, 
                               uint8_t channels, VorbisSetup& setup) {
    if (!is_vorbis_header(in_packet, in_bytes, VORBIS_PACKET_SETUP) || in_bytes < 16) {
        return false;
    }

    VorbisBitReader reader(in_packet, in_bytes);
    reader.skip_bits(7 * 8); // Skip 0x05 + "vorbis"

    // 1. Codebooks
    uint32_t codebook_count = 0;
    if (!reader.read_bits(8, codebook_count)) return false;
    setup.codebook_count = codebook_count + 1;
    if (setup.codebook_count > VORBIS_MAX_CODEBOOKS) return false;

    for (size_t i = 0; i < setup.codebook_count; ++i) {
        if (!vorbis_codebook_unpack(reader, setup.codebooks[i])) {
            return false;
        }
    }

    // 2. Time domain transforms (must be 0 in Vorbis I)
    uint32_t time_count = 0;
    if (!reader.read_bits(6, time_count)) return false;
    time_count += 1;
    for (size_t i = 0; i < time_count; ++i) {
        uint32_t val = 0;
        if (!reader.read_bits(16, val) || val != 0) return false;
    }

    // 3. Floors
    uint32_t floor_count = 0;
    if (!reader.read_bits(6, floor_count)) return false;
    setup.floor_count = floor_count + 1;
    if (setup.floor_count > VORBIS_MAX_FLOORS) return false;

    for (size_t i = 0; i < setup.floor_count; ++i) {
        uint32_t ftype = 0;
        if (!reader.read_bits(16, ftype)) return false;
        if (ftype == 1) {
            if (!vorbis_floor1_unpack(reader, setup.floors[i])) {
                return false;
            }
        } else {
            return false; // Unsupported floor type
        }
    }

    // 4. Residues
    uint32_t residue_count = 0;
    if (!reader.read_bits(6, residue_count)) return false;
    setup.residue_count = residue_count + 1;
    if (setup.residue_count > VORBIS_MAX_RESIDUES) return false;

    for (size_t i = 0; i < setup.residue_count; ++i) {
        if (!vorbis_residue_unpack(reader, setup.residues[i])) {
            return false;
        }
    }

    // 5. Mappings
    uint32_t mapping_count = 0;
    if (!reader.read_bits(6, mapping_count)) return false;
    setup.mapping_count = mapping_count + 1;
    if (setup.mapping_count > VORBIS_MAX_MAPPINGS) return false;

    for (size_t i = 0; i < setup.mapping_count; ++i) {
        uint32_t mtype = 0;
        if (!reader.read_bits(16, mtype) || mtype != 0) return false;
        if (!vorbis_mapping_unpack(reader, setup.mappings[i], channels)) {
            return false;
        }
    }

    // 6. Modes
    uint32_t mode_count = 0;
    if (!reader.read_bits(6, mode_count)) return false;
    setup.mode_count = mode_count + 1;
    if (setup.mode_count > VORBIS_MAX_MODES) return false;

    for (size_t i = 0; i < setup.mode_count; ++i) {
        uint32_t blockflag = 0, win = 0, trans = 0, map = 0;
        if (!reader.read_bits(1, blockflag) || !reader.read_bits(16, win) ||
            !reader.read_bits(16, trans) || !reader.read_bits(8, map)) {
            return false;
        }
        if (win != 0 || trans != 0 || map >= setup.mapping_count) return false;
        setup.modes[i].blockflag = static_cast<uint8_t>(blockflag);
        setup.modes[i].windowtype = static_cast<uint16_t>(win);
        setup.modes[i].transformtype = static_cast<uint16_t>(trans);
        setup.modes[i].mapping = static_cast<uint8_t>(map);
    }

    uint32_t framing = 0;
    if (!reader.read_bits(1, framing) || framing != 1) return false;

    return true;
}

} // namespace audio_codecs::vorbis
