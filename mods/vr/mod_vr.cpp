/*
mod_vr.cpp — PRIMEIRO Mod real: realidade virtual via OpenXR (PCVR, GLX).

Fase 2 do PLAN.md: sessão OpenXR (WiVRn/Monado/SteamVR), frame loop com
pacing pelo HMD, swapchains GL por olho, câmera estereoscópica por cabeça
(sem 6DoF — mira pela cabeça, MVP), espelho da vista no monitor.

Estéreo: cada olho usa a base (frente/direita/cima) e a posição da SUA pose, e
o frustum ASSIMÉTRICO que o runtime declara para ele (headset de ótica canted
entrega retângulos fora de centro e espelhados entre os olhos). O afastamento
entre as câmeras sai da posição real dos dois olhos, não de um IPD chutado.

Este Mod NÃO toca no engine: usa a ABI (engine/common/mod_api.h) — os hooks
que ele declara são os que o engine oferece. Sem runtime OpenXR disponível (ou
headset desligado), o Mod fica inativo e o jogo roda flat normalmente (fallback
automático).

Variáveis de ambiente:
  VR_DEBUG=1               log verboso (poses, frame times)
  VR_RESOLUTION_SCALE=1.0  escala do buffer de render por olho (0.3–3.0)
  VR_IPD=0                 força uma distância interpupilar em metros;
                           0 (padrão) = usa a posição real de cada olho
                           que o runtime informa (IPD calibrado do headset)
  VR_IPD_HUD=0             não escreve o valor do ajuste dentro dos olhos

Ajuste fino do estéreo (teclado, dentro do jogo):
  F9 (ou Home)             liga/desliga o MODO DE AJUSTE do IPD
  - e =                    aproximam/afastam as câmeras em 1.0 mm
  , e .                    idem, em 0.1 mm (fino)
  0                        zera o ajuste (volta à base: IPD do runtime ou VR_IPD)
  Com o modo ligado essas teclas são CONSUMIDAS (não chegam ao jogo) e o valor
  aparece escrito DENTRO do headset — nos dois olhos, com disparidade zero, que
  é o único jeito de ler o número calibrando com o HMD na cabeça — e no painel
  2D. Cada mudança vai para o log e para mods/vr_ipd.cfg, que é relido no
  próximo início: o ajuste (delta) sobrevive ao restart.
  O IPD final fica em [0, 110] mm. O piso 0 é física: as duas câmeras coincidem
  e os olhos recebem a mesma imagem (mono); abaixo de 0 os olhos trocariam de
  lado e o cérebro não funde. O ajuste nasce em VR_IPD_DELTA_DEFAULT_MM — hoje
  −67,2 mm, que é o piso na base do Quest 3 (67,2 mm): a calibração no headset
  terminou com as câmeras coincidindo (IPD 0,0, mono), que foi onde o par fundiu
  melhor. VR_IPD_ADJUST=<mm> sobrescreve esse padrão.
  VR_DEBUG_CLEAR=1         limpa cada olho com cor distinta (teste de estéreo)
  VR_DEBUG_PANEL=0         esconde o painel de debug da tela 2D (padrão: visível)
  VR_DEBUG_PANEL_SCALE=2   escala da fonte do painel (1–3, auto-ajusta se não caber)
  VR_PANEL_DUMP=saida.ppm  salva um PPM do quadro (painel incluso) ~2 s após o init
  VR_STEREO_DUMP=pref      grava pref-<n>-eye0.ppm e pref-<n>-eye1.ppm de 4 quadros
                           (mesmo quadro nos dois olhos: mede o par fora do HMD)

Input / locomoção (Fase 3):
  Stick esquerdo       andar (a direção segue o yaw da cabeça, não o pitch)
  Stick direito X      snap turn de VR_TURN_SNAP_ANGLE graus
  Stick direito Y      subir/descer onde o jogo usa upmove (água, espectador)
  Trigger direito/esq  atirar / ataque secundário
  A / B                pular / recarregar
  X / Y                usar / placar (segurar)
  Click stick esquerdo agachar (além do agachar físico)
  Grip (qualquer)      andar devagar (cl_movespeedkey)
  Botão menu           recentrar: zera o giro do snap e recalibra a altura

  VR_DEADZONE=0.20         zona morta radial do stick (0–0.9)
  VR_TURN_SNAP_ANGLE=30    graus por snap turn
  VR_MOVE_SCALE=1.0        multiplica a velocidade do stick (0–2)
  VR_TRIGGER_THRESHOLD=0.5 quanto do gatilho conta como pressionado
  VR_SQUEEZE_THRESHOLD=0.6 quanto do grip conta como andar devagar
  VR_LEFT_HANDED=1         troca os sticks (locomoção na mão direita)
  VR_CROUCH_MODE=3         agachar: 0 desliga, 1 só físico, 2 só botão, 3 os dois
  VR_CROUCH_DROP=0.25      metros de queda da cabeça que contam como agachar
  VR_ROOMSCALE=0           desliga a posição do HMD (só rotação)
  VR_ROOMSCALE_MAX=0.6     metros de deslocamento do roomscale aplicados à câmera
  VR_COMFORT_VIGNETTE=0    intensidade da vignette de conforto (0 = desligada,
                           0.35 é um valor leve; só age enquanto há locomoção)

Arma na mão / mira (Fase 6):
  VR_AIM_SOURCE=right      quem manda na mira: "head" (como na Fase 3), "left"
                           ou "right". Com a mão, o eixo da arma é o que vai ao
                           servidor (o tiro vai para onde a arma aponta) e a
                           ORIENTAÇÃO do viewmodel acompanha sozinha, porque o
                           client DLL a deriva da view. O que este mod move é a
                           ORIGEM: do meio dos olhos para a mão.
  VR_WEAPON_WORLD_DEPTH=0  desliga o depth normal: a arma volta a ser desenhada
                           por cima do mundo (comportamento do jogo flat)
  VR_WEAPON_DEBUG=1        loga 1x/s os números crus da arma: origem que o client
                           DLL escreveu, olho do player, delta da mão e origem
                           final — é o que responde "por que a arma não aparece
                           onde eu esperava"
  VR_MOVE_PITCH_COMP=1     com a mira na mão, o pitch do usercmd passa a ser o da
                           ARMA, e o pmove escala o andar por cos(pitch) — apontar
                           a arma para o chão enquanto se anda reduziria a
                           velocidade. Esta opção compensa (1/cos, com piso).
                           Padrão 0 = idêntico ao jogo (a cabeça é que mandava).
  O encaixe do modelo na mão (offset no referencial da ARMA) vem de
  mods/vr_weapon.cfg, com uma tabela por classe de arma, relido quando o arquivo
  muda (ver o cabeçalho do próprio .cfg).

Menus no VR (Fase 4, recorte: esc / m / b):
  Com um MENU aberto (esc, m de time, b de compra, classe), o desenho 2D do
  jogo — que contém o menu — é capturado num canvas offscreen pelo engine (ABI
  v8, R_HudCanvas*) e este Mod o desenha como um PAINEL parado no mundo, na
  frente e na altura dos olhos, ancorado no instante em que o menu abriu (não
  segue a cabeça). A mão DIREITA vira o mouse: um laser sai do eixo dela e o
  GATILHO DIREITO clica no item apontado. Sem menu aberto não há painel, laser,
  cursor nem clique — o jogo normal continua exatamente como era (e o
  mods/vr_ipd.cfg, o teclado do IPD e o mouse do SO seguem intactos).
  Nada disto funciona sem VR ativo: o Mod só pede o canvas quando há VR e menu.

  VR_MENU=0                desliga os menus no VR (nem canvas, nem laser)
  VR_MENU_LASER=0          desenha o painel mas não o laser/ponto
  VR_MENU_DIST=<m>         distância do painel à frente (padrão 1.30)
  VR_MENU_HEIGHT=<m>       altura física do painel (padrão 0.95; o TEXTO cresce
                           junto — é esta razão altura/distância que decide a
                           legibilidade; a largura segue o aspecto da janela)
  VR_MENU_DEBUG=1          loga 1x/s o estado do ponteiro e do painel
  VR_MENU_* acima são só o padrão: game/mods/vr_menu.cfg (dist_m, height_m,
  yaw_off_deg, y_off_m) tem a palavra final e é RELIDO a cada ~1s — salvar o
  arquivo já aplica, inclusive com o menu aberto (a âncora é refeita na hora).

Painel de debug (2D):
  Desenhado em Mod_Overlay2D (ABI v3) — depois do HUD/menus do jogo e antes do
  swap, portanto POR CIMA da UI. Ele pinta apenas o framebuffer 2D da janela:
  os olhos são renderizados nas swapchains do HMD (Mod_Camera/Mod_AfterEyes +
  submit em Mod_EndFrame) e o painel nunca escreve nelas — o headset não vê o
  painel, só o monitor. Mostra FPS e o histórico de erros/avisos do
  carregamento do VR (o mesmo texto que vai para mods/mod_vr.log). Continua
  visível quando o VR falha e o jogo roda flat — que é quando ele importa.
  Desde a Fase 3 mostra também o bloco INPUT: perfil de interação ativo por mão
  (é ele que responde "o controle casou?"), sticks, giro acumulado e botões.

Build: ./build.sh → game/mods/mod_vr.so (+ libopenxr_loader ao lado)
Uso:   scripts/run-cs-vr.sh   (seta XR_RUNTIME_JSON do WiVRn etc.)
*/

// ORDEM OBRIGATÓRIA: defines + headers GL/X11 ANTES do openxr_platform.h
#define XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL
#include <X11/Xlib.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <dlfcn.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include <sys/stat.h> // stat() — mtime do mods/vr_weapon.cfg (hot-reload)

#include "mod_api.h"
#include "vr_font.h" // fonte bitmap do painel (gerada por gen_font.py)
#include "vr_paths.h"
#include "vr_async.h"
#include "vr_hands.h" // viewmodel proprio (bracos + arma) — subsistema a parte

// GL 1.x / GLX 1.2: typedefs PFN das funções de core não existem em
// gl.h/glext.h/glxext.h (só cobrem 1.2+/extensões) — declarar na mão:
typedef void ( APIENTRYP PFNGLVIEWPORTPROC )( GLint, GLint, GLsizei, GLsizei );
typedef void ( APIENTRYP PFNGLCLEARCOLORPROC )( GLclampf, GLclampf, GLclampf, GLclampf );
typedef void ( APIENTRYP PFNGLCLEARPROC )( GLbitfield );
typedef void ( APIENTRYP PFNGLGETINTEGERVPROC )( GLenum, GLint * );
typedef GLXDrawable ( APIENTRYP PFNGLXGETCURRENTDRAWABLEPROC )( void );
typedef GLXContext ( APIENTRYP PFNGLXGETCURRENTCONTEXTPROC )( void );

static const mod_engine_api_t *eng;
static bool verbose;
static void logf( const char *fmt, ... ) __attribute__(( format( printf, 1, 2 )));
static void logf( const char *fmt, ... )
{
    char buf[1024]; va_list ap; va_start(ap,fmt); vsnprintf(buf,sizeof(buf),fmt,ap); va_end(ap);
    vr_diagnostic(CSMOD_LOG_INFO,buf);
}

// ---------------- diagnóstico: mensagens do carregamento (painel 2D) ----------------
// Tudo que é registrado aqui vai para mods/mod_vr.log E para o painel de debug
// da tela 2D. A ideia é que qualquer falha do carregamento do VR seja visível
// no monitor, com detalhe (código XrResult traduzido, tempo, origem).
enum vr_sev_t
{
	VR_SEV_INFO = 0,  // linha informativa
	VR_SEV_OK,        // marco concluído com sucesso
	VR_SEV_WARN,      // atenção: pode degradar o VR
	VR_SEV_ERROR,     // falha: o VR não vai funcionar assim
};

static double vr_t0;

// status resumido, lido pelo painel
static char vr_runtime[160];                // "WiVRn 26.9.0"
static char vr_video_driver[32];            // driver de vídeo do SDL: x11/wayland/...
static char vr_gl_version[64];              // GL_VERSION do contexto do jogo
static int  vr_gl_major, vr_gl_minor;       // versão GL parseada (0 = desconhecida)
static std::atomic<int> vr_session_state { -1 };
static int  vr_eye_w, vr_eye_h, vr_eye_images, vr_eye_count;
static char vr_glx_info[128];

static void vr_note( int sev, const char *fmt, ... )
{
    char buf[1024]; va_list ap; va_start(ap,fmt); vsnprintf(buf,sizeof(buf),fmt,ap); va_end(ap);
    vr_diagnostic(sev==VR_SEV_ERROR?CSMOD_LOG_ERROR:sev==VR_SEV_WARN?CSMOD_LOG_WARN:CSMOD_LOG_INFO,buf);
}

// código de erro OpenXR → nome legível (o que o painel mostra no lugar de "-51")
static const char *vr_xr_result_str( int r )
{
	switch( r )
	{
	case XR_SUCCESS: return "XR_SUCCESS";
	case XR_TIMEOUT_EXPIRED: return "XR_TIMEOUT_EXPIRED";
	case XR_SESSION_LOSS_PENDING: return "XR_SESSION_LOSS_PENDING";
	case XR_EVENT_UNAVAILABLE: return "XR_EVENT_UNAVAILABLE";
	case XR_ERROR_VALIDATION_FAILURE: return "XR_ERROR_VALIDATION_FAILURE";
	case XR_ERROR_RUNTIME_FAILURE: return "XR_ERROR_RUNTIME_FAILURE";
	case XR_ERROR_OUT_OF_MEMORY: return "XR_ERROR_OUT_OF_MEMORY";
	case XR_ERROR_API_VERSION_UNSUPPORTED: return "XR_ERROR_API_VERSION_UNSUPPORTED";
	case XR_ERROR_INITIALIZATION_FAILED: return "XR_ERROR_INITIALIZATION_FAILED";
	case XR_ERROR_FUNCTION_UNSUPPORTED: return "XR_ERROR_FUNCTION_UNSUPPORTED";
	case XR_ERROR_FEATURE_UNSUPPORTED: return "XR_ERROR_FEATURE_UNSUPPORTED";
	case XR_ERROR_EXTENSION_NOT_PRESENT: return "XR_ERROR_EXTENSION_NOT_PRESENT";
	case XR_ERROR_RUNTIME_UNAVAILABLE: return "XR_ERROR_RUNTIME_UNAVAILABLE";
	case XR_ERROR_INSTANCE_LOST: return "XR_ERROR_INSTANCE_LOST";
	case XR_ERROR_SESSION_RUNNING: return "XR_ERROR_SESSION_RUNNING";
	case XR_ERROR_SESSION_NOT_RUNNING: return "XR_ERROR_SESSION_NOT_RUNNING";
	case XR_ERROR_SESSION_LOST: return "XR_ERROR_SESSION_LOST";
	case XR_ERROR_SYSTEM_INVALID: return "XR_ERROR_SYSTEM_INVALID";
	case XR_ERROR_PATH_INVALID: return "XR_ERROR_PATH_INVALID";
	case XR_ERROR_FORM_FACTOR_UNAVAILABLE: return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
	case XR_ERROR_FORM_FACTOR_UNSUPPORTED: return "XR_ERROR_FORM_FACTOR_UNSUPPORTED";
	case XR_ERROR_CALL_ORDER_INVALID: return "XR_ERROR_CALL_ORDER_INVALID";
	case XR_ERROR_GRAPHICS_DEVICE_INVALID: return "XR_ERROR_GRAPHICS_DEVICE_INVALID";
	case XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED: return "XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED";
	case XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING: return "XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING";
	default: return "XR erro desconhecido";
	}
}

static const char *vr_session_state_str( int s )
{
	switch( s )
	{
	case XR_SESSION_STATE_IDLE: return "IDLE";
	case XR_SESSION_STATE_READY: return "READY";
	case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
	case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
	case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
	case XR_SESSION_STATE_STOPPING: return "STOPPING";
	case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
	case XR_SESSION_STATE_EXITING: return "EXITING";
	case XR_SESSION_STATE_UNKNOWN: return "UNKNOWN";
	default: return "?";
	}
}

#define OXR( x ) do { XrResult _r = (x); if( XR_FAILED( _r )) { vr_note( VR_SEV_ERROR, "%s falhou em %s:%d: %d (%s)\n", #x, __FILE__, __LINE__, (int)_r, vr_xr_result_str( (int)_r )); return false; } } while( 0 )

// ---------------- rastreio do pipeline estéreo (HMD preto) ----------------
// "Tudo preto no headset" pode vir de vários pontos silenciosos: imagem da
// swapchain nunca renderizada, FBO incompleto, camadas não submetidas, ou o
// engine nem chamar o hook de câmera (no menu ele não desenha 3D). Estes
// traços marcam cada etapa dos primeiros frames do estéreo — VR_TRACE=1
// mantém ligado o tempo todo.
static const int VR_TRACE_FRAMES = 12;
static int  vr_stereo_frames;
static bool vr_trace_all;

static void vr_trace( const char *fmt, ... )
{
	char buf[512];
	va_list ap;

	if( !vr_trace_all && vr_stereo_frames > VR_TRACE_FRAMES )
		return;

	va_start( ap, fmt );
	vsnprintf( buf, sizeof( buf ), fmt, ap );
	va_end( ap );
	vr_note( VR_SEV_INFO, "%s", buf );
}

static void vr_log_stereo_geometry( void ); // definida junto do locate de views
static void stereo_dump( void );            // definida junto do painel de 2D
static void vr_draw_ipd_hud( int eye );     // definida junto do painel de 2D
static void vr_ipd_cfg_tick( void );        // definida junto do ajuste do IPD
static void vr_menu_cfg_tick( void );       // definida junto dos menus no VR
static void vr_menu_cfg_load( void );       // idem (chamada no init)
static void vr_menu_cfg_seed( void );       // idem (cria o template se faltar)

// Drena e registra os erros de GL. O engine não checa glGetError na nossa
// região de desenho: sem isto, um desenho descartado pelo driver (FBO
// incompleto, formato não renderizável) não deixa rastro nenhum.
// (Definida abaixo, junto das funções de GL carregadas em runtime.)
static void vr_gl_check( const char *where );

// ---------------- medição de desempenho ----------------
// "Preto nas áreas novas ao virar a cabeça" é sintoma clássico de quadro
// ATRASADO: o compositor não tem conteúdo para a região recém-exposta. Medir
// cada etapa (render dos dois olhos, espelho no monitor, painel) e comparar com
// o período do HMD diz onde está o gargalo — em vez de adivinhar.
static double vr_perf_eyes_t0;      // marca do 1º Mod_Camera do frame
static double vr_perf_t0;           // início da janela de 1 s
static int    vr_perf_n;
static double vr_perf_eyes_ms, vr_perf_blit_ms, vr_perf_panel_ms, vr_perf_work_ms;
static double vr_perf_work_t0;
static double vr_perf_hmd_period_ms;

static double vr_now( void ) { return eng ? eng->DoubleTime() : 0.0; }

static void vr_perf_flush( void )
{
	if( vr_perf_n == 0 || vr_perf_t0 <= 0.0 )
	{
		vr_perf_t0 = vr_now();
		return;
	}

	const double elapsed = vr_now() - vr_perf_t0;

	if( elapsed < 1.0 )
		return;

	vr_note( VR_SEV_INFO, "perf: %.0f fps | trabalho %.1f ms de %.1f ms do HMD (%.0f Hz) | espelho %.1f, painel %.1f\n",
		vr_perf_n / elapsed,
		vr_perf_work_ms / vr_perf_n,
		vr_perf_hmd_period_ms,
		vr_perf_hmd_period_ms > 0.0 ? 1000.0 / vr_perf_hmd_period_ms : 0.0,
		vr_perf_blit_ms / vr_perf_n,
		vr_perf_panel_ms / vr_perf_n );

	vr_perf_n = 0;
	vr_perf_eyes_ms = vr_perf_blit_ms = vr_perf_panel_ms = vr_perf_work_ms = 0.0;
	vr_perf_t0 = vr_now();
}

static const char *vr_fbo_status_str( unsigned st )
{
	switch( st )
	{
	case GL_FRAMEBUFFER_COMPLETE: return "completo";
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: return "INCOMPLETO (attachment)";
	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: return "INCOMPLETO (falta attachment)";
	case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER: return "INCOMPLETO (draw buffer)";
	case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER: return "INCOMPLETO (read buffer)";
	case GL_FRAMEBUFFER_UNSUPPORTED: return "NAO SUPORTADO pelo driver";
	case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE: return "INCOMPLETO (multisample)";
	case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS: return "INCOMPLETO (camadas)";
	default: return "desconhecido";
	}
}

// ---------------- GL/GLX dinâmicos (sem link direto; via glXGetProcAddress) ----------------
static void *g_libgl;
static PFNGLXGETPROCADDRESSPROC glx_get_proc;

#define GL_FUN( name, type ) static type p##name;
GL_FUN( glBindFramebuffer, PFNGLBINDFRAMEBUFFERPROC )
GL_FUN( glGenFramebuffers, PFNGLGENFRAMEBUFFERSPROC )
GL_FUN( glDeleteFramebuffers, PFNGLDELETEFRAMEBUFFERSPROC )
GL_FUN( glFramebufferTexture2D, PFNGLFRAMEBUFFERTEXTURE2DPROC )
GL_FUN( glGenRenderbuffers, PFNGLGENRENDERBUFFERSPROC )
GL_FUN( glDeleteRenderbuffers, PFNGLDELETERENDERBUFFERSPROC )
GL_FUN( glBindRenderbuffer, PFNGLBINDRENDERBUFFERPROC )
GL_FUN( glRenderbufferStorage, PFNGLRENDERBUFFERSTORAGEPROC )
GL_FUN( glFramebufferRenderbuffer, PFNGLFRAMEBUFFERRENDERBUFFERPROC )
GL_FUN( glViewport, PFNGLVIEWPORTPROC )
GL_FUN( glClearColor, PFNGLCLEARCOLORPROC )
GL_FUN( glClear, PFNGLCLEARPROC )
GL_FUN( glGetIntegerv, PFNGLGETINTEGERVPROC )
GL_FUN( glBlitFramebuffer, PFNGLBLITFRAMEBUFFERPROC )
#undef GL_FUN

static PFNGLXGETCURRENTDISPLAYPROC     pglXGetCurrentDisplay;
static PFNGLXGETCURRENTDRAWABLEPROC    pglXGetCurrentDrawable;
static PFNGLXGETCURRENTCONTEXTPROC     pglXGetCurrentContext;
static PFNGLXQUERYCONTEXTPROC          pglXQueryContext;
static PFNGLXCHOOSEFBCONFIGPROC        pglXChooseFBConfig;
static PFNGLXGETVISUALFROMFBCONFIGPROC pglXGetVisualFromFBConfig;
static PFNGLXQUERYDRAWABLEPROC         pglXQueryDrawable;

// ---------------- GL fixo (desenho 2D do painel) ----------------
// gl.h não declara typedef PFN para as funções de core 1.x (as mesmas que o
// mod já carrega à mão acima). O painel é o único cliente delas, por isso os
// nomes são próprios (vrgl*) e não colidem com as declarações do cabeçalho.
typedef void ( APIENTRYP VRGL_MATRIXMODE )( GLenum );
typedef void ( APIENTRYP VRGL_LOADIDENTITY )( void );
typedef void ( APIENTRYP VRGL_ORTHO )( GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble );
typedef void ( APIENTRYP VRGL_PUSHMATRIX )( void );
typedef void ( APIENTRYP VRGL_POPMATRIX )( void );
typedef void ( APIENTRYP VRGL_ENABLE )( GLenum );
typedef void ( APIENTRYP VRGL_DISABLE )( GLenum );
typedef void ( APIENTRYP VRGL_BLENDFUNC )( GLenum, GLenum );
typedef void ( APIENTRYP VRGL_COLOR4F )( GLfloat, GLfloat, GLfloat, GLfloat );
typedef void ( APIENTRYP VRGL_BEGIN )( GLenum );
typedef void ( APIENTRYP VRGL_END )( void );
typedef void ( APIENTRYP VRGL_VERTEX2F )( GLfloat, GLfloat );
typedef void ( APIENTRYP VRGL_TEXCOORD2F )( GLfloat, GLfloat );
typedef void ( APIENTRYP VRGL_GENTEXTURES )( GLsizei, GLuint * );
typedef void ( APIENTRYP VRGL_BINDTEXTURE )( GLenum, GLuint );
typedef void ( APIENTRYP VRGL_TEXPARAMETERI )( GLenum, GLenum, GLint );
typedef void ( APIENTRYP VRGL_TEXIMAGE2D )( GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid * );
typedef void ( APIENTRYP VRGL_PIXELSTOREI )( GLenum, GLint );
typedef void ( APIENTRYP VRGL_READPIXELS )( GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid * );
typedef void ( APIENTRYP VRGL_PUSHATTRIB )( GLbitfield );
typedef void ( APIENTRYP VRGL_POPATTRIB )( void );
typedef const GLubyte *( APIENTRYP VRGL_GETSTRING )( GLenum );
typedef GLenum ( APIENTRYP VRGL_CHECKFRAMEBUFFERSTATUS )( GLenum );
typedef GLenum ( APIENTRYP VRGL_GETERROR )( void );

static VRGL_MATRIXMODE     vrglMatrixMode;
static VRGL_LOADIDENTITY   vrglLoadIdentity;
static VRGL_ORTHO          vrglOrtho;
static VRGL_PUSHMATRIX     vrglPushMatrix;
static VRGL_POPMATRIX      vrglPopMatrix;
static VRGL_ENABLE         vrglEnable;
static VRGL_DISABLE        vrglDisable;
static VRGL_BLENDFUNC      vrglBlendFunc;
static VRGL_COLOR4F        vrglColor4f;
static VRGL_BEGIN          vrglBegin;
static VRGL_END            vrglEnd;
static VRGL_VERTEX2F       vrglVertex2f;
static VRGL_TEXCOORD2F     vrglTexCoord2f;
static VRGL_GENTEXTURES    vrglGenTextures;
static VRGL_BINDTEXTURE    vrglBindTexture;
static VRGL_TEXPARAMETERI  vrglTexParameteri;
static VRGL_TEXIMAGE2D     vrglTexImage2D;
static VRGL_PIXELSTOREI    vrglPixelStorei;
static VRGL_READPIXELS     vrglReadPixels;
static VRGL_PUSHATTRIB     vrglPushAttrib;
static VRGL_POPATTRIB      vrglPopAttrib;
static VRGL_GETSTRING      vrglGetString;
static VRGL_CHECKFRAMEBUFFERSTATUS vrglCheckFramebufferStatus;
static VRGL_GETERROR       vrglGetError;
static bool                vr_panel_gl_ok;

// Carrega as funções fixas usadas pelo Mod (desenho do painel e leitura da
// versão de GL para conferir a faixa que o runtime aceita).
static bool gl_fixed_init( void )
{
	if( !g_libgl )
		return false;

	#define VRGL( name, type ) vrgl##name = ( type )dlsym( g_libgl, "gl" #name ); \
		if( !vrgl##name ) { logf( "painel: libGL sem gl%s\n", #name ); return false; }
	VRGL( MatrixMode, VRGL_MATRIXMODE )
	VRGL( LoadIdentity, VRGL_LOADIDENTITY )
	VRGL( Ortho, VRGL_ORTHO )
	VRGL( PushMatrix, VRGL_PUSHMATRIX )
	VRGL( PopMatrix, VRGL_POPMATRIX )
	VRGL( Enable, VRGL_ENABLE )
	VRGL( Disable, VRGL_DISABLE )
	VRGL( BlendFunc, VRGL_BLENDFUNC )
	VRGL( Color4f, VRGL_COLOR4F )
	VRGL( Begin, VRGL_BEGIN )
	VRGL( End, VRGL_END )
	VRGL( Vertex2f, VRGL_VERTEX2F )
	VRGL( TexCoord2f, VRGL_TEXCOORD2F )
	VRGL( GenTextures, VRGL_GENTEXTURES )
	VRGL( BindTexture, VRGL_BINDTEXTURE )
	VRGL( TexParameteri, VRGL_TEXPARAMETERI )
	VRGL( TexImage2D, VRGL_TEXIMAGE2D )
	VRGL( PixelStorei, VRGL_PIXELSTOREI )
	VRGL( ReadPixels, VRGL_READPIXELS )
	VRGL( PushAttrib, VRGL_PUSHATTRIB )
	VRGL( PopAttrib, VRGL_POPATTRIB )
	VRGL( GetString, VRGL_GETSTRING )
	VRGL( CheckFramebufferStatus, VRGL_CHECKFRAMEBUFFERSTATUS )
	VRGL( GetError, VRGL_GETERROR )
	#undef VRGL

	vr_panel_gl_ok = true;
	return true;
}

// Drena e registra os erros de GL (declarada lá em cima, onde é usada).
// O engine não checa glGetError na região em que desenhamos: sem isto, um
// desenho descartado pelo driver não deixa rastro nenhum. Erros repetidos são
// agregados: com o FBO do olho ligado o `glDrawBuffer( GL_BACK )` do engine
// (escrito para a janela, inválido dentro de um FBO) dispara 0x502 todo frame —
// é benigno, mas não pode inundar o painel nem o log.
static void vr_gl_check( const char *where )
{
	static GLenum        last;
	static unsigned long count;

	if( !vrglGetError )
		return;

	for( int i = 0; i < 8; i++ )
	{
		GLenum e = vrglGetError();

		if( e == GL_NO_ERROR )
			return;

		if( e == last )
		{
			if( ++count % 900 == 0 )
				vr_note( VR_SEV_WARN, "GL erro 0x%X em %s (x%lu)\n", ( unsigned )e, where, count );
		}
		else
		{
			last = e;
			count = 1;
			vr_note( VR_SEV_WARN, "GL erro 0x%X em %s\n", ( unsigned )e, where );
		}
	}
}

static bool gl_init( void )
{
	if( g_libgl )
		return true;

	g_libgl = dlopen( "libGL.so.1", RTLD_NOW | RTLD_GLOBAL );
	if( !g_libgl )
	{
		vr_note( VR_SEV_ERROR, "sem libGL.so.1: %s\n", dlerror());
		return false;
	}

	glx_get_proc = (PFNGLXGETPROCADDRESSPROC)dlsym( g_libgl, "glXGetProcAddress" );
	if( !glx_get_proc )
		return false;

	#define GL_FUN( name, type ) p##name = (type)glx_get_proc(( const GLubyte * )#name ); if( !p##name ) { vr_note( VR_SEV_ERROR, "GL sem %s\n", #name ); return false; }
	GL_FUN( glBindFramebuffer, PFNGLBINDFRAMEBUFFERPROC )
	GL_FUN( glGenFramebuffers, PFNGLGENFRAMEBUFFERSPROC )
GL_FUN( glDeleteFramebuffers, PFNGLDELETEFRAMEBUFFERSPROC )
	GL_FUN( glFramebufferTexture2D, PFNGLFRAMEBUFFERTEXTURE2DPROC )
	GL_FUN( glGenRenderbuffers, PFNGLGENRENDERBUFFERSPROC )
GL_FUN( glDeleteRenderbuffers, PFNGLDELETERENDERBUFFERSPROC )
	GL_FUN( glBindRenderbuffer, PFNGLBINDRENDERBUFFERPROC )
	GL_FUN( glRenderbufferStorage, PFNGLRENDERBUFFERSTORAGEPROC )
	GL_FUN( glFramebufferRenderbuffer, PFNGLFRAMEBUFFERRENDERBUFFERPROC )
	GL_FUN( glViewport, PFNGLVIEWPORTPROC )
	GL_FUN( glClearColor, PFNGLCLEARCOLORPROC )
	GL_FUN( glClear, PFNGLCLEARPROC )
	GL_FUN( glGetIntegerv, PFNGLGETINTEGERVPROC )
	GL_FUN( glBlitFramebuffer, PFNGLBLITFRAMEBUFFERPROC )
	#undef GL_FUN

	#define GLX_FUN( name, type ) p##name = (type)glx_get_proc(( const GLubyte * )#name ); if( !p##name ) { vr_note( VR_SEV_ERROR, "GLX sem %s\n", #name ); return false; }
	GLX_FUN( glXGetCurrentDisplay, PFNGLXGETCURRENTDISPLAYPROC )
	GLX_FUN( glXGetCurrentDrawable, PFNGLXGETCURRENTDRAWABLEPROC )
	GLX_FUN( glXGetCurrentContext, PFNGLXGETCURRENTCONTEXTPROC )
	GLX_FUN( glXQueryContext, PFNGLXQUERYCONTEXTPROC )
	GLX_FUN( glXChooseFBConfig, PFNGLXCHOOSEFBCONFIGPROC )
	GLX_FUN( glXGetVisualFromFBConfig, PFNGLXGETVISUALFROMFBCONFIGPROC )
	GLX_FUN( glXQueryDrawable, PFNGLXQUERYDRAWABLEPROC )
	#undef GLX_FUN

	gl_fixed_init(); // painel + leitura da versão de GL; falha aqui não impede o VR

	return true;
}

// ---------------- estado OpenXR ----------------
struct EyeRT
{
	XrSwapchain sc = XR_NULL_HANDLE;
	int32_t w = 0, h = 0;
	std::vector<XrSwapchainImageOpenGLKHR> images;
	std::vector<GLuint> fbos;
	GLuint depth = 0;
	int32_t cur = -1;
};

static XrInstance  xr_instance = XR_NULL_HANDLE;
static XrSystemId  xr_system = XR_NULL_SYSTEM_ID;
static XrSession   xr_session = XR_NULL_HANDLE;
static XrSpace     xr_space = XR_NULL_HANDLE;
static EyeRT       xr_eye[2];
static XrFrameState xr_fs {};
static XrView      xr_views[2] {};
static std::atomic<bool> xr_views_ok { false };
// Sessão iniciada (xrBeginSession aceito). Em SYNCHRONIZED — antes de ficar
// VISIBLE — o app PRECISA continuar chamando xrWaitFrame/xrBeginFrame e
// submetendo quadro vazio (regra do OpenXR): sem esse bombeamento o runtime não
// avança o estado, a sessão fica presa em READY/SYNCHRONIZED e o HMD mostra
// preto sem erro nenhum. É o que o hello_xr faz.
static std::atomic<bool> xr_session_begun { false };
static std::atomic<bool> xr_frame_began { false };
static std::atomic<bool> vr_active { false };  // estéreo neste frame?
static std::atomic<bool> vr_ready { false };   // infra inicializada (instance+session)
static std::atomic<bool> xr_init_started { false };
static std::atomic<bool> xr_init_failed { false }; // init terminou com falha (não re-tentar)
static std::atomic<bool> xr_init_done { false };   // init terminou (ok ou falha)
// A thread de init só cria a INSTÂNCIA (o xrCreateInstance do WiVRn bloqueia
// sem headset). O resto — getSystem, requisitos gráficos, sessão, reference
// space — roda na MAIN thread, onde o contexto GLX está current: o client
// compositor do WiVRn faz glXMakeCurrent ao criar a sessão e devolve
// XR_ERROR_INITIALIZATION_FAILED se o contexto não estiver current ali.
static std::atomic<bool> xr_instance_ready { false };
static bool        xr_setup_permanent;   // falha definitiva (não re-tenta)
static int         xr_setup_last_err = 0x7fffffff;
static bool        xr_gl_reqs_logged;
static double      xr_setup_next_t;      // próximo retry (segundos do engine)
static double      vr_active_since;      // quando o VR ativo começou
static double      vr_last_eye_render_t; // último frame com render 3D por olho
static float       vr_scale = 1.0f;
static float       vr_ipd;                 // BASE em metros: VR_IPD (0 = IPD real do runtime)
static const char *vr_ipd_base_src = "runtime"; // rótulo da base (painel/log): runtime | VR_IPD
static float       vr_ipd_delta;           // ajuste fino do teclado, metros (ver seção do IPD)
static int         vr_ipd_steps;           // o mesmo ajuste em DÉCIMOS de mm (o "numero da posicao")
static float       vr_ipd_rt;              // separação entre os olhos que o runtime informa (m)
static float       vr_eye_dl[3];           // olho esq→dir no referencial da cabeça (m), do runtime
static bool        vr_ipd_adj_on;          // modo de ajuste do IPD ligado (F9/Home)
static bool        vr_ipd_hud = true;      // escreve o valor dentro dos olhos (VR_IPD_HUD=0 desliga)
static const char *vr_ipd_delta_src = "-"; // de onde veio o ajuste: vr_ipd.cfg | teclado
static bool        vr_debug_clear;
static bool        vr_panel = true;        // painel de debug na tela 2D
static int         vr_panel_scale = 2;     // escala da fonte (1..3)
static char        vr_panel_dump_path[256];// VR_PANEL_DUMP: PPM único p/ diagnóstico
static float       vr_panel_dump_at = 2.0f;// VR_PANEL_DUMP_AT: atraso do dump (s)
static bool        vr_panel_dump_done;
static char        vr_stereo_dump_prefix[256]; // VR_STEREO_DUMP: prefixo das 2 imagens
static float       vr_stereo_dump_at = 6.0f;   // VR_STEREO_DUMP_AT: atraso (s)
static int         vr_stereo_dump_n;           // quantos pares já saíram
static double      vr_init_ms = -1.0;      // duração do init do OpenXR (ms)
static const float UNITS_PER_METER = 39.3701f; // GoldSrc: unidades ≈ polegadas

// ---------------- menus no VR (Fase 4, recorte por menu) ----------------
// Estado do intercâmbio com o engine (ABI v8, Mod_Hud) e da âncora do painel.
// Ver a seção "menus no VR" mais abaixo para o desenho e o raio.
static bool        vr_menu = true;             // VR_MENU=0 desliga tudo
static bool        vr_menu_laser = true;       // VR_MENU_LASER=0 desenha só o painel
static bool        vr_menu_debug;              // VR_MENU_DEBUG=1 fala mais no log
static float       vr_menu_dist_m = 1.30f;     // VR_MENU_DIST: distância do painel (m)
static float       vr_menu_height_m = 0.95f;   // VR_MENU_HEIGHT: altura do painel (m)
// faixas de sanidade (o .cfg é editado à mão): fora disto o painel ficaria
// inalcançável (colado na cara) ou ilegível (uma parede longe) sem o usuário
// entender por quê
#define VR_MENU_DIST_MIN_M   0.40f
#define VR_MENU_DIST_MAX_M   5.00f
#define VR_MENU_HEIGHT_MIN_M 0.20f
#define VR_MENU_HEIGHT_MAX_M 3.00f
static float       vr_menu_yaw_off;            // vr_menu.cfg: giro fino do painel (graus)
static float       vr_menu_y_off_m;            // vr_menu.cfg: subir/descer o painel (m)
static bool        vr_menu_open;               // o engine disse: há menu no comando
static bool        vr_menu_prev_open;
static bool        vr_menu_ui_visible;         // UI_IsVisible() (mainui)
static bool        vr_menu_want_canvas;        // decisão enviada ao engine
static unsigned    vr_menu_canvas_tex;         // textura do canvas 2D (0 = não existe)
static int         vr_menu_canvas_w, vr_menu_canvas_h;
static bool        vr_menu_need_anchor;        // capturar a âncora no próximo Mod_Camera
static bool        vr_menu_anchor_ok;          // âncora congelada e válida
static float       vr_menu_anchor[3];          // centro do painel no mundo (unidades)
static float       vr_menu_right[3];           // eixo direito do painel (unitário, horizontal)
static float       vr_menu_norm[3];            // normal do painel, apontando para o jogador
static float       vr_menu_half_w, vr_menu_half_h; // meias dimensões (unidades)
static float       vr_menu_eye_base[3];        // olho do player no mundo, SEM roomscale/IPD
static bool        vr_menu_eye_base_ok;
static bool        vr_menu_ptr_ok;             // cursor já tem uma posição válida
static float       vr_menu_ptr_x, vr_menu_ptr_y;// cursor em pixels do canvas
static bool        vr_menu_hit;                // o raio acertou o painel NESTE frame
static float       vr_menu_hit_world[3];       // ponto de impacto (para o laser)
static bool        vr_menu_arm;                // gatilho já foi solto (clique exige aperto novo)
static bool        vr_menu_click;              // clique neste frame (borda)
static unsigned    vr_menu_clicks;             // contador (log/painel)

// câmera de cada olho, como o engine a usou no frame (para projetar o painel no
// FBO daquele olho). Capturada no fim do VR_Camera.
struct vr_eye_cam_t
{
	float origin[3];
	float angles[3]; // como o engine recebeu (a projeção sai daqui, não da pose)
	float fov[4];    // fov_asym, radianos, sinal OpenXR (left/down negativos)
	int   w, h;
	bool  ok;
};
static vr_eye_cam_t vr_eye_cam[2];

// FPS (contado no frame hook, independente do VR estar ativo)
static unsigned long vr_frames;
static float         vr_fps;
static float         vr_frame_ms;
static double        vr_fps_t0;
static unsigned long vr_fps_frames0;

static bool xr_begin_frame( void );
static void xr_end_frame( void );
static void xr_poll_events( void );

// ---------------- util ----------------
static double rad2deg( double r ) { return r * 180.0 / M_PI; }

// Driver de vídeo do SDL em uso. O mod exige X11/GLX (XR_USE_PLATFORM_XLIB):
// se o SDL abriu a janela no Wayland (EGL), não existe contexto GLX nenhum e o
// binding gráfico do OpenXR é impossível — é a causa nº 1 de "sem contexto
// GLX" numa sessão Wayland (WAYLAND_DISPLAY definido, SDL_VIDEODRIVER vazio).
// Perguntar ao SDL já carregado (RTLD_NOLOAD) evita carregar uma 2ª cópia.
static void detect_video_driver( void )
{
	void *sdl = dlopen( "libSDL2-2.0.so.0", RTLD_NOW | RTLD_NOLOAD );
	const char *( *getdrv )( void ) = NULL;

	if( !sdl )
		sdl = dlopen( "libSDL2-2.0.so.0", RTLD_NOW | RTLD_LOCAL );

	if( sdl )
		getdrv = ( const char *( * )( void ))dlsym( sdl, "SDL_GetCurrentVideoDriver" );

	if( getdrv )
	{
		const char *d = getdrv();
		snprintf( vr_video_driver, sizeof( vr_video_driver ), "%s", ( d && *d ) ? d : "?" );
	}
	else
	{
		snprintf( vr_video_driver, sizeof( vr_video_driver ), "?" );
	}
}

static bool env_on( const char *name )
{
	const char *v = getenv( name );
	return v && *v && strcmp( v, "0" ) != 0;
}

// variável cujo padrão é LIGADO: só desliga com "0" explícito
static bool env_off( const char *name )
{
	const char *v = getenv( name );
	return v && *v && strcmp( v, "0" ) == 0;
}

static float env_float( const char *name, float def )
{
	const char *v = getenv( name );
	return ( v && *v ) ? (float)atof( v ) : def;
}

static void quat_matrix( const XrQuaternionf &q, float c0[3], float c1[3], float c2[3] )
{
	float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
	float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
	float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

	c0[0] = 1 - 2 * ( yy + zz ); c0[1] = 2 * ( xy + wz ); c0[2] = 2 * ( xz - wy );
	c1[0] = 2 * ( xy - wz );     c1[1] = 1 - 2 * ( xx + zz ); c1[2] = 2 * ( yz + wx );
	c2[0] = 2 * ( xz + wy );     c2[1] = 2 * ( yz - wx );     c2[2] = 1 - 2 * ( xx + yy );
}

// OpenXR (X direita, Y cima, Z trás) → GoldSrc (X frente, Y esquerda, Z cima).
// O mapa PRECISA ser rotação própria (det +1): os dois espaços são destros
// (OpenXR: X×Y=Z para trás; GoldSrc: frente×esquerda=cima, e o right×up do
// AngleVectors dá -forward). Qualquer espelho aqui troca a esquerda pela
// direita — a cabeça gira para o lado errado, o offset dos olhos vai para o
// lado errado e o estéreo estoura (o cérebro não funde par espelhado).
static void xr_to_gold( const float v[3], float out[3] )
{
	out[0] = -v[2]; // frente
	out[1] = -v[0]; // esquerda
	out[2] =  v[1]; // cima
}

static float dot3( const float a[3], const float b[3] )
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// Base da cabeça no mundo GoldSrc a partir da pose do runtime (frente, direita
// e cima já no eixo do mundo do jogo).
static void pose_basis_g( const XrPosef &pose, float fwd[3], float rgt[3], float up[3] )
{
	float c0[3], c1[3], c2[3];

	quat_matrix( pose.orientation, c0, c1, c2 );

	float fwd_xr[3] = { -c2[0], -c2[1], -c2[2] }; // forward = -(3ª coluna)

	xr_to_gold( fwd_xr, fwd );
	xr_to_gold( c0, rgt ); // 1ª coluna = eixo X do headset = direita
	xr_to_gold( c1, up );  // 2ª coluna = eixo Y do headset = cima
}

// pose do HMD → ângulos que o engine espera (pitch, yaw, roll). As três
// fórmulas batem com o AngleVectors do engine (conferido numericamente, erro
// ~1e-4): pitch = -asin(fwd.z), yaw = atan2(fwd.y, fwd.x) e roll extraído do
// par right/up. O roll era descartado antes e agora entra: com frustum
// assimétrico por olho (ótica canted) o centro ótico sai do eixo da view e
// inclinar a cabeça desloca a imagem se o render não rolar junto.
static void pose_to_viewangles( const XrPosef &pose, float out[3] )
{
	float fwd[3], rgt[3], up[3];

	pose_basis_g( pose, fwd, rgt, up );

	out[0] = ( float )-rad2deg( asin( fmaxf( -1.0f, fminf( 1.0f, fwd[2] )))); // pitch
	out[1] = ( float )rad2deg( atan2( fwd[1], fwd[0] ));                       // yaw
	out[2] = ( float )rad2deg( atan2( -rgt[2], up[2] ));                       // roll
}

#include "vr_input.inc"

#include "vr_configuration.inc"

static bool vr_make_action( XrAction *out, XrActionType type, const char *name, const char *label )
{
	XrActionCreateInfo ci {}; ci.type = XR_TYPE_ACTION_CREATE_INFO;
	XrResult r;

	ci.actionType = type;
	ci.countSubactionPaths = 0;
	snprintf( ci.actionName, XR_MAX_ACTION_NAME_SIZE, "%s", name );
	snprintf( ci.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE, "%s", label );

	r = xrCreateAction( xr_actionset, &ci, out );
	if( XR_FAILED( r ))
	{
		vr_note( VR_SEV_ERROR, "xrCreateAction(%s): %d %s\n", name, ( int )r, vr_xr_result_str( ( int )r ));
		return false;
	}

	return true;
}

// Igual, mas LIGADA a uma mao (subaction path): o runtime devolve um estado POR
// MAO, que e o que permite distinguir o grip esquerdo do direito. Sem isto os dois
// squeezes caem no MESMO valor e nao da para pegar/soltar a arma com uma mao so.
static bool vr_make_action_sub( XrAction *out, XrActionType type, const char *name,
	const char *label, XrPath hand )
{
	XrActionCreateInfo ci {};   // {} (e nao { XR_TYPE_... }) zera o resto sem aviso
	XrResult r;

	ci.type = XR_TYPE_ACTION_CREATE_INFO;
	ci.actionType = type;
	ci.countSubactionPaths = 1;
	ci.subactionPaths = &hand;
	snprintf( ci.actionName, XR_MAX_ACTION_NAME_SIZE, "%s", name );
	snprintf( ci.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE, "%s", label );

	r = xrCreateAction( xr_actionset, &ci, out );
	if( XR_FAILED( r ))
	{
		vr_note( VR_SEV_ERROR, "xrCreateAction(%s): %d %s\n", name, ( int )r, vr_xr_result_str( ( int )r ));
		return false;
	}

	return true;
}

struct VrBind
{
	XrAction    *action;
	const char  *path;
};

static XrResult vr_suggest_bindings( const char *profile, const VrBind *binds, int n )
{
	std::vector<XrActionSuggestedBinding> sb;
	XrPath pp = XR_NULL_PATH;
	XrInteractionProfileSuggestedBinding ip {}; ip.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
	XrResult r = xrStringToPath( xr_instance, profile, &pp );

	if( XR_FAILED( r ))
		return r;

	sb.reserve( ( size_t )n );
	for( int i = 0; i < n; i++ )
	{
		XrPath bp = XR_NULL_PATH;

		if( XR_FAILED( xrStringToPath( xr_instance, binds[i].path, &bp )))
			continue;

		XrActionSuggestedBinding b;

		b.action = *binds[i].action;
		b.binding = bp;
		sb.push_back( b );
	}

	ip.interactionProfile = pp;
	ip.countSuggestedBindings = ( uint32_t )sb.size();
	ip.suggestedBindings = sb.data();

	return xrSuggestInteractionProfileBindings( xr_instance, &ip );
}

// Cria o action set, as ações e os bindings, e liga o conjunto à sessão.
// PRECISA rodar antes do xrBeginSession (que o xr_poll_events faz no estado
// READY) — daí a chamada no fim do xr_setup_session.
static bool vr_actions_init( void )
{
	static const char *prof_touch[] = {
		"/interaction_profiles/oculus/touch_controller",
		"/interaction_profiles/meta/touch_controller_plus",
	};
	XrResult r;

	if( xr_actionset != XR_NULL_HANDLE )
		return true;

	if( xr_session == XR_NULL_HANDLE )
		return false;

	XrActionSetCreateInfo asc {}; asc.type = XR_TYPE_ACTION_SET_CREATE_INFO;

	snprintf( asc.actionSetName, XR_MAX_ACTION_SET_NAME_SIZE, "gameplay" );
	snprintf( asc.localizedActionSetName, XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE, "Gameplay" );
	asc.priority = 0;

	r = xrCreateActionSet( xr_instance, &asc, &xr_actionset );
	if( XR_FAILED( r ))
	{
		xr_actionset = XR_NULL_HANDLE;
		vr_note( VR_SEV_ERROR, "xrCreateActionSet: %d %s\n", ( int )r, vr_xr_result_str( ( int )r ));
		return false;
	}

	bool ok = true;

	// os caminhos das maos sao precisos JAH na criacao das acoes (o grip e a mira
	// sao por mao), entao eles saem daqui e nao do laco dos action spaces
	for( int h = 0; h < 2; h++ )
		xrStringToPath( xr_instance, h == 0 ? "/user/hand/left" : "/user/hand/right", &xr_path_hand[h] );

	ok = vr_make_action( &xr_act_move,    XR_ACTION_TYPE_VECTOR2F_INPUT, "move",     "Mover" ) && ok;
	ok = vr_make_action( &xr_act_turn,    XR_ACTION_TYPE_VECTOR2F_INPUT, "turn",     "Girar" ) && ok;
	ok = vr_make_action( &xr_act_fire,    XR_ACTION_TYPE_FLOAT_INPUT,    "fire",     "Atirar" ) && ok;
	ok = vr_make_action( &xr_act_altfire, XR_ACTION_TYPE_FLOAT_INPUT,    "altfire",  "Ataque secundario" ) && ok;
	// grip POR MAO: e o mesmo squeeze de sempre, mas com subaction path — e o que
	// permite saber QUAL mao apertou (sem isso nao da para pegar/soltar a arma, nem
	// exigir as duas maos). O MOD_BTN_WALK continua saindo do maior dos dois.
	ok = vr_make_action_sub( &xr_act_grip[0], XR_ACTION_TYPE_FLOAT_INPUT, "grip_left",  "Grip esquerdo", xr_path_hand[0] ) && ok;
	ok = vr_make_action_sub( &xr_act_grip[1], XR_ACTION_TYPE_FLOAT_INPUT, "grip_right", "Grip direito",  xr_path_hand[1] ) && ok;
	ok = vr_make_action( &xr_act_jump,    XR_ACTION_TYPE_BOOLEAN_INPUT,  "jump",     "Pular" ) && ok;
	ok = vr_make_action( &xr_act_reload,  XR_ACTION_TYPE_BOOLEAN_INPUT,  "reload",   "Recarregar" ) && ok;
	ok = vr_make_action( &xr_act_use,     XR_ACTION_TYPE_BOOLEAN_INPUT,  "use",      "Usar" ) && ok;
	ok = vr_make_action( &xr_act_score,   XR_ACTION_TYPE_BOOLEAN_INPUT,  "score",    "Placar" ) && ok;
	ok = vr_make_action( &xr_act_crouch,  XR_ACTION_TYPE_BOOLEAN_INPUT,  "crouch",   "Agachar" ) && ok;
	ok = vr_make_action( &xr_act_menu,    XR_ACTION_TYPE_BOOLEAN_INPUT,  "menu",     "Recentrar" ) && ok;
	ok = vr_make_action( &xr_act_haptic,  XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic", "Vibracao" ) && ok;
	ok = vr_make_action( &xr_act_aim[0],  XR_ACTION_TYPE_POSE_INPUT,     "aim_left", "Mira esquerda" ) && ok;
	ok = vr_make_action( &xr_act_aim[1],  XR_ACTION_TYPE_POSE_INPUT,     "aim_right", "Mira direita" ) && ok;

	if( !ok )
	{
		vr_note( VR_SEV_ERROR, "action set incompleto: input VR desativado\n" );
		return false;
	}

	for( int h = 0; h < 2; h++ )
	{
		XrActionSpaceCreateInfo sci {}; sci.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;

		sci.action = xr_act_aim[h];
		sci.subactionPath = xr_path_hand[h];
		sci.poseInActionSpace.orientation = { 0, 0, 0, 1 };

		r = xrCreateActionSpace( xr_session, &sci, &xr_aim_space[h] );
		if( XR_FAILED( r ))
			vr_note( VR_SEV_WARN, "xrCreateActionSpace(aim mao %d): %d %s\n", h, ( int )r, vr_xr_result_str( ( int )r ));
	}

	VrBind touch[] = {
		{ &xr_act_move,    "/user/hand/left/input/thumbstick" },
		{ &xr_act_turn,    "/user/hand/right/input/thumbstick" },
		{ &xr_act_fire,    "/user/hand/right/input/trigger/value" },
		{ &xr_act_altfire, "/user/hand/left/input/trigger/value" },
		{ &xr_act_grip[0], "/user/hand/left/input/squeeze/value" },
		{ &xr_act_grip[1], "/user/hand/right/input/squeeze/value" },
		{ &xr_act_jump,    "/user/hand/right/input/a/click" },
		{ &xr_act_reload,  "/user/hand/right/input/b/click" },
		{ &xr_act_use,     "/user/hand/left/input/x/click" },
		{ &xr_act_score,   "/user/hand/left/input/y/click" },
		{ &xr_act_crouch,  "/user/hand/left/input/thumbstick/click" },
		{ &xr_act_menu,    "/user/hand/left/input/menu/click" },
		{ &xr_act_aim[0],  "/user/hand/left/input/aim/pose" },
		{ &xr_act_aim[1],  "/user/hand/right/input/aim/pose" },
		{ &xr_act_haptic,  "/user/hand/left/output/haptic" },
		{ &xr_act_haptic,  "/user/hand/right/output/haptic" },
	};
	// perfil mínimo: não tem thumbstick (locomoção não funciona nele), mas evita
	// um app sem NENHUM binding em runtimes que só conhecem o KHR
	VrBind khr[] = {
		{ &xr_act_fire,   "/user/hand/left/input/select/click" },
		{ &xr_act_fire,   "/user/hand/right/input/select/click" },
		{ &xr_act_menu,   "/user/hand/left/input/menu/click" },
		{ &xr_act_menu,   "/user/hand/right/input/menu/click" },
		{ &xr_act_haptic, "/user/hand/left/output/haptic" },
		{ &xr_act_haptic, "/user/hand/right/output/haptic" },
	};

	for( size_t i = 0; i < sizeof( prof_touch ) / sizeof( prof_touch[0] ); i++ )
	{
		r = vr_suggest_bindings( prof_touch[i], touch, ( int )( sizeof( touch ) / sizeof( touch[0] )));
		vr_note( XR_SUCCEEDED( r ) ? VR_SEV_INFO : VR_SEV_WARN, "bindings %s: %s (%d)\n",
			prof_touch[i], XR_SUCCEEDED( r ) ? "ok" : vr_xr_result_str( ( int )r ), ( int )r );
	}

	r = vr_suggest_bindings( "/interaction_profiles/khr/simple_controller", khr, ( int )( sizeof( khr ) / sizeof( khr[0] )));
	vr_note( XR_SUCCEEDED( r ) ? VR_SEV_INFO : VR_SEV_WARN, "bindings khr/simple_controller: %s (%d)\n",
		XR_SUCCEEDED( r ) ? "ok" : vr_xr_result_str( ( int )r ), ( int )r );

	XrSessionActionSetsAttachInfo ai {}; ai.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;

	ai.countActionSets = 1;
	ai.actionSets = &xr_actionset;

	r = xrAttachSessionActionSets( xr_session, &ai );
	if( XR_FAILED( r ))
	{
		vr_note( VR_SEV_ERROR, "xrAttachSessionActionSets: %d %s\n", ( int )r, vr_xr_result_str( ( int )r ));
		return false;
	}

	vr_note( VR_SEV_OK, "input VR pronto: action set ligado a sessao\n" );
	return true;
}

// Perfil de interação ativo por mão. É o diagnóstico que responde "o controle
// casou?" sem precisar adivinhar: se vier vazio, nenhum binding sugerido pegou.
static void vr_query_profiles( void )
{
	for( int h = 0; h < 2; h++ )
	{
		XrInteractionProfileState ips {}; ips.type = XR_TYPE_INTERACTION_PROFILE_STATE;
		char buf[XR_MAX_PATH_LENGTH] = { 0 };
		char *dst = in_profile[h];
		uint32_t len = 0;

		if( XR_FAILED( xrGetCurrentInteractionProfile( xr_session, xr_path_hand[h], &ips )) || ips.interactionProfile == XR_NULL_PATH
			|| XR_FAILED( xrPathToString( xr_instance, ips.interactionProfile, sizeof( buf ), &len, buf ))
			|| !buf[0] )
		{
			snprintf( dst, 128, "%s", "(nenhum)" );
			continue;
		}

		if( strcmp( dst, buf ) != 0 )
			vr_note( VR_SEV_INFO, "perfil de interacao (mao %s): %s\n", h ? "direita" : "esquerda", buf );

		snprintf( dst, 128, "%s", buf );
	}
}

// xrSyncActions + leitura dos estados. Roda depois do xrBeginFrame (a pose e o
// predictedDisplayTime do frame já estão válidos).
static void vr_actions_sync( void )
{
	XrResult r;

	in_valid = false;

	if( xr_actionset == XR_NULL_HANDLE || xr_session == XR_NULL_HANDLE )
		return;

	XrActiveActionSet aas { xr_actionset, XR_NULL_PATH };
	XrActionsSyncInfo si {}; si.type = XR_TYPE_ACTIONS_SYNC_INFO;

	si.countActiveActionSets = 1;
	si.activeActionSets = &aas;

	r = xrSyncActions( xr_session, &si );
	if( XR_FAILED( r ))
	{
		static bool warned;

		if( !warned )
		{
			warned = true;
			vr_note( VR_SEV_WARN, "xrSyncActions: %d %s\n", ( int )r, vr_xr_result_str( ( int )r ));
		}
		return;
	}

	vr_query_profiles();

	XrActionStateGetInfo gi {}; gi.type = XR_TYPE_ACTION_STATE_GET_INFO;
	XrActionStateVector2f v2 {}; v2.type = XR_TYPE_ACTION_STATE_VECTOR2F;
	XrActionStateFloat fv {}; fv.type = XR_TYPE_ACTION_STATE_FLOAT;
	XrActionStateBoolean bl {}; bl.type = XR_TYPE_ACTION_STATE_BOOLEAN;

	gi.subactionPath = XR_NULL_PATH;

	in_move[0] = in_move[1] = in_turn[0] = in_turn[1] = 0.0f;
	gi.action = xr_act_move;
	if( XR_SUCCEEDED( xrGetActionStateVector2f( xr_session, &gi, &v2 )) && v2.isActive )
	{
		in_move[0] = v2.currentState.x;
		in_move[1] = v2.currentState.y;
	}
	gi.action = xr_act_turn;
	if( XR_SUCCEEDED( xrGetActionStateVector2f( xr_session, &gi, &v2 )) && v2.isActive )
	{
		in_turn[0] = v2.currentState.x;
		in_turn[1] = v2.currentState.y;
	}

	in_fire = in_altfire = in_squeeze = 0.0f;
	gi.action = xr_act_fire;
	if( XR_SUCCEEDED( xrGetActionStateFloat( xr_session, &gi, &fv )) && fv.isActive ) in_fire = fv.currentState;
	gi.action = xr_act_altfire;
	if( XR_SUCCEEDED( xrGetActionStateFloat( xr_session, &gi, &fv )) && fv.isActive ) in_altfire = fv.currentState;
	// squeeze: POR MAO (cada acao tem o subaction path da sua mao). O in_squeeze
	// segue sendo o maior dos dois, para o MOD_BTN_WALK nao mudar de comportamento.
	in_grip[0] = in_grip[1] = 0.0f;
	for( int h = 0; h < 2; h++ )
	{
		gi.action = xr_act_grip[h];
		gi.subactionPath = xr_path_hand[h];
		if( XR_SUCCEEDED( xrGetActionStateFloat( xr_session, &gi, &fv )) && fv.isActive )
			in_grip[h] = fv.currentState;
	}
	in_squeeze = ( in_grip[0] > in_grip[1] ) ? in_grip[0] : in_grip[1];

#define VR_BOOL( act, dst ) \
	do { \
		gi.action = ( act ); \
		( dst ) = ( XR_SUCCEEDED( xrGetActionStateBoolean( xr_session, &gi, &bl )) && bl.isActive && bl.currentState ); \
	} while( 0 )

	VR_BOOL( xr_act_jump,   in_jump );
	VR_BOOL( xr_act_reload, in_reload );
	VR_BOOL( xr_act_use,    in_use );
	VR_BOOL( xr_act_score,  in_score );
	VR_BOOL( xr_act_crouch, in_crouch_btn );
	VR_BOOL( xr_act_menu,   in_menu );
#undef VR_BOOL

	// poses das mãos: a POSIÇÃO alimenta o roomscale e a arma na mão; a
	// ORIENTAÇÃO é o eixo da mira quando VR_AIM_SOURCE é uma das mãos (Fase 6)
	for( int h = 0; h < 2; h++ )
	{
		XrSpaceLocation loc {}; loc.type = XR_TYPE_SPACE_LOCATION;
		float p[3];

		in_hand_ok[h] = false;

		if( xr_aim_space[h] == XR_NULL_HANDLE )
			continue;

		if( XR_FAILED( xrLocateSpace( xr_aim_space[h], xr_space, xr_fs.predictedDisplayTime, &loc )))
			continue;

		if(( loc.locationFlags & ( XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT ))
			!= ( XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT ))
			continue;

		p[0] = loc.pose.position.x;
		p[1] = loc.pose.position.y;
		p[2] = loc.pose.position.z;
		xr_to_gold( p, in_hand_g[h] );

		for( int i = 0; i < 3; i++ )
			in_hand_g[h][i] *= UNITS_PER_METER;

		in_hand_pose[h] = loc.pose;
		in_hand_ok[h] = true;
	}

	// pose da cabeça: o MEIO entre os olhos tira o IPD do caminho (o que o
	// roomscale quer é a cabeça, não um dos olhos)
	{
		float p[3] = {
			0.5f * ( xr_views[0].pose.position.x + xr_views[1].pose.position.x ),
			0.5f * ( xr_views[0].pose.position.y + xr_views[1].pose.position.y ),
			0.5f * ( xr_views[0].pose.position.z + xr_views[1].pose.position.z ),
		};

		xr_to_gold( p, vr_head_m );
		pose_to_viewangles( xr_views[0].pose, vr_head_raw );
	}

	if( !vr_neutral_valid )
	{
		vr_neutral_m[0] = vr_head_m[0];
		vr_neutral_m[1] = vr_head_m[1];
		vr_neutral_m[2] = vr_head_m[2];
		vr_neutral_valid = true;
	}

	in_valid = true;
}

// Callback da ABI (MOD_CAP_INPUT). Fora de uma sessão ativa devolve a amostra
// zerada — é o que mantém o jogo jogável no plano/flat, com mouse e teclado
// intactos, e o menu do jogo respondendo ao mouse.
// ---------------- viewmodel proprio: estado consultado pelo input ----------------
// O subsistema vive em vr_hands.cpp e nao depende da ABI do Mod. Estes sao os
// fatos que o VR_Input (mira e gatilho) precisa ANTES de a pose ser resolvida:
// o mod escreve aqui no VR_FramePre e le no VR_Input, no MESMO frame.
static bool vr_hands_test;   // VR_HANDS_TEST=1: modo de teste sem headset
static bool vr_hands_draw_ours;  // decisao do VR_ViewModel: o viewmodel e o nosso
static const char *vr_hands_fake_model;  // VR_HANDS_FAKE_MODEL: nome injetado (sem headset)
static bool  vr_hands_drew;              // ja desenhou nos olhos (log unico)

// Corpo do jogador: ancora do coldre e do chao. Definidos depois (usam o que o
// VR_Camera ja capturou).
static void vr_hands_body_fill( vr_hands_body_t *b );
static void vr_hands_frame( void );
static void vr_hands_fill( vr_hands_hand_t out[2] );

// Relato do frame do subsistema (o VR_Input consulta: mira e gatilho).
static vr_hands_state_t vr_hands_state;
static double vr_hands_t_prev;

static void VR_Input( mod_input_sample_t *s )
{
	// zerada a cada frame: se a sessão cair, a vignette não fica presa
	vr_vignette_now = 0.0f;

	if( !vr_active || !xr_views_ok || !in_valid )
		return;

	// ---- orientação: quem manda é a ARMA na mão; sem ela, a fonte da mira ----
	// Com o encaixe calibrado, a direção do CANO é a resposta certa: é o que faz o
	// grip de duas mãos (e o saque com qualquer das mãos) concordar com o que se vê.
	// VR_AIM_FROM_WEAPON=0 volta a mirar pela mão/cabeça (VR_AIM_SOURCE).
	int aim_hand = ( vr_aim_source == VR_AIM_LEFT ) ? 0 : ( vr_aim_source == VR_AIM_RIGHT ? 1 : -1 );
	const float *baim = vr_hands_state.aim;
	bool gun_aim = vr_aim_from_weapon && vr_hands_draw_ours && vr_hands_state.armed
		&& ( baim[0] != 0.0f || baim[1] != 0.0f || baim[2] != 0.0f );

	if( gun_aim )
	{
		// a direção já vem no espaço do MUNDO (o vr_hands_fill gira pelo snap turn),
		// então aqui NÃO se soma o vr_yaw_offset de novo
		s->viewangles_abs[0] = ( float )-rad2deg( asin( fmaxf( -1.0f, fminf( 1.0f, ( double )baim[2] ))));
		s->viewangles_abs[1] = wrap180( ( float )rad2deg( atan2( ( double )baim[1], ( double )baim[0] )));
		s->viewangles_abs[2] = 0.0f;
	}
	else if( aim_hand >= 0 && in_hand_ok[aim_hand] )
	{
		pose_to_viewangles( in_hand_pose[aim_hand], s->viewangles_abs );
		s->viewangles_abs[1] = wrap180( s->viewangles_abs[1] + vr_yaw_offset );
		s->viewangles_abs[2] = 0.0f; // roll do cmd fica 0; o render usa o do Mod_Camera
	}
	else
	{
		s->viewangles_abs[0] = vr_head_raw[0];
		s->viewangles_abs[1] = wrap180( vr_head_raw[1] + vr_yaw_offset );
		s->viewangles_abs[2] = 0.0f;
	}

	s->has_viewangles_abs = 1;

	// yaw da cabeça: é ele que a LOCOMOÇÃO deve seguir mesmo com a mira na mão
	float head_yaw = wrap180( vr_head_raw[1] + vr_yaw_offset );
	float d_move_yaw = wrap180( s->viewangles_abs[1] - head_yaw );

	// ---- botão de menu: recentrar (borda) ----
	if( in_menu && !in_menu_prev )
		vr_recenter();

	// ---- snap turn (stick direito) ----
	const float *mv = vr_left_handed ? in_turn : in_move;
	const float *tn = vr_left_handed ? in_move : in_turn;
	float tx = tn[0];

	// Com um MENU aberto o giro fica desligado (e o latch zerado): o painel do
	// menu está ancorado no mundo, então um toque acidental no stick o deixaria
	// para trás — e no CS você também não anda com o menu de compra aberto.
	// Olhar com a cabeça continua livre.
	if( vr_menu_open && vr_menu )
	{
		tx = 0.0f;
		vr_snap_latch = 0;
	}

	// o latch evita girar em rajada com o stick segurado: dispara uma vez e só
	// re-arma quando o stick volta para perto do centro
	if( tx > 0.7f && vr_snap_latch == 0 )
	{
		vr_yaw_offset -= vr_snap_angle; // direita = yaw diminui no GoldSrc
		vr_snap_latch = 1;
	}
	else if( tx < -0.7f && vr_snap_latch == 0 )
	{
		vr_yaw_offset += vr_snap_angle;
		vr_snap_latch = -1;
	}
	else if( fabsf( tx ) < 0.3f )
	{
		vr_snap_latch = 0;
	}

	vr_yaw_offset = wrap180( vr_yaw_offset );

	// ---- locomoção (stick esquerdo) ----
	float mx = mv[0], my = mv[1];
	float mag = sqrtf( mx * mx + my * my );

	if( mag < vr_deadzone )
	{
		mx = my = 0.0f; // centrado: o teclado continua valendo (ver IN_ModAppendMove)
	}
	else
	{
		float k = ( mag > 1.0f ? 1.0f : mag );

		// a deadzone é um piso, não um degrau: reescala o resto da faixa
		k = ( k - vr_deadzone ) / ( 1.0f - vr_deadzone );
		k *= vr_move_scale;
		mx = mx / mag * k;
		my = my / mag * k;
	}

	// O canal de ângulos é UM só: o pmove monta o wishdir com AngleVectors dos
	// ângulos do cmd, que agora são os da ARMA. Como o stick é montado aqui,
	// basta rodar o vetor por (mira − cabeça) para o movimento continuar saindo
	// do olhar — servidor e predição do cliente rodam o mesmo pmove, então não
	// há divergência de predição. Com a mira na cabeça o desvio é 0 (no-op).
	if( fabsf( d_move_yaw ) > 0.01f )
	{
		float c = ( float )cos( d_move_yaw * M_PI / 180.0 );
		float s = ( float )sin( d_move_yaw * M_PI / 180.0 );
		float f = my * c + mx * s;
		float sd = -my * s + mx * c;

		my = f;
		mx = sd;
	}

	// Efeito colateral de o pitch do cmd passar a ser o da ARMA: o pmove escala
	// o componente "frente" por cos(pitch), então apontar a arma para o chão
	// enquanto se anda frearia o jogador sem a cabeça ter culpa. Compensação
	// exata do 1/cos (opcional; VR_MOVE_PITCH_COMP, ver cabeçalho).
	if( vr_move_pitch_comp && aim_hand >= 0 )
	{
		float cp = ( float )cos( s->viewangles_abs[0] * M_PI / 180.0 );

		if( cp < 0.35f )
			cp = 0.35f; // ~69°; abaixo disso o 1/cos explode

		my /= cp;
	}

	s->move_forward = my;
	s->move_side = mx;
	s->move_up = tn[1]; // só tem efeito onde o jogo usa upmove (água/espectador)

	// ---- botões ----
	unsigned int st = 0;

	// Com menu aberto o gatilho direito é o CLIQUE do ponteiro (quem o manda ao
	// menu é o VR_Hud, como cursor virtual), não o gatilho da arma: o engine já
	// ignora movimento/tiro em menu, e assim o jogo não vê "atirando" por trás
	// da tela de compra.
	const bool menu_takes_trigger = ( vr_menu_open && vr_menu );

	// A ARMA TEM DE ESTAR NA MÃO. Vale só para a classe que nós desenhamos (com o
	// rifle equipado nada disso se aplica): guardar no coldre ou largar no chão
	// passa a ter consequência — o gatilho não atira. enforce_armed 0 no cfg desliga.
	const bool can_fire = !vr_hands_draw_ours || !vr_hands_enforce_armed() || vr_hands_state.armed;

	if( !menu_takes_trigger && in_fire > vr_trigger_th && can_fire ) st |= MOD_BTN_ATTACK;
	// (o recuo visual do viewmodel próprio saiu daqui: quem dispara o kick agora é
	// o vr_hands, na borda do gatilho que ele mesmo lê — vr_hands_frame)
	if( in_altfire > vr_trigger_th ) st |= MOD_BTN_ATTACK2;
	if( in_squeeze > vr_squeeze_th ) st |= MOD_BTN_WALK;
	if( in_jump ) st |= MOD_BTN_JUMP;
	if( in_reload ) st |= MOD_BTN_RELOAD;
	if( in_use ) st |= MOD_BTN_USE;
	if( in_score ) st |= MOD_BTN_SCORE;

	// agachar: físico (queda da cabeça com histerese) e/ou botão. O físico é
	// comparado com a altura do último recentrar, então jogar sentado não
	// dispara sozinho.
	if( vr_crouch_mode == 1 || vr_crouch_mode == 3 )
	{
		float drop = vr_neutral_m[2] - vr_head_m[2];

		if( !vr_duck_phys && drop > vr_crouch_drop )
			vr_duck_phys = true;
		else if( vr_duck_phys && drop < vr_crouch_drop * 0.5f )
			vr_duck_phys = false;
	}
	else
	{
		vr_duck_phys = false;
	}

	if( vr_duck_phys || (( vr_crouch_mode == 2 || vr_crouch_mode == 3 ) && in_crouch_btn ))
		st |= MOD_BTN_CROUCH;

	s->buttons_state = st;
	s->buttons_changed = st ^ vr_buttons_prev;
	vr_buttons_prev = st;

	in_menu_prev = in_menu;

	// ---- vignette de conforto ----
	// intensidade proporcional ao quanto você está andando de verdade
	float act = sqrtf( s->move_forward * s->move_forward + s->move_side * s->move_side );

	vr_vignette_now = vr_vignette * ( act > 1.0f ? 1.0f : act );
}

#include "vr_runtime.inc"

#include "vr_render.inc"

// ---------------- viewmodel proprio (bracos + arma) ----------------
// O subsistema vive em vr_hands.cpp e nao depende da ABI: recebe o que o mod ja
// sabe (poses das maos, corpo) e devolve o estado. Aqui so ligamos/desligamos.
static void vr_hands_log_adapter( const char *msg )
{
	vr_note( VR_SEV_INFO, "%s", msg );
}

// vr_eye_cam_t (do mod) -> vr_hands_cam_t (do subsistema): são tipos distintos
// de propósito, para o subsistema não depender do mod.
static void vr_hands_cam( vr_hands_cam_t *out, const vr_eye_cam_t &c )
{
	memcpy( out->origin, c.origin, sizeof( out->origin ));
	memcpy( out->angles, c.angles, sizeof( out->angles ));
	memcpy( out->fov,    c.fov,    sizeof( out->fov ));
	out->w = c.w; out->h = c.h; out->ok = c.ok;
}

// Poses das mãos no mundo, para o subsistema — definida junto do VR_AfterEyes
// (depende de vr_room_to_world/pose_basis_g, que ficam mais abaixo).

static int VR_Init( const mod_engine_api_t *e )
{
	eng = e;
	vr_paths_init(e->services);
	vr_t0 = e->DoubleTime();
	verbose = env_on( "VR_DEBUG" );
	vr_trace_all = env_on( "VR_TRACE" ); // rastreio do pipeline estéreo sem parar
	vr_debug_clear = env_on( "VR_DEBUG_CLEAR" );
	vr_scale = env_float( "VR_RESOLUTION_SCALE", 1.0f );
	if( vr_scale < 0.3f ) vr_scale = 0.3f;
	if( vr_scale > 3.0f ) vr_scale = 3.0f;

	// estéreo: base do IPD (runtime ou VR_IPD) + o ajuste fino que o teclado
	// escreve e o mods/vr_ipd.cfg guarda (ver a seção "ajuste fino do IPD").
	// O ajuste já NASCE no valor calibrado (VR_IPD_DELTA_DEFAULT_MM): sem isto o
	// jogo voltaria ao IPD do runtime a cada início até alguém recalibrar.
	vr_ipd = env_float( "VR_IPD", 0.0f ); // 0 = usar o IPD que o runtime informa
	vr_ipd_base_src = vr_ipd > 0.0f ? "VR_IPD" : "runtime";
	vr_ipd_delta = vr_ipd_clamp_mm( env_float( "VR_IPD_ADJUST", VR_IPD_DELTA_DEFAULT_MM ), NULL ) / 1000.0f;
	vr_ipd_steps = ( int )lroundf( vr_ipd_delta * 10000.0f );
	vr_ipd_delta_src = "default";
	vr_ipd_hud = !env_off( "VR_IPD_HUD" );
	vr_ipd_cfg_load(); // o arquivo (calibração ao vivo) tem a palavra final

	// menus no VR (Fase 4): painel 2D + ponteiro laser. Precedência, como no IPD:
	// padrão compilado < ambiente < game/mods/vr_menu.cfg. O template do .cfg é
	// criado só se faltar (nunca sobrescreve a calibração de quem já editou).
	vr_menu = !env_off( "VR_MENU" );
	vr_menu_laser = !env_off( "VR_MENU_LASER" );
	vr_menu_debug = env_on( "VR_MENU_DEBUG" );
	vr_menu_dist_m = fmaxf( VR_MENU_DIST_MIN_M, fminf( VR_MENU_DIST_MAX_M,
		env_float( "VR_MENU_DIST", vr_menu_dist_m )));
	vr_menu_height_m = fmaxf( VR_MENU_HEIGHT_MIN_M, fminf( VR_MENU_HEIGHT_MAX_M,
		env_float( "VR_MENU_HEIGHT", vr_menu_height_m )));
	vr_menu_cfg_seed();
	vr_menu_cfg_load();

	// input / locomoção (Fase 3): ver a seção "input VR" e o cabeçalho
	vr_deadzone = env_float( "VR_DEADZONE", 0.20f );
	vr_trigger_th = env_float( "VR_TRIGGER_THRESHOLD", 0.50f );
	vr_squeeze_th = env_float( "VR_SQUEEZE_THRESHOLD", 0.60f );
	vr_snap_angle = env_float( "VR_TURN_SNAP_ANGLE", 30.0f );
	vr_move_scale = env_float( "VR_MOVE_SCALE", 1.0f );
	vr_left_handed = env_on( "VR_LEFT_HANDED" );
	vr_crouch_mode = ( int )env_float( "VR_CROUCH_MODE", 3.0f );
	vr_crouch_drop = env_float( "VR_CROUCH_DROP", 0.25f );
	vr_roomscale = !env_off( "VR_ROOMSCALE" );
	vr_roomscale_max = env_float( "VR_ROOMSCALE_MAX", 0.6f );
	vr_vignette = env_float( "VR_COMFORT_VIGNETTE", 0.0f );

	// yaw do tronco com atraso (o coldre fica no tronco, nao na cabeca). 0 = como era.
	vr_body_yaw_lag = env_float( "VR_BODY_YAW_LAG", 0.0f );
	if( vr_body_yaw_lag < 0.0f ) vr_body_yaw_lag = 0.0f;
	if( vr_body_yaw_lag > 2.0f ) vr_body_yaw_lag = 2.0f;

	if( vr_deadzone < 0.0f ) vr_deadzone = 0.0f;
	if( vr_deadzone > 0.9f ) vr_deadzone = 0.9f;
	if( vr_snap_angle < 0.0f ) vr_snap_angle = 0.0f;
	if( vr_snap_angle > 180.0f ) vr_snap_angle = 180.0f;
	if( vr_move_scale < 0.0f ) vr_move_scale = 0.0f;
	if( vr_move_scale > 2.0f ) vr_move_scale = 2.0f;
	if( vr_crouch_mode < 0 ) vr_crouch_mode = 0;
	if( vr_crouch_mode > 3 ) vr_crouch_mode = 3;
	if( vr_crouch_drop < 0.05f ) vr_crouch_drop = 0.05f;
	if( vr_roomscale_max < 0.0f ) vr_roomscale_max = 0.0f;
	if( vr_vignette < 0.0f ) vr_vignette = 0.0f;
	if( vr_vignette > 1.0f ) vr_vignette = 1.0f;

	// arma na mão / mira (Fase 6): ver a seção "arma na mão" e o cabeçalho
	vr_aim_source = VR_AIM_RIGHT;
	{
		const char *as = getenv( "VR_AIM_SOURCE" );

		if( as && *as )
		{
			if( !strcmp( as, "head" ))        vr_aim_source = VR_AIM_HEAD;
			else if( !strcmp( as, "left" ))   vr_aim_source = VR_AIM_LEFT;
			else if( !strcmp( as, "right" ))  vr_aim_source = VR_AIM_RIGHT;
			else logf( "VR_AIM_SOURCE invalido (%s) — usando right\n", as );
		}
	}

	vr_wpn_world_depth = !env_off( "VR_WEAPON_WORLD_DEPTH" );
	vr_move_pitch_comp = env_on( "VR_MOVE_PITCH_COMP" );
	vr_wpn_debug = env_on( "VR_WEAPON_DEBUG" );
	// mira pelo CANO da arma (padrão ligado): VR_AIM_FROM_WEAPON=0 volta a mirar
	// pela mão/cabeça, como antes
	vr_aim_from_weapon = !env_off( "VR_AIM_FROM_WEAPON" );

	// offset sintético de teste (diagnóstico sem headset; ver VR_ViewModel)
	{
		const char *ts = getenv( "VR_WEAPON_TEST" );

		if( ts && *ts && sscanf( ts, "%f %f %f", &vr_wpn_test[0], &vr_wpn_test[1], &vr_wpn_test[2] ) == 3 )
		{
			vr_wpn_test_on = true;
			logf( "VR_WEAPON_TEST=%.0f %.0f %.0f (offset fixo a partir do olho)\n",
				( double )vr_wpn_test[0], ( double )vr_wpn_test[1], ( double )vr_wpn_test[2] );
		}
	}

	// encaixe: começa neutro (a arma nasce no eixo da mão) e o .cfg, se existir,
	// sobrepõe — inclusive por classe de arma
	memset( &vr_wpn, 0, sizeof( vr_wpn ));
	vr_wpn_class = VR_WPN_CLASSES - 1;
	vr_wpn_cfg_load( true );

	// painel de debug da tela 2D (ver cabeçalho). VR_DEBUG_PANEL=0 desliga.
	{
		const char *p = getenv( "VR_DEBUG_PANEL" );
		const char *dump = getenv( "VR_PANEL_DUMP" );

		vr_panel = !( p && *p && strcmp( p, "0" ) == 0 );
		vr_panel_scale = ( int )env_float( "VR_DEBUG_PANEL_SCALE", 2.0f );
		if( vr_panel_scale < 1 ) vr_panel_scale = 1;
		if( vr_panel_scale > 3 ) vr_panel_scale = 3;

		if( dump && *dump )
		{
			snprintf( vr_panel_dump_path, sizeof( vr_panel_dump_path ), "%s", dump );
			vr_panel_dump_at = env_float( "VR_PANEL_DUMP_AT", 2.0f );
		}
	}

	// dump do par estéreo (VR_STEREO_DUMP=prefixo): grava <prefixo>-eye0.ppm e
	// <prefixo>-eye1.ppm do mesmo quadro para medir o par fora do headset
	{
		const char *sd = getenv( "VR_STEREO_DUMP" );

		if( sd && *sd )
		{
			snprintf( vr_stereo_dump_prefix, sizeof( vr_stereo_dump_prefix ), "%s", sd );
			vr_stereo_dump_at = env_float( "VR_STEREO_DUMP_AT", 6.0f );
		}
	}

	vr_note( VR_SEV_INFO, "mod_vr: init (api %d, escala %.2f, ipd %.1f mm base %s ajuste %+.1f, painel %s x%d)\n",
		MOD_API_VERSION, vr_scale, ( double )( vr_ipd_eff() * 1000.0f ), vr_ipd_base_src,
		( double )( vr_ipd_delta * 1000.0f ), vr_panel ? "on" : "off", vr_panel_scale );

	// ambiente: as causas mais comuns de VR que não sobe aparecem aqui
	{
		const char *rt = getenv( "XR_RUNTIME_JSON" );
		if( rt && *rt )
			vr_note( VR_SEV_INFO, "XR_RUNTIME_JSON=%s\n", rt );
		else
			vr_note( VR_SEV_WARN, "XR_RUNTIME_JSON nao definido (loader usa o runtime do sistema)\n" );
	}

	detect_video_driver();
	vr_note( VR_SEV_INFO, "video driver do SDL: %s\n", vr_video_driver );
	if( strcmp( vr_video_driver, "x11" ) != 0 )
		vr_note( VR_SEV_WARN, "o mod precisa de X11/GLX: rode com SDL_VIDEODRIVER=x11 (Wayland usa EGL, sem GLX)\n" );

	if( !gl_init())
	{
		vr_note( VR_SEV_ERROR, "sem libGL: mod inativo, jogo flat\n" );
		return 0; // recusa: sem GL não há nem painel
	}

	// viewmodel proprio: falha aqui NAO derruba o mod (sem os assets, o mod
	// simplesmente segue com o viewmodel do jogo, como antes).
	vr_hands_test = env_on( "VR_HANDS_TEST" );
	vr_hands_fake_model = getenv( "VR_HANDS_FAKE_MODEL" );
	vr_hands_init( vr_hands_log_adapter );
	// folha de contato (diagnostico sem headset): VR_HANDS_SHEET=fit|weapon|curl|states
	{
		const char *sh = getenv( "VR_HANDS_SHEET" );

		if( sh && *sh )
		{
			int m = VR_SHEET_FIT;

			if     ( !strcmp( sh, "weapon" )) m = VR_SHEET_WEAPON;
			else if( !strcmp( sh, "curl" ))   m = VR_SHEET_CURL;
			else if( !strcmp( sh, "states" )) m = VR_SHEET_STATES;
			else if( !strcmp( sh, "walk" ))   m = VR_SHEET_WALK;
			vr_hands_set_sheet( m );
			logf( "VR_HANDS_SHEET=%s (folha de contato ligada)\n", sh );
			vr_hands_test = true;   // a folha so desenha no caminho sem headset
		}
	}

	// GLX só na main thread (contexto atual); o resto do init roda fora —
	// o xrCreateInstance do WiVRn bloqueia sem headset e congelaria o jogo
	if( !capture_glx())
	{
		vr_note( VR_SEV_ERROR, "sem contexto GLX (display=%p ctx=%p drawable=%p, video=%s): jogo flat\n",
			(void *)g_glbind.xDisplay, (void *)g_glbind.glxContext, (void *)g_glbind.glxDrawable,
			vr_video_driver );
		return 1;
	}

	// o binding XR_KHR_opengl_enable (Xlib) leva contexto, visual e fbconfig:
	// registrar os três ajuda quando o runtime recusa em xrCreateSession
	vr_note( VR_SEV_INFO, "GLX ok: ctx %p visual %u fbconfig %p\n",
		(void *)g_glbind.glxContext, ( unsigned )g_glbind.visualid, (void *)g_glbind.glxFBConfig );
	snprintf( vr_glx_info, sizeof( vr_glx_info ), "GLX ok (visual %u)", ( unsigned )g_glbind.visualid );

	// versão de GL do contexto (precisa ser lida na main thread, onde o
	// contexto está current) — o runtime exigirá uma faixa de versões
	if( vrglGetString )
	{
		const GLubyte *gv = vrglGetString( GL_VERSION );

		if( gv )
		{
			snprintf( vr_gl_version, sizeof( vr_gl_version ), "%s", ( const char * )gv );
			if( sscanf( vr_gl_version, "%d.%d", &vr_gl_major, &vr_gl_minor ) != 2 )
			{
				vr_gl_major = vr_gl_minor = 0;
			}
		}
	}

	xr_init_started = true;
	vr_async_start(xr_init_thread);

	vr_note(VR_SEV_INFO,"OpenXR iniciando em segundo plano; jogo flat continua responsivo\n");

	return 1;
}

static void VR_StopSession()
{
    if(xr_session!=XR_NULL_HANDLE) {
        xr_end_frame();
        if(xr_session_begun) xrRequestExitSession(xr_session);
        for(int h=0;h<2;h++)if(xr_aim_space[h]!=XR_NULL_HANDLE){xrDestroySpace(xr_aim_space[h]);xr_aim_space[h]=XR_NULL_HANDLE;}
        if(xr_space!=XR_NULL_HANDLE){xrDestroySpace(xr_space);xr_space=XR_NULL_HANDLE;}
        for(auto& eye:xr_eye){
            if(!eye.fbos.empty())pglDeleteFramebuffers((GLsizei)eye.fbos.size(),eye.fbos.data());
            if(eye.depth)pglDeleteRenderbuffers(1,&eye.depth);
            if(eye.sc!=XR_NULL_HANDLE)xrDestroySwapchain(eye.sc);
            eye=EyeRT{};
        }
        xrDestroySession(xr_session);xr_session=XR_NULL_HANDLE;
    }
    if(xr_actionset!=XR_NULL_HANDLE){xrDestroyActionSet(xr_actionset);xr_actionset=XR_NULL_HANDLE;}
    vr_active=false;vr_ready=false;in_valid=false;xr_views_ok=false;vr_buttons_prev=0;
    xr_session_begun=false;xr_frame_began=false;vr_session_state=-1;
    xr_setup_next_t=0;xr_setup_permanent=false;xr_setup_last_err=0x7fffffff;
}

static void VR_Shutdown( void )
{
    VR_StopSession();vr_hands_shutdown();vr_async_finish();
    if(!vr_async_done()) { vr_note(VR_SEV_INFO,"OpenXR ainda iniciando: biblioteca permanece carregada até sair\n"); return; }
    if(xr_instance!=XR_NULL_HANDLE){xrDestroyInstance(xr_instance);xr_instance=XR_NULL_HANDLE;}
    vr_note(VR_SEV_INFO,"VR encerrado\n");
}

static int VR_Enable(int enabled)
{
    if(!enabled){
        VR_StopSession();
        vr_note(VR_SEV_INFO,"VR desativado: câmera, ponteiro e entrada devolvidos ao jogo\n");
    }else{
        if(xr_init_failed&&vr_async_done()){
            vr_async_finish();xr_init_done=false;xr_init_failed=false;xr_instance_ready=false;
            vr_async_start(xr_init_thread);
        }
        vr_note(VR_SEV_INFO,"VR ativado; aguardando runtime e rastreio quando necessário\n");
    }
    return 1;
}

static void VR_DebugPanel(const csmod_ui_t *ui)
{
    char line[256];
    if(xr_init_failed)ui->Text("OpenXR indisponível: o jogo continua flat. Desative e ative para tentar novamente.");
    snprintf(line,sizeof(line),"Runtime: %s · sessão: %s",xr_init_done?vr_runtime:"iniciando",vr_session_state_str(vr_session_state.load()));ui->Text(line);
    snprintf(line,sizeof(line),"%.1f FPS · %.2f ms/frame · olhos %dx%d",(double)vr_fps,(double)vr_frame_ms,vr_eye_w,vr_eye_h);ui->Text(line);
    vr_ipd_state(line,sizeof(line));ui->Text(line);
    ui->Text("F9: calibração do IPD. A visibilidade deste painel não ativa o VR.");
}

static void VR_FramePre( double time )
{
	// FPS: conta todo frame, com ou sem VR (é a taxa real do jogo na janela)
	vr_frames++;
	{
		double now = eng ? eng->DoubleTime() : time;

		if( vr_fps_t0 == 0.0 )
		{
			vr_fps_t0 = now;
			vr_fps_frames0 = vr_frames;
		}
		else if( now - vr_fps_t0 >= 0.5 )
		{
			unsigned long df = vr_frames - vr_fps_frames0;
			double dt = now - vr_fps_t0;

			if( dt > 0.0 )
			{
				vr_fps = ( float )( df / dt );
				vr_frame_ms = ( float )( dt * 1000.0 / ( df ? ( double )df : 1.0 ));
			}
			vr_fps_t0 = now;
			vr_fps_frames0 = vr_frames;
		}
	}


	vr_wpn_cfg_tick(); // encaixe da arma: relê mods/vr_weapon.cfg se ele mudou
	vr_hands_tick();   // viewmodel próprio: relê mods/vr_hands.cfg se ele mudou
	vr_ipd_cfg_tick(); // ajuste do IPD: relê mods/vr_ipd.cfg se ele mudou
	vr_menu_cfg_tick(); // painel dos menus: relê mods/vr_menu.cfg se ele mudou

	if( !vr_ready )
	{
		// Session setup roda AQUI (main thread, contexto GLX current) e re-tenta
		// de 1 em 1 s: com o headset desligado o runtime pode recusar e passar a
		// aceitar depois, sem precisar reiniciar o jogo.
		if( xr_instance_ready.load() && !xr_setup_permanent && xr_instance != XR_NULL_HANDLE && eng )
		{
			double now = eng->DoubleTime();

			if( now >= xr_setup_next_t )
			{
				xr_setup_next_t = now + 1.0;
				xr_setup_session();
			}
		}
		return;
	}

	xr_poll_events();

	// Bombeia frames a partir do xrBeginSession, mesmo antes de ficar VISIBLE
	// (SYNCHRONIZED): sem isso o runtime não avança o estado e a sessão fica
	// presa — HMD preto, sem erro. Só desenhamos nos olhos quando VISIBLE/FOCUSED
	// (vr_active); fora disso o quadro é submetido vazio (layerCount 0).
	if( xr_session != XR_NULL_HANDLE && xr_session_begun )
	{
		if( !xr_begin_frame())
			vr_active = false;
		else
		{
			// input: xrSyncActions + leitura dos estados, com a pose e o
			// predictedDisplayTime deste frame já em mãos
			vr_actions_sync();

			// viewmodel próprio: 1x por FRAME (não por olho) — resolve a pose dos
			// braços, os dedos, o encaixe da arma e o ESTADO (mão/coldre/chão).
			// Tem de ser aqui: é o que o VR_Input deste frame vai consultar.
			vr_hands_frame();

			double now = eng ? eng->DoubleTime() : 0.0;

			vr_stereo_frames++;
			vr_perf_n++;
			vr_perf_flush();
			if( vr_active_since <= 0.0 )
				vr_active_since = now;

			// A cada ~5 s diz onde o pipeline parou: sem VISIBLE (headset/
			// streaming) ou sem render 3D (menu/carregando, quando o engine não
			// chama o hook de câmera). São as duas causas de HMD preto mudo.
			if( now > 0.0 && vr_frames % 300 == 0 )
			{
				if( !vr_active.load())
				{
					vr_note( VR_SEV_WARN, "sessao %s: ainda nao VISIBLE - o HMD mostra preto ate o runtime liberar o streaming\n",
						vr_session_state.load() < 0 ? "-" : vr_session_state_str( vr_session_state.load()));
				}
				else if( !xr_views_ok.load())
				{
					// sessão viva mas SEM poses: o headset parou de rastrear
					// (tirado da cabeça, dormindo, streaming caído). Sem poses o
					// mod não renderiza estéreo e o jogo cai para flat — o
					// headset mostra o espelho preto/parado.
					vr_note( VR_SEV_WARN, "sem poses do HMD (headset parado/dormindo?): fora do rastreio o jogo roda flat e o HMD fica sem imagem\n" );
				}
				else
				{
					double last = vr_last_eye_render_t > 0.0 ? vr_last_eye_render_t : vr_active_since;

					if( last > 0.0 && now - last > 1.0 )
						vr_note( VR_SEV_WARN, "sem render 3D nos olhos ha %.1fs (menu ou carregando?): HMD preto ate entrar num mapa\n",
							now - last );
				}
			}
		}
	}
	else
	{
		vr_active_since = 0.0; // re-arma o aviso quando a sessão parar
	}
}

static int VR_EyeCount( void )
{
	return ( vr_active && xr_views_ok && xr_eye[0].sc != XR_NULL_HANDLE ) ? 2 : 1;
}

// ---------------- menus no VR (Fase 4: HUD/menus no headset) ----------------
// O QUE ISTO FAZ
// Com um menu aberto (esc, m = time, b = compra, classe), o desenho 2D do engine
// — que é onde o menu vive — é capturado num canvas offscreen (R_HudCanvas*, no
// ref) e este Mod desenha esse canvas como um PAINEL no mundo: na frente e na
// altura dos olhos, ancorado no instante em que o menu abriu (NÃO segue a
// cabeça), com uma moldura. Um laser sai do eixo da mão DIREITA e vale como o
// mouse — aponta para os itens e o gatilho direito clica.
//
// POR QUE ASSIM: os menus são do jogo (mainui + g_pMenu do client). Refazê-los
// seria perder o conteúdo real (itens de compra, times, classes). Então o
// caminho é sempre: capturar o 2D → mostrar no headset; e para o ponteiro,
// deixar o ENGINE crer que o mouse está onde o laser aponta.
//
// QUEM DECIDE O QUÊ (ABI v8, mod_hud_t)
// - o ENGINE informa os FATOS: menu aberto (`key_dest != key_game` — o menu do
//   ESC e os do client, time/classe/compra, todos marcam KEY_MENU), o tamanho do
//   espaço 2D e a textura do canvas;
// - este MOD decide a POLÍTICA: pedir o canvas quando há menu, onde fica o
//   painel, para onde o laser aponta e quando clicar. Sem menu aberto o Mod não
//   pede NADA (nem canvas, nem cursor, nem clique) — o jogo normal segue igual.
//
// CINCO ARMADILHAS QUE ESTE DESENHO JÁ EVITA
// 1. o cursor do menu tem DOIS consumidores (o UI_MouseMove do engine e o
//    GetMousePosition que o client DLL usa nos menus dele) e o engine passou os
//    dois pelo mesmo override — se o Mod mandasse o cursor para um só, o outro o
//    puxaria de volta e o hover piscaria;
// 2. o canvas desenhado aqui é o do frame ANTERIOR (o 2D deste frame só acontece
//    depois dos olhos), então o primeiro frame de cada abertura é pulado (texnum
//    0): ~11 ms de atraso na imagem do menu, imperceptível;
// 3. um gatilho JÁ apertado quando o menu abre não pode clicar (senão abrir o
//    menu com o gatilho do tiro preso compraria um item): o clique exige começar
//    com o gatilho solto (vr_menu_arm);
// 4. a âncora nasce das CÂMERAS (que já têm snap turn e roomscale) mas o painel
//    é um objeto do MUNDO: recalcular por frame o faria seguir a cabeça;
// 5. o mundo continua desenhado atrás (o canvas é transparente onde a UI não
//    pinta) e o painel/laser são desenhados sem depth: menu nunca fica escondido
//    atrás de parede.

static void vr_vsub( const float a[3], const float b[3], float out[3] )
{
	for( int i = 0; i < 3; i++ )
		out[i] = a[i] - b[i];
}

// out = a + b * k
static void vr_vmad( const float a[3], float k, const float b[3], float out[3] )
{
	for( int i = 0; i < 3; i++ )
		out[i] = a[i] + b[i] * k;
}

static void vr_vscale( const float a[3], float k, float out[3] )
{
	for( int i = 0; i < 3; i++ )
		out[i] = a[i] * k;
}

// Base dos ângulos de view no MESMO sentido do engine — cópia fiel do
// AngleVectors de public/xash3d_mathlib.h. É o que garante que a projeção do
// painel caia exatamente onde o engine desenhou aquele olho: reconstruir a base
// "por fora" (ou usar a pose do runtime) daria erro de fração de grau e o painel
// apareceria deslocado do que o olho vê.
static void vr_angles_basis( const float ang[3], float fwd[3], float rgt[3], float up[3] )
{
	const double sy = sin( ang[1] * M_PI / 180.0 ), cy = cos( ang[1] * M_PI / 180.0 );
	const double sp = sin( ang[0] * M_PI / 180.0 ), cp = cos( ang[0] * M_PI / 180.0 );
	const double sr = sin( ang[2] * M_PI / 180.0 ), cr = cos( ang[2] * M_PI / 180.0 );

	fwd[0] = ( float )( cp * cy );       fwd[1] = ( float )( cp * sy );       fwd[2] = ( float )( -sp );
	rgt[0] = ( float )( -sr * sp * cy + cr * sy );
	rgt[1] = ( float )( -sr * sp * sy - cr * cy );
	rgt[2] = ( float )( -sr * cp );
	up[0]  = ( float )( cr * sp * cy + sr * sy );
	up[1]  = ( float )( cr * sp * sy - sr * cy );
	up[2]  = ( float )( cr * cp );
}

// ponto do MUNDO → pixel no FBO do olho. O frustum é ASSIMÉTRICO (fov_asym, no
// sinal do OpenXR: left/down negativos), ou seja o eixo da câmera NÃO é o centro
// da imagem — cada lado tem o seu tan. A conta é a mesma do engine
// (Matrix4x4_CreateProjection + divide perspectiva):
//   ndc = (2·x/z − (tanMax+tanMin)) / (tanMax−tanMin)
// Devolve false para o que está atrás da câmera (z ≤ 0): quem chama pula a
// célula em vez de desenhar um polígono espelhado.
static bool vr_eye_project( int eye, const float p[3], float *px, float *py )
{
	const vr_eye_cam_t &c = vr_eye_cam[eye];
	float fwd[3], rgt[3], up[3], d[3];
	float x_c, y_c, z_c, ndc_x, ndc_y;
	const float tanL = tanf( c.fov[0] ), tanR = tanf( c.fov[1] );
	const float tanD = tanf( c.fov[3] ), tanU = tanf( c.fov[2] );

	if( !c.ok || c.w <= 0 || c.h <= 0 )
		return false;

	vr_angles_basis( c.angles, fwd, rgt, up );
	vr_vsub( p, c.origin, d );

	x_c = dot3( d, rgt );
	y_c = dot3( d, up );
	z_c = dot3( d, fwd );

	if( z_c <= 1.0f ) // atrás (ou colado) na câmera
		return false;

	ndc_x = ( 2.0f * x_c / z_c - ( tanR + tanL )) / ( tanR - tanL );
	ndc_y = ( 2.0f * y_c / z_c - ( tanU + tanD )) / ( tanU - tanD );

	*px = ( ndc_x + 1.0f ) * 0.5f * ( float )c.w;
	*py = ( 1.0f - ndc_y ) * 0.5f * ( float )c.h;
	return true;
}

// Ponto do espaço do QUARTO (unidades GoldSrc, eixos GoldSrc — como in_hand_g)
// para o MUNDO. É a MESMA relação que a câmera usa: base do olho do player vinda
// do engine (sem roomscale, que é o que estamos repondo) + R_yaw(q − neutro).
// Diferença deliberada: aqui o VERTICAL entra — a câmera o ignora porque o
// agachar já move o olho pelo jogo, mas a MÃO precisa dele, senão o laser sairia
// da altura errada.
static void vr_room_to_world( const float q[3], float out[3] )
{
	float d[3];

	if( !vr_neutral_valid || !vr_menu_eye_base_ok )
	{
		out[0] = q[0];
		out[1] = q[1];
		out[2] = q[2];
		return;
	}

	d[0] = q[0] - vr_neutral_m[0] * UNITS_PER_METER;
	d[1] = q[1] - vr_neutral_m[1] * UNITS_PER_METER;
	d[2] = q[2] - vr_neutral_m[2] * UNITS_PER_METER;

	vr_rotate_yaw_offset( d ); // só o horizontal (z preservado de propósito)

	out[0] = vr_menu_eye_base[0] + d[0];
	out[1] = vr_menu_eye_base[1] + d[1];
	out[2] = vr_menu_eye_base[2] + d[2];
}

// Congela a âncora: "na frente e na altura dos olhos, no instante em que o menu
// abriu". Sem pitch e sem roll — painel em pé, de frente para onde o player
// olhava na horizontal (é o que um menu de tela faz; um painel torto pelo pitch
// do momento ficaria fora de alcance). Chamado no fim do VR_Camera do olho 0,
// onde `view` já traz roomscale e snap turn aplicados.
static void vr_menu_capture_anchor( const mod_view_t *view )
{
	const float yaw = ( float )(( view->viewangles[1] + vr_menu_yaw_off ) * M_PI / 180.0 );
	const float cy = cosf( yaw ), sy = sinf( yaw );
	const float ratio = vr_menu_canvas_h > 0
		? ( float )vr_menu_canvas_w / ( float )vr_menu_canvas_h
		: 16.0f / 9.0f;

	// frente e direita HORIZONTAIS — as mesmas do AngleVectors com pitch 0.
	// A direita tem de ser fwd × up = (sy, −cy, 0); com o X invertido o painel
	// saía ESPELHADO em todo yaw com seno não nulo e cisalhado nos demais (o
	// "direita" deixava de ser perpendicular à frente) — era o espelhamento do
	// menu de compra. Ver o teste numérico em /tmp/vr_menu_math.py.
	const float fwd_h[3] = { cy, sy, 0.0f };
	const float rgt_h[3] = { sy, -cy, 0.0f };

	vr_menu_half_h = 0.5f * vr_menu_height_m * UNITS_PER_METER;
	vr_menu_half_w = vr_menu_half_h * ratio;

	for( int i = 0; i < 3; i++ )
	{
		vr_menu_anchor[i] = view->vieworigin[i] + fwd_h[i] * ( vr_menu_dist_m * UNITS_PER_METER );
		vr_menu_right[i] = rgt_h[i];
		vr_menu_norm[i] = -fwd_h[i]; // normal aponta DE VOLTA para o jogador
	}

	vr_menu_anchor[2] += vr_menu_y_off_m * UNITS_PER_METER;

	vr_menu_anchor_ok = true;
	vr_menu_need_anchor = false;
	vr_menu_ptr_ok = false;
	vr_menu_ptr_x = vr_menu_canvas_w * 0.5f;
	vr_menu_ptr_y = vr_menu_canvas_h * 0.5f;

	vr_note( VR_SEV_INFO, "menu: painel ancorado em (%.0f %.0f %.0f), dist %.2f m, altura %.2f m, canvas %dx%d, yaw %.1f\n",
		( double )vr_menu_anchor[0], ( double )vr_menu_anchor[1], ( double )vr_menu_anchor[2],
		( double )vr_menu_dist_m, ( double )vr_menu_height_m, vr_menu_canvas_w, vr_menu_canvas_h,
		( double )view->viewangles[1] );
}

// O raio: eixo da mão DIREITA (pose `aim`, a mesma que serve a mira da arma)
// contra o plano do painel. Dentro → vira pixel do canvas; fora → o cursor fica
// onde estava e o clique não vale (apontar para o vazio não clica em item).
static void vr_menu_ray( void )
{
	float fwd[3], rgt[3], up[3], O[3], d[3], rel[3], hit[3];
	float dn, t, u, v;

	vr_menu_hit = false;

	if( !vr_menu_anchor_ok || !in_hand_ok[1] )
		return;

	pose_basis_g( in_hand_pose[1], fwd, rgt, up );
	(void)rgt; (void)up;

	vr_room_to_world( in_hand_g[1], O ); // origem do laser: a mão direita no mundo

	vr_vsub( vr_menu_anchor, O, d );
	dn = dot3( fwd, vr_menu_norm );

	if( dn >= -1e-4f ) // olhando para o lado oposto do painel
		return;

	// t do raio contra o plano: dot(A−O, n) / dot(D, n), com dn = dot(D, n) já
	// negativo (o raio vai CONTRA a normal, de frente para o painel). Era aqui
	// que estava o sinal trocado: com `/ -dn` o t saía negativo para todo alvo
	// à frente e o laser nunca acertava nada (pego pelo teste numérico em
	// /tmp/vr_menu_math.py, antes de ir para o headset).
	t = dot3( d, vr_menu_norm ) / dn;

	if( t <= 0.0f ) // o painel está atrás da mão
		return;

	vr_vmad( O, t, fwd, hit );
	vr_vsub( hit, vr_menu_anchor, rel );

	u = dot3( rel, vr_menu_right );
	v = rel[2]; // o "para cima" do painel é o +Z do mundo (painel em pé)

	if( fabsf( u ) > vr_menu_half_w || fabsf( v ) > vr_menu_half_h )
		return;

	// pixel do canvas: u cresce para a direita, v para cima, e o pixel y cresce
	// para baixo (a UI do jogo é medida de cima para baixo)
	vr_menu_ptr_x = ( 0.5f + 0.5f * u / vr_menu_half_w ) * ( float )vr_menu_canvas_w;
	vr_menu_ptr_y = ( 0.5f - 0.5f * v / vr_menu_half_h ) * ( float )vr_menu_canvas_h;
	vr_menu_ptr_ok = true;
	vr_menu_hit = true;

	memcpy( vr_menu_hit_world, hit, sizeof( hit ));
}

// Desenha o painel + laser no FBO do olho. Roda em Mod_AfterEyes: DEPOIS do
// mundo dos dois olhos (então o painel cobre o mundo) e ANTES do blit do espelho
// (então o monitor também mostra o painel — é o que permite testar sem tirar o
// headset).
static void vr_menu_draw( int eye )
{
	enum { GRID = 6 }; // subdivisões por eixo: a projeção de um retângulo é uma
	                   // homografia, e a grade de quads mata o erro da
	                   // interpolação afim de cada triângulo
	float px[GRID + 1][GRID + 1], py[GRID + 1][GRID + 1];
	bool  vok[GRID + 1][GRID + 1];
	float w[3], P[3];
	GLint prev_draw = 0, prev_read = 0;
	int W, H;

	if( !vr_menu || !vr_menu_anchor_ok || !vr_menu_canvas_tex )
		return;

	if( eye < 0 || eye > 1 || !vr_eye_cam[eye].ok || xr_eye[eye].cur < 0 || xr_eye[eye].fbos.empty() || !vr_panel_gl_ok )
		return;

	W = xr_eye[eye].w;
	H = xr_eye[eye].h;

	if( W <= 0 || H <= 0 )
		return;

	// projeta a grade do painel
	for( int i = 0; i <= GRID; i++ )
	{
		const float u = -vr_menu_half_w + ( 2.0f * vr_menu_half_w ) * ( float )i / ( float )GRID;

		for( int j = 0; j <= GRID; j++ )
		{
			const float v = vr_menu_half_h - ( 2.0f * vr_menu_half_h ) * ( float )j / ( float )GRID;

			vr_vscale( vr_menu_right, u, P );
			vr_vmad( vr_menu_anchor, 1.0f, P, w ); // w = anchor + right*u
			w[2] += v;                             // up do painel = +Z do mundo

			vok[i][j] = vr_eye_project( eye, w, &px[i][j], &py[i][j] );
		}
	}

	pglGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw );
	pglGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &prev_read );
	pglBindFramebuffer( GL_FRAMEBUFFER, xr_eye[eye].fbos[xr_eye[eye].cur] );

	vrglPushAttrib( GL_ALL_ATTRIB_BITS );
	vrglMatrixMode( GL_PROJECTION ); vrglPushMatrix(); vrglLoadIdentity();
	vrglOrtho( 0.0, ( GLdouble )W, ( GLdouble )H, 0.0, -1.0, 1.0 ); // y para baixo
	vrglMatrixMode( GL_MODELVIEW ); vrglPushMatrix(); vrglLoadIdentity();
	vrglDisable( GL_DEPTH_TEST );   // o menu nunca fica atrás de parede
	vrglDisable( GL_CULL_FACE );
	vrglDisable( GL_ALPHA_TEST );
	vrglDisable( GL_FOG );
	vrglDisable( GL_SCISSOR_TEST );
	vrglEnable( GL_BLEND );
	vrglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	vrglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );

	// moldura: um fundo escuro um pouco maior que o painel, para o menu não
	// competir com um céu claro atrás (leitura)
	{
		float mins[2] = { 1e9f, 1e9f }, maxs[2] = { -1e9f, -1e9f };
		bool any = false;

		for( int i = 0; i <= GRID; i++ )
			for( int j = 0; j <= GRID; j++ )
				if( vok[i][j] )
				{
					const float m = 10.0f; // margem em pixels do FBO do olho
					if( px[i][j] - m < mins[0] ) mins[0] = px[i][j] - m;
					if( py[i][j] - m < mins[1] ) mins[1] = py[i][j] - m;
					if( px[i][j] + m > maxs[0] ) maxs[0] = px[i][j] + m;
					if( py[i][j] + m > maxs[1] ) maxs[1] = py[i][j] + m;
					any = true;
				}

		if( any )
		{
			const float bg[4] = { 0.02f, 0.03f, 0.05f, 0.85f };
			panel_rect( mins[0], mins[1], maxs[0], maxs[1], bg );
		}
	}

	// o canvas: uma textura por célula da grade. O texnum que o engine manda é o
	// ID do GL (R_HudCanvasInfo converte o índice interno dele), e o engine já
	// invalidou o cache de textura do ref depois do Mod desenhar — ver
	// R_InvalidateTextures. O filtro LINEAR é do Mod porque o canvas nasce
	// NOMIPMAP (NEAREST): sem isto o menu sai pixelado ao ser ampliado no painel.
	vrglEnable( GL_TEXTURE_2D );
	vrglBindTexture( GL_TEXTURE_2D, ( GLuint )vr_menu_canvas_tex );
	vrglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	vrglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

	for( int i = 0; i < GRID; i++ )
	{
		for( int j = 0; j < GRID; j++ )
		{
			const float s0 = ( float )i / ( float )GRID, s1 = ( float )( i + 1 ) / ( float )GRID;
			const float t0 = 1.0f - ( float )j / ( float )GRID, t1 = 1.0f - ( float )( j + 1 ) / ( float )GRID;

			if( !vok[i][j] || !vok[i + 1][j] || !vok[i + 1][j + 1] || !vok[i][j + 1] )
				continue;

			vrglBegin( GL_QUADS );
				vrglTexCoord2f( s0, t0 ); vrglVertex2f( px[i][j], py[i][j] );
				vrglTexCoord2f( s1, t0 ); vrglVertex2f( px[i + 1][j], py[i + 1][j] );
				vrglTexCoord2f( s1, t1 ); vrglVertex2f( px[i + 1][j + 1], py[i + 1][j + 1] );
				vrglTexCoord2f( s0, t1 ); vrglVertex2f( px[i][j + 1], py[i][j + 1] );
			vrglEnd();
		}
	}

	vrglDisable( GL_TEXTURE_2D );

	// laser: da mão até o ponto de impacto (ou até um comprimento fixo quando
	// erra, para o raio continuar visível e dizendo para onde se aponta)
	if( vr_menu_laser && in_hand_ok[1] )
	{
		const vr_eye_cam_t &c = vr_eye_cam[eye];
		float O[3], fwd[3], rgt[3], up[3], tip[3], p0[2], p1[2];
		bool ok0, ok1;

		pose_basis_g( in_hand_pose[1], fwd, rgt, up );
		(void)rgt; (void)up;
		vr_room_to_world( in_hand_g[1], O );

		if( vr_menu_hit )
		{
			tip[0] = vr_menu_hit_world[0];
			tip[1] = vr_menu_hit_world[1];
			tip[2] = vr_menu_hit_world[2];
		}
		else
		{
			// 4 m à frente: mais que a distância típica do painel, então o raio
			// "passa" pelo painel quando o usuário mira nele
			vr_vmad( O, 4.0f * UNITS_PER_METER, fwd, tip );
		}

		// sobe um pouco a origem para o laser não nascer de dentro do modelo da
		// arma (e não atravessar a tela quando a mão está perto dela)
		O[2] += 0.5f;

		ok0 = vr_eye_project( eye, O, &p0[0], &p0[1] );
		ok1 = vr_eye_project( eye, tip, &p1[0], &p1[1] );

		(void)c;

		if( ok0 && ok1 )
		{
			float dx = p1[0] - p0[0], dy = p1[1] - p0[1];
			float len = sqrtf( dx * dx + dy * dy );
			float nx, ny;

			if( len > 0.5f )
			{
				nx = -dy / len;
				ny = dx / len;

				vrglEnable( GL_BLEND );
				vrglColor4f( vr_menu_hit ? 1.0f : 0.55f, vr_menu_hit ? 0.65f : 0.55f, 0.2f, vr_menu_hit ? 0.85f : 0.35f );
				vrglBegin( GL_QUADS );
					vrglVertex2f( p0[0] + nx * 1.5f, p0[1] + ny * 1.5f );
					vrglVertex2f( p1[0] + nx * 1.5f, p1[1] + ny * 1.5f );
					vrglVertex2f( p1[0] - nx * 1.5f, p1[1] - ny * 1.5f );
					vrglVertex2f( p0[0] - nx * 1.5f, p0[1] - ny * 1.5f );
				vrglEnd();
			}
		}
	}

	// o ponto de mira (o cursor do laser). O jogo NÃO desenha cursor para estes
	// menus — o cursor do mainui só existe na pilha do menu principal — então é
	// este ponto que diz onde o clique vai cair.
	if( vr_menu_hit )
	{
		float pc[2];

		if( vr_eye_project( eye, vr_menu_hit_world, &pc[0], &pc[1] ))
		{
			const float r = 5.0f;

			vrglColor4f( 1.0f, 0.72f, 0.25f, 0.95f );
			vrglBegin( GL_QUADS );
				vrglVertex2f( pc[0], pc[1] - r );
				vrglVertex2f( pc[0] + r, pc[1] );
				vrglVertex2f( pc[0], pc[1] + r );
				vrglVertex2f( pc[0] - r, pc[1] );
			vrglEnd();
		}
	}

	vrglMatrixMode( GL_PROJECTION ); vrglPopMatrix();
	vrglMatrixMode( GL_MODELVIEW ); vrglPopMatrix();
	vrglPopAttrib();

	pglBindFramebuffer( GL_DRAW_FRAMEBUFFER, ( GLuint )prev_draw );
	pglBindFramebuffer( GL_READ_FRAMEBUFFER, ( GLuint )prev_read );

	if( vr_menu_debug )
		vr_gl_check( "desenho do painel do menu" );
}

// ---- calibração ao vivo do painel: game/mods/vr_menu.cfg -------------------
// Mesmo padrão do vr_weapon.cfg (relido por mtime, no máximo 1x/s): distância,
// altura e enquadramento do painel só se acertam com o headset na cabeça, e
// salvar o arquivo já aplica — inclusive com um menu ABERTO (aí a âncora é
// recapturada na hora, então dá para ver o painel mudar de lugar ao vivo).
static double vr_menu_cfg_mtime = -1.0;
static double vr_menu_cfg_next_t;

// escreve o template SÓ se o arquivo não existir (nunca sobrescreve calibração)
static void vr_menu_cfg_seed( void )
{
	FILE *f = fopen( vr_config_path("vr_menu.cfg"), "r" );

	if( f )
	{
		fclose( f );
		return;
	}

	f = fopen( vr_config_path("vr_menu.cfg"), "w" );

	if( !f )
		return;

	fprintf( f, "# painel dos menus no VR (esc / m / b) — escrito pelo mod_vr.so se faltar\n" );
	fprintf( f, "# relido a cada ~1 s: salvar o arquivo JA aplica, sem reiniciar o jogo\n" );
	fprintf( f, "#\n" );
	fprintf( f, "# dist_m      distancia do painel a frente, em metros\n" );
	fprintf( f, "# height_m    altura fisica do painel, em metros: o TEXTO cresce junto\n" );
	fprintf( f, "#             (a largura segue o aspecto do canvas, ou seja, da janela)\n" );
	fprintf( f, "# yaw_off_deg gira o painel na horizontal (graus; + = para a direita)\n" );
	fprintf( f, "# y_off_m     sobe (+) ou desce (-) o painel, em metros\n" );
	fprintf( f, "dist_m %.2f\n", ( double )vr_menu_dist_m );
	fprintf( f, "height_m %.2f\n", ( double )vr_menu_height_m );
	fprintf( f, "yaw_off_deg %.1f\n", ( double )vr_menu_yaw_off );
	fprintf( f, "y_off_m %.2f\n", ( double )vr_menu_y_off_m );
	fclose( f );

	vr_note( VR_SEV_INFO, "menu: template de mods/vr_menu.cfg criado\n" );
}

static void vr_menu_cfg_load( void )
{
	struct stat st;
	char line[256];
	bool any = false;
	FILE *f = fopen( vr_config_path("vr_menu.cfg"), "r" );

	if( stat( vr_config_path("vr_menu.cfg"), &st ) == 0 )
		vr_menu_cfg_mtime = ( double )st.st_mtime;

	if( !f )
		return;

	while( fgets( line, sizeof( line ), f ))
	{
		char *p = line;
		float v;

		while( *p == ' ' || *p == '\t' )
			p++;

		if( *p == '#' || *p == '\n' || *p == '\0' )
			continue;

		if( sscanf( p, "dist_m %f", &v ) == 1 )
		{
			vr_menu_dist_m = fmaxf( VR_MENU_DIST_MIN_M, fminf( VR_MENU_DIST_MAX_M, v ));
			any = true;
		}
		else if( sscanf( p, "height_m %f", &v ) == 1 )
		{
			vr_menu_height_m = fmaxf( VR_MENU_HEIGHT_MIN_M, fminf( VR_MENU_HEIGHT_MAX_M, v ));
			any = true;
		}
		else if( sscanf( p, "yaw_off_deg %f", &v ) == 1 )
		{
			vr_menu_yaw_off = fmaxf( -60.0f, fminf( 60.0f, v ));
			any = true;
		}
		else if( sscanf( p, "y_off_m %f", &v ) == 1 )
		{
			vr_menu_y_off_m = fmaxf( -1.5f, fminf( 1.5f, v ));
			any = true;
		}
	}

	fclose( f );

	if( any )
	{
		vr_note( VR_SEV_INFO, "menu: mods/vr_menu.cfg = dist %.2f m, altura %.2f m, yaw %.1f, y %.2f\n",
			( double )vr_menu_dist_m, ( double )vr_menu_height_m,
			( double )vr_menu_yaw_off, ( double )vr_menu_y_off_m );

		// se já existe um painel ancorado, refaz a âncora com os valores novos —
		// é o que permite calibrar com o menu aberto
		if( vr_menu_anchor_ok )
			vr_menu_need_anchor = true;
	}
}

static void vr_menu_cfg_tick( void )
{
	struct stat st;
	double now;

	if( !eng )
		return;

	now = eng->DoubleTime();

	if( now < vr_menu_cfg_next_t )
		return;

	vr_menu_cfg_next_t = now + 1.0;

	if( stat( vr_config_path("vr_menu.cfg"), &st ) != 0 )
		return;

	if(( double )st.st_mtime != vr_menu_cfg_mtime )
		vr_menu_cfg_load();
}

// Trampolim da ABI v8 (Mod_Hud): roda 1x por frame, ANTES do input frame e do
// desenho 2D — é essa posição na ordem que faz o cursor e o clique valerem para
// o frame que está começando.
static void VR_Hud( mod_hud_t *h )
{
	const bool open = h->menu_open != 0;
	const bool was_open = vr_menu_open;

	vr_menu_prev_open = was_open;
	vr_menu_open = open;
	vr_menu_ui_visible = h->ui_visible != 0;
	vr_menu_canvas_tex = h->canvas_tex;
	vr_menu_canvas_w = h->canvas_w;
	vr_menu_canvas_h = h->canvas_h;

	// diagnóstico dos FATOS do engine (VR_MENU_DEBUG): vale mesmo com o VR
	// inativo, porque é a única janela para o que o engine está dizendo (o
	// console do engine só aparece em modo dedicado ou com -console)
	if( vr_menu_debug )
	{
		static int last_open = -1;
		static unsigned last_tex;

		if( open != last_open || h->canvas_tex != last_tex )
		{
			last_open = open ? 1 : 0;
			last_tex = h->canvas_tex;
			vr_note( VR_SEV_INFO, "menu (engine): aberto=%d ui=%d espaco2D=%dx%d canvas=%u | vr_ativo=%d\n",
				open ? 1 : 0, h->ui_visible, h->canvas_w, h->canvas_h, h->canvas_tex,
				vr_active ? 1 : 0 );
		}
	}

	// sem VR (ou com o recurso desligado) o Mod não pede NADA: nem canvas, nem
	// cursor, nem clique. É isto que mantém o jogo flat/fora de menu idêntico.
	if( !vr_active || !vr_menu )
	{
		h->want_canvas = 0;
		h->has_pointer = 0;
		h->buttons = 0;
		vr_menu_want_canvas = false;
		vr_menu_anchor_ok = false;
		vr_menu_need_anchor = false;
		vr_menu_ptr_ok = false;
		vr_menu_click = false;
		vr_menu_hit = false;
		return;
	}

	if( open && !was_open )
	{
		// abriu agora: a âncora é capturada no Mod_Camera DESTE frame (é lá que
		// existe a posição do olho no mundo) e o cursor nasce no centro do menu
		vr_menu_need_anchor = true;
		vr_menu_arm = false;
		vr_menu_ptr_ok = ( h->canvas_w > 0 && h->canvas_h > 0 );
		vr_menu_ptr_x = h->canvas_w * 0.5f;
		vr_menu_ptr_y = h->canvas_h * 0.5f;
		vr_note( VR_SEV_INFO, "menu: ABRIU (espaco 2D %dx%d, canvas %s)\n", h->canvas_w, h->canvas_h,
			h->canvas_tex ? "ja existente" : "novo neste frame" );
	}
	else if( !open && was_open )
	{
		vr_menu_anchor_ok = false;
		vr_menu_need_anchor = false;
		vr_menu_ptr_ok = false;
		vr_menu_hit = false;
		vr_menu_click = false;
		vr_note( VR_SEV_INFO, "menu: FECHOU (cliques enviados: %u)\n", vr_menu_clicks );
	}

	vr_menu_want_canvas = open;
	h->want_canvas = open ? 1 : 0;
	vr_menu_click = false;

	if( !open )
	{
		h->has_pointer = 0;
		h->buttons = 0;
		vr_menu_hit = false;
		return;
	}

	// gatilho: clique só com aperto NOVO (armadilha 3)
	const bool trig = in_fire > vr_trigger_th;

	if( !trig )
		vr_menu_arm = true;

	vr_menu_ray();

	vr_menu_click = trig && vr_menu_arm && vr_menu_hit;

	if( vr_menu_click )
	{
		vr_menu_clicks++;
		vr_note( VR_SEV_INFO, "menu: CLIQUE em (%.0f, %.0f) do canvas de %dx%d\n",
			( double )vr_menu_ptr_x, ( double )vr_menu_ptr_y, vr_menu_canvas_w, vr_menu_canvas_h );
	}

	h->has_pointer = vr_menu_ptr_ok ? 1 : 0;
	h->x = vr_menu_ptr_x;
	h->y = vr_menu_ptr_y;
	h->buttons = vr_menu_click ? 1 : 0;

	if( vr_menu_debug )
	{
		static double last_log;
		const double now = vr_now();

		if( now - last_log >= 1.0 )
		{
			last_log = now;
			vr_note( VR_SEV_INFO, "menu: ponteiro %s (%.0f, %.0f) mao %s | ancora %s | canvas tx %u\n",
				vr_menu_hit ? "NO PAINEL" : "fora do painel",
				( double )vr_menu_ptr_x, ( double )vr_menu_ptr_y,
				in_hand_ok[1] ? "ok" : "sem pose", vr_menu_anchor_ok ? "ok" : "pendente",
				vr_menu_canvas_tex );
		}
	}
}

static void VR_Camera( mod_view_t *view )
{
	// Modo de diagnóstico (VR_HANDS_TEST) SEM VR ativo: mesmo sem sessão, a
	// câmera que o engine usou é o que permite desenhar o modelo na janela e
	// conferir a projeção sem headset. Só registra e sai — não mexe no render.
	if( vr_hands_test && !vr_active && view->eye == 0 )
	{
		vr_eye_cam_t &c = vr_eye_cam[0];
		for( int i = 0; i < 3; i++ ) { c.origin[i] = view->vieworigin[i]; c.angles[i] = view->viewangles[i]; }
		if( view->has_fov_asym ) for( int i = 0; i < 4; i++ ) c.fov[i] = view->fov_asym[i];
		else
		{
			// Frustum simétrico a partir do fov_x/fov_y do engine (graus, total).
			// ATENÇÃO à unidade: cam->fov[] é em RADIANOS (é o que a ABI entrega
			// em fov_asym, e o proj_matrix aplica tan() em cima) — guardar a
			// tangente aqui aplicaria o tan duas vezes e achataria o modelo.
			const double hx = view->fov_x * M_PI / 360.0, hy = view->fov_y * M_PI / 360.0;
			c.fov[0] = ( float )-hx; c.fov[1] = ( float )hx;
			c.fov[2] = ( float ) hy; c.fov[3] = ( float )-hy;
		}
		c.w = view->viewport[2] ? view->viewport[2] : 1920;
		c.h = view->viewport[3] ? view->viewport[3] : 1080;
		c.ok = true;
		return;
	}

	if( !vr_active || !xr_views_ok || view->eye < 0 || view->eye > 1 )
		return;

	if( vr_perf_eyes_t0 <= 0.0 )
		vr_perf_eyes_t0 = vr_now();

	// base do olho do player ANTES de qualquer offset deste Mod: é o ponto de
	// referência do espaço do quarto (o roomscale abaixo é justamente o que
	// repõe o deslocamento horizontal em relação a ele) — a mão (laser do menu)
	// precisa da mesma base
	if( view->eye == 0 )
	{
		for( int i = 0; i < 3; i++ )
			vr_menu_eye_base[i] = view->vieworigin[i];

		vr_menu_eye_base_ok = true;
	}

	bind_eye( view->eye );

	const XrView &v = xr_views[view->eye];

	// rotação da cabeça → ângulos do engine (inclusive roll). A posição
	// absoluta do HMD é ignorada na translação: a câmera fica na altura dos
	// olhos do player e só o afastamento ENTRE os olhos entra (abaixo).
	pose_to_viewangles( v.pose, view->viewangles );

	// snap turn: o giro artificial entra no render TAMBÉM, não só no cmd. Sem
	// isto, depois de um snap a câmera olharia para um lado e o corpo (que atira
	// pela direção que foi ao servidor) para o outro — os dois somam o MESMO
	// offset, senão o tiro sai torto em relação ao que se vê.
	view->viewangles[1] = wrap180( view->viewangles[1] + vr_yaw_offset );

	// roomscale: inclinar-se e passos pequenos movem a câmera. Só o plano
	// horizontal entra aqui — o vertical é responsabilidade do agachar físico,
	// que vira IN_DUCK e deixa o jogo baixar a vista (uma vez só).
	{
		float off[3];

		vr_roomscale_offset( off );
		view->vieworigin[0] += off[0];
		view->vieworigin[1] += off[1];
		view->vieworigin[2] += off[2];
	}

	float basis_f[3], basis_r[3], basis_u[3];
	pose_basis_g( v.pose, basis_f, basis_r, basis_u );

	// ---- afastamento interpupilar ----
	// O runtime informa a posição de cada olho, já calibrada com o IPD real do
	// headset e com o ajuste do usuário (medido aqui: 68,3 mm, contra os 63 mm
	// chutados antes — 8% de erro só nisso). O que importa é o DELTA entre os
	// dois olhos: ele é a única parte que separa as câmeras (a posição absoluta
	// só repetiria o quarto onde o player já está). O delta é medido 1x por
	// frame em vr_measure_eye_delta(), já no referencial da cabeça.
	//
	// O ALVO vem do vr_ipd_eff(): base (runtime ou VR_IPD, como antes) já com o
	// ajuste fino do teclado. O vetor medido é ESCALADO até esse comprimento —
	// escala e não substitui, para preservar a direção dos olhos (o cant da
	// ótica, um eventual deslocamento vertical): o ajuste muda a DISTÂNCIA entre
	// as câmeras, não o eixo em que elas estão.
	float dl[3];
	float half = ( view->eye == 0 ? -0.5f : 0.5f );
	const float target = vr_ipd_eff(); // metros
	const float measured = sqrtf( dot3( vr_eye_dl, vr_eye_dl ));

	if( target > 0.0001f && measured > 1e-5f )
	{
		const float k = target / measured;

		for( int i = 0; i < 3; i++ )
			dl[i] = vr_eye_dl[i] * k;
	}
	else if( target > 0.0001f )
	{
		// ainda sem medida do runtime (primeiro frame): afasta pela direita da
		// cabeça, como o override antigo fazia
		dl[0] = target;
		dl[1] = dl[2] = 0.0f;
	}
	else
	{
		dl[0] = dl[1] = dl[2] = 0.0f; // sem base e sem medida: estéreo nulo
	}

	for( int i = 0; i < 3; i++ )
	{
		// c2 = "trás" no espaço XR, e basis_f é a frente: daí o sinal negativo
		view->vieworigin[i] += ( basis_r[i] * dl[0] + basis_u[i] * dl[1] - basis_f[i] * dl[2] )
			* half * UNITS_PER_METER;
	}

	// ---- frustum do olho ----
	// A ótica do headset é canted: cada olho enxerga um retângulo FORA DE
	// CENTRO e espelhado (medido aqui: esquerdo L54 R40 U44 D55, direito
	// L40 R54 U44 D55). fov_x/fov_y só expressam frustum centrado — desenhar
	// centrado e submeter a fov assimétrica faz o compositor esticar a imagem
	// de cada olho num fator diferente, que é exatamente "cada olho sozinho
	// parece ok, os dois juntos não fundem". Os quatro ângulos vão ao engine
	// em radianos, no sinal do OpenXR (left/down negativos).
	view->fov_asym[0] = v.fov.angleLeft;
	view->fov_asym[1] = v.fov.angleRight;
	view->fov_asym[2] = v.fov.angleUp;
	view->fov_asym[3] = v.fov.angleDown;
	view->has_fov_asym = 1;

	// fov_x/fov_y são ABERTURAS TOTAIS em graus — o engine divide por dois
	// internamente (tan(fov/2) na projeção, SinCos(fov*0,5) no frustum). Um
	// semi-ângulo publicado aqui valeria como se fosse o dobro: foi assim que o
	// culling ficou com metade da abertura e o mundo sumia nas beiradas. O
	// culling por olho NÃO depende mais disto (com RF_FOV_ASYM ele usa os quatro
	// ângulos de fov_asym), mas o campo segue com a unidade certa para qualquer
	// outro consumidor.
	view->fov_x = ( float )rad2deg( v.fov.angleRight - v.fov.angleLeft );
	view->fov_y = ( float )rad2deg( v.fov.angleUp - v.fov.angleDown );

	view->viewport[0] = 0;
	view->viewport[1] = 0;
	view->viewport[2] = xr_eye[view->eye].w;
	view->viewport[3] = xr_eye[view->eye].h;

	// câmera deste olho como o engine vai usá-la: o painel dos menus é projetado
	// por esta base (e não pela pose do runtime), então a conta bate com o pixel
	// que o olho vê — inclusive depois do snap turn e do roomscale
	{
		vr_eye_cam_t &c = vr_eye_cam[view->eye];

		for( int i = 0; i < 3; i++ )
		{
			c.origin[i] = view->vieworigin[i];
			c.angles[i] = view->viewangles[i];
		}

		for( int i = 0; i < 4; i++ )
			c.fov[i] = view->fov_asym[i];

		c.w = xr_eye[view->eye].w;
		c.h = xr_eye[view->eye].h;
		c.ok = true;
	}

	// âncora do painel dos menus: capturada AQUI (posição do olho no mundo já
	// montada) no primeiro frame depois de o menu abrir, e congelada — é o "na
	// frente e na altura dos olhos, no instante em que o menu abriu"
	if( view->eye == 0 && vr_menu_need_anchor )
		vr_menu_capture_anchor( view );

	if( eng )
		vr_last_eye_render_t = eng->DoubleTime();

	if( vr_trace_all || vr_stereo_frames <= VR_TRACE_FRAMES )
		vr_trace( "trace: camera olho %d: origem (%.1f %.1f %.1f) angulos (%.1f %.1f %.1f) fov L%.1f R%.1f U%.1f D%.1f vp %dx%d\n",
			view->eye, ( double )view->vieworigin[0], ( double )view->vieworigin[1], ( double )view->vieworigin[2],
			( double )view->viewangles[0], ( double )view->viewangles[1], ( double )view->viewangles[2],
			( double )- rad2deg( view->fov_asym[0] ), ( double )rad2deg( view->fov_asym[1] ),
			( double )rad2deg( view->fov_asym[2] ), ( double )- rad2deg( view->fov_asym[3] ),
			view->viewport[2], view->viewport[3] );
}

// Callback da ABI v6 (MOD_CAP_DISPLAY): leva a arma para a mão que mira.
//
// A ORIENTAÇÃO não é tocada de propósito. O client DLL orienta o viewmodel a
// partir da view (V_CalcGunAngle em view.cpp), e a view do usercmd agora é a MÃO
// (VR_AIM_SOURCE) — então a arma já aponta para onde o tiro vai, com a convenção
// de cada modelo (inclusive o pitch invertido). O que falta é a ORIGEM, que o
// DLL põe no meio dos olhos: é isso que passa a vir da mão.
//
// A base é o olho CENTRAL do player + o MESMO offset de roomscale que a câmera
// recebeu: assim arma e câmera concordam, e os dois olhos veem a arma no mesmo
// lugar (é por isso que a base é player_origin e não o vieworigin do rvp, que já
// traz o afastamento entre os olhos).
//
// O engine chama de novo no segundo olho (o pfnCalcRefdef reescreve o
// viewmodel), então aqui só se LÊ estado — a função é idempotente.
static void VR_ViewModel( mod_viewmodel_t *vm )
{
	// diagnóstico primeiro: roda em TODOS os caminhos (inclusive flat) e é o que
	// separa "o jogo não me deu arma" de "eu errei a posição da arma"
	vr_wpn_diag( vm );

	// Classe/nome do modelo atual — PRECISA rodar antes de qualquer `return`:
	// é o que o gate do viewmodel próprio (abaixo) E o gate do desenho (no
	// VR_AfterEyes) consultam. Quando isto ficava depois do `return` do hide, o
	// nome nunca era atualizado para a pistola: o jogo escondia a arma dele e o
	// nosso desenho não acontecia — "sem arma e sem braços". Só o primeiro olho
	// escreve (senão o log sairia em dobro por frame).
	const char *mname = vm->model_name[0] ? vm->model_name
		: ( vr_hands_fake_model ? vr_hands_fake_model : "" );
	if( vm->eye == 0 && mname[0] )
	{
		int cls = vr_wpn_class_id( mname );

		if( cls != vr_wpn_class || strcmp( vr_wpn_model, mname ))
		{
			vr_wpn_class = cls;
			snprintf( vr_wpn_model, sizeof( vr_wpn_model ), "%s", mname );
			vr_note( VR_SEV_INFO, "arma: modelo %s (classe %s) | mira %s%s\n",
				vr_wpn_model, vr_wpn_class_names[cls], vr_aim_source_name(),
				vm->model_name[0] ? "" : " [nome injetado por VR_HANDS_FAKE_MODEL]" );
		}
	}

	// Decisão ÚNICA sobre "o desenho é nosso?": o mesmo booleano esconde o
	// viewmodel do jogo aqui e autoriza o desenho no VR_AfterEyes. Duas consultas
	// separadas podiam discordar (foi o que causou o sumiço).
	{
		const bool before = vr_hands_draw_ours;
		vr_hands_draw_ours = vr_hands_ready() && vr_hands_covers_model( mname );
		if( vr_hands_draw_ours != before )
			vr_note( VR_SEV_INFO, "maos: %s o viewmodel da classe coberta (%s)\n",
				vr_hands_draw_ours ? "ASSUMINDO" : "devolvendo ao jogo", vr_wpn_model );
	}

	// Modo de teste SEM headset (VR_WEAPON_TEST="x y z"): põe a arma num offset
	// fixo, em unidades GoldSrc, a partir do olho do player. Serve para conferir
	// o caminho do engine (a origem é aplicada? o depth/oclusão funciona?) sem
	// depender do runtime — com VR ativo não vale.
	if( vr_wpn_test_on )
	{
		vm->origin[0] = vm->player_origin[0] + vr_wpn_test[0];
		vm->origin[1] = vm->player_origin[1] + vr_wpn_test[1];
		vm->origin[2] = vm->player_origin[2] + vr_wpn_test[2];
		vm->has_origin = 1;
		vm->world_depth = vr_wpn_world_depth ? 1 : 0;
		return;
	}

	if( !vr_active || !xr_views_ok || !in_valid )
		return;

	// Viewmodel próprio: cobrindo esta arma, o JOGO não desenha a dele — quem
	// desenha é o vr_hands (braços + arma, seguindo as mãos). Só com VR ativo:
	// no modo flat o jogador continua vendo o viewmodel de sempre.
	if( vr_hands_draw_ours )
	{
		vm->hide = 1;
		vm->world_depth = 1;
		return;
	}

	int h = ( vr_aim_source == VR_AIM_LEFT ) ? 0 : 1;

	// com a mira na cabeça (Fase 3), a arma ainda vai para a mão: a principal
	if( vr_aim_source == VR_AIM_HEAD )
		h = vr_left_handed ? 0 : 1;

	if( !in_hand_ok[h] )
		return;

	// a origem que o client DLL escreveu (o olho) — guardada só para o
	// diagnóstico: é o que o jogo faria sem esta sobrescrita
	float vm_in_origin[3] = { vm->origin[0], vm->origin[1], vm->origin[2] };

	float off[3];

	vr_roomscale_offset( off );

	float o[3] = {
		vm->player_origin[0] + off[0],
		vm->player_origin[1] + off[1],
		vm->player_origin[2] + off[2],
	};

	// delta da mão em relação à cabeça, no espaço do playspace (a mão já está em
	// unidades GoldSrc; a cabeça está em metros e é convertida aqui)
	float d[3] = {
		in_hand_g[h][0] - vr_head_m[0] * UNITS_PER_METER,
		in_hand_g[h][1] - vr_head_m[1] * UNITS_PER_METER,
		in_hand_g[h][2] - vr_head_m[2] * UNITS_PER_METER,
	};

	vr_rotate_yaw_offset( d );

	// sanidade: a mão não pode estar a metros de distância da cabeça. Se a pose
	// vier implausível (runtime devolvendo zeros/identidade, referencial trocado),
	// NÃO move a arma — melhor a arma no lugar de sempre do que uma arma
	// invisível em outro lugar. O log diz que disparou (é o diagnóstico nº 1).
	float reach = sqrtf( d[0] * d[0] + d[1] * d[1] + d[2] * d[2] );

	if( reach > VR_WPN_MAX_REACH )
	{
		if( eng && eng->DoubleTime() >= vr_wpn_dbg_next )
		{
			vr_wpn_dbg_next = eng->DoubleTime() + 1.0;
			vr_note( VR_SEV_WARN,
				"arma: pose da mao implausivel (%.0f unidades ≈ %.2f m) — mantendo a arma onde o jogo a poe\n",
				( double )reach, ( double )( reach / UNITS_PER_METER ));
		}
		return;
	}

	// encaixe no referencial da ARMA (frente/direita/cima da mão): girar a mão
	// não pode mudar o encaixe
	float fwd[3], rgt[3], up[3];

	pose_basis_g( in_hand_pose[h], fwd, rgt, up );

	const float *t = vr_wpn_active_offset();

	for( int i = 0; i < 3; i++ )
		o[i] += d[i] + fwd[i] * t[0] + rgt[i] * t[1] + up[i] * t[2];

	vm->origin[0] = o[0];
	vm->origin[1] = o[1];
	vm->origin[2] = o[2];
	vm->has_origin = 1;
	vm->world_depth = vr_wpn_world_depth ? 1 : 0;

	// diagnóstico (VR_WEAPON_DEBUG=1, 1x/s): os números crus é que dizem onde a
	// conta sai do previsto quando a arma não aparece onde devia
	if( vr_wpn_debug && vm->eye == 0 && eng && eng->DoubleTime() >= vr_wpn_dbg_next )
	{
		vr_wpn_dbg_next = eng->DoubleTime() + 1.0;
		vr_note( VR_SEV_INFO,
			"arma dbg: mao %d/%d | dll_origem %+.0f %+.0f %+.0f | olho %+.0f %+.0f %+.0f |"
			" d_mundo %+.0f %+.0f %+.0f | saida %+.0f %+.0f %+.0f | flags%s%s\n",
			in_hand_ok[0] ? 1 : 0, in_hand_ok[1] ? 1 : 0,
			( double )vm_in_origin[0], ( double )vm_in_origin[1], ( double )vm_in_origin[2],
			( double )vm->player_origin[0], ( double )vm->player_origin[1], ( double )vm->player_origin[2],
			( double )d[0], ( double )d[1], ( double )d[2],
			( double )o[0], ( double )o[1], ( double )o[2],
			vm->has_origin ? " origem" : "", vm->world_depth ? " world_depth" : "" );
	}
}

// Poses das mãos no mundo, para o subsistema — a MESMA conversão que o laser do
// menu usa (espaço do quarto -> mundo, com a base GoldSrc da pose).
static void vr_hands_fill( vr_hands_hand_t out[2] )
{
	for( int h = 0; h < 2; h++ )
	{
		out[h].ok = in_hand_ok[h];
		if( !out[h].ok ) continue;

		vr_room_to_world( in_hand_g[h], out[h].pos );

		float fwd[3], rgt[3], up[3];
		pose_basis_g( in_hand_pose[h], fwd, rgt, up );
		vr_rotate_yaw_offset( fwd );
		vr_rotate_yaw_offset( rgt );
		vr_rotate_yaw_offset( up );
		memcpy( out[h].fwd, fwd, sizeof( fwd ));
		memcpy( out[h].rgt, rgt, sizeof( rgt ));
		memcpy( out[h].up,  up,  sizeof( up ));
	}
}

// Corpo do jogador para o subsistema: posição (olho central + roomscale), eixos
// do tronco e o plano do chão. Sem sessão (modo de teste) o corpo sai da câmera
// que o VR_Camera capturou — é assim que a folha de contato mostra coldre e chão
// sem headset.
static void vr_hands_body_fill( vr_hands_body_t *b )
{
	memset( b, 0, sizeof( *b ));

	if( vr_hands_test && !vr_active && vr_eye_cam[0].ok )
	{
		memcpy( b->pos, vr_eye_cam[0].origin, sizeof( b->pos ));
		float ang[3] = { 0.0f, vr_eye_cam[0].angles[1], 0.0f };
		vr_angles_basis( ang, b->fwd, b->rgt, b->up );
	}
	else
	{
		float off[3];

		vr_roomscale_offset( off );
		for( int i = 0; i < 3; i++ )
			b->pos[i] = ( vr_menu_eye_base_ok ? vr_menu_eye_base[i] : 0.0f ) + off[i];

		// yaw do tronco: segue a cabeca, com atraso opcional (VR_BODY_YAW_LAG em
		// segundos; 0 = instantaneo, igual ao comportamento de antes)
		{
			const double now = eng ? eng->DoubleTime() : 0.0;
			const float alvo = wrap180( vr_head_raw[1] + vr_yaw_offset );

			if( vr_body_yaw_lag > 0.01f && vr_body_yaw_t > 0.0 && now > vr_body_yaw_t )
			{
				float k = ( float )(( now - vr_body_yaw_t ) / vr_body_yaw_lag );

				if( k > 1.0f ) k = 1.0f;
				vr_body_yaw = wrap180( vr_body_yaw + wrap180( alvo - vr_body_yaw ) * k );
			}
			else vr_body_yaw = alvo;

			vr_body_yaw_t = now;
		}
		float ang[3] = { 0.0f, vr_body_yaw, 0.0f };
		vr_angles_basis( ang, b->fwd, b->rgt, b->up );
	}

	// o plano do chão do JOGO: a ABI não dá raycast, então o chão da arma largada
	// é o chão do próprio jogador (altura dos olhos em pé/agachado, como o engine)
	b->eye_height = vr_duck_phys ? 12.0f : 28.0f;
	b->floor_z = b->pos[2] - b->eye_height;
}

// 1x por frame, logo depois do vr_actions_sync (as poses já são deste frame).
static void vr_hands_frame( void )
{
	static double done_t = -1.0;
	const double now = eng ? eng->DoubleTime() : 0.0;

	if( !vr_hands_ready()) return;
	// o modo de teste chama isto do VR_AfterEyes (não há sessão nem FramePre de
	// VR): este guarda evita resolver o MESMO frame duas vezes
	if( now == done_t ) return;
	// sem sessão só há trabalho a fazer no modo de diagnóstico
	if( ( !vr_active || !xr_views_ok || !in_valid ) && !vr_hands_test )
		return;
	done_t = now;

	vr_hands_body_t b;
	vr_hands_in_t in;

	vr_hands_body_fill( &b );

	memset( &in, 0, sizeof( in ));
	// grip: por enquanto o squeeze MISTURADO vale para as duas mãos — o grip por
	// mão (ações separadas por subaction path) é a Fase 3
	in.grip[0] = in_grip[0];
	in.grip[1] = in_grip[1];
	in.fire = ( in_fire > vr_trigger_th ) && !( vr_menu_open && vr_menu );

	in.dt = ( vr_hands_t_prev > 0.0 && now > vr_hands_t_prev && ( now - vr_hands_t_prev ) < 0.5 )
		? ( float )( now - vr_hands_t_prev ) : 0.016f;
	vr_hands_t_prev = now;

	// o recuo visual só vale com a arma na mão (mesma regra do gatilho no VR_Input)
	{
		const bool can_fire = !vr_hands_draw_ours || !vr_hands_enforce_armed() || vr_hands_state.armed;

		if( !can_fire ) in.fire = false;
	}

	vr_hands_hand_t hands[2];

	if( vr_active )
	{
		vr_hands_fill( hands );
		vr_hands_update( &b, &in, hands, &vr_hands_state );
	}
	else
		vr_hands_update( &b, &in, NULL, &vr_hands_state ); // poses sintéticas
}

static void VR_AfterEyes( void )
{
	if( vr_active && xr_frame_began )
	{
		// O binding aqui responde a pergunta central do "headset preto": o
		// engine terminou o desenho do olho no FBO do olho (esperado) ou ele
		// voltou para a janela no meio do caminho?
		if( vr_trace_all || vr_stereo_frames <= VR_TRACE_FRAMES )
		{
			GLint bound = -1;
			pglGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &bound );
			vr_trace( "trace: apos olhos (frame %d) eye0.cur=%d eye1.cur=%d fbo_olho0=%u bound=%d\n",
				vr_stereo_frames, xr_eye[0].cur, xr_eye[1].cur, xr_eye[0].fbos[0], ( int )bound );
		}

		// MEDIÇÃO do conteúdo: "o HMD está preto" se responde lendo o pixel da
		// imagem da swapchain. Se vier 0,0,0, o engine não desenhou nada ali (o
		// problema é o render); se vier com cor e o HMD seguir preto, o problema
		// é o submit/camada.
		if( vrglReadPixels && xr_eye[0].cur >= 0 && ( vr_trace_all || vr_stereo_frames <= VR_TRACE_FRAMES ))
		{
			const int ew = xr_eye[0].w, eh = xr_eye[0].h;
			const int pts[3][2] = { { ew / 4, eh / 4 }, { ew / 2, eh / 2 }, { ( ew * 3 ) / 4, ( eh * 3 ) / 4 } };
			unsigned char px[3][4] = {{0}};
			GLint prev = 0;

			pglGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &prev );
			pglBindFramebuffer( GL_READ_FRAMEBUFFER, xr_eye[0].fbos[xr_eye[0].cur] );

			for( int i = 0; i < 3; i++ )
				vrglReadPixels( pts[i][0], pts[i][1], 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px[i] );

			pglBindFramebuffer( GL_READ_FRAMEBUFFER, ( GLuint )prev );

			vr_trace( "trace: conteudo olho0 1/4=(%u,%u,%u) 1/2=(%u,%u,%u) 3/4=(%u,%u,%u)\n",
				px[0][0], px[0][1], px[0][2], px[1][0], px[1][1], px[1][2], px[2][0], px[2][1], px[2][2] );
		}

		if( vr_perf_eyes_t0 > 0.0 )
		{
			vr_perf_eyes_ms += ( vr_now() - vr_perf_eyes_t0 ) * 1000.0;
			vr_perf_eyes_t0 = 0.0;
		}

		vr_gl_check( "apos render dos olhos" );

		// viewmodel próprio — Fase 1: só o modo de diagnóstico (VR_HANDS_TEST).
		// Desenha o modelo parado na frente da câmera NOS DOIS olhos, com
		// profundidade real: o mundo já está no depth buffer, então ele oclui
		// os braços. Este é o teste que prova carga+skin+textura+projeção sem
		// headset (o dump do espelho mostra o resultado).
		// Viewmodel próprio na VR: braços+arma seguindo as mãos, nos DOIS olhos.
		// Entra quando cobrimos a arma em uso (o viewmodel do jogo já foi
		// escondido no VR_ViewModel).
		if( vr_hands_draw_ours )
		{
			// a pose já foi resolvida no vr_hands_frame (1x por frame): aqui só se
			// desenha, trocando o alvo por olho
			for( int e = 0; e < 2; e++ )
			{
				if( xr_eye[e].fbos[0] == 0 || xr_eye[e].cur < 0 || !vr_eye_cam[e].ok )
					continue;

				GLint prev = 0;
				pglGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &prev );
				pglBindFramebuffer( GL_FRAMEBUFFER, xr_eye[e].fbos[xr_eye[e].cur] );

				vr_hands_cam_t hc;
				vr_hands_cam( &hc, vr_eye_cam[e] );
				vr_hands_draw( &hc );

				pglBindFramebuffer( GL_FRAMEBUFFER, ( GLuint )prev );
			}
			vr_gl_check( "viewmodel proprio" );
			if( !vr_hands_drew )
			{
				vr_hands_drew = true;
				vr_note( VR_SEV_OK, "maos: desenhando o viewmodel proprio nos dois olhos (%s)\n", vr_wpn_model );
			}
		}
		else if( vr_hands_test && vr_hands_ready() )
		{
			for( int e = 0; e < 2; e++ )
			{
				if( xr_eye[e].fbos[0] == 0 || xr_eye[e].cur < 0 || !vr_eye_cam[e].ok )
					continue;

				GLint prev = 0;
				pglGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &prev );
				pglBindFramebuffer( GL_FRAMEBUFFER, xr_eye[e].fbos[xr_eye[e].cur] );

				vr_hands_cam_t hc;
				vr_hands_cam( &hc, vr_eye_cam[e] );
				vr_hands_draw( &hc );   // poses sintéticas (diagnóstico)

				pglBindFramebuffer( GL_FRAMEBUFFER, ( GLuint )prev );
			}
			vr_gl_check( "viewmodel proprio (teste)" );
		}

		// menus no VR: painel + laser por cima do mundo dos DOIS olhos e antes do
		// espelho (o monitor também mostra o painel — dá para testar sem tirar o
		// headset). Depois deste ponto o Mod desenhou, então o engine zera o
		// cache de textura do ref (ver R_InvalidateTextures).
		if( vr_menu_open && vr_menu )
		{
			for( int e = 0; e < 2; e++ )
				vr_menu_draw( e );
		}

		{
			double t0 = vr_now();
			blit_mirror(); // espelho do olho esquerdo no monitor + 2D por cima
			vr_perf_blit_ms += ( vr_now() - t0 ) * 1000.0;
		}
	}
	else
	{
		// garante que o 2D desenha na janela mesmo sem VR ativa
		GLint fbo = 0;
		pglGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &fbo );
		pglBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );
		pglBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
		( void )fbo;

		// diagnóstico sem headset, na janela (FBO 0), antes do HUD 2D — é o que o
		// dump do painel captura. Com a folha de contato ligada (VR_HANDS_SHEET) é
		// ELA que sai no dump: a grade de vistas do mesmo modelo/encaixe.
		if( vr_hands_test && vr_hands_ready() && vr_eye_cam[0].ok )
		{
			vr_hands_cam_t hc;
			vr_hands_cam( &hc, vr_eye_cam[0] );
			vr_hands_frame();        // sem sessão o frame é resolvido aqui
			vr_hands_sheet( &hc );   // só desenha se VR_HANDS_SHEET ligou a folha
			vr_hands_draw( &hc );    // poses sintéticas: prova a IK sem headset
		}
	}
}

static void VR_EndFrame( void )
{
	if( vr_ready )
		xr_end_frame();
}

static const char g_name[] = "vr";

static mod_api_t g_api =
{
	.api_version = MOD_API_VERSION,
	.mod_name = g_name,
		// HUD (v3) entra por causa do painel de debug: é o hook 2D pós-HUD/pré-swap,
		// o único ponto em que dá para desenhar por cima da UI sem ir para o headset
		// INPUT (v5): locomoção/botões dos controles; ver a seção "input VR"
		// KEYS (v7): o ajuste fino do IPD pelo teclado, consumido antes do jogo
		.capabilities = MOD_CAP_DISPLAY | MOD_CAP_HUD | MOD_CAP_INPUT | MOD_CAP_KEYS,
	.Mod_Init = VR_Init,
	.Mod_Shutdown = VR_Shutdown,
	.Mod_FramePre = VR_FramePre,
	.Mod_FramePost = NULL,
	.Mod_Camera = VR_Camera,
	.Mod_BeginFrame = NULL, // bind do alvo do olho acontece em Mod_Camera
	.Mod_EndFrame = VR_EndFrame,
	.Mod_Input = VR_Input,
	.Mod_Hud = VR_Hud,
	.Mod_EyeCount = VR_EyeCount,
	.Mod_AfterEyes = VR_AfterEyes,
	.Mod_Overlay2D = VR_Overlay2D,
	.Mod_ViewModel = VR_ViewModel,
	.Mod_Key = vr_ipd_key,
	.size = sizeof(mod_api_t),
	.Mod_Enable = VR_Enable,
	.Mod_DebugPanel = VR_DebugPanel,
	.Mod_WorldOverlay = NULL,
};

extern "C" __attribute__((visibility("default"))) mod_api_t *ModGetAPI( void )
{
	return &g_api;
}
