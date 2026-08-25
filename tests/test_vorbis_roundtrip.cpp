#include "audio_codecs/audio_codecs.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs;
    using namespace audio_codecs::vorbis;

    VorbisEncoder encoder;
    VorbisDecoder decoder;

    AudioConfig config{};
    config.channels = 2;
    config.sample_rate = 44100;
    config.bitrate_kbps = 128;

    assert(encoder.init(config));
    assert(decoder.init(config));

    // Generate 2 seconds of stereo test audio: mixed sinusoids (440Hz, 1000Hz, 2500Hz)
    const size_t sample_rate = 44100;
    const size_t total_frames = sample_rate * 2;
    std::vector<float> input_pcm(total_frames * 2);

    for (size_t i = 0; i < total_frames; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(sample_rate);
        float s1 = 0.3f * std::sin(2.0f * 3.14159265f * 440.0f * t);
        float s2 = 0.2f * std::sin(2.0f * 3.14159265f * 1000.0f * t);
        float s3 = 0.1f * std::sin(2.0f * 3.14159265f * 2500.0f * t);
        input_pcm[i * 2 + 0] = s1 + s2;
        input_pcm[i * 2 + 1] = s1 + s3;
    }

    // Stream encoding in chunks of 1024 samples
    std::vector<uint8_t> ogg_bitstream;
    uint8_t chunk_buf[8192];

    size_t chunk_size = 1024;
    for (size_t i = 0; i < input_pcm.size(); i += chunk_size) {
        size_t samples_to_encode = std::min(chunk_size, input_pcm.size() - i);
        int bytes_written = encoder.encode_frame(input_pcm.data() + i, samples_to_encode, 
                                                 chunk_buf, sizeof(chunk_buf));
        if (bytes_written > 0) {
            ogg_bitstream.insert(ogg_bitstream.end(), chunk_buf, chunk_buf + bytes_written);
        }
    }

    int flush_bytes = encoder.flush(chunk_buf, sizeof(chunk_buf));
    if (flush_bytes > 0) {
        ogg_bitstream.insert(ogg_bitstream.end(), chunk_buf, chunk_buf + flush_bytes);
    }

    assert(!ogg_bitstream.empty());
    std::cout << "Encoded " << input_pcm.size() << " samples into " 
              << ogg_bitstream.size() << " Ogg/Vorbis bytes.\n";

    // Stream decoding in chunks of 512 bytes
    std::vector<float> decoded_pcm;
    float dec_buf[4096];

    for (size_t i = 0; i < ogg_bitstream.size(); i += 512) {
        size_t bytes_to_feed = std::min<size_t>(512, ogg_bitstream.size() - i);
        int samples_out = decoder.decode_frame(ogg_bitstream.data() + i, bytes_to_feed, 
                                               dec_buf, sizeof(dec_buf) / sizeof(float));
        if (samples_out > 0) {
            decoded_pcm.insert(decoded_pcm.end(), dec_buf, dec_buf + samples_out);
        }
    }

    assert(decoder.has_headers());
    assert(!decoded_pcm.empty());
    std::cout << "Decoded " << decoded_pcm.size() << " PCM samples back from Ogg bitstream.\n";

    // Verify audio signal presence and energy
    float input_energy = 0.0f;
    for (float s : input_pcm) input_energy += s * s;

    float decoded_energy = 0.0f;
    for (float s : decoded_pcm) decoded_energy += s * s;

    std::cout << "Input energy: " << input_energy << ", Decoded energy: " << decoded_energy << "\n";
    assert(decoded_energy > 0.0f);

    std::cout << "Vorbis full streaming roundtrip test passed!\n";
    return 0;
}
