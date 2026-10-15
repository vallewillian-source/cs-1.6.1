/* Xash3D adapter. Generic hooks only; package policy lives outside the engine.
 * GPL-3.0. Installed packages are discovered once; activation is per-frame.
 */
#include "common.h"
#include "client.h"
#include "input.h"
#include "modloader.h"
#include "runtime.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <stddef.h>

static mod_engine_api_t engines[MH_MAX_MODS];
static mod_gl_ctx_t video;
static mod_input_sample_t sample;
static qboolean initialized, input_valid, clearing_keys;
static int capture_previous;

static mod_api_t *API( mh_mod_t *m ) { return (mod_api_t *)m->api; }
static mh_mod_t *Owner( unsigned cap )
{
    int i;
    for( i=0; i<MH_Count(); i++ ) {
        mh_mod_t *m=MH_At(i);
        if( m->active && API(m) && (API(m)->capabilities&cap) &&
            (cap!=MOD_CAP_HUD || (m->claims&MH_CLAIM_POINTER))) return m;
    }
    return NULL;
}
static void Info( void )
{
    int i;
    Con_Printf("modui: capture=%d\n",ModUI_Capturing());
    for( i=0; i<MH_Count(); i++ ) {
        mh_mod_t *m=MH_At(i);
        Con_Printf("mod %s: %s panel=%d %s\n",m->id,m->active?"ativo":"desativado",m->panel,m->error);
    }
}
static void Toggle( void )
{
    int i;
    for( i=0; i<MH_Count(); i++ ) if(!Q_strcmp(MH_At(i)->id,Cmd_Argv(1))) {
        MH_Request(MH_At(i),Cmd_Argc()>2?Q_atoi(Cmd_Argv(2)):!MH_At(i)->desired); return;
    }
    Con_Printf("Uso: mod_toggle <id> [0|1]\n");
}
static void Panel( void )
{
    int i;
    for(i=0;i<MH_Count();i++) if(!Q_strcmp(MH_At(i)->id,Cmd_Argv(1))) {
        MH_At(i)->panel=Cmd_Argc()>2?Q_atoi(Cmd_Argv(2)):!MH_At(i)->panel;
        MH_Preferences(); return;
    }
}
qboolean Mods_Active(void) { return Owner(MOD_CAP_DISPLAY)!=NULL; }
const char *Mods_Name(void) { mh_mod_t *m=Owner(MOD_CAP_DISPLAY); return m?m->id:""; }
void Mods_Init(void)
{
    const char *root=getenv("CS_MOD_ROOT"), *state=getenv("CS_MOD_STATE_ROOT");
    char name[64]; int i;
    if(Sys_CheckParm("-nomods")) return;
    MH_Init(root?root:"mods",state?state:"mod-state","client"); initialized=true;
    if(video.window&&video.gl_context)ModUI_Init(video.window,video.gl_context);
    Cmd_AddCommand("mod_info",Info,"list installed mods and state");
    Cmd_AddCommand("mod_toggle",Toggle,"enable/disable a package by id");
    Cmd_AddCommand("mod_panel",Panel,"show/hide a mod debug panel");
    if(Sys_GetParmFromCmdLine("-mod",name)) for(i=0;i<MH_Count();i++)
        MH_Request(MH_At(i),!Q_strcmp(MH_At(i)->id,name));
}
static int Load(mh_mod_t *m,int index)
{
    ModGetAPI_t get;
    if(!m->client[0]) {m->initialized=1;return 1;}
    if(!m->library) m->library=MH_LoadLibrary(m,m->client);
    if(!m->library) return 0;
    get=(ModGetAPI_t)dlsym(m->library,"ModGetAPI");
    if(!get) {MH_Error(m,"Símbolo ModGetAPI ausente");return 0;}
    m->api=get();
    if(!API(m)||API(m)->api_version!=MOD_API_VERSION) {m->api=NULL;MH_Error(m,"ABI incompatível: recompile com o SDK v9");return 0;}
    if(API(m)->size<sizeof(mod_api_t)) {m->api=NULL;MH_Error(m,"Estrutura da API incompleta");return 0;}
    if(!API(m)->mod_name || Q_strcmp(API(m)->mod_name,m->id)) {m->api=NULL;MH_Error(m,"Identidade da biblioteca diferente do manifesto");return 0;}
    if(((API(m)->capabilities&MOD_CAP_DISPLAY)&&!(m->claims&MH_CLAIM_CAMERA)) ||
       ((API(m)->capabilities&MOD_CAP_INPUT)&&!(m->claims&MH_CLAIM_INPUT)) ||
       (API(m)->Mod_Hud&& (API(m)->capabilities&MOD_CAP_HUD)&&!(m->claims&MH_CLAIM_POINTER))) {
        m->api=NULL;MH_Error(m,"Hook exclusivo sem claim no manifesto");return 0;
    }
    engines[index].size=sizeof(mod_engine_api_t);
    engines[index].Printf=Con_Printf; engines[index].DoubleTime=Platform_DoubleTime;
    engines[index].engine_version=XASH_VERSION; engines[index].services=&m->services;
    if(API(m)->Mod_Init&&!API(m)->Mod_Init(&engines[index])) {
        if(API(m)->Mod_Shutdown)API(m)->Mod_Shutdown();
        MH_Error(m,"Inicialização recusada pelo mod");return 0;
    }
    m->initialized=1;MH_Log(m,0,"Componente cliente inicializado (ABI v9)");return 1;
}
void Mods_FramePre(double time)
{
    int i,capture;
    if(!initialized) return;
    MH_Sync();
    for(i=0;i<MH_Count();i++) {
        mh_mod_t *m=MH_At(i);
        if(m->active&&!m->desired) {
            if(API(m)&&API(m)->Mod_Enable) API(m)->Mod_Enable(0);
            m->active=0; input_valid=false; IN_SetVirtualCursor(false,0,0);
            MH_Log(m,0,"Mod desativado");
        }
    }
    for(i=0;i<MH_Count();i++) {
        mh_mod_t *m=MH_At(i); mod_api_t *a;
        if(m->desired&&!m->active&&!strncmp(m->error,"Conflito",8))m->error[0]=0;
        if(m->desired&&!m->active&&(!m->client[0]||video.gl_context)) {
            if(m->error[0]) continue;
            if(!MH_CanEnable(m)||(!m->initialized&&!Load(m,i))) continue;
            if(API(m)&&API(m)->Mod_Enable&&!API(m)->Mod_Enable(1)) {MH_Error(m,"Ativação recusada pelo mod");continue;}
            m->active=1;MH_Log(m,0,"Mod ativado");
        }
        a=API(m);
        if(m->active&&a&&a->Mod_FramePre) a->Mod_FramePre(time);
    }
    capture=ModUI_Capturing();
    if(capture!=capture_previous) {
        clearing_keys=true; Key_ClearStates(); clearing_keys=false;
        IN_ClearMouseState(); capture_previous=capture;
        if(capture) IN_DeactivateMouse();
        else if(cls.key_dest==key_game) IN_ActivateMouse();
    }
}
void Mods_FramePost(double time)
{
    int i;for(i=0;i<MH_Count();i++) {mh_mod_t *m=MH_At(i);if(m->active&&API(m)&&API(m)->Mod_FramePost) API(m)->Mod_FramePost(time);}
}
void Mods_Shutdown(void)
{
    int i;if(!initialized)return;
    for(i=MH_Count()-1;i>=0;i--) {mh_mod_t *m=MH_At(i);if(API(m)&&m->initialized) {
        if(m->active&&API(m)->Mod_Enable) API(m)->Mod_Enable(0);
        if(API(m)->Mod_Shutdown) API(m)->Mod_Shutdown();
        m->active=0;
    }}
    ModUI_Shutdown();MH_Sync();MH_Shutdown();initialized=false;
}
void Mods_Input(void)
{
    mh_mod_t *m=Owner(MOD_CAP_INPUT);input_valid=false;
    if(!m||!API(m)->Mod_Input||ModUI_Capturing())return;
    memset(&sample,0,sizeof(sample));API(m)->Mod_Input(&sample);input_valid=true;
}
qboolean Mods_GetInput(mod_input_sample_t *out) {if(!input_valid||ModUI_Capturing())return false;*out=sample;return true;}
void Mods_Camera(ref_viewpass_t *rvp,int eye)
{
    mh_mod_t *m=Owner(MOD_CAP_DISPLAY);mod_view_t v;int old[4];
    if(!m||!API(m)->Mod_Camera)return;
    memset(&v,0,sizeof(v));v.eye=eye;VectorCopy(rvp->vieworigin,v.vieworigin);VectorCopy(rvp->viewangles,v.viewangles);
    v.fov_x=rvp->fov_x;v.fov_y=rvp->fov_y;memcpy(v.viewport,rvp->viewport,sizeof(old));memcpy(old,v.viewport,sizeof(old));
    API(m)->Mod_Camera(&v);VectorCopy(v.vieworigin,rvp->vieworigin);VectorCopy(v.viewangles,rvp->viewangles);
    rvp->fov_x=v.fov_x;rvp->fov_y=v.fov_y;
    if(v.has_fov_asym) {memcpy(rvp->fov_asym,v.fov_asym,sizeof(v.fov_asym));SetBits(rvp->flags,RF_FOV_ASYM);}
    if(memcmp(v.viewport,old,sizeof(old))) {memcpy(rvp->viewport,v.viewport,sizeof(old));SetBits(rvp->flags,RF_VIEWPORT_ABSOLUTE);}
}
int Mods_EyeCount(void) {mh_mod_t *m=Owner(MOD_CAP_DISPLAY);return m&&API(m)->Mod_EyeCount?bound(1,API(m)->Mod_EyeCount(),2):1;}
void Mods_AfterEyes(void) {mh_mod_t *m=Owner(MOD_CAP_DISPLAY);if(m&&API(m)->Mod_AfterEyes)API(m)->Mod_AfterEyes();}
qboolean Mods_ViewModel(mod_viewmodel_t *v) {mh_mod_t *m=Owner(MOD_CAP_DISPLAY);if(!m||!API(m)->Mod_ViewModel)return false;API(m)->Mod_ViewModel(v);return true;}
qboolean Mods_Key(int key,int down)
{
    int i;if(!initialized||clearing_keys)return false;
    if(ModUI_Key(key,down))return true;
    for(i=0;i<MH_Count();i++) {mh_mod_t *m=MH_At(i);if(m->active&&API(m)&&(API(m)->capabilities&MOD_CAP_KEYS)&&API(m)->Mod_Key&&API(m)->Mod_Key(key,down))return true;}
    return false;
}
void Mods_VideoInit(void *window,void *context) {video.window=window;video.gl_context=context;if(initialized)ModUI_Init(window,context);}
void Mods_BeginFrame(void) {mh_mod_t *m=Owner(MOD_CAP_DISPLAY);if(m&&API(m)->Mod_BeginFrame)API(m)->Mod_BeginFrame(&video);}
void Mods_EndFrame(void) {mh_mod_t *m=Owner(MOD_CAP_DISPLAY);if(m&&API(m)->Mod_EndFrame)API(m)->Mod_EndFrame();}
qboolean Mods_Hud(mod_hud_t *h) {mh_mod_t *m=Owner(MOD_CAP_HUD);if(!m||!API(m)->Mod_Hud||ModUI_Capturing())return false;API(m)->Mod_Hud(h);return true;}
void Mods_Overlay2D(void)
{
    int i;for(i=0;i<MH_Count();i++) {mh_mod_t *m=MH_At(i);if(m->active&&API(m)&&(API(m)->capabilities&MOD_CAP_HUD)&&API(m)->Mod_Overlay2D)API(m)->Mod_Overlay2D();}
    if(initialized)ModUI_Draw();
}
void Mods_DebugPanels(const csmod_ui_t *ui,int index) {mh_mod_t *m=MH_At(index);if(m&&m->initialized&&API(m)&&API(m)->Mod_DebugPanel)API(m)->Mod_DebugPanel(ui);}
void Mods_DrawWorld(const csmod_view_t *v,const csmod_draw_t *draw)
{
    int i;
    if(!MH_Managed()||cls.state!=ca_active||!getenv("CS_MOD_SERVER"))return;
    // The endpoint is supplied by the managed launcher. Never reuse local data on another connection.
    if(Q_strcmp(NET_AdrToString(cls.netchan.remote_address),getenv("CS_MOD_SERVER")))return;
    for(i=0;i<MH_Count();i++) {mh_mod_t *m=MH_At(i);if(m->active&&API(m)&&API(m)->Mod_WorldOverlay)API(m)->Mod_WorldOverlay(v,draw);}
}
