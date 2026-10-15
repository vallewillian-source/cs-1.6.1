#include "csmod_server.h"
#include "json.hpp"
#include <cassert>
#include <cstring>
#include <string>
using json=nlohmann::json;
extern "C" const csmod_server_api_t *CSModServerAPI();
namespace {
double now=100;csmod_match_t match{};std::string plan;int moves=0,resets=0;
double clock_now(){return now;}
void log(void*,int,const char*){}
void metric(void*,const char*,double){}
int snapshot(void*,const char *channel,void *out,uint32_t size,uint32_t){
    if(std::string(channel)!="plan-t"||plan.size()>size)return 0;
    memcpy(out,plan.data(),plan.size());return plan.size();
}
void publish(void*,const char*,const void*,uint32_t){}
int get_match(csmod_match_t *out){*out=match;return 1;}
int position(int,float*){return 1;}
int command(uint32_t,int action,int){if(action==CSMOD_BOT_RESET)resets++;else moves++;return 1;}
json candidate(){return {{"v",2},{"session","test"},{"map","de_dust2"},{"round",1},{"revision",1},{"team","T"},{"seq",1},{"intent","RUSH_B"},{"source","jev"},{"observed_at",now}};}
}
int main(){
    match.size=sizeof(match);strcpy(match.session,"test");strcpy(match.map,"de_dust2");match.round=1;match.revision=1;
    match.count=1;match.players[0].id=65;match.players[0].slot=1;match.players[0].team=1;match.players[0].alive=1;match.players[0].bot=1;
    csmod_services_t common={sizeof(common),nullptr,".",".","test",log,metric,snapshot,publish,clock_now};
    csmod_server_services_t host={sizeof(host),&common,get_match,position,command};auto *api=CSModServerAPI();
    assert(api->version==1&&api->Init(&host));api->Enable(1);
    auto invalid=candidate();invalid["session"]="previous";plan=invalid.dump();api->Frame(0);api->BotUpdate(65);assert(!moves);
    invalid=candidate();invalid["revision"]=0;plan=invalid.dump();now+=.2;api->Frame(0);api->BotUpdate(65);assert(!moves);
    invalid=candidate();invalid["observed_at"]=now-11;plan=invalid.dump();now+=.2;api->Frame(0);api->BotUpdate(65);assert(!moves);
    plan=candidate().dump();now+=.2;api->Frame(0);api->BotUpdate(65);assert(moves==1);
    api->BotUpdate(65);assert(moves==1); // don't repeatedly overwrite stock navigation
    plan.clear();now+=11;api->Frame(0);assert(resets==1);api->BotUpdate(65);assert(moves==1);
    auto fresh=candidate();fresh["seq"]=2;plan=fresh.dump();now+=.2;api->Frame(0);api->BotUpdate(65);assert(moves==2);
    api->Enable(0);assert(resets==2);api->BotUpdate(65);assert(moves==2);
}
