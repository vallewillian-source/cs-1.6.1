// vr_hands.cpp — viewmodel proprio: bracos skinnados (WRAD ARMS) + arma (Quaternius).
//
// Escopo da V1: classe "pistol". O mod esconde o viewmodel do jogo (RF_NO_VIEWMODEL)
// e este arquivo desenha o par bracos+arma dentro do FBO do olho, com profundidade
// real (por isso o mundo oclui os bracos) e a projecao ASSIMETRICA do olho.
//
// Decisoes de engenharia (e o porque):
//  * Este arquivo carrega as proprias funcoes de GL (dlsym em libGL). Sao GL 1.1 core
//    + um proc de FBO: nao vale acoplar no carregador do mod, que serve ao painel 2D e
//    falha em bloco. Aqui, faltar uma funcao so desliga ESTE subsistema.
//  * Malha vira "sopa de triangulos" (sem indice). Custa duplicar vertices, mas o total
//    e ~1.700 triangulos e elimina todo o plumbing de index buffer.
//  * Skinning na CPU, uma vez por FRAME (nao por olho): os dois olhos so trocam a
//    matriz. Mantem o custo irrelevante e dispensa shader.
//  * Sombreamento por vertice na CPU (glColor) — luz fixa junto da camera. Sem
//    iluminacao de GL: mais previsivel e usa a normal que os assets ja trazem.
//  * Convencao de eixos: o resto do mod trabalha em unidades/eixos GoldSrc (Z para
//    cima, 1 unidade ~ 1 polegada). glTF/OBJ vem Y-up: uma matriz fixa converte
//    (frente=+X, direita=-Y, cima=+Z, igual ao GoldSrc) e uma escala vem do cfg.
#include "vr_hands.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dlfcn.h>

// ORDEM OBRIGATORIA: GL antes dos headers das libs.
#include <GL/gl.h>
#include <GL/glext.h>

#define CGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#include "third_party/cgltf.h"
#include "third_party/stb_image.h"
#pragma GCC diagnostic pop

// ---------------------------------------------------------------- log ------
static vr_hands_log_fn g_log;
static void vlog( const char *fmt, ... )
{
	char buf[512];
	va_list ap;
	va_start( ap, fmt );
	vsnprintf( buf, sizeof( buf ), fmt, ap );
	va_end( ap );
	if( g_log ) g_log( buf );
}

// ------------------------------------------------------- GL particular -----
static bool g_dbg_flat;   // diagnostico (cfg test_flat): cor chapada, sem textura
static float g_vlo[3] = { 1e9f, 1e9f, 1e9f };   // bbox do que FOI enviado ao GL
static float g_vhi[3] = { -1e9f, -1e9f, -1e9f };
static long  g_vcount;
static float g_cam_org[3];        // origem da camera deste desenho
static float g_min_dist = 1e9f;   // vertice mais proximo da camera
typedef void ( APIENTRYP PFN_bindfb )( GLenum, GLuint );
typedef void ( APIENTRYP PFN_matrixmode )( GLenum );
typedef void ( APIENTRYP PFN_void )( void );
typedef void ( APIENTRYP PFN_loadmatrix )( const GLfloat * );
typedef void ( APIENTRYP PFN_pushattrib )( GLbitfield );
typedef void ( APIENTRYP PFN_enum )( GLenum );
typedef void ( APIENTRYP PFN_enum_enum_int )( GLenum, GLenum, GLint );
typedef void ( APIENTRYP PFN_depthfunc )( GLenum );
typedef void ( APIENTRYP PFN_depthmask )( GLboolean );
typedef void ( APIENTRYP PFN_depthrange )( GLclampd, GLclampd );
typedef void ( APIENTRYP PFN_viewport )( GLint, GLint, GLsizei, GLsizei );
typedef void ( APIENTRYP PFN_v3f )( GLfloat, GLfloat, GLfloat );
typedef void ( APIENTRYP PFN_uvf )( GLfloat, GLfloat );
typedef void ( APIENTRYP PFN_c4f )( GLfloat, GLfloat, GLfloat, GLfloat );
typedef void ( APIENTRYP PFN_gentex )( GLsizei, GLuint * );
typedef void ( APIENTRYP PFN_deltex )( GLsizei, const GLuint * );
typedef void ( APIENTRYP PFN_bindtex )( GLenum, GLuint );
typedef void ( APIENTRYP PFN_texparam )( GLenum, GLenum, GLint );
typedef void ( APIENTRYP PFN_teximage )( GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid * );
typedef void ( APIENTRYP PFN_pixelstore )( GLenum, GLint );
typedef void ( APIENTRYP PFN_geterror )( void );
typedef void ( APIENTRYP PFN_clientstate )( GLenum );
typedef void ( APIENTRYP PFN_attribarray )( GLuint );

static PFN_bindfb      p_glBindFramebuffer;
static PFN_matrixmode  p_glMatrixMode;
static PFN_void        p_glLoadIdentity;
static PFN_loadmatrix  p_glLoadMatrixf;
static PFN_void        p_glPushMatrix, p_glPopMatrix;
static PFN_pushattrib  p_glPushAttrib;
static PFN_void        p_glPopAttrib;
static PFN_enum        p_glEnable, p_glDisable, p_glCullFace, p_glFrontFace, p_glShadeModel, p_glDepthFunc;
static PFN_enum_enum_int p_glTexEnvi;
static PFN_depthmask   p_glDepthMask;
static PFN_depthrange  p_glDepthRange;
static PFN_viewport    p_glViewport;
static PFN_enum        p_glBegin;
static PFN_void        p_glEnd;
static PFN_v3f         p_glVertex3f, p_glNormal3f;
static PFN_uvf         p_glTexCoord2f;
static PFN_c4f         p_glColor4f;
static PFN_gentex      p_glGenTextures;
static PFN_deltex      p_glDeleteTextures;
static PFN_bindtex     p_glBindTexture;
static PFN_texparam    p_glTexParameteri;
static PFN_teximage    p_glTexImage2D;
static PFN_pixelstore  p_glPixelStorei;
static PFN_geterror    p_glGetError;
static PFN_clientstate p_glEnableClientState, p_glDisableClientState;
static PFN_attribarray p_glEnableVertexAttribArray, p_glDisableVertexAttribArray;

static void *g_libgl;
static bool  gl_ok;

static bool gl_load( void )
{
	if( gl_ok ) return true;
	g_libgl = dlopen( "libGL.so.1", RTLD_NOW | RTLD_GLOBAL );
	if( !g_libgl ) { vlog( "maos: sem libGL.so.1 — subsistema desligado\n" ); return false; }

	#define L( name, var ) var = reinterpret_cast<decltype( var )>( dlsym( g_libgl, "gl" #name ) ); \
		if( !var ) { vlog( "maos: libGL sem gl%s\n", #name ); return false; }
	L( MatrixMode, p_glMatrixMode )   L( LoadIdentity, p_glLoadIdentity )
	L( LoadMatrixf, p_glLoadMatrixf ) L( PushMatrix, p_glPushMatrix )
	L( PopMatrix, p_glPopMatrix )     L( PushAttrib, p_glPushAttrib )
	L( PopAttrib, p_glPopAttrib )     L( Enable, p_glEnable )       L( Disable, p_glDisable )
	L( CullFace, p_glCullFace )       L( FrontFace, p_glFrontFace ) L( ShadeModel, p_glShadeModel )
	L( DepthFunc, p_glDepthFunc )     L( DepthMask, p_glDepthMask ) L( DepthRange, p_glDepthRange )
	L( Viewport, p_glViewport )       L( Begin, p_glBegin )         L( End, p_glEnd )
	L( Vertex3f, p_glVertex3f )       L( Normal3f, p_glNormal3f )   L( TexCoord2f, p_glTexCoord2f )
	L( Color4f, p_glColor4f )         L( GenTextures, p_glGenTextures )
	L( DeleteTextures, p_glDeleteTextures ) L( BindTexture, p_glBindTexture )
	L( TexParameteri, p_glTexParameteri )   L( TexImage2D, p_glTexImage2D )
	L( PixelStorei, p_glPixelStorei ) L( TexEnvi, p_glTexEnvi )     L( GetError, p_glGetError )
	L( EnableClientState, p_glEnableClientState ) L( DisableClientState, p_glDisableClientState )
	#undef L

	{
		typedef void *( *getproc_t )( const GLubyte * );
		getproc_t gp = ( getproc_t )dlsym( g_libgl, "glXGetProcAddress" );
		if( !gp ) gp = ( getproc_t )dlsym( g_libgl, "glXGetProcAddressARB" );
		if( !gp ) { vlog( "maos: sem glXGetProcAddress\n" ); return false; }
		p_glBindFramebuffer = ( PFN_bindfb )gp( ( const GLubyte * )"glBindFramebuffer" );
		if( !p_glBindFramebuffer )
			p_glBindFramebuffer = ( PFN_bindfb )gp( ( const GLubyte * )"glBindFramebufferEXT" );
		if( !p_glBindFramebuffer ) { vlog( "maos: sem glBindFramebuffer\n" ); return false; }

		p_glEnableVertexAttribArray  = ( PFN_attribarray )gp( ( const GLubyte * )"glEnableVertexAttribArray" );
		p_glDisableVertexAttribArray = ( PFN_attribarray )gp( ( const GLubyte * )"glDisableVertexAttribArray" );
	}
	gl_ok = true;
	return true;
}

// ------------------------------------------------------------- math --------
// Matrizes column-major (m[col*4+row]) — o layout que glLoadMatrixf espera.
typedef float m4[16];

static void m4_ident( m4 o ) { memset( o, 0, sizeof( m4 )); o[0]=o[5]=o[10]=o[15]=1.0f; }

static void m4_mul( m4 o, const m4 a, const m4 b )
{
	m4 t;
	for( int c = 0; c < 4; c++ )
		for( int r = 0; r < 4; r++ )
			t[c*4+r] = a[0*4+r]*b[c*4+0] + a[1*4+r]*b[c*4+1] + a[2*4+r]*b[c*4+2] + a[3*4+r]*b[c*4+3];
	memcpy( o, t, sizeof( m4 ));
}

static void m4_from_axes( m4 o, const float x[3], const float y[3], const float z[3], const float t[3] )
{
	o[0]=x[0]; o[1]=x[1]; o[2]=x[2];  o[3]=0;
	o[4]=y[0]; o[5]=y[1]; o[6]=y[2];  o[7]=0;
	o[8]=z[0]; o[9]=z[1]; o[10]=z[2]; o[11]=0;
	o[12]=t[0]; o[13]=t[1]; o[14]=t[2]; o[15]=1;
}

static void m4_xform_point( const m4 m, const float p[3], float out[3] )
{
	out[0] = m[0]*p[0] + m[4]*p[1] + m[8]*p[2]  + m[12];
	out[1] = m[1]*p[0] + m[5]*p[1] + m[9]*p[2]  + m[13];
	out[2] = m[2]*p[0] + m[6]*p[1] + m[10]*p[2] + m[14];
}

static void m4_xform_dir( const m4 m, const float p[3], float out[3] )
{
	out[0] = m[0]*p[0] + m[4]*p[1] + m[8]*p[2];
	out[1] = m[1]*p[0] + m[5]*p[1] + m[9]*p[2];
	out[2] = m[2]*p[0] + m[6]*p[1] + m[10]*p[2];
}

static void v3_norm( float v[3] )
{
	float l = sqrtf( v[0]*v[0] + v[1]*v[1] + v[2]*v[2] );
	if( l > 1e-9f ) { v[0]/=l; v[1]/=l; v[2]/=l; }
}

// Base da camera a partir dos angulos do engine (pitch, yaw, roll) — a MESMA
// convencao do vr_angles_basis do mod, para a projecao casar com a do mundo.
static void cam_basis( const vr_hands_cam_t *cam, float fwd[3], float rgt[3], float up[3] )
{
	const float d2r = ( float )M_PI / 180.0f;
	const float cy = cosf( cam->angles[1]*d2r ), sy = sinf( cam->angles[1]*d2r );
	const float cp = cosf( cam->angles[0]*d2r ), sp = sinf( cam->angles[0]*d2r );
	const float cr = cosf( cam->angles[2]*d2r ), sr = sinf( cam->angles[2]*d2r );
	fwd[0] = cp*cy;                 fwd[1] = cp*sy;                 fwd[2] = -sp;
	rgt[0] = -sr*sp*cy + cr*sy;     rgt[1] = -sr*sp*sy - cr*cy;     rgt[2] = -sr*cp;
	up[0]  =  cr*sp*cy + sr*sy;     up[1]  =  cr*sp*sy - sr*cy;     up[2]  =  cr*cp;
}

static void euler_matrix( m4 o, float pitch, float yaw, float roll )
{
	const float d2r = ( float )M_PI / 180.0f;
	const float rx = pitch*d2r, ry = yaw*d2r, rz = roll*d2r;
	const float cx=cosf(rx), sx=sinf(rx), cy=cosf(ry), sy=sinf(ry), cz=cosf(rz), sz=sinf(rz);
	float R[3][3];
	R[0][0]=cz*cy;               R[0][1]=cz*sy*sx - sz*cx;   R[0][2]=cz*sy*cx + sz*sx;
	R[1][0]=sz*cy;               R[1][1]=sz*sy*sx + cz*cx;   R[1][2]=sz*sy*cx - cz*sx;
	R[2][0]=-sy;                 R[2][1]=cy*sx;              R[2][2]=cy*cx;
	m4_ident( o );
	for( int r = 0; r < 3; r++ )
		for( int c = 0; c < 3; c++ )
			o[c*4+r] = R[r][c];
}

// glTF/OBJ (Y-up, frente -Z) -> GoldSrc (frente +X, direita -Y, cima +Z).
static void conv_axis_matrix( m4 o )
{
	memset( o, 0, sizeof( m4 ));
	o[0]=0; o[4]=0; o[8]=-1; o[12]=0;   // gx = -gz
	o[1]=-1; o[5]=0; o[9]=0; o[13]=0;   // gy = -gx
	o[2]=0; o[6]=1; o[10]=0; o[14]=0;   // gz = +gy
	o[15]=1;
}

static void scale_matrix( m4 o, float s )
{
	m4_ident( o );
	o[0]=o[5]=o[10]=s;
}

// Inversa 4x4 GERAL (Gauss-Jordan). Nao podemos assumir matriz ortonormal: as
// matrizes de mundo do rig carregam a escala (scale_arms), e usar uma inversa
// "rigida" nelas produzia locais com translacao de centenas de unidades — o que
// explodia a malha (bbox de +-200000). Esta inversa nao tem essa suposicao.
static void m4_inv( m4 out, const m4 m )
{
	double aug[4][8];
	for( int r = 0; r < 4; r++ )
		for( int c = 0; c < 4; c++ )
			aug[r][c] = ( double )m[c*4+r];
	for( int r = 0; r < 4; r++ )
		for( int c = 0; c < 4; c++ )
			aug[r][4+c] = ( r == c ) ? 1.0 : 0.0;

	for( int i = 0; i < 4; i++ )
	{
		int piv = i; double best = fabs( aug[i][i] );
		for( int r = i+1; r < 4; r++ ) if( fabs( aug[r][i] ) > best ) { best = fabs( aug[r][i] ); piv = r; }
		if( best < 1e-12 ) { m4_ident( out ); return; }
		if( piv != i ) for( int c = 0; c < 8; c++ ) { double t = aug[i][c]; aug[i][c] = aug[piv][c]; aug[piv][c] = t; }
		const double d = aug[i][i];
		for( int c = 0; c < 8; c++ ) aug[i][c] /= d;
		for( int r = 0; r < 4; r++ )
			if( r != i )
			{
				const double f = aug[r][i];
				if( f != 0.0 ) for( int c = 0; c < 8; c++ ) aug[r][c] -= f * aug[i][c];
			}
	}
	for( int r = 0; r < 4; r++ )
		for( int c = 0; c < 4; c++ )
			out[c*4+r] = ( float )aug[r][4+c];
}

// Inversa de uma transformacao RIGIDA (rotacao ortogonal + translacao).
static void m4_rigid_inv( m4 o, const m4 a )
{
	m4 t;
	for( int r = 0; r < 3; r++ )
		for( int c = 0; c < 3; c++ )
			t[r*4+c] = a[c*4+r];           // R^T
	t[3]=t[7]=t[11]=0.0f; t[15]=1.0f;
	t[12] = -( t[0]*a[12] + t[4]*a[13] + t[8]*a[14] );
	t[13] = -( t[1]*a[12] + t[5]*a[13] + t[9]*a[14] );
	t[14] = -( t[2]*a[12] + t[6]*a[13] + t[10]*a[14] );
	memcpy( o, t, sizeof( m4 ));
}

// Rotacao MINIMA que leva o vetor unitario a no vetor unitario b (Rodrigues).
static void rot_between( m4 o, const float a[3], const float b[3] )
{
	const float c = a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
	float ax[3] = { a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0] };
	float s = sqrtf( ax[0]*ax[0] + ax[1]*ax[1] + ax[2]*ax[2] );
	if( c > 0.999999f ) { m4_ident( o ); return; }
	if( s < 1e-6f )
	{
		// 180 graus: qualquer eixo perpendicular a `a`; R = 2*aa^T - I
		float p[3] = { 0.0f, 0.0f, 1.0f };
		if( fabsf( a[2] ) > 0.9f ) { p[0] = 1.0f; p[2] = 0.0f; }
		ax[0] = a[1]*p[2]-a[2]*p[1]; ax[1] = a[2]*p[0]-a[0]*p[2]; ax[2] = a[0]*p[1]-a[1]*p[0];
		v3_norm( ax );
		memset( o, 0, sizeof( m4 ));
		for( int r = 0; r < 3; r++ )
			for( int cc = 0; cc < 3; cc++ )
				o[cc*4+r] = 2.0f*ax[r]*ax[cc] - ( r == cc ? 1.0f : 0.0f );
		o[15] = 1.0f;
		return;
	}
	for( int i = 0; i < 3; i++ ) ax[i] /= s;
	const float x = ax[0], y = ax[1], z = ax[2], ic = 1.0f - c;
	memset( o, 0, sizeof( m4 ));
	o[0] = c + x*x*ic;      o[4] = x*y*ic - z*s;   o[8]  = x*z*ic + y*s;
	o[1] = y*x*ic + z*s;    o[5] = c + y*y*ic;     o[9]  = y*z*ic - x*s;
	o[2] = z*x*ic - y*s;    o[6] = z*y*ic + x*s;   o[10] = c + z*z*ic;
	o[15] = 1.0f;
}

static void translate_matrix( m4 o, const float t[3] )
{
	m4_ident( o );
	o[12]=t[0]; o[13]=t[1]; o[14]=t[2];
}

// ------------------------------------------------------------ malha --------
typedef struct
{
	float p[3][3];
	float n[3][3];
	float uv[3][2];
	unsigned char j[3][4];
	float w[3][4];
	float tint[3];           // cor do material (arma); (1,1,1) nos bracos
} htri_t;

typedef struct
{
	char  name[64];
	float local[16];
	int   parent;
	float ibm[16];
} hjoint_t;

typedef struct
{
	htri_t  *tri;
	int      ntri, cap;
	unsigned tex;
	bool     skinned;
	hjoint_t *joint;
	int      njoint;
	bool     loaded;
	int      nverts;
	int      nmats;
	m4      *rest_world;                       // mundo de cada osso no repouso
	int      j_root, j_head;
	int      j_bicep[2], j_fore[2], j_wrist[2], j_socket[2];
} hmesh_t;

static int hjoint_find( hmesh_t *m, const char *name )
{
	for( int i = 0; i < m->njoint; i++ )
		if( !strcmp( m->joint[i].name, name )) return i;
	return -1;
}

// Mundo de um osso com um conjunto de locais CORRENTE (a pose do frame).
static void cur_world( hmesh_t *m, m4 *cur, int idx, m4 out )
{
	if( idx < 0 ) { m4_ident( out ); return; }
	if( m->joint[idx].parent >= 0 )
	{
		m4 p;
		cur_world( m, cur, m->joint[idx].parent, p );
		m4_mul( out, p, cur[idx] );
	}
	else memcpy( out, cur[idx], sizeof( m4 ));
}

static void mesh_clear( hmesh_t *m )
{
	free( m->tri ); free( m->joint ); free( m->rest_world );
	if( m->tex && p_glDeleteTextures ) { GLuint t = m->tex; p_glDeleteTextures( 1, &t ); }
	memset( m, 0, sizeof( *m ));
}

static htri_t *mesh_push( hmesh_t *m )
{
	if( m->ntri == m->cap )
	{
		m->cap = m->cap ? m->cap * 2 : 1024;
		m->tri = ( htri_t * )realloc( m->tri, ( size_t )m->cap * sizeof( htri_t ));
	}
	htri_t *t = &m->tri[m->ntri++];
	memset( t, 0, sizeof( *t ));
	t->tint[0]=t->tint[1]=t->tint[2]=1.0f;
	return t;
}

// -------------------------------------------------------------- cfg --------
typedef struct
{
	bool  enable;
	char  arms_glb[160];
	char  arms_tex[160];
	char  weapon_obj[160];
	float scale_arms, scale_weapon;
	float test_dist, test_side, test_height, test_yaw, test_pitch;
	bool  test_nodepth;      // diagnostico: ignora profundidade (sempre visivel)
	float arm_pitch, arm_yaw, arm_roll;
	float wpn_pitch, wpn_yaw, wpn_roll;
	float wpn_off[3];
	float rig_yaw;           // giro fino do conjunto (graus)
	bool  synth_hands;       // diagnostico: maos sinteticas (sem headset)
	float near_clip, far_clip, color_gamma;
	bool  debug;
} hcfg_t;

static hcfg_t cfg;
static double cfg_mtime;

static void cfg_defaults( void )
{
	memset( &cfg, 0, sizeof( cfg ));
	cfg.enable = true;
	snprintf( cfg.arms_glb, sizeof( cfg.arms_glb ), "mods/vr_assets/arms.glb" );
	snprintf( cfg.arms_tex, sizeof( cfg.arms_tex ), "mods/vr_assets/arm_albedo_pale.png" );
	snprintf( cfg.weapon_obj, sizeof( cfg.weapon_obj ), "mods/vr_assets/Pistol_1.obj" );
	cfg.scale_arms = 4.2f;   // ver o cabecalho do vr_hands.cfg (rig ~5,2 un. de alcance)
	cfg.scale_weapon = 4.2f;
	cfg.test_dist = 40.0f; cfg.test_side = 0.0f; cfg.test_height = 0.0f;
	cfg.test_nodepth = true;
	cfg.near_clip = 4.0f;        // igual ao ref (gl_rmain.c: zNear = 4.0f)
	cfg.far_clip = 7086.0f;      // ~ movevars.zmax * 1.73 (idem)
	cfg.color_gamma = 2.2f;
	cfg.synth_hands = false;
	cfg.rig_yaw = 0.0f;
	cfg.wpn_off[0]=cfg.wpn_off[1]=cfg.wpn_off[2]=0.0f;
}

static void cfg_parse( const char *path )
{
	FILE *f = fopen( path, "r" );
	if( !f ) { vlog( "maos: cfg ausente (%s) — padroes\n", path ); return; }
	char line[512];
	while( fgets( line, sizeof( line ), f ))
	{
		char *h = strchr( line, '#' );
		if( h ) *h = 0;
		char key[64], val[200];
		if( sscanf( line, "%63s %199[^\n]", key, val ) != 2 ) continue;
		if     ( !strcmp( key, "enable" ))       cfg.enable = atoi( val ) != 0;
		else if( !strcmp( key, "arms_glb" ))     snprintf( cfg.arms_glb, sizeof( cfg.arms_glb ), "%s", val );
		else if( !strcmp( key, "arms_tex" ))     snprintf( cfg.arms_tex, sizeof( cfg.arms_tex ), "%s", val );
		else if( !strcmp( key, "weapon_obj" ))   snprintf( cfg.weapon_obj, sizeof( cfg.weapon_obj ), "%s", val );
		else if( !strcmp( key, "scale_arms" ))   cfg.scale_arms = ( float )atof( val );
		else if( !strcmp( key, "scale_weapon" )) cfg.scale_weapon = ( float )atof( val );
		else if( !strcmp( key, "test_dist" ))    cfg.test_dist = ( float )atof( val );
		else if( !strcmp( key, "test_side" ))    cfg.test_side = ( float )atof( val );
		else if( !strcmp( key, "test_height" ))  cfg.test_height = ( float )atof( val );
		else if( !strcmp( key, "test_yaw" ))     cfg.test_yaw = ( float )atof( val );
		else if( !strcmp( key, "test_pitch" ))   cfg.test_pitch = ( float )atof( val );
		else if( !strcmp( key, "test_nodepth" )) cfg.test_nodepth = atoi( val ) != 0;
		else if( !strcmp( key, "test_flat" ))    g_dbg_flat = atoi( val ) != 0;
		else if( !strcmp( key, "synth_hands" ))  cfg.synth_hands = atoi( val ) != 0;
		else if( !strcmp( key, "rig_yaw" ))      cfg.rig_yaw = ( float )atof( val );
		else if( !strcmp( key, "weapon_off" ))   sscanf( val, "%f %f %f", &cfg.wpn_off[0], &cfg.wpn_off[1], &cfg.wpn_off[2] );
		else if( !strcmp( key, "arm_euler" ))    sscanf( val, "%f %f %f", &cfg.arm_pitch, &cfg.arm_yaw, &cfg.arm_roll );
		else if( !strcmp( key, "weapon_euler" )) sscanf( val, "%f %f %f", &cfg.wpn_pitch, &cfg.wpn_yaw, &cfg.wpn_roll );
		else if( !strcmp( key, "near" ))         cfg.near_clip = ( float )atof( val );
		else if( !strcmp( key, "far" ))          cfg.far_clip = ( float )atof( val );
		else if( !strcmp( key, "color_gamma" ))  cfg.color_gamma = ( float )atof( val );
		else if( !strcmp( key, "debug" ))        cfg.debug = atoi( val ) != 0;
		else vlog( "maos: cfg: chave ignorada '%s'\n", key );
	}
	fclose( f );
}

// --------------------------------------------------------- estado ----------
static hmesh_t g_arms, g_weapon;
static bool    g_ready;
static float   g_recoil;
static unsigned g_draws;

// ---------------------------------------------------------- textura --------
static unsigned tex_from_memory( const unsigned char *bytes, int len )
{
	int w = 0, h = 0, comp = 0;
	stbi_uc *px = stbi_load_from_memory( bytes, len, &w, &h, &comp, 4 );
	if( !px ) { vlog( "maos: textura nao decodificou (%s)\n", stbi_failure_reason()); return 0; }
	GLuint t = 0;
	p_glGenTextures( 1, &t );
	p_glBindTexture( GL_TEXTURE_2D, t );
	p_glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	p_glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	p_glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	p_glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
	p_glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
	p_glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px );
	p_glBindTexture( GL_TEXTURE_2D, 0 );
	stbi_image_free( px );
	return t;
}

static bool file_read( const char *path, unsigned char **out, int *outlen )
{
	FILE *f = fopen( path, "rb" );
	if( !f ) return false;
	fseek( f, 0, SEEK_END );
	long n = ftell( f );
	fseek( f, 0, SEEK_SET );
	if( n <= 0 ) { fclose( f ); return false; }
	unsigned char *b = ( unsigned char * )malloc( ( size_t )n );
	if( !b ) { fclose( f ); return false; }
	size_t rd = fread( b, 1, ( size_t )n, f );
	fclose( f );
	if( rd != ( size_t )n ) { free( b ); return false; }
	*out = b; *outlen = ( int )n;
	return true;
}

// ------------------------------------------------------------ glTF ---------
static void joint_world( hmesh_t *m, int idx, m4 out )
{
	if( idx < 0 ) { m4_ident( out ); return; }
	if( m->joint[idx].parent >= 0 )
	{
		m4 p;
		joint_world( m, m->joint[idx].parent, p );
		m4_mul( out, p, m->joint[idx].local );
	}
	else memcpy( out, m->joint[idx].local, sizeof( m4 ));
}

static bool load_gltf( hmesh_t *m, const char *path )
{
	cgltf_options opt; memset( &opt, 0, sizeof( opt ));
	cgltf_data *d = NULL;
	if( cgltf_parse_file( &opt, path, &d ) != cgltf_result_success || !d )
	{ vlog( "maos: falhei ao parsear '%s'\n", path ); return false; }
	if( cgltf_load_buffers( &opt, d, path ) != cgltf_result_success )
	{ vlog( "maos: falhei nos buffers de '%s'\n", path ); cgltf_free( d ); return false; }

	cgltf_skin *sk = d->skins_count ? &d->skins[0] : NULL;
	if( sk )
	{
		m->skinned = true;
		m->njoint = ( int )sk->joints_count;
		m->joint = ( hjoint_t * )calloc( ( size_t )m->njoint, sizeof( hjoint_t ));
		for( int i = 0; i < m->njoint; i++ )
		{
			cgltf_node *n = sk->joints[i];
			snprintf( m->joint[i].name, sizeof( m->joint[i].name ), "%s", n->name ? n->name : "?" );
			cgltf_node_transform_local( n, m->joint[i].local );
			m->joint[i].parent = -1;
			for( int k = 0; k < m->njoint; k++ )
				if( sk->joints[k] == n->parent ) { m->joint[i].parent = k; break; }
			if( sk->inverse_bind_matrices )
				cgltf_accessor_read_float( sk->inverse_bind_matrices, ( cgltf_size )i, m->joint[i].ibm, 16 );
			else m4_ident( m->joint[i].ibm );
		}

		// mundo de repouso + os ossos de que a pose depende (por nome)
		m->rest_world = ( m4 * )calloc( ( size_t )m->njoint, sizeof( m4 ));
		for( int i = 0; i < m->njoint; i++ ) joint_world( m, i, m->rest_world[i] );
		m->j_root  = hjoint_find( m, "root" );
		m->j_head  = hjoint_find( m, "head" );
		m->j_bicep[0] = hjoint_find( m, "bicep.l" );  m->j_bicep[1] = hjoint_find( m, "bicep.r" );
		m->j_fore[0]  = hjoint_find( m, "forearm.l" );m->j_fore[1]  = hjoint_find( m, "forearm.r" );
		m->j_wrist[0] = hjoint_find( m, "wrist.l" );  m->j_wrist[1] = hjoint_find( m, "wrist.r" );
		m->j_socket[0]= hjoint_find( m, "socket.l" ); m->j_socket[1]= hjoint_find( m, "socket.r" );
		vlog( "maos: ossos ref root=%d head=%d bicep=%d/%d fore=%d/%d wrist=%d/%d socket=%d/%d\n",
			m->j_root, m->j_head, m->j_bicep[0], m->j_bicep[1], m->j_fore[0], m->j_fore[1],
			m->j_wrist[0], m->j_wrist[1], m->j_socket[0], m->j_socket[1] );
	}

	for( cgltf_size mi = 0; mi < d->meshes_count && m->ntri == 0; mi++ )
	{
		for( cgltf_size pi = 0; pi < d->meshes[mi].primitives_count; pi++ )
		{
			cgltf_primitive *pr = &d->meshes[mi].primitives[pi];
			cgltf_accessor *ap=NULL, *an=NULL, *auv=NULL, *aj=NULL, *aw=NULL;
			for( cgltf_size a = 0; a < pr->attributes_count; a++ )
			{
				cgltf_attribute *at = &pr->attributes[a];
				switch( at->type )
				{
				case cgltf_attribute_type_position: ap = at->data; break;
				case cgltf_attribute_type_normal:   an = at->data; break;
				case cgltf_attribute_type_texcoord: if( at->index == 0 ) auv = at->data; break;
				case cgltf_attribute_type_joints:   if( at->index == 0 ) aj = at->data; break;
				case cgltf_attribute_type_weights:  if( at->index == 0 ) aw = at->data; break;
				default: break;
				}
			}
			if( !ap ) continue;
			m->nverts = ( int )ap->count;
			const cgltf_size icount = pr->indices ? pr->indices->count : ap->count;

			for( cgltf_size i = 0; i + 2 < icount; i += 3 )
			{
				htri_t *t = mesh_push( m );
				for( int k = 0; k < 3; k++ )
				{
					cgltf_size vi = pr->indices ? cgltf_accessor_read_index( pr->indices, i + ( cgltf_size )k ) : i + ( cgltf_size )k;
					cgltf_accessor_read_float( ap, vi, t->p[k], 3 );
					if( an ) cgltf_accessor_read_float( an, vi, t->n[k], 3 );
					else { t->n[k][0]=0; t->n[k][1]=1; t->n[k][2]=0; }
					if( auv ) cgltf_accessor_read_float( auv, vi, t->uv[k], 2 );
					if( aj && aw )
					{
						cgltf_uint ji[4] = {0,0,0,0};
						float wt[4] = {0,0,0,0};
						cgltf_accessor_read_uint( aj, vi, ji, 4 );
						cgltf_accessor_read_float( aw, vi, wt, 4 );
						for( int q = 0; q < 4; q++ ) { t->j[k][q] = ( unsigned char )ji[q]; t->w[k][q] = wt[q]; }
					}
				}
			}
		}
	}

	if( d->images_count )
	{
		cgltf_image *im = &d->images[0];
		if( im->buffer_view )
		{
			const unsigned char *b = ( const unsigned char * )im->buffer_view->buffer->data + im->buffer_view->offset;
			m->tex = tex_from_memory( b, ( int )im->buffer_view->size );
		}
	}

	cgltf_free( d );
	vlog( "maos: glTF '%s' -> %d tri, %d verts, %d ossos, skin=%s, tex=%u\n",
		path, m->ntri, m->nverts, m->njoint, m->skinned ? "sim" : "nao", m->tex );
	m->loaded = m->ntri > 0;
	return m->loaded;
}

// -------------------------------------------------------------- OBJ ---------
typedef struct { char name[64]; float kd[3]; } objmat_t;

static bool load_obj( hmesh_t *m, const char *objpath, const char *mtlpath )
{
	objmat_t mats[32];
	int nmats = 0;
	if( mtlpath && *mtlpath )
	{
		FILE *f = fopen( mtlpath, "r" );
		if( f )
		{
			char line[256]; objmat_t *cur = NULL;
			while( fgets( line, sizeof( line ), f ))
			{
				if( !strncmp( line, "newmtl ", 7 ) && nmats < 32 )
				{
					cur = &mats[nmats++];
					snprintf( cur->name, sizeof( cur->name ), "%63s", line + 7 );
					char *nl = strpbrk( cur->name, "\r\n" ); if( nl ) *nl = 0;
					cur->kd[0]=cur->kd[1]=cur->kd[2]=0.5f;
				}
				else if( !strncmp( line, "Kd ", 3 ) && cur )
					sscanf( line + 3, "%f %f %f", &cur->kd[0], &cur->kd[1], &cur->kd[2] );
			}
			fclose( f );
		}
	}
	// O Kd do MTL e linear e MUITO escuro (ex.: 0,053). Converte para sRGB.
	const float g = cfg.color_gamma > 0.1f ? cfg.color_gamma : 2.2f;
	for( int i = 0; i < nmats; i++ )
		for( int c = 0; c < 3; c++ )
		{
			float v = mats[i].kd[c]; if( v < 0 ) v = 0;
			mats[i].kd[c] = powf( v, 1.0f / g );
		}

	FILE *f = fopen( objpath, "r" );
	if( !f ) { vlog( "maos: sem '%s'\n", objpath ); return false; }

	float *vp = NULL, *vn = NULL;
	int nvp=0, cvp=0, nvn=0, cvn=0;
	float curkd[3] = { 0.6f, 0.6f, 0.6f };
	char line[1024];
	int fv[64], fn[64];

	while( fgets( line, sizeof( line ), f ))
	{
		if( line[0]=='v' && line[1]==' ' )
		{
			if( nvp == cvp ) { cvp = cvp ? cvp*2 : 4096; vp = ( float * )realloc( vp, ( size_t )cvp*3*sizeof(float)); }
			if( sscanf( line+2, "%f %f %f", &vp[nvp*3], &vp[nvp*3+1], &vp[nvp*3+2] ) == 3 ) nvp++;
		}
		else if( line[0]=='v' && line[1]=='n' )
		{
			if( nvn == cvn ) { cvn = cvn ? cvn*2 : 4096; vn = ( float * )realloc( vn, ( size_t )cvn*3*sizeof(float)); }
			if( sscanf( line+3, "%f %f %f", &vn[nvn*3], &vn[nvn*3+1], &vn[nvn*3+2] ) == 3 ) nvn++;
		}
		else if( !strncmp( line, "usemtl ", 7 ))
		{
			char nm[64]; snprintf( nm, sizeof( nm ), "%63s", line+7 );
			char *nl = strpbrk( nm, "\r\n" ); if( nl ) *nl = 0;
			curkd[0]=curkd[1]=curkd[2]=0.6f;
			for( int i = 0; i < nmats; i++ )
				if( !strcmp( mats[i].name, nm ))
				{ curkd[0]=mats[i].kd[0]; curkd[1]=mats[i].kd[1]; curkd[2]=mats[i].kd[2]; break; }
		}
		else if( line[0]=='f' && line[1]==' ' )
		{
			int nn = 0;
			char *tok = strtok( line+2, " \t\r\n" );
			while( tok && nn < 64 )
			{
				int a=0,b=0,c=0,n=0;
				if( sscanf( tok, "%d/%d/%d", &a,&b,&c ) == 3 ) n = c;
				else if( sscanf( tok, "%d//%d", &a,&c ) == 2 )  n = c;
				else { b = 0; if( sscanf( tok, "%d/%d", &a,&b ) != 2 ) { b=0; sscanf( tok, "%d", &a ); } }
				fv[nn] = a; fn[nn] = n;
				nn++;
				tok = strtok( NULL, " \t\r\n" );
			}
			for( int k = 1; k + 1 < nn; k++ )
			{
				const int tri[3] = { fv[0], fv[k], fv[k+1] };
				const int tnr[3] = { fn[0], fn[k], fn[k+1] };
				htri_t *t = mesh_push( m );
				memcpy( t->tint, curkd, 3*sizeof(float));
				for( int q = 0; q < 3; q++ )
				{
					int vi = tri[q] > 0 ? tri[q]-1 : nvp + tri[q];
					if( vi >= 0 && vi < nvp )
					{
						t->p[q][0]=vp[vi*3]; t->p[q][1]=vp[vi*3+1]; t->p[q][2]=vp[vi*3+2];
					}
					int ni = tnr[q] > 0 ? tnr[q]-1 : nvn + tnr[q];
					if( tnr[q] && ni >= 0 && ni < nvn )
					{
						t->n[q][0]=vn[ni*3]; t->n[q][1]=vn[ni*3+1]; t->n[q][2]=vn[ni*3+2];
					}
				}
				// sem normal no arquivo: normal geometrica da face
				if( !tnr[0] || !tnr[1] || !tnr[2] )
				{
					float u[3], v[3], nrm[3];
					for( int c = 0; c < 3; c++ ) { u[c]=t->p[1][c]-t->p[0][c]; v[c]=t->p[2][c]-t->p[0][c]; }
					nrm[0]=u[1]*v[2]-u[2]*v[1]; nrm[1]=u[2]*v[0]-u[0]*v[2]; nrm[2]=u[0]*v[1]-u[1]*v[0];
					v3_norm( nrm );
					for( int q = 0; q < 3; q++ ) memcpy( t->n[q], nrm, 3*sizeof(float));
				}
			}
		}
	}
	fclose( f );
	free( vp ); free( vn );
	m->nverts = nvp; m->nmats = nmats;
	vlog( "maos: OBJ '%s' -> %d tri, %d verts, %d materiais (cor solida)\n", objpath, m->ntri, nvp, nmats );
	m->loaded = m->ntri > 0;
	return m->loaded;
}

// ------------------------------------------------------------ desenho ------
static void compute_skin( hmesh_t *m, m4 *out )
{
	for( int i = 0; i < m->njoint; i++ )
	{
		m4 w;
		joint_world( m, i, w );
		m4_mul( out[i], w, m->joint[i].ibm );
	}
}

// Sombreamento simples: luz fixa na direcao da camera + um pouco de cima.
static float shade_of( const float n[3], const float light[3] )
{
	float d = n[0]*light[0] + n[1]*light[1] + n[2]*light[2];
	if( d < 0.0f ) d = 0.0f;
	return 0.42f + 0.58f * d;
}

static void draw_mesh( hmesh_t *m, const m4 model, const m4 *skin, const float light[3] )
{
	const bool tex = m->tex != 0 && !g_dbg_flat;
	if( tex )
	{
		p_glEnable( GL_TEXTURE_2D );
		p_glBindTexture( GL_TEXTURE_2D, m->tex );
		p_glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	}
	else p_glDisable( GL_TEXTURE_2D );

	p_glBegin( GL_TRIANGLES );
	for( int i = 0; i < m->ntri; i++ )
	{
		const htri_t *t = &m->tri[i];
		for( int k = 0; k < 3; k++ )
		{
			float p[3] = { t->p[k][0], t->p[k][1], t->p[k][2] };
			float n[3] = { t->n[k][0], t->n[k][1], t->n[k][2] };

			if( skin && m->skinned )
			{
				float ps[3]={0,0,0}, ns[3]={0,0,0};
				for( int q = 0; q < 4; q++ )
				{
					float w = t->w[k][q];
					if( w <= 0.0f ) continue;
					const m4 *jm = &skin[t->j[k][q]];
					float tp[3], tn[3];
					m4_xform_point( *jm, p, tp );
					m4_xform_dir( *jm, n, tn );
					for( int c = 0; c < 3; c++ ) { ps[c] += w*tp[c]; ns[c] += w*tn[c]; }
				}
				memcpy( p, ps, sizeof( p ));
				memcpy( n, ns, sizeof( n ));
			}

			float wp[3], wn[3];
			m4_xform_point( model, p, wp );
			m4_xform_dir( model, n, wn );
			v3_norm( wn );

			const float s = shade_of( wn, light );
			if( g_dbg_flat ) p_glColor4f( 1.0f, 0.1f, 0.1f, 1.0f );
			else p_glColor4f( t->tint[0]*s, t->tint[1]*s, t->tint[2]*s, 1.0f );
			p_glNormal3f( wn[0], wn[1], wn[2] );
			if( tex ) p_glTexCoord2f( t->uv[k][0], t->uv[k][1] );
			p_glVertex3f( wp[0], wp[1], wp[2] );
			g_vcount++;
			for( int c = 0; c < 3; c++ ) { if( wp[c]<g_vlo[c] ) g_vlo[c]=wp[c]; if( wp[c]>g_vhi[c] ) g_vhi[c]=wp[c]; }
			{
				float dd[3] = { wp[0]-g_cam_org[0], wp[1]-g_cam_org[1], wp[2]-g_cam_org[2] };
				float dm = sqrtf( dd[0]*dd[0]+dd[1]*dd[1]+dd[2]*dd[2] );
				if( dm < g_min_dist ) g_min_dist = dm;
			}
		}
	}
	p_glEnd();
	if( tex ) p_glDisable( GL_TEXTURE_2D );
}

static void proj_matrix( m4 o, const vr_hands_cam_t *cam )
{
	const float tL = tanf( cam->fov[0] ), tR = tanf( cam->fov[1] );
	const float tU = tanf( cam->fov[2] ), tD = tanf( cam->fov[3] );
	const float n = cfg.near_clip, f = cfg.far_clip;
	memset( o, 0, sizeof( m4 ));
	o[0]  = 2.0f / ( tR - tL );
	o[5]  = 2.0f / ( tU - tD );
	o[8]  = ( tR + tL ) / ( tR - tL );
	o[9]  = ( tU + tD ) / ( tU - tD );
	o[10] = -( f + n ) / ( f - n );
	o[11] = -1.0f;
	o[14] = -2.0f * f * n / ( f - n );
}

static void view_matrix( m4 o, const vr_hands_cam_t *cam )
{
	float fwd[3], rgt[3], up[3];
	cam_basis( cam, fwd, rgt, up );
	const float nf[3] = { -fwd[0], -fwd[1], -fwd[2] };
	const float *org = cam->origin;

	// A visao e a INVERSA da transformacao da camera: a rotacao entra TRANSPOSTA
	// (as linhas sao a base da camera) e a translacao e -(R * origem).
	o[0]=rgt[0]; o[1]=up[0]; o[2]=nf[0]; o[3]=0;
	o[4]=rgt[1]; o[5]=up[1]; o[6]=nf[1]; o[7]=0;
	o[8]=rgt[2]; o[9]=up[2]; o[10]=nf[2]; o[11]=0;
	o[12] = -( rgt[0]*org[0] + rgt[1]*org[1] + rgt[2]*org[2] );
	o[13] = -( up[0]*org[0]  + up[1]*org[1]  + up[2]*org[2] );
	o[14] = -( nf[0]*org[0]  + nf[1]*org[1]  + nf[2]*org[2] );
	o[15] = 1.0f;
}

static void apply_3d_state( const vr_hands_cam_t *cam )
{
	p_glPushAttrib( GL_ALL_ATTRIB_BITS );
	m4 pj, vw;
	proj_matrix( pj, cam );
	view_matrix( vw, cam );
	p_glMatrixMode( GL_PROJECTION ); p_glPushMatrix(); p_glLoadMatrixf( pj );
	p_glMatrixMode( GL_MODELVIEW );  p_glPushMatrix(); p_glLoadMatrixf( vw );
	p_glViewport( 0, 0, cam->w, cam->h );
	p_glEnable( GL_DEPTH_TEST );
	p_glDepthFunc( GL_LEQUAL );
	p_glDepthMask( GL_TRUE );
	p_glDepthRange( 0.0, 1.0 );   // desfaz o squeeze de viewmodel do engine
	p_glShadeModel( GL_SMOOTH );
	p_glDisable( GL_CULL_FACE );
	p_glDisable( GL_BLEND );

	// CRITICO: o ref_gl desenha o mundo com VERTEX ARRAYS. No modo imediato
	// (glBegin/glVertex3f) com arrays de cliente ligados o GL DESCARTA os
	// vertices — o desenho vira residuo (ou nada). Aqui desligamos tudo antes.
	if( p_glDisableClientState )
	{
		p_glDisableClientState( GL_VERTEX_ARRAY );
		p_glDisableClientState( GL_NORMAL_ARRAY );
		p_glDisableClientState( GL_COLOR_ARRAY );
		p_glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	}
	if( p_glDisableVertexAttribArray )
		for( GLuint i = 0; i < 16; i++ ) p_glDisableVertexAttribArray( i );
}

static void restore_3d_state( void )
{
	p_glMatrixMode( GL_PROJECTION ); p_glPopMatrix();
	p_glMatrixMode( GL_MODELVIEW );  p_glPopMatrix();
	p_glPopAttrib();
}

// ------------------------------------------------------------ publico ------
bool vr_hands_ready( void ) { return g_ready; }

void vr_hands_trigger_recoil( void ) { g_recoil = 1.0f; }

bool vr_hands_covers_model( const char *model_name )
{
	if( !cfg.enable || !g_ready || !model_name ) return false;
	static const char *pistols[] = { "v_usp", "v_glock18", "v_p228", "v_deagle", "v_elite", "v_fiveseven" };
	for( size_t i = 0; i < sizeof( pistols )/sizeof( pistols[0] ); i++ )
		if( strstr( model_name, pistols[i] )) return true;
	return false;
}

void vr_hands_reload( void )
{
	mesh_clear( &g_arms );
	mesh_clear( &g_weapon );
	g_ready = false;

	if( !load_gltf( &g_arms, cfg.arms_glb )) return;
	if( g_arms.tex == 0 && cfg.arms_tex[0] )
	{
		unsigned char *b = NULL; int n = 0;
		if( file_read( cfg.arms_tex, &b, &n )) { g_arms.tex = tex_from_memory( b, n ); free( b ); }
	}
	char mtl[168];
	snprintf( mtl, sizeof( mtl ), "%s", cfg.weapon_obj );
	char *dot = strrchr( mtl, '.' );
	if( dot ) snprintf( dot, ( size_t )( mtl + sizeof( mtl ) - dot ), ".mtl" );
	load_obj( &g_weapon, cfg.weapon_obj, mtl );

	g_ready = g_arms.loaded;
}

bool vr_hands_init( vr_hands_log_fn log )
{
	g_log = log;
	cfg_defaults();
	cfg_parse( "mods/vr_hands.cfg" );
	if( !gl_load()) return false;
	vr_hands_reload();
	if( g_ready ) vlog( "maos: pronto — bracos %d tri / arma %d tri\n", g_arms.ntri, g_weapon.ntri );
	else vlog( "maos: indisponivel (asset nao carregou) — o mod segue como antes\n" );
	return g_ready;
}

void vr_hands_shutdown( void ) { mesh_clear( &g_arms ); mesh_clear( &g_weapon ); g_ready = false; }

void vr_hands_tick( void )
{
	struct stat st;
	if( stat( "mods/vr_hands.cfg", &st ) != 0 ) return;
	if( ( double )st.st_mtime != cfg_mtime )
	{
		cfg_mtime = ( double )st.st_mtime;
		cfg_parse( "mods/vr_hands.cfg" );
		vlog( "maos: cfg relido\n" );
	}
}

// Modelo parado na frente da camera (diagnostico sem headset).
void vr_hands_draw_test( const vr_hands_cam_t *cam )
{
	if( !g_ready || !cam || !cam->ok || cam->w <= 0 ) return;
	apply_3d_state( cam );

	float fwd[3], rgt[3], up[3];
	cam_basis( cam, fwd, rgt, up );
	const float negr[3] = { -rgt[0], -rgt[1], -rgt[2] };
	const float zero[3] = { 0.0f, 0.0f, 0.0f };

	m4 Rm, Ro, Sc, Cv, t1, t2, model;
	m4_from_axes( Rm, fwd, negr, up, zero );
	euler_matrix( Ro, cfg.test_pitch, cfg.test_yaw, cfg.arm_roll );
	scale_matrix( Sc, cfg.scale_arms );
	conv_axis_matrix( Cv );

	float pos[3];
	for( int i = 0; i < 3; i++ )
		pos[i] = cam->origin[i] + fwd[i]*cfg.test_dist + rgt[i]*cfg.test_side + up[i]*cfg.test_height;

	m4 Tm; translate_matrix( Tm, pos );
	m4_mul( t1, Tm, Rm );
	m4_mul( t2, t1, Ro );
	m4_mul( t1, t2, Cv );
	m4_mul( model, t1, Sc );

	m4 *skin = g_arms.skinned ? ( m4 * )malloc( ( size_t )g_arms.njoint * sizeof( m4 )) : NULL;
	if( skin ) compute_skin( &g_arms, skin );

	const float light[3] = { fwd[0], fwd[1], fwd[2] };

	// Diagnostico: sem profundidade o modelo aparece mesmo atras de parede — e o
	// que separa "nao desenhou" de "esta ocluido" (o dust2 tem parede a 20 unidades).
	if( cfg.test_nodepth ) { p_glDisable( GL_DEPTH_TEST ); p_glDepthMask( GL_FALSE ); }

	draw_mesh( &g_arms, model, skin, light );
	free( skin );

	if( g_draws < 3 )
		vlog( "maos dbg: draw %u verts_enviados=%ld bbox_GL (%.2f %.2f %.2f)..(%.2f %.2f %.2f) tam(%.2f %.2f %.2f) cam(%.0f %.0f %.0f)\n",
			g_draws, g_vcount,
			( double )g_vlo[0], ( double )g_vlo[1], ( double )g_vlo[2],
			( double )g_vhi[0], ( double )g_vhi[1], ( double )g_vhi[2],
			( double )( g_vhi[0]-g_vlo[0] ), ( double )( g_vhi[1]-g_vlo[1] ), ( double )( g_vhi[2]-g_vlo[2] ),
			( double )cam->origin[0], ( double )cam->origin[1], ( double )cam->origin[2] );

	// A arma e desenhada ao LADO, no mesmo teste: prova que o OBJ tambem entra
	// (na Fase 2 ela vai no socket da mao, nao aqui).
	{
		const float off[3] = { rgt[0]*16.0f, rgt[1]*16.0f, rgt[2]*16.0f };
		m4 Tw, m2;
		translate_matrix( Tw, off );
		m4_mul( m2, Tw, model );
		draw_mesh( &g_weapon, m2, NULL, light );
	}

	restore_3d_state();

	if( g_draws < 3 )
	{
		m4 pj; proj_matrix( pj, cam );
		vlog( "maos dbg: draw %u fov=(%.4f %.4f %.4f %.4f) alvo=%dx%d proj diag=(%.3f %.3f) "
			"near/far=%.0f/%.0f verts=%ld\n",
			g_draws, ( double )cam->fov[0], ( double )cam->fov[1], ( double )cam->fov[2], ( double )cam->fov[3],
			cam->w, cam->h, ( double )pj[0], ( double )pj[5],
			( double )cfg.near_clip, ( double )cfg.far_clip, g_vcount );
	}
	g_draws++;
}

void vr_hands_draw( const vr_hands_cam_t *cam, const vr_hands_hand_t hands[2] )
{
	if( !g_ready || !cam || !cam->ok || cam->w <= 0 ) return;
	if( !g_arms.rest_world || g_arms.j_bicep[0] < 0 || g_arms.j_bicep[1] < 0 )
	{ vr_hands_draw_test( cam ); return; }

	// poses a usar: as reais, ou sinteticas (diagnostico sem headset)
	vr_hands_hand_t H[2];
	float fwd[3], rgt[3], up[3];
	cam_basis( cam, fwd, rgt, up );
	if( cfg.synth_hands || !hands || !hands[0].ok || !hands[1].ok )
	{
		// duas maos a frente, a direita com a arma e a esquerda de apoio —
		// e o que permite ver a IK trabalhando SEM headset.
		for( int h = 0; h < 2; h++ )
		{
			const float fw = 24.0f, sd = ( h == 1 ? 5.5f : 1.5f ), dn = 9.0f;
			for( int i = 0; i < 3; i++ )
				H[h].pos[i] = cam->origin[i] + fwd[i]*fw + rgt[i]*sd - up[i]*dn;
			// a base da mao aponta junto do antebraco (aproxima o "punho")
			for( int i = 0; i < 3; i++ ) { H[h].fwd[i] = fwd[i]; H[h].rgt[i] = rgt[i]; H[h].up[i] = up[i]; }
			H[h].ok = true;
		}
	}
	else { H[0] = hands[0]; H[1] = hands[1]; }

	apply_3d_state( cam );
	if( cfg.test_nodepth ) { p_glDisable( GL_DEPTH_TEST ); p_glDepthMask( GL_FALSE ); }

	// ---- 1) onde o conjunto (armature) fica no mundo ----
	// Gira com o yaw da cabeca e ancora o osso `head` do rig NA posicao do HMD:
	// assim os ombros caem no lugar certo sem numeros magicos.
	m4 Rz, Marm, Cv, Sc, t1;
	const float d2r = ( float )M_PI / 180.0f;
	const float cy = cosf( cam->angles[1]*d2r ), sy = sinf( cam->angles[1]*d2r );
	const float yaw_eff = ( cam->angles[1] + cfg.rig_yaw ) * d2r;
	const float cy2 = cosf( yaw_eff ), sy2 = sinf( yaw_eff );
	(void)cy; (void)sy;
	memset( Rz, 0, sizeof( m4 ));
	Rz[0]=cy2; Rz[1]=sy2; Rz[4]=-sy2; Rz[5]=cy2; Rz[10]=1.0f; Rz[15]=1.0f;
	scale_matrix( Sc, cfg.scale_arms );
	conv_axis_matrix( Cv );
	m4_mul( t1, Rz, Cv );
	m4_mul( Marm, t1, Sc );
	{
		float hp[3];
		m4_xform_point( Marm, g_arms.rest_world[g_arms.j_head], hp );
		for( int i = 0; i < 3; i++ ) Marm[12+i] = cam->origin[i] - hp[i];
	}

	// ---- 2) poses correntes: comeca do repouso e sobrescreve a raiz ----
	m4 *cur = ( m4 * )malloc( ( size_t )g_arms.njoint * sizeof( m4 ));
	for( int i = 0; i < g_arms.njoint; i++ ) memcpy( cur[i], g_arms.joint[i].local, sizeof( m4 ));
	{
		m4 inv0;
		// a raiz nao tem pai-osso: o local dela E o mundo
		m4_mul( cur[g_arms.j_root], Marm, g_arms.rest_world[g_arms.j_root] );
		(void)inv0;
	}

	// ---- 3) IK de dois ossos por braco ----
	const float poleWorld[3] = { 0.0f, 0.0f, -1.0f }; // para baixo
	for( int h = 0; h < 2; h++ )
	{
		const int jb = g_arms.j_bicep[h], jf = g_arms.j_fore[h], jw = g_arms.j_wrist[h];
		m4 Wb, Wf, Ww;
		cur_world( &g_arms, cur, jb, Wb );
		cur_world( &g_arms, cur, jf, Wf );
		cur_world( &g_arms, cur, jw, Ww );

		float e0[3] = { Wb[12], Wb[13], Wb[14] };
		float L1 = sqrtf( ( Wf[12]-e0[0] )*( Wf[12]-e0[0] ) + ( Wf[13]-e0[1] )*( Wf[13]-e0[1] ) + ( Wf[14]-e0[2] )*( Wf[14]-e0[2] ) );
		float L2 = sqrtf( ( Ww[12]-Wf[12] )*( Ww[12]-Wf[12] ) + ( Ww[13]-Wf[13] )*( Ww[13]-Wf[13] ) + ( Ww[14]-Wf[14] )*( Ww[14]-Wf[14] ) );
		if( L1 < 1e-4f || L2 < 1e-4f ) continue;

		float d[3] = { H[h].pos[0]-e0[0], H[h].pos[1]-e0[1], H[h].pos[2]-e0[2] };
		float dist = sqrtf( d[0]*d[0]+d[1]*d[1]+d[2]*d[2] );
		if( dist < 1e-4f ) continue;
		float n[3] = { d[0]/dist, d[1]/dist, d[2]/dist };
		float dmax = ( L1 + L2 )*0.999f, dmin = fabsf( L1 - L2 )*1.05f + 1e-3f;
		if( dist > dmax ) dist = dmax;
		if( dist < dmin ) dist = dmin;

		// posicao do cotovelo: circulo em torno do ombro, no plano do polo
		const float aa = ( L1*L1 - L2*L2 + dist*dist ) / ( 2.0f*dist );
		float hh = L1*L1 - aa*aa; hh = hh > 0 ? sqrtf( hh ) : 0.0f;
		float pv[3] = { poleWorld[0], poleWorld[1], poleWorld[2] };
		float dp = pv[0]*n[0] + pv[1]*n[1] + pv[2]*n[2];
		for( int i = 0; i < 3; i++ ) pv[i] -= n[i]*dp;
		v3_norm( pv );
		float E1[3];
		for( int i = 0; i < 3; i++ ) E1[i] = e0[i] + n[i]*aa + pv[i]*hh;

		if( g_draws < 3 )
			vlog( "maos ik %s: ombro(%.1f %.1f %.1f) L1=%.1f L2=%.1f alvo(%.1f %.1f %.1f) dist=%.1f cotovelo(%.1f %.1f %.1f)\n",
				h ? "dir" : "esq",
				( double )e0[0], ( double )e0[1], ( double )e0[2], ( double )L1, ( double )L2,
				( double )H[h].pos[0], ( double )H[h].pos[1], ( double )H[h].pos[2], ( double )dist,
				( double )E1[0], ( double )E1[1], ( double )E1[2] );

		// braco (bicep): gira o repouso em torno do ombro ate apontar o cotovelo
		float ru[3] = { Wf[12]-e0[0], Wf[13]-e0[1], Wf[14]-e0[2] };
		float nu[3] = { E1[0]-e0[0], E1[1]-e0[1], E1[2]-e0[2] };
		v3_norm( ru ); v3_norm( nu );
		m4 R1, Tp, Tn, Wb2;
		rot_between( R1, ru, nu );
		translate_matrix( Tp, e0 );
		{ float ne[3] = { -e0[0], -e0[1], -e0[2] }; translate_matrix( Tn, ne ); }
		m4_mul( t1, Tp, R1 );
		m4_mul( Wb2, t1, Tn );
		m4_mul( t1, Wb2, Wb );
		memcpy( Wb2, t1, sizeof( m4 ));
		{
			m4 Wsh, iWsh;
			cur_world( &g_arms, cur, g_arms.joint[jb].parent, Wsh );
			m4_inv( iWsh, Wsh );
			m4_mul( cur[jb], iWsh, Wb2 );
		}

		// antebraco: agora segue o braco; gira ate apontar a mao
		m4 Wf2, Ww2;
		cur_world( &g_arms, cur, jf, Wf2 );
		cur_world( &g_arms, cur, jw, Ww2 );
		float e2[3] = { Wf2[12], Wf2[13], Wf2[14] };
		float rf[3] = { Ww2[12]-e2[0], Ww2[13]-e2[1], Ww2[14]-e2[2] };
		float nf[3] = { H[h].pos[0]-e2[0], H[h].pos[1]-e2[1], H[h].pos[2]-e2[2] };
		v3_norm( rf ); v3_norm( nf );
		m4 R2, Wf3;
		rot_between( R2, rf, nf );
		translate_matrix( Tp, e2 );
		{ float ne[3] = { -e2[0], -e2[1], -e2[2] }; translate_matrix( Tn, ne ); }
		m4_mul( t1, Tp, R2 );
		m4_mul( Wf3, t1, Tn );
		m4_mul( t1, Wf3, Wf2 );
		memcpy( Wf3, t1, sizeof( m4 ));
		{
			m4 iWb;
			m4_inv( iWb, Wb2 );
			m4_mul( cur[jf], iWb, Wf3 );
		}
	}

	// ---- 4) skinning com a pose corrente (uma vez por frame) ----
	m4 *skin = ( m4 * )malloc( ( size_t )g_arms.njoint * sizeof( m4 ));
	for( int i = 0; i < g_arms.njoint; i++ )
	{
		m4 w;
		cur_world( &g_arms, cur, i, w );
		m4_mul( skin[i], w, g_arms.joint[i].ibm );
	}

	const float light[3] = { fwd[0], fwd[1], fwd[2] };
	for( int i = 0; i < 3; i++ ) { g_cam_org[i] = cam->origin[i]; g_vlo[i] = 1e9f; g_vhi[i] = -1e9f; }
	g_vcount = 0; g_min_dist = 1e9f;
	// ATENCAO: as matrizes de skin JA estao em mundo (cur[root] carrega o Marm),
	// entao aqui o modelo entra IDENTIDADE. Aplicar o Marm de novo colocaria o
	// modelo uma segunda vez no espaco do rig (os bracos sumiam para longe,
	// enquanto a arma — que nao tem skin — continuava certa).
	m4 IDENT; m4_ident( IDENT );
	draw_mesh( &g_arms, ( const float * )IDENT, skin, light );
	free( skin );
	float alo[3], ahi[3], adist; long averts;
	for( int i = 0; i < 3; i++ ) { alo[i]=g_vlo[i]; ahi[i]=g_vhi[i]; }
	adist = g_min_dist; averts = g_vcount;

	// ---- 5) arma no socket da mao direita ----
	for( int i = 0; i < 3; i++ ) { g_vlo[i] = 1e9f; g_vhi[i] = -1e9f; }
	g_vcount = 0; g_min_dist = 1e9f;
	if( g_weapon.loaded && g_arms.j_socket[1] >= 0 )
	{
		m4 Ws;
		cur_world( &g_arms, cur, g_arms.j_socket[1], Ws );
		m4 Ro, Sw, Mo, t2;
		euler_matrix( Ro, cfg.wpn_pitch + g_recoil*10.0f, cfg.wpn_yaw, cfg.wpn_roll );
		scale_matrix( Sw, cfg.scale_weapon );
		{ // recuo: empurra a arma para tras no referencial do soco
			float back[3] = { -cfg.wpn_off[0] - g_recoil*2.5f, cfg.wpn_off[1], cfg.wpn_off[2] };
			translate_matrix( Mo, back );
		}
		m4_mul( t2, Mo, Ro );
		m4_mul( Mo, t2, Cv );
		m4_mul( t2, Mo, Sw );
		m4_mul( Mo, Ws, t2 );
		draw_mesh( &g_weapon, Mo, NULL, light );
	}

	free( cur );
	restore_3d_state();

	g_recoil *= 0.80f; // decai por frame (V1; nao e fisica, e retorno visual)
	if( g_recoil < 0.001f ) g_recoil = 0.0f;

	if( g_draws < 3 )
		vlog( "maos dbg: BRACOS bbox(%.0f %.0f %.0f)..(%.0f %.0f %.0f) dist_min=%.1f verts=%ld"
			" | ARMA bbox(%.0f %.0f %.0f)..(%.0f %.0f %.0f) verts=%ld\n",
			( double )alo[0], ( double )alo[1], ( double )alo[2],
			( double )ahi[0], ( double )ahi[1], ( double )ahi[2], ( double )adist, averts,
			( double )g_vlo[0], ( double )g_vlo[1], ( double )g_vlo[2],
			( double )g_vhi[0], ( double )g_vhi[1], ( double )g_vhi[2], g_vcount );
	g_draws++;
}

void vr_hands_status( char *buf, size_t n )
{
	if( !g_ready ) { snprintf( buf, n, "maos: off" ); return; }
	snprintf( buf, n, "maos: ON br=%dt wi=%dt es=%.2f/%.2f %s",
		g_arms.ntri, g_weapon.ntri, ( double )cfg.scale_arms, ( double )cfg.scale_weapon,
		g_arms.skinned ? "skin" : "SEM SKIN" );
}
