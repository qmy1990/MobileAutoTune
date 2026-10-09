#pragma once
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
class Delay {
    std::vector<float> data; size_t pos=0;
public:
    explicit Delay(size_t size):data(std::max<size_t>(1,size),0){}
    void reset(){std::fill(data.begin(),data.end(),0);pos=0;}
    float tick(float x){float y=data[pos];data[pos]=x;if(++pos==data.size())pos=0;return y;}
};
class Effects {
    static constexpr std::array<int,8> lengths{1493,1601,1867,2053,2381,2671,2999,3253};
    std::array<std::vector<float>,8> lines;
    std::array<int,8> positions{};
    std::array<float,8> filters{},feedbackGains{};
    std::vector<float> echoBuffer;
    int echoPos=0;
    float echoDelay=13440;
public:
    Effects():echoBuffer(38402,0){for(int k=0;k<8;++k){lines[k].resize(lengths[k],0);feedbackGains[k]=std::pow(0.001f,lengths[k]/(48000.f*1.8f));}}
    void reset(){for(auto &l:lines)std::fill(l.begin(),l.end(),0);positions.fill(0);filters.fill(0);std::fill(echoBuffer.begin(),echoBuffer.end(),0);echoPos=0;echoDelay=13440;}
    float tick(float x,float reverb,float echo,float delayMs){
        // Orthogonal Householder feedback matrix, damped for a smooth diffuse tail.
        float sum=0;std::array<float,8> reads{};
        for(int k=0;k<8;++k){reads[k]=lines[k][positions[k]];filters[k]+=0.30f*(reads[k]-filters[k]);sum+=filters[k];}
        float wet=0;
        for(int k=0;k<8;++k){
            float feedback=feedbackGains[k];
            lines[k][positions[k]]=0.22f*x+feedback*(filters[k]-0.25f*sum);
            wet+=(k&1?-1:1)*reads[k];if(++positions[k]==lengths[k])positions[k]=0;
        }
        // Fractional echo delay; bounded read-pointer motion avoids abrupt clicks.
        float wanted=delayMs*48.f;
        echoDelay+=std::max(-0.02f,std::min(0.02f,wanted-echoDelay));
        float p=echoPos-echoDelay; if(p<0)p+=echoBuffer.size();
        int a=int(p);float f=p-a;
        float e=echoBuffer[a]*(1-f)+echoBuffer[(a+1)%echoBuffer.size()]*f;
        echoBuffer[echoPos]=x+0.42f*e;
        if(++echoPos==int(echoBuffer.size()))echoPos=0;
        float y=x+0.32f*reverb*wet+0.65f*echo*e;
        // Transparent below -2 dBFS, instantaneous bounded safety soft knee.
        float magnitude=std::abs(y);
        if(magnitude>0.8f)y=std::copysign(0.8f+0.2f*std::tanh((magnitude-0.8f)/0.2f),y);
        return y;
    }
};
