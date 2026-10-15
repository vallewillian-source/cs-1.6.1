// Shared native host: deterministic discovery, bounded logs and background IPC.
// GPL-3.0. Packages contain trusted native code, not a sandbox.
#include "runtime.h"
#include "json.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>
#include <unistd.h>
using json = nlohmann::json;
namespace fs = std::filesystem;
namespace {
constexpr size_t LOG_LIMIT=512, QUEUE_LIMIT=4096, SNAP_LIMIT=65536;
struct Snapshot { std::vector<char> bytes; double time=0; uint32_t generation=0; };
struct ModData {
    std::deque<mh_log_t> logs, pending;
    std::map<std::string,double> metrics;
    std::map<std::string,Snapshot> snapshots, published;
    std::map<std::string,std::streamoff> offsets;
    uint32_t generation=0;
    uint32_t pending_generation=0;
    bool desired=false;
    bool manifest_error=false;
    std::string status;
    int health=0;
};
struct Ack {int active=0,desired=0,initialized=0;uint32_t generation=0;char error[256]{};};
Ack acknowledgments[MH_MAX_MODS];
struct Envelope { uint32_t magic, size, generation, reserved; double time; char session[64]; };
static_assert(sizeof(Envelope)==88, "wire envelope must match i386 and x86_64");
mh_mod_t mods[MH_MAX_MODS]{};
ModData data[MH_MAX_MODS];
int count=0;
std::string state_root, packages_root, origin, session;
std::mutex lock;
std::condition_variable wake;
std::thread worker;
bool stopped=false, managed=false, preferences_dirty=false;
uint64_t request_seq=0;
json disk_control, requests=json::array(), preferences=json::object();
int ui_capture=0,ui_panels=1,ui_selected=0;double ui_frame_ms=0;
double clock_now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
void strcopy(char *out, size_t n, const std::string& value) { std::snprintf(out,n,"%s",value.c_str()); }
bool valid_id(const std::string& s) {
    return !s.empty() && s.size()<64 && s[0]!='.' &&
        s.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_")==std::string::npos;
}
json read_json(const fs::path& path) {
    std::error_code ec;
    if(fs::file_size(path,ec)>1024*1024 || ec) return json::object();
    std::ifstream f(path);
    auto j=json::parse(f,nullptr,false);
    return j.is_object()?j:json::object();
}
void atomic_bytes(const fs::path& path,const char *bytes,size_t size) {
    fs::create_directories(path.parent_path());
    auto tmp=path.string()+"."+origin+"."+std::to_string(getpid())+".tmp";
    { std::ofstream f(tmp,std::ios::binary); f.write(bytes,size); f.close(); if(!f) throw std::runtime_error("write failed: "+tmp); }
    fs::rename(tmp,path);
}
void atomic_json(const fs::path& path,const json& j) { auto s=j.dump(2); atomic_bytes(path,s.data(),s.size()); }
int index_of(void *context) { auto *m=static_cast<mh_mod_t*>(context); return int(m-mods); }
void append_log(ModData& d,const mh_log_t& line) {
    if(d.logs.size()==LOG_LIMIT) d.logs.pop_front();
    d.logs.push_back(line);
}
void log_service(void *ctx,int severity,const char *msg) {
    if(!ctx || !msg) return;
    std::lock_guard<std::mutex> g(lock);
    auto& d=data[index_of(ctx)];
    mh_log_t line{}; line.time=clock_now(); line.severity=std::clamp(severity,0,2);
    strcopy(line.origin,sizeof(line.origin),origin); strcopy(line.text,sizeof(line.text),msg);
    append_log(d,line);
    if(d.pending.size()<QUEUE_LIMIT) d.pending.push_back(line);
    wake.notify_one();
}
void metric_service(void *ctx,const char *name,double value) {
    if(!ctx||!name||std::strlen(name)>96||!std::isfinite(value)) return;
    std::lock_guard<std::mutex> g(lock);
    auto& m=data[index_of(ctx)].metrics;
    if(m.size()<64 || m.count(name)) m[name]=value;
}
int read_snapshot(void *ctx,const char *channel,void *out,uint32_t capacity,uint32_t age) {
    if(!ctx||!out||!channel||!valid_id(channel)||!managed) return 0;
    std::lock_guard<std::mutex> g(lock);
    auto& d=data[index_of(ctx)]; auto it=d.snapshots.find(channel);
    if(it==d.snapshots.end()) return 0;
    auto& s=it->second;
    if(s.generation!=d.generation||s.bytes.size()>capacity||s.bytes.empty()||
       clock_now()<s.time || (clock_now()-s.time)*1000>age) return 0;
    std::memcpy(out,s.bytes.data(),s.bytes.size()); return int(s.bytes.size());
}
void publish_snapshot(void *ctx,const char *channel,const void *bytes,uint32_t size) {
    if(!ctx||!bytes||!channel||!valid_id(channel)||size>SNAP_LIMIT||!managed) return;
    std::lock_guard<std::mutex> g(lock);
    auto& d=data[index_of(ctx)];
    if(d.published.size()>=16&&!d.published.count(channel)) return;
    auto& s=d.published[channel]; s.time=clock_now(); s.generation=d.generation;
    s.bytes.assign(static_cast<const char*>(bytes),static_cast<const char*>(bytes)+size);
}
void rotate(const fs::path& path) {
    std::error_code ec;
    if(fs::file_size(path,ec)<4*1024*1024||ec) return;
    fs::remove(path.string()+".2",ec);
    if(fs::exists(path.string()+".1")) fs::rename(path.string()+".1",path.string()+".2");
    fs::rename(path,path.string()+".1");
}
void read_foreign_logs(int i) {
    auto dir=fs::path(mods[i].state_dir)/"logs";
    if(!fs::exists(dir)) return;
    for(auto& item:fs::directory_iterator(dir)) {
        auto name=item.path().filename().string();
        if((name!="client.jsonl"&&name!="server.jsonl"&&name!="sidecar.jsonl")||name==origin+".jsonl") continue;
        auto& pos=data[i].offsets[name];
        auto size=fs::file_size(item.path()); if(size<static_cast<uint64_t>(pos)) pos=0;
        if(pos==0&&size>256*1024) pos=std::streamoff(size-256*1024);
        std::ifstream f(item.path()); f.seekg(pos);
        std::string line; int n=0;
        while(n++<256) {
            auto before=f.tellg(); if(!std::getline(f,line)) break;
            if(f.eof()) { f.clear(); f.seekg(before); break; } // wait for a complete line
            pos=f.tellg(); if(line.size()>8192) continue;
            auto j=json::parse(line,nullptr,false);
            if(!j.is_object()) continue;
            try {
                if(j.value("session",std::string())!=session)continue;
                mh_log_t entry{};
                entry.time=j.value("time",clock_now()); entry.severity=j.value("severity",0);
                strcopy(entry.origin,sizeof(entry.origin),j.value("origin",name));
                strcopy(entry.text,sizeof(entry.text),j.value("message",std::string()));
                std::lock_guard<std::mutex> g(lock); append_log(data[i],entry);
            } catch(const json::exception&) { /* malformed foreign record */ }
        }
    }
}
void io_tick() {
    auto incoming=read_json(fs::path(state_root)/"host/control.json");
    if(incoming.value("session",std::string())!=session) incoming=json::object();
    auto status=read_json(fs::path(state_root)/"host/status.json");
    json reqs, prefs; bool save_prefs;
    {
        std::lock_guard<std::mutex> g(lock);
        if(!incoming.empty()) disk_control=incoming;
        if(status.value("session",std::string())==session)for(int i=0;i<count;i++) {
            auto item=status.value("mods",json::object()).value(mods[i].id,json::object());
            std::string text;
            auto components=item.value("components",json::object());
            for(auto it=components.begin();it!=components.end();++it)text+=it.key()+": "+(it.value()==true?"ativo":"desativado")+"  ";
            for(auto& error:item.value("errors",json::array()))if(error.is_string())text+="\n"+error.get<std::string>();
            data[i].status=text;
            data[i].health=item.value("errors",json::array()).empty()?(item.value("active",false)?1:0):-1;
            if(clock_now()-status.value("time",0.0)>2){data[i].health=-1;data[i].status="Supervisor local sem resposta";}
        }
        reqs=requests; requests=json::array();
        prefs=preferences; save_prefs=preferences_dirty; preferences_dirty=false;
    }
    for(auto& r:reqs) {
        if(managed) atomic_json(fs::path(state_root)/"host/requests"/(std::to_string(r["seq"].get<uint64_t>())+".json"),r);
        else {
            std::lock_guard<std::mutex> g(lock);
            auto id=r["id"].get<std::string>();
            disk_control["mods"][id]["enabled"]=r["enabled"];
            auto generation=disk_control["mods"][id].value("generation",0u)+1;
            disk_control["mods"][id]["generation"]=generation;
        }
    }
    if(save_prefs) atomic_json(fs::path(state_root)/"host/ui.json",prefs);
    json ack={{"session",session},{"time",clock_now()},{"mods",json::object()}};
    {
        std::lock_guard<std::mutex> g(lock);
        for(int i=0;i<count;i++) {
            try {
                auto desired=disk_control.value("mods",json::object()).value(mods[i].id,json::object());
                data[i].desired=desired.value("enabled",false);
                data[i].pending_generation=desired.value("generation",0u);
            }catch(const json::exception&){data[i].desired=false;}
            auto& a=acknowledgments[i];
            ack["mods"][mods[i].id]={{"enabled",bool(a.active)},{"desired",bool(a.desired)},
                {"initialized",bool(a.initialized)},{"error",a.error},{"generation",a.generation}};
        }
        if(origin=="client")ack["ui"]={{"capture",ui_capture},{"panels",ui_panels},{"selected",ui_selected},{"frame_ms",ui_frame_ms}};
    }
    atomic_json(fs::path(state_root)/"host"/(origin+".json"),ack);
    for(int i=0;i<count;i++) {
        std::deque<mh_log_t> pending; std::map<std::string,Snapshot> publications;
        {
            std::lock_guard<std::mutex> g(lock);
            pending.swap(data[i].pending); publications.swap(data[i].published);
        }
        auto base=fs::path(mods[i].state_dir);
        if(!pending.empty()) {
            auto path=base/"logs"/(origin+".jsonl"); fs::create_directories(path.parent_path()); rotate(path);
            std::ofstream f(path,std::ios::app);
            for(auto& l:pending) f<<json({{"time",l.time},{"session",session},{"origin",l.origin},{"severity",l.severity},{"message",l.text}}).dump()<<'\n';
        }
        for(auto& p:publications) {
            Envelope e{0x43534d31u,uint32_t(p.second.bytes.size()),p.second.generation,0,p.second.time,{}};
            strcopy(e.session,sizeof(e.session),session);
            std::vector<char> bytes(sizeof(e)+e.size); std::memcpy(bytes.data(),&e,sizeof(e));
            std::memcpy(bytes.data()+sizeof(e),p.second.bytes.data(),e.size);
            atomic_bytes(base/"channels"/(p.first+".bin"),bytes.data(),bytes.size());
        }
        auto channels=base/"channels";
        if(fs::exists(channels)) for(auto& file:fs::directory_iterator(channels)) {
            if(file.path().extension()!=".bin"||!valid_id(file.path().stem().string())) continue;
            auto file_size=fs::file_size(file.path()); if(file_size<sizeof(Envelope)||file_size>SNAP_LIMIT+sizeof(Envelope)) continue;
            std::ifstream f(file.path(),std::ios::binary); Envelope e{}; f.read(reinterpret_cast<char*>(&e),sizeof(e));
            if(e.magic!=0x43534d31u||e.size!=file_size-sizeof(e)||std::string(e.session,strnlen(e.session,64))!=session) continue;
            Snapshot s; s.bytes.resize(e.size); s.time=e.time; s.generation=e.generation;
            f.read(s.bytes.data(),e.size); if(!f) continue;
            std::lock_guard<std::mutex> g(lock);
            auto key=file.path().stem().string();
            if(data[i].snapshots.size()<16||data[i].snapshots.count(key))data[i].snapshots[key]=std::move(s);
        }
        if(origin=="client") read_foreign_logs(i);
    }
}
void run_worker() {
    while(true) {
        try { io_tick(); }
        catch(const std::exception& e) { std::fprintf(stderr,"modhost I/O: %s\n",e.what()); }
        std::unique_lock<std::mutex> g(lock);
        if(stopped) break;
        wake.wait_for(g,std::chrono::milliseconds(50),[]{return stopped;});
    }
}
}
extern "C" {
const char *MH_StateRoot() {return state_root.c_str();}
const char *MH_Session() {return session.c_str();}
int MH_Managed() {return managed;}
int MH_Count() {return count;}
mh_mod_t *MH_At(int i) {return i>=0&&i<count?&mods[i]:nullptr;}
void MH_Init(const char *root,const char *state,const char *source) {
    packages_root=fs::absolute(root).string(); state_root=fs::absolute(state).string(); origin=source;
    auto *s=getenv("CS_MOD_SESSION"); session=s?s:"standalone";
    managed=getenv("CS_MOD_MANAGED")&&std::string(getenv("CS_MOD_MANAGED"))=="1";
    fs::create_directories(fs::path(state_root)/"host");
    preferences=read_json(fs::path(state_root)/"host/ui.json");
    std::vector<fs::path> dirs;
    if(fs::exists(root)) for(auto& e:fs::directory_iterator(root)) if(e.is_directory()) dirs.push_back(e.path());
    std::sort(dirs.begin(),dirs.end());
    for(auto& path:dirs) {
        if(count==MH_MAX_MODS) break;
        auto id=path.filename().string(); if(!valid_id(id)||!fs::exists(path/"mod.json")) continue;
        auto manifest=read_json(path/"mod.json");
        auto& m=mods[count]; strcopy(m.id,sizeof(m.id),id); strcopy(m.title,sizeof(m.title),id);
        strcopy(m.package_dir,sizeof(m.package_dir),fs::absolute(path).string());
        strcopy(m.state_dir,sizeof(m.state_dir),(fs::path(state_root)/id).string());
        try {
            if(manifest.empty()||manifest.at("id")!=id||manifest.at("manifest_version")!=1) throw std::runtime_error("Manifesto inválido ou id diferente da pasta");
            strcopy(m.title,sizeof(m.title),manifest.value("name",id));
            auto components=manifest.value("components",json::object());
            strcopy(m.client,sizeof(m.client),components.value("client",std::string()));
            strcopy(m.server,sizeof(m.server),components.value("server",std::string()));
            for(auto& c:manifest.value("claims",json::array())) {
                if(c=="camera") m.claims|=MH_CLAIM_CAMERA;
                else if(c=="input") m.claims|=MH_CLAIM_INPUT;
                else if(c=="pointer") m.claims|=MH_CLAIM_POINTER;
                else if(c=="bots") m.claims|=MH_CLAIM_BOTS;
                else throw std::runtime_error("Recurso exclusivo desconhecido");
            }
            auto defaults=manifest.value("defaults",json::object());
            m.desired=defaults.value("enabled",false); m.panel=defaults.value("panel",false);
            auto p=preferences.value(id,json::object());
            m.panel=p.value("panel",bool(m.panel)); m.toggle_key=p.value("toggle_key",0); m.panel_key=p.value("panel_key",0);
            for(const auto *component:{m.client,m.server}) if(*component) {
                auto resolved=fs::weakly_canonical(path/component).string();
                if(resolved.rfind(fs::canonical(path).string()+"/",0)!=0) throw std::runtime_error("Componente fora da pasta do mod");
            }
        } catch(const std::exception& e) {strcopy(m.error,sizeof(m.error),e.what()); m.desired=0;data[count].manifest_error=true;}
        fs::create_directories(m.state_dir);
        m.services={sizeof(csmod_services_t),&m,m.package_dir,m.state_dir,session.c_str(),log_service,metric_service,read_snapshot,publish_snapshot,clock_now};
        disk_control["mods"][id]={{"enabled",bool(m.desired)},{"generation",0}};
        count++;
    }
    stopped=false; io_tick(); MH_Sync(); worker=std::thread(run_worker);
}
void MH_Sync() {
    std::lock_guard<std::mutex> g(lock);
    for(int i=0;i<count;i++) {
        auto& m=mods[i];
        auto generation=data[i].pending_generation;
        if(generation!=data[i].generation&&!data[i].manifest_error)m.error[0]=0;
        m.desired=data[i].desired&&!data[i].manifest_error;data[i].generation=generation;
        auto& a=acknowledgments[i];a.active=m.active;a.desired=m.desired;a.initialized=m.initialized;a.generation=generation;
        std::memcpy(a.error,m.error,sizeof(a.error));
    }
}
int MH_CanEnable(mh_mod_t *m) {
    for(int i=0;i<count;i++) if(&mods[i]!=m&&mods[i].active&&(mods[i].claims&m->claims)) {
        std::snprintf(m->error,sizeof(m->error),"Conflito com %.63s: recurso exclusivo em uso",mods[i].id); return 0;
    }
    return 1;
}
void MH_Request(mh_mod_t *m,int enabled) {
    if(!m) return;
    std::lock_guard<std::mutex> g(lock);
    requests.push_back({{"session",session},{"seq",++request_seq},{"id",m->id},{"enabled",bool(enabled)}});
    wake.notify_one();
}
void MH_Preferences() {
    std::lock_guard<std::mutex> g(lock);
    for(int i=0;i<count;i++) preferences[mods[i].id]={{"panel",bool(mods[i].panel)},{"toggle_key",mods[i].toggle_key},{"panel_key",mods[i].panel_key}};
    preferences_dirty=true; wake.notify_one();
}
void MH_Error(mh_mod_t *m,const char *message) {strcopy(m->error,sizeof(m->error),message); MH_Log(m,CSMOD_LOG_ERROR,message);}
void *MH_LoadLibrary(mh_mod_t *m,const char *relative) {
    auto path=fs::weakly_canonical(fs::path(m->package_dir)/relative);
    if(path.string().rfind(fs::canonical(m->package_dir).string()+"/",0)!=0) {MH_Error(m,"Componente fora da pasta do mod");return nullptr;}
    void *lib=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);
    if(!lib) MH_Error(m,dlerror());
    return lib;
}
void MH_Log(mh_mod_t *m,int severity,const char *message) {log_service(m,severity,message);}
int MH_Logs(mh_mod_t *m,mh_log_t *out,int capacity,int clear) {
    std::lock_guard<std::mutex> g(lock); auto& logs=data[index_of(m)].logs;
    if(clear) logs.clear();
    int n=std::min(capacity,int(logs.size()));
    for(int i=0;i<n;i++) out[i]=logs[logs.size()-n+i];
    return n;
}
int MH_Metrics(mh_mod_t *m,char *out,int capacity) {
    std::lock_guard<std::mutex> g(lock); std::ostringstream text;
    for(auto& p:data[index_of(m)].metrics) text<<p.first<<": "<<p.second<<"\n";
    return std::snprintf(out,capacity,"%s",text.str().c_str());
}
int MH_Status(mh_mod_t *m,char *out,int capacity) {
    std::lock_guard<std::mutex> g(lock);return std::snprintf(out,capacity,"%s",data[index_of(m)].status.c_str());
}
int MH_Health(mh_mod_t *m) {std::lock_guard<std::mutex> g(lock);return managed?data[index_of(m)].health:m->active;}
void MH_UITelemetry(int capture,int panels,int selected,double frame_ms){std::lock_guard<std::mutex> g(lock);ui_capture=capture;ui_panels=panels;ui_selected=selected;ui_frame_ms=frame_ms;}
void MH_Shutdown() {
    if(!worker.joinable()) return;
    {std::lock_guard<std::mutex> g(lock);stopped=true;}
    wake.notify_one(); worker.join();
    // Never dlclose native packages here: a runtime may still own a blocking worker.
}
}
