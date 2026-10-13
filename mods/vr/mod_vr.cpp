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
static FILE *vrlog;

static void logf( const char *fmt, ... ) __attribute__(( format( printf, 1, 2 )));
static void logf( const char *fmt, ... )
{
	char buf[1024];
	va_list ap;

	va_start( ap, fmt );
	vsnprintf( buf, sizeof( buf ), fmt, ap );
	va_end( ap );

	// arquivo próprio: o Con_Printf do engine não é capturado no -log
	// para o que acontece antes do primeiro frame (ex.: o load do Mod)
	if( !vrlog )
		vrlog = fopen( "mods/mod_vr.log", "a" );
	if( vrlog )
	{
		fprintf( vrlog, "%s", buf );
		fflush( vrlog );
	}
	if( eng )
		eng->Printf( "[mod_vr] %s", buf );
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

static const int VR_MSG_MAX = 64; // anel do painel; o log em arquivo não tem limite

struct vr_msg_t
{
	int    sev;
	double t;             // segundos desde Mod_Init (relógio do engine)
	char   text[168];
};

static vr_msg_t       vr_msgs[VR_MSG_MAX];      // anel (escrito sob lock)
static vr_msg_t       vr_msg_view[VR_MSG_MAX];  // cópia p/ o frame (só main thread)
static int            vr_msg_count;             // quantos já entraram (satura)
static unsigned long  vr_msg_total;             // total emitido (inclusive os que saíram)
static std::mutex     vr_msg_lock;
static double         vr_t0;
static int            vr_msg_view_count;        // linhas válidas em vr_msg_view
static unsigned long  vr_msg_view_total;

// status resumido, lido pelo painel
static char vr_runtime[160];                // "WiVRn 26.9.0"
static char vr_video_driver[32];            // driver de vídeo do SDL: x11/wayland/...
static char vr_gl_version[64];              // GL_VERSION do contexto do jogo
static int  vr_gl_major, vr_gl_minor;       // versão GL parseada (0 = desconhecida)
static std::atomic<int> vr_session_state { -1 };
static int  vr_eye_w, vr_eye_h, vr_eye_images, vr_eye_count;
static char vr_glx_info[128];

static const char *vr_sev_tag( int sev )
{
	switch( sev )
	{
	case VR_SEV_OK:    return "OK ";
	case VR_SEV_WARN:  return "AV ";
	case VR_SEV_ERROR: return "ER ";
	default:           return "   ";
	}
}

static int vr_msg_view_errors;                  // erros presentes na cópia do frame
static int vr_msg_view_warns;                   // avisos presentes na cópia do frame

// Cópia do anel para o frame (uma vez por frame, sem GL): o painel lê a cópia,
// nunca o anel, para não segurar o lock enquanto desenha.
static void vr_msgs_refresh( void )
{
	std::lock_guard<std::mutex> lock( vr_msg_lock );

	memcpy( vr_msg_view, vr_msgs, sizeof( vr_msg_t ) * (size_t)vr_msg_count );
	vr_msg_view_count = vr_msg_count;
	vr_msg_view_total = vr_msg_total;

	vr_msg_view_errors = 0;
	vr_msg_view_warns = 0;
	for( int i = 0; i < vr_msg_count; i++ )
	{
		if( vr_msg_view[i].sev == VR_SEV_ERROR )
			vr_msg_view_errors++;
		else if( vr_msg_view[i].sev == VR_SEV_WARN )
			vr_msg_view_warns++;
	}
}

static void vr_note( int sev, const char *fmt, ... )
{
	char buf[sizeof( vr_msgs[0].text )];
	va_list ap;

	va_start( ap, fmt );
	vsnprintf( buf, sizeof( buf ), fmt, ap );
	va_end( ap );

	logf( "%s", buf ); // arquivo + console: mesmo texto de antes do painel existir

	std::lock_guard<std::mutex> lock( vr_msg_lock );

	if( vr_msg_count >= VR_MSG_MAX ) // anel cheio: descarta a mais antiga
	{
		memmove( vr_msgs, vr_msgs + 1, sizeof( vr_msgs[0] ) * ( VR_MSG_MAX - 1 ));
		vr_msg_count = VR_MSG_MAX - 1;
	}

	vr_msg_t &m = vr_msgs[vr_msg_count++];
	m.sev = sev;
	m.t = eng ? eng->DoubleTime() - vr_t0 : 0.0;
	snprintf( m.text, sizeof( m.text ), "%s", buf );
	{
		size_t len = strlen( m.text ); // o painel desenha por linha: sem \n no fim
		while( len > 0 && ( m.text[len - 1] == '\n' || m.text[len - 1] == '\r' ))
			m.text[--len] = 0;
	}
	vr_msg_total++;
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
GL_FUN( glFramebufferTexture2D, PFNGLFRAMEBUFFERTEXTURE2DPROC )
GL_FUN( glGenRenderbuffers, PFNGLGENRENDERBUFFERSPROC )
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
	GL_FUN( glFramebufferTexture2D, PFNGLFRAMEBUFFERTEXTURE2DPROC )
	GL_FUN( glGenRenderbuffers, PFNGLGENRENDERBUFFERSPROC )
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

// ---------------- input VR (Fase 3) ----------------
// Locomoção com os controles do Quest 3. O desenho:
//
//   VR_FramePre  xrWaitFrame/xrBeginFrame/xrLocateViews  (pose pronta)
//                vr_actions_sync                          (xrSyncActions + leitura)
//   Mod_Input    VR_Input                                 (preenche a amostra)
//   CL_CreateCmd IN_ModAppendMove                         (a amostra vira usercmd)
//   VR_Camera    yaw+snap e offset de roomscale           (só o render)
//
// "Orientado pela cabeça" não custa conta nenhuma: este mod injeta o yaw do HMD
// no cmd e manda o stick cru em forwardmove/sidemove; o pmove monta o wishdir
// com AngleVectors(cmd.viewangles) e zera wishvel[2] no chão. Ou seja, olhar
// para cima não muda a velocidade e virar a cabeça muda a direção de andar.
//
// O giro é só snap (decisão de 12/10): o stick direito NÃO gira a câmera
// continuamente, ele acumula um offset de yaw aplicado igualmente no cmd e no
// render — senão o corpo passaria a atirar para um lado e a olhar para o outro.
static XrActionSet xr_actionset = XR_NULL_HANDLE;
static XrAction xr_act_move, xr_act_turn, xr_act_fire, xr_act_altfire, xr_act_grip;
static XrAction xr_act_jump, xr_act_reload, xr_act_use, xr_act_score, xr_act_crouch, xr_act_menu, xr_act_haptic;
static XrAction xr_act_aim[2];
static XrSpace  xr_aim_space[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
static XrPath   xr_path_hand[2] = { XR_NULL_PATH, XR_NULL_PATH };

// knobs (ver o cabeçalho do arquivo)
static float vr_deadzone = 0.20f;         // VR_DEADZONE
static float vr_trigger_th = 0.50f;       // VR_TRIGGER_THRESHOLD
static float vr_squeeze_th = 0.60f;       // VR_SQUEEZE_THRESHOLD
static float vr_snap_angle = 30.0f;       // VR_TURN_SNAP_ANGLE (graus)
static float vr_move_scale = 1.0f;        // VR_MOVE_SCALE
static bool  vr_left_handed;              // VR_LEFT_HANDED (troca os sticks)
static int   vr_crouch_mode = 3;          // VR_CROUCH_MODE: 0 off, 1 físico, 2 botão, 3 os dois
static float vr_crouch_drop = 0.25f;      // VR_CROUCH_DROP (metros)
static bool  vr_roomscale = true;         // VR_ROOMSCALE=0 desliga a posição do HMD
static float vr_roomscale_max = 0.6f;     // VR_ROOMSCALE_MAX (metros)
static float vr_vignette = 0.0f;          // VR_COMFORT_VIGNETTE (0 = desligado)

// estado da locomoção
static float vr_yaw_offset;               // graus acumulados pelo snap turn
static int   vr_snap_latch;               // 0 solto, +1/-1 lado já disparado
static float vr_head_m[3];                // cabeça no espaço LOCAL (metros, eixos GoldSrc)
static float vr_neutral_m[3];             // idem no último recentrar
static bool  vr_neutral_valid;
static bool  vr_duck_phys;
static float vr_head_raw[3];              // pitch/yaw/roll crus do HMD (sem o snap)
static unsigned int vr_buttons_prev;
static float vr_vignette_now;             // intensidade da vignette neste frame

// estado lido do OpenXR (por frame)
static bool  in_valid;
static float in_move[2], in_turn[2];
static float in_fire, in_altfire, in_squeeze;
static bool  in_jump, in_reload, in_use, in_score, in_crouch_btn, in_menu;
static bool  in_menu_prev;
static bool  in_hand_ok[2];
static float in_hand_g[2][3];
static char  in_profile[2][128] = { "(?)", "(?)" };

// ---------------- arma na mão / mira (Fase 6) ----------------
// Ver a seção "arma na mão" mais abaixo e o cabeçalho do arquivo.
enum { VR_AIM_HEAD = 0, VR_AIM_LEFT = 1, VR_AIM_RIGHT = 2 };

static int    vr_aim_source = VR_AIM_RIGHT; // VR_AIM_SOURCE (padrão: mão direita)
static bool   vr_wpn_world_depth = true;    // VR_WEAPON_WORLD_DEPTH=0 desliga
static bool   vr_move_pitch_comp;           // VR_MOVE_PITCH_COMP (padrão off)
static XrPosef in_hand_pose[2];             // pose da mira (orientação inclusa)

#define VR_WPN_CLASSES 10
static const char *vr_wpn_class_names[VR_WPN_CLASSES] =
{
	"pistol", "smg", "rifle", "sniper", "heavy", "knife", "grenade", "c4", "shield", "other"
};

// tabela de encaixe, de mods/vr_weapon.cfg (hot-reload). Offset no referencial
// da ARMA: [0] = frente (ao longo do cano), [1] = direita, [2] = cima.
static struct
{
	float global[3];
	float cls[VR_WPN_CLASSES][3];
	bool  has_cls[VR_WPN_CLASSES];
} vr_wpn;

static int    vr_wpn_class = VR_WPN_CLASSES - 1; // classe do modelo atual
static char   vr_wpn_model[64] = "-";            // viewmodel atual (painel/log)
static double vr_wpn_cfg_mtime = -1.0;
static double vr_wpn_cfg_next_t;                 // próxima checagem de mtime
static bool   vr_wpn_debug;                      // VR_WEAPON_DEBUG: log cru 1x/s
static double vr_wpn_dbg_next;                   // throttle do log e dos avisos

// alcance máximo aceitável da mão em relação à cabeça, em unidades GoldSrc
// (120 u ≈ 3 m — a mão real não passa de ~1,2 m). Acima disso a pose veio
// furada e é melhor deixar a arma onde o jogo a põe do que movê-la para longe.
#define VR_WPN_MAX_REACH 120.0f

// offset sintético de teste (VR_WEAPON_TEST), em unidades GoldSrc, a partir do
// olho; definido = ligado (inclusive "0 0 0"). Só para diagnóstico sem headset.
static float  vr_wpn_test[3];
static bool   vr_wpn_test_on;

static float wrap180( float a )
{
	while( a > 180.0f ) a -= 360.0f;
	while( a < -180.0f ) a += 360.0f;
	return a;
}

// último componente de um path de perfil de interação, para o painel caber
static const char *short_profile( const char *p )
{
	const char *s = strrchr( p, '/' );

	return ( s && s[1] ) ? s + 1 : p;
}

// O jogo só tem arma na mão se o player tiver um VIEWMODEL: quem entra na
// partida e não escolhe time fica espectador — sem arma, sem HUD e sem
// crosshair (mas ainda andando). Sem separar isso, "a arma não aparece" parece
// erro de posição quando pode ser o jogo sem arma nenhuma.
//
// Estas linhas existem para responder isso de dentro do jogo, sem depender do
// painel nem de headset: logam quando o viewmodel MUDA e quando ele DESAPARECE
// (o caso do espectador). Valem no flat também, porque o engine preenche
// `model_name` de qualquer forma.
static char vr_wpn_last_model[64] = "?"; // sentinela != "" para o 1o log sair
static char vr_wpn_last_prof[2][128] = { "?", "?" };

static void vr_wpn_diag( const mod_viewmodel_t *vm )
{
	// modelo do viewmodel: é a arma que o jogo quer desenhar
	if( strcmp( vr_wpn_last_model, vm->model_name ) != 0 )
	{
		snprintf( vr_wpn_last_model, sizeof( vr_wpn_last_model ), "%s", vm->model_name );

		if( vm->model_name[0] )
			vr_note( VR_SEV_INFO, "arma: viewmodel do jogo = %s\n", vm->model_name );
		else
			vr_note( VR_SEV_WARN,
				"arma: o JOGO nao tem viewmodel — nao ha arma para por na mao "
				"(espectador? falta escolher time/classe; o menu e 2D, no monitor)\n" );
	}

	// perfil de interação por mão: "(nenhum)" = nenhum binding casou naquela mão.
	// Só faz sentido com a sessão ativa (no flat não há ações).
	if( !in_valid )
		return;

	for( int h = 0; h < 2; h++ )
	{
		if( strcmp( vr_wpn_last_prof[h], in_profile[h] ) == 0 )
			continue;

		snprintf( vr_wpn_last_prof[h], sizeof( vr_wpn_last_prof[h] ), "%s", in_profile[h] );
		vr_note( VR_SEV_INFO, "arma: perfil de interacao da mao %s = %s\n",
			h == 0 ? "esquerda" : "direita", short_profile( in_profile[h] ));
	}
}

static void vr_recenter( void )
{
	vr_yaw_offset = 0.0f;
	vr_neutral_valid = false; // vr_actions_sync recaptura no próximo frame
	vr_duck_phys = false;
	vr_snap_latch = 0;
	vr_note( VR_SEV_INFO, "recentrar: giro do snap zerado e altura recalibrada\n" );
}

// roda um vetor horizontal do espaço do playspace para o do mundo: é a mesma
// relação que o snap turn estabelece entre os dois (usada pelo roomscale e pela
// arma, para os dois concordarem)
static void vr_rotate_yaw_offset( float v[3] )
{
	float c = ( float )cos( vr_yaw_offset * M_PI / 180.0 );
	float s = ( float )sin( vr_yaw_offset * M_PI / 180.0 );
	float x = v[0], y = v[1];

	v[0] = x * c - y * s;
	v[1] = x * s + y * c;
}

// deslocamento horizontal do roomscale (unidades GoldSrc), já rodado pelo snap.
// O eixo vertical NÃO entra: agachar move o corpo pelo IN_DUCK do jogo, não pela
// câmera — se os dois acontecessem a vista desceria duas vezes.
static void vr_roomscale_offset( float out[3] )
{
	out[0] = out[1] = out[2] = 0.0f;

	if( !vr_roomscale || !vr_neutral_valid || !in_valid )
		return;

	float d[3] = { vr_head_m[0] - vr_neutral_m[0], vr_head_m[1] - vr_neutral_m[1], 0.0f };
	float len = sqrtf( d[0] * d[0] + d[1] * d[1] );

	if( len > vr_roomscale_max )
	{
		float k = vr_roomscale_max / len;

		d[0] *= k;
		d[1] *= k;
	}

	vr_rotate_yaw_offset( d );

	out[0] = d[0] * UNITS_PER_METER;
	out[1] = d[1] * UNITS_PER_METER;
	out[2] = 0.0f;
}

// ---------------- ajuste fino do IPD (estéreo) ----------------
// O afastamento entre as câmeras sai da posição dos dois olhos que o runtime
// informa — e esse número é do HEADSET, não do jogo. Ele é o que decide se o par
// funde: grande demais, objetos PRÓXIMOS pedem convergência excessiva e não
// fundem, enquanto os distantes (disparidade minúscula) continuam parecendo
// certos. É exatamente esse o sintoma que se corrige aqui.
//
// Por isso o ajuste é um DELTA sobre a base (runtime ou VR_IPD) e não um valor
// absoluto: mexer no IPD do runtime não invalida a calibração. O delta é
// aplicado escalando o vetor medido entre os olhos — a DIREÇÃO (o cant da ótica,
// um eventual deslocamento vertical) fica preservada.
//
// Esta seção é só estado + arquivo + teclado + log; quem aplica é o VR_Camera.

// Faixa do IPD FINAL, em mm. O piso é 0 e isso é FÍSICA, não escolha: com 0 mm as
// duas câmeras ficam no mesmo ponto, os dois olhos recebem imagens idênticas e o
// estéreo simplesmente não existe (imagem sem profundidade, "mono em VR"). Abaixo
// de zero os olhos trocam de lado — o par vira espelho e o cérebro não funde.
// O teto é folga de sanidade (nenhum headset chega perto disso).
//
// Aplicado sobre o IPD EFETIVO, não sobre o ajuste: assim a folga não muda de
// tamanho quando a base muda (o runtime ora informa 68,3, ora 67,3 mm).
#define VR_IPD_MIN_MM 0.0f
#define VR_IPD_MAX_MM 110.0f

// Ajuste compilado (delta em mm) — o valor que ficou melhor calibrando o Quest 3
// aqui, para o próximo `make` já começar nele. Ordem de precedência no init:
// este padrão < VR_IPD_ADJUST < mods/vr_ipd.cfg (a calibração ao vivo).
//
// −67,2 mm é o PISO na base do Quest 3 (67,2 mm): calibrando no headset o dono
// desceu de 1 em 1 mm até as câmeras coincidirem (IPD final 0,0 mm, mono) e foi
// ali que o par fundiu melhor — "quanto menos, melhor" até o fim da faixa. Com
// uma base menor o init prende no piso (vr_ipd_clamp_mm) e dá no mesmo; com uma
// base maior sobram os poucos mm que couberem, que já é quase mono.
#define VR_IPD_DELTA_DEFAULT_MM (-67.2f)

static double vr_ipd_cfg_mtime = -1.0;
static double vr_ipd_cfg_next_t;             // próxima checagem de mtime

// separação entre os olhos no referencial da CABEÇA, medida 1x por frame (o
// delta vem do runtime no espaço de referência e giraria junto com a cabeça; no
// referencial dela fica parado em ~(IPD, 0, 0))
static void vr_measure_eye_delta( void )
{
	const XrVector3f &p0 = xr_views[0].pose.position;
	const XrVector3f &p1 = xr_views[1].pose.position;
	float d_ref[3] = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
	float c0[3], c1[3], c2[3];

	quat_matrix( xr_views[0].pose.orientation, c0, c1, c2 );
	vr_eye_dl[0] = dot3( c0, d_ref );
	vr_eye_dl[1] = dot3( c1, d_ref );
	vr_eye_dl[2] = dot3( c2, d_ref );
	vr_ipd_rt = sqrtf( dot3( vr_eye_dl, vr_eye_dl ));
}

// IPD de base em mm, tal como o runtime informa (0 = ainda sem medida dos olhos)
static float vr_ipd_base_mm( void )
{
	return ( vr_ipd > 0.0f ? vr_ipd : vr_ipd_rt ) * 1000.0f;
}

// IPD que o render usa agora, em metros: base (runtime ou VR_IPD) + ajuste, já
// preso na faixa utilizável. Com ele em 0 o VR_Camera não afasta câmera nenhuma
// e os dois olhos desenham a MESMA imagem (mono).
static float vr_ipd_eff( void )
{
	float eff = ( vr_ipd > 0.0f ? vr_ipd : vr_ipd_rt ) + vr_ipd_delta;

	if( eff < VR_IPD_MIN_MM / 1000.0f )
		eff = VR_IPD_MIN_MM / 1000.0f;

	return eff;
}

// Prende um ajuste (mm) para o IPD final caber em [VR_IPD_MIN_MM, VR_IPD_MAX_MM].
// Com a base conhecida é ela que define o quanto ainda dá para descer; sem base
// (antes da primeira pose válida, ou arquivo editado à mão) vale um teto
// simétrico — o render reaplica o piso de qualquer forma.
static float vr_ipd_clamp_mm( float dmm, const char **why )
{
	const float base_mm = vr_ipd_base_mm();

	if( why )
		*why = NULL;

	if( base_mm > 0.0f )
	{
		const float eff = base_mm + dmm;

		if( eff < VR_IPD_MIN_MM )
		{
			dmm += VR_IPD_MIN_MM - eff;
			if( why ) *why = "limite do ajuste (IPD minimo)";
		}
		else if( eff > VR_IPD_MAX_MM )
		{
			dmm -= eff - VR_IPD_MAX_MM;
			if( why ) *why = "limite do ajuste (IPD maximo)";
		}
	}
	else if( dmm > 100.0f )
	{
		dmm = 100.0f;
		if( why ) *why = "limite do ajuste (IPD maximo)";
	}
	else if( dmm < -100.0f )
	{
		dmm = -100.0f;
		if( why ) *why = "limite do ajuste (IPD minimo)";
	}

	return dmm;
}

// o arquivo guarda o DELTA (não o valor final): é o que continua fazendo sentido
// se o IPD do headset mudar
static void vr_ipd_cfg_save( void )
{
	struct stat st;
	FILE *f = fopen( "mods/vr_ipd.cfg", "w" );

	if( !f )
		return;

	fprintf( f, "# ajuste fino do IPD (estéreo) — escrito pelo mod_vr.so, não precisa editar\n" );
	fprintf( f, "# ipd_mm é um DELTA em milímetros sobre o IPD que o runtime informa;\n" );
	fprintf( f, "# positivo = câmeras mais afastadas. posicao = o mesmo em décimos de mm.\n" );
	fprintf( f, "ipd_mm %.1f\n", ( double )( vr_ipd_delta * 1000.0f ));
	fprintf( f, "posicao %d\n", vr_ipd_steps );
	fclose( f );

	// registra a mtime do que ACABAMOS de escrever: sem isto o tick veria a
	// mudança no próximo segundo e releria o próprio arquivo (log em dobro)
	if( stat( "mods/vr_ipd.cfg", &st ) == 0 )
		vr_ipd_cfg_mtime = ( double )st.st_mtime;
}

static void vr_ipd_cfg_load( void )
{
	struct stat st;
	const char *why = NULL;
	float mm = 0.0f;
	bool  got = false;
	char  line[256];
	FILE *f = fopen( "mods/vr_ipd.cfg", "r" );

	if( stat( "mods/vr_ipd.cfg", &st ) == 0 )
		vr_ipd_cfg_mtime = ( double )st.st_mtime;

	if( !f )
		return;

	while( fgets( line, sizeof( line ), f ))
	{
		char *p = line;

		while( *p == ' ' || *p == '\t' )
			p++;

		// 'ipd_mm' é o campo canônico (o outro, 'posicao', é o mesmo número em
		// décimos de mm — legível para quem for reportar o valor)
		if( sscanf( p, "ipd_mm %f", &mm ) == 1 )
		{
			got = true;
			break;
		}
	}

	fclose( f );

	if( !got )
		return;

	// sanidade: arquivo editado à mão para um valor fora da faixa não deixa a
	// imagem impossível de fundir sem o usuário perceber por quê
	mm = vr_ipd_clamp_mm( mm, &why );

	vr_ipd_delta = mm / 1000.0f;
	vr_ipd_steps = ( int )lroundf( vr_ipd_delta * 10000.0f ); // décimos de mm
	vr_ipd_delta_src = "vr_ipd.cfg";

	vr_note( VR_SEV_INFO, "ipd: ajuste de mods/vr_ipd.cfg = %+.1f mm (posicao %+d)%s\n",
		( double )mm, vr_ipd_steps, why ? " — preso na faixa" : "" );
}

// cronometra o arquivo no máximo 1x/s (é o que faz editar o .cfg valer sem
// reiniciar o jogo — o mesmo padrão do vr_weapon.cfg)
static void vr_ipd_cfg_tick( void )
{
	struct stat st;
	double now;

	if( !eng )
		return;

	now = eng->DoubleTime();

	if( now < vr_ipd_cfg_next_t )
		return;

	vr_ipd_cfg_next_t = now + 1.0;

	if( stat( "mods/vr_ipd.cfg", &st ) != 0 )
		return;

	if(( double )st.st_mtime != vr_ipd_cfg_mtime )
		vr_ipd_cfg_load();
}

// linha única com tudo o que importa para calibrar: o valor em uso, a base de
// onde ele saiu, o ajuste e o número da posição (décimos de mm — é o número que
// o dono lê e reporta). Vai tanto para o log quanto para o painel.
static void vr_ipd_state( char *buf, size_t len )
{
	const float base_mm = vr_ipd_base_mm();
	int n;

	// antes da primeira pose válida não existe base nenhuma: dizer "0.0 mm"
	// seria pior do que dizer que falta a medida
	if( base_mm <= 0.0f )
	{
		snprintf( buf, len, "IPD ainda sem medida do runtime | ajuste %+.1f mm | posicao %+d",
			( double )( vr_ipd_delta * 1000.0f ), vr_ipd_steps );
		return;
	}

	n = snprintf( buf, len, "IPD %.1f mm = base %.1f (%s) %+.1f | posicao %+d",
		( double )( vr_ipd_eff() * 1000.0f ), ( double )base_mm, vr_ipd_base_src,
		( double )( vr_ipd_delta * 1000.0f ), vr_ipd_steps );

	// 0 mm é mono (as câmeras coincidem) — vale dizer, para não ser confundido
	// com "o ajuste parou de funcionar"
	if( vr_ipd_eff() <= 0.0001f && n > 0 && ( size_t )n < len )
		snprintf( buf + n, len - n, " | MONO (cameras coincidem)" );
	else if( vr_ipd_delta != 0.0f && n > 0 && ( size_t )n < len )
		snprintf( buf + n, len - n, " | ajuste por %s", vr_ipd_delta_src );
}

static void vr_ipd_report( const char *why )
{
	char buf[192];

	vr_ipd_state( buf, sizeof( buf ));
	vr_note( VR_SEV_INFO, "ipd: %s | %s%s\n", why, buf,
		vr_ipd_rt > 0.0f ? "" : " | (runtime ainda nao informou os olhos)" );
	vr_ipd_cfg_save();
}

static void vr_ipd_nudge( float dmm )
{
	const char *why = NULL;
	float mm = vr_ipd_clamp_mm( vr_ipd_delta * 1000.0f + dmm, &why );

	vr_ipd_delta = mm / 1000.0f;
	vr_ipd_steps = ( int )lroundf( vr_ipd_delta * 10000.0f );
	vr_ipd_delta_src = "teclado";

	vr_ipd_report( why ? why : ( dmm > 0.0f ? "afastando" : "aproximando" ));
}

static void vr_ipd_reset( void )
{
	if( vr_ipd_delta == 0.0f && vr_ipd_steps == 0 )
	{
		vr_ipd_report( "ja estava zerado" );
		return;
	}

	vr_ipd_delta = 0.0f;
	vr_ipd_steps = 0;
	vr_ipd_delta_src = "-";

	vr_ipd_report( "zerado (volta a base)" );
}

// Teclado do ajuste (ABI v7, Mod_Key). Só consome tecla com o modo LIGADO — fora
// dele, devolver 1 aqui apagaria a tecla do jogo sem o usuário ter pedido nada.
// As teclas são as de POSIÇÃO física no layout americano (o engine mapeia por
// scancode): '-', '=', ',' e '.' ficam no mesmo lugar no ABNT2, e '['/']' NÃO
// (ficam trocados) — por isso eles não entram.
static int vr_ipd_key( int key, int down )
{
	// modo de ajuste: F9 (Home/End também, para o caso da F9 estar presa no
	// gerenciador de janelas). Consome o down E o up, senão o jogo vê meia tecla.
	if( key == MOD_KEY_F9 || key == MOD_KEY_HOME || key == MOD_KEY_END )
	{
		if( down )
		{
			vr_ipd_adj_on = !vr_ipd_adj_on;
			vr_ipd_report( vr_ipd_adj_on ? "MODO DE AJUSTE LIGADO (teclas presas por este mod)"
				: "modo de ajuste DESLIGADO (teclado de volta ao jogo)" );
		}
		return 1;
	}

	if( !vr_ipd_adj_on )
		return 0;

	switch( key )
	{
	case '-': if( down ) vr_ipd_nudge( -1.0f ); return 1;
	case '=': if( down ) vr_ipd_nudge( +1.0f ); return 1;
	case ',': if( down ) vr_ipd_nudge( -0.1f ); return 1;
	case '.': if( down ) vr_ipd_nudge( +0.1f ); return 1;
	case '0': if( down ) vr_ipd_reset();        return 1;
	default: break;
	}

	return 0;
}

// ---------------- encaixe da arma (mods/vr_weapon.cfg) ----------------
// O viewmodel do CS é de 1999: a origem dele é o OLHO do player e a geometria
// (inclusive a mão desenhada) fica abaixo/à direita DISSO. Com a origem na mão,
// o modelo aparece deslocado — este offset corrige, e é aplicado no referencial
// da ARMA (frente/direita/cima da mão) para que girar a mão não mude o encaixe.
//
// Formato (uma diretiva por linha, '#' comenta, linhas vazias ignoradas):
//   global X Y Z              padrão para todas as classes
//   class <nome> X Y Z        sobrescreve aquela classe
//
// A leitura é tolerante: linha inválida é ignorada com aviso e o que já valia
// continua valendo — o arquivo nunca deixa a arma numa posição absurda.

// nome do viewmodel → classe (a tabela de encaixe usa o índice)
static int vr_wpn_class_id( const char *name )
{
	static const struct { const char *tok; int cls; } map[] =
	{
		{ "deagle", 0 }, { "usp", 0 }, { "glock", 0 }, { "p228", 0 }, { "fiveseven", 0 }, { "elite", 0 },
		{ "mp5", 1 }, { "tmp", 1 }, { "mac10", 1 }, { "ump45", 1 }, { "p90", 1 },
		{ "ak47", 2 }, { "m4a1", 2 }, { "sg552", 2 }, { "aug", 2 }, { "galil", 2 }, { "famas", 2 },
		{ "awp", 3 }, { "scout", 3 }, { "g3sg1", 3 }, { "sg550", 3 },
		{ "m249", 4 }, { "xm1014", 4 }, { "m3", 4 },
		{ "knife", 5 },
		{ "hegrenade", 6 }, { "flashbang", 6 }, { "smokegrenade", 6 },
		{ "c4", 7 }, { "shield", 8 },
	};

	for( size_t i = 0; i < sizeof( map ) / sizeof( map[0] ); i++ )
		if( strstr( name, map[i].tok ))
			return map[i].cls;

	return VR_WPN_CLASSES - 1; // "other"
}

static void vr_wpn_cfg_load( bool first )
{
	struct stat st;
	FILE *f = fopen( "mods/vr_weapon.cfg", "r" );

	// registra a mtime JÁ na carga inicial: sem isto o primeiro tick veria "-1"
	// e releria o mesmo arquivo no primeiro frame (log em dobro, à toa)
	if( stat( "mods/vr_weapon.cfg", &st ) == 0 )
		vr_wpn_cfg_mtime = ( double )st.st_mtime;

	if( !f )
	{
		if( first )
			vr_note( VR_SEV_INFO, "arma: mods/vr_weapon.cfg ausente — encaixe neutro (0 0 0)\n" );
		return;
	}

	// trabalha numa cópia: um arquivo com erro não apaga o que já valia
	float g[3], c[VR_WPN_CLASSES][3];
	bool  hc[VR_WPN_CLASSES];
	char  line[256];
	int   n = 0;

	memcpy( g, vr_wpn.global, sizeof( g ));
	memcpy( c, vr_wpn.cls, sizeof( c ));
	memcpy( hc, vr_wpn.has_cls, sizeof( hc ));

	while( fgets( line, sizeof( line ), f ))
	{
		char *p = line;
		float x, y, z;

		while( *p == ' ' || *p == '\t' )
			p++;

		// tira o fim de linha antes de qualquer coisa (senão ele vai para o log)
		size_t L = strlen( p );
		while( L > 0 && ( p[L - 1] == '\n' || p[L - 1] == '\r' ))
			p[--L] = 0;

		if( !*p || *p == '#' )
			continue;

		if( !strncmp( p, "global", 6 ))
		{
			if( sscanf( p + 6, "%f %f %f", &x, &y, &z ) == 3 )
			{
				g[0] = x; g[1] = y; g[2] = z;
				n++;
			}
			else
			{
				vr_note( VR_SEV_WARN, "arma: linha 'global' invalida no vr_weapon.cfg (ignorada)\n" );
			}
			continue;
		}

		if( !strncmp( p, "class", 5 ))
		{
			char nm[32];

			if( sscanf( p + 5, "%31s %f %f %f", nm, &x, &y, &z ) == 4 )
			{
				int k = -1;

				for( int i = 0; i < VR_WPN_CLASSES; i++ )
					if( !strcmp( nm, vr_wpn_class_names[i] ))
						k = i;

				if( k >= 0 )
				{
					c[k][0] = x; c[k][1] = y; c[k][2] = z;
					hc[k] = true;
					n++;
				}
				else
				{
					vr_note( VR_SEV_WARN, "arma: classe desconhecida '%s' no vr_weapon.cfg\n", nm );
				}
			}
			else
			{
				vr_note( VR_SEV_WARN, "arma: linha 'class' invalida no vr_weapon.cfg (ignorada)\n" );
			}
			continue;
		}

		vr_note( VR_SEV_WARN, "arma: linha ignorada no vr_weapon.cfg: %s\n", p );
	}

	fclose( f );

	memcpy( vr_wpn.global, g, sizeof( g ));
	memcpy( vr_wpn.cls, c, sizeof( c ));
	memcpy( vr_wpn.has_cls, hc, sizeof( hc ));

	vr_note( VR_SEV_INFO, "arma: vr_weapon.cfg lido (%d linha(s)) — global %+.0f %+.0f %+.0f\n",
		n, ( double )g[0], ( double )g[1], ( double )g[2] );
}

// offset que vale agora: a classe do modelo, se ela tiver linha própria
static const float *vr_wpn_active_offset( void )
{
	return vr_wpn.has_cls[vr_wpn_class] ? vr_wpn.cls[vr_wpn_class] : vr_wpn.global;
}

// checa a mtime do .cfg no máximo 1x/s: salvou o arquivo, o efeito aparece no
// headset em ~1 s, sem reiniciar o jogo (é assim que se calibra o encaixe)
static void vr_wpn_cfg_tick( void )
{
	struct stat st;
	double now;

	if( !eng )
		return;

	now = eng->DoubleTime();

	if( now < vr_wpn_cfg_next_t )
		return;

	vr_wpn_cfg_next_t = now + 1.0;

	if( stat( "mods/vr_weapon.cfg", &st ) != 0 )
		return;

	if(( double )st.st_mtime != vr_wpn_cfg_mtime )
	{
		vr_wpn_cfg_mtime = ( double )st.st_mtime;
		vr_wpn_cfg_load( false );
	}
}

static const char *vr_aim_source_name( void )
{
	if( vr_aim_source == VR_AIM_HEAD )
		return "cabeca";

	return vr_aim_source == VR_AIM_LEFT ? "mao esq" : "mao dir";
}

static bool vr_make_action( XrAction *out, XrActionType type, const char *name, const char *label )
{
	XrActionCreateInfo ci { XR_TYPE_ACTION_CREATE_INFO };
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

struct VrBind
{
	XrAction    *action;
	const char  *path;
};

static XrResult vr_suggest_bindings( const char *profile, const VrBind *binds, int n )
{
	std::vector<XrActionSuggestedBinding> sb;
	XrPath pp = XR_NULL_PATH;
	XrInteractionProfileSuggestedBinding ip { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
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

	XrActionSetCreateInfo asc { XR_TYPE_ACTION_SET_CREATE_INFO };

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

	ok = vr_make_action( &xr_act_move,    XR_ACTION_TYPE_VECTOR2F_INPUT, "move",     "Mover" ) && ok;
	ok = vr_make_action( &xr_act_turn,    XR_ACTION_TYPE_VECTOR2F_INPUT, "turn",     "Girar" ) && ok;
	ok = vr_make_action( &xr_act_fire,    XR_ACTION_TYPE_FLOAT_INPUT,    "fire",     "Atirar" ) && ok;
	ok = vr_make_action( &xr_act_altfire, XR_ACTION_TYPE_FLOAT_INPUT,    "altfire",  "Ataque secundario" ) && ok;
	ok = vr_make_action( &xr_act_grip,    XR_ACTION_TYPE_FLOAT_INPUT,    "grip",     "Andar devagar" ) && ok;
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
		XrActionSpaceCreateInfo sci { XR_TYPE_ACTION_SPACE_CREATE_INFO };

		xrStringToPath( xr_instance, h == 0 ? "/user/hand/left" : "/user/hand/right", &xr_path_hand[h] );

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
		{ &xr_act_grip,    "/user/hand/left/input/squeeze/value" },
		{ &xr_act_grip,    "/user/hand/right/input/squeeze/value" },
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

	XrSessionActionSetsAttachInfo ai { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };

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
		XrInteractionProfileState ips { XR_TYPE_INTERACTION_PROFILE_STATE };
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
	XrActionsSyncInfo si { XR_TYPE_ACTIONS_SYNC_INFO };

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

	XrActionStateGetInfo gi { XR_TYPE_ACTION_STATE_GET_INFO };
	XrActionStateVector2f v2 { XR_TYPE_ACTION_STATE_VECTOR2F };
	XrActionStateFloat fv { XR_TYPE_ACTION_STATE_FLOAT };
	XrActionStateBoolean bl { XR_TYPE_ACTION_STATE_BOOLEAN };

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
	gi.action = xr_act_grip;
	if( XR_SUCCEEDED( xrGetActionStateFloat( xr_session, &gi, &fv )) && fv.isActive ) in_squeeze = fv.currentState;

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
		XrSpaceLocation loc { XR_TYPE_SPACE_LOCATION };
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
static void VR_Input( mod_input_sample_t *s )
{
	// zerada a cada frame: se a sessão cair, a vignette não fica presa
	vr_vignette_now = 0.0f;

	if( !vr_active || !xr_views_ok || !in_valid )
		return;

	// ---- orientação: quem manda é a fonte da mira (VR_AIM_SOURCE) ----
	// Com a mira na mão, o eixo da ARMA é o que vai ao servidor: a direção do
	// tiro é v_angle + punchangle lá, então é isto que faz o tiro sair para onde
	// a arma aponta. Se a pose da mão não estiver válida, cai na cabeça — nunca
	// se fica sem mira (fail-safe).
	int aim_hand = ( vr_aim_source == VR_AIM_LEFT ) ? 0 : ( vr_aim_source == VR_AIM_RIGHT ? 1 : -1 );

	if( aim_hand >= 0 && in_hand_ok[aim_hand] )
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

	if( !menu_takes_trigger && in_fire > vr_trigger_th ) st |= MOD_BTN_ATTACK;
	// recuo visual do viewmodel próprio: dispara na BORDA do gatilho (não a cada
	// frame, senão o kick ficaria "ligado" enquanto o gatilho estivesse puxado).
	{
		static bool fire_prev;
		const bool fire_now = !menu_takes_trigger && in_fire > vr_trigger_th;
		if( fire_now && !fire_prev ) vr_hands_trigger_recoil();
		fire_prev = fire_now;
	}
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

// ---------------- OpenXR ----------------
static bool xr_make_swapchains( void )
{
	uint32_t nvc = 0;
	OXR( xrEnumerateViewConfigurationViews( xr_instance, xr_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &nvc, NULL ));
	if( nvc < 2 )
	{
		vr_note( VR_SEV_ERROR, "view config: %u olho(s); runtime nao expoe estereo\n", nvc );
		return false;
	}

	std::vector<XrViewConfigurationView> vc( nvc, { XR_TYPE_VIEW_CONFIGURATION_VIEW } );
	OXR( xrEnumerateViewConfigurationViews( xr_instance, xr_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, nvc, &nvc, vc.data()));

	// Formato das swapchains: precisa ser um dos que o runtime enumera (o
	// valor 0 = GL_NONE não é válido; o Monado recusa com
	// XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED). Preferimos formato LINEAR
	// (GL_RGBA8) porque o engine escreve valores já codificados em gama: um
	// alvo sRGB faria o compositor decodificar de novo e a imagem sairia
	// escura. VR_SWAPCHAIN_FORMAT=<inteiro GL> força outro (diagnóstico).
	GLint chosen_format = 0;
	{
		uint32_t nfmt = 0;
		OXR( xrEnumerateSwapchainFormats( xr_session, 0, &nfmt, NULL ));

		std::vector<int64_t> fmts( nfmt > 0 ? nfmt : 1, 0 );
		OXR( xrEnumerateSwapchainFormats( xr_session, nfmt, &nfmt, fmts.data()));

		const int64_t prefer[] = { GL_RGBA8, GL_SRGB8_ALPHA8 };
		for( size_t w = 0; w < sizeof( prefer ) / sizeof( prefer[0] ) && !chosen_format; w++ )
			for( uint32_t i = 0; i < nfmt; i++ )
				if( fmts[i] == prefer[w] )
				{
					chosen_format = ( GLint )fmts[i];
					break;
				}

		if( !chosen_format && nfmt > 0 )
			chosen_format = ( GLint )fmts[0]; // nenhum conhecido: o primeiro serve

		{
			int over = ( int )env_float( "VR_SWAPCHAIN_FORMAT", 0.0f );
			if( over > 0 )
				chosen_format = over;
		}

		if( !chosen_format )
		{
			vr_note( VR_SEV_ERROR, "runtime nao ofereceu nenhum formato de swapchain OpenGL\n" );
			return false;
		}

		{
			char list[160];
			size_t off = 0;

			list[0] = 0;
			for( uint32_t i = 0; i < nfmt && off < sizeof( list ) - 12; i++ )
				off += ( size_t )snprintf( list + off, sizeof( list ) - off, "%s%lld", i ? " " : "", ( long long )fmts[i] );

			vr_note( VR_SEV_INFO, "swapchain: formato 0x%X (%s) de [%s]\n", ( unsigned )chosen_format,
				chosen_format == GL_RGBA8 ? "RGBA8 linear" : ( chosen_format == GL_SRGB8_ALPHA8 ? "SRGB8_ALPHA8" : "escolhido" ),
				list );
		}
	}

	for( uint32_t e = 0; e < 2; e++ )
	{
		EyeRT &rt = xr_eye[e];
		rt.w = ( int32_t )( vc[e].recommendedImageRectWidth * vr_scale );
		rt.h = ( int32_t )( vc[e].recommendedImageRectHeight * vr_scale );

		XrSwapchainCreateInfo sci { XR_TYPE_SWAPCHAIN_CREATE_INFO };
		sci.arraySize = 1;
		sci.format = chosen_format;
		sci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
		sci.sampleCount = vc[e].recommendedSwapchainSampleCount;
		sci.width = rt.w;
		sci.height = rt.h;
		sci.faceCount = 1;
		sci.mipCount = 1;
		OXR( xrCreateSwapchain( xr_session, &sci, &rt.sc ));

		uint32_t nimg = 0;
		OXR( xrEnumerateSwapchainImages( rt.sc, 0, &nimg, NULL ));
		rt.images.resize( nimg, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR } );
		OXR( xrEnumerateSwapchainImages( rt.sc, nimg, &nimg,
			reinterpret_cast<XrSwapchainImageBaseHeader *>( rt.images.data())));

		pglGenRenderbuffers( 1, &rt.depth );
		pglBindRenderbuffer( GL_RENDERBUFFER, rt.depth );
		pglRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, rt.w, rt.h );

		rt.fbos.resize( nimg, 0 );
		pglGenFramebuffers( nimg, rt.fbos.data());
		for( uint32_t i = 0; i < nimg; i++ )
		{
			pglBindFramebuffer( GL_FRAMEBUFFER, rt.fbos[i] );
			pglFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt.images[i].image, 0 );
			pglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rt.depth );
		}
		pglBindFramebuffer( GL_FRAMEBUFFER, 0 );

		// Um FBO incompleto faz o driver DESCARTAR todo desenho em silêncio —
		// é uma das causas de "headset preto com log limpo", então é checado.
		if( vrglCheckFramebufferStatus )
		{
			pglBindFramebuffer( GL_FRAMEBUFFER, rt.fbos[0] );
			GLenum st = vrglCheckFramebufferStatus( GL_FRAMEBUFFER );
			pglBindFramebuffer( GL_FRAMEBUFFER, 0 );

			if( st != GL_FRAMEBUFFER_COMPLETE )
				vr_note( VR_SEV_ERROR, "FBO do olho %u INCOMPLETO: 0x%X (%s) — o desenho sera descartado\n",
					e, ( unsigned )st, vr_fbo_status_str( ( unsigned )st ));
			else
				vr_note( VR_SEV_INFO, "FBO do olho %u: %dx%d completo (tex %u, depth %u)\n",
					e, rt.w, rt.h, rt.images[0].image, rt.depth );
		}

		// guarda p/ o painel (o resumo de "olhos" na tela 2D)
		if( e == 0 )
		{
			vr_eye_w = rt.w;
			vr_eye_h = rt.h;
			vr_eye_images = ( int )nimg;
		}
		vr_note( VR_SEV_OK, "olho %u: %dx%d, %u imagens\n", e, rt.w, rt.h, nimg );
	}
	vr_eye_count = 2;
	return true;
}

static void xr_poll_events( void )
{
	XrEventDataBuffer evd { XR_TYPE_EVENT_DATA_BUFFER };
	while( xrPollEvent( xr_instance, &evd ) == XR_SUCCESS )
	{
		switch( evd.type )
		{
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
		{
			XrEventDataSessionStateChanged *sc = reinterpret_cast<XrEventDataSessionStateChanged *>( &evd );
			vr_session_state = ( int )sc->state;
			vr_note( VR_SEV_INFO, "sessao: estado %d (%s)\n", ( int )sc->state, vr_session_state_str( ( int )sc->state ));
			switch( sc->state )
			{
			case XR_SESSION_STATE_READY:
			{
				XrSessionBeginInfo bi { XR_TYPE_SESSION_BEGIN_INFO };
				bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
				if( XR_FAILED( xrBeginSession( xr_session, &bi )))
					vr_note( VR_SEV_ERROR, "xrBeginSession falhou\n" );
				else
				{
					xr_session_begun = true; // a partir daqui bombeamos frames
					vr_note( VR_SEV_OK, "sessao iniciada (xrBeginSession)\n" );
				}
				if( xr_eye[0].sc == XR_NULL_HANDLE )
					xr_make_swapchains();
				break;
			}
			case XR_SESSION_STATE_SYNCHRONIZED:
				// Ainda não é para desenhar no HMD, mas o bombeamento de frames
				// continua (ver xr_session_begun) — é o que faz o runtime
				// avançar para VISIBLE.
				break;
			case XR_SESSION_STATE_VISIBLE:
			case XR_SESSION_STATE_FOCUSED:
				vr_active = true;
				break;
			case XR_SESSION_STATE_STOPPING:
				xr_end_frame();
				xr_session_begun = false;
				vr_active = false;
				break;
			case XR_SESSION_STATE_LOSS_PENDING:
			case XR_SESSION_STATE_EXITING:
				vr_note( VR_SEV_WARN, "sessao %s: VR desligando\n", vr_session_state_str( ( int )sc->state ));
				xr_session_begun = false;
				vr_active = false;
				break;
			default:
				break;
			}
			break;
		}
		case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
			// O runtime recentralizou o playspace (o próprio headset faz isso, e o
			// comando de recentrar do WiVRn também). Sem reancorar, o delta do
			// roomscale passaria a ser medido contra uma origem que não existe mais
			// e o jogador parece teleportar.
			vr_neutral_valid = false;
			vr_note( VR_SEV_INFO, "referencia recentralizada pelo runtime: ancora/altura recapturadas\n" );
			break;
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
			vr_note( VR_SEV_ERROR, "instancia OpenXR perdida: VR desativado\n" );
			xr_session_begun = false;
			vr_active = false;
			vr_ready = false;
			return;
		default:
			break;
		}
		evd = { XR_TYPE_EVENT_DATA_BUFFER };
	}
}

static bool xr_begin_frame( void )
{
	xr_fs = { XR_TYPE_FRAME_STATE };
	if( XR_FAILED( xrWaitFrame( xr_session, NULL, &xr_fs )))
		return false;

	vr_perf_hmd_period_ms = vr_perf_hmd_period_ms > 0.0 ? vr_perf_hmd_period_ms : xr_fs.predictedDisplayPeriod / 1000000.0;
	vr_perf_work_t0 = vr_now(); // o trabalho do frame começa quando o HMD libera

	XrFrameBeginInfo bi { XR_TYPE_FRAME_BEGIN_INFO };
	XrResult r = xrBeginFrame( xr_session, &bi );
	if( XR_FAILED( r ))
	{
		// típico sem headset ativo: sessão não está rodando
		vr_active = false;
		return false;
	}

	xr_frame_began = true;

	xr_views[0] = { XR_TYPE_VIEW };
	xr_views[1] = { XR_TYPE_VIEW };
	XrViewLocateInfo vli { XR_TYPE_VIEW_LOCATE_INFO };
	vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	vli.displayTime = xr_fs.predictedDisplayTime;
	vli.space = xr_space;
	uint32_t n = 0;
	XrViewState vs { XR_TYPE_VIEW_STATE };
	r = xrLocateViews( xr_session, &vli, &vs, 2, &n, xr_views );
	bool valid = XR_SUCCEEDED( r ) && n == 2 &&
		( vs.viewStateFlags & ( XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT )) != 0;

	if( !xr_views_ok && valid && verbose )
		logf( "poses válidas chegando\n" );
	xr_views_ok = valid;

	if( vr_trace_all || vr_stereo_frames <= VR_TRACE_FRAMES )
		vr_trace( "trace: views n=%u flags=0x%X valid=%d | pose0 pos(%.2f %.2f %.2f) ori(%.2f %.2f %.2f %.2f)\n",
			n, ( unsigned )vs.viewStateFlags, valid ? 1 : 0,
			( double )xr_views[0].pose.position.x, ( double )xr_views[0].pose.position.y, ( double )xr_views[0].pose.position.z,
			( double )xr_views[0].pose.orientation.x, ( double )xr_views[0].pose.orientation.y,
			( double )xr_views[0].pose.orientation.z, ( double )xr_views[0].pose.orientation.w );

	if( valid )
	{
		// alimenta a câmera (afastamento entre os olhos) e o diagnóstico
		vr_measure_eye_delta();
		vr_log_stereo_geometry();
	}

	return true;
}

// Diagnóstico do estéreo: o que o runtime manda por olho vs. o que este mod
// desenha. É exatamente esta comparação que explica "cada olho sozinho parece
// ok, os dois juntos não fundem": qualquer diferença entre a pose/fov que o
// runtime declara em xrEndFrame e a que o engine rasteriza vira erro de
// perspectiva (ou de disparidade) que o cérebro não consegue fundir.
static void vr_log_stereo_geometry( void )
{
	static int  logged;
	static int  last_slot = -1;
	static double last_ipd = -1.0;

	const XrView &a = xr_views[0]; // olho esquerdo
	const XrView &b = xr_views[1]; // olho direito

	const double dx = ( double )( b.pose.position.x - a.pose.position.x );
	const double dy = ( double )( b.pose.position.y - a.pose.position.y );
	const double dz = ( double )( b.pose.position.z - a.pose.position.z );
	const double ipd = sqrt( dx * dx + dy * dy + dz * dz );

	// loga na primeira vez, de 5 em 5 s e quando o IPD muda (> 2 mm: o
	// usuário mexeu no ajuste do headset / outro usuário entrou)
	const int slot = ( int )( vr_now() / 5.0 );
	if( logged && slot == last_slot && fabs( ipd - last_ipd ) < 0.002 )
		return;
	logged = 1;
	last_slot = slot;
	last_ipd = ipd;

	const double d2r = 57.29577951308232;
	const XrVector3f &pa = a.pose.position;
	const XrVector3f &pb = b.pose.position;

	// delta no referencial da cabeça: a separação "de verdade" entre os olhos.
	// O dx global varia com a direção em que a cabeça está virada (o delta vem
	// no espaço de referência); no referencial da cabeça ele fica parado em
	// ~(IPD, 0, 0) — é o que o mod usa para afastar as câmeras.
	float c0[3], c1[3], c2[3], d_ref[3] = { ( float )dx, ( float )dy, ( float )dz };
	float dl[3];
	quat_matrix( a.pose.orientation, c0, c1, c2 );
	dl[0] = dot3( c0, d_ref );
	dl[1] = dot3( c1, d_ref );
	dl[2] = dot3( c2, d_ref );

	const double ipd_gap = fabs( ipd - ( double )dl[0] );

	vr_note( VR_SEV_INFO, "estereo: IPD %.1f mm do runtime (base %s, ajuste %+.1f mm = %.1f mm) | na cabeca (%.1f %.1f %.1f) mm | ori olho0/1 %s\n",
		ipd * 1000.0, vr_ipd_base_src, ( double )( vr_ipd_delta * 1000.0f ), ( double )( vr_ipd_eff() * 1000.0f ),
		( double )dl[0] * 1000.0, ( double )dl[1] * 1000.0, ( double )dl[2] * 1000.0,
		( a.pose.orientation.x == b.pose.orientation.x && a.pose.orientation.y == b.pose.orientation.y
			&& a.pose.orientation.z == b.pose.orientation.z && a.pose.orientation.w == b.pose.orientation.w )
			? "iguais (cameras paralelas)" : "DIFERENTES (olhos canted)" );

	if( ipd_gap > 0.001 )
		vr_note( VR_SEV_WARN, "estereo: eixo dos olhos nao esta ao longo da direita da cabeca (%.1f mm fora do eixo) — headset com olhos deslocados\n",
			ipd_gap * 1000.0 );

	// assimetria da frustum: se -L != R (ou U != -D), a fov NÃO é central; o
	// render tem que usar os mesmos quatro ângulos (fov_asym) ou o compositor
	// estica cada olho de um jeito.
	vr_note( VR_SEV_INFO, "estereo olho0: pos(%.1f %.1f %.1f)mm ori(%.2f %.2f %.2f %.2f) fov L%.1f R%.1f U%.1f D%.1f | assim %.1f\n",
		( double )pa.x * 1000.0, ( double )pa.y * 1000.0, ( double )pa.z * 1000.0,
		( double )a.pose.orientation.x, ( double )a.pose.orientation.y, ( double )a.pose.orientation.z, ( double )a.pose.orientation.w,
		( double )- a.fov.angleLeft * d2r, ( double )a.fov.angleRight * d2r, ( double )a.fov.angleUp * d2r, ( double )- a.fov.angleDown * d2r,
		( double )( a.fov.angleRight + a.fov.angleLeft ) * d2r );
	vr_note( VR_SEV_INFO, "estereo olho1: pos(%.1f %.1f %.1f)mm ori(%.2f %.2f %.2f %.2f) fov L%.1f R%.1f U%.1f D%.1f | assim %.1f\n",
		( double )pb.x * 1000.0, ( double )pb.y * 1000.0, ( double )pb.z * 1000.0,
		( double )b.pose.orientation.x, ( double )b.pose.orientation.y, ( double )b.pose.orientation.z, ( double )b.pose.orientation.w,
		( double )- b.fov.angleLeft * d2r, ( double )b.fov.angleRight * d2r, ( double )b.fov.angleUp * d2r, ( double )- b.fov.angleDown * d2r,
		( double )( b.fov.angleRight + b.fov.angleLeft ) * d2r );
}

// Vignette de conforto (VR_COMFORT_VIGNETTE): escurece a periferia enquanto há
// locomoção. Tem de ser desenhada na PRÓPRIA imagem do olho, antes do release —
// o desenho 2D do engine sai na janela e o headset nunca o veria.
// Malha 4x4 com alfa crescendo do centro para a borda; a interpolação do GL
// suaviza o gradiente. Estado é salvo e restaurado (PushAttrib + push/pop de
// matriz), então o cache de estado do ref_gl continua válido.
static void vr_draw_vignette( int eye )
{
	const int N = 4;

	if( eye < 0 || eye > 1 || xr_eye[eye].cur < 0 || xr_eye[eye].fbos.empty() || !vr_panel_gl_ok )
		return;

	if( xr_eye[eye].w <= 0 || xr_eye[eye].h <= 0 )
		return;

	pglBindFramebuffer( GL_FRAMEBUFFER, xr_eye[eye].fbos[xr_eye[eye].cur] );

	vrglPushAttrib( GL_ALL_ATTRIB_BITS );
	vrglMatrixMode( GL_PROJECTION ); vrglPushMatrix(); vrglLoadIdentity();
	vrglOrtho( 0.0, 1.0, 0.0, 1.0, -1.0, 1.0 );
	vrglMatrixMode( GL_MODELVIEW ); vrglPushMatrix(); vrglLoadIdentity();
	vrglDisable( GL_DEPTH_TEST );
	vrglDisable( GL_CULL_FACE );
	vrglDisable( GL_TEXTURE_2D );
	vrglEnable( GL_BLEND );
	vrglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	for( int iy = 0; iy < N; iy++ )
	{
		for( int ix = 0; ix < N; ix++ )
		{
			vrglBegin( GL_QUADS );
			for( int k = 0; k < 4; k++ )
			{
				float u = ( float )( ix + (( k == 1 || k == 2 ) ? 1 : 0 )) / N;
				float v = ( float )( iy + ( k >= 2 ? 1 : 0 )) / N;
				float dx = u - 0.5f, dy = v - 0.5f;
				float r = sqrtf( dx * dx + dy * dy ) / 0.70710678f;
				float a;

				if( r > 1.0f ) r = 1.0f;
				a = vr_vignette_now * r * r;
				if( a > 1.0f ) a = 1.0f;

				vrglColor4f( 0.0f, 0.0f, 0.0f, a );
				vrglVertex2f( u, v );
			}
			vrglEnd();
		}
	}

	vrglMatrixMode( GL_PROJECTION ); vrglPopMatrix();
	vrglMatrixMode( GL_MODELVIEW ); vrglPopMatrix();
	vrglPopAttrib();

	pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

static void xr_end_frame( void )
{
	if( !xr_frame_began )
		return;

	// Só submete o que foi de fato renderizado: as imagens dos olhos só têm
	// conteúdo se o hook de câmera rodou (fora de um mapa o engine não desenha
	// 3D — e, sem checar isto, o app submeteria imagens nunca renderizadas).
	const bool rendered = xr_eye[0].cur >= 0 && xr_eye[1].cur >= 0;

	// dump dos pares AQUI, com as imagens ainda adquiridas e depois de todo o
	// desenho do frame: é exatamente o conteúdo que vai ao compositor. Fazer
	// isto antes do 2D/painel (dentro do Mod_AfterEyes) mediria uma imagem que
	// ainda não é a submetida — e não pegaria nada que vazasse para o olho.
	// 4 amostras com 3 s de intervalo (cena escura não dá para medir; escolhe-se
	// depois a de mais textura).
	if( vr_stereo_dump_prefix[0] && vr_stereo_dump_n < 4 && vrglReadPixels
		&& rendered && vr_now() - vr_t0 >= vr_stereo_dump_at )
	{
		stereo_dump();
		vr_stereo_dump_at += 3.0f;
	}

	// vignette de conforto: entra na própria imagem do olho, antes do release
	if( vr_vignette_now > 0.001f && rendered )
	{
		for( int e = 0; e < 2; e++ )
			vr_draw_vignette( e );
	}

	// ajuste do IPD: só com o modo ligado, e DEPOIS do dump de diagnóstico (que
	// mede o que o engine desenhou, não o que este mod escreve por cima)
	if( rendered )
	{
		for( int e = 0; e < 2; e++ )
			vr_draw_ipd_hud( e );
	}

	for( int e = 0; e < 2; e++ )
	{
		if( xr_eye[e].sc != XR_NULL_HANDLE && xr_eye[e].cur >= 0 )
		{
			XrResult rr = xrReleaseSwapchainImage( xr_eye[e].sc, NULL );
			if( XR_FAILED( rr ))
				vr_note( VR_SEV_WARN, "release swapchain olho %d: %d %s\n", e, ( int )rr, vr_xr_result_str( ( int )rr ));
			xr_eye[e].cur = -1;
		}
	}

	XrCompositionLayerProjectionView lv[2] {};
	const bool submit = xr_views_ok && vr_active && rendered;

	if( submit )
	{
		for( int e = 0; e < 2; e++ )
		{
			lv[e].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			lv[e].pose = xr_views[e].pose;
			lv[e].fov = xr_views[e].fov;
			lv[e].subImage.swapchain = xr_eye[e].sc;
			lv[e].subImage.imageRect.offset = { 0, 0 };
			lv[e].subImage.imageRect.extent = { xr_eye[e].w, xr_eye[e].h };
			lv[e].subImage.imageArrayIndex = 0;
		}
	}

	XrCompositionLayerProjection proj { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	proj.space = xr_space;
	proj.viewCount = 2;
	proj.views = lv;

	const XrCompositionLayerBaseHeader *layers[1] = { reinterpret_cast<const XrCompositionLayerBaseHeader *>( &proj ) };

	XrFrameEndInfo fei { XR_TYPE_FRAME_END_INFO };
	fei.displayTime = xr_fs.predictedDisplayTime;
	fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	if( submit )
	{
		fei.layerCount = 1;
		fei.layers = layers;
	}

	XrResult re = xrEndFrame( xr_session, &fei );

	if( XR_FAILED( re ))
		vr_note( VR_SEV_ERROR, "xrEndFrame: %d %s (camadas=%u)\n", ( int )re, vr_xr_result_str( ( int )re ), ( unsigned )fei.layerCount );
	else if( vr_trace_all || vr_stereo_frames <= VR_TRACE_FRAMES )
		vr_trace( "trace: end frame %d submit=%d (views_ok=%d ativo=%d renderizado=%d) camadas=%u\n",
			vr_stereo_frames, submit ? 1 : 0, xr_views_ok.load() ? 1 : 0, vr_active.load() ? 1 : 0,
			rendered ? 1 : 0, ( unsigned )fei.layerCount );

	xr_frame_began = false;

	if( vr_perf_work_t0 > 0.0 )
	{
		vr_perf_work_ms += ( vr_now() - vr_perf_work_t0 ) * 1000.0;
		vr_perf_work_t0 = 0.0;
	}
}

// binding GLX capturado na main thread (o contexto só está ativo lá);
// a thread de init consome isso — o xrCreateInstance do WiVRn BLOQUEIA sem
// headset, por isso o init inteiro roda fora da main thread, com timeout.
static XrGraphicsBindingOpenGLXlibKHR g_glbind { XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR };
static bool g_glbind_ok = false;

static bool capture_glx( void )
{
	g_glbind.xDisplay = pglXGetCurrentDisplay();
	g_glbind.glxContext = pglXGetCurrentContext();
	g_glbind.glxDrawable = pglXGetCurrentDrawable();
	g_glbind.glxFBConfig = NULL;
	g_glbind.visualid = 0;

	// O binding XR_KHR_opengl_enable (Xlib) leva contexto + visual + fbconfig.
	// glXQueryContext devolve **0 em sucesso** (e um código de erro X caso
	// contrário): a versão anterior testava o retorno ao contrário, então o
	// visual nunca era resolvido (visualid 0 / fbconfig nulo) — binding
	// incompleto, que o runtime pode recusar ao criar a swapchain.
	int fbid = 0;
	if( g_glbind.xDisplay && g_glbind.glxContext
		&& pglXQueryContext( g_glbind.xDisplay, g_glbind.glxContext, GLX_FBCONFIG_ID, &fbid ) == 0 && fbid > 0 )
	{
		int attribs[] = { GLX_FBCONFIG_ID, fbid, None };
		int n = 0;
		GLXFBConfig *cfgs = pglXChooseFBConfig( g_glbind.xDisplay, DefaultScreen( g_glbind.xDisplay ), attribs, &n );

		if( cfgs && n > 0 )
		{
			g_glbind.glxFBConfig = cfgs[0];

			XVisualInfo *vi = pglXGetVisualFromFBConfig( g_glbind.xDisplay, cfgs[0] );
			if( vi )
			{
				g_glbind.visualid = ( uint32_t )vi->visualid;
				XFree( vi );
			}
		}
		if( cfgs )
			XFree( cfgs );
	}

	// Plano B: o visual da própria janela em que o engine está desenhando.
	// Cobre o caso em que a consulta acima não devolve nada (contexto sem
	// fbconfig associado, GLX em cima de XWayland, etc.).
	if( !g_glbind.visualid && g_glbind.xDisplay && g_glbind.glxDrawable )
	{
		XWindowAttributes wa;

		if( XGetWindowAttributes( g_glbind.xDisplay, ( Window )g_glbind.glxDrawable, &wa ) && wa.visual )
			g_glbind.visualid = ( uint32_t )wa.visual->visualid;
	}

	// idem para o fbconfig: se a consulta falhou, procura pelo visual da janela
	if( !g_glbind.glxFBConfig && g_glbind.visualid && g_glbind.xDisplay )
	{
		int attribs[] = { GLX_VISUAL_ID, ( int )g_glbind.visualid, None };
		int n = 0;
		GLXFBConfig *cfgs = pglXChooseFBConfig( g_glbind.xDisplay, DefaultScreen( g_glbind.xDisplay ), attribs, &n );

		if( cfgs && n > 0 )
			g_glbind.glxFBConfig = cfgs[0];
		if( cfgs )
			XFree( cfgs );
	}

	g_glbind_ok = g_glbind.xDisplay && g_glbind.glxContext && g_glbind.glxDrawable;
	return g_glbind_ok;
}

static void xr_init_thread( void )
{
	XrInstanceCreateInfo ici {};
	ici.type = XR_TYPE_INSTANCE_CREATE_INFO;
	strncpy( ici.applicationInfo.applicationName, "cs16-open-source", XR_MAX_APPLICATION_NAME_SIZE - 1 );
	ici.applicationInfo.apiVersion = XR_API_VERSION_1_1; // WiVRn 26.x: runtime 1.1 (1.0 → -51)
	static char exts[1][sizeof( "XR_KHR_opengl_enable" )] = { "XR_KHR_opengl_enable" };
	char *extp[1] = { exts[0] };
	ici.enabledExtensionCount = 1;
	ici.enabledExtensionNames = ( const char *const * )extp;

	XrResult r = xrCreateInstance( &ici, &xr_instance );
	if( XR_FAILED( r ))
	{
		vr_note( VR_SEV_ERROR, "xrCreateInstance: %d %s\n",
			( int )r, vr_xr_result_str( ( int )r ));
		xr_init_failed = true;
		xr_init_done = true;
		return;
	}

	XrInstanceProperties ip { XR_TYPE_INSTANCE_PROPERTIES };
	ip.type = XR_TYPE_INSTANCE_PROPERTIES;
	if( XR_SUCCEEDED( xrGetInstanceProperties( xr_instance, &ip )))
	{
		snprintf( vr_runtime, sizeof( vr_runtime ), "%s %u.%u.%u", ip.runtimeName,
			XR_VERSION_MAJOR( ip.runtimeVersion ), XR_VERSION_MINOR( ip.runtimeVersion ), XR_VERSION_PATCH( ip.runtimeVersion ));
		vr_note( VR_SEV_INFO, "runtime: %s\n", vr_runtime );
	}
	else
	{
		snprintf( vr_runtime, sizeof( vr_runtime ), "(xrGetInstanceProperties falhou)" );
		vr_note( VR_SEV_WARN, "xrGetInstanceProperties falhou\n" );
	}

	xr_init_done = true;
	xr_instance_ready = true; // a partir daqui a main thread assume o setup
}

// Erro de setup: registra uma vez por código (o setup re-tenta de 1 em 1 s, e
// repetir a mesma linha a cada segundo poluiria o painel e o log).
static void vr_setup_error( XrResult r, const char *what )
{
	if( ( int )r == xr_setup_last_err )
		return;
	xr_setup_last_err = ( int )r;

	vr_note(( r == XR_ERROR_FORM_FACTOR_UNAVAILABLE || r == XR_ERROR_SESSION_NOT_RUNNING )
		? VR_SEV_WARN : VR_SEV_ERROR,
		"%s: %d %s (tentando de novo)\n", what, ( int )r, vr_xr_result_str( ( int )r ));
}

// Setup da sessão — SEMPRE na main thread, com o contexto GLX current.
// Re-tenta de 1 em 1 s para o Mod "ligar sozinho" quando o headset conectar.
static void xr_setup_session( void )
{
	static PFN_xrGetOpenGLGraphicsRequirementsKHR get_reqs;
	XrResult r;

	if( xr_system == XR_NULL_SYSTEM_ID )
	{
		XrSystemGetInfo sgi { XR_TYPE_SYSTEM_GET_INFO };
		sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
		r = xrGetSystem( xr_instance, &sgi, &xr_system );
		if( XR_FAILED( r ))
		{
			vr_setup_error( r, "xrGetSystem(HMD)" );
			return;
		}
	}

	// Requisitos gráficos: OBRIGATÓRIO antes de xrCreateSession quando se usa
	// XR_KHR_opengl_enable. Faltando, o runtime devolve
	// -50 XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING e nenhuma sessão abre
	// (era o que travava o WiVRn; runtimes mais permissivos não reclamam).
	if( !get_reqs )
	{
		XrResult rproc = xrGetInstanceProcAddr( xr_instance, "xrGetOpenGLGraphicsRequirementsKHR",
			( PFN_xrVoidFunction * )&get_reqs );

		if( XR_FAILED( rproc ) || !get_reqs )
		{
			vr_note( VR_SEV_ERROR, "xrGetOpenGLGraphicsRequirementsKHR indisponivel (%d): runtime sem OpenGL?\n",
				( int )rproc );
			xr_setup_permanent = true;
			return;
		}
	}

	{
		XrGraphicsRequirementsOpenGLKHR reqs { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
		r = get_reqs( xr_instance, xr_system, &reqs );
		if( XR_FAILED( r ))
		{
			vr_setup_error( r, "xrGetOpenGLGraphicsRequirementsKHR" );
			return;
		}

		if( !xr_gl_reqs_logged )
		{
			unsigned req_ma = XR_VERSION_MAJOR( reqs.minApiVersionSupported );
			unsigned req_mi = XR_VERSION_MINOR( reqs.minApiVersionSupported );
			unsigned req_xa = XR_VERSION_MAJOR( reqs.maxApiVersionSupported );
			unsigned req_xi = XR_VERSION_MINOR( reqs.maxApiVersionSupported );

			xr_gl_reqs_logged = true;
			vr_note( VR_SEV_INFO, "OpenGL aceito pelo runtime: %u.%u a %u.%u; contexto do jogo: %s\n",
				req_ma, req_mi, req_xa, req_xi, vr_gl_version[0] ? vr_gl_version : "(desconhecido)" );

			if( vr_gl_major )
			{
				int mine = vr_gl_major * 1000 + vr_gl_minor;
				if( mine < ( int )( req_ma * 1000 + req_mi ) || mine > ( int )( req_xa * 1000 + req_xi ))
					vr_note( VR_SEV_WARN, "contexto OpenGL %d.%d fora da faixa suportada: pode renderizar errado\n",
						vr_gl_major, vr_gl_minor );
			}
		}
	}

	// A sessão precisa do contexto current NA THREAD QUE CHAMA (o compositor
	// do runtime faz glXMakeCurrent) — por isso este código não roda na thread
	// de init.
	if( xr_session == XR_NULL_HANDLE )
	{
		XrSessionCreateInfo sci { XR_TYPE_SESSION_CREATE_INFO };
		sci.next = &g_glbind;
		sci.systemId = xr_system;

		r = xrCreateSession( xr_instance, &sci, &xr_session );
		if( XR_FAILED( r ))
		{
			xr_session = XR_NULL_HANDLE;
			vr_setup_error( r, "xrCreateSession" );
			return;
		}
		vr_note( VR_SEV_INFO, "sessao criada (video=%s, visual %u)\n", vr_video_driver, ( unsigned )g_glbind.visualid );
	}

	if( xr_space == XR_NULL_HANDLE )
	{
		XrReferenceSpaceCreateInfo rsi { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
		rsi.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
		rsi.poseInReferenceSpace.orientation = { 0, 0, 0, 1 };

		r = xrCreateReferenceSpace( xr_session, &rsi, &xr_space );
		if( XR_FAILED( r ))
		{
			vr_setup_error( r, "xrCreateReferenceSpace" );
			return;
		}
	}

	// Action set do input (v5). PRECISA ser ligado à sessão antes do
	// xrBeginSession, que o xr_poll_events faz no estado READY — logo, aqui.
	// Falha não impede o estéreo: só o input fica inerte.
	vr_actions_init();

	vr_ready = true;
	vr_init_ms = eng ? ( eng->DoubleTime() - vr_t0 ) * 1000.0 : -1.0;
	vr_note( VR_SEV_OK, "VR pronta em %.0f ms (aguardando estado READY do headset)\n", vr_init_ms );
}

// ---------------- render targets por olho ----------------
static void bind_eye( int eye )
{
	EyeRT &rt = xr_eye[eye];

	if( rt.sc == XR_NULL_HANDLE )
		return;

	if( rt.cur < 0 )
	{
		XrSwapchainImageAcquireInfo ai { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
		XrResult ra = xrAcquireSwapchainImage( rt.sc, &ai, reinterpret_cast<uint32_t *>( &rt.cur ));
		if( XR_FAILED( ra ))
		{
			vr_note( VR_SEV_ERROR, "acquire swapchain olho %d: %d %s\n", eye, ( int )ra, vr_xr_result_str( ( int )ra ));
			rt.cur = -1;
			return;
		}
		XrSwapchainImageWaitInfo wi { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
		wi.timeout = 100000000; // 100 ms
		XrResult rw = xrWaitSwapchainImage( rt.sc, &wi );
		if( XR_FAILED( rw ))
			vr_note( VR_SEV_WARN, "wait swapchain olho %d: %d %s\n", eye, ( int )rw, vr_xr_result_str( ( int )rw ));

		pglBindFramebuffer( GL_FRAMEBUFFER, rt.fbos[rt.cur] );

		if( vr_debug_clear )
		{
			pglClearColor( eye == 0 ? 0.6f : 0.0f, 0.0f, eye == 0 ? 0.0f : 0.6f, 1.0f );
			pglClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
		}

		if( vr_trace_all || vr_stereo_frames <= VR_TRACE_FRAMES )
		{
			GLint bound = -1;
			pglGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &bound );
			vr_trace( "trace: bind olho %d -> imagem %d, fbo %u (bound=%d)\n", eye, rt.cur, rt.fbos[rt.cur], ( int )bound );
		}
	}
	else
	{
		pglBindFramebuffer( GL_FRAMEBUFFER, rt.fbos[rt.cur] );
	}
}

static void blit_mirror( void )
{
	EyeRT &rt = xr_eye[0];
	Display *dpy = pglXGetCurrentDisplay();
	GLXDrawable drawable = pglXGetCurrentDrawable();

	if( rt.sc == XR_NULL_HANDLE || rt.cur < 0 || !dpy || !drawable )
		return;

	unsigned int dw = 0, dh = 0;
	pglXQueryDrawable( dpy, drawable, GLX_WIDTH, &dw );
	pglXQueryDrawable( dpy, drawable, GLX_HEIGHT, &dh );
	if( !dw || !dh )
		return;

	GLint fbo = 0;
	pglGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &fbo );
	pglBindFramebuffer( GL_READ_FRAMEBUFFER, rt.fbos[rt.cur] );
	pglBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );
	pglBlitFramebuffer( 0, 0, rt.w, rt.h, 0, 0, dw, dh, GL_COLOR_BUFFER_BIT, GL_LINEAR );
	pglBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
	pglBindFramebuffer( GL_DRAW_FRAMEBUFFER, ( GLuint )fbo );
	pglViewport( 0, 0, dw, dh );
}

// ---------------- painel de debug (só na janela 2D) ----------------
// Chamado em Mod_Overlay2D: HUD, menus, console e placar já foram desenhados e
// o swap ainda não aconteceu — logo isto fica POR CIMA da UI do jogo. Escreve
// só no framebuffer 2D da janela; as imagens dos olhos vivem nas swapchains do
// HMD e são submetidas em Mod_EndFrame, então o headset nunca recebe o painel.
static GLuint vr_font_tex;
static int    vr_font_tex_state; // 0 = ainda não tentou, 1 = criada, -1 = falhou

static const int VR_PANEL_COLS = 78; // largura nominal do painel, em caracteres

static const float COL_BG[4]    = { 0.031f, 0.055f, 0.086f, 0.87f };
static const float COL_LINE[4]  = { 0.26f, 0.36f, 0.47f, 1.00f };
static const float COL_HDR[4]   = { 0.95f, 0.97f, 1.00f, 1.00f };
static const float COL_DIM[4]   = { 0.45f, 0.54f, 0.64f, 1.00f };
static const float COL_INFO[4]  = { 0.72f, 0.79f, 0.87f, 1.00f };
static const float COL_OK[4]    = { 0.31f, 0.86f, 0.55f, 1.00f };
static const float COL_WARN[4]  = { 0.96f, 0.65f, 0.14f, 1.00f };
static const float COL_ERR[4]   = { 1.00f, 0.36f, 0.36f, 1.00f };

static const float *vr_sev_color( int sev )
{
	switch( sev )
	{
	case VR_SEV_OK:    return COL_OK;
	case VR_SEV_WARN:  return COL_WARN;
	case VR_SEV_ERROR: return COL_ERR;
	default:           return COL_INFO;
	}
}

// textura 1:1 do atlas (GL_ALPHA: 0 = transparente), sem filtragem
static bool panel_font_texture( void )
{
	if( vr_font_tex_state == 1 )
		return true;

	vrglPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	vrglGenTextures( 1, &vr_font_tex );
	if( !vr_font_tex )
	{
		vr_font_tex_state = -1;
		return false;
	}

	vrglBindTexture( GL_TEXTURE_2D, vr_font_tex );
	vrglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	vrglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	vrglTexImage2D( GL_TEXTURE_2D, 0, GL_ALPHA, VR_FONT_ATLAS_W, VR_FONT_ATLAS_H, 0,
		GL_ALPHA, GL_UNSIGNED_BYTE, vr_font_atlas );

	vr_font_tex_state = 1;
	return true;
}

static void panel_rect( float x0, float y0, float x1, float y1, const float c[4] )
{
	vrglDisable( GL_TEXTURE_2D );
	vrglColor4f( c[0], c[1], c[2], c[3] );
	vrglBegin( GL_QUADS );
	vrglVertex2f( x0, y0 );
	vrglVertex2f( x1, y0 );
	vrglVertex2f( x1, y1 );
	vrglVertex2f( x0, y1 );
	vrglEnd();
}

// uma linha de texto; para no máximo `max_chars` caracteres (0 = tudo), para
// nada escapar do fundo do painel. Retorna a largura desenhada (px).
static float panel_text( float x, float y, float sc, const char *s, const float c[4], int max_chars )
{
	const float cw = VR_FONT_W * sc, chh = VR_FONT_H * sc;
	const float u = 1.0f / VR_FONT_COLS, v = 1.0f / VR_FONT_ROWS;
	float x0 = x;

	if( !panel_font_texture())
		return 0.0f;

	vrglEnable( GL_TEXTURE_2D );
	vrglBindTexture( GL_TEXTURE_2D, vr_font_tex );
	vrglColor4f( c[0], c[1], c[2], c[3] );

	vrglBegin( GL_QUADS );
	for( const char *p = s; *p; p++ )
	{
		int ch = ( unsigned char )*p;

		if( max_chars > 0 && ( int )( p - s ) >= max_chars )
			break;

		if( ch < VR_FONT_FIRST || ch > VR_FONT_LAST )
		{
			x += cw;
			continue;
		}

		int idx = ch - VR_FONT_FIRST;
		float u0 = ( idx % VR_FONT_COLS ) * u;
		float v0 = ( idx / VR_FONT_COLS ) * v;
		float u1 = u0 + u, v1 = v0 + v;

		vrglTexCoord2f( u0, v0 ); vrglVertex2f( x, y );
		vrglTexCoord2f( u1, v0 ); vrglVertex2f( x + cw, y );
		vrglTexCoord2f( u1, v1 ); vrglVertex2f( x + cw, y + chh );
		vrglTexCoord2f( u0, v1 ); vrglVertex2f( x, y + chh );
		x += cw;
	}
	vrglEnd();
	vrglDisable( GL_TEXTURE_2D );

	return x - x0;
}

// Dump único do framebuffer 2D (PPM, sem dependência de encoder): serve para
// levar o estado do painel junto de um relato de problema quando a máquina não
// tem como capturar a janela (Wayland sem portal, por exemplo).
static void panel_dump( int W, int H )
{
	FILE *f = fopen( vr_panel_dump_path, "wb" );

	vr_panel_dump_done = true;

	if( !f )
	{
		vr_note( VR_SEV_WARN, "painel: nao consegui abrir %s para dump\n", vr_panel_dump_path );
		return;
	}

	std::vector<unsigned char> px( ( size_t )W * H * 4 );
	// lê sempre da JANELA (FBO 0): é o alvo do painel, e ler de outro FBO daria
	// um recorte do tamanho errado quando o VR está ativo
	pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
	vrglPixelStorei( GL_PACK_ALIGNMENT, 1 );
	vrglReadPixels( 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());

	fprintf( f, "P6\n%d %d\n255\n", W, H );
	for( int y = H - 1; y >= 0; y-- ) // OpenGL lê com origem embaixo; PPM é de cima
	{
		for( int x = 0; x < W; x++ )
		{
			const unsigned char *p = &px[(( size_t )y * W + x) * 4];
			fputc( p[0], f );
			fputc( p[1], f );
			fputc( p[2], f );
		}
	}
	fclose( f );

	vr_note( VR_SEV_OK, "painel: dump %dx%d salvo em %s\n", W, H, vr_panel_dump_path );
}

// Dump do PAR estéreo: grava as duas imagens dos olhos do MESMO quadro, para
// conferir fora do headset se o par casa (mesma escala e disparidade pequena e
// coerente entre os olhos). "Cada olho sozinho parece bom, os dois juntos não
// fundem" é um sintoma que só se mede comparando as duas imagens — com o
// headset na cara não dá para separar erro de geometria de desconforto.
static void stereo_dump( void )
{
	std::vector<unsigned char> px;
	GLint prev = 0;

	pglGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &prev );
	vr_stereo_dump_n++;

	for( int e = 0; e < 2; e++ )
	{
		char path[320];
		FILE *f;

		if( xr_eye[e].fbos[0] == 0 || xr_eye[e].w <= 0 || xr_eye[e].h <= 0 )
			continue;

		snprintf( path, sizeof( path ), "%s-%d-eye%d.ppm", vr_stereo_dump_prefix, vr_stereo_dump_n, e );
		f = fopen( path, "wb" );
		if( !f )
		{
			vr_note( VR_SEV_WARN, "estereo: nao consegui abrir %s para dump\n", path );
			continue;
		}

		px.resize(( size_t )xr_eye[e].w * xr_eye[e].h * 4 );
		pglBindFramebuffer( GL_FRAMEBUFFER, xr_eye[e].fbos[0] );
		vrglPixelStorei( GL_PACK_ALIGNMENT, 1 );
		vrglReadPixels( 0, 0, xr_eye[e].w, xr_eye[e].h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());

		fprintf( f, "P6\n%d %d\n255\n", xr_eye[e].w, xr_eye[e].h );
		for( int y = xr_eye[e].h - 1; y >= 0; y-- ) // GL lê de baixo; PPM de cima
		{
			for( int x = 0; x < xr_eye[e].w; x++ )
			{
				const unsigned char *p = &px[(( size_t )y * xr_eye[e].w + x) * 4];
				fputc( p[0], f );
				fputc( p[1], f );
				fputc( p[2], f );
			}
		}
		fclose( f );

		vr_note( VR_SEV_OK, "estereo: dump olho %d %dx%d -> %s\n", e, xr_eye[e].w, xr_eye[e].h, path );
	}

	pglBindFramebuffer( GL_FRAMEBUFFER, ( GLuint )prev );
	vr_gl_check( "dump estereo" );
}

static void panel_draw( void )
{
	if( !vr_panel || !vr_panel_gl_ok )
		return;

	// espaço de coordenadas = o mesmo em que o engine acabou de desenhar o 2D
	// (R_Set2DMode fixa o viewport no alvo 2D), então o painel fica correto
	// com ou sem render target interno do ref_gl
	GLint vp[4] = { 0, 0, 0, 0 };
	pglGetIntegerv( GL_VIEWPORT, vp );

	int W = vp[2], H = vp[3];
	if( W <= 0 || H <= 0 )
	{
		Display *dpy = pglXGetCurrentDisplay();
		GLXDrawable dr = pglXGetCurrentDrawable();
		unsigned int dw = 0, dh = 0;

		if( !dpy || !dr )
			return;
		pglXQueryDrawable( dpy, dr, GLX_WIDTH, &dw );
		pglXQueryDrawable( dpy, dr, GLX_HEIGHT, &dh );
		W = ( int )dw;
		H = ( int )dh;
		if( W <= 0 || H <= 0 )
			return;
	}

	int sc = vr_panel_scale;
	if( 24 + VR_PANEL_COLS * VR_FONT_W * sc > W ) // não cabe: reduz a fonte
		sc = ( W - 24 ) / ( VR_PANEL_COLS * VR_FONT_W );
	if( sc < 1 ) sc = 1;

	// O alvo tem que ser a JANELA, explícito: o blit do espelho (Mod_AfterEyes)
	// salva e RESTAURA o FBO que estava ligado — que no VR ativo é o do último
	// olho. Sem este bind o painel era desenhado dentro da imagem do olho e
	// aparecia no HEADSET, que é exatamente o contrário do que ele serve (o
	// headset não tem como mostrar o log de inicialização, e a imagem do olho
	// ainda ia com o painel colado nela).
	GLint prev_draw = 0, prev_read = 0;
	pglGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw );
	pglGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &prev_read );
	pglBindFramebuffer( GL_FRAMEBUFFER, 0 ); // desenha E lê da janela

	const float cw = VR_FONT_W * sc;
	const float chh = VR_FONT_H * sc;
	const float x = 12.0f;
	float y = 10.0f;

	// cabeçalho fixo + rodapé; o resto do painel é a lista de mensagens
	// (16 = inclui as 2 linhas dos menus no VR, Fase 4)
	const int fixas = 16;
	int max_msgs = ( int )(( H - 20 ) / chh ) - fixas;
	int omitidas = 0;
	int first_msg = 0;

	if( max_msgs < 1 ) max_msgs = 1;
	if( vr_msg_view_count > max_msgs )
	{
		omitidas = vr_msg_view_count - ( max_msgs - 1 );
		first_msg = omitidas;
		max_msgs -= 1; // linha "-- N anteriores omitidas --"
	}

	const int n_msgs = vr_msg_view_count - first_msg;
	const int n_linhas = fixas + ( vr_msg_view_count > 0 ? n_msgs + ( omitidas ? 1 : 0 ) : 1 );

	// fundo do painel (cobre só o que vai ser escrito)
	vrglPushAttrib( GL_ALL_ATTRIB_BITS );

	{
		static bool logged;
		GLint vp[4] = {0};

		pglGetIntegerv( GL_VIEWPORT, vp );

		if( !logged )
		{
			GLint box[4] = {0};
			logged = true;
			pglGetIntegerv( GL_SCISSOR_BOX, box );
			vr_note( VR_SEV_INFO, "painel: GL do engine (viewport %d %d %d %d, scissor %d %d %d %d, FBO %d) - scissor off, desenho na janela (FBO 0)\n",
				vp[0], vp[1], vp[2], vp[3], box[0], box[1], box[2], box[3], ( int )prev_draw );
		}

		// O 2D tem que chegar com a JANELA ligada (FBO 0): com r_scene_scale 1 e
		// msaa 0 não existe render target interno, e o binding herdado pode ser
		// o do último olho — foi assim que o painel (e o HUD do jogo) apareciam
		// no headset. Se isto aparecer no log, o alvo do 2D voltou a se perder.
		if( prev_draw != 0 )
		{
			static double last_nz;
			double t = vr_now();

			if( t - last_nz > 5.0 )
			{
				last_nz = t;
				vr_note( VR_SEV_WARN, "painel: o 2D chegou com FBO %d ligado (esperado 0 = janela com r_scene_scale 1/msaa 0)\n",
					( int )prev_draw );
			}
		}
	}

	vrglMatrixMode( GL_PROJECTION );
	vrglPushMatrix();
	vrglLoadIdentity();
	vrglOrtho( 0.0, ( GLdouble )W, ( GLdouble )H, 0.0, -1.0, 1.0 ); // y para baixo
	vrglMatrixMode( GL_MODELVIEW );
	vrglPushMatrix();
	vrglLoadIdentity();

	vrglDisable( GL_DEPTH_TEST );
	vrglDisable( GL_CULL_FACE );
	vrglDisable( GL_ALPHA_TEST );
	vrglDisable( GL_FOG );
	// o engine deixa scissor ligado do desenho 2D dele; sem desligar, o painel
	// é recortado para a área do HUD e some do framebuffer
	vrglDisable( GL_SCISSOR_TEST );
	vrglEnable( GL_BLEND );
	vrglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	{
		float bg_h = y + n_linhas * chh + 20.0f;
		float bg_w = x + VR_PANEL_COLS * cw + 12.0f;
		panel_rect( 6.0f, 6.0f, bg_w, bg_h, COL_BG );
	}

	// --- cabeçalho ---
	const char *estado =
		xr_init_failed.load()  ? "INATIVO (sem runtime)" :
		xr_setup_permanent     ? "INATIVO (setup falhou)" :
		vr_active.load()       ? "ATIVO (estereo)" :
		vr_ready.load()        ? "PRONTO (aguardando headset)" :
		xr_init_started.load() ? "INICIANDO..." : "DESLIGADO";

	char buf[512];
	const int COLS = VR_PANEL_COLS;

	panel_text( x, y, ( float )sc, "MOD VR - PAINEL DE DEBUG", COL_HDR, 40 );
	{
		// rodapé do cabeçalho alinhado à direita, dentro da largura do painel
		const char *nota = "so na tela 2D (VR_DEBUG_PANEL=0 esconde)";
		int n = ( int )strlen( nota );
		if( n > COLS - 26 )
			n = COLS - 26;
		panel_text( x + ( COLS - n ) * cw, y, ( float )sc, nota, COL_DIM, n );
	}
	y += chh;

	panel_rect( x, y + chh * 0.5f, x + COLS * cw, y + chh * 0.5f + ( sc > 1 ? 2.0f : 1.0f ), COL_LINE );
	y += chh;

	snprintf( buf, sizeof( buf ), "FPS %6.1f | frame %5.1f ms | frames %lu",
		( double )vr_fps, ( double )vr_frame_ms, vr_frames );

	if( vr_eye_count == 2 )
		snprintf( buf + strlen( buf ), sizeof( buf ) - strlen( buf ),
			" | olhos 2 (%dx%d, %d img)", vr_eye_w, vr_eye_h, vr_eye_images );

	panel_text( x, y, ( float )sc, buf, COL_INFO, COLS );
	y += chh;

	snprintf( buf, sizeof( buf ), "VR  %s | poses %s | sessao %s",
		estado, xr_views_ok.load() ? "ok" : "sem poses",
		vr_session_state.load() < 0 ? "-" : vr_session_state_str( vr_session_state.load()));
	panel_text( x, y, ( float )sc, buf, xr_init_failed.load() ? COL_WARN : COL_INFO, COLS );
	y += chh;

	snprintf( buf, sizeof( buf ), "INI runtime=%s | video=%s | init %s%.0f ms",
		vr_runtime[0] ? vr_runtime : "n/a", vr_video_driver[0] ? vr_video_driver : "?",
		vr_init_ms < 0.0 ? "(em curso) " : "", vr_init_ms < 0.0 ? 0.0 : vr_init_ms );
	panel_text( x, y, ( float )sc, buf, COL_INFO, COLS );
	y += chh;

	// estéreo: o que o runtime manda por olho e o que o render usa. É a linha
	// que denuncia IPD errado ou olho com fov espelhada sem tirar o headset.
	{
		const double ipd_mm = xr_views_ok.load()
			? sqrt( pow( ( double )xr_views[1].pose.position.x - xr_views[0].pose.position.x, 2 )
			      + pow( ( double )xr_views[1].pose.position.y - xr_views[0].pose.position.y, 2 )
			      + pow( ( double )xr_views[1].pose.position.z - xr_views[0].pose.position.z, 2 )) * 1000.0
			: 0.0;

		if( ipd_mm > 0.1 )
		{
			const XrFovf &f0 = xr_views[0].fov;
			const XrFovf &f1 = xr_views[1].fov;

			snprintf( buf, sizeof( buf ), "ESTEREO ipd %.1f mm (%s) | fov esq L%.0f R%.0f dir L%.0f R%.0f",
				ipd_mm, vr_ipd_base_src,
				rad2deg( -f0.angleLeft ), rad2deg( f0.angleRight ),
				rad2deg( -f1.angleLeft ), rad2deg( f1.angleRight ));
			panel_text( x, y, ( float )sc, buf, COL_INFO, COLS );
		}
		else
		{
			panel_text( x, y, ( float )sc, "ESTEREO sem poses por olho (runtime nao expos)", COL_DIM, COLS );
		}
		y += chh;
	}

	// ajuste do IPD: a linha de estado é permanente (é o número que se lê para
	// calibrar); a linha de instruções só aparece com o modo ligado, e aí em
	// âmbar — com o modo ligado o teclado pertence a este bloco, não ao jogo.
	{
		vr_ipd_state( buf, sizeof( buf ));
		panel_text( x, y, ( float )sc, buf, vr_ipd_delta != 0.0f ? COL_OK : COL_INFO, COLS );
		y += chh;

		if( vr_ipd_adj_on )
		{
			panel_text( x, y, ( float )sc,
				"  AJUSTE LIGADO: - aprox, = afasta (1.0 mm) | , . fino (0.1 mm) | 0 zero | F9",
				COL_WARN, COLS );
			y += chh;
		}
		else
		{
			panel_text( x, y, ( float )sc,
				"  F9 liga o ajuste fino do IPD pelo teclado (valor tambem no headset)",
				COL_DIM, COLS );
			y += chh;
		}
	}

	// input VR (Fase 3): o perfil de interação é o que responde "o controle
	// casou?" — se vier "(nenhum)", nenhum binding sugerido pegou e não há
	// locomoção, mesmo com o estéreo perfeito.
	{
		snprintf( buf, sizeof( buf ), "INPUT %s | perfil %s / %s",
			!in_valid ? ( xr_actionset == XR_NULL_HANDLE ? "sem action set" : "inativo" ) : "ok",
			short_profile( in_profile[0] ), short_profile( in_profile[1] ));
		panel_text( x, y, ( float )sc, buf, in_valid ? COL_INFO : COL_WARN, COLS );
		y += chh;

		snprintf( buf, sizeof( buf ), "  move %+.2f %+.2f | turn %+.2f | snap %.0f yaw %+.0f | botoes 0x%03X",
			( double )in_move[0], ( double )in_move[1], ( double )in_turn[0],
			( double )vr_snap_angle, ( double )vr_yaw_offset, vr_buttons_prev );
		panel_text( x, y, ( float )sc, buf, COL_INFO, COLS );
		y += chh;

		snprintf( buf, sizeof( buf ), "  agacha %s | roomscale %s | maos %s %s",
			vr_duck_phys ? "fisico" : ( in_crouch_btn ? "botao" : "-" ),
			vr_roomscale ? "on" : "off",
			in_hand_ok[0] ? "L" : "--", in_hand_ok[1] ? "R" : "--" );
		panel_text( x, y, ( float )sc, buf, COL_INFO, COLS );
		y += chh;

		// arma na mão (Fase 6): é esta linha que permite calibrar o encaixe sem
		// adivinhar (classe do modelo + offsets em uso, de mods/vr_weapon.cfg)
		{
			const float *tw = vr_wpn_active_offset();

			snprintf( buf, sizeof( buf ), "  arma %s | mira %s | encaixe %+.0f %+.0f %+.0f %s",
				vr_wpn_model, vr_aim_source_name(),
				( double )tw[0], ( double )tw[1], ( double )tw[2],
				vr_wpn.has_cls[vr_wpn_class] ? "(classe)" : "(global)" );
			panel_text( x, y, ( float )sc, buf, COL_INFO, COLS );
			y += chh;
		}

		// menus no VR (Fase 4): é por estas duas linhas que se confirma, sem
		// tirar o headset, que o painel está sendo desenhado e para onde o
		// laser aponta — e que o clique foi enviado
		{
			snprintf( buf, sizeof( buf ), "  menu %s | canvas tx %u %dx%d | ponteiro %s (%.0f, %.0f) | cliques %u",
				vr_menu_open ? "ABERTO" : "fechado", vr_menu_canvas_tex, vr_menu_canvas_w,
				vr_menu_canvas_h, vr_menu_hit ? "NO PAINEL" : "fora",
				( double )vr_menu_ptr_x, ( double )vr_menu_ptr_y, vr_menu_clicks );
			panel_text( x, y, ( float )sc, buf, vr_menu_open ? COL_OK : COL_DIM, COLS );
			y += chh;

			snprintf( buf, sizeof( buf ), "  painel dist %.2f m altura %.2f m (%s) | ancora %s | laser %s",
				( double )vr_menu_dist_m, ( double )vr_menu_height_m,
				vr_menu ? "on" : "OFF", vr_menu_anchor_ok ? "ok" : "pendente",
				vr_menu_laser ? "on" : "off" );
			panel_text( x, y, ( float )sc, buf, COL_DIM, COLS );
			y += chh;
		}
	}

	{
		char hb[160];
		vr_hands_status( hb, sizeof( hb ));
		panel_text( x, y, ( float )sc, hb, vr_hands_ready() ? COL_OK : COL_DIM, COLS );
		y += chh;
	}

	panel_rect( x, y + chh * 0.5f, x + COLS * cw, y + chh * 0.5f + ( sc > 1 ? 2.0f : 1.0f ), COL_LINE );
	y += chh;

	snprintf( buf, sizeof( buf ), "ERROS E AVISOS DO CARREGAMENTO  (%d erro(s), %d aviso(s), %lu evento(s))",
		vr_msg_view_errors, vr_msg_view_warns, vr_msg_view_total );
	panel_text( x, y, ( float )sc, buf, vr_msg_view_errors ? COL_ERR : COL_HDR, COLS );
	y += chh;

	// --- mensagens (mais antiga primeiro; as que não cabem saem do topo) ---
	if( vr_msg_view_count == 0 )
	{
		panel_text( x, y, ( float )sc, "nenhum erro ou aviso registrado ate agora", COL_DIM, COLS );
		y += chh;
	}
	else
	{
		if( omitidas > 0 )
		{
			snprintf( buf, sizeof( buf ), "-- %d mensagem(ns) anterior(es) omitida(s) (ver mods/mod_vr.log) --", omitidas );
			panel_text( x, y, ( float )sc, buf, COL_DIM, COLS );
			y += chh;
		}

		for( int i = first_msg; i < vr_msg_view_count; i++ )
		{
			const vr_msg_t &m = vr_msg_view[i];
			char pre[32];

			// colunas: 0..9 = "t    12.3s ", 10..12 = severidade, 13.. = texto
			snprintf( pre, sizeof( pre ), "t%7.1fs ", m.t );
			panel_text( x, y, ( float )sc, pre, COL_DIM, 10 );
			panel_text( x + 10 * cw, y, ( float )sc, vr_sev_tag( m.sev ), vr_sev_color( m.sev ), 3 );
			panel_text( x + 13 * cw, y, ( float )sc, m.text, vr_sev_color( m.sev ), COLS - 13 );
			y += chh;
		}
	}

	panel_rect( x, y + chh * 0.5f, x + COLS * cw, y + chh * 0.5f + ( sc > 1 ? 2.0f : 1.0f ), COL_LINE );
	y += chh;

	panel_text( x, y, ( float )sc, "log completo: mods/mod_vr.log", COL_DIM, COLS );

	// dump de diagnóstico (uma vez, quando o 2D já tem conteúdo; VR_PANEL_DUMP_AT
	// muda o atraso em segundos — útil para capturar já dentro de um mapa)
	if( vr_panel_dump_path[0] && !vr_panel_dump_done && eng && ( eng->DoubleTime() - vr_t0 ) >= vr_panel_dump_at )
		panel_dump( W, H );

	vrglMatrixMode( GL_MODELVIEW );
	vrglPopMatrix();
	vrglMatrixMode( GL_PROJECTION );
	vrglPopMatrix();
	vrglPopAttrib();

	// devolve os alvos que estavam ligados (o engine segue daqui para o R_EndFrame);
	// read e draw separados, como faz o blit do espelho — não deixamos o read
	// apontando para a janela por conta própria
	pglBindFramebuffer( GL_DRAW_FRAMEBUFFER, ( GLuint )prev_draw );
	pglBindFramebuffer( GL_READ_FRAMEBUFFER, ( GLuint )prev_read );
}

// Valor do ajuste DENTRO do headset. O painel de debug vive só na janela 2D, e
// quem calibra estéreo está com o HMD na cabeça: sem isto, o número só seria
// legível tirando o headset (e, na prática, se perderia a referência de qual
// tecla produziu qual valor). O texto vai IDÊNTICO nos dois olhos — disparidade
// zero, que funde num ponto só — e é desenhado na imagem do olho ANTES do
// release, com o mesmo padrão de estado do vr_draw_vignette.
static void vr_draw_ipd_hud( int eye )
{
	char linhas[3][192];
	int W, H, sc, n = 0;
	float cw, chh, x, y, wmax = 0.0f;
	GLint prev_draw = 0, prev_read = 0;

	if( !vr_ipd_hud || !vr_ipd_adj_on )
		return;

	if( eye < 0 || eye > 1 || xr_eye[eye].cur < 0 || xr_eye[eye].fbos.empty() || !vr_panel_gl_ok )
		return;

	W = xr_eye[eye].w;
	H = xr_eye[eye].h;
	if( W <= 0 || H <= 0 )
		return;

	// a fonte é 8x16: a escala sai da altura do olho (~1/32 dela por linha)
	sc = H / ( 32 * VR_FONT_H );
	if( sc < 1 ) sc = 1;

	cw = VR_FONT_W * sc;
	chh = VR_FONT_H * sc;

	snprintf( linhas[n++], sizeof( linhas[0] ), "AJUSTE DO IPD (F9 sai)" );
	vr_ipd_state( linhas[n++], sizeof( linhas[0] ));
	snprintf( linhas[n++], sizeof( linhas[0] ), "- aprox / = afasta  1.0 mm    , . fino  0.1 mm    0 zera" );

	for( int i = 0; i < n; i++ )
	{
		float w = ( float )strlen( linhas[i] ) * cw;

		if( w > wmax )
			wmax = w;
	}

	x = ( W - wmax ) * 0.5f;
	y = H * 0.06f;

	// fundo: texto claro sobre cena clara (céu, parede branca) não se lê
	pglGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw );
	pglGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &prev_read );
	pglBindFramebuffer( GL_FRAMEBUFFER, xr_eye[eye].fbos[xr_eye[eye].cur] );

	vrglPushAttrib( GL_ALL_ATTRIB_BITS );
	vrglMatrixMode( GL_PROJECTION ); vrglPushMatrix(); vrglLoadIdentity();
	vrglOrtho( 0.0, ( GLdouble )W, ( GLdouble )H, 0.0, -1.0, 1.0 ); // y para baixo
	vrglMatrixMode( GL_MODELVIEW ); vrglPushMatrix(); vrglLoadIdentity();
	vrglDisable( GL_DEPTH_TEST );
	vrglDisable( GL_CULL_FACE );
	vrglDisable( GL_ALPHA_TEST );
	vrglDisable( GL_FOG );
	vrglDisable( GL_SCISSOR_TEST );
	vrglEnable( GL_BLEND );
	vrglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	panel_rect( x - 14.0f, y - 8.0f, x + wmax + 14.0f, y + n * chh + 8.0f, COL_BG );

	for( int i = 0; i < n; i++ )
	{
		panel_text( x, y, ( float )sc, linhas[i], i == 0 ? COL_WARN : COL_HDR, 0 );
		y += chh;
	}

	vrglMatrixMode( GL_PROJECTION ); vrglPopMatrix();
	vrglMatrixMode( GL_MODELVIEW ); vrglPopMatrix();
	vrglPopAttrib();

	pglBindFramebuffer( GL_DRAW_FRAMEBUFFER, ( GLuint )prev_draw );
	pglBindFramebuffer( GL_READ_FRAMEBUFFER, ( GLuint )prev_read );
}

static void VR_Overlay2D( void )
{
	double t0 = vr_now();

	panel_draw();
	vr_perf_panel_ms += ( vr_now() - t0 ) * 1000.0;

	// dump de diagnóstico: independente de o painel estar VISÍVEL, senão não
	// haveria como capturar a tela limpa (VR_DEBUG_PANEL=0) — e é justamente aí
	// que se enxerga o que o painel tapava.
	if( vr_panel_dump_path[0] && !vr_panel_dump_done && eng && ( eng->DoubleTime() - vr_t0 ) >= vr_panel_dump_at )
	{
		GLint vp[4] = { 0, 0, 0, 0 };

		pglGetIntegerv( GL_VIEWPORT, vp );

		if( vp[2] > 0 && vp[3] > 0 )
			panel_dump( vp[2], vp[3] );
	}
}// ---------------- hooks da ABI ----------------
// ---------------- viewmodel proprio (bracos + arma) ----------------
// O subsistema vive em vr_hands.cpp e nao depende da ABI: recebe o que o mod ja
// sabe (camera por olho) e desenha. Aqui so ligamos/desligamos e damos o log.
static bool vr_hands_test;   // VR_HANDS_TEST=1: modelo parado na frente da camera

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
	vr_hands_init( vr_hands_log_adapter );

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
	std::thread( xr_init_thread ).detach();

	// espera curta pela INSTÂNCIA (é o xrCreateInstance do WiVRn que bloqueia
	// sem headset). A sessão é criada depois, na main thread, de 1 em 1 s.
	for( int i = 0; i < 60 && !xr_init_done; i++ )
		std::this_thread::sleep_for( std::chrono::milliseconds( 100 ));

	if( !xr_init_done )
	{
		vr_note( VR_SEV_WARN, "runtime demorando (headset off?) - flat; VR liga ao conectar\n" );
		return 1;
	}

	if( xr_init_failed )
	{
		vr_init_ms = ( e->DoubleTime() - vr_t0 ) * 1000.0;
		vr_note( VR_SEV_WARN, "sem runtime OpenXR (%.0f ms): jogo segue flat\n", vr_init_ms );
		return 1;
	}

	vr_note( VR_SEV_OK, "instancia OpenXR criada em %.0f ms; montando a sessao na main thread\n",
		( e->DoubleTime() - vr_t0 ) * 1000.0 );

	return 1;
}

static void VR_Shutdown( void )
{
	char ipd[192];

	// o valor do IPD sai SEMPRE, inclusive quando o VR nem ligou: é o número que
	// o dono precisa ler depois de calibrar (o painel 2D já não existe aqui)
	vr_ipd_state( ipd, sizeof( ipd ));
	vr_note( VR_SEV_INFO, "shutdown: %s\n", ipd );

	vr_note( VR_SEV_INFO, "shutdown (frames=%lu, fps medio final %.1f)\n", vr_frames, vr_fps );
	if( !xr_init_done )
	{
		if( xr_init_started )
			vr_note( VR_SEV_INFO, "init ainda em andamento - abortando sem destruir sessao\n" );
		else
			vr_note( VR_SEV_INFO, "init do OpenXR nunca comecou (mod inativo)\n" );
		return;
	}
	if( xr_session != XR_NULL_HANDLE )
	{
		// xrEndSession só é aceito no estado STOPPING (o WiVRn/Monado devolve
		// XR_ERROR_SESSION_NOT_STOPPING fora dele); fora disso o runtime
		// derruba a sessão sozinho quando o processo cliente desaparece.
		if( vr_session_state.load() == XR_SESSION_STATE_STOPPING )
		{
			xr_end_frame();
			xrEndSession( xr_session );
		}
		else
		{
			vr_note( VR_SEV_INFO, "encerrando sem xrEndSession (estado %s): o runtime limpa a sessao\n",
				vr_session_state.load() < 0 ? "-" : vr_session_state_str( vr_session_state.load()));
		}
	}
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

	vr_msgs_refresh(); // instantâneo do log p/ o painel deste frame
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
	FILE *f = fopen( "mods/vr_menu.cfg", "r" );

	if( f )
	{
		fclose( f );
		return;
	}

	f = fopen( "mods/vr_menu.cfg", "w" );

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
	FILE *f = fopen( "mods/vr_menu.cfg", "r" );

	if( stat( "mods/vr_menu.cfg", &st ) == 0 )
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

	if( stat( "mods/vr_menu.cfg", &st ) != 0 )
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
	if( vr_hands_ready() && vr_hands_covers_model( vm->model_name ))
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

	// classe do modelo atual: alimenta o painel e o encaixe por classe. Só o
	// primeiro olho escreve (senão o log sairia em dobro por frame).
	if( vm->eye == 0 && vm->model_name[0] )
	{
		int cls = vr_wpn_class_id( vm->model_name );

		if( cls != vr_wpn_class || strcmp( vr_wpn_model, vm->model_name ))
		{
			vr_wpn_class = cls;
			snprintf( vr_wpn_model, sizeof( vr_wpn_model ), "%s", vm->model_name );
			vr_note( VR_SEV_INFO, "arma: modelo %s (classe %s) | mira %s\n",
				vr_wpn_model, vr_wpn_class_names[cls], vr_aim_source_name());
		}
	}

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
		if( vr_hands_ready() && vr_hands_covers_model( vr_wpn_model ))
		{
			vr_hands_hand_t hands[2];
			vr_hands_fill( hands );
			for( int e = 0; e < 2; e++ )
			{
				if( xr_eye[e].fbos[0] == 0 || xr_eye[e].cur < 0 || !vr_eye_cam[e].ok )
					continue;

				GLint prev = 0;
				pglGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &prev );
				pglBindFramebuffer( GL_FRAMEBUFFER, xr_eye[e].fbos[xr_eye[e].cur] );

				vr_hands_cam_t hc;
				vr_hands_cam( &hc, vr_eye_cam[e] );
				vr_hands_draw( &hc, hands );

				pglBindFramebuffer( GL_FRAMEBUFFER, ( GLuint )prev );
			}
			vr_gl_check( "viewmodel proprio" );
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
				vr_hands_draw( &hc, NULL ); // poses sintéticas (diagnóstico)

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

		// diagnóstico sem headset: o MESMO modelo, agora na janela (FBO 0), por
		// cima do mundo e antes do HUD 2D — é o que o dump do painel captura.
		if( vr_hands_test && vr_hands_ready() && vr_eye_cam[0].ok )
		{
			vr_hands_cam_t hc;
			vr_hands_cam( &hc, vr_eye_cam[0] );
			vr_hands_draw( &hc, NULL ); // poses sintéticas: prova a IK sem headset
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
};

extern "C" mod_api_t *ModGetAPI( void )
{
	return &g_api;
}
