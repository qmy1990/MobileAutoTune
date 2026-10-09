// Read-only Android36 capability diagnostic. Dynamic lookup preserves old OS support.
// API signatures: https://developer.android.com/ndk/reference/group/audio
#include <dlfcn.h>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
int main(){
    void *lib=dlopen("libaaudio.so",RTLD_NOW);
    if(!lib){puts("libaaudio_unavailable");return 1;}
    using Policy=int32_t(*)(int32_t,int32_t);
    auto policy=reinterpret_cast<Policy>(dlsym(lib,"AAudio_getPlatformMMapPolicy"));
    auto exclusive=reinterpret_cast<Policy>(dlsym(lib,"AAudio_getPlatformMMapExclusivePolicy"));
    printf("platform_policy_api=%d exclusive_policy_api=%d\n",policy!=nullptr,exclusive!=nullptr);
    // DeviceType values match AudioDeviceInfo; direction0=output,1=input.
    struct Device {int type,direction;const char*name;};
    for(auto d:{Device{2,0,"speaker"},Device{3,0,"wired_headset_out"},Device{4,0,"wired_headphones"},Device{3,1,"wired_headset_in"},Device{15,1,"built_in_mic"},Device{11,0,"usb_device_out"},Device{11,1,"usb_device_in"},Device{22,0,"usb_headset_out"},Device{22,1,"usb_headset_in"}})
        printf("type=%d direction=%d name=%s mmap_policy=%d exclusive_policy=%d\n",d.type,d.direction,d.name,policy?policy(d.type,d.direction):-9999,exclusive?exclusive(d.type,d.direction):-9999);
    dlclose(lib);return 0;
}
