#include "audio_codecs/audio_codecs.h"
#include "audio_codecs/wav/wav_decoder.h"
#include "audio_codecs/wav/wav_encoder.h"
#include <cassert>
#include <cmath>
#include <vector>
#include <iostream>

using namespace audio_codecs;
using namespace audio_codecs::preview;

void test_wav_decoder_preview_integration() {
    using namespace audio_codecs::wav;

    constexpr uint32_t sample_rate = 44100;
    constexpr uint8_t channels = 2;
    constexpr size_t num_frames = sample_rate * 2; // 2 seconds
    std::vector<int16_t> pcm(num_frames * channels);
    for (size_t f = 0; f < num_frames; ++f) {
        float t = static_cast<float>(f) / sample_rate;
        float env = 1.0f - (static_cast<float>(f) / num_frames);
        int16_t val = static_cast<int16_t>(std::sin(2.0f * 3.14159265f * 440.0f * t) * 30000.0f * env);
        pcm[f * 2] = val;
        pcm[f * 2 + 1] = -val;
    }

    // 1. Encode short PCM buffer to WAV using audio_codecs::wav::WavEncoder
    WavEncoder encoder;
    WavEncoderConfig enc_cfg;
    enc_cfg.core_config.sample_rate = sample_rate;
    enc_cfg.core_config.channels = channels;
    enc_cfg.sample_format = WavSampleFormat::Int16LE;
    bool enc_ok = encoder.init_wav(enc_cfg);
    assert(enc_ok);

    std::vector<uint8_t> wav_stream(44);
    int hdr_len = encoder.write_stream_header(wav_stream.data(), wav_stream.size());
    assert(hdr_len == 44);

    std::vector<uint8_t> payload(pcm.size() * sizeof(int16_t));
    int enc_bytes = encoder.encode_frame_i16(pcm.data(), pcm.size(), payload.data(), payload.size());
    assert(enc_bytes == static_cast<int>(pcm.size() * sizeof(int16_t)));

    encoder.finalize_header(wav_stream.data(), static_cast<uint32_t>(enc_bytes));
    wav_stream.insert(wav_stream.end(), payload.begin(), payload.begin() + enc_bytes);

    // 2. Decode WAV using audio_codecs::wav::WavDecoder
    WavDecoder decoder;
    size_t consumed = 0;
    bool dec_header_ok = decoder.parse_stream_header(wav_stream.data(), wav_stream.size(), consumed);
    assert(dec_header_ok);
    assert(consumed == 44);
    assert(decoder.get_sample_rate() == sample_rate);
    assert(decoder.get_channels() == channels);

    std::vector<int16_t> decoded_pcm(pcm.size());
    int dec_samples = decoder.decode_frame_i16(wav_stream.data() + consumed,
                                               wav_stream.size() - consumed,
                                               decoded_pcm.data(),
                                               decoded_pcm.size());
    assert(dec_samples == static_cast<int>(pcm.size()));

    // Verify PCM exact match
    for (size_t i = 0; i < pcm.size(); ++i) {
        assert(pcm[i] == decoded_pcm[i]);
    }

    // 3. Feed decoded frames into PreviewGenerator
    MemoryReader pcm_reader(reinterpret_cast<const uint8_t*>(decoded_pcm.data()),
                            decoded_pcm.size() * sizeof(int16_t));
    std::vector<uint8_t> apv_storage(1024 * 64, 0);
    MemoryWriter apv_writer(apv_storage.data(), apv_storage.size());

    PreviewGenerator gen;
    bool gen_ok = gen.init(pcm_reader, apv_writer, nullptr, sample_rate, channels, true);
    assert(gen_ok);
    assert(gen.generate_all());

    // 4. Verify PreviewReader queries it accurately
    MemoryReader apv_reader(apv_storage.data(), apv_writer.size());
    PreviewReader pr;
    assert(pr.init(apv_reader));
    assert(pr.duration_ms() == 2000);
    assert(pr.sample_rate() == sample_rate);
    assert(pr.channels() == channels);
    assert(pr.total_frames() == num_frames);

    WaveformPointStereo points[10];
    assert(pr.read_preview_stereo(0, 2000, points, 10) == 10);
    for (size_t i = 1; i < 10; ++i) {
        assert(points[i].left_max <= points[i - 1].left_max);
    }
    for (size_t i = 0; i < 10; ++i) {
        assert(points[i].left_max > 0);
        assert(points[i].left_min < 0);
        assert(points[i].right_max > 0);
        assert(points[i].right_min < 0);
    }
}

int main() {
    constexpr uint32_t sample_rate = 44100;
    constexpr size_t num_frames = sample_rate * 5;
    std::vector<int16_t> pcm(num_frames * 2);
    for (size_t f = 0; f < num_frames; ++f) {
        float t = static_cast<float>(f) / sample_rate;
        float env = 1.0f - (static_cast<float>(f) / num_frames);
        int16_t val = static_cast<int16_t>(std::sin(2.0f * 3.14159f * 440.0f * t) * 30000.0f * env);
        pcm[f * 2] = val;
        pcm[f * 2 + 1] = -val;
    }

    MemoryReader pcm_reader(reinterpret_cast<const uint8_t*>(pcm.data()), pcm.size() * sizeof(int16_t));
    std::vector<uint8_t> apv_storage(1024 * 64, 0);
    MemoryWriter apv_writer(apv_storage.data(), apv_storage.size());

    PreviewGenerator gen;
    bool ok = gen.init(pcm_reader, apv_writer, nullptr, sample_rate, 2, true);
    assert(ok);
    assert(gen.generate_all());

    MemoryReader apv_reader(apv_storage.data(), apv_writer.size());
    PreviewReader pr;
    assert(pr.init(apv_reader));
    assert(pr.duration_ms() == 5000);

    WaveformPointStereo points[10];
    assert(pr.read_preview_stereo(0, 5000, points, 10) == 10);
    for (size_t i = 1; i < 10; ++i) {
        assert(points[i].left_max <= points[i - 1].left_max);
    }

    test_wav_decoder_preview_integration();

    std::cout << "test_preview_integration PASSED\n";
    return 0;
}
