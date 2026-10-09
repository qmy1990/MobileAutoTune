#include "mat/offline.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <unistd.h>
#define CHECK(v) do{if(!(v)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#v);std::exit(1);}}while(0)
void put16(std::ofstream &f,uint16_t v){f.put(v&255);f.put(v>>8);}
void put32(std::ofstream &f,uint32_t v){put16(f,v&65535);put16(f,v>>16);}
void wav(const std::string &name,int channels=1){
    std::ofstream f(name,std::ios::binary);int frames=1337,bytes=frames*channels*2;
    f<<"RIFF";put32(f,36+bytes);f<<"WAVEfmt ";put32(f,16);put16(f,1);put16(f,channels);put32(f,48000);put32(f,48000*channels*2);put16(f,channels*2);put16(f,16);f<<"data";put32(f,bytes);
    for(int i=0;i<frames;++i)for(int c=0;c<channels;++c)put16(f,int16_t(std::sin(i*0.05)*4000));
}
std::vector<char> contents(const std::string &name){std::ifstream f(name,std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
int cancel(float,void*){return 1;}
int main(){
    char root[]="/tmp/mat-tests-XXXXXX";CHECK(mkdtemp(root));std::string dir=root,in=dir+"/in.wav",out=dir+"/out.wav",alias=dir+"/alias.wav";
    auto p=mat_default_parameters();p.tuning=p.reverb=p.echo=p.denoise=0;
    for(auto mode:{MAT_REALTIME,MAT_OFFLINE}){
        wav(in);CHECK(!mat_process_wav(in.c_str(),out.c_str(),&p,mode,0,nullptr,nullptr));CHECK(contents(in)==contents(out));
        CHECK(!mat_process_wav(in.c_str(),out.c_str(),&p,mode,1,nullptr,nullptr));CHECK(std::filesystem::file_size(out)==44+(1337+4*48000)*2);
        auto saved=contents(out);CHECK(mat_process_wav(in.c_str(),out.c_str(),&p,mode,0,cancel,nullptr)==-5);CHECK(saved==contents(out));
        wav(in,2);CHECK(!mat_process_wav(in.c_str(),out.c_str(),&p,mode,0,nullptr,nullptr));CHECK(std::filesystem::file_size(out)==44+1337*2);
    }
    wav(in);CHECK(mat_process_wav(in.c_str(),in.c_str(),&p,MAT_OFFLINE,0,nullptr,nullptr)==-1);
    std::filesystem::create_symlink(in,alias);CHECK(mat_process_wav(in.c_str(),alias.c_str(),&p,MAT_OFFLINE,0,nullptr,nullptr)==-1);
    std::ofstream(in,std::ios::binary)<<"RIFFjunk";CHECK(mat_process_wav(in.c_str(),out.c_str(),&p,MAT_OFFLINE,0,nullptr,nullptr)==-2);
    wav(in);std::filesystem::resize_file(in,40);CHECK(mat_process_wav(in.c_str(),out.c_str(),&p,MAT_OFFLINE,0,nullptr,nullptr)==-2);
    for(auto &entry:std::filesystem::directory_iterator(dir))CHECK(entry.path().string().find("tmp-")==std::string::npos);
    std::filesystem::remove_all(dir);std::puts("All offline WAV tests passed");
}
