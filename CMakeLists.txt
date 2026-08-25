cmake_minimum_required(VERSION 3.16)
project(audio_codecs VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Core library
add_library(audio_codecs_core STATIC
    src/core/bit_reader.cpp
    src/core/bit_writer.cpp
    src/core/fft.cpp
)

# MP3 library
add_library(audio_codecs_mp3 STATIC
    src/mp3/mp3_tables.cpp
    src/mp3/decoder/bit_reservoir.cpp
    src/mp3/decoder/huffman_decoder.cpp
    src/mp3/decoder/requantizer.cpp
    src/mp3/decoder/imdct.cpp
    src/mp3/decoder/synthesis_filter.cpp
    src/mp3/decoder/mp3_decoder.cpp
    src/mp3/encoder/analysis_filter.cpp
    src/mp3/encoder/mdct.cpp
    src/mp3/encoder/psychoacoustic.cpp
    src/mp3/encoder/huffman_encoder.cpp
    src/mp3/encoder/quantizer.cpp
    src/mp3/encoder/mp3_encoder.cpp
)
target_link_libraries(audio_codecs_mp3 PUBLIC audio_codecs_core)

# FLAC library
add_library(audio_codecs_flac STATIC
    src/flac/crc.cpp
    src/flac/md5.cpp
    src/flac/metadata.cpp
    src/flac/decoder/rice_decoder.cpp
    src/flac/decoder/subframe_decoder.cpp
    src/flac/decoder/channel_decorrelator.cpp
    src/flac/decoder/flac_decoder.cpp
    src/flac/encoder/rice_encoder.cpp
    src/flac/encoder/fixed_predictor.cpp
    src/flac/encoder/channel_decorrelator.cpp
    src/flac/encoder/lpc_analyzer.cpp
    src/flac/encoder/flac_encoder.cpp
)
target_link_libraries(audio_codecs_flac PUBLIC audio_codecs_core)

# Ogg library
add_library(audio_codecs_ogg STATIC
    src/ogg/ogg_crc.cpp
    src/ogg/ogg_demuxer.cpp
    src/ogg/ogg_muxer.cpp
)
target_link_libraries(audio_codecs_ogg PUBLIC audio_codecs_core)

# Vorbis library
add_library(audio_codecs_vorbis STATIC
    src/vorbis/vorbis_common.cpp
    src/vorbis/vorbis_mdct.cpp
    src/vorbis/vorbis_codebook.cpp
    src/vorbis/vorbis_floor.cpp
    src/vorbis/vorbis_residue.cpp
    src/vorbis/vorbis_mapping.cpp
    src/vorbis/decoder/header_parser.cpp
    src/vorbis/encoder/setup_builder.cpp
    src/vorbis/decoder/vorbis_decoder.cpp
    src/vorbis/encoder/vorbis_encoder.cpp
)
target_link_libraries(audio_codecs_vorbis PUBLIC audio_codecs_core audio_codecs_ogg)

# AAC library
add_library(audio_codecs_aac STATIC
    src/aac/aac_tables.cpp
    src/aac/aac_mdct.cpp
    src/aac/adts_parser.cpp
    src/aac/decoder/huffman_decoder.cpp
    src/aac/decoder/requantizer.cpp
    src/aac/decoder/stereo_processor.cpp
    src/aac/decoder/tns_decoder.cpp
    src/aac/decoder/aac_decoder.cpp
    src/aac/encoder/transient_detector.cpp
    src/aac/encoder/psychoacoustic.cpp
    src/aac/encoder/quantizer.cpp
    src/aac/encoder/huffman_encoder.cpp
    src/aac/encoder/aac_encoder.cpp
)
target_link_libraries(audio_codecs_aac PUBLIC audio_codecs_core)

# MP4 library
add_library(audio_codecs_mp4 STATIC
    src/mp4/mp4_demuxer.cpp
    src/mp4/mp4_muxer.cpp
)
target_link_libraries(audio_codecs_mp4 PUBLIC audio_codecs_core)

# WAV library
add_library(audio_codecs_wav STATIC
    src/wav/g711.cpp
    src/wav/sample_converter.cpp
    src/wav/decoder/wav_parser.cpp
    src/wav/decoder/wav_decoder.cpp
    src/wav/encoder/wav_encoder.cpp
)
target_link_libraries(audio_codecs_wav PUBLIC audio_codecs_core)






enable_testing()

add_executable(test_vorbis_float tests/test_vorbis_float.cpp)
target_link_libraries(test_vorbis_float PRIVATE audio_codecs_vorbis)
add_test(NAME VorbisFloatTest COMMAND test_vorbis_float)

add_executable(test_vorbis_mdct tests/test_vorbis_mdct.cpp)
target_link_libraries(test_vorbis_mdct PRIVATE audio_codecs_vorbis)
add_test(NAME VorbisMdctTest COMMAND test_vorbis_mdct)

add_executable(test_vorbis_codebook tests/test_vorbis_codebook.cpp)
target_link_libraries(test_vorbis_codebook PRIVATE audio_codecs_vorbis)
add_test(NAME VorbisCodebookTest COMMAND test_vorbis_codebook)

add_executable(test_vorbis_floor tests/test_vorbis_floor.cpp)
target_link_libraries(test_vorbis_floor PRIVATE audio_codecs_vorbis)
add_test(NAME VorbisFloorTest COMMAND test_vorbis_floor)

add_executable(test_vorbis_residue tests/test_vorbis_residue.cpp)
target_link_libraries(test_vorbis_residue PRIVATE audio_codecs_vorbis)
add_test(NAME VorbisResidueTest COMMAND test_vorbis_residue)

add_executable(test_vorbis_headers tests/test_vorbis_headers.cpp)
target_link_libraries(test_vorbis_headers PRIVATE audio_codecs_vorbis)
add_test(NAME VorbisHeadersTest COMMAND test_vorbis_headers)

add_executable(test_vorbis_decoder tests/test_vorbis_decoder.cpp)
target_link_libraries(test_vorbis_decoder PRIVATE audio_codecs_vorbis)
add_test(NAME VorbisDecoderTest COMMAND test_vorbis_decoder)

add_executable(test_vorbis_encoder tests/test_vorbis_encoder.cpp)
target_link_libraries(test_vorbis_encoder PRIVATE audio_codecs_vorbis)
add_test(NAME VorbisEncoderTest COMMAND test_vorbis_encoder)

add_executable(test_vorbis_roundtrip tests/test_vorbis_roundtrip.cpp)
target_link_libraries(test_vorbis_roundtrip PRIVATE audio_codecs_vorbis)
add_test(NAME VorbisRoundtripTest COMMAND test_vorbis_roundtrip)

add_executable(test_ogg_crc tests/test_ogg_crc.cpp)
target_link_libraries(test_ogg_crc PRIVATE audio_codecs_ogg)
add_test(NAME OggCrcTest COMMAND test_ogg_crc)

add_executable(test_ogg_framing tests/test_ogg_framing.cpp)
target_link_libraries(test_ogg_framing PRIVATE audio_codecs_ogg)
add_test(NAME OggFramingTest COMMAND test_ogg_framing)

add_executable(test_flac_crc tests/test_flac_crc.cpp)
target_link_libraries(test_flac_crc PRIVATE audio_codecs_flac)
add_test(NAME FlacCrcTest COMMAND test_flac_crc)

add_executable(test_flac_md5 tests/test_flac_md5.cpp)
target_link_libraries(test_flac_md5 PRIVATE audio_codecs_flac)
add_test(NAME FlacMd5Test COMMAND test_flac_md5)

add_executable(test_flac_rice tests/test_flac_rice.cpp)
target_link_libraries(test_flac_rice PRIVATE audio_codecs_flac)
add_test(NAME FlacRiceTest COMMAND test_flac_rice)

add_executable(test_flac_predictors tests/test_flac_predictors.cpp)
target_link_libraries(test_flac_predictors PRIVATE audio_codecs_flac)
add_test(NAME FlacPredictorsTest COMMAND test_flac_predictors)

add_executable(test_flac_decorrelator tests/test_flac_decorrelator.cpp)
target_link_libraries(test_flac_decorrelator PRIVATE audio_codecs_flac)
add_test(NAME FlacDecorrelatorTest COMMAND test_flac_decorrelator)

add_executable(test_flac_metadata tests/test_flac_metadata.cpp)
target_link_libraries(test_flac_metadata PRIVATE audio_codecs_flac)
add_test(NAME FlacMetadataTest COMMAND test_flac_metadata)

add_executable(test_flac_decoder tests/test_flac_decoder.cpp)
target_link_libraries(test_flac_decoder PRIVATE audio_codecs_flac)
add_test(NAME FlacDecoderTest COMMAND test_flac_decoder)

add_executable(test_flac_lpc tests/test_flac_lpc.cpp)
target_link_libraries(test_flac_lpc PRIVATE audio_codecs_flac)
add_test(NAME FlacLpcTest COMMAND test_flac_lpc)

add_executable(test_flac_encoder tests/test_flac_encoder.cpp)
target_link_libraries(test_flac_encoder PRIVATE audio_codecs_flac)
add_test(NAME FlacEncoderTest COMMAND test_flac_encoder)

add_executable(test_core_types tests/test_core_types.cpp)
add_test(NAME CoreTypesTest COMMAND test_core_types)

add_executable(test_bitstream tests/test_bitstream.cpp)
target_link_libraries(test_bitstream PRIVATE audio_codecs_core)
add_test(NAME BitstreamTest COMMAND test_bitstream)

add_executable(test_fft tests/test_fft.cpp)
target_link_libraries(test_fft PRIVATE audio_codecs_core)
add_test(NAME FftTest COMMAND test_fft)

add_executable(test_mp3_tables tests/test_mp3_tables.cpp)
target_link_libraries(test_mp3_tables PRIVATE audio_codecs_mp3)
add_test(NAME Mp3TablesTest COMMAND test_mp3_tables)

add_executable(test_huffman_decoder tests/test_huffman_decoder.cpp)
target_link_libraries(test_huffman_decoder PRIVATE audio_codecs_mp3)
add_test(NAME HuffmanDecoderTest COMMAND test_huffman_decoder)

add_executable(test_requantizer tests/test_requantizer.cpp)
target_link_libraries(test_requantizer PRIVATE audio_codecs_mp3)
add_test(NAME RequantizerTest COMMAND test_requantizer)

add_executable(test_imdct tests/test_imdct.cpp)
target_link_libraries(test_imdct PRIVATE audio_codecs_mp3)
add_test(NAME ImdctTest COMMAND test_imdct)

add_executable(test_synthesis_filter tests/test_synthesis_filter.cpp)
target_link_libraries(test_synthesis_filter PRIVATE audio_codecs_mp3)
add_test(NAME SynthesisFilterTest COMMAND test_synthesis_filter)

add_executable(test_mp3_decoder tests/test_mp3_decoder.cpp)
target_link_libraries(test_mp3_decoder PRIVATE audio_codecs_mp3)
add_test(NAME Mp3DecoderTest COMMAND test_mp3_decoder)

add_executable(test_analysis_filter tests/test_analysis_filter.cpp)
target_link_libraries(test_analysis_filter PRIVATE audio_codecs_mp3)
add_test(NAME AnalysisFilterTest COMMAND test_analysis_filter)

add_executable(test_forward_mdct tests/test_forward_mdct.cpp)
target_link_libraries(test_forward_mdct PRIVATE audio_codecs_mp3)
add_test(NAME ForwardMdctTest COMMAND test_forward_mdct)

add_executable(test_psychoacoustic tests/test_psychoacoustic.cpp)
target_link_libraries(test_psychoacoustic PRIVATE audio_codecs_mp3)
add_test(NAME PsychoacousticTest COMMAND test_psychoacoustic)

add_executable(test_huffman_encoder tests/test_huffman_encoder.cpp)
target_link_libraries(test_huffman_encoder PRIVATE audio_codecs_mp3)
add_test(NAME HuffmanEncoderTest COMMAND test_huffman_encoder)

add_executable(test_mp3_encoder tests/test_mp3_encoder.cpp)
target_link_libraries(test_mp3_encoder PRIVATE audio_codecs_mp3)
add_test(NAME Mp3EncoderTest COMMAND test_mp3_encoder)

add_executable(test_roundtrip tests/test_roundtrip.cpp)
target_link_libraries(test_roundtrip PRIVATE audio_codecs_mp3)
add_test(NAME RoundtripTest COMMAND test_roundtrip)

add_executable(test_flac_roundtrip tests/test_flac_roundtrip.cpp)
target_link_libraries(test_flac_roundtrip PRIVATE audio_codecs_flac)
add_test(NAME FlacRoundtripTest COMMAND test_flac_roundtrip)

add_executable(test_aac_tables tests/test_aac_tables.cpp)
target_link_libraries(test_aac_tables PRIVATE audio_codecs_aac)
add_test(NAME AacTablesTest COMMAND test_aac_tables)

add_executable(test_aac_mdct tests/test_aac_mdct.cpp)
target_link_libraries(test_aac_mdct PRIVATE audio_codecs_aac)
add_test(NAME AacMdctTest COMMAND test_aac_mdct)

add_executable(test_adts_framing tests/test_adts_framing.cpp)
target_link_libraries(test_adts_framing PRIVATE audio_codecs_aac)
add_test(NAME AdtsFramingTest COMMAND test_adts_framing)

add_executable(test_aac_huffman tests/test_aac_huffman.cpp)
target_link_libraries(test_aac_huffman PRIVATE audio_codecs_aac)
add_test(NAME AacHuffmanTest COMMAND test_aac_huffman)

add_executable(test_aac_stereo_tns tests/test_aac_stereo_tns.cpp)
target_link_libraries(test_aac_stereo_tns PRIVATE audio_codecs_aac)
add_test(NAME AacStereoTnsTest COMMAND test_aac_stereo_tns)

add_executable(test_aac_decoder tests/test_aac_decoder.cpp)
target_link_libraries(test_aac_decoder PRIVATE audio_codecs_aac)
add_test(NAME AacDecoderTest COMMAND test_aac_decoder)

add_executable(test_aac_encoder_components tests/test_aac_encoder_components.cpp)
target_link_libraries(test_aac_encoder_components PRIVATE audio_codecs_aac)
add_test(NAME AacEncoderComponentsTest COMMAND test_aac_encoder_components)

add_executable(test_aac_encoder tests/test_aac_encoder.cpp)
target_link_libraries(test_aac_encoder PRIVATE audio_codecs_aac)
add_test(NAME AacEncoderTest COMMAND test_aac_encoder)

add_executable(test_aac_roundtrip tests/test_aac_roundtrip.cpp)
target_link_libraries(test_aac_roundtrip PRIVATE audio_codecs_aac)
add_test(NAME AacRoundtripTest COMMAND test_aac_roundtrip)

add_executable(test_mp4_container tests/test_mp4_container.cpp)
target_link_libraries(test_mp4_container PRIVATE audio_codecs_mp4 audio_codecs_aac audio_codecs_core)
add_test(NAME Mp4ContainerTest COMMAND test_mp4_container)

add_executable(test_wav_types tests/test_wav_types.cpp)
add_test(NAME WavTypesTest COMMAND test_wav_types)

add_executable(test_wav_g711 tests/test_wav_g711.cpp)
target_link_libraries(test_wav_g711 PRIVATE audio_codecs_wav)
add_test(NAME WavG711Test COMMAND test_wav_g711)

add_executable(test_wav_converters tests/test_wav_converters.cpp)
target_link_libraries(test_wav_converters PRIVATE audio_codecs_wav)
add_test(NAME WavConvertersTest COMMAND test_wav_converters)

add_executable(test_wav_parser tests/test_wav_parser.cpp)
target_link_libraries(test_wav_parser PRIVATE audio_codecs_wav)
add_test(NAME WavParserTest COMMAND test_wav_parser)

add_executable(test_wav_decoder tests/test_wav_decoder.cpp)
target_link_libraries(test_wav_decoder PRIVATE audio_codecs_wav)
add_test(NAME WavDecoderTest COMMAND test_wav_decoder)

add_executable(test_wav_encoder tests/test_wav_encoder.cpp)
target_link_libraries(test_wav_encoder PRIVATE audio_codecs_wav)
add_test(NAME WavEncoderTest COMMAND test_wav_encoder)

add_executable(test_wav_roundtrip tests/test_wav_roundtrip.cpp)
target_link_libraries(test_wav_roundtrip PRIVATE audio_codecs_wav)
add_test(NAME WavRoundtripTest COMMAND test_wav_roundtrip)

add_executable(test_aiff_types tests/test_aiff_types.cpp)
add_test(NAME AiffTypesTest COMMAND test_aiff_types)

add_executable(test_aiff_ieee80 tests/test_aiff_ieee80.cpp src/aiff/ieee80.cpp)
add_test(NAME AiffIeee80Test COMMAND test_aiff_ieee80)

# Fetch test-files repository for integration testing
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
FetchContent_Declare(
    test_files
    GIT_REPOSITORY https://github.com/ybrid/test-files.git
    GIT_TAG main
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(test_files)

add_executable(test_real_mp3s tests/test_real_mp3s.cpp)
target_compile_definitions(test_real_mp3s PRIVATE TEST_FILES_DIR="${test_files_SOURCE_DIR}")
target_link_libraries(test_real_mp3s PRIVATE audio_codecs_mp3)
add_test(NAME RealMp3sTest COMMAND test_real_mp3s)

add_executable(test_real_flacs tests/test_real_flacs.cpp)
target_compile_definitions(test_real_flacs PRIVATE TEST_FILES_DIR="${test_files_SOURCE_DIR}")
target_link_libraries(test_real_flacs PRIVATE audio_codecs_flac)
add_test(NAME RealFlacsTest COMMAND test_real_flacs)

add_executable(test_real_ogg_vorbis tests/test_real_ogg_vorbis.cpp)
target_compile_definitions(test_real_ogg_vorbis PRIVATE TEST_FILES_DIR="${test_files_SOURCE_DIR}")
target_link_libraries(test_real_ogg_vorbis PRIVATE audio_codecs_vorbis)
add_test(NAME RealOggVorbisTest COMMAND test_real_ogg_vorbis)
