#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ugripper::camera {
// Conservative AVC parser for progressive, reference-only I/P camera AUs.
// frame_num detects encoded-reference loss, not sensor-side missing exposures.
class MainCameraH264Pictures {
    struct Bits {
        std::vector<uint8_t> data;
        size_t pos = 0;
        uint32_t Read(unsigned n) {
            if(n>32 || pos+n>data.size()*8) throw false;
            uint32_t v=0;
            while(n--) { v=(v<<1)|((data[pos/8]>>(7-pos%8))&1); ++pos; }
            return v;
        }
        uint32_t Ue() {
            unsigned n=0;
            while(!Read(1)) if(++n>30) throw false;
            return ((uint32_t{1}<<n)-1)+Read(n);
        }
    };
    struct Sps { unsigned frame_bits; };
    std::array<std::optional<Sps>,32> sps_{};
    std::array<std::optional<unsigned>,256> pps_{};
public:
    struct Picture { uint32_t frame_num; unsigned frame_bits; bool idr; bool intra; size_t vcl_bytes; };
    std::optional<Picture> Inspect(const uint8_t* data,size_t size) {
        std::optional<Picture> picture;
        try {
            auto start=[&](size_t from) {
                for(size_t i=from;i+3<=size;++i)
                    if(data[i]==0 && data[i+1]==0 && data[i+2]==1) return i;
                return size;
            };
            for(size_t begin=start(0);begin<size;) {
                const size_t offset=begin+3,end=start(offset);begin=end;
                if(offset>=end || (data[offset]&0x80)) throw false;
                const unsigned type=data[offset]&31;
                if(type!=7 && type!=8 && type!=1 && type!=5) {
                    if(type>=2 && type<=4) throw false;
                    continue;
                }
                Bits b;unsigned zeros=0;
                for(size_t i=offset+1;i<end;++i) {
                    const auto v=data[i];
                    if(zeros>=2 && v==3) {zeros=0;continue;}
                    b.data.push_back(v);zeros=v==0?zeros+1:0;
                }
                if(type==7) {
                    const auto profile=b.Read(8);b.Read(8);b.Read(8);const auto id=b.Ue();
                    if(id>=sps_.size()) throw false;
                    sps_[id].reset();
                    if(profile==100 || profile==110 || profile==122 || profile==244 || profile==44 || profile==83 || profile==86 || profile==118 || profile==128 || profile==138 || profile==139 || profile==134 || profile==135) {
                        if(b.Ue()!=1 || b.Ue()!=0 || b.Ue()!=0) throw false;
                        b.Read(1);if(b.Read(1)) throw false; // scaling matrices unsupported
                    } else if(profile!=66 && profile!=77 && profile!=88) throw false;
                    const auto width=b.Ue()+4,poc_type=b.Ue();
                    if(width>16) throw false;
                    if(poc_type==0) { if(b.Ue()>12) throw false; }
                    else if(poc_type!=2) throw false;
                    b.Ue();if(b.Read(1)) throw false; // gaps_in_frame_num not supported
                    b.Ue();b.Ue();if(!b.Read(1)) throw false; // no fields/MBAFF
                    sps_[id]=Sps{width};
                } else if(type==8) {
                    const auto id=b.Ue(),sps=b.Ue();
                    if(id>=pps_.size()) throw false;
                    pps_[id].reset();
                    if(sps>=sps_.size()) throw false;
                    b.Read(1);b.Read(1);if(b.Ue()!=0) throw false; // no slice groups
                    pps_[id]=sps;
                } else {
                    if(!(data[offset]&0x60)) throw false; // non-reference frames can repeat frame_num
                    const auto first=b.Ue(),slice=b.Ue(),id=b.Ue();
                    if(slice>9 || (slice%5!=0 && slice%5!=2) || id>=pps_.size() || !pps_[id] || !sps_[*pps_[id]]) throw false;
                    const auto width=sps_[*pps_[id]]->frame_bits;
                    Picture p{b.Read(width),width,type==5,slice%5==2,end-offset};
                    if(first==0) {if(picture) throw false;picture=p;}
                    else {
                        if(!picture || p.frame_num!=picture->frame_num || p.idr!=picture->idr || p.intra!=picture->intra || p.frame_bits!=picture->frame_bits) throw false;
                        picture->vcl_bytes+=p.vcl_bytes;
                    }
                }
            }
        } catch(bool) { sps_={};pps_={};return std::nullopt; }
        return picture;
    }
};
class MainCameraH264Continuity {
    std::optional<MainCameraH264Pictures::Picture> previous_;
    uint32_t sequence_=0;
public:
    bool Observe(std::optional<MainCameraH264Pictures::Picture> current,uint32_t sequence) {
        bool continuous=false;
        if(current && previous_ && sequence==sequence_+uint32_t{1}) {
            // IDR resets frame_num; only transport continuity crosses that boundary.
            continuous=current->idr || (current->frame_bits==previous_->frame_bits &&
                current->frame_num==((previous_->frame_num+1)&((uint32_t{1}<<current->frame_bits)-1)));
        }
        previous_=current;sequence_=sequence;return continuous;
    }
};
} // namespace ugripper::camera
