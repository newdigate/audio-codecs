#include <audio_codecs/slice/slice_generator.h>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace audio_codecs::slice {

SliceGenerator::SliceGenerator() {
    reset();
}

bool SliceGenerator::init(const GeneratorConfig& config) {
    config_ = config;
    if (config_.sample_rate == 0) config_.sample_rate = 44100;
    if (config_.time_sig_num == 0) config_.time_sig_num = 4;
    if (config_.time_sig_denom == 0) config_.time_sig_denom = 4;
    if (config_.bpm_q16 == 0) config_.bpm_q16 = (120 << 16);
    if (config_.max_slices == 0) config_.max_slices = MAX_CAPACITY;

    reset();

    DetectorConfig det_cfg;
    det_cfg.sample_rate = config_.sample_rate;
    det_cfg.sensitivity = config_.sensitivity;
    det_cfg.min_slice_samples = 512;
    det_cfg.pre_emphasis_alpha = 0.95f;
    detector_.init(det_cfg);

    header_.magic = ASL_MAGIC;
    header_.version = ASL_VERSION;
    header_.header_size = 128;
    header_.sample_rate = config_.sample_rate;
    header_.channels = config_.channels;
    header_.time_signature_num = config_.time_sig_num;
    header_.time_signature_denom = config_.time_sig_denom;
    header_.slice_mode = config_.mode;
    header_.original_bpm_q16 = config_.bpm_q16;
    header_.ppqn = ASL_PPQN;
    header_.slice_descriptor_size = sizeof(AslSlice);
    header_.slices_offset = 128;

    return true;
}

void SliceGenerator::reset() {
    detector_.reset();
    std::memset(&header_, 0, sizeof(header_));
    std::memset(slices_, 0, sizeof(slices_));
    slice_count_ = 0;
    raw_transient_count_ = 0;
    total_samples_ = 0;
    finalized_ = false;
}

bool SliceGenerator::process_pcm(const int16_t* pcm, uint32_t sample_count) {
    if (pcm == nullptr || sample_count == 0 || finalized_) {
        return false;
    }

    uint32_t detected[32];
    uint32_t count = detector_.process_block(pcm, sample_count, detected, 32);

    for (uint32_t i = 0; i < count; ++i) {
        if (raw_transient_count_ < MAX_CAPACITY) {
            raw_transients_[raw_transient_count_++] = detected[i];
        }
    }

    total_samples_ += sample_count;
    return true;
}

void SliceGenerator::generate_grid_slices() {
    double bpm = static_cast<double>(config_.bpm_q16) / 65536.0;
    if (bpm <= 0.0) bpm = 120.0;

    // 16th note subdivision: 4 divisions per beat
    double seconds_per_beat = 60.0 / bpm;
    double seconds_per_16th = seconds_per_beat / 4.0;
    double samples_per_16th = seconds_per_16th * config_.sample_rate;

    uint32_t total_16ths = static_cast<uint32_t>(total_samples_ / samples_per_16th);
    if (total_16ths > config_.max_slices) total_16ths = config_.max_slices;
    if (total_16ths > MAX_CAPACITY) total_16ths = MAX_CAPACITY;

    slice_count_ = static_cast<uint16_t>(total_16ths);
    uint32_t denom = config_.time_sig_denom ? config_.time_sig_denom : 4;
    uint32_t ticks_per_bar = ASL_PPQN * 4 * config_.time_sig_num / denom;
    if (ticks_per_bar == 0) ticks_per_bar = ASL_PPQN * 4;

    for (uint16_t i = 0; i < slice_count_; ++i) {
        AslSlice& s = slices_[i];
        s.start_sample = static_cast<uint32_t>(i * samples_per_16th);
        if (i + 1 < slice_count_) {
            s.length_samples = static_cast<uint32_t>((i + 1) * samples_per_16th) - s.start_sample;
        } else {
            s.length_samples = total_samples_ - s.start_sample;
        }

        s.musical_tick = i * 120; // 120 ticks per 16th note at PPQN=480
        s.midi_note = static_cast<uint8_t>(36 + (i % 64));
        s.bar_index = static_cast<uint8_t>(s.musical_tick / ticks_per_bar);
        s.beat_within_bar = static_cast<uint8_t>((s.musical_tick % ticks_per_bar) / ASL_PPQN);
        s.subdivision = static_cast<uint8_t>((s.musical_tick % ASL_PPQN) / 120);
        s.decay_ms = static_cast<uint16_t>((s.length_samples * 1000) / config_.sample_rate);
        s.tail_mode = ASL_TAIL_STRETCH_DECAY;
    }
}

void SliceGenerator::quantize_transients_to_grid() {
    double bpm = static_cast<double>(config_.bpm_q16) / 65536.0;
    if (bpm <= 0.0) bpm = 120.0;

    double ticks_per_sample = (bpm * ASL_PPQN) / (60.0 * config_.sample_rate);
    uint32_t denom = config_.time_sig_denom ? config_.time_sig_denom : 4;
    uint32_t ticks_per_bar = ASL_PPQN * 4 * config_.time_sig_num / denom;
    if (ticks_per_bar == 0) ticks_per_bar = ASL_PPQN * 4;

    slice_count_ = 0;

    // Ensure slice 0 starts at sample 0 if first transient is after 0
    if (raw_transient_count_ > 0 && raw_transients_[0] > 0) {
        AslSlice& s0 = slices_[slice_count_++];
        s0.start_sample = 0;
        s0.musical_tick = 0;
        s0.midi_note = 36;
        s0.bar_index = 0;
        s0.beat_within_bar = 0;
        s0.subdivision = 0;
        s0.tail_mode = ASL_TAIL_STRETCH_DECAY;
    }

    for (uint16_t i = 0; i < raw_transient_count_ && slice_count_ < config_.max_slices && slice_count_ < MAX_CAPACITY; ++i) {
        uint32_t pos = raw_transients_[i];
        if (pos == 0 && slice_count_ > 0) continue; // Already added

        AslSlice& s = slices_[slice_count_++];
        s.start_sample = pos;
        
        // Exact musical tick
        double exact_tick = static_cast<double>(pos) * ticks_per_sample;
        // Snap to nearest 16th note (120 ticks) within +/- 60 ticks
        uint32_t nearest_16th = static_cast<uint32_t>((exact_tick + 60.0) / 120.0) * 120;
        s.musical_tick = nearest_16th;
        s.midi_note = static_cast<uint8_t>(36 + ((slice_count_ - 1) % 64));
        s.bar_index = static_cast<uint8_t>(s.musical_tick / ticks_per_bar);
        s.beat_within_bar = static_cast<uint8_t>((s.musical_tick % ticks_per_bar) / ASL_PPQN);
        s.subdivision = static_cast<uint8_t>((s.musical_tick % ASL_PPQN) / 120);
        s.tail_mode = ASL_TAIL_STRETCH_DECAY;
    }

    // Compute slice lengths
    for (uint16_t i = 0; i < slice_count_; ++i) {
        if (i + 1 < slice_count_) {
            slices_[i].length_samples = slices_[i + 1].start_sample - slices_[i].start_sample;
        } else {
            slices_[i].length_samples = total_samples_ - slices_[i].start_sample;
        }
        slices_[i].decay_ms = static_cast<uint16_t>((slices_[i].length_samples * 1000) / config_.sample_rate);
    }
}

bool SliceGenerator::finalize() {
    if (finalized_) return true;

    if (config_.mode == ASL_MODE_GRID) {
        generate_grid_slices();
    } else if (config_.mode == ASL_MODE_TRANSIENT_TO_GRID) {
        quantize_transients_to_grid();
    } else {
        // Pure Transient Mode
        slice_count_ = 0;
        for (uint16_t i = 0; i < raw_transient_count_ && slice_count_ < config_.max_slices && slice_count_ < MAX_CAPACITY; ++i) {
            AslSlice& s = slices_[slice_count_++];
            s.start_sample = raw_transients_[i];
            s.midi_note = static_cast<uint8_t>(36 + ((slice_count_ - 1) % 64));
            s.tail_mode = ASL_TAIL_ONE_SHOT;
        }
        // Slice lengths
        for (uint16_t i = 0; i < slice_count_; ++i) {
            if (i + 1 < slice_count_) {
                slices_[i].length_samples = slices_[i + 1].start_sample - slices_[i].start_sample;
            } else {
                slices_[i].length_samples = total_samples_ - slices_[i].start_sample;
            }
            slices_[i].decay_ms = static_cast<uint16_t>((slices_[i].length_samples * 1000) / config_.sample_rate);
        }
    }

    header_.duration_samples = total_samples_;
    header_.duration_ms = static_cast<uint32_t>((static_cast<uint64_t>(total_samples_) * 1000) / config_.sample_rate);
    header_.total_slices = slice_count_;

    uint32_t denom = config_.time_sig_denom ? config_.time_sig_denom : 4;
    uint32_t ticks_per_bar = ASL_PPQN * 4 * config_.time_sig_num / denom;
    if (slice_count_ > 0 && ticks_per_bar > 0) {
        header_.num_bars = static_cast<uint16_t>((slices_[slice_count_ - 1].musical_tick / ticks_per_bar) + 1);
    }

    finalized_ = true;
    return true;
}

bool SliceGenerator::step(uint32_t /*budget_us*/) {
    if (!finalized_) {
        finalize();
    }
    return true;
}

bool SliceGenerator::is_complete() const {
    return finalized_;
}

uint16_t SliceGenerator::slice_count() const {
    return slice_count_;
}

const AslSlice* SliceGenerator::get_slice(uint16_t index) const {
    if (index >= slice_count_) return nullptr;
    return &slices_[index];
}

const AslHeader& SliceGenerator::header() const {
    return header_;
}

bool SliceGenerator::serialize(void* out_buffer, size_t buffer_size, size_t* out_bytes_written) const {
    if (out_buffer == nullptr || !finalized_) return false;

    size_t total_size = sizeof(AslHeader) + slice_count_ * sizeof(AslSlice);
    if (buffer_size < total_size) return false;

    uint8_t* ptr = static_cast<uint8_t*>(out_buffer);
    std::memcpy(ptr, &header_, sizeof(AslHeader));
    ptr += sizeof(AslHeader);

    std::memcpy(ptr, slices_, slice_count_ * sizeof(AslSlice));

    if (out_bytes_written != nullptr) {
        *out_bytes_written = total_size;
    }
    return true;
}

} // namespace audio_codecs::slice
