#include "include/audio_codecs/mp4/mp4_demuxer.h"
#include "src/core/bit_reader.h"
#include "src/core/bit_writer.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

namespace audio_codecs::mp4 {

namespace {

static const uint32_t kSamplingFreqTable[13] = {
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000, 7350
};

static int get_sf_index_from_rate(uint32_t sample_rate) {
    for (int i = 0; i < 13; ++i) {
        if (kSamplingFreqTable[i] == sample_rate) return i;
    }
    return 15; // explicit 24-bit rate
}

inline uint16_t read_u16_be(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

inline uint32_t read_u24_be(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 16) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]));
}

inline uint32_t read_u32_be(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           (static_cast<uint32_t>(p[3]));
}

inline uint64_t read_u64_be(const uint8_t* p) {
    return (static_cast<uint64_t>(read_u32_be(p)) << 32) | read_u32_be(p + 4);
}

struct Box {
    uint32_t type{0};
    uint64_t offset{0};      // Start of box header in memory
    uint64_t header_size{0}; // 8 or 16
    uint64_t size{0};        // Total box size including header

    uint64_t payload_offset() const { return offset + header_size; }
    uint64_t payload_size() const { return size >= header_size ? size - header_size : 0; }
};

bool read_box_header(const uint8_t* data, size_t file_size, uint64_t curr_offset, Box& box) {
    if (curr_offset + 8 > file_size) return false;
    uint32_t s32 = read_u32_be(data + curr_offset);
    uint32_t type = read_u32_be(data + curr_offset + 4);
    uint64_t size = s32;
    uint64_t header_size = 8;

    if (s32 == 1) {
        if (curr_offset + 16 > file_size) return false;
        size = read_u64_be(data + curr_offset + 8);
        header_size = 16;
    } else if (s32 == 0) {
        size = file_size - curr_offset;
    }

    if (size < header_size || curr_offset + size > file_size) {
        return false;
    }

    box.type = type;
    box.offset = curr_offset;
    box.header_size = header_size;
    box.size = size;
    return true;
}

bool read_descriptor(const uint8_t* p, size_t len, size_t& offset, uint8_t& tag, size_t& desc_len) {
    if (offset >= len) return false;
    tag = p[offset++];
    desc_len = 0;
    for (int i = 0; i < 4; ++i) {
        if (offset >= len) return false;
        uint8_t b = p[offset++];
        desc_len = (desc_len << 7) | (b & 0x7F);
        if ((b & 0x80) == 0) break;
    }
    return (offset + desc_len <= len);
}

bool parse_esds_box(const uint8_t* esds_payload, size_t esds_size, AudioSpecificConfig& asc, uint32_t& avg_bitrate) {
    if (esds_size < 4) return false; // FullBox version(1) + flags(3)
    size_t offset = 4;

    uint8_t tag = 0;
    size_t desc_len = 0;
    if (!read_descriptor(esds_payload, esds_size, offset, tag, desc_len) || tag != 0x03) {
        return false; // Expected ES_DescrTag
    }

    size_t es_end = offset + desc_len;
    if (offset + 3 > es_end) return false;
    offset += 2; // ES_ID (uint16)
    uint8_t flags = esds_payload[offset++];

    if (flags & 0x80) offset += 2; // streamDependenceFlag
    if (flags & 0x40) {            // URL_Flag
        if (offset >= es_end) return false;
        uint8_t url_len = esds_payload[offset++];
        offset += url_len;
    }
    if (flags & 0x20) offset += 2; // OCRstreamFlag

    if (offset >= es_end) return false;
    if (!read_descriptor(esds_payload, es_end, offset, tag, desc_len) || tag != 0x04) {
        return false; // Expected DecoderConfigDescrTag
    }

    size_t dec_end = offset + desc_len;
    if (offset + 13 > dec_end) return false;
    avg_bitrate = read_u32_be(esds_payload + offset + 9) / 1000;
    offset += 13;

    if (offset < dec_end) {
        if (read_descriptor(esds_payload, dec_end, offset, tag, desc_len) && tag == 0x05) {
            if (parse_asc(esds_payload + offset, desc_len, asc)) {
                return true;
            }
        }
    }
    return false;
}

struct StscEntry {
    uint32_t first_chunk{0};
    uint32_t samples_per_chunk{0};
    uint32_t sample_description_index{0};
};

} // anonymous namespace

std::vector<uint8_t> serialize_asc(const AudioSpecificConfig& asc) {
    core::BitWriter writer;
    uint8_t buf[32] = {0};
    writer.init(buf, sizeof(buf));

    uint8_t aot = asc.audio_object_type ? asc.audio_object_type : 2;
    if (aot < 31) {
        writer.write_bits(aot, 5);
    } else {
        writer.write_bits(31, 5);
        writer.write_bits(aot - 32, 6);
    }

    uint8_t sf_index = asc.sampling_frequency_index;
    if (sf_index > 15 || (sf_index == 0 && asc.sample_rate != 96000)) {
        sf_index = static_cast<uint8_t>(get_sf_index_from_rate(asc.sample_rate));
    }

    writer.write_bits(sf_index, 4);
    if (sf_index == 15) {
        writer.write_bits(asc.sample_rate, 24);
    }

    writer.write_bits(asc.channel_configuration, 4);

    // GASpecificConfig fields for standard AAC
    writer.write_bits(0, 1); // frameLengthFlag: 0 (1024 lines)
    writer.write_bits(0, 1); // dependsOnCoreCoder: 0
    writer.write_bits(0, 1); // extensionFlag: 0

    writer.flush_to_byte();

    size_t byte_count = writer.get_byte_count();
    return std::vector<uint8_t>(buf, buf + byte_count);
}

bool parse_asc(const uint8_t* data, size_t size, AudioSpecificConfig& asc) {
    if (!data || size < 2) return false;

    core::BitReader reader;
    reader.init(data, size);

    if (reader.bits_remaining() < 5) return false;
    uint32_t aot = reader.read_bits(5);
    if (aot == 31) {
        if (reader.bits_remaining() < 6) return false;
        aot = 32 + reader.read_bits(6);
    }
    asc.audio_object_type = static_cast<uint8_t>(aot);

    if (reader.bits_remaining() < 4) return false;
    uint32_t sf_index = reader.read_bits(4);
    asc.sampling_frequency_index = static_cast<uint8_t>(sf_index);

    if (sf_index == 15) {
        if (reader.bits_remaining() < 24) return false;
        asc.sample_rate = reader.read_bits(24);
    } else if (sf_index < 13) {
        asc.sample_rate = kSamplingFreqTable[sf_index];
    } else {
        return false;
    }

    if (reader.bits_remaining() < 4) return false;
    asc.channel_configuration = static_cast<uint8_t>(reader.read_bits(4));

    // Support SBR / PS extension if present
    if (asc.audio_object_type == 5 || asc.audio_object_type == 29) {
        if (reader.bits_remaining() >= 4) {
            uint32_t ext_sf_index = reader.read_bits(4);
            if (ext_sf_index == 15 && reader.bits_remaining() >= 24) {
                reader.read_bits(24);
            }
            if (reader.bits_remaining() >= 5) {
                uint32_t core_aot = reader.read_bits(5);
                if (core_aot == 31 && reader.bits_remaining() >= 6) {
                    core_aot = 32 + reader.read_bits(6);
                }
                asc.audio_object_type = static_cast<uint8_t>(core_aot);
            }
        }
    }

    return true;
}

struct Mp4Demuxer::Impl {
    const uint8_t* data_{nullptr};
    size_t size_{0};

    AudioConfig config_{};
    AudioSpecificConfig asc_{};
    bool has_asc_{false};

    std::vector<uint64_t> sample_offsets_;
    std::vector<size_t> sample_sizes_;
    size_t current_sample_idx_{0};

    void clear() {
        data_ = nullptr;
        size_ = 0;
        config_ = AudioConfig{};
        asc_ = AudioSpecificConfig{};
        has_asc_ = false;
        sample_offsets_.clear();
        sample_sizes_.clear();
        current_sample_idx_ = 0;
    }

    bool parse_stbl(const uint8_t* stbl_data, size_t stbl_size, uint64_t stbl_file_offset) {
        uint64_t offset = 0;
        std::vector<StscEntry> stsc_entries;
        std::vector<uint64_t> chunk_offsets;
        std::vector<uint32_t> stsz_sizes;
        uint32_t uniform_sample_size = 0;
        uint32_t sample_count = 0;

        while (offset + 8 <= stbl_size) {
            Box sub;
            if (!read_box_header(stbl_data, stbl_size, offset, sub)) {
                break;
            }

            const uint8_t* payload = stbl_data + sub.payload_offset();
            size_t payload_len = static_cast<size_t>(sub.payload_size());

            if (sub.type == STSD && payload_len >= 8) {
                // stsd FullBox: version(1) + flags(3) + entry_count(4)
                uint32_t entry_count = read_u32_be(payload + 4);
                uint64_t entry_offset = 8;
                for (uint32_t e = 0; e < entry_count && entry_offset + 8 <= payload_len; ++e) {
                    Box entry_box;
                    if (!read_box_header(payload, payload_len, entry_offset, entry_box)) {
                        break;
                    }
                    if (entry_box.type == MP4A && entry_box.payload_size() >= 28) {
                        const uint8_t* mp4a_payload = payload + entry_box.payload_offset();
                        size_t mp4a_len = static_cast<size_t>(entry_box.payload_size());

                        uint16_t channels = read_u16_be(mp4a_payload + 16);
                        uint32_t sample_rate = read_u32_be(mp4a_payload + 24) >> 16;
                        config_.channels = static_cast<uint8_t>(channels ? channels : 2);
                        config_.sample_rate = sample_rate ? sample_rate : 44100;

                        // Parse child boxes inside mp4a (looking for esds)
                        uint64_t child_offset = 28;
                        while (child_offset + 8 <= mp4a_len) {
                            Box child_box;
                            if (!read_box_header(mp4a_payload, mp4a_len, child_offset, child_box)) {
                                break;
                            }
                            if (child_box.type == ESDS) {
                                const uint8_t* esds_data = mp4a_payload + child_box.payload_offset();
                                size_t esds_len = static_cast<size_t>(child_box.payload_size());
                                uint32_t avg_br = 0;
                                if (parse_esds_box(esds_data, esds_len, asc_, avg_br)) {
                                    has_asc_ = true;
                                    config_.sample_rate = asc_.sample_rate;
                                    config_.channels = asc_.channel_configuration;
                                    if (avg_br > 0) config_.bitrate_kbps = avg_br;
                                }
                            }
                            child_offset += child_box.size;
                        }
                    }
                    entry_offset += entry_box.size;
                }
            } else if (sub.type == STSZ && payload_len >= 12) {
                // stsz FullBox: version(1) + flags(3) + sample_size(4) + sample_count(4)
                uniform_sample_size = read_u32_be(payload + 4);
                sample_count = read_u32_be(payload + 8);
                if (uniform_sample_size == 0 && payload_len >= 12 + 4 * static_cast<size_t>(sample_count)) {
                    stsz_sizes.resize(sample_count);
                    for (size_t i = 0; i < sample_count; ++i) {
                        stsz_sizes[i] = read_u32_be(payload + 12 + i * 4);
                    }
                }
            } else if (sub.type == STSC && payload_len >= 8) {
                // stsc FullBox: version(1) + flags(3) + entry_count(4)
                uint32_t entry_count = read_u32_be(payload + 4);
                if (payload_len >= 8 + 12 * static_cast<size_t>(entry_count)) {
                    stsc_entries.resize(entry_count);
                    for (size_t i = 0; i < entry_count; ++i) {
                        stsc_entries[i].first_chunk = read_u32_be(payload + 8 + i * 12);
                        stsc_entries[i].samples_per_chunk = read_u32_be(payload + 8 + i * 12 + 4);
                        stsc_entries[i].sample_description_index = read_u32_be(payload + 8 + i * 12 + 8);
                    }
                }
            } else if (sub.type == STCO && payload_len >= 8) {
                // stco FullBox: version(1) + flags(3) + entry_count(4)
                uint32_t entry_count = read_u32_be(payload + 4);
                if (payload_len >= 8 + 4 * static_cast<size_t>(entry_count)) {
                    chunk_offsets.resize(entry_count);
                    for (size_t i = 0; i < entry_count; ++i) {
                        chunk_offsets[i] = read_u32_be(payload + 8 + i * 4);
                    }
                }
            } else if (sub.type == CO64 && payload_len >= 8) {
                // co64 FullBox: version(1) + flags(3) + entry_count(4)
                uint32_t entry_count = read_u32_be(payload + 4);
                if (payload_len >= 8 + 8 * static_cast<size_t>(entry_count)) {
                    chunk_offsets.resize(entry_count);
                    for (size_t i = 0; i < entry_count; ++i) {
                        chunk_offsets[i] = read_u64_be(payload + 8 + i * 8);
                    }
                }
            }

            offset += sub.size;
        }

        // Build sample index table
        if (sample_count == 0) {
            sample_offsets_.clear();
            sample_sizes_.clear();
            return true;
        }

        sample_offsets_.resize(sample_count);
        sample_sizes_.resize(sample_count);

        for (size_t i = 0; i < sample_count; ++i) {
            sample_sizes_[i] = (uniform_sample_size != 0) ? uniform_sample_size : (i < stsz_sizes.size() ? stsz_sizes[i] : 0);
        }

        if (chunk_offsets.empty() || stsc_entries.empty()) {
            return false;
        }

        size_t stsc_idx = 0;
        size_t current_sample = 0;

        for (size_t c = 1; c <= chunk_offsets.size(); ++c) {
            if (stsc_idx + 1 < stsc_entries.size() && c >= stsc_entries[stsc_idx + 1].first_chunk) {
                stsc_idx++;
            }

            uint32_t samples_in_chunk = stsc_entries[stsc_idx].samples_per_chunk;
            uint64_t chunk_off = chunk_offsets[c - 1];
            uint64_t off_in_chunk = 0;

            for (uint32_t s = 0; s < samples_in_chunk; ++s) {
                if (current_sample >= sample_count) break;
                sample_offsets_[current_sample] = chunk_off + off_in_chunk;
                off_in_chunk += sample_sizes_[current_sample];
                current_sample++;
            }
        }

        if (current_sample != sample_count) {
            return false;
        }

        // Validate bounds for all samples
        for (size_t i = 0; i < sample_count; ++i) {
            if (sample_offsets_[i] + sample_sizes_[i] > size_) {
                return false;
            }
        }

        return true;
    }

    bool parse_minf(const uint8_t* minf_data, size_t minf_size, uint64_t minf_file_offset) {
        uint64_t offset = 0;
        while (offset + 8 <= minf_size) {
            Box sub;
            if (!read_box_header(minf_data, minf_size, offset, sub)) {
                break;
            }
            if (sub.type == STBL) {
                if (parse_stbl(minf_data + sub.payload_offset(), static_cast<size_t>(sub.payload_size()),
                               minf_file_offset + sub.payload_offset())) {
                    return true;
                }
            }
            offset += sub.size;
        }
        return false;
    }

    bool parse_mdia(const uint8_t* mdia_data, size_t mdia_size, uint64_t mdia_file_offset) {
        uint64_t offset = 0;
        bool is_sound = false;
        Box minf_box{};
        bool has_minf = false;

        while (offset + 8 <= mdia_size) {
            Box sub;
            if (!read_box_header(mdia_data, mdia_size, offset, sub)) {
                break;
            }

            if (sub.type == HDLR && sub.payload_size() >= 12) {
                const uint8_t* hdlr_payload = mdia_data + sub.payload_offset();
                uint32_t handler_type = read_u32_be(hdlr_payload + 8);
                if (handler_type == HANDLER_SOUN) {
                    is_sound = true;
                }
            } else if (sub.type == MINF) {
                minf_box = sub;
                has_minf = true;
            }
            offset += sub.size;
        }

        if (has_minf) {
            // Attempt minf parsing if it is a sound handler or default track
            if (parse_minf(mdia_data + minf_box.payload_offset(), static_cast<size_t>(minf_box.payload_size()),
                           mdia_file_offset + minf_box.payload_offset())) {
                return true;
            }
        }
        return false;
    }

    bool parse_trak(const uint8_t* trak_data, size_t trak_size, uint64_t trak_file_offset) {
        uint64_t offset = 0;
        while (offset + 8 <= trak_size) {
            Box sub;
            if (!read_box_header(trak_data, trak_size, offset, sub)) {
                break;
            }
            if (sub.type == MDIA) {
                if (parse_mdia(trak_data + sub.payload_offset(), static_cast<size_t>(sub.payload_size()),
                               trak_file_offset + sub.payload_offset())) {
                    return true;
                }
            }
            offset += sub.size;
        }
        return false;
    }

    bool parse_moov(const uint8_t* moov_data, size_t moov_size, uint64_t moov_file_offset) {
        uint64_t offset = 0;
        bool audio_track_parsed = false;

        while (offset + 8 <= moov_size) {
            Box sub;
            if (!read_box_header(moov_data, moov_size, offset, sub)) {
                break;
            }
            if (sub.type == TRAK) {
                if (parse_trak(moov_data + sub.payload_offset(), static_cast<size_t>(sub.payload_size()),
                               moov_file_offset + sub.payload_offset())) {
                    audio_track_parsed = true;
                    break;
                }
            }
            offset += sub.size;
        }
        return audio_track_parsed;
    }
};

Mp4Demuxer::Mp4Demuxer() : impl_(std::make_unique<Impl>()) {}

Mp4Demuxer::~Mp4Demuxer() = default;

Mp4Demuxer::Mp4Demuxer(Mp4Demuxer&&) noexcept = default;
Mp4Demuxer& Mp4Demuxer::operator=(Mp4Demuxer&&) noexcept = default;

void Mp4Demuxer::reset() {
    if (impl_) {
        impl_->current_sample_idx_ = 0;
    }
}

bool Mp4Demuxer::open(const uint8_t* data, size_t size) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->clear();

    if (!data || size < 8) {
        return false;
    }

    impl_->data_ = data;
    impl_->size_ = size;

    uint64_t offset = 0;
    bool found_moov = false;

    while (offset + 8 <= size) {
        Box box;
        if (!read_box_header(data, size, offset, box)) {
            break;
        }

        if (box.type == MOOV) {
            if (impl_->parse_moov(data + box.payload_offset(), static_cast<size_t>(box.payload_size()), box.payload_offset())) {
                found_moov = true;
            }
        }
        offset += box.size;
    }

    if (!found_moov) {
        impl_->clear();
        return false;
    }

    if (!impl_->has_asc_) {
        impl_->asc_.audio_object_type = 2; // AAC-LC
        impl_->asc_.sample_rate = impl_->config_.sample_rate;
        impl_->asc_.sampling_frequency_index = static_cast<uint8_t>(get_sf_index_from_rate(impl_->config_.sample_rate));
        impl_->asc_.channel_configuration = impl_->config_.channels;
        impl_->has_asc_ = true;
    }

    return true;
}

bool Mp4Demuxer::get_audio_config(AudioConfig& config) const {
    if (!impl_ || !impl_->data_ || impl_->config_.sample_rate == 0) return false;
    config = impl_->config_;
    return true;
}

bool Mp4Demuxer::get_asc(AudioSpecificConfig& asc) const {
    if (!impl_ || !impl_->has_asc_) return false;
    asc = impl_->asc_;
    return true;
}

size_t Mp4Demuxer::get_sample_count() const {
    return impl_ ? impl_->sample_sizes_.size() : 0;
}

bool Mp4Demuxer::read_sample(size_t sample_index, const uint8_t*& sample_ptr, size_t& sample_size) const {
    if (!impl_ || !impl_->data_ || sample_index >= impl_->sample_sizes_.size()) {
        sample_ptr = nullptr;
        sample_size = 0;
        return false;
    }

    sample_ptr = impl_->data_ + impl_->sample_offsets_[sample_index];
    sample_size = impl_->sample_sizes_[sample_index];
    return true;
}

bool Mp4Demuxer::read_next_sample(const uint8_t*& sample_ptr, size_t& sample_size) {
    if (!impl_) {
        sample_ptr = nullptr;
        sample_size = 0;
        return false;
    }
    if (impl_->current_sample_idx_ >= impl_->sample_sizes_.size()) {
        sample_ptr = nullptr;
        sample_size = 0;
        return false;
    }
    return read_sample(impl_->current_sample_idx_++, sample_ptr, sample_size);
}

} // namespace audio_codecs::mp4
