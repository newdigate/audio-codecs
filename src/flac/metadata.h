#pragma once
#include "src/flac/flac_common.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::flac {

class MetadataParser {
public:
    // Parse "fLaC" marker and all leading metadata blocks up to the first audio frame
    static bool parse_stream_header(const uint8_t* in, size_t len, FlacStreamInfo& out_info, size_t& bytes_consumed);

    // Parse single 34-byte STREAMINFO block payload
    static bool parse_streaminfo_payload(const uint8_t* in, size_t len, FlacStreamInfo& out_info);
};

class MetadataBuilder {
public:
    // Write "fLaC" marker (4 bytes) + STREAMINFO metadata block (38 bytes) = 42 bytes total
    static size_t write_stream_header(uint8_t* out, size_t max_len, const FlacStreamInfo& info, bool is_last = true);

    // Update total samples and MD5 in-place in existing stream header buffer
    static bool update_streaminfo(uint8_t* streaminfo_header_ptr, uint64_t total_samples, const uint8_t md5[16]);
};

} // namespace audio_codecs::flac
