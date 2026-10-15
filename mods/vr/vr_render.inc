// Private implementation unit; included once by mod_vr.cpp. GPL-3.0.
// ---------------- render targets por olho ----------------
static void bind_eye( int eye )
{
	EyeRT &rt = xr_eye[eye];

	if( rt.sc == XR_NULL_HANDLE )
		return;

	if( rt.cur < 0 )
	{
		XrSwapchainImageAcquireInfo ai {}; ai.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
		XrResult ra = xrAcquireSwapchainImage( rt.sc, &ai, reinterpret_cast<uint32_t *>( &rt.cur ));
		if( XR_FAILED( ra ))
		{
			vr_note( VR_SEV_ERROR, "acquire swapchain olho %d: %d %s\n", eye, ( int )ra, vr_xr_result_str( ( int )ra ));
			rt.cur = -1;
			return;
		}
		XrSwapchainImageWaitInfo wi {}; wi.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
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

	// Shared modhost owns the monitor debug panels.
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
