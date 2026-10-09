#pragma once
#include <array>
#include <cmath>
#include <algorithm>
// Pitch-synchronous dual read heads, periodic Hann crossfade. At a stable F0,
// each reset spans two pitch periods so harmonics meet at the same phase.
// Bounded 20ms history; low voices (<100Hz) use the maximum window.
class LivePitchShifter {
    std::array<float,2048> ring{};
    int pos=0;
    float phase=0, span=960;
    float read(float delay) const {
        float p=pos-delay;while(p<0)p+=2048;
        int a=int(p);float f=p-a;
        return ring[a]*(1-f)+ring[(a+1)&2047]*f;
    }
public:
    static constexpr int latency=480;
    static constexpr int maximumDelay=960;
    void reset(){ring.fill(0);pos=0;phase=0;span=960;}
    void process(const float *input,float *output,int count,float semitones,float hz){
        float ratio=std::exp2(semitones/12.f);
        float wanted=hz>0?std::clamp(96000.f/hz,160.f,960.f):span;
        for(int i=0;i<count;++i){
            ring[pos]=input[i];span+=0.001f*(wanted-span);
            float second=phase+0.5f;if(second>=1)second-=1;
            float w=0.5f-0.5f*std::cos(6.28318530718f*phase);
            output[i]=w*read(latency+(phase-0.5f)*span)+(1-w)*read(latency+(second-0.5f)*span);
            phase+=(1-ratio)/span;phase-=std::floor(phase);pos=(pos+1)&2047;
        }
    }
};
