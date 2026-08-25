#pragma once
#include "audio_codecs/vorbis/vorbis_types.h"
#include "src/vorbis/decoder/header_parser.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::vorbis {

// Build Vorbis Identification header packet (type 0x01)
size_t build_vorbis_id_header(uint8_t* out_packet, size_t max_bytes, const VorbisInfo& info);

// Build Vorbis Comment header packet (type 0x03)
size_t build_vorbis_comment_header(uint8_t* out_packet, size_t max_bytes, const char* vendor = nullptr);

// Build Vorbis Setup header packet (type 0x05) and initialize VorbisSetup struct
size_t build_vorbis_setup_header(uint8_t* out_packet, size_t max_bytes, 
                                 const VorbisInfo& info, VorbisSetup& out_setup);

} // namespace audio_codecs::vorbis
