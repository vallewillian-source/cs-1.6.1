// Private implementation unit; included once by mod_vr.cpp. GPL-3.0.
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
static XrAction xr_act_move, xr_act_turn, xr_act_fire, xr_act_altfire;
static XrAction xr_act_jump, xr_act_reload, xr_act_use, xr_act_score, xr_act_crouch, xr_act_menu, xr_act_haptic;
static XrAction xr_act_grip[2];   // POR MAO: o grip decide quem segura a arma
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
static float in_grip[2];    // squeeze por mao (0 = esquerda, 1 = direita)
// yaw do TRONCO (o coldre fica nele): com VR_BODY_YAW_LAG > 0 ele atrasa em relacao
// a cabeca, entao olhar para o lado nao gira o coldre junto. 0 = instantaneo.
static float  vr_body_yaw;
static double vr_body_yaw_t;
static float  vr_body_yaw_lag;
static bool  in_jump, in_reload, in_use, in_score, in_crouch_btn, in_menu;
static bool  in_menu_prev;
static bool  in_hand_ok[2];
static float in_hand_g[2][3];
static char  in_profile[2][128] = { "(?)", "(?)" };

// ---------------- arma na mão / mira (Fase 6) ----------------
// Ver a seção "arma na mão" mais abaixo e o cabeçalho do arquivo.
enum { VR_AIM_HEAD = 0, VR_AIM_LEFT = 1, VR_AIM_RIGHT = 2 };

static int    vr_aim_source = VR_AIM_RIGHT; // VR_AIM_SOURCE (padrão: mão direita)
static bool   vr_aim_from_weapon = true;    // VR_AIM_FROM_WEAPON=0 volta ao da mão
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
