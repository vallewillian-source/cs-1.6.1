#ifndef XRAY_PROJECTION_H
#define XRAY_PROJECTION_H
#include "csmod_services.h"
#include <math.h>
/* GoldSrc angles: pitch down, yaw left, roll around forward. View-local pixels. */
static inline int xray_project(const csmod_view_t *v,const float point[3],float *x,float *y){
    const float r=.017453292519943295f;
    float sp=sinf(v->angles[0]*r),cp=cosf(v->angles[0]*r),sy=sinf(v->angles[1]*r),cy=cosf(v->angles[1]*r);
    float sr=sinf(v->angles[2]*r),cr=cosf(v->angles[2]*r);
    float f[3]={cp*cy,cp*sy,-sp};
    float right[3]={-sr*sp*cy+cr*sy,-sr*sp*sy-cr*cy,-sr*cp};
    float up[3]={cr*sp*cy+sr*sy,cr*sp*sy-sr*cy,cr*cp};
    float z=0,h=0,k=0;for(int i=0;i<3;i++){float d=point[i]-v->origin[i];z+=d*f[i];h+=d*right[i];k+=d*up[i];}
    if(z<=.1f)return 0;
    if(v->has_fov_asym){
        float l=tanf(v->fov_asym[0]),rr=tanf(v->fov_asym[1]),u=tanf(v->fov_asym[2]),b=tanf(v->fov_asym[3]);
        if(rr<=l||u<=b)return 0;
        *x=(h/z-l)/(rr-l)*v->width;*y=(u-k/z)/(u-b)*v->height;
    }else{
        *x=(.5f+.5f*h/(z*tanf(v->fov_x*r*.5f)))*v->width;
        *y=(.5f-.5f*k/(z*tanf(v->fov_y*r*.5f)))*v->height;
    }
    return isfinite(*x)&&isfinite(*y);
}
#endif
