#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ugripper::camera {
// Bounded parser for single-layer, non-reordered HEVC camera access units.
// Unsupported/incomplete headers yield unknown, never "no frame loss".
class MainCameraH265Pictures {
    struct Bits {
        std::vector<uint8_t> data;
        std::size_t pos = 0;
        uint32_t Read(unsigned n) {
            if (n > 32 || pos + n > data.size() * 8) throw false;
            uint32_t v = 0;
            while (n--) { v = (v << 1) | ((data[pos / 8] >> (7 - pos % 8)) & 1); ++pos; }
            return v;
        }
        void Skip(unsigned n) { while (n > 32) { Read(32); n -= 32; } Read(n); }
        uint32_t Ue() {
            unsigned zeros = 0;
            while (!Read(1)) { if (++zeros > 30) throw false; }
            return ((uint32_t{1} << zeros) - 1) + Read(zeros);
        }
    };
    struct Sps { unsigned poc_bits; bool separate; };
    struct Pps { unsigned sps; bool output; unsigned extra; };
    std::array<std::optional<Sps>, 16> sps_{};
    std::array<std::optional<Pps>, 64> pps_{};
public:
    struct Picture { uint32_t poc; unsigned poc_bits; bool idr; bool intra; std::size_t vcl_bytes = 0; };
    std::optional<Picture> Inspect(const uint8_t* data, std::size_t size) {
        std::optional<Picture> picture;
        try {
            auto start = [&](std::size_t from) {
                for (auto i = from; i + 3 <= size; ++i) {
                    if (data[i] == 0 && data[i+1] == 0 && data[i+2] == 1) return i;
                }
                return size;
            };
            for (auto begin = start(0); begin < size;) {
                const auto offset = begin + 3;
                const auto end = start(offset);
                begin = end;
                if (end < offset + 2) return std::nullopt;
                const auto type = (data[offset] >> 1) & 63;
                if (type > 31 && type != 33 && type != 34) continue;
                // No multilayer or temporal reordering support.
                if ((data[offset] & 0x81) || (data[offset+1] & 0xf8) ||
                    (data[offset+1] & 7) != 1) return std::nullopt;
                Bits bits;
                unsigned zeros = 0;
                for (auto i = offset + 2; i < end; ++i) {
                    const auto b = data[i];
                    if (zeros >= 2 && b == 3) { zeros = 0; continue; }
                    bits.data.push_back(b); zeros = b == 0 ? zeros + 1 : 0;
                }
                if (type == 33) {
                    bits.Skip(4); const auto layers = bits.Read(3); bits.Skip(1);
                    if (layers > 6) return std::nullopt;
                    bits.Skip(96); // general profile_tier_level + level_idc
                    std::array<bool, 7> profile{}, level{};
                    for (unsigned i=0;i<layers;++i) { profile[i]=bits.Read(1); level[i]=bits.Read(1); }
                    if (layers) bits.Skip(2 * (8 - layers));
                    for (unsigned i=0;i<layers;++i) { if(profile[i]) bits.Skip(88); if(level[i]) bits.Skip(8); }
                    const auto id=bits.Ue(), chroma=bits.Ue();
                    if(id>=sps_.size() || chroma>3) return std::nullopt;
                    const bool separate=chroma==3 && bits.Read(1);
                    bits.Ue(); bits.Ue();
                    if(bits.Read(1)) for(int i=0;i<4;++i) bits.Ue();
                    bits.Ue(); bits.Ue(); const auto width=bits.Ue()+4;
                    if(width>16) return std::nullopt;
                    sps_[id]=Sps{width,separate};
                } else if(type==34) {
                    const auto id=bits.Ue(), sps=bits.Ue();
                    if(id>=pps_.size() || sps>=sps_.size()) return std::nullopt;
                    bits.Skip(1); const bool output=bits.Read(1); const auto extra=bits.Read(3);
                    pps_[id]=Pps{sps,output,extra};
                } else {
                    if(!bits.Read(1)) { if(picture) picture->vcl_bytes += end-offset; continue; } // additional slice
                    if(type>=16 && type<=23) bits.Skip(1);
                    const auto id=bits.Ue();
                    if(id>=pps_.size() || !pps_[id]) return std::nullopt;
                    const auto pps=*pps_[id];
                    if(!sps_[pps.sps]) return std::nullopt;
                    const auto sps=*sps_[pps.sps];
                    bits.Skip(pps.extra); const auto slice=bits.Ue();
                    if(slice==0 || slice>2 || picture) return std::nullopt; // B or multiple pictures
                    if(pps.output) bits.Skip(1);
                    if(sps.separate) bits.Skip(2);
                    const bool idr=type==19 || type==20;
                    picture=Picture{idr ? 0 : bits.Read(sps.poc_bits),sps.poc_bits,idr,slice==2,end-offset};
                }
            }
        } catch(bool) { return std::nullopt; }
        return picture;
    }
};

class MainCameraH265Continuity {
    std::optional<MainCameraH265Pictures::Picture> previous_;
    uint32_t sequence_ = 0;
public:
    bool Observe(std::optional<MainCameraH265Pictures::Picture> current, uint32_t sequence) {
        bool continuous=false;
        if(current && previous_ && sequence == sequence_ + uint32_t{1}) {
            // IDR has no POC history across GOPs: transport continuity is the
            // only evidence here. Cadence code excludes intra-frame windows.
            continuous=current->idr || (current->poc_bits==previous_->poc_bits &&
                current->poc==((previous_->poc+1)&((uint32_t{1}<<current->poc_bits)-1)));
        }
        previous_=current; sequence_=sequence;
        return continuous;
    }
};
} // namespace ugripper::camera
