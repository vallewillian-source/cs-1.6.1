// Run the actual package overlay against copied server facts, without an engine.
#include "../../mods/x-ray-debug/client.cpp"
#include <cassert>
namespace {
csmod_match_t facts{};
bool expired=false;
int lines=0,labels=0;
float first_height=0,last_y=0;
int read_facts(void *,const char *,void *out,uint32_t capacity,uint32_t age) {
    assert(age==500&&capacity==sizeof(facts));
    if(expired)return 0;
    std::memcpy(out,&facts,sizeof(facts));return sizeof(facts);
}
void log_message(void *,int,const char *) {}
void line(float,float y1,float,float y2,uint32_t) {
    if(lines==1)first_height=std::fabs(y2-y1);
    last_y=y2;lines++;
}
void label(float,float,uint32_t,const char *) {labels++;}
void render(const csmod_view_t& view) {
    lines=labels=0;csmod_draw_t draw{sizeof(draw),line,label};overlay(&view,&draw);
}
}
int main() {
    csmod_services_t services{};services.session="test";services.state_dir="/nonexistent";
    services.ReadSnapshot=read_facts;services.Log=log_message;
    mod_engine_api_t engine{};engine.services=&services;assert(init(&engine));
    facts.size=sizeof(facts);std::strcpy(facts.session,"test");facts.count=4;
    for(int i=0;i<4;i++) {
        auto& p=facts.players[i];p.slot=i+1;p.team=1;p.alive=1;
        p.origin[0]=100;p.mins[0]=p.mins[1]=-16;p.maxs[0]=p.maxs[1]=16;
        p.mins[2]=-36;p.maxs[2]=36;std::strcpy(p.name,"player");
    }
    facts.players[2].alive=0;facts.players[3].origin[0]=-100;
    csmod_view_t view{};view.width=1280;view.height=720;view.fov_x=90;view.fov_y=60;view.local_slot=1;
    render(view);assert(lines==4&&labels==1); // self, dead and behind camera excluded
    float standing=first_height;
    facts.players[1].mins[2]=-18;facts.players[1].maxs[2]=18;
    render(view);assert(lines==4&&first_height<standing*.6f); // crouched bounds from server
    names=0;render(view);assert(lines==4&&labels==0);
    terrorists=0;render(view);assert(lines==0);
    terrorists=1;expired=true;render(view);assert(lines==0);
    expired=false;std::strcpy(facts.session,"previous-session");render(view);assert(lines==0);
    std::strcpy(facts.session,"test");facts.count=1;render(view);assert(lines==0); // disconnected removed
}
