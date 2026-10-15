// Private implementation unit; included once by mod_vr.cpp. GPL-3.0.
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
	FILE *f = fopen( vr_config_path("vr_ipd.cfg"), "w" );

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
	if( stat( vr_config_path("vr_ipd.cfg"), &st ) == 0 )
		vr_ipd_cfg_mtime = ( double )st.st_mtime;
}

static void vr_ipd_cfg_load( void )
{
	struct stat st;
	const char *why = NULL;
	float mm = 0.0f;
	bool  got = false;
	char  line[256];
	FILE *f = fopen( vr_config_path("vr_ipd.cfg"), "r" );

	if( stat( vr_config_path("vr_ipd.cfg"), &st ) == 0 )
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

	if( stat( vr_config_path("vr_ipd.cfg"), &st ) != 0 )
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
	FILE *f = fopen( vr_config_path("vr_weapon.cfg"), "r" );

	// registra a mtime JÁ na carga inicial: sem isto o primeiro tick veria "-1"
	// e releria o mesmo arquivo no primeiro frame (log em dobro, à toa)
	if( stat( vr_config_path("vr_weapon.cfg"), &st ) == 0 )
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

	if( stat( vr_config_path("vr_weapon.cfg"), &st ) != 0 )
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
