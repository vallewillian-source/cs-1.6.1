// Private implementation unit; included once by mod_vr.cpp. GPL-3.0.
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

	std::vector<XrViewConfigurationView> vc( nvc );
	for(auto& v:vc)v.type=XR_TYPE_VIEW_CONFIGURATION_VIEW;
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

		XrSwapchainCreateInfo sci {}; sci.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
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
		rt.images.resize( nimg );
		for(auto& image:rt.images)image.type=XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
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
	XrEventDataBuffer evd {}; evd.type = XR_TYPE_EVENT_DATA_BUFFER;
	while( xrPollEvent( xr_instance, &evd ) == XR_SUCCESS )
	{
		switch( evd.type )
		{
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
		{
			XrEventDataSessionStateChanged *sc = reinterpret_cast<XrEventDataSessionStateChanged *>( &evd );
			if(sc->session!=xr_session)break;
			vr_session_state = ( int )sc->state;
			vr_note( VR_SEV_INFO, "sessao: estado %d (%s)\n", ( int )sc->state, vr_session_state_str( ( int )sc->state ));
			switch( sc->state )
			{
			case XR_SESSION_STATE_READY:
			{
				XrSessionBeginInfo bi {}; bi.type = XR_TYPE_SESSION_BEGIN_INFO;
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
				if(xr_session_begun)xrEndSession(xr_session);
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
		evd = {};evd.type=XR_TYPE_EVENT_DATA_BUFFER;
	}
}

static bool xr_begin_frame( void )
{
	xr_fs = {};xr_fs.type=XR_TYPE_FRAME_STATE;
	if( XR_FAILED( xrWaitFrame( xr_session, NULL, &xr_fs )))
		return false;

	vr_perf_hmd_period_ms = vr_perf_hmd_period_ms > 0.0 ? vr_perf_hmd_period_ms : xr_fs.predictedDisplayPeriod / 1000000.0;
	vr_perf_work_t0 = vr_now(); // o trabalho do frame começa quando o HMD libera

	XrFrameBeginInfo bi {}; bi.type = XR_TYPE_FRAME_BEGIN_INFO;
	XrResult r = xrBeginFrame( xr_session, &bi );
	if( XR_FAILED( r ))
	{
		// típico sem headset ativo: sessão não está rodando
		vr_active = false;
		return false;
	}

	xr_frame_began = true;

	xr_views[0] = {};xr_views[0].type=XR_TYPE_VIEW;
	xr_views[1] = {};xr_views[1].type=XR_TYPE_VIEW;
	XrViewLocateInfo vli {}; vli.type = XR_TYPE_VIEW_LOCATE_INFO;
	vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	vli.displayTime = xr_fs.predictedDisplayTime;
	vli.space = xr_space;
	uint32_t n = 0;
	XrViewState vs {}; vs.type = XR_TYPE_VIEW_STATE;
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

	XrCompositionLayerProjection proj {}; proj.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
	proj.space = xr_space;
	proj.viewCount = 2;
	proj.views = lv;

	const XrCompositionLayerBaseHeader *layers[1] = { reinterpret_cast<const XrCompositionLayerBaseHeader *>( &proj ) };

	XrFrameEndInfo fei {}; fei.type = XR_TYPE_FRAME_END_INFO;
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
static XrGraphicsBindingOpenGLXlibKHR g_glbind = []{ XrGraphicsBindingOpenGLXlibKHR value{}; value.type=XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR; return value; }();
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

	XrInstanceProperties ip {}; ip.type = XR_TYPE_INSTANCE_PROPERTIES;
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
		XrSystemGetInfo sgi {}; sgi.type = XR_TYPE_SYSTEM_GET_INFO;
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
		XrGraphicsRequirementsOpenGLKHR reqs {}; reqs.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;
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
		XrSessionCreateInfo sci {}; sci.type = XR_TYPE_SESSION_CREATE_INFO;
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
		XrReferenceSpaceCreateInfo rsi {}; rsi.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
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
