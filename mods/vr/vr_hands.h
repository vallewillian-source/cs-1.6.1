// vr_hands.h — viewmodel proprio do Mod VR (V2): bracos (WRAD ARMS) + arma (Quaternius).
//
// Este subsistema NAO depende do engine nem da ABI do Mod: ele recebe fatos ja
// convertidos para unidades/eixos GoldSrc (a mesma convencao que o resto do
// mod usa) e desenha sozinho, em GL cru, dentro do FBO do olho que o mod liga
// antes de chamar. Assim ele pode ser testado e evoluido sem tocar no mod.
//
// Por que existe: a ABI do Mod (v8) nao oferece carregador de modelo/textura
// nem helper de desenho — o engine so entrega o FBO do olho e as poses das
// maos. Ver docs/MODS.md e mods/vr/assets/SOURCES.md.
//
// Contrato de quem chama (o mod):
//   1x por frame   vr_hands_update()  — resolve pose/estado, devolve o relato
//   por olho       vr_hands_draw()    — so desenha o que o update resolveu
//   diagnostico    vr_hands_sheet()   — folha de contato (varias vistas)
#ifndef VR_HANDS_H
#define VR_HANDS_H

#include <stdbool.h>
#include <stddef.h>

// Camera de um olho, em unidades/eixos GoldSrc — os MESMOS campos que o mod ja
// guarda em vr_eye_cam[] (o frustum e assimetrico; fov no sinal do OpenXR).
typedef struct
{
	float origin[3];
	float angles[3];   // pitch, yaw, roll em graus (convencao do engine)
	float fov[4];      // fov_asym em radianos: left, right, up, down (left/down negativos)
	int   w, h;        // tamanho do alvo daquele olho (pixels)
	bool  ok;
} vr_hands_cam_t;

// Uma mao no MUNDO, em unidades/eixos GoldSrc. A base e ortonormal: `fwd` e a
// direcao do apontar (a do cano), `rgt` a direita (GoldSrc: -Y num yaw 0) e
// `up` a cima. Vem de pose_basis_g() no mod (o mesmo que a mira usa).
typedef struct
{
	bool  ok;
	float pos[3];
	float fwd[3];
	float rgt[3];
	float up[3];
} vr_hands_hand_t;

// Corpo do jogador — e o que ancora o coldre (barriga) e o chao da arma largada.
// O mod entrega: posicao do olho central + offset de roomscale, eixos do YAW DO
// TRONCO (ja amortecido por ele, se quiser) e o plano do chao do jogador.
typedef struct
{
	float pos[3];
	float fwd[3];      // para onde o tronco aponta
	float rgt[3];      // direita
	float up[3];       // cima
	float floor_z;     // chao do jogador (0 = desconhecido: usa pos[2] - altura)
	float eye_height;  // altura dos olhos usada quando floor_z = 0
} vr_hands_body_t;

// Entrada dos controles por mao (o grip e analogico 0..1, o mesmo squeeze).
typedef struct
{
	float grip[2];
	bool  fire;
	float dt;          // segundos desde o frame anterior (queda da arma largada)
} vr_hands_in_t;

// Estado da arma coberta por este subsistema.
typedef enum
{
	VR_WS_HOLSTER = 0,  // guardada no coldre (barriga)
	VR_WS_HAND    = 1,  // na mao
	VR_WS_GROUND  = 2,  // largada no chao
} vr_ws_t;

// Relato do frame — e o que o mod usa para mirar e para decidir se o gatilho vale.
typedef struct
{
	bool  armed;       // ha arma NA MAO (sem isto o gatilho NAO atira)
	bool  two_hand;    // as duas maos na arma
	int   state;       // vr_ws_t
	int   rear_hand;   // mao que segura o cabo (0 = esquerda, 1 = direita)
	float aim[3];      // direcao do CANO no mundo (0 0 0 = invalida)
	float fit_dist;    // diagnostico: distancia do ponto de empunhadura a palma
	float scale;       // diagnostico: unidades GoldSrc por unidade do modelo
} vr_hands_state_t;

// Log: o mod passa o proprio (eng->Printf via wrapper). Evita depender dele aqui.
typedef void ( *vr_hands_log_fn )( const char *msg );

// Carrega assets + carrega o GL que ele mesmo precisa. false = indisponivel
// (asset ausente, GL sem as funcoes) e o mod segue como se nao existisse.
bool vr_hands_init( vr_hands_log_fn log );

void vr_hands_shutdown( void );

// true = assets carregados e prontos para desenhar.
bool vr_hands_ready( void );

// true = o gatilho so vale com a arma NA MAO (cfg `enforce_armed`). O mod usa
// isto para o MOD_BTN_ATTACK: soltar/guardar a arma passa a ter consequencia.
bool vr_hands_enforce_armed( void );

// Rele o cfg e, se pedido, recarrega os assets do disco (hot-reload).
void vr_hands_reload( void );

// Chamado 1x por frame para releitura de cfg por mtime (barato).
void vr_hands_tick( void );

// Folha de contato para diagnostico (0 = desligada). Ver vr_hands_sheet.
typedef enum
{
	VR_SHEET_OFF = 0,
	VR_SHEET_FIT,     // encaixe: a arma na mao, orbitando
	VR_SHEET_WEAPON,  // a arma sozinha + marcadores (empunhadura/eixo do cano)
	VR_SHEET_CURL,    // fechamento dos dedos em 4 niveis, as duas maos
	VR_SHEET_STATES,  // as poses da maquina de estados
	VR_SHEET_WALK,    // roteiro pela maquina de estados (pegar/guardar/largar/2 maos)
} vr_sheet_t;

void vr_hands_set_sheet( int mode );

// 1x por frame, ANTES do VR_Input do mod: resolve a pose, o estado da arma (na
// mao / coldre / chao), a mao de tras, as duas maos e devolve o relato do frame.
// `hands` pode ser NULL (ou por mao invalida): essa mao vira sintetica, o que
// permite ver a IK sem headset.
void vr_hands_update( const vr_hands_body_t *body, const vr_hands_in_t *in,
	const vr_hands_hand_t hands[2], vr_hands_state_t *out );

// Por olho: desenha a pose resolvida pelo ultimo vr_hands_update.
void vr_hands_draw( const vr_hands_cam_t *cam );

// Diagnostico sem headset: desenha uma GRADE de vistas da mesma pose no alvo
// atual (usa a camera dada para o tamanho/fov da folha). Precisa de VR_HANDS_TEST
// no mod para ser chamada.
void vr_hands_sheet( const vr_hands_cam_t *cam );

// Linha curta de estado para o painel de debug do mod.
void vr_hands_status( char *buf, size_t n );

// Cobre este nome de viewmodel? (usado para decidir vm->hide). Ex.: "v_usp.mdl".
bool vr_hands_covers_model( const char *model_name );

#endif // VR_HANDS_H
