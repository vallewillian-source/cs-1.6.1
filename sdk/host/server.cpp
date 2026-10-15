// ReGameDLL adapter: facts and small bot operations, no mod-specific policy.
// GPL-3.0.
#include "precompiled.h"
#include "server.h"
#include "runtime.h"
#include "csmod_server.h"
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <dlfcn.h>
namespace {
bool initialized=false;
csmod_match_t match{};
csmod_server_services_t services[MH_MAX_MODS]{};
uint32_t round_id=0,revision=0,last_fingerprint=0;
double next_sample=0;
int caller=-1,owners[33]{};
csmod_server_api_t const *api(mh_mod_t *m) {return static_cast<const csmod_server_api_t*>(m->api);}
uint32_t player_id(CBasePlayer *p) {return (uint32_t(p->edict()->serialnumber)<<6)|uint32_t(p->entindex());}
int zone_for_site(int site) {
    if(!TheCSBots())return -1;
    for(int i=0;i<TheCSBots()->GetZoneCount();i++) {
        auto *z=TheCSBots()->GetZone(i); const char *name=z&&z->m_entity?STRING(z->m_entity->pev->target):"";
        auto n=std::strlen(name);if(n&&std::tolower(name[n-1])==(site==1?'a':'b'))return i;
    }
    return site>0&&site<=TheCSBots()->GetZoneCount()?site-1:-1;
}
int position(int site,float out[3]) {
    int i=zone_for_site(site);if(i<0)return 0;
    auto *z=TheCSBots()->GetZone(i);auto *p=TheCSBots()->GetRandomPositionInZone(z);if(!p)return 0;
    out[0]=p->x;out[1]=p->y;out[2]=p->z;return 1;
}
int get_match(csmod_match_t *out) {if(!out||!CSGameRules())return 0;*out=match;return 1;}
int command(uint32_t id,int action,int site) {
    int slot=int(id&63u);
    if(slot<1||slot>gpGlobals->maxClients||caller<0||!(MH_At(caller)->claims&MH_CLAIM_BOTS))return 0;
    auto *p=static_cast<CBasePlayer*>(UTIL_PlayerByIndex(slot));
    if(!p||!p->IsBot()||player_id(p)!=id||!p->IsAlive())return 0;
    auto *bot=static_cast<CCSBot*>(p);
    if(action==CSMOD_BOT_RESET) {
        if(owners[slot]!=caller+1)return 1;
        bot->SetDisposition(CCSBot::ENGAGE_AND_INVESTIGATE);bot->Idle();owners[slot]=0;return 1;
    }
    if(action!=CSMOD_BOT_SAVE&&action!=CSMOD_BOT_MOVE&&action!=CSMOD_BOT_GUARD)return 0;
    owners[slot]=caller+1;
    if(action==CSMOD_BOT_SAVE) {bot->SetDisposition(CCSBot::SELF_DEFENSE);return 1;}
    float xyz[3];if(!position(site,xyz))return 0;Vector point(xyz[0],xyz[1],xyz[2]);
    if(action==CSMOD_BOT_MOVE) {
        bot->SetTask(CCSBot::SEEK_AND_DESTROY);bot->SetDisposition(CCSBot::ENGAGE_AND_INVESTIGATE);bot->MoveTo(&point);
    } else if(action==CSMOD_BOT_GUARD) {
        bot->SetTask(CCSBot::GUARD_BOMB_ZONE);bot->SetDisposition(CCSBot::OPPORTUNITY_FIRE);bot->Hide(&point,20.0f,false);
    } else return 0;
    return 1;
}
void reset_owned(int index) {
    caller=index;
    for(int slot=1;slot<=gpGlobals->maxClients;slot++)if(owners[slot]==index+1) {
        auto *p=static_cast<CBasePlayer*>(UTIL_PlayerByIndex(slot));if(p&&p->IsBot())command(player_id(p),CSMOD_BOT_RESET,0);owners[slot]=0;
    }
    caller=-1;
}
void update_match() {
    double now=MH_At(0)?MH_At(0)->services.MonotonicTime():0;
    if(now<next_sample)return;
    next_sample=now+.05;
    std::memset(&match,0,sizeof(match));match.size=sizeof(match);match.round=round_id;
    std::snprintf(match.session,sizeof(match.session),"%s",MH_Session());
    std::snprintf(match.map,sizeof(match.map),"%s",STRING(gpGlobals->mapname));
    match.time=gpGlobals->time;
    if(CSGameRules()) {
        match.freeze=CSGameRules()->IsFreezePeriod();match.score[1]=CSGameRules()->m_iNumTerroristWins;match.score[2]=CSGameRules()->m_iNumCTWins;
    }
    match.bomb_planted=TheCSBots()&&TheCSBots()->IsBombPlanted();
    if(match.bomb_planted) {
        for(int i=1;i<=gpGlobals->maxEntities;i++) {
            edict_t *e=INDEXENT(i);if(!e||e->free||std::strcmp(STRING(e->v.classname),"grenade"))continue;
            auto *g=static_cast<CGrenade*>(CBaseEntity::Instance(e));if(!g||!g->m_bIsC4)continue;
            float best=1e30f;
            for(int site=1;site<=2;site++) {int z=zone_for_site(site);if(z<0)continue;
                float d=(TheCSBots()->GetZone(z)->m_center-e->v.origin).LengthSquared();if(d<best){best=d;match.bomb_site=site;}}
            break;
        }
    }
    uint32_t fingerprint=uint32_t(match.freeze+match.bomb_planted*3+match.bomb_site*7+match.score[1]*31+match.score[2]*127);
    for(int slot=1;slot<=gpGlobals->maxClients&&match.count<CSMOD_MAX_PLAYERS;slot++) {
        auto *p=static_cast<CBasePlayer*>(UTIL_PlayerByIndex(slot));if(!p||p->IsDormant()||!STRING(p->pev->netname)[0])continue;
        auto& out=match.players[match.count++];out.id=player_id(p);out.slot=slot;out.team=p->m_iTeam;
        out.alive=p->IsAlive();out.bot=p->IsBot();out.money=p->m_iAccount;
        std::snprintf(out.name,sizeof(out.name),"%s",STRING(p->pev->netname));
        for(int j=0;j<3;j++){out.origin[j]=p->pev->origin[j];out.mins[j]=p->pev->mins[j];out.maxs[j]=p->pev->maxs[j];}
        if(p->m_bHasC4)match.bomb_carried=1;
        fingerprint=fingerprint*16777619u+out.id+out.alive*33+out.team*3+out.money/1200;
    }
    if(fingerprint!=last_fingerprint){last_fingerprint=fingerprint;revision++;}
    match.revision=revision;
}
bool load(mh_mod_t *m,int index) {
    if(!m->server[0]){m->initialized=1;return true;}
    if(!m->library)m->library=MH_LoadLibrary(m,m->server);
    if(!m->library)return false;
    auto get=reinterpret_cast<CSModServerAPI_t>(dlsym(m->library,"CSModServerAPI"));
    if(!get){MH_Error(m,"Símbolo CSModServerAPI ausente");return false;}
    m->api=const_cast<csmod_server_api_t*>(get());
    if(!api(m)||api(m)->version!=CSMOD_SERVER_ABI||api(m)->size<sizeof(csmod_server_api_t)){m->api=nullptr;MH_Error(m,"ABI de servidor incompatível");return false;}
    services[index]={sizeof(csmod_server_services_t),&m->services,get_match,position,command};
    caller=index;bool ok=!api(m)->Init||api(m)->Init(&services[index]);caller=-1;
    if(!ok){
        caller=index;if(api(m)->Shutdown)api(m)->Shutdown();reset_owned(index);caller=-1;
        MH_Error(m,"Inicialização do servidor recusada");return false;
    }
    m->initialized=1;MH_Log(m,0,"Componente servidor inicializado (ABI v1)");return true;
}
}
void CSModHost_Init() {
    if(initialized||!std::getenv("CS_MOD_MANAGED"))return;
    const char *root=std::getenv("CS_MOD_ROOT"),*state=std::getenv("CS_MOD_STATE_ROOT");
    if(!root||!state)return;
    MH_Init(root,state,"server");initialized=true;std::atexit(CSModHost_Shutdown);
}
void CSModHost_Frame() {
    if(!initialized)return;
    update_match();MH_Sync();
    for(int i=0;i<MH_Count();i++) {
        auto *m=MH_At(i);
        if(m->active&&!m->desired) {caller=i;if(api(m)&&api(m)->Enable)api(m)->Enable(0);reset_owned(i);m->active=0;MH_Log(m,0,"Mod desativado no servidor");}
    }
    for(int i=0;i<MH_Count();i++) {
        auto *m=MH_At(i);
        if(m->desired&&!m->active&&!std::strncmp(m->error,"Conflito",8))m->error[0]=0;
        if(m->desired&&!m->active&&!m->error[0]) {
            if(!MH_CanEnable(m)||(!m->initialized&&!load(m,i)))continue;
            caller=i;if(api(m)&&api(m)->Enable)api(m)->Enable(1);m->active=1;MH_Log(m,0,"Mod ativo no servidor");
        }
        caller=i;if(m->active&&api(m)&&api(m)->Frame)api(m)->Frame(gpGlobals->time);caller=-1;
    }
}
void CSModHost_Round() {
    if(!initialized)return;
    round_id++;std::memset(owners,0,sizeof(owners));
    for(int i=0;i<MH_Count();i++){auto *m=MH_At(i);caller=i;if(m->active&&api(m)&&api(m)->RoundStart)api(m)->RoundStart();}caller=-1;
}
void CSModHost_Bot(CCSBot *bot) {
    if(!initialized||!bot)return;
    for(int i=0;i<MH_Count();i++){auto *m=MH_At(i);caller=i;if(m->active&&api(m)&&api(m)->BotUpdate)api(m)->BotUpdate(player_id(bot));}caller=-1;
}
void CSModHost_MapEnd() {if(initialized)for(int i=0;i<MH_Count();i++)reset_owned(i);revision++;next_sample=0;}
void CSModHost_Shutdown() {
    if(!initialized)return;
    for(int i=MH_Count()-1;i>=0;i--){auto *m=MH_At(i);caller=i;if(m->initialized&&api(m)&&api(m)->Shutdown)api(m)->Shutdown();m->active=0;}
    caller=-1;MH_Sync();MH_Shutdown();initialized=false;
}
