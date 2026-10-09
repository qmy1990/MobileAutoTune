#pragma once
#include <algorithm>
#include <vector>

// Single audio-thread bounded capture FIFO. No counters from the hardware:
// legacy AAudio's written-read difference can include already lost frames.
class LiveInput {
    std::vector<float> data;
    int head=0,count=0;
    double phase=0;
public:
    explicit LiveInput(int capacity):data(std::max(1,capacity),0){}
    int size() const{return count;}
    int discard(int n){n=std::clamp(n,0,count);head=(head+n)%int(data.size());count-=n;return n;}
    int push(const float *p,int n){
        int lost=0;
        if(n>int(data.size())){lost=n-int(data.size());p+=lost;n-=lost;}
        lost+=discard(std::max(0,count+n-int(data.size())));
        int tail=(head+count)%int(data.size());
        for(int i=0;i<n;++i){data[tail]=p[i];if(++tail==int(data.size()))tail=0;}
        count+=n;return lost;
    }
    int pop(float *p,int n){n=std::min(n,count);for(int i=0;i<n;++i){p[i]=data[head];if(++head==int(data.size()))head=0;}count-=n;return n;}
    // Fractional clock recovery, continuous across callbacks. Ratio is limited
    // by the caller to +/-200ppm (<0.004 semitone). Unity with zero phase is an
    // exact copy. Requires one lookahead sample only when interpolating.
    int popClocked(float *p,int n,double ratio){
        int produced=0;
        while(produced<n&&count){
            if(phase>0&&count<2)break;
            float x=data[head];
            if(phase>0){int next=head+1;if(next==int(data.size()))next=0;x+=float(phase)*(data[next]-x);}
            p[produced++]=x;phase+=ratio;
            int advance=int(phase);phase-=advance;
            discard(advance);
        }
        return produced;
    }
};
