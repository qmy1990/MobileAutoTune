#pragma once
#include <array>
#include <algorithm>
#include <cmath>

// Causal YIN (de Cheveigne/Kawahara): cumulative normalized difference,
// first periodic minimum, parabolic refinement, confidence and energy gate.
// Analysis at 12kHz; two-pole low-pass precedes decimation.
class PitchTracker {
    std::array<float, 1024> ring{}, diff{};
    int write=0, fill=0, decimation=0, window;
    float z1=0, z2=0;
public:
    float hz=0, confidence=0;
    explicit PitchTracker(bool offline):window(offline?1024:384){}
    void reset(){ring.fill(0);write=fill=decimation=0;z1=z2=hz=confidence=0;}
    void feed(const float *x, int n){
        for(int i=0;i<n;++i){
            z1 += 0.24f*(x[i]-z1); z2 += 0.24f*(z1-z2);
            if(++decimation==4){decimation=0;ring[write]=z2;write=(write+1)%window;fill=std::min(fill+1,window);}
        }
    }
    void analyze(){
        hz=confidence=0;
        if(fill<window)return;
        std::array<float,1024> x{};
        float energy=0,mean=0;
        for(int i=0;i<window;++i){x[i]=ring[(write+i)%window];mean+=x[i];}
        mean/=window;
        for(int i=0;i<window;++i){x[i]-=mean;energy+=x[i]*x[i];}
        if(energy/window<1e-7f)return;
        int maxLag=std::min(window/2-2,184), count=window-maxLag;
        float sum=0;diff[0]=1;
        for(int lag=1;lag<=maxLag;++lag){
            float d=0;for(int i=0;i<count;++i){float v=x[i]-x[i+lag];d+=v*v;}
            sum+=d;diff[lag]=sum>1e-12f?d*lag/sum:1;
        }
        int best=0;
        for(int lag=12;lag<maxLag-1;++lag){
            if(diff[lag]<0.18f){while(lag+1<maxLag && diff[lag+1]<diff[lag])++lag;best=lag;break;}
        }
        if(!best)return;
        float a=diff[best-1],b=diff[best],c=diff[best+1];
        float denominator=a-2*b+c;
        float offset=std::abs(denominator)>1e-9f?0.5f*(a-c)/denominator:0;
        offset=std::max(-0.5f,std::min(0.5f,offset));
        hz=12000.0f/(best+offset);confidence=1-b;
    }
};
inline float nearestScaleNote(float midi,int tonic,int scale){
    static constexpr int major[]{0,2,4,5,7,9,11};
    static constexpr int minor[]{0,2,3,5,7,8,10};
    float best=std::round(midi), distance=1e9f;
    for(int n=int(std::floor(midi))-12;n<=int(std::ceil(midi))+12;++n){
        int pc=((n-tonic)%12+12)%12;bool allowed=scale==2;
        for(int k=0;k<7;++k)allowed|=pc==(scale==1?minor[k]:major[k]);
        if(allowed && std::abs(n-midi)<distance){distance=std::abs(n-midi);best=float(n);}
    }return best;
}
