/*
mod_api.h — contrato público entre o engine e os Mods externos (v1).

Um "Mod" aqui é uma biblioteca compartilhada carregada com `-mod <nome>`,
NÃO é um game dir e NÃO tem relação com os carregadores de modelos
(mod_alias.c, mod_bmodel.c etc.). Um Mod exporta um único símbolo:

    const mod_api_t *ModGetAPI( void );

O engine resolve os hooks declarados em `capabilities`; hooks ausentes ou
capability não declarada simplesmente não são chamados. Campos futuros
devem ser adicionados SEMPRE no fim da struct, com incremento de
MOD_API_VERSION. Ver docs/MODS.md (workspace) para o guia completo.

Este header é autocontido: não depende de nenhum header do engine, e é
consumido tanto pelo engine quanto pelos fontes dos Mods (mods/).
*/
#ifndef MOD_API_H
#define MOD_API_H

#include "csmod_services.h"

#define MOD_API_VERSION 9 // v2 (Fase 2): Mod_EyeCount, Mod_AfterEyes, mod_view_t.viewport
                          // v3: Mod_Overlay2D (2D pronto, antes do swap)
                          // v4: mod_view_t.fov_asym/has_fov_asym (frustum por olho fora de centro)
                          // v5 (Fase 3): mod_input_sample_t.move_up/viewangles_abs/has_viewangles_abs
                          //     — e o engine passou a APLICAR a amostra (antes só coletava)
                          // v6 (Fase 6): mod_viewmodel_t + Mod_ViewModel — o Mod leva a arma
                          //     para a mão (sobrescreve origem/ângulos do viewmodel) e escolhe
                          //     o depth (ocludida pelo mundo) ou escondê-la
                          // v7: Mod_Key — o Mod vê a tecla antes de qualquer handler do engine
                          //     e pode CONSUMIR (UI de calibração por teclado)
                          // v8 (Fase 4): mod_hud_t + Mod_Hud — o engine entrega os fatos do
                          //     frame (menu aberto, canvas 2D, tamanho) e o Mod decide: quer o
                          //     2D num canvas offscreen e qual é a posição/clique do CURSOR do
                          //     menu (ponteiro laser). Substitui Mod_HudRedirect/Mod_HudPointer,
                          //     que nunca foram fiados (eram só declaração).

// capabilities — o que o Mod declara que quer receber
#define MOD_CAP_DISPLAY  (1u<<0)  // camera override por olho + begin/end frame (render)
#define MOD_CAP_INPUT    (1u<<1)  // injeta input por frame (movimento/botões/ângulos)
#define MOD_CAP_HUD      (1u<<2)  // notificações do pipeline 2D (HUD/menus)
#define MOD_CAP_KEYS     (1u<<3)  // v7: recebe os eventos de teclado e pode consumi-los

// keynums do engine entregues em Mod_Key (v7). Teclas IMPRIMÍVEIS chegam como
// ASCII minúsculo ('a' = 97) — e o engine mapeia por SCANCODE, não por layout,
// então '['/']' são as TECLAS onde o layout americano as tem. As especiais que
// uma UI de calibração costuma querer ficam fixadas aqui dentro (o Mod não
// enxerga engine/keydefs.h: este header é autocontido de propósito).
#define MOD_KEY_F1       135
#define MOD_KEY_F9       143
#define MOD_KEY_DEL      148
#define MOD_KEY_PGDN     149
#define MOD_KEY_PGUP     150
#define MOD_KEY_HOME     151
#define MOD_KEY_END      152

// botões virtuais (mod_input_sample_t.buttons_state / buttons_changed)
#define MOD_BTN_ATTACK   (1u<<0)
#define MOD_BTN_ATTACK2  (1u<<1)
#define MOD_BTN_JUMP     (1u<<2)
#define MOD_BTN_USE      (1u<<3)
#define MOD_BTN_RELOAD   (1u<<4)
#define MOD_BTN_CROUCH   (1u<<5)
#define MOD_BTN_WALK     (1u<<6)
#define MOD_BTN_SCORE    (1u<<7)
#define MOD_BTN_MENU     (1u<<8)  // ESC virtual (abre/fecha menu/console)

// APIs do engine entregues ao Mod no Mod_Init. Extensível: campos novos
// sempre no fim; `size` permite ao Mod detectar o quanto o engine oferece.
typedef struct mod_engine_api_s
{
	int          size;            // sizeof( mod_engine_api_t ) do engine
	void       (*Printf)( const char *fmt, ... );   // log do engine (console)
	double     (*DoubleTime)( void );               // tempo real, segundos
	const char *engine_version;                      // ex.: "0.21"
	const csmod_services_t *services; // v9: per-mod context, stable C interfaces
} mod_engine_api_t;

// Câmera por olho (capability MOD_CAP_DISPLAY).
// O engine preenche com a view atual; o Mod pode sobrescrever
// vieworigin/viewangles/fov_x/fov_y (graus). `projection` é somente saída
// no v1: o engine ainda não injeta matriz externa (fase 2 do plano VR).
typedef struct mod_view_s
{
	int   eye;               // 0 = mono/olho esquerdo, 1 = olho direito
	float vieworigin[3];
	float viewangles[3];     // pitch, yaw, roll
	float fov_x, fov_y;      // graus
	float projection[16];    // coluna-major; engine ignora no v1
	int   has_projection;    // 1 se o Mod preencheu `projection`
	int   viewport[4];       // v2: se o Mod mudar, vira viewport absoluto em
	                         // pixels do alvo de render (flag RF_VIEWPORT_ABSOLUTE)
	float fov_asym[4];       // v4: frustum do olho fora de centro, em RADIANOS e
	                         // no sinal do OpenXR (left/down negativos, right/up
	                         // positivos). Com has_fov_asym=1 o engine desenha com
	                         // estes limites em vez de fov_x/fov_y — exigido pela
	                         // ótica canted do headset (fov_x/fov_y só fazem
	                         // frustum centralizado e o render sairia diferente do
	                         // que o compositor assume).
	int   has_fov_asym;      // 1 se o Mod preencheu fov_asym
} mod_view_t;

// Contexto gráfico do engine, entregue no Mod_BeginFrame (v1: ponteiros
// crus do backend SDL usado pelo engine).
typedef struct mod_gl_ctx_s
{
	void *window;      // SDL_Window *
	void *gl_context;  // SDL_GLContext
} mod_gl_ctx_t;

// Injeção de input (capability MOD_CAP_INPUT). O Mod preenche uma amostra
// por frame, no Mod_Input (chamado em Host_Frame, ANTES de o usercmd do frame
// ser montado); o engine aplica a amostra no CL_CreateCmd, logo depois do
// CL_CreateMove do cliente e da IN_EngineAppendMove — portanto o Mod vence o
// mouse/teclado e o que ele escreve aqui é o que vai no usercmd enviado ao
// servidor e na predição.
//
// Considere a amostra ZERADA a cada frame: o Mod só deve preencher o que quer
// controlar. Um campo em zero significa "não mexo nisso" para movimento
// (o teclado segue valendo), mas `has_viewangles_abs` é sempre decisão do Mod.
typedef struct mod_input_sample_s
{
	float        move_forward, move_side;      // -1..1, no referencial do yaw injetado (cabeça)
	float        viewangles_delta[2];          // yaw, pitch (graus, este frame) — v1
	unsigned int buttons_state;                // bitmask MOD_BTN_*
	unsigned int buttons_changed;              // bits que mudaram neste frame
	// v5
	float        move_up;                      // -1..1 vertical. Vai para o upmove do jogo,
	                                           // que no chão é ignorado (o pmove zera
	                                           // wishvel[2]) e vale em água/espectador.
	float        viewangles_abs[3];            // pitch, yaw, roll ABSOLUTOS em graus.
	                                           // Absoluto e não delta porque a pose do HMD
	                                           // já é uma orientação absoluta: com delta o
	                                           // Mod brigaria com o mouse e com o
	                                           // CL_AdjustAngles do cliente. Roll deve ficar
	                                           // em 0 — o jogo o ignora, e o roll do render
	                                           // vem do Mod_Camera (por olho).
	int          has_viewangles_abs;           // 1 = usar viewangles_abs (VR); 0 = delta do v1
} mod_input_sample_t;

// Viewmodel — a arma na mão (capability MOD_CAP_DISPLAY, v6).
//
// O engine pré-preenche a ENTRADA com o que o client DLL montou neste frame e
// chama o Mod; o Mod ajusta o que quiser e marca a SAÍDA. O engine então aplica
// no clgame.viewent e nas flags do ref_viewpass_t do olho corrente.
//
// Por que a entrada vem pronta: a ORIENTAÇÃO do viewmodel já é a convenção de
// cada modelo — o client DLL a deriva dos ângulos da view em V_CalcGunAngle
// (inclusive o pitch invertido e o roll de idle). Reconstruir isso do zero
// seria adivinhar; o Mod deve NUDGE, não refazer.
//
// Chamado POR OLHO: o client DLL reescreve o viewmodel a cada pass de olho
// (pfnCalcRefdef), então uma sobrescrita feita só no primeiro olho seria
// desfeita no segundo. O Mod precisa ser idempotente dentro do frame.
typedef struct mod_viewmodel_s
{
	float player_origin[3];   // ENTRADA: olho CENTRAL do player no mundo — sem
	                          // o offset de roomscale e sem o afastamento entre
	                          // os olhos. A arma é UM objeto no mundo: os dois
	                          // olhos têm de vê-la no mesmo lugar, senão a
	                          // estereoscopia da arma sai errada (é por isso que
	                          // a base aqui NÃO é o vieworigin do rvp, que já
	                          // traz o offset por olho).
	float origin[3];          // ENTRADA/SAÍDA: origem do viewmodel, como o
	                          // client DLL a escreveu → o Mod sobrescreve os 3
	                          // e marca has_origin
	float angles[3];          // ENTRADA/SAÍDA: ângulos do viewmodel (mesma ideia)
	char  model_name[64];     // ENTRADA: nome do modelo, ex. "v_ak47.mdl" — dá
	                          // para afinar o encaixe por classe de arma
	int   eye;                // ENTRADA: olho do pass corrente (0 ou 1)

	int   has_origin;         // SAÍDA: 1 = aplicar origin[]
	int   has_angles;         // SAÍDA: 1 = aplicar angles[] (espelhado também em
	                          // curstate.angles e latched.prevangles, como o
	                          // client DLL faz em view.cpp)
	int   world_depth;        // SAÍDA: 1 = depth NORMAL: a arma é ocluída pelo
	                          // mundo. Sem isto vale o hack VIEWMODEL_DEPTH_RANGE
	                          // do ref, cujo propósito é a arma NUNCA cortar na
	                          // parede — exatamente o oposto do que VR quer
	int   hide;               // SAÍDA: 1 = não desenhar o viewmodel do jogo
	                          // (para um Mod que desenhe a própria arma/mãos)
} mod_viewmodel_t;

// HUD/UI 2D — menus no headset e ponteiro laser (capability MOD_CAP_HUD, v8).
//
// Um hook só, no mesmo estilo do mod_viewmodel_t: o ENGINE PREENCHE A ENTRADA
// com os fatos do frame e o Mod marca as SAÍDAS. Chamado 1x por frame em
// Host_Frame, DEPOIS do Mod_FramePre (as poses já são deste frame) e ANTES do
// input frame e do desenho 2D — é essa posição na ordem que faz o cursor e o
// clique valerem para o frame que está começando.
//
// Por que os fatos vêm do engine: "tem menu aberto" é `cls.key_dest != key_game`
// (o menu do ESC e os menus do client — time, classe, compra — todos marcam
// KEY_MENU ao abrir), e o tamanho do espaço 2D é do engine. O Mod não tem como
// saber nenhum dos dois, e a POLÍTICA (desenhar painel? ligar o laser?) é do Mod.
typedef struct mod_hud_s
{
	int      size;          // sizeof( mod_hud_t ) do engine

	// --- ENTRADA (engine) ---
	int      menu_open;     // 1 = há UI de tela cheia no comando (key_dest != key_game)
	int      ui_visible;    // UI_IsVisible() (menu principal do mainui)
	int      canvas_w;      // espaço 2D do engine em pixels (= resolução do render)
	int      canvas_h;
	unsigned canvas_tex;    // textura do canvas 2D preenchido no frame anterior
	                        // (0 = ainda não existe: o Mod pula o desenho)

	// --- SAÍDA (Mod) ---
	int      want_canvas;   // 1 = quero o desenho 2D deste frame num framebuffer
	                        // offscreen. Sem isto o 2D vai direto para a janela,
	                        // como sempre foi (é o caminho do jogo flat).
	int      has_pointer;   // 1 = o cursor do menu é MEU: o engine usa x/y como a
	                        // posição absoluta do mouse e ignora o mouse do SO
	                        // (os DOIS consumidores: UI_MouseMove e o
	                        // GetMousePosition que o client DLL usa nos menus dele)
	float    x, y;          // pixels do espaço 2D (canvas_w/canvas_h)
	int      buttons;       // BIT(0) = botão esquerdo. O engine gera o clique na
	                        // BORDA (K_MOUSE1 → UI_KeyEvent) — o Mod só descreve o
	                        // estado; segurar o botão = botão preso, como no mouse.
} mod_hud_t;

typedef struct mod_api_s
{
	// identidade
	int          api_version;   // tem que ser exatamente MOD_API_VERSION
	const char  *mod_name;
	unsigned int capabilities;  // bitmask MOD_CAP_*

	// lifecycle
	int  (*Mod_Init)( const mod_engine_api_t *eng );   // 0 = recusa de load
	void (*Mod_Shutdown)( void );

	// frame (sempre chamados, independem de capabilities)
	void (*Mod_FramePre)( double time );
	void (*Mod_FramePost)( double time );

	// capability MOD_CAP_DISPLAY
	void (*Mod_Camera)( mod_view_t *view );            // por view pass (por olho)
	int  (*Mod_BeginFrame)( const mod_gl_ctx_t *ctx ); // antes do render 3D
	void (*Mod_EndFrame)( void );                      // após o render 2D

	// capability MOD_CAP_INPUT
	void (*Mod_Input)( mod_input_sample_t *sample );   // 1x por frame

	// capability MOD_CAP_HUD
	// v8: troca os antigos Mod_HudRedirect/Mod_HudPointer (declaração sem
	// fiamento) por um único intercâmbio por frame. Ver mod_hud_t.
	void (*Mod_Hud)( mod_hud_t *hud );                 // 1x por frame, antes do input

	// v2 (capability MOD_CAP_DISPLAY)
	int  (*Mod_EyeCount)( void );  // passes por frame: 1 = mono, 2 = estéreo
	void (*Mod_AfterEyes)( void ); // após o loop de olhos, antes do desenho 2D

	// v3 (capability MOD_CAP_HUD)
	// Último ponto de desenho do frame: HUD, menus, console e placar já foram
	// desenhados e o swap ainda não aconteceu — o que o Mod desenhar aqui
	// aparece POR CIMA da UI do jogo (e, no VR, só na janela: o alvo dos olhos
	// é outro framebuffer, e o submit ao runtime acontece em Mod_EndFrame).
	// Também é onde um overlay de debug da tela 2D deve morar.
	void (*Mod_Overlay2D)( void );

	// v6 (capability MOD_CAP_DISPLAY)
	// Arma na mão: reposiciona o viewmodel no mundo e escolhe o depth. Chamado
	// em V_RenderView DEPOIS de Mods_Camera — o rvp->flags já está montado
	// (V_GetRefParams ZERA as flags, então o Mod não pode ser chamado antes
	// dele) e ainda dá tempo de escrever clgame.viewent antes do render do
	// olho. Ver mod_viewmodel_t para o contrato de entrada/saída.
	void (*Mod_ViewModel)( mod_viewmodel_t *vm );

	// v7 (capability MOD_CAP_KEYS)
	// Evento de teclado, chamado no topo do Key_Event do engine — DEPOIS de
	// Key_Rotate (o keynum já está no espaço do jogo) e ANTES de qualquer
	// handler: teclado virtual (OSK), console, menu, cliente DLL e binds.
	// Devolver 1 = CONSUMIDO: o engine encerra o evento ali e a tecla não chega
	// ao jogo. O Mod deve consumir só o que ele realmente usa (as teclas da UI
	// de calibração) e devolver 0 em todo o resto — inclusive quando a UI está
	// desligada, senão a tecla some do jogo sem o usuário pedir.
	int (*Mod_Key)( int key, int down );

	// v9: additive hooks; world runs after each scene, before eye submission.
	unsigned int size;
	int (*Mod_Enable)( int enabled ); // init once; enable/disable between frames
	void (*Mod_DebugPanel)( const csmod_ui_t *ui );
	void (*Mod_WorldOverlay)( const csmod_view_t *view, const csmod_draw_t *draw );
} mod_api_t;

typedef mod_api_t *(*ModGetAPI_t)( void );

#endif // MOD_API_H
