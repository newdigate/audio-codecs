#include "src/vorbis/vorbis_codebook.h"
#include "src/vorbis/vorbis_bitstream.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace audio_codecs::vorbis;

    VorbisCodebook book;
    book.dimensions = 2;
    book.entries = 4;
    book.lengths = {2, 2, 2, 2}; // 4 equal codewords: 00, 01, 10, 11
    book.lookup_type = 1;
    book.min_value = 0.0f;
    book.delta_value = 1.0f;
    book.quant_bits = 4;
    book.sequence_p = 0;
    book.lookup_values = 2; // 2^2 = 4 entries
    book.quantlist = {0, 1};

    assert(vorbis_codebook_init_tables(book));
    assert(!book.tree.empty());
    assert(book.valuelist.size() == 8);

    // Pack codebook into bitstream
    uint8_t buffer[256];
    VorbisBitWriter writer(buffer, sizeof(buffer));
    vorbis_codebook_pack(writer, book);
    writer.flush();

    // Read back and unpack
    VorbisBitReader reader(buffer, writer.bytes_written());
    VorbisCodebook unpacked;
    assert(vorbis_codebook_unpack(reader, unpacked));
    assert(unpacked.dimensions == 2);
    assert(unpacked.entries == 4);
    assert(unpacked.lookup_type == 1);
    assert(std::fabs(unpacked.min_value - 0.0f) < 0.01f);
    assert(std::fabs(unpacked.delta_value - 1.0f) < 0.01f);

    // Test vector nearest-neighbor search
    float target[2] = {0.0f, 0.0f};
    int best = vorbis_book_find_best(book, target);
    assert(best == 0);

    float target1[2] = {1.0f, 1.0f};
    int best1 = vorbis_book_find_best(book, target1);
    assert(best1 == 3);

    std::cout << "Vorbis codebook test passed!\n";
    return 0;
}
