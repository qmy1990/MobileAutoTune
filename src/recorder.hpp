#pragma once
#include <array>
#include <atomic>
#include <thread>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <memory>
// One audio producer, one disk consumer. Disk I/O never runs in the callback.
// Stop the audio stream before stop(). Files retain pipeline latency for monitoring
// recordings; use the raw WAV with mat_process_wav for aligned offline export.
class Recorder {
    struct Pair {float raw,wet;};
    static constexpr uint32_t capacity=1<<18;
    std::unique_ptr<Pair[]> ring{new Pair[capacity]};
    std::atomic<uint32_t> write{0},read{0};
    std::atomic<bool> done{false};
    std::thread worker;
    FILE *raw=nullptr,*wet=nullptr;
    uint64_t samples=0;
    static void p16(FILE*f,uint16_t n){unsigned char b[]{(unsigned char)n,(unsigned char)(n>>8)};fwrite(b,1,2,f);}
    static void p32(FILE*f,uint32_t n){p16(f,n&65535);p16(f,n>>16);}
    static void header(FILE*f,uint32_t size){fseek(f,0,SEEK_SET);fwrite("RIFF",1,4,f);p32(f,36+size);fwrite("WAVEfmt ",1,8,f);p32(f,16);p16(f,1);p16(f,1);p32(f,48000);p32(f,96000);p16(f,2);p16(f,16);fwrite("data",1,4,f);p32(f,size);}
    static int16_t pcm(float x){return int16_t(std::lrint(std::clamp(x,-1.f,0.999969f)*32768.f));}
public:
    std::atomic<uint64_t> dropped{0};
    std::atomic<int> error{0};
    bool start(const char *rawPath,const char *wetPath){
        if(worker.joinable())return false;
        raw=fopen(rawPath,"wb");wet=fopen(wetPath,"wb");
        if(!raw||!wet){if(raw)fclose(raw);if(wet)fclose(wet);raw=wet=nullptr;return false;}
        header(raw,0);header(wet,0);write=read=0;done=false;samples=0;dropped=0;error=0;
        worker=std::thread([this]{
            std::array<unsigned char,2048> a{},b{};
            while(!done.load()||read.load()!=write.load()){
                uint32_t r=read.load(std::memory_order_relaxed),w=write.load(std::memory_order_acquire);
                uint32_t n=std::min<uint32_t>(1024,w-r);
                if(!n){std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}
                for(uint32_t i=0;i<n;++i){auto value=ring[(r+i)&(capacity-1)];int16_t x=pcm(value.raw),y=pcm(value.wet);a[2*i]=x&255;a[2*i+1]=(uint16_t(x)>>8)&255;b[2*i]=y&255;b[2*i+1]=(uint16_t(y)>>8)&255;}
                if(samples+n>(UINT32_MAX-36)/2){error=1;}
                else if(!error.load()){
                    if(fwrite(a.data(),2,n,raw)!=n || fwrite(b.data(),2,n,wet)!=n)error=1;
                    samples+=n;
                }
                read.store(r+n,std::memory_order_release);
            }
        });return true;
    }
    void push(const float *input,const float *output,int n){
        uint32_t w=write.load(std::memory_order_relaxed),r=read.load(std::memory_order_acquire);
        if(uint32_t(n)>capacity-(w-r)){dropped.fetch_add(n);return;}
        for(int i=0;i<n;++i)ring[(w+i)&(capacity-1)]={input[i],output[i]};
        write.store(w+n,std::memory_order_release);
    }
    void stop(){
        if(worker.joinable()){done=true;worker.join();}
        if(raw){header(raw,uint32_t(samples*2));bool failed=ferror(raw);if(fclose(raw)||failed)error=1;raw=nullptr;}
        if(wet){header(wet,uint32_t(samples*2));bool failed=ferror(wet);if(fclose(wet)||failed)error=1;wet=nullptr;}
    }
    ~Recorder(){stop();}
};
