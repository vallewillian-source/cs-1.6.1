/*
mod_vr.cpp — PRIMEIRO Mod real: realidade virtual via OpenXR (PCVR, GLX).

Fase 2 do PLAN.md: sessão OpenXR (WiVRn/Monado/SteamVR), frame loop com
pacing pelo HMD, swapchains GL por olho, câmera estereoscópica por cabeça
(sem 6DoF — mira pela cabeça, MVP), espelho da vista no monitor.

Este Mod NÃO toca no engine: usa a ABI v2 (engine/common/mod_api.h).
Sem runtime OpenXR disponível (ou headset desligado), o Mod fica inativo e
o jogo roda flat normalmente (fallback automático).

Variáveis de ambiente:
  VR_DEBUG=1               log verboso (poses, frame times)
  VR_RESOLUTION_SCALE=1.0  escala do buffer de render por olho (0.3–3.0)
  VR_IPD=0.063             distância interpupilar em metros
  VR_DEBUG_CLEAR=1         limpa cada olho com cor distinta (teste de estéreo)

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
#include <thread>
#include <vector>

#include "mod_api.h"

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

#define OXR( x ) do { XrResult _r = (x); if( XR_FAILED( _r )) { logf( "OXR falhou %d em %s:%d (%s)\n", (int)_r, __FILE__, __LINE__, #x ); return false; } } while( 0 )

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

static bool gl_init( void )
{
	if( g_libgl )
		return true;

	g_libgl = dlopen( "libGL.so.1", RTLD_NOW | RTLD_GLOBAL );
	if( !g_libgl )
	{
		logf( "sem libGL.so.1: %s\n", dlerror());
		return false;
	}

	glx_get_proc = (PFNGLXGETPROCADDRESSPROC)dlsym( g_libgl, "glXGetProcAddress" );
	if( !glx_get_proc )
		return false;

	#define GL_FUN( name, type ) p##name = (type)glx_get_proc(( const GLubyte * )#name ); if( !p##name ) { logf( "GL sem %s\n", #name ); return false; }
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

	#define GLX_FUN( name, type ) p##name = (type)glx_get_proc(( const GLubyte * )#name ); if( !p##name ) { logf( "GLX sem %s\n", #name ); return false; }
	GLX_FUN( glXGetCurrentDisplay, PFNGLXGETCURRENTDISPLAYPROC )
	GLX_FUN( glXGetCurrentDrawable, PFNGLXGETCURRENTDRAWABLEPROC )
	GLX_FUN( glXGetCurrentContext, PFNGLXGETCURRENTCONTEXTPROC )
	GLX_FUN( glXQueryContext, PFNGLXQUERYCONTEXTPROC )
	GLX_FUN( glXChooseFBConfig, PFNGLXCHOOSEFBCONFIGPROC )
	GLX_FUN( glXGetVisualFromFBConfig, PFNGLXGETVISUALFROMFBCONFIGPROC )
	GLX_FUN( glXQueryDrawable, PFNGLXQUERYDRAWABLEPROC )
	#undef GLX_FUN

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
static std::atomic<bool> xr_session_running { false };
static std::atomic<bool> xr_frame_began { false };
static std::atomic<bool> vr_active { false };  // estéreo neste frame?
static std::atomic<bool> vr_ready { false };   // infra inicializada (instance+session)
static std::atomic<bool> xr_init_started { false };
static std::atomic<bool> xr_init_failed { false }; // init terminou com falha (não re-tentar)
static std::atomic<bool> xr_init_done { false };   // init terminou (ok ou falha)
static float       vr_scale = 1.0f;
static float       vr_ipd = 0.063f;
static bool        vr_debug_clear;
static const float UNITS_PER_METER = 39.3701f; // GoldSrc: unidades ≈ polegadas

static bool xr_begin_frame( void );
static void xr_end_frame( void );
static void xr_poll_events( void );

// ---------------- util ----------------
static double rad2deg( double r ) { return r * 180.0 / M_PI; }

static bool env_on( const char *name )
{
	const char *v = getenv( name );
	return v && *v && strcmp( v, "0" ) != 0;
}

static float env_float( const char *name, float def )
{
	const char *v = getenv( name );
	return ( v && *v ) ? (float)atof( v ) : def;
}

static void copy3( float *d, const float *s ) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }

static void quat_matrix( const XrQuaternionf &q, float c0[3], float c1[3], float c2[3] )
{
	float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
	float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
	float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

	c0[0] = 1 - 2 * ( yy + zz ); c0[1] = 2 * ( xy + wz ); c0[2] = 2 * ( xz - wy );
	c1[0] = 2 * ( xy - wz );     c1[1] = 1 - 2 * ( xx + zz ); c1[2] = 2 * ( yz + wx );
	c2[0] = 2 * ( xz + wy );     c2[1] = 2 * ( yz - wx );     c2[2] = 1 - 2 * ( xx + yy );
}

// pose do HMD (espaço LOCAL, Y-up, -Z frente) → ângulos GoldSrc (Z-up, +X frente, +pitch=baixo)
static void pose_to_viewangles( const XrPosef &pose, float out[3] )
{
	float c0[3], c1[3], c2[3];
	quat_matrix( pose.orientation, c0, c1, c2 );

	float fwd_xr[3] = { -c2[0], -c2[1], -c2[2] };       // forward = -(3ª coluna)
	float fwd_g[3] = { -fwd_xr[2], fwd_xr[0], fwd_xr[1] }; // XR(Y-up) → Gold(Z-up)

	out[0] = ( float )-rad2deg( asin( fmaxf( -1.0f, fminf( 1.0f, fwd_g[2] )))); // pitch
	out[1] = ( float )rad2deg( atan2( fwd_g[1], fwd_g[0] ));                    // yaw
	out[2] = 0.0f; // roll ignorado no MVP
}

// "right" da cabeça em coordenadas GoldSrc (p/ offset de IPD)
static void right_vector_g( const XrPosef &pose, float out[3] )
{
	float c0[3], c1[3], c2[3];
	quat_matrix( pose.orientation, c0, c1, c2 );
	float r_g[3] = { -c0[2], c0[0], c0[1] }; // XR → GoldSrc
	copy3( out, r_g );
}

// ---------------- OpenXR ----------------
static bool xr_make_swapchains( void )
{
	uint32_t nvc = 0;
	OXR( xrEnumerateViewConfigurationViews( xr_instance, xr_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &nvc, NULL ));
	if( nvc < 2 )
	{
		logf( "configuração de view com %u olhos\n", nvc );
		return false;
	}

	std::vector<XrViewConfigurationView> vc( nvc, { XR_TYPE_VIEW_CONFIGURATION_VIEW } );
	OXR( xrEnumerateViewConfigurationViews( xr_instance, xr_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, nvc, &nvc, vc.data()));

	for( uint32_t e = 0; e < 2; e++ )
	{
		EyeRT &rt = xr_eye[e];
		rt.w = ( int32_t )( vc[e].recommendedImageRectWidth * vr_scale );
		rt.h = ( int32_t )( vc[e].recommendedImageRectHeight * vr_scale );

		XrSwapchainCreateInfo sci { XR_TYPE_SWAPCHAIN_CREATE_INFO };
		sci.arraySize = 1;
		sci.format = 0; // GL: runtime escolhe RGBA8
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

		logf( "olho %u: %dx%d, %u imagens\n", e, rt.w, rt.h, nimg );
	}
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
			logf( "sessão: estado %d\n", ( int )sc->state );
			switch( sc->state )
			{
			case XR_SESSION_STATE_READY:
			{
				XrSessionBeginInfo bi { XR_TYPE_SESSION_BEGIN_INFO };
				bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
				if( XR_FAILED( xrBeginSession( xr_session, &bi )))
					logf( "xrBeginSession falhou\n" );
				if( xr_eye[0].sc == XR_NULL_HANDLE )
					xr_make_swapchains();
				break;
			}
			case XR_SESSION_STATE_VISIBLE:
			case XR_SESSION_STATE_FOCUSED:
				xr_session_running = true;
				vr_active = true;
				break;
			case XR_SESSION_STATE_STOPPING:
				xr_end_frame();
				xr_session_running = false;
				vr_active = false;
				break;
			case XR_SESSION_STATE_LOSS_PENDING:
			case XR_SESSION_STATE_EXITING:
				xr_session_running = false;
				vr_active = false;
				break;
			default:
				break;
			}
			break;
		}
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
			logf( "instância OpenXR perdida — desativando\n" );
			xr_session_running = false;
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

	return true;
}

static void xr_end_frame( void )
{
	if( !xr_frame_began )
		return;

	for( int e = 0; e < 2; e++ )
	{
		if( xr_eye[e].sc != XR_NULL_HANDLE && xr_eye[e].cur >= 0 )
		{
			xrReleaseSwapchainImage( xr_eye[e].sc, NULL );
			xr_eye[e].cur = -1;
		}
	}

	XrCompositionLayerProjectionView lv[2] {};
	const bool submit = xr_views_ok && vr_active;

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

	xrEndFrame( xr_session, &fei );
	xr_frame_began = false;
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

	int fbid = 0;
	if( g_glbind.xDisplay && g_glbind.glxContext && pglXQueryContext( g_glbind.xDisplay, g_glbind.glxContext, GLX_FBCONFIG_ID, &fbid ) && fbid )
	{
		int attribs[] = { GLX_FBCONFIG_ID, fbid, None };
		int n = 0;
		GLXFBConfig *cfgs = pglXChooseFBConfig( g_glbind.xDisplay, DefaultScreen( g_glbind.xDisplay ), attribs, &n );
		if( cfgs && n > 0 )
		{
			g_glbind.glxFBConfig = cfgs[0];
			XVisualInfo *vi = pglXGetVisualFromFBConfig( g_glbind.xDisplay, cfgs[0] );
			if( vi )
				g_glbind.visualid = vi->visualid;
		}
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
		logf( "sem runtime OpenXR (xrCreateInstance = %d) — rodando flat\n", ( int )r );
		xr_init_failed = true;
		xr_init_done = true;
		return;
	}

	XrInstanceProperties ip { XR_TYPE_INSTANCE_PROPERTIES };
	ip.type = XR_TYPE_INSTANCE_PROPERTIES;
	if( XR_SUCCEEDED( xrGetInstanceProperties( xr_instance, &ip )))
		logf( "runtime: %s %u.%u.%u\n", ip.runtimeName,
			XR_VERSION_MAJOR( ip.runtimeVersion ), XR_VERSION_MINOR( ip.runtimeVersion ), XR_VERSION_PATCH( ip.runtimeVersion ));

	XrSystemGetInfo sgi { XR_TYPE_SYSTEM_GET_INFO };
	sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	r = xrGetSystem( xr_instance, &sgi, &xr_system );
	if( XR_FAILED( r ))
	{
		logf( "sem HMD (xrGetSystem = %d) — rodando flat\n", ( int )r );
		xr_init_failed = true;
		xr_init_done = true;
		return;
	}

	XrSessionCreateInfo sci { XR_TYPE_SESSION_CREATE_INFO };
	sci.next = &g_glbind;
	sci.systemId = xr_system;
	if( XR_FAILED( xrCreateSession( xr_instance, &sci, &xr_session )))
	{
		logf( "xrCreateSession falhou — rodando flat\n" );
		xr_init_failed = true;
		xr_init_done = true;
		return;
	}

	XrReferenceSpaceCreateInfo rsi { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	rsi.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	rsi.poseInReferenceSpace.orientation = { 0, 0, 0, 1 };
	if( XR_FAILED( xrCreateReferenceSpace( xr_session, &rsi, &xr_space )))
	{
		logf( "xrCreateReferenceSpace falhou — rodando flat\n" );
		xr_init_failed = true;
		xr_init_done = true;
		return;
	}

	vr_ready = true;
	xr_init_done = true;
	logf( "sessão OpenXR criada (aguardando headset)\n" );
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
		if( XR_FAILED( xrAcquireSwapchainImage( rt.sc, &ai, reinterpret_cast<uint32_t *>( &rt.cur ))))
		{
			rt.cur = -1;
			return;
		}
		XrSwapchainImageWaitInfo wi { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
		wi.timeout = 100000000; // 100 ms
		if( XR_FAILED( xrWaitSwapchainImage( rt.sc, &wi )))
			logf( "wait swapchain olho %d estourou\n", eye );

		pglBindFramebuffer( GL_FRAMEBUFFER, rt.fbos[rt.cur] );

		if( vr_debug_clear )
		{
			pglClearColor( eye == 0 ? 0.6f : 0.0f, 0.0f, eye == 0 ? 0.0f : 0.6f, 1.0f );
			pglClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
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

// ---------------- hooks da ABI ----------------
static int VR_Init( const mod_engine_api_t *e )
{
	eng = e;
	verbose = env_on( "VR_DEBUG" );
	vr_debug_clear = env_on( "VR_DEBUG_CLEAR" );
	vr_scale = env_float( "VR_RESOLUTION_SCALE", 1.0f );
	if( vr_scale < 0.3f ) vr_scale = 0.3f;
	if( vr_scale > 3.0f ) vr_scale = 3.0f;
	vr_ipd = env_float( "VR_IPD", 0.063f );

	e->Printf( "mod_vr: init (escala %.2f, ipd %.3f m)\n", vr_scale, vr_ipd );

	if( !gl_init())
	{
		e->Printf( "mod_vr: sem libGL — inativo, jogo flat\n" );
		return 1;
	}

	// GLX só na main thread (contexto atual); o resto do init roda fora —
	// o xrCreateInstance do WiVRn bloqueia sem headset e congelaria o jogo
	if( !capture_glx())
	{
		e->Printf( "mod_vr: sem contexto GLX — inativo, jogo flat\n" );
		return 1;
	}

	xr_init_started = true;
	std::thread( xr_init_thread ).detach();

	// espera curta por um runtime responsivo; caso contrário segue flat e
	// o init continua em background (headset conectado depois = VR "liga sozinha")
	for( int i = 0; i < 60 && !xr_init_done; i++ )
		std::this_thread::sleep_for( std::chrono::milliseconds( 100 ));

	if( !xr_init_done )
	{
		logf( "runtime demorando (headset off?) — jogo flat por ora; VR ativa assim que conectar\n" );
		return 1;
	}

	if( xr_init_failed )
		logf( "inativo — jogo segue flat (fallback)\n" );
	else
		logf( "OpenXR pronto\n" );

	return 1;
}

static void VR_Shutdown( void )
{
	logf( "shutdown\n" );
	if( !xr_init_done )
	{
		logf( "init ainda em andamento — abortando sem destruir sessão\n" );
		return;
	}
	if( xr_session != XR_NULL_HANDLE )
	{
		xr_end_frame();
		xrEndSession( xr_session ); // erro inofensivo se não estava rodando
	}
}

static void VR_FramePre( double time )
{
	( void )time;
	if( !vr_ready )
		return;

	xr_poll_events();

	if( xr_session != XR_NULL_HANDLE && xr_session_running )
	{
		if( !xr_begin_frame())
			vr_active = false;
		else if( verbose )
			logf( "frame: pred=%.2fms\n", xr_fs.predictedDisplayTime / 1000000.0 );
	}
}

static int VR_EyeCount( void )
{
	return ( vr_active && xr_views_ok && xr_eye[0].sc != XR_NULL_HANDLE ) ? 2 : 1;
}

static void VR_Camera( mod_view_t *view )
{
	if( !vr_active || !xr_views_ok || view->eye < 0 || view->eye > 1 )
		return;

	bind_eye( view->eye );

	const XrView &v = xr_views[view->eye];

	// rotação da cabeça → ângulos (posição ignorada no MVP: câmera fica na
	// altura dos olhos do player; mira pela cabeça)
	pose_to_viewangles( v.pose, view->viewangles );

	// offset de IPD ao longo do "right" da cabeça = estéreo de verdade
	float r_g[3];
	right_vector_g( v.pose, r_g );
	float half = ( view->eye == 0 ? -0.5f : 0.5f ) * vr_ipd * UNITS_PER_METER;
	view->vieworigin[0] += r_g[0] * half;
	view->vieworigin[1] += r_g[1] * half;
	view->vieworigin[2] += r_g[2] * half;

	// FOV simétrico = máx por eixo (fallback do MVP; o submit usa o fov
	// assimétrico real, então o compositor não distorce)
	view->fov_x = ( float )rad2deg( fmaxf( -v.fov.angleLeft, v.fov.angleRight ));
	view->fov_y = ( float )rad2deg( fmaxf( v.fov.angleUp, -v.fov.angleDown ));

	view->viewport[0] = 0;
	view->viewport[1] = 0;
	view->viewport[2] = xr_eye[view->eye].w;
	view->viewport[3] = xr_eye[view->eye].h;
}

static void VR_AfterEyes( void )
{
	if( vr_active && xr_frame_began )
	{
		blit_mirror(); // espelho do olho esquerdo no monitor + 2D por cima
	}
	else
	{
		// garante que o 2D desenha na janela mesmo sem VR ativa
		GLint fbo = 0;
		pglGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &fbo );
		pglBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );
		pglBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
		( void )fbo;
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
	.capabilities = MOD_CAP_DISPLAY, // input/hud chegam nas Fases 3/4
	.Mod_Init = VR_Init,
	.Mod_Shutdown = VR_Shutdown,
	.Mod_FramePre = VR_FramePre,
	.Mod_FramePost = NULL,
	.Mod_Camera = VR_Camera,
	.Mod_BeginFrame = NULL, // bind do alvo do olho acontece em Mod_Camera
	.Mod_EndFrame = VR_EndFrame,
	.Mod_Input = NULL,
	.Mod_HudRedirect = NULL,
	.Mod_HudPointer = NULL,
	.Mod_EyeCount = VR_EyeCount,
	.Mod_AfterEyes = VR_AfterEyes,
};

extern "C" mod_api_t *ModGetAPI( void )
{
	return &g_api;
}
