#include "csmod_server.h"
namespace {
const csmod_server_services_t *host;double next=0;
int init(const csmod_server_services_t *s){host=s;return 1;}
void enable(int on){next=0;host->common->Log(host->common->context,0,on?"Snapshots de jogadores ativados (20 Hz, independentes do PVS)":"Snapshots de jogadores desativados");}
void frame(double){
    double now=host->common->MonotonicTime();if(now<next)return;next=now+.05;
    csmod_match_t match{};if(host->Match(&match))host->common->PublishSnapshot(host->common->context,"players",&match,sizeof(match));
}
const csmod_server_api_t api={CSMOD_SERVER_ABI,sizeof(api),init,enable,frame,nullptr,nullptr,nullptr};
}
extern "C" __attribute__((visibility("default"))) const csmod_server_api_t *CSModServerAPI(){return &api;}
