// Example consumer: pure SDK + math, no engine headers, no renderer internals.
#include "mod_api.h"
#include "projection.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
namespace {
const csmod_services_t *host;
int terrorists=1,counter=1,names=1;float max_distance=6000;int drawn=0;
void save(){std::ofstream f(std::string(host->state_dir)+"/settings.cfg");f<<terrorists<<' '<<counter<<' '<<names<<' '<<max_distance;}
int init(const mod_engine_api_t *s){host=s->services;std::ifstream f(std::string(host->state_dir)+"/settings.cfg");f>>terrorists>>counter>>names>>max_distance;if(!std::isfinite(max_distance))max_distance=6000;max_distance=std::clamp(max_distance,100.f,12000.f);host->Log(host->context,0,"x-ray-debug pronto: caixas e nomes por view, sem depender do mod VR");return 1;}
void panel(const csmod_ui_t *ui){
    int changed=ui->Checkbox("Terroristas",&terrorists);changed|=ui->Checkbox("Contra-terroristas",&counter);changed|=ui->Checkbox("Identificação",&names);
    changed|=ui->Slider("Distância máxima",&max_distance,100,12000);if(changed)save();
    char text[128];std::snprintf(text,sizeof(text),"Jogadores destacados: %d · dados expiram em 500 ms",drawn);ui->Text(text);
}
void overlay(const csmod_view_t *view,const csmod_draw_t *draw){
    drawn=0;csmod_match_t match{};int bytes=host->ReadSnapshot(host->context,"players",&match,sizeof(match),500);
    if(bytes!=sizeof(match)||match.size!=sizeof(match)||std::strcmp(match.session,host->session)||match.count<0||match.count>32)return;
    for(int i=0;i<match.count;i++){
        const auto& p=match.players[i];if(!p.alive||p.slot==view->local_slot||p.team<1||p.team>2||(p.team==1?!terrorists:!counter))continue;
        float distance=0;for(int j=0;j<3;j++)distance+=(p.origin[j]-view->origin[j])*(p.origin[j]-view->origin[j]);if(distance>max_distance*max_distance)continue;
        float left=1e20f,right=-1e20f,top=1e20f,bottom=-1e20f;bool valid=true;
        for(int c=0;c<8;c++){float point[3],x,y;for(int j=0;j<3;j++)point[j]=p.origin[j]+((c&(1<<j))?p.maxs[j]:p.mins[j]);if(!xray_project(view,point,&x,&y)){valid=false;break;}left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);}
        if(!valid||right<0||bottom<0||left>view->width||top>view->height)continue;
        left=std::clamp(left,-1.f,float(view->width)+1);right=std::clamp(right,-1.f,float(view->width)+1);top=std::clamp(top,-1.f,float(view->height)+1);bottom=std::clamp(bottom,-1.f,float(view->height)+1);
        uint32_t color=p.team==1?0xff7594ff:0xffffd085;
        draw->Line(left,top,right,top,color);draw->Line(right,top,right,bottom,color);draw->Line(right,bottom,left,bottom,color);draw->Line(left,bottom,left,top,color);
        if(names){char label[128];std::snprintf(label,sizeof(label),"%.*s · %s #%d",63,p.name,p.bot?"bot":"jogador",p.slot);draw->Text(std::max(0.f,left),std::max(0.f,top-17),color,label);}
        drawn++;
    }
}
mod_api_t api{};
}
extern "C" __attribute__((visibility("default"))) mod_api_t *ModGetAPI(){api.api_version=MOD_API_VERSION;api.mod_name="x-ray-debug";api.size=sizeof(api);api.Mod_Init=init;api.Mod_DebugPanel=panel;api.Mod_WorldOverlay=overlay;return &api;}
