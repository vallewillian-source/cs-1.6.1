/* Generic server extension ABI; GPL-3.0. */
#ifndef CSMOD_SERVER_H
#define CSMOD_SERVER_H
#include "csmod_services.h"
#define CSMOD_SERVER_ABI 1
#ifdef __cplusplus
extern "C" {
#endif
enum { CSMOD_BOT_RESET, CSMOD_BOT_MOVE, CSMOD_BOT_GUARD, CSMOD_BOT_SAVE };
typedef struct csmod_server_services_s {
    uint32_t size;
    const csmod_services_t *common;
    int (*Match)( csmod_match_t *out );
    int (*SitePosition)( int site, float out[3] ); /* 1=A, 2=B */
    int (*BotCommand)( uint32_t player, int action, int site );
} csmod_server_services_t;
typedef struct csmod_server_api_s {
    uint32_t version, size;
    int (*Init)( const csmod_server_services_t *services );
    void (*Enable)( int enabled );
    void (*Frame)( double time );
    void (*RoundStart)( void );
    void (*BotUpdate)( uint32_t player );
    void (*Shutdown)( void );
} csmod_server_api_t;
typedef const csmod_server_api_t *(*CSModServerAPI_t)(void);
#ifdef __cplusplus
}
#endif
#endif
