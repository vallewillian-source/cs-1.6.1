#include "runtime.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <cstdlib>
#include <cstring>
namespace fs=std::filesystem;
int main(int argc,char **argv){
    assert(argc==2);auto base=fs::path(argv[1]);auto packages=base/"packages",state=base/"state";
    for(auto id:{"alpha","beta"}){fs::create_directories(packages/id);std::ofstream f(packages/id/"mod.json");
        f<<"{\"manifest_version\":1,\"id\":\""<<id<<"\",\"claims\":[\"camera\"],\"defaults\":{\"enabled\":true}}";}
    fs::create_directories(packages/"assets"); // asset directories are not packages
    setenv("CS_MOD_MANAGED","1",1);setenv("CS_MOD_SESSION","contract-test",1);
    MH_Init(packages.c_str(),state.c_str(),"client");assert(MH_Count()==2);
    auto *a=MH_At(0),*b=MH_At(1);assert(!strcmp(a->id,"alpha"));
    a->active=1;assert(!MH_CanEnable(b));assert(strstr(b->error,"alpha"));
    a->active=0;assert(MH_CanEnable(b));
    const char text[]="snapshot";a->services.PublishSnapshot(a,"sample",text,sizeof(text));
    for(int i=0;i<1000;i++)MH_Log(a,0,"bounded log");
    char out[64]{};int n=0;
    for(int i=0;i<50&&!n;i++){std::this_thread::sleep_for(std::chrono::milliseconds(25));n=a->services.ReadSnapshot(a,"sample",out,sizeof(out),1000);}
    assert(n==sizeof(text)&&!strcmp(out,text));assert(fs::exists(state/"alpha/channels/sample.bin"));
    mh_log_t logs[512];assert(MH_Logs(a,logs,512,0)==512);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));assert(!a->services.ReadSnapshot(a,"sample",out,sizeof(out),1));
    MH_Log(a,2,"shutdown flush");MH_Shutdown();assert(fs::exists(state/"alpha/logs/client.jsonl"));
}
