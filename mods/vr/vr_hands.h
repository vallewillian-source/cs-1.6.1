// vr_hands.h — viewmodel proprio do Mod VR: bracos (WRAD ARMS) + arma (Quaternius).
//
// Este subsistema NAO depende do engine nem da ABI do Mod: ele recebe fatos ja
// convertidos para unidades/eixos GoldSrc (a mesma convencao que o resto do
// mod usa) e desenha sozinho, em GL cru, dentro do FBO do olho que o mod liga
// antes de chamar. Assim ele pode ser testado e evoluido sem tocar no mod.
//
// Por que existe: a ABI do Mod (v8) nao oferece carregador de modelo/textura
// nem helper de desenho — o engine so entrega o FBO do olho e as poses das
// maos. Ver docs/MODS.md e mods/vr/assets/SOURCES.md.
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

// Uma mao no MUNDO, em unidades/eixos GoldSrc. A base e ortonormal.
typedef struct
{
	bool  ok;
	float pos[3];
	float fwd[3];      // para onde o eixo da mao aponta
	float rgt[3];
	float up[3];
} vr_hands_hand_t;

// Log: o mod passa o proprio (eng->Printf via wrapper). Evita depender dele aqui.
typedef void ( *vr_hands_log_fn )( const char *msg );

// Carrega assets + carrega o GL que ele mesmo precisa. false = indisponivel
// (asset ausente, GL sem as funcoes) e o mod segue como se nao existisse.
bool vr_hands_init( vr_hands_log_fn log );

void vr_hands_shutdown( void );

// true = assets carregados e prontos para desenhar.
bool vr_hands_ready( void );

// Rele o cfg e, se pedido, recarrega os assets do disco (hot-reload).
void vr_hands_reload( void );

// Chamado 1x por frame para releitura de cfg por mtime (barato).
void vr_hands_tick( void );

// Desenho ESTATICO de diagnostico: poe o modelo parado na frente da camera.
// Serve para provar carga+skin+textura+projecao sem headset (VR_HANDS_TEST).
void vr_hands_draw_test( const vr_hands_cam_t *cam );

// Desenho real: pose conduzida pelas maos + arma presa na mao direita.
void vr_hands_draw( const vr_hands_cam_t *cam, const vr_hands_hand_t hands[2] );

// Linha curta de estado para o painel de debug do mod.
void vr_hands_status( char *buf, size_t n );

// Cobre este nome de viewmodel? (usado para decidir vm->hide). Ex.: "v_usp.mdl".
bool vr_hands_covers_model( const char *model_name );

// Liga/desliga o recuo visual (chamado quando o jogo atira).
void vr_hands_trigger_recoil( void );

#endif // VR_HANDS_H
