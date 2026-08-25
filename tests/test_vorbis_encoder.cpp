#include "audio_codecs/vorbis/vorbis_encoder.h"
#include "audio_codecs/vorbis/vorbis_decoder.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs;
    using namespace audio_codecs::vorbis;

    VorbisEncoder encoder;
    AudioConfig config{};
    config.channels = 2;
    config.sample_rate = 44100;
    config.bitrate_kbps = 128;

    assert(encoder.init(config));

    // Generate 44100 samples (1 second) of 440Hz sine wave
    const size_t num_samples = 44100 * 2;
    std::vector<float> in_pcm(num_samples);
    for (size_t i = 0; i < 44100; ++i) {
        float val = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        in_pcm[i * 2 + 0] = val;
        in_pcm[i * 2 + 1] = val;
    }

    std::vector<uint8_t> ogg_data(262144);
    int enc_bytes = encoder.encode_frame(in_pcm.data(), in_pcm.size(), ogg_data.data(), ogg_data.size());
    assert(enc_bytes > 0);

    int flush_bytes = encoder.flush(ogg_data.data() + enc_bytes, ogg_data.size() - enc_bytes);
    assert(flush_bytes >= 0);
    size_t total_ogg_bytes = enc_bytes + flush_bytes;

    // Check Ogg magic header
    assert(total_ogg_bytes > 4);
    assert(ogg_data[0] == 'O' && ogg_data[1] == 'g' && ogg_data[2] == 'g' && ogg_data[3] == 'S');

    // Decode with VorbisDecoder
    VorbisDecoder decoder;
    assert(decoder.init(config));

    std::vector<float> decoded_pcm(num_samples * 2);
    int dec_samples = decoder.decode_frame(ogg_data.data(), total_ogg_bytes, decoded_pcm.data(), decoded_pcm.size());
    assert(dec_samples > 0);
    assert(decoder.has_headers());
    assert(decoder.get_info().channels == 2);
    assert(decoder.get_info().sample_rate == 44100);

    std::cout << "Encoded " << in_pcm.size() << " samples to " << total_ogg_bytes << " Ogg bytes.\n";
    std::cout << "Decoded " << dec_samples << " PCM samples.\n";
    std::cout << "VorbisEncoder unit test passed!\n";
    return 0;
}
