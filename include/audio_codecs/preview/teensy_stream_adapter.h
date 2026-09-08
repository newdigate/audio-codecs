#pragma once
#include "audio_codecs/preview/preview_stream.h"

namespace audio_codecs::preview {

template <typename TFile>
class TeensyFileStream : public SeekableReader, public SeekableWriter {
public:
    explicit TeensyFileStream(TFile& file) : file_(file) {}

    size_t read(uint8_t* dest, size_t bytes) override {
        return static_cast<size_t>(file_.read(dest, bytes));
    }

    size_t write(const uint8_t* src, size_t bytes) override {
        return static_cast<size_t>(file_.write(src, bytes));
    }

    bool seek(uint64_t position) override {
        return file_.seek(position);
    }

    uint64_t position() const override {
        return file_.position();
    }

    uint64_t size() const override {
        return file_.size();
    }

    void flush() override {
        file_.flush();
    }

private:
    TFile& file_;
};

} // namespace audio_codecs::preview
