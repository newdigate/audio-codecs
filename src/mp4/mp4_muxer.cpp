#include "include/audio_codecs/mp4/mp4_muxer.h"
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
    return 15;
}

class BoxWriter {
public:
    void write_u8(uint8_t v) { buf_.push_back(v); }

    void write_u16(uint16_t v) {
        buf_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        buf_.push_back(static_cast<uint8_t>(v & 0xFF));
    }

    void write_u24(uint32_t v) {
        buf_.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        buf_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        buf_.push_back(static_cast<uint8_t>(v & 0xFF));
    }

    void write_u32(uint32_t v) {
        buf_.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
        buf_.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        buf_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        buf_.push_back(static_cast<uint8_t>(v & 0xFF));
    }

    void write_u64(uint64_t v) {
        write_u32(static_cast<uint32_t>(v >> 32));
        write_u32(static_cast<uint32_t>(v & 0xFFFFFFFF));
    }

    void write_bytes(const uint8_t* p, size_t n) {
        if (p && n > 0) {
            buf_.insert(buf_.end(), p, p + n);
        }
    }

    void write_zeros(size_t n) {
        buf_.insert(buf_.end(), n, 0);
    }

    void write_string(const char* str) {
        while (*str) {
            buf_.push_back(static_cast<uint8_t>(*str++));
        }
        buf_.push_back(0);
    }

    size_t start_box(uint32_t type) {
        size_t pos = buf_.size();
        write_u32(0); // placeholder for size
        write_u32(type);
        return pos;
    }

    size_t start_full_box(uint32_t type, uint8_t version = 0, uint32_t flags = 0) {
        size_t pos = start_box(type);
        write_u8(version);
        write_u24(flags);
        return pos;
    }

    void end_box(size_t start_pos) {
        uint32_t size = static_cast<uint32_t>(buf_.size() - start_pos);
        buf_[start_pos] = static_cast<uint8_t>((size >> 24) & 0xFF);
        buf_[start_pos + 1] = static_cast<uint8_t>((size >> 16) & 0xFF);
        buf_[start_pos + 2] = static_cast<uint8_t>((size >> 8) & 0xFF);
        buf_[start_pos + 3] = static_cast<uint8_t>(size & 0xFF);
    }

    void write_descriptor_header(uint8_t tag, size_t len) {
        write_u8(tag);
        if (len <= 127) {
            write_u8(static_cast<uint8_t>(len));
        } else {
            // Encode as 4 bytes with continuation bits
            write_u8(static_cast<uint8_t>(0x80 | ((len >> 21) & 0x7F)));
            write_u8(static_cast<uint8_t>(0x80 | ((len >> 14) & 0x7F)));
            write_u8(static_cast<uint8_t>(0x80 | ((len >> 7) & 0x7F)));
            write_u8(static_cast<uint8_t>(len & 0x7F));
        }
    }

    std::vector<uint8_t>& buffer() { return buf_; }
    const std::vector<uint8_t>& buffer() const { return buf_; }

private:
    std::vector<uint8_t> buf_;
};

} // anonymous namespace

struct Mp4Muxer::Impl {
    AudioConfig config_;
    AudioSpecificConfig asc_;
    std::vector<uint8_t> mdat_data_;
    std::vector<size_t> sample_sizes_;

    void reset() {
        mdat_data_.clear();
        sample_sizes_.clear();
    }

    std::vector<uint8_t> finalize() {
        BoxWriter writer;

        // 1. 'ftyp' File Type Box
        size_t ftyp_pos = writer.start_box(FTYP);
        writer.write_u32(BRAND_M4A);  // major_brand
        writer.write_u32(0);          // minor_version
        writer.write_u32(BRAND_M4A);  // compatible_brands
        writer.write_u32(BRAND_MP42);
        writer.write_u32(BRAND_ISOM);
        writer.end_box(ftyp_pos);

        // 2. 'mdat' Media Data Box
        size_t mdat_pos = writer.start_box(MDAT);
        uint64_t mdat_payload_offset = writer.buffer().size();
        if (!mdat_data_.empty()) {
            writer.write_bytes(mdat_data_.data(), mdat_data_.size());
        }
        writer.end_box(mdat_pos);

        // 3. 'moov' Movie Box
        uint32_t timescale = config_.sample_rate ? config_.sample_rate : 44100;
        uint32_t duration = static_cast<uint32_t>(sample_sizes_.size() * 1024);

        size_t moov_pos = writer.start_box(MOOV);

        // 3.1 'mvhd' Movie Header Box
        size_t mvhd_pos = writer.start_full_box(MVHD, 0, 0);
        writer.write_u32(0);          // creation_time
        writer.write_u32(0);          // modification_time
        writer.write_u32(timescale);  // timescale
        writer.write_u32(duration);   // duration
        writer.write_u32(0x00010000); // rate (1.0)
        writer.write_u16(0x0100);     // volume (1.0)
        writer.write_u16(0);          // reserved
        writer.write_zeros(8);        // reserved[2]
        // Unity matrix
        writer.write_u32(0x00010000); writer.write_u32(0); writer.write_u32(0);
        writer.write_u32(0); writer.write_u32(0x00010000); writer.write_u32(0);
        writer.write_u32(0); writer.write_u32(0); writer.write_u32(0x40000000);
        writer.write_zeros(24);       // pre_defined[6]
        writer.write_u32(2);          // next_track_ID
        writer.end_box(mvhd_pos);

        // 3.2 'trak' Track Box
        size_t trak_pos = writer.start_box(TRAK);

        // 3.2.1 'tkhd' Track Header Box
        size_t tkhd_pos = writer.start_full_box(TKHD, 0, 0x000007); // Track enabled, in movie, in preview
        writer.write_u32(0);          // creation_time
        writer.write_u32(0);          // modification_time
        writer.write_u32(1);          // track_ID
        writer.write_u32(0);          // reserved
        writer.write_u32(duration);   // duration
        writer.write_zeros(8);        // reserved[2]
        writer.write_u16(0);          // layer
        writer.write_u16(0);          // alternate_group
        writer.write_u16(0x0100);     // volume (1.0)
        writer.write_u16(0);          // reserved
        // Unity matrix
        writer.write_u32(0x00010000); writer.write_u32(0); writer.write_u32(0);
        writer.write_u32(0); writer.write_u32(0x00010000); writer.write_u32(0);
        writer.write_u32(0); writer.write_u32(0); writer.write_u32(0x40000000);
        writer.write_u32(0);          // width (0 for audio)
        writer.write_u32(0);          // height (0 for audio)
        writer.end_box(tkhd_pos);

        // 3.2.2 'mdia' Media Box
        size_t mdia_pos = writer.start_box(MDIA);

        // 3.2.2.1 'mdhd' Media Header Box
        size_t mdhd_pos = writer.start_full_box(MDHD, 0, 0);
        writer.write_u32(0);          // creation_time
        writer.write_u32(0);          // modification_time
        writer.write_u32(timescale);  // timescale
        writer.write_u32(duration);   // duration
        writer.write_u16(0x55C4);     // language ('und')
        writer.write_u16(0);          // pre_defined
        writer.end_box(mdhd_pos);

        // 3.2.2.2 'hdlr' Handler Reference Box
        size_t hdlr_pos = writer.start_full_box(HDLR, 0, 0);
        writer.write_u32(0);          // pre_defined
        writer.write_u32(HANDLER_SOUN); // handler_type ('soun')
        writer.write_zeros(12);       // reserved[3]
        writer.write_string("SoundHandler");
        writer.end_box(hdlr_pos);

        // 3.2.2.3 'minf' Media Information Box
        size_t minf_pos = writer.start_box(MINF);

        // 'smhd' Sound Media Header Box
        size_t smhd_pos = writer.start_full_box(SMHD, 0, 0);
        writer.write_u16(0);          // balance (center)
        writer.write_u16(0);          // reserved
        writer.end_box(smhd_pos);

        // 'dinf' Data Information Box
        size_t dinf_pos = writer.start_box(DINF);
        size_t dref_pos = writer.start_full_box(DREF, 0, 0);
        writer.write_u32(1);          // entry_count
        // 'url ' Data Entry Box
        size_t url_pos = writer.start_full_box(make_fourcc('u', 'r', 'l', ' '), 0, 0x000001);
        writer.end_box(url_pos);
        writer.end_box(dref_pos);
        writer.end_box(dinf_pos);

        // 'stbl' Sample Table Box
        size_t stbl_pos = writer.start_box(STBL);

        // 1. 'stsd' Sample Description Box
        size_t stsd_pos = writer.start_full_box(STSD, 0, 0);
        writer.write_u32(1);          // entry_count

        // 'mp4a' Audio Sample Entry
        size_t mp4a_pos = writer.start_box(MP4A);
        writer.write_zeros(6);        // reserved
        writer.write_u16(1);          // data_reference_index
        writer.write_zeros(8);        // reserved[2]
        writer.write_u16(config_.channels); // channelcount
        writer.write_u16(16);         // samplesize (16 bits)
        writer.write_u16(0);          // pre_defined
        writer.write_u16(0);          // reserved
        writer.write_u32(timescale << 16); // samplerate (16.16)

        // 'esds' Elementary Stream Descriptor Box
        std::vector<uint8_t> asc_bytes = serialize_asc(asc_);
        uint32_t bitrate_bps = config_.bitrate_kbps * 1000;

        size_t esds_pos = writer.start_full_box(ESDS, 0, 0);

        // Tag 0x05 (DecSpecificInfo)
        size_t tag5_len = asc_bytes.size();
        // Tag 0x04 (DecoderConfigDescr)
        size_t tag4_len = 13 + 2 + tag5_len; // 13 + tag(1)+len(1)+tag5_len
        // Tag 0x06 (SLConfigDescr)
        size_t tag6_len = 1;
        // Tag 0x03 (ES_Descr)
        size_t tag3_len = 3 + 2 + tag4_len + 2 + tag6_len;

        // Write Tag 0x03
        writer.write_descriptor_header(0x03, tag3_len);
        writer.write_u16(1);          // ES_ID = 1
        writer.write_u8(0);           // flags = 0

        // Write Tag 0x04
        writer.write_descriptor_header(0x04, tag4_len);
        writer.write_u8(0x40);        // objectTypeIndication (Audio ISO/IEC 14496-3)
        writer.write_u8(0x15);        // streamType = 5 (AudioStream) << 2 | 1
        writer.write_u24(0x000600);   // bufferSizeDB (1536 bytes)
        writer.write_u32(bitrate_bps); // maxBitrate
        writer.write_u32(bitrate_bps); // avgBitrate

        // Write Tag 0x05
        writer.write_descriptor_header(0x05, tag5_len);
        writer.write_bytes(asc_bytes.data(), asc_bytes.size());

        // Write Tag 0x06
        writer.write_descriptor_header(0x06, tag6_len);
        writer.write_u8(0x02);        // predefined = 2 (MP4)

        writer.end_box(esds_pos);
        writer.end_box(mp4a_pos);
        writer.end_box(stsd_pos);

        // 2. 'stts' Time-to-Sample Box
        size_t stts_pos = writer.start_full_box(STTS, 0, 0);
        if (sample_sizes_.empty()) {
            writer.write_u32(0);      // entry_count
        } else {
            writer.write_u32(1);      // entry_count
            writer.write_u32(static_cast<uint32_t>(sample_sizes_.size())); // sample_count
            writer.write_u32(1024);   // sample_delta (1024 samples per AAC frame)
        }
        writer.end_box(stts_pos);

        // 3. 'stsz' Sample Size Box
        size_t stsz_pos = writer.start_full_box(STSZ, 0, 0);
        writer.write_u32(0);          // sample_size (0 = variable size)
        writer.write_u32(static_cast<uint32_t>(sample_sizes_.size())); // sample_count
        for (size_t s : sample_sizes_) {
            writer.write_u32(static_cast<uint32_t>(s));
        }
        writer.end_box(stsz_pos);

        // 4. 'stsc' Sample-to-Chunk Box
        size_t stsc_pos = writer.start_full_box(STSC, 0, 0);
        if (sample_sizes_.empty()) {
            writer.write_u32(0);      // entry_count
        } else {
            writer.write_u32(1);      // entry_count
            writer.write_u32(1);      // first_chunk (1-indexed)
            writer.write_u32(1);      // samples_per_chunk
            writer.write_u32(1);      // sample_description_index
        }
        writer.end_box(stsc_pos);

        // 5. 'stco' Chunk Offset Box
        size_t stco_pos = writer.start_full_box(STCO, 0, 0);
        writer.write_u32(static_cast<uint32_t>(sample_sizes_.size())); // entry_count
        uint64_t current_chunk_offset = mdat_payload_offset;
        for (size_t s : sample_sizes_) {
            writer.write_u32(static_cast<uint32_t>(current_chunk_offset));
            current_chunk_offset += s;
        }
        writer.end_box(stco_pos);

        writer.end_box(stbl_pos);
        writer.end_box(minf_pos);
        writer.end_box(mdia_pos);
        writer.end_box(trak_pos);
        writer.end_box(moov_pos);

        return writer.buffer();
    }
};

Mp4Muxer::Mp4Muxer() : impl_(std::make_unique<Impl>()) {}

Mp4Muxer::~Mp4Muxer() = default;

Mp4Muxer::Mp4Muxer(Mp4Muxer&&) noexcept = default;
Mp4Muxer& Mp4Muxer::operator=(Mp4Muxer&&) noexcept = default;

bool Mp4Muxer::init(const AudioConfig& config) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->reset();

    impl_->config_ = config;
    impl_->asc_.audio_object_type = 2; // AAC-LC
    impl_->asc_.sample_rate = config.sample_rate;
    impl_->asc_.sampling_frequency_index = static_cast<uint8_t>(get_sf_index_from_rate(config.sample_rate));
    impl_->asc_.channel_configuration = config.channels;

    return true;
}

bool Mp4Muxer::add_sample(const uint8_t* sample_data, size_t sample_size) {
    if (!impl_ || !sample_data || sample_size == 0) {
        return false;
    }

    impl_->mdat_data_.insert(impl_->mdat_data_.end(), sample_data, sample_data + sample_size);
    impl_->sample_sizes_.push_back(sample_size);
    return true;
}

std::vector<uint8_t> Mp4Muxer::finalize() {
    if (!impl_) return {};
    return impl_->finalize();
}

void Mp4Muxer::reset() {
    if (impl_) {
        impl_->reset();
    }
}

} // namespace audio_codecs::mp4
