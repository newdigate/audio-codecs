#include <audio_codecs/slice/slice_reader.h>
#include <algorithm>

namespace audio_codecs::slice {

SliceReader::SliceReader() = default;

bool SliceReader::init(const uint8_t* asl_data, size_t asl_size) {
    header_ = nullptr;
    slices_ = nullptr;
    total_slices_ = 0;
    data_size_ = 0;

    if (asl_data == nullptr || asl_size < sizeof(AslHeader)) {
        return false;
    }

    const auto* hdr = reinterpret_cast<const AslHeader*>(asl_data);
    if (hdr->magic != ASL_MAGIC || hdr->version != ASL_VERSION || hdr->header_size != 128) {
        return false;
    }

    if (hdr->slices_offset < sizeof(AslHeader)) {
        return false;
    }

    size_t required_size = static_cast<size_t>(hdr->slices_offset) + static_cast<size_t>(hdr->total_slices) * sizeof(AslSlice);
    if (asl_size < required_size) {
        return false;
    }

    header_ = hdr;
    slices_ = reinterpret_cast<const AslSlice*>(asl_data + hdr->slices_offset);
    total_slices_ = hdr->total_slices;
    data_size_ = asl_size;

    return true;
}

const AslHeader& SliceReader::header() const {
    static const AslHeader empty_header{};
    return (header_ != nullptr) ? *header_ : empty_header;
}

uint16_t SliceReader::total_slices() const {
    return total_slices_;
}

const AslSlice* SliceReader::get_slice(uint16_t index) const {
    if (slices_ == nullptr || index >= total_slices_) {
        return nullptr;
    }
    return &slices_[index];
}

const AslSlice* SliceReader::find_slice_at_sample(uint32_t sample_offset) const {
    if (slices_ == nullptr || total_slices_ == 0) {
        return nullptr;
    }

    // Binary search: upper_bound on start_sample
    int low = 0;
    int high = static_cast<int>(total_slices_) - 1;
    int best = -1;

    while (low <= high) {
        int mid = low + (high - low) / 2;
        if (slices_[mid].start_sample <= sample_offset) {
            best = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    if (best >= 0) {
        const AslSlice& s = slices_[best];
        if (sample_offset < static_cast<uint64_t>(s.start_sample) + s.length_samples) {
            return &s;
        }
    }
    return nullptr;
}

const AslSlice* SliceReader::find_slice_at_ms(uint32_t ms) const {
    if (header_ == nullptr || header_->sample_rate == 0) {
        return nullptr;
    }
    uint32_t sample_offset = static_cast<uint32_t>((static_cast<uint64_t>(ms) * header_->sample_rate) / 1000);
    return find_slice_at_sample(sample_offset);
}

const AslSlice* SliceReader::find_slice_at_tick(uint32_t tick) const {
    if (slices_ == nullptr || total_slices_ == 0) {
        return nullptr;
    }

    int low = 0;
    int high = static_cast<int>(total_slices_) - 1;
    int best = -1;

    while (low <= high) {
        int mid = low + (high - low) / 2;
        if (slices_[mid].musical_tick <= tick) {
            best = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    return (best >= 0) ? &slices_[best] : nullptr;
}

} // namespace audio_codecs::slice
