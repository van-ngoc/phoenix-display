#include "pipeline.h"
#include <cmath>
#include <algorithm>

Image turbo_quant(const Image& in, QuantLevel level) {
    Image out = in;
    float factor = (level==Q4)?16.f:(level==Q6)?8.f:(level==Q8)?4.f:(level==F16)?2.f:1.f;
    for(auto& v : out.data) v = std::round(v*factor)/factor;
    return out;
}

std::vector<uint64_t> compute_hash(const Image& in) {
    std::vector<uint64_t> hashes;
    int bw = in.width/8, bh = in.height/8;
    for(int by=0; by<8; ++by) for(int bx=0; bx<8; ++bx) {
        float sum=0; int cnt=0;
        for(int y=by*bh; y<(by+1)*bh && y<in.height; ++y)
            for(int x=bx*bw; x<(bx+1)*bw && x<in.width; ++x) {
                int idx = (y*in.width + x) * (in.data.size()/(in.width*in.height));
                sum += in.data[idx]; cnt++;
            }
        hashes.push_back((uint64_t)(sum/cnt*1000000));
    }
    return hashes;
}

std::vector<Image> multiplex(const Image& in, int streams) {
    std::vector<Image> result;
    int strip_h = in.height / streams;
    for(int s=0; s<streams; ++s) {
        Image strip; strip.width = in.width; strip.height = strip_h;
        int start_y = s*strip_h;
        for(int y=0; y<strip_h; ++y) for(int x=0; x<in.width; ++x) {
            int idx_in = ((start_y+y)*in.width + x) * (in.data.size()/(in.width*in.height));
            // int idx_out = (y*in.width + x) * (in.data.size()/(in.width*in.height)); // non utilisé
            for(size_t c=0; c<in.data.size()/(in.width*in.height); ++c)
                strip.data.push_back(in.data[idx_in+c]);
        }
        result.push_back(strip);
    }
    return result;
}

std::vector<Image> process_streams(const std::vector<Image>& streams) {
    // Factice : on renvoie les streams inchangés
    return streams;
}

Image demultiplex(const std::vector<Image>& streams) {
    if(streams.empty()) return {0,0,{}};
    int w=streams[0].width, total_h=0;
    for(auto& s: streams) total_h += s.height;
    Image out; out.width=w; out.height=total_h; out.data.resize(w*total_h*3);
    int y_off=0;
    for(auto& s: streams) {
        for(int y=0; y<s.height; ++y) for(int x=0; x<w; ++x) {
            int idx_in = (y*w+x)*3;
            int idx_out = ((y_off+y)*w+x)*3;
            for(int c=0;c<3;++c) out.data[idx_out+c] = s.data[idx_in+c];
        }
        y_off += s.height;
    }
    return out;
}

Image dehash(const std::vector<uint64_t>&, const Image& ref) { return ref; }

Image process_image(const Image& input, QuantLevel level) {
    auto quant = turbo_quant(input, level);
    auto hashes = compute_hash(quant);
    auto streams = multiplex(quant, 4);
    auto processed = process_streams(streams);
    auto demux = demultiplex(processed);
    return dehash(hashes, demux);
}