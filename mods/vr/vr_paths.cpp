// Package paths and diagnostics are independent of OpenXR and the renderer.
#include "vr_paths.h"
#include <map>
#include <mutex>
#include <string>
namespace {const csmod_services_t *host;std::map<std::string,std::string> paths;std::mutex path_lock;}
void vr_paths_init(const csmod_services_t *services){host=services;}
const char *vr_config_path(const char *name){
    std::lock_guard<std::mutex> g(path_lock);
    auto& path=paths[std::string("config:")+name];if(path.empty())path=std::string(host->state_dir)+"/"+name;return path.c_str();
}
const char *vr_asset_path(const char *name){
    std::lock_guard<std::mutex> g(path_lock);
    auto& path=paths[std::string("asset:")+name];
    if(path.empty()){
        std::string original=name;
        const std::string legacy="mods/vr_assets/";
        if(original.rfind(legacy,0)==0)path=std::string(host->package_dir)+"/assets/"+original.substr(legacy.size());
        else if(!original.empty()&&original[0]=='/')path=original;
        else path=std::string(host->package_dir)+"/"+original;
    }
    return path.c_str();
}
void vr_diagnostic(int severity,const char *text){if(host)host->Log(host->context,severity,text);}
