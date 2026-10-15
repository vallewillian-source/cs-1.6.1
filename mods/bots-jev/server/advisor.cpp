// Jev policy lives in this removable package, never in ReGameDLL.
// GPL-3.0.
#include "csmod_server.h"
#include "json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using json=nlohmann::json;
namespace {
const csmod_server_services_t *host;
csmod_match_t match{};
struct Team {std::string intent="DEFAULT",source="stock";double expires=0;uint64_t seq=0;std::vector<std::string> history;};
Team teams[3];
std::map<uint32_t,std::string> applied;
double next_observation=0,next_plan=0,next_status=0;uint32_t last_revision=0;
bool enabled=false;
const char *name(int t){return t==1?"T":"CT";}
void log(int severity,const std::string& text){host->common->Log(host->common->context,severity,text.c_str());}
bool allowed(int team,const std::string& intent) {
    if(intent=="DEFAULT"||intent=="ECO_SAVE")return true;
    if(team==1)return intent=="RUSH_B"||intent=="RUSH_A_LONG"||intent=="RUSH_A_SHORT"||intent=="SPLIT_A"||intent=="HOLD_AFTER_PLANT";
    return intent=="STACK_A"||intent=="STACK_B"||intent=="RETAKE_A"||intent=="RETAKE_B";
}
void reset() {
    if(host->Match(&match))for(int i=0;i<match.count;i++)if(applied.count(match.players[i].id))host->BotCommand(match.players[i].id,CSMOD_BOT_RESET,0);
    applied.clear();for(auto& t:teams)t=Team{};next_observation=next_plan=0;last_revision=0;
}
void publish_observation(int team) {
    int alive[3]{},money[3]{},count[3]{};
    for(int i=0;i<match.count;i++){auto& p=match.players[i];if(p.team<1||p.team>2)continue;alive[p.team]+=p.alive;money[p.team]+=p.money;count[p.team]++;}
    int other=3-team;
    auto& s=teams[team];std::string hist;
    for(auto& h:s.history){if(!hist.empty())hist+=",";hist+=h;}
    json observation={{"v",2},{"session",match.session},{"map",match.map},{"round",match.round},{"revision",match.revision},
        {"observed_at",host->common->MonotonicTime()},{"team",name(team)},
        {"phase",match.freeze?"freeze":match.bomb_planted?"postplant":"live"},
        {"score_us",match.score[team]},{"score_them",match.score[other]},
        {"alive_us",alive[team]},{"alive_them",alive[other]},{"players_us",count[team]},{"players_them",count[other]},
        {"money_avg_us",count[team]?money[team]/count[team]:0},{"money_avg_them",count[other]?money[other]/count[other]:0},
        {"bomb",match.bomb_planted?"planted":match.bomb_carried?"carried":"none"},
        {"bomb_zone",match.bomb_site==1?"A":match.bomb_site==2?"B":"none"},{"history",hist}};
    auto bytes=observation.dump();host->common->PublishSnapshot(host->common->context,team==1?"observe-t":"observe-ct",bytes.data(),bytes.size());
}
void poll(int team) {
    char buf[8192];int n=host->common->ReadSnapshot(host->common->context,team==1?"plan-t":"plan-ct",buf,sizeof(buf),10000);
    if(!n)return;
    auto j=json::parse(buf,buf+n,nullptr,false);if(!j.is_object())return;
    try {
        auto& t=teams[team];auto seq=j.at("seq").get<uint64_t>();auto intent=j.at("intent").get<std::string>();
        double issued=j.at("observed_at").get<double>(),now=host->common->MonotonicTime();
        if(j.at("v")!=2||j.at("session")!=match.session||j.at("map")!=match.map||j.at("round")!=match.round||
            j.at("revision")!=match.revision||j.at("team")!=name(team)||seq<=t.seq||!std::isfinite(issued)||
            issued>now||now-issued>10||!allowed(team,intent))return;
        if(intent!=t.intent) {
            for(int i=0;i<match.count;i++)if(match.players[i].team==team&&applied.erase(match.players[i].id))host->BotCommand(match.players[i].id,CSMOD_BOT_RESET,0);
            t.history.push_back(intent);if(t.history.size()>8)t.history.erase(t.history.begin());
            log(0,std::string(name(team))+" ← "+intent+" ("+j.value("source","?")+")");
        }
        t.seq=seq;t.intent=intent;t.source=j.value("source","?");t.expires=issued+10;
    }catch(const json::exception&){log(1,"Plano inválido descartado");}
}
int init(const csmod_server_services_t *s){host=s;log(0,"Advisor Jev pronto; política e vocabulário pertencem ao pacote");return 1;}
void enable(int on){reset();enabled=on;log(0,on?"Advisor ativado":"Advisor suspenso; bots com IA original");}
void frame(double) {
    if(!enabled||!host->Match(&match))return;
    double now=host->common->MonotonicTime();
    if(std::strcmp(match.map,"de_dust2")){reset();return;} // vocabulary is map-specific
    if(now>=next_observation||last_revision!=match.revision){publish_observation(1);publish_observation(2);last_revision=match.revision;next_observation=now+.5;}
    if(now>=next_plan){poll(1);poll(2);next_plan=now+.1;}
    for(int t=1;t<=2;t++)if(teams[t].expires&&now>teams[t].expires){
        for(int i=0;i<match.count;i++)if(match.players[i].team==t&&applied.erase(match.players[i].id))host->BotCommand(match.players[i].id,CSMOD_BOT_RESET,0);
        teams[t].intent="DEFAULT";teams[t].source="stock";teams[t].expires=0;log(1,std::string(name(t))+": plano expirou; IA original restaurada");
    }
    if(now<next_status)return;
    next_status=now+.25;
    json status={{"round",match.round},{"map",match.map},{"observed_at",now},{"teams",json::array()}};
    for(int t=1;t<=2;t++)status["teams"].push_back({{"team",name(t)},{"intent",teams[t].intent},{"source",teams[t].source},{"expires",teams[t].expires}});
    auto bytes=status.dump();host->common->PublishSnapshot(host->common->context,"advisor-status",bytes.data(),bytes.size());
}
void round_start(){reset();log(0,"Nova rodada: aguardando plano válido");}
void bot_update(uint32_t id) {
    if(!enabled)return;
    const csmod_player_t *p=nullptr;for(int i=0;i<match.count;i++)if(match.players[i].id==id)p=&match.players[i];
    if(!p||!p->alive||p->team<1||p->team>2)return;
    auto& t=teams[p->team];if(t.intent=="DEFAULT"||t.expires<host->common->MonotonicTime()||applied[id]==t.intent)return;
    int action=CSMOD_BOT_MOVE,site=1;
    if(t.intent=="ECO_SAVE")action=CSMOD_BOT_SAVE;
    else if(t.intent=="STACK_A"||t.intent=="STACK_B"||t.intent=="HOLD_AFTER_PLANT")action=CSMOD_BOT_GUARD;
    if(t.intent=="RUSH_B"||t.intent=="STACK_B"||t.intent=="RETAKE_B")site=2;
    if(t.intent=="HOLD_AFTER_PLANT")site=match.bomb_site;
    if(host->BotCommand(id,action,site))applied[id]=t.intent;
}
void shutdown(){enable(0);}
const csmod_server_api_t api={CSMOD_SERVER_ABI,sizeof(api),init,enable,frame,round_start,bot_update,shutdown};
}
extern "C" __attribute__((visibility("default"))) const csmod_server_api_t *CSModServerAPI(){return &api;}
