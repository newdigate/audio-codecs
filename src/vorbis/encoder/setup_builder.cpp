#include "src/vorbis/encoder/setup_builder.h"
#include "src/vorbis/vorbis_common.h"
#include <cmath>
#include <cstring>

namespace audio_codecs::vorbis {

namespace {

const uint8_t kVorbisMagic[6] = {'v', 'o', 'r', 'b', 'i', 's'};

} // namespace

size_t build_vorbis_id_header(uint8_t* out_packet, size_t max_bytes, const VorbisInfo& info) {
    if (!out_packet || max_bytes < 30) return 0;

    VorbisBitWriter writer(out_packet, max_bytes);
    writer.write_bits(VORBIS_PACKET_ID, 8);
    for (int i = 0; i < 6; ++i) {
        writer.write_bits(kVorbisMagic[i], 8);
    }

    writer.write_bits(0, 32); // Vorbis version 0
    writer.write_bits(info.channels, 8);
    writer.write_bits(info.sample_rate, 32);
    writer.write_bits(info.bitrate_maximum, 32);
    writer.write_bits(info.bitrate_nominal, 32);
    writer.write_bits(info.bitrate_minimum, 32);

    uint32_t bs0_exp = vorbis_ilog(info.blocksize_0 - 1);
    uint32_t bs1_exp = vorbis_ilog(info.blocksize_1 - 1);
    writer.write_bits(bs0_exp, 4);
    writer.write_bits(bs1_exp, 4);

    writer.write_bits(1, 1); // Framing flag
    writer.flush();

    return writer.bytes_written();
}

size_t build_vorbis_comment_header(uint8_t* out_packet, size_t max_bytes, const char* vendor) {
    if (!out_packet || max_bytes < 25) return 0;
    const char* v = vendor ? vendor : "audio_codecs vorbis v1.0.0";
    size_t v_len = std::strlen(v);

    VorbisBitWriter writer(out_packet, max_bytes);
    writer.write_bits(VORBIS_PACKET_COMMENT, 8);
    for (int i = 0; i < 6; ++i) {
        writer.write_bits(kVorbisMagic[i], 8);
    }

    writer.write_bits(static_cast<uint32_t>(v_len), 32);
    for (size_t i = 0; i < v_len; ++i) {
        writer.write_bits(static_cast<uint8_t>(v[i]), 8);
    }

    writer.write_bits(0, 32); // 0 user comments
    writer.write_bits(1, 1);  // Framing flag
    writer.flush();

    return writer.bytes_written();
}

size_t build_vorbis_setup_header(uint8_t* out_packet, size_t max_bytes, 
                                 const VorbisInfo& info, VorbisSetup& out_setup) {
    if (!out_packet || max_bytes < 1024) return 0;

    out_setup.blocksize_0 = info.blocksize_0;
    out_setup.blocksize_1 = info.blocksize_1;

    VorbisBitWriter writer(out_packet, max_bytes);
    writer.write_bits(VORBIS_PACKET_SETUP, 8);
    for (int i = 0; i < 6; ++i) {
        writer.write_bits(kVorbisMagic[i], 8);
    }

    // 1. Setup Codebooks
    out_setup.codebook_count = 4;
    writer.write_bits(out_setup.codebook_count - 1, 8);

    // Book 0: Floor 1 masterbook
    VorbisCodebook& book0 = out_setup.codebooks[0];
    book0.dimensions = 1;
    book0.entries = 1;
    book0.lengths = {1};
    book0.lookup_type = 0;
    vorbis_codebook_init_tables(book0);
    vorbis_codebook_pack(writer, book0);

    // Book 1: Floor 1 subclass values
    VorbisCodebook& book1 = out_setup.codebooks[1];
    book1.dimensions = 1;
    book1.entries = 64;
    book1.lengths.assign(64, 6);
    book1.lookup_type = 1;
    book1.min_value = 0.0f;
    book1.delta_value = 1.0f;
    book1.quant_bits = 6;
    book1.sequence_p = 0;
    book1.lookup_values = 64;
    book1.quantlist.resize(64);
    for (uint32_t i = 0; i < 64; ++i) book1.quantlist[i] = i;
    vorbis_codebook_init_tables(book1);
    vorbis_codebook_pack(writer, book1);

    // Book 2: Residue 2 classbook
    VorbisCodebook& book2 = out_setup.codebooks[2];
    book2.dimensions = 1;
    book2.entries = 1;
    book2.lengths = {1};
    book2.lookup_type = 0;
    vorbis_codebook_init_tables(book2);
    vorbis_codebook_pack(writer, book2);

    // Book 3: Residue 2 vector book (2D, 16 entries)
    VorbisCodebook& book3 = out_setup.codebooks[3];
    book3.dimensions = 2;
    book3.entries = 16;
    book3.lengths.assign(16, 4);
    book3.lookup_type = 1;
    book3.min_value = -2.0f;
    book3.delta_value = 1.0f;
    book3.quant_bits = 3;
    book3.sequence_p = 0;
    book3.lookup_values = 4; // 4^2 = 16
    book3.quantlist = {0, 1, 2, 3}; // -2, -1, 0, 1
    vorbis_codebook_init_tables(book3);
    vorbis_codebook_pack(writer, book3);

    // 2. Time domain transforms (count = 1, placeholder = 0)
    writer.write_bits(0, 6); // count - 1 = 0
    writer.write_bits(0, 16);

    // 3. Floor 1 configuration
    out_setup.floor_count = 1;
    writer.write_bits(out_setup.floor_count - 1, 6);

    VorbisFloor1Config& floor0 = out_setup.floors[0];
    floor0.partitions = 4;
    floor0.partition_class[0] = 0;
    floor0.partition_class[1] = 1;
    floor0.partition_class[2] = 1;
    floor0.partition_class[3] = 1;

    floor0.class_dimensions[0] = 1;
    floor0.class_subclasses[0] = 0;
    floor0.class_dimensions[1] = 1;
    floor0.class_subclasses[1] = 0;

    floor0.multiplier = 1;
    floor0.rangebits = 12; // up to 4096 bins
    floor0.post_list = {0, 4096, 64, 256, 1024}; // Bark-scale spaced posts
    vorbis_floor1_setup_neighbors(floor0);

    writer.write_bits(1, 16); // Floor type 1
    vorbis_floor1_pack(writer, floor0);

    // 4. Residue 2 configuration
    out_setup.residue_count = 1;
    writer.write_bits(out_setup.residue_count - 1, 6);

    VorbisResidueConfig& res0 = out_setup.residues[0];
    res0.type = 2; // Residue 2 (interleaved across channels)
    res0.begin = 0;
    res0.end = 2048;
    res0.partition_size = 32;
    res0.classifications = 1;
    res0.classbook = 2;
    for (int s = 0; s < 8; ++s) {
        res0.books[0][s] = (s == 0) ? 3 : -1;
    }

    vorbis_residue_pack(writer, res0);

    // 5. Mapping 0 configuration
    out_setup.mapping_count = 1;
    writer.write_bits(out_setup.mapping_count - 1, 6);

    VorbisMappingConfig& map0 = out_setup.mappings[0];
    map0.submaps = 1;
    if (info.channels == 2) {
        map0.coupling_steps = 1;
        map0.coupling_mag[0] = 0;
        map0.coupling_angle[0] = 1;
    } else {
        map0.coupling_steps = 0;
    }
    map0.submap_floor[0] = 0;
    map0.submap_residue[0] = 0;

    writer.write_bits(0, 16); // Mapping type 0
    vorbis_mapping_pack(writer, map0, info.channels);

    // 6. Modes (Mode 0: short block, Mode 1: long block)
    out_setup.mode_count = 2;
    writer.write_bits(out_setup.mode_count - 1, 6);

    // Mode 0: short
    out_setup.modes[0].blockflag = 0;
    out_setup.modes[0].windowtype = 0;
    out_setup.modes[0].transformtype = 0;
    out_setup.modes[0].mapping = 0;

    writer.write_bits(0, 1);  // blockflag = 0
    writer.write_bits(0, 16); // windowtype = 0
    writer.write_bits(0, 16); // transformtype = 0
    writer.write_bits(0, 8);  // mapping = 0

    // Mode 1: long
    out_setup.modes[1].blockflag = 1;
    out_setup.modes[1].windowtype = 0;
    out_setup.modes[1].transformtype = 0;
    out_setup.modes[1].mapping = 0;

    writer.write_bits(1, 1);  // blockflag = 1
    writer.write_bits(0, 16); // windowtype = 0
    writer.write_bits(0, 16); // transformtype = 0
    writer.write_bits(0, 8);  // mapping = 0

    writer.write_bits(1, 1); // Framing flag
    writer.flush();

    return writer.bytes_written();
}

} // namespace audio_codecs::vorbis
