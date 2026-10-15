#include "projection.h"
#include <cassert>
#include <cstddef>
#include <cmath>
int main(){
    static_assert(sizeof(csmod_player_t)==124,"player wire layout");
    static_assert(offsetof(csmod_match_t,time)==144,"match wire alignment");
    static_assert(sizeof(csmod_match_t)==4152,"match wire layout");
    csmod_view_t v{};v.width=1000;v.height=600;v.fov_x=90;v.fov_y=60;
    float p[3]={100,0,0},x,y;assert(xray_project(&v,p,&x,&y));assert(fabs(x-500)<.01&&fabs(y-300)<.01);
    p[1]=-100;assert(xray_project(&v,p,&x,&y));assert(fabs(x-1000)<.01);
    p[0]=-10;assert(!xray_project(&v,p,&x,&y));
    v.angles[1]=90;p[0]=0;p[1]=100;assert(xray_project(&v,p,&x,&y));assert(fabs(x-500)<.01);
    v.angles[1]=0;v.has_fov_asym=1;v.fov_asym[0]=-.7f;v.fov_asym[1]=.9f;v.fov_asym[2]=.8f;v.fov_asym[3]=-.6f;
    p[0]=100;p[1]=0;assert(xray_project(&v,p,&x,&y));assert(x>0&&x<500&&y>300&&y<600);
}
