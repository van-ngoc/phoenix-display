#pragma once
#include <vector>
#include <cstdint>

enum QuantLevel { Q4, Q6, Q8, F16, F32 };

struct Image {
    int width, height;
    std::vector<float> data;
};

Image turbo_quant(const Image& in, QuantLevel level);
std::vector<uint64_t> compute_hash(const Image& in);
std::vector<Image> multiplex(const Image& in, int streams);
std::vector<Image> process_streams(const std::vector<Image>& streams);
Image demultiplex(const std::vector<Image>& streams);
Image dehash(const std::vector<uint64_t>& hashes, const Image& ref);
Image process_image(const Image& input, QuantLevel level);
