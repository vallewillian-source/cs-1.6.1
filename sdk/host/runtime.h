/* Internal host glue, not part of the mod ABI. GPL-3.0. */
#ifndef CSMOD_RUNTIME_H
#define CSMOD_RUNTIME_H
#include "csmod_services.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MH_MAX_MODS 32
#define MH_CLAIM_CAMERA 1u
#define MH_CLAIM_INPUT 2u
#define MH_CLAIM_POINTER 4u
#define MH_CLAIM_BOTS 8u
typedef struct mh_mod_s {
    char id[64], title[96], package_dir[1024], state_dir[1024];
    char client[256], server[256], error[256];
    uint32_t claims;
    int desired, active, initialized, panel, toggle_key, panel_key;
    void *library, *api;
    csmod_services_t services;
} mh_mod_t;
typedef struct mh_log_s { double time; int severity; char origin[16], text[1024]; } mh_log_t;
/* Init once per process. Worker owns file I/O; hosts only dispatch callbacks. */
void MH_Init( const char *root, const char *state, const char *origin );
void MH_Shutdown( void );
int MH_Count( void );
mh_mod_t *MH_At( int index );
void MH_Sync( void );
int MH_CanEnable( mh_mod_t *mod );
void MH_Request( mh_mod_t *mod, int enabled );
void MH_Preferences( void );
void MH_Error( mh_mod_t *mod, const char *message );
void *MH_LoadLibrary( mh_mod_t *mod, const char *relative );
void MH_Log( mh_mod_t *mod, int severity, const char *message );
int MH_Logs( mh_mod_t *mod, mh_log_t *out, int capacity, int clear );
int MH_Metrics( mh_mod_t *mod, char *out, int capacity );
int MH_Status( mh_mod_t *mod, char *out, int capacity ); /* confirmed components */
int MH_Health( mh_mod_t *mod ); /* 1=all active, 0=inactive/pending, -1=component error */
void MH_UITelemetry( int capture, int panels, int selected, double frame_ms );
void MH_UIPointer( float x, float y, int buttons, int relative, int manager, int menu );
int MH_Managed( void );
const char *MH_StateRoot( void );
const char *MH_Session( void );
/* Backend provides Dear ImGui without exposing it to the C engine/SDK. */
void ModUI_Init( void *window, void *context );
void ModUI_Shutdown( void );
int ModUI_Event( void *sdl_event );
int ModUI_Key( int key, int down );
int ModUI_Capturing( void );
int ModUI_ManagerOpen( void );
void ModUI_GameMenu( int visible );
void ModUI_Draw( void );
void ModUI_World( const csmod_view_t *view );
/* Implemented by the engine host; called with GL state guarded by UI glue. */
void Mods_DebugPanels( const csmod_ui_t *ui, int index );
void Mods_DrawWorld( const csmod_view_t *view, const csmod_draw_t *draw );
#ifdef __cplusplus
}
#endif
#endif
