/*
mod_null.c — Mod-template log-only: PRIMEIRO consumidor da ABI de Mods
(docs/MODS.md) e teste de que a arquitetura é genérica (nada de VR aqui).

Capabilities = 0: o mod recebe só lifecycle + frame hooks. Os hooks
gated (camera/begin/end/input/hud) NUNCA devem disparar — se algum log
"(não deveria acontecer)" aparecer, o gating do engine está quebrado.

Build: ./build.sh → game/mods/mod_null.so
Uso:   ./xash3d -game cstrike -mod null
*/
#include <stdio.h>
#include <stdarg.h>
#include "mod_api.h"

// Log próprio em arquivo (mods/mod_null.log, relativo ao cwd do engine):
// evidência determinística do ciclo de vida do Mod, independente do canal
// de console do engine (que em modo gráfico não vai pra stdout).
static FILE *lg;

static void blog( const char *fmt, ... )
{
	va_list ap;

	if( !lg )
		return;
	va_start( ap, fmt );
	vfprintf( lg, fmt, ap );
	va_end( ap );
	fflush( lg );
}

static const mod_engine_api_t *eng;
static unsigned long n_frames, n_pre, n_post;
static double t_start;

static void note_gated( const char *hook )
{
	eng->Printf( "mod_null: %s chamado (não deveria acontecer com capabilities=0!)\n", hook );
}

static int Null_Init( const mod_engine_api_t *e )
{
	eng = e;
	t_start = e->DoubleTime();
	lg = fopen( "mods/mod_null.log", "a" );
	blog( "==== mod_null init: engine %s, mod api v%d ====\n", e->engine_version, MOD_API_VERSION );
	e->Printf( "mod_null: init (engine %s, mod api v%d) — template, não altera o jogo\n",
		e->engine_version, MOD_API_VERSION );
	return 1;
}

static void Null_Shutdown( void )
{
	blog( "mod_null shutdown: frames=%lu pre=%lu post=%lu (%.1fs)\n",
		n_frames, n_pre, n_post, eng->DoubleTime() - t_start );
	eng->Printf( "mod_null: shutdown — frames=%lu pre=%lu post=%lu (%.1fs)\n",
		n_frames, n_pre, n_post, eng->DoubleTime() - t_start );
	if( lg )
	{
		fclose( lg );
		lg = NULL;
	}
}

static void Null_FramePre( double time )
{
	n_pre++;

	if(( ++n_frames % 600 ) == 0 )
	{
		blog( "mod_null: %lu frames (%.1fs)\n", n_frames, time - t_start );
		eng->Printf( "mod_null: %lu frames (%.1fs)\n", n_frames, time - t_start );
	}
}

static void Null_FramePost( double time )
{
	(void)time;
	n_post++;
}

static void Null_Camera( mod_view_t *view )
{
	(void)view;
	note_gated( "Mod_Camera" );
}

static int Null_BeginFrame( const mod_gl_ctx_t *ctx )
{
	(void)ctx;
	note_gated( "Mod_BeginFrame" );
	return 0;
}

static void Null_EndFrame( void )
{
	note_gated( "Mod_EndFrame" );
}

static void Null_Input( mod_input_sample_t *sample )
{
	(void)sample;
	note_gated( "Mod_Input" );
}

static void Null_HudRedirect( int enable )
{
	(void)enable;
	note_gated( "Mod_HudRedirect" );
}

static void Null_HudPointer( float x, float y, int buttons )
{
	(void)x; (void)y; (void)buttons;
	note_gated( "Mod_HudPointer" );
}

static const char g_name[] = "null";

static mod_api_t g_api =
{
	.api_version = MOD_API_VERSION,
	.mod_name = g_name,
	.capabilities = 0, // só lifecycle + frame — gating do engine é o teste
	.Mod_Init = Null_Init,
	.Mod_Shutdown = Null_Shutdown,
	.Mod_FramePre = Null_FramePre,
	.Mod_FramePost = Null_FramePost,
	.Mod_Camera = Null_Camera,
	.Mod_BeginFrame = Null_BeginFrame,
	.Mod_EndFrame = Null_EndFrame,
	.Mod_Input = Null_Input,
	.Mod_HudRedirect = Null_HudRedirect,
	.Mod_HudPointer = Null_HudPointer,
};

mod_api_t *ModGetAPI( void )
{
	return &g_api;
}
