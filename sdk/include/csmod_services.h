/* CS 1.6.1 SDK — GPL-3.0. All callbacks use the supplied per-mod context.
 * No engine, C++, OpenXR, SDL or ImGui types cross this boundary.
 * Services are valid until process exit; gameplay callbacks run on the main thread.
 */
#ifndef CSMOD_SERVICES_H
#define CSMOD_SERVICES_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define CSMOD_MAX_PLAYERS 32
#define CSMOD_LOG_INFO 0
#define CSMOD_LOG_WARN 1
#define CSMOD_LOG_ERROR 2

typedef struct csmod_player_s {
    uint32_t id; /* slot + generation; never retain internal engine pointers */
    int32_t slot, team, alive, bot, money;
    char name[64];
    float origin[3], mins[3], maxs[3];
} csmod_player_t;

typedef struct csmod_match_s {
    uint32_t size, round, revision, reserved; /* identical wire alignment on i386/x86_64 */
    char session[64], map[64];
    double time;
    int32_t freeze, bomb_planted, bomb_carried, bomb_site;
    int32_t score[3], count;
    csmod_player_t players[CSMOD_MAX_PLAYERS];
} csmod_match_t;
#if defined(__cplusplus)
static_assert(sizeof(csmod_player_t)==124, "player wire layout changed");
static_assert(sizeof(csmod_match_t)==4152, "match wire layout changed");
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(csmod_player_t)==124, "player wire layout changed");
_Static_assert(sizeof(csmod_match_t)==4152, "match wire layout changed");
#endif

typedef struct csmod_ui_s {
    uint32_t size;
    void (*Text)( const char *text );
    int (*Checkbox)( const char *label, int *value );
    int (*Slider)( const char *label, float *value, float min, float max );
    void (*Separator)( void );
} csmod_ui_t;

typedef struct csmod_view_s {
    uint32_t size;
    int32_t eye, local_slot, width, height;
    float origin[3], angles[3], fov_x, fov_y;
    float fov_asym[4];
    int32_t has_fov_asym;
} csmod_view_t;

typedef struct csmod_draw_s {
    uint32_t size;
    /* Coordinates are local to this view's viewport. RGBA is 0xAABBGGRR. */
    void (*Line)( float x1, float y1, float x2, float y2, uint32_t rgba );
    void (*Text)( float x, float y, uint32_t rgba, const char *text );
} csmod_draw_t;

typedef struct csmod_services_s {
    uint32_t size;
    void *context;
    const char *package_dir, *state_dir, *session;
    /* Thread safe and queued: no console or disk access from caller threads. */
    void (*Log)( void *context, int severity, const char *message );
    void (*Metric)( void *context, const char *name, double value );
    /* Cached mailbox; 0 = absent/stale/wrong session. No disk I/O here. */
    int (*ReadSnapshot)( void *context, const char *channel, void *out,
                         uint32_t capacity, uint32_t max_age_ms );
    void (*PublishSnapshot)( void *context, const char *channel, const void *data,
                             uint32_t size );
    double (*MonotonicTime)( void );
} csmod_services_t;
#ifdef __cplusplus
}
#endif
#endif
