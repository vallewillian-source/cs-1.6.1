// vr_hands.cpp — viewmodel proprio do Mod VR (V2): bracos skinnados (WRAD ARMS) +
// arma (Quaternius) que segue as maos, com coldre, saque, dedos e duas maos.
//
// Escopo: classe "pistol". O mod esconde o viewmodel do jogo (RF_NO_VIEWMODEL) e
// este arquivo desenha o par bracos+arma dentro do FBO do olho, com profundidade
// real (por isso o mundo oclui) e a projecao ASSIMETRICA do olho.
//
// Decisoes de engenharia (e o porque) — V2:
//  * ENCAIXE RESOLVIDO, nao chutado. O osso `socket` do rig tem um quaternion
//    arbitrario (era a causa da arma "de lado/para tras"): o encaixe passou a ser
//    expresso no REFERENCIAL DA MAO (frente/direita/cima do controle) e o ponto de
//    empunhadura vem do centro da palma, MEDIDO no rig no load. Resultado: a arma
//    nasce na direcao do cano e no punho, com residuo (cfg) so para gosto.
//  * ESCALA PELO COMPRIMENTO REAL (cm), nao por multiplicador magico: o tamanho do
//    modelo e medido no load e a escala sai dele. Antes a escala era aplicada DUAS
//    vezes (o mundo do socket ja carregava scale_arms) e a arma saia 4,2x maior.
//  * EIXOS DO ASSET declarados no cfg (`arms_axes`/`weapon_axes`): o glTF do WRAD e
//    frente -Z/cima +Y/direita +X; o OBJ do Quaternius e frente +X. Aplicar a mesma
//    conversao nos dois punha a pistola atravessada.
//  * A MAO segue a rotacao do controle (P_hand): sem isso o punho ficava na pose de
//    repouso do rig e a arma nao tinha como "entrar" na mao.
//  * DEDOS: o rig tem 30 ossos de dedo; o eixo de flexao de cada um e DERIVADO do
//    repouso (cross(Y_local, direcao da palma)), entao fechar a mao nao precisa de
//    tabela por asset e vale igual para as duas maos.
//  * ESTADO (na mao / coldre / chao / duas maos) vive aqui; o mod da o corpo (onde
//    fica a barriga), o grip por mao e consulta o relato para mirar e para o
//    gatilho. A ABI nao deixa trocar arma de verdade: o efeito e no cliente.
//  * FOLHA DE CONTATO (vr_hands_sheet): grade de vistas no MESMO caminho de desenho,
//    para conferir encaixe/escala/dedos/estados por imagem, sem headset.
//  * Skinning na CPU, uma vez por FRAME (update), nao por olho: os dois olhos so
//    trocam a matriz — e a arma/braco ficam IGUAIS nos dois olhos (senao a
//    estereoscopia do que esta na mao sai errada).
//  * Malha vira "sopa de triangulos" (sem indice) e o sombreamento e por vertice na
//    CPU: ~2.200 triangulos, custo irrelevante, sem shader nem index buffer.
//  * Convencao de eixos: tudo em unidades/eixos GoldSrc (Z para cima, 1 unidade ~
//    1 polegada, 39,37 por metro). glTF/OBJ vem do autor: convertidos por cfg.
#include "vr_hands.h"
#include "vr_paths.h"

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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "third_party/stb_image.h"
#pragma GCC diagnostic pop
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
// Este arquivo carrega as PROPRAS funcoes de GL (dlsym em libGL): sao GL 1.1 core
// + alguns procs. Nao vale acoplar no carregador do mod, que serve ao painel 2D e
// falha em bloco — aqui, faltar uma funcao so desliga ESTE subsistema.
static float g_vlo[3] = { 1e9f, 1e9f, 1e9f };   // bbox do que FOI enviado ao GL
static float g_vhi[3] = { -1e9f, -1e9f, -1e9f };
static long  g_vcount;
static float g_cam_org[3];        // origem da camera deste desenho
static float g_min_dist = 1e9f;   // vertice mais proximo da camera

typedef void ( APIENTRYP PFN_matrixmode )( GLenum );
typedef void ( APIENTRYP PFN_void )( void );
typedef void ( APIENTRYP PFN_loadmatrix )( const GLfloat * );
typedef void ( APIENTRYP PFN_pushattrib )( GLbitfield );
typedef void ( APIENTRYP PFN_enum )( GLenum );
typedef void ( APIENTRYP PFN_enum_enum_int )( GLenum, GLenum, GLint );
typedef void ( APIENTRYP PFN_enum_enum_enum )( GLenum, GLenum, GLenum );
typedef void ( APIENTRYP PFN_depthmask )( GLboolean );
typedef void ( APIENTRYP PFN_depthrange )( GLclampd, GLclampd );
typedef void ( APIENTRYP PFN_viewport )( GLint, GLint, GLsizei, GLsizei );
typedef void ( APIENTRYP PFN_scissor )( GLint, GLint, GLsizei, GLsizei );
typedef void ( APIENTRYP PFN_clear )( GLbitfield );
typedef void ( APIENTRYP PFN_clearcolor )( GLclampf, GLclampf, GLclampf, GLclampf );
typedef void ( APIENTRYP PFN_linewidth )( GLfloat );
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
typedef void ( APIENTRYP PFN_getfloat )( GLenum, GLfloat * );

static PFN_matrixmode  p_glMatrixMode;
static PFN_void        p_glLoadIdentity;
static PFN_loadmatrix  p_glLoadMatrixf;
static PFN_void        p_glPushMatrix, p_glPopMatrix;
static PFN_pushattrib  p_glPushAttrib;
static PFN_void        p_glPopAttrib;
static PFN_enum        p_glEnable, p_glDisable, p_glCullFace, p_glFrontFace, p_glShadeModel, p_glDepthFunc;
static PFN_enum_enum_int p_glTexEnvi;
static PFN_enum_enum_enum p_glBlendFunc;
static PFN_depthmask   p_glDepthMask;
static PFN_depthrange  p_glDepthRange;
static PFN_viewport    p_glViewport;
static PFN_scissor     p_glScissor;
static PFN_clear       p_glClear;
static PFN_clearcolor  p_glClearColor;
static PFN_linewidth   p_glLineWidth;
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
	L( Viewport, p_glViewport )       L( Scissor, p_glScissor )     L( Clear, p_glClear )
	L( ClearColor, p_glClearColor )   L( LineWidth, p_glLineWidth ) L( BlendFunc, p_glBlendFunc )
	L( Begin, p_glBegin )             L( End, p_glEnd )
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

static float v3_dot( const float a[3], const float b[3] ) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }

static void v3_cross( float o[3], const float a[3], const float b[3] )
{
	o[0] = a[1]*b[2] - a[2]*b[1];
	o[1] = a[2]*b[0] - a[0]*b[2];
	o[2] = a[0]*b[1] - a[1]*b[0];
}

// Base a partir dos angulos do engine (pitch, yaw, roll) — a MESMA convencao do
// vr_angles_basis do mod, para a projecao casar com a do mundo.
static void angles_basis( const float ang[3], float fwd[3], float rgt[3], float up[3] )
{
	const double d2r = M_PI / 180.0;
	const double sy = sin( ang[1]*d2r ), cy = cos( ang[1]*d2r );
	const double sp = sin( ang[0]*d2r ), cp = cos( ang[0]*d2r );
	const double sr = sin( ang[2]*d2r ), cr = cos( ang[2]*d2r );

	fwd[0] = ( float )( cp*cy );          fwd[1] = ( float )( cp*sy );          fwd[2] = ( float )( -sp );
	rgt[0] = ( float )( -sr*sp*cy + cr*sy ); rgt[1] = ( float )( -sr*sp*sy - cr*cy ); rgt[2] = ( float )( -sr*cp );
	up[0]  = ( float )(  cr*sp*cy + sr*sy ); up[1]  = ( float )(  cr*sp*sy - sr*cy ); up[2]  = ( float )(  cr*cp );
}

static void cam_basis( const vr_hands_cam_t *cam, float fwd[3], float rgt[3], float up[3] )
{
	angles_basis( cam->angles, fwd, rgt, up );
}

// Rotacao de `deg` graus em torno de `axis` (Rodrigues).
static void m4_axis_rot( m4 o, const float axis[3], float deg )
{
	float a[3] = { axis[0], axis[1], axis[2] };
	v3_norm( a );
	const float t = deg * ( float )M_PI / 180.0f;
	const float c = cosf( t ), s = sinf( t ), ic = 1.0f - c;
	const float x = a[0], y = a[1], z = a[2];

	memset( o, 0, sizeof( m4 ));
	o[0] = c + x*x*ic;     o[4] = x*y*ic - z*s;   o[8]  = x*z*ic + y*s;
	o[1] = y*x*ic + z*s;   o[5] = c + y*y*ic;     o[9]  = y*z*ic - x*s;
	o[2] = z*x*ic - y*s;   o[6] = z*y*ic + x*s;   o[10] = c + z*z*ic;
	o[15] = 1.0f;
}

// Rotacao MINIMA que leva o vetor unitario a ao b (Rodrigues). Usada no IK e para
// alinhar o cano com a mira de duas maos.
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

// Inversa 4x4 GERAL (Gauss-Jordan). Nao podemos assumir matriz ortonormal: as
// matrizes de mundo do rig carregam a escala (scale_arms), e usar uma inversa
// "rigida" nelas produzia locais com translacao de centenas de unidades.
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

static void translate_matrix( m4 o, const float t[3] )
{
	m4_ident( o );
	o[12]=t[0]; o[13]=t[1]; o[14]=t[2];
}

// P = T(t) . R . S(s): coloca a rotacao R (3x3, colunas = eixos) na origem t com a
// escala uniforme s. E a forma do P_hand (referencial da mao) e do Marm (rig).
static void m4_place( m4 o, const m4 rot, const float t[3], float s )
{
	memcpy( o, rot, sizeof( m4 ));
	for( int c = 0; c < 3; c++ )
		for( int r = 0; r < 3; r++ )
			o[c*4+r] *= s;
	o[3] = o[7] = o[11] = 0.0f;
	o[12] = t[0]; o[13] = t[1]; o[14] = t[2]; o[15] = 1.0f;
}

// Transposta da parte 3x3 (para B_ctrl . B_rest^T).
static void m4_rot_transpose( m4 o, const m4 a )
{
	memset( o, 0, sizeof( m4 ));
	for( int r = 0; r < 3; r++ )
		for( int c = 0; c < 3; c++ )
			o[c*4+r] = a[r*4+c];
	o[15] = 1.0f;
}

// Eixo a partir de "+X"/"-Z"/"y" (a convencao declarada de cada asset).
static bool axis_from_token( const char *tok, float out[3] )
{
	out[0]=out[1]=out[2]=0.0f;
	if( !tok || !*tok ) return false;
	float s = 1.0f;
	if( *tok == '+' ) tok++;
	else if( *tok == '-' ) { s = -1.0f; tok++; }
	switch( *tok )
	{
	case 'x': case 'X': out[0] = s; return true;
	case 'y': case 'Y': out[1] = s; return true;
	case 'z': case 'Z': out[2] = s; return true;
	default: return false;
	}
}

// Conversao modelo -> GoldSrc a partir de "fwd up rgt" (ex.: "-Z +Y +X").
// As linhas sao: gx = frente, gy = -direita (GoldSrc tem +Y para a ESQUERDA),
// gz = cima. Assim a base convertida mantem a mesma mao do GoldSrc.
static bool axes_from_spec( m4 o, const char *spec )
{
	char a[8], b[8], c[8];
	float f[3], u[3], r[3];
	if( sscanf( spec, "%7s %7s %7s", a, b, c ) != 3 ) return false;
	if( !axis_from_token( a, f ) || !axis_from_token( b, u ) || !axis_from_token( c, r )) return false;

	memset( o, 0, sizeof( m4 ));
	o[0] = f[0]; o[1] = -r[0]; o[2] = u[0];
	o[4] = f[1]; o[5] = -r[1]; o[6] = u[1];
	o[8] = f[2]; o[9] = -r[2]; o[10] = u[2];
	o[15] = 1.0f;
	return true;
}

// Residuo de orientacao da arma NO REFERENCIAL EM QUE ELE VAI — ou seja, nas
// coordenadas LOCAIS do referencial (x = frente/cano, y = esquerda, z = cima):
//  - pitch em torno do eixo local Y: positivo = BOCA PARA CIMA
//  - yaw em torno do eixo local Z: positivo = boca para a ESQUERDA
//  - roll em torno do eixo local X (o cano): canted
// ATENCAO: o eixo e o LOCAL (0,1,0), nao o vetor de mundo do referencial. Como o
// residuo entra ANTES do referencial na composicao (Mh . Tres . Rres . ...), a
// rotacao acontece dentro dele — usar o vetor de mundo dava ~16 graus em vez de 70
// (o eixo "certo" so coincidia quando o corpo estava virado para +X).
static void residual_matrix( m4 o, float pitch, float yaw, float roll )
{
	const float ax[3] = { 1.0f, 0.0f, 0.0f };
	const float ay[3] = { 0.0f, 1.0f, 0.0f };
	const float az[3] = { 0.0f, 0.0f, 1.0f };
	m4 rp, ry, rr, t;

	m4_axis_rot( rp, ay, -pitch );
	m4_axis_rot( ry, az, yaw );
	m4_axis_rot( rr, ax, roll );
	m4_mul( t, ry, rp );
	m4_mul( o, t, rr );
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

#define HFING 5
#define HJOINTS_PER_FING 3

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
	int      j_bicep[2], j_fore[2], j_wrist[2];
	// dedos: [mao][dedo][falange] — indice 0 = esquerda, 1 = direita
	int      j_finger[2][HFING][HJOINTS_PER_FING];
	m4       fin_axis[2][HFING][HJOINTS_PER_FING];   // eixo de flexao (espaco LOCAL do osso)
	// referencia da palma no REPOUSO (espaco do rig): e dela que sai o encaixe.
	// palm_ok = false quando o rig nao tem os ossos de que ela depende: nesse caso o
	// subsistema se desliga em vez de desenhar com um referencial zerado.
	float    palm_fwd[2][3], palm_left[2][3], palm_up[2][3], palm_ctr[2][3];
	float    palm_off[2];      // distancia punho->centro da palma (unidades do rig)
	bool     palm_ok[2];
	float    len[3];           // tamanho do modelo ja convertido (GoldSrc)
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

// Tamanho do modelo JA CONVERTIDO: a extensao em X e o "comprimento" do asset na
// direcao declarada como frente — e dela que sai a escala em cm (ver cfg).
static void mesh_measure( hmesh_t *m, const m4 conv )
{
	float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
	for( int i = 0; i < m->ntri; i++ )
		for( int k = 0; k < 3; k++ )
		{
			float q[3];
			m4_xform_point( conv, m->tri[i].p[k], q );
			for( int c = 0; c < 3; c++ ) { if( q[c]<lo[c] ) lo[c]=q[c]; if( q[c]>hi[c] ) hi[c]=q[c]; }
		}
	for( int c = 0; c < 3; c++ ) m->len[c] = hi[c] - lo[c];
}

// -------------------------------------------------------------- cfg --------
typedef struct
{
	bool  enable;
	char  arms_glb[160];
	char  arms_tex[160];
	char  weapon_obj[160];
	char  arms_axes[32];
	char  weapon_axes[32];
	float scale_arms, scale_weapon;
	float weapon_length_cm;
	float weapon_grip[3];    // ponto de empunhadura no espaco do MODELO
	float wpn_pitch, wpn_yaw, wpn_roll;
	float wpn_off[3];
	float rig_yaw;
	float support_fwd, support_side, support_up;
	float curl_max[3];
	float curl_w[HFING];
	float curl_fist;
	float grip_th;
	float holster_fwd, holster_down, holster_side;
	float holster_euler[3];
	float holster_radius, grab_radius, grab_hyst;
	float two_hand_radius, two_hand_blend;
	float wrist_roll;        // 0 = roll do punho vem do braco (natural); 1 = do controle
	float drop_gravity, drop_eye_height;
	float recoil_deg, recoil_kick, recoil_interval;
	bool  enforce_armed;     // fora da mao o gatilho nao atira (so na classe coberta)
	int   sheet;
	bool  synth_hands;
	float synth_grip;
	float synth_dist, synth_side, synth_down;
	float near_clip, far_clip, color_gamma;
} hcfg_t;

static hcfg_t  cfg;
static double  cfg_mtime;
static int     g_sheet;          // vr_sheet_t (o cfg e o VR_HANDS_SHEET escrevem)
static bool    g_sheet_forced;   // o ambiente pediu: o cfg nao sobrepoe

static void cfg_defaults( void )
{
	memset( &cfg, 0, sizeof( cfg ));
	cfg.enable = true;
	snprintf( cfg.arms_glb, sizeof( cfg.arms_glb ), "mods/vr_assets/arms.glb" );
	snprintf( cfg.arms_tex, sizeof( cfg.arms_tex ), "mods/vr_assets/arm_albedo_pale.png" );
	snprintf( cfg.weapon_obj, sizeof( cfg.weapon_obj ), "mods/vr_assets/Pistol_1.obj" );
	// WRAD (glTF): frente -Z, cima +Y, direita +X. Quaternius (OBJ): frente +X.
	snprintf( cfg.arms_axes, sizeof( cfg.arms_axes ), "-Z +Y +X" );
	snprintf( cfg.weapon_axes, sizeof( cfg.weapon_axes ), "+X +Y +Z" );

	cfg.scale_arms = 4.2f;
	cfg.weapon_length_cm = 19.0f;   // comprimento real alvo (USP ~21,8 / Glock18 ~18,6)
	cfg.scale_weapon = 1.0f;        // residuo multiplicativo (1 = so o comprimento manda)
	// MEDIDO no Pistol_1.obj: bbox x -0,34..1,48 (cano em +X), e o aglomerado do
	// cabo (x < 0,21 e y < 0) tem centroide (-0,109 -0,252 0,000). Ver SOURCES.md.
	cfg.weapon_grip[0] = -0.10f; cfg.weapon_grip[1] = -0.23f; cfg.weapon_grip[2] = 0.0f;

	cfg.wpn_off[0]=cfg.wpn_off[1]=cfg.wpn_off[2]=0.0f;
	// duas maos: a mao de apoio vai a frente e para o lado, no referencial da palma
	cfg.support_fwd = 1.6f; cfg.support_side = 1.4f; cfg.support_up = 0.0f;
	// dedos: falanges e pesos por dedo (pinky, ring, middle, index, thumb)
	cfg.curl_max[0] = 85.0f; cfg.curl_max[1] = 95.0f; cfg.curl_max[2] = 75.0f;
	cfg.curl_w[0] = 1.0f; cfg.curl_w[1] = 1.0f; cfg.curl_w[2] = 0.95f;
	cfg.curl_w[3] = 0.85f; cfg.curl_w[4] = 0.60f;
	cfg.curl_fist = 0.75f;   // fechamento minimo quando a mao esta segurando
	cfg.grip_th = 0.60f;
	cfg.holster_fwd = 10.0f; cfg.holster_down = 22.0f; cfg.holster_side = 0.0f;
	cfg.holster_euler[0] = -70.0f; cfg.holster_euler[1] = 0.0f; cfg.holster_euler[2] = 0.0f;
	cfg.holster_radius = 9.0f; cfg.grab_radius = 7.0f; cfg.grab_hyst = 1.7f;
	cfg.two_hand_radius = 6.0f; cfg.two_hand_blend = 0.5f;
	cfg.wrist_roll = 1.0f;   // roll do punho: o do CONTROLE (o jogador segura o controle de
	                         // cabo em pe, entao a arma sai em pe) — o referencial de
	                         // empunhadura e que estava 90 graus torto, ver palm_from_rest.
	                         // 0 = roll vindo do braco (o repouso do rig tem a palma para
	                         // cima, o que deita a arma): so como alternativa.
	cfg.drop_gravity = 386.0f; cfg.drop_eye_height = 28.0f;
	cfg.recoil_deg = 7.0f; cfg.recoil_kick = 0.9f; cfg.recoil_interval = 0.09f;
	cfg.enforce_armed = true;   // com a arma no coldre/chao, o gatilho nao atira
	cfg.synth_grip = 1.0f;
	cfg.synth_dist = 16.0f;      // dentro do alcance (~22 un): alvo fora do alcance deixa o braco reto
	cfg.synth_side = 5.5f; cfg.synth_down = 9.0f;
	cfg.near_clip = 4.0f;        // igual ao ref (gl_rmain.c: zNear = 4.0f)
	cfg.far_clip = 7086.0f;      // ~ movevars.zmax * 1.73 (idem)
	cfg.color_gamma = 2.2f;
	cfg.enable = true;
}

// Copia de string do cfg com truncagem EXPLICITA: os buffers de destino sao
// menores que a linha lida, e a truncagem aqui e de proposito (o valor e curto).
static void cfg_copy( char *dst, size_t n, const char *src )
{
	snprintf( dst, n, "%.*s", ( int )( n - 1 ), src );
}

static void cfg_parse( const char *path )
{
	cfg_defaults();

	// Marca o mtime JA na leitura: sem isto o primeiro tick concluiria que o
	// arquivo mudou e recarregaria os assets no 1o frame — e, pior, o `sheet 0` do
	// cfg apagaria a folha que o ambiente pediu.
	{
		struct stat st;
		if( stat( path, &st ) == 0 ) cfg_mtime = ( double )st.st_mtime;
	}

	FILE *f = fopen( path, "r" );
	if( !f ) { vlog( "maos: cfg ausente (%s) — padroes\n", path ); return; }

	char line[512];
	while( fgets( line, sizeof( line ), f ))
	{
		char *h = strchr( line, '#' );
		if( h ) *h = 0;
		char key[64], val[240];
		if( sscanf( line, "%63s %239[^\n]", key, val ) != 2 ) continue;
		if     ( !strcmp( key, "enable" ))         cfg.enable = atoi( val ) != 0;
		else if( !strcmp( key, "arms_glb" ))       cfg_copy( cfg.arms_glb, sizeof( cfg.arms_glb ), val );
		else if( !strcmp( key, "arms_tex" ))       cfg_copy( cfg.arms_tex, sizeof( cfg.arms_tex ), val );
		else if( !strcmp( key, "weapon_obj" ))     cfg_copy( cfg.weapon_obj, sizeof( cfg.weapon_obj ), val );
		else if( !strcmp( key, "arms_axes" ))      cfg_copy( cfg.arms_axes, sizeof( cfg.arms_axes ), val );
		else if( !strcmp( key, "weapon_axes" ))    cfg_copy( cfg.weapon_axes, sizeof( cfg.weapon_axes ), val );
		else if( !strcmp( key, "scale_arms" ))     cfg.scale_arms = ( float )atof( val );
		else if( !strcmp( key, "weapon_scale_res" )) cfg.scale_weapon = ( float )atof( val );
		else if( !strcmp( key, "weapon_length_cm" )) cfg.weapon_length_cm = ( float )atof( val );
		else if( !strcmp( key, "weapon_grip" ))    sscanf( val, "%f %f %f", &cfg.weapon_grip[0], &cfg.weapon_grip[1], &cfg.weapon_grip[2] );
		else if( !strcmp( key, "weapon_off" ))     sscanf( val, "%f %f %f", &cfg.wpn_off[0], &cfg.wpn_off[1], &cfg.wpn_off[2] );
		else if( !strcmp( key, "weapon_euler" ))   sscanf( val, "%f %f %f", &cfg.wpn_pitch, &cfg.wpn_yaw, &cfg.wpn_roll );
		else if( !strcmp( key, "rig_yaw" ))        cfg.rig_yaw = ( float )atof( val );
		else if( !strcmp( key, "support_fwd" ))    cfg.support_fwd = ( float )atof( val );
		else if( !strcmp( key, "support_side" ))   cfg.support_side = ( float )atof( val );
		else if( !strcmp( key, "support_up" ))     cfg.support_up = ( float )atof( val );
		else if( !strcmp( key, "curl_max" ))       sscanf( val, "%f %f %f", &cfg.curl_max[0], &cfg.curl_max[1], &cfg.curl_max[2] );
		else if( !strcmp( key, "curl_fist" ))      cfg.curl_fist = ( float )atof( val );
		else if( !strcmp( key, "curl_index" ))     cfg.curl_w[3] = ( float )atof( val );
		else if( !strcmp( key, "curl_middle" ))    cfg.curl_w[2] = ( float )atof( val );
		else if( !strcmp( key, "curl_ring" ))      cfg.curl_w[1] = ( float )atof( val );
		else if( !strcmp( key, "curl_pinky" ))     cfg.curl_w[0] = ( float )atof( val );
		else if( !strcmp( key, "curl_thumb" ))     cfg.curl_w[4] = ( float )atof( val );
		else if( !strcmp( key, "grip_th" ))        cfg.grip_th = ( float )atof( val );
		else if( !strcmp( key, "holster_fwd" ))    cfg.holster_fwd = ( float )atof( val );
		else if( !strcmp( key, "holster_down" ))   cfg.holster_down = ( float )atof( val );
		else if( !strcmp( key, "holster_side" ))   cfg.holster_side = ( float )atof( val );
		else if( !strcmp( key, "holster_euler" ))  sscanf( val, "%f %f %f", &cfg.holster_euler[0], &cfg.holster_euler[1], &cfg.holster_euler[2] );
		else if( !strcmp( key, "holster_radius" )) cfg.holster_radius = ( float )atof( val );
		else if( !strcmp( key, "grab_radius" ))    cfg.grab_radius = ( float )atof( val );
		else if( !strcmp( key, "grab_hyst" ))      cfg.grab_hyst = ( float )atof( val );
		else if( !strcmp( key, "two_hand_radius" )) cfg.two_hand_radius = ( float )atof( val );
		else if( !strcmp( key, "two_hand_blend" )) cfg.two_hand_blend = ( float )atof( val );
		else if( !strcmp( key, "wrist_roll" ))    cfg.wrist_roll = ( float )atof( val );
		else if( !strcmp( key, "drop_gravity" ))   cfg.drop_gravity = ( float )atof( val );
		else if( !strcmp( key, "drop_eye_height" )) cfg.drop_eye_height = ( float )atof( val );
		else if( !strcmp( key, "recoil_deg" ))     cfg.recoil_deg = ( float )atof( val );
		else if( !strcmp( key, "recoil_kick" ))    cfg.recoil_kick = ( float )atof( val );
		else if( !strcmp( key, "recoil_interval" )) cfg.recoil_interval = ( float )atof( val );
		else if( !strcmp( key, "enforce_armed" )) cfg.enforce_armed = atoi( val ) != 0;
		else if( !strcmp( key, "synth_hands" ))    cfg.synth_hands = atoi( val ) != 0;
		else if( !strcmp( key, "synth_grip" ))     cfg.synth_grip = ( float )atof( val );
		else if( !strcmp( key, "synth_dist" ))     cfg.synth_dist = ( float )atof( val );
		else if( !strcmp( key, "synth_side" ))     cfg.synth_side = ( float )atof( val );
		else if( !strcmp( key, "synth_down" ))     cfg.synth_down = ( float )atof( val );
		else if( !strcmp( key, "near" ))           cfg.near_clip = ( float )atof( val );
		else if( !strcmp( key, "far" ))            cfg.far_clip = ( float )atof( val );
		else if( !strcmp( key, "color_gamma" ))    cfg.color_gamma = ( float )atof( val );
		else if( !strcmp( key, "sheet" ))
		{
			// o que o AMBIENTE pediu manda: com VR_HANDS_SHEET ligado, o `sheet 0`
			// do cfg nao desliga a folha (senao o modo sumiria no 1o tick)
			if( !g_sheet_forced )
			{
				if     ( !strcmp( val, "fit" ))    g_sheet = VR_SHEET_FIT;
				else if( !strcmp( val, "weapon" )) g_sheet = VR_SHEET_WEAPON;
				else if( !strcmp( val, "curl" ))   g_sheet = VR_SHEET_CURL;
				else if( !strcmp( val, "states" )) g_sheet = VR_SHEET_STATES;
				else if( !strcmp( val, "walk" ))   g_sheet = VR_SHEET_WALK;
				else if( atoi( val ) )             g_sheet = VR_SHEET_FIT;
				else g_sheet = VR_SHEET_OFF;
			}
		}
		else vlog( "maos: cfg: chave ignorada '%s'\n", key );
	}
	fclose( f );
}

// --------------------------------------------------------- estado ----------
static hmesh_t g_arms, g_weapon;
static bool    g_ready;
static m4      g_conv_arms, g_conv_weapon;
static float   g_weapon_scale;   // unidades GoldSrc por unidade do modelo
static float   g_recoil;
static double  g_recoil_next;    // proximo disparo automatico (tiro preso)
static double  g_time;
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

// ------------------------------------------------------------ rig ----------
static const char *g_fing_name[HFING] = { "pinky", "ring", "middle", "index", "thumb" };
static const char *g_side_suffix[2] = { "l", "r" };   // 0 = esquerda, 1 = direita

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

// Referencial de EMPUNHADURA de cada mao, medido no repouso do rig (espaco do rig):
//  * frente (X) = punho -> media das bases dos dedos longos (para onde a mao aponta; e
//    a direcao em que o cano de uma arma empunhada corre)
//  * cima (Z) = LINHA DOS NOS, do mindinho para o indicador (que continua ate o
//    polegar). E o eixo do CABO numa empunhadura — o beavertail de uma pistola fica
//    entre o polegar e o indicador e o carregador sai no mindinho — e o mesmo eixo que
//    um controle de cabo (Touch) reporta como "cima".
//  * esquerda (Y) = cross(cima, frente), o que mantem o referencial destro e da a
//    direcao em que a palma olha (e o "esquerda" que o controle reporta).
// A normal da palma (com o sinal escolhido para o centro do rig) fica so como
// conferencia de polaridade no log: quem manda no referencial e a anatomia acima.
//  * centro = 55% do caminho punho -> dedos (onde o cabo da arma se apoia).
static void palm_from_rest( hmesh_t *m )
{
	for( int h = 0; h < 2; h++ )
	{
		m4 Ww;
		float wrist[3], mean1[3] = {0,0,0}, s[3] = {0,0,0};
		float ipos[3] = {0,0,0}, ppos[3] = {0,0,0}, tpos[3] = {0,0,0};
		int n1 = 0, nlong = 0;
		bool have_index = false, have_pinky = false, have_thumb = false;

		if( m->j_wrist[h] < 0 ) continue;
		joint_world( m, m->j_wrist[h], Ww );
		wrist[0] = Ww[12]; wrist[1] = Ww[13]; wrist[2] = Ww[14];

		for( int f = 0; f < HFING; f++ )
		{
			int j = m->j_finger[h][f][0];
			if( j < 0 ) continue;
			m4 W;
			joint_world( m, j, W );
			// o polegar nao entra na direcao da frente (ele sai para o lado)
			if( f != 4 )
			{
				for( int c = 0; c < 3; c++ ) mean1[c] += W[12+c];
				nlong++;
			}
			if( f == 3 ) { for( int c = 0; c < 3; c++ ) { s[c] = W[12+c]; ipos[c] = W[12+c]; } have_index = true; }   // index
			if( f == 0 ) { for( int c = 0; c < 3; c++ ) { s[c] -= W[12+c]; ppos[c] = W[12+c]; } have_pinky = true; } // menos pinky
			if( f == 4 ) { for( int c = 0; c < 3; c++ ) tpos[c] = W[12+c]; have_thumb = true; }                        // polegar
			n1++;
		}
		if( n1 < 3 || nlong < 1 || !have_index || !have_pinky ) continue;
		// ATENCAO: divide pela quantidade de dedos SOMADOS (4 longos, sem o
		// polegar). Dividir por 3 inflava a media em 4/3 e a "frente da palma"
		// saia torta — o encaixe saia do lugar por causa disto.
		for( int c = 0; c < 3; c++ ) mean1[c] /= ( float )nlong;

		float fwd[3] = { mean1[0]-wrist[0], mean1[1]-wrist[1], mean1[2]-wrist[2] };
		float d = sqrtf( v3_dot( fwd, fwd ));
		v3_norm( fwd );
		float nrm[3];
		v3_cross( nrm, fwd, s );
		v3_norm( nrm );

		// sinal: a palma olha para o centro do rig (onde esta o corpo)
		float to_center[3] = { -wrist[0], -wrist[1], -wrist[2] };
		if( v3_dot( nrm, to_center ) < 0.0f ) { nrm[0]=-nrm[0]; nrm[1]=-nrm[1]; nrm[2]=-nrm[2]; }

		// ---- eixo "CIMA" da mao (e da arma, que e montada na mao): a LINHA DOS NOS ----
		// A direcao do dedo minimo para o indicador (que continua ate o polegar) e o eixo
		// que aponta para CIMA quando a mao EMPUNHA:
		//  * numa pistola o beavertail fica entre o polegar e o indicador e o carregador
		//    sai no mindinho: o cabo corre ao longo da linha dos nos e a alca (o "cima" da
		//    arma) fica do lado do polegar;
		//  * e o mesmo eixo que o controle entrega como "cima" quando o jogador segura um
		//    controle de CABO (Touch: +Y sai da face de cima, onde esta o polegar).
		// Antes o "cima" era a NORMAL da palma (as costas da mao), o que poe a linha dos nos
		// na HORIZONTAL: o punho aparece girado ~90 graus ("girei meu pulso para a direita")
		// e a arma, montada com o "cima" no eixo da mao, sai DEITADA e de cabeca para baixo,
		// com o cabo apontando para FORA da palma em vez de dentro do punho. Um erro de 90
		// graus no referencial explica os tres relatos (o punho torto, a arma invertida e o
		// cabo fora do lugar).
		float up[3] = { -s[0], -s[1], -s[2] };   // `s` = index - pinky: o polegar esta no -s
		{
			float proj = v3_dot( up, fwd );
			for( int c = 0; c < 3; c++ ) up[c] -= fwd[c]*proj;      // perpendicular a frente
			v3_norm( up );
		}
		// O eixo tem de apontar para o POLEGAR (e nao para o mindinho). Um sinal trocado
		// aqui deita a arma, entao ele e conferido contra a anatomia do proprio rig no log.
		float th_side = 0.0f;
		if( have_thumb )
		{
			float mid[3], td[3];
			for( int c = 0; c < 3; c++ ) mid[c] = 0.5f*( ipos[c] + ppos[c] );
			for( int c = 0; c < 3; c++ ) td[c] = tpos[c] - mid[c];
			float proj = v3_dot( td, fwd );
			for( int c = 0; c < 3; c++ ) td[c] -= fwd[c]*proj;
			v3_norm( td );
			th_side = v3_dot( td, up );
		}

		for( int c = 0; c < 3; c++ )
		{
			m->palm_fwd[h][c] = fwd[c];
			m->palm_up[h][c]  = up[c];                       // linha dos nos, lado do polegar
			// CENTRO DA PALMA **relativo ao punho**. Guardar a posicao ABSOLUTA do rig
			// (`wrist + fwd*0.55d`, ~6 un. do centro do corpo) parecia inofensivo, mas o
			// referencial da mao tem ORIGEM no punho: com um ponto absoluto ali, a arma
			// ganhava ~27 un. de deslocamento e saia da mao. Mesma familia do bug do
			// rest_world na munheca — coordenada absoluta onde se esperava deslocamento.
			m->palm_ctr[h][c] = fwd[c] * ( 0.55f * d );
		}
		v3_cross( m->palm_left[h], m->palm_up[h], m->palm_fwd[h] );
		m->palm_off[h] = 0.55f * d;
		m->palm_ok[h] = true;

		// `polegar` = o eixo "cima" esta do lado do polegar (TEM de ser positivo: o sinal
		// e conferido contra a anatomia do rig, e nao deduzido). `palma x lateral` (em
		// modulo) = a normal da palma cai no eixo lateral do referencial, o que confirma
		// que o referencial esta em volta do cabo. A POLARIDADE da normal nao e confiavel
		// (o sinal escolhido pelo "centro do rig" saiu trocado de uma mao para a outra),
		// por isso ela nao define mais nada aqui.
		vlog( "maos: palma %s centro a %.2f (rig) da munheca | frente(%.2f %.2f %.2f) | cima=linha-dos-nos: polegar %+.2f (tem de ser +) | palma x lateral |%.2f|\n",
			h ? "dir" : "esq", ( double )m->palm_off[h],
			( double )fwd[0], ( double )fwd[1], ( double )fwd[2],
			( double )th_side, ( double )fabsf( v3_dot( nrm, m->palm_left[h] )));
	}
}

// Eixo de flexao de cada falange, DERIVADO do repouso: no espaco LOCAL do osso o
// comprimento corre em +Y; girar em torno de cross(Y, direcao-para-a-palma) move a
// ponta do dedo NA DIRECAO DA PALMA. Vale igual para as duas maos e para o polegar.
static void finger_axis_from_rest( hmesh_t *m )
{
	for( int h = 0; h < 2; h++ )
		for( int f = 0; f < HFING; f++ )
			for( int k = 0; k < HJOINTS_PER_FING; k++ )
			{
				const int j = m->j_finger[h][f][k];
				if( j < 0 ) continue;

				m4 W, iW;
				float jw[3], want[3], dl[3];
				joint_world( m, j, W );
				jw[0] = W[12]; jw[1] = W[13]; jw[2] = W[14];

				// direcao (no espaco do rig) do osso para o centro da palma
				for( int c = 0; c < 3; c++ ) want[c] = m->palm_ctr[h][c] - jw[c];
				v3_norm( want );

				m4_inv( iW, W );
				m4_xform_dir( iW, want, dl );

				const float y[3] = { 0.0f, 1.0f, 0.0f };
				const float dy = v3_dot( dl, y );
				for( int c = 0; c < 3; c++ ) dl[c] -= y[c] * dy;    // perpendicular ao osso

				float axis[3];
				v3_cross( axis, y, dl );
				if( sqrtf( v3_dot( axis, axis )) < 1e-5f ) { axis[0] = 1.0f; axis[1] = 0.0f; axis[2] = 0.0f; }
				v3_norm( axis );
				memcpy( m->fin_axis[h][f][k], axis, sizeof( axis ));
			}
}

// ------------------------------------------------------------ glTF ---------
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

		for( int h = 0; h < 2; h++ )
			for( int f = 0; f < HFING; f++ )
				for( int k = 0; k < HJOINTS_PER_FING; k++ )
				{
					char nm[64];
					snprintf( nm, sizeof( nm ), "finger_%s%d.%s", g_fing_name[f], k+1, g_side_suffix[h] );
					m->j_finger[h][f][k] = hjoint_find( m, nm );
				}

		palm_from_rest( m );
		finger_axis_from_rest( m );
		vlog( "maos: rig root=%d head=%d bicep=%d/%d fore=%d/%d wrist=%d/%d | dedos %d/%d\n",
			m->j_root, m->j_head, m->j_bicep[0], m->j_bicep[1], m->j_fore[0], m->j_fore[1],
			m->j_wrist[0], m->j_wrist[1],
			m->j_finger[1][3][0], m->j_finger[0][3][0] );
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
	mesh_measure( m, g_conv_arms );
	vlog( "maos: glTF '%s' -> %d tri, %d verts, %d ossos, skin=%s, tex=%u, tam(%.2f %.2f %.2f) un. convertidas\n",
		path, m->ntri, m->nverts, m->njoint, m->skinned ? "sim" : "nao", m->tex,
		( double )m->len[0], ( double )m->len[1], ( double )m->len[2] );
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
					snprintf( cur->name, sizeof( cur->name ), "%.63s", line + 7 );
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
			char nm[64]; snprintf( nm, sizeof( nm ), "%.63s", line+7 );
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
					v3_cross( nrm, u, v );
					v3_norm( nrm );
					for( int q = 0; q < 3; q++ ) memcpy( t->n[q], nrm, 3*sizeof(float));
				}
			}
		}
	}
	fclose( f );
	free( vp ); free( vn );
	m->nverts = nvp; m->nmats = nmats;
	mesh_measure( m, g_conv_weapon );
	vlog( "maos: OBJ '%s' -> %d tri, %d verts, %d materiais, comprimento %.2f un. do modelo\n",
		objpath, m->ntri, nvp, nmats, ( double )m->len[0] );
	m->loaded = m->ntri > 0;
	return m->loaded;
}

// ------------------------------------------------------------ desenho ------
static float shade_of( const float n[3], const float light[3] )
{
	float d = n[0]*light[0] + n[1]*light[1] + n[2]*light[2];
	if( d < 0.0f ) d = 0.0f;
	return 0.42f + 0.58f * d;
}

static void draw_mesh( hmesh_t *m, const m4 model, const m4 *skin, const float light[3] )
{
	const bool tex = m->tex != 0;
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
			p_glColor4f( t->tint[0]*s, t->tint[1]*s, t->tint[2]*s, 1.0f );
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

// Linha crua no espaco do mundo — os marcadores da folha de contato.
static void draw_line( const float a[3], const float b[3], float r, float g, float bl )
{
	p_glColor4f( r, g, bl, 1.0f );
	p_glBegin( GL_LINES );
	p_glVertex3f( a[0], a[1], a[2] );
	p_glVertex3f( b[0], b[1], b[2] );
	p_glEnd();
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

static void apply_3d_state( const vr_hands_cam_t *cam, int vx, int vy, int vw, int vh )
{
	p_glPushAttrib( GL_ALL_ATTRIB_BITS );
	m4 pj, vw_m;
	proj_matrix( pj, cam );
	view_matrix( vw_m, cam );
	p_glMatrixMode( GL_PROJECTION ); p_glPushMatrix(); p_glLoadMatrixf( pj );
	p_glMatrixMode( GL_MODELVIEW );  p_glPushMatrix(); p_glLoadMatrixf( vw_m );
	p_glViewport( vx, vy, vw, vh );
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

// ------------------------------------------------------------- cena --------
// Tudo o que um frame DI-BU-XA-DO precisa: a pose resolvida dos bracos/maos e a
// matriz da arma. O update monta isto UMA vez; os dois olhos so desenham.
typedef struct
{
	m4   *cur;          // pose corrente dos ossos (njoint)
	m4   *skin;         // matrizes de skin dos bracos
	int   njoint;
	bool  hands_ok[2];
	float palm_pos[2][3];      // centro da palma no mundo
	m4    hand[2];             // referencial da mao (x = frente/cano, y = esquerda, z = cima)
	bool  weapon_ok;
	m4    weapon;              // modelo -> mundo da arma
	float grip_world[3];       // ponto de empunhadura no mundo (diagnostico)
	float aim[3];
	float fit_dist;
	float wrist_ik[2][3];      // onde o IK pos o punho (unidades GoldSrc, mundo)
	float wrist_slip[2];       // distancia da malha do punho ate o alvo do IK (0 = certo)
	float palm_slip[2];        // distancia do centro da palma da MALHA ate o referencial (0 = arma na mao)
	float arm_target[2];       // distancia ombro->alvo, CRUA (antes do clamp)
	float arm_reach[2];        // alcance da cadeia (biceps+antebraco) no repouso
	float elbow_deg[2];        // quanto o cotovelo dobra: 0 = esticado, 90+ = dobrado
	float wrist_twist[2];      // torcao que o CONTROLE pedia no punho, graus (0 = concorda com o repouso)
	float focus[3];            // onde a folha de contato deve enquadrar
} hscene_t;

static void scene_free( hscene_t *s )
{
	free( s->cur ); free( s->skin );
	memset( s, 0, sizeof( *s ));
}

// Monta a arma no referencial `frame` (x = frente/cano, y = esquerda, z = cima):
// ancora a EMPUNHADURA do modelo na origem do referencial, alinha o cano com X,
// escala pelo comprimento real e aplica o residuo (off em unidades GoldSrc e
// euler em graus, ambos NAQUELE referencial). E a MESMA conta para a mao, o
// coldre e o chao — so muda o referencial e o residuo.
static void mount_weapon( m4 out, const m4 frame, const float off[3], float pitch, float yaw,
	float roll, float scale )
{
	m4 Tres, Rres, Sw, Tg, t1, t2;

	translate_matrix( Tres, off );
	residual_matrix( Rres, pitch, yaw, roll );
	memset( Sw, 0, sizeof( m4 )); Sw[0]=Sw[5]=Sw[10]=scale; Sw[15]=1.0f;

	// Tg = Cv com a translacao que leva a empunhadura do modelo para a origem
	memcpy( Tg, g_conv_weapon, sizeof( m4 ));
	{
		float gp[3] = { cfg.weapon_grip[0], cfg.weapon_grip[1], cfg.weapon_grip[2] }, q[3];
		m4_xform_point( g_conv_weapon, gp, q );
		Tg[12] = -q[0]; Tg[13] = -q[1]; Tg[14] = -q[2];
	}

	m4_mul( t2, Tres, Rres );
	m4_mul( t1, t2, Sw );
	m4_mul( t2, t1, Tg );
	m4_mul( out, frame, t2 );
}

// Estado da arma (persistente entre frames).
typedef struct
{
	int    state;
	int    rear;              // mao de tras (quem segura o cabo)
	bool   two_hand;
	bool   held;              // "alguem esta segurando" (borda de soltar; ver weapon_step)
	bool   grip_prev[2];
	float  ground[3];         // posicao da arma largada
	float  ground_fwd[3];     // orientacao da arma largada (eixo do cano)
	float  fall_v;
	bool   landed;
} hwstate_t;

static hwstate_t g_ws;
static hscene_t  g_scene;

// ------------------------------------------------------- estado da arma ----
// Anda um passo da maquina de estados. Usa SO as posicoes das maos (as do
// controle) e o coldre: nada de IK aqui, entao nao ha dependencia circular.
static void weapon_step( const vr_hands_body_t *body, const vr_hands_in_t *in,
	const vr_hands_hand_t H[2], float dt )
{
	float holster[3];
	for( int c = 0; c < 3; c++ )
		holster[c] = body->pos[c] + body->fwd[c]*cfg.holster_fwd
			+ body->rgt[c]*cfg.holster_side - body->up[c]*cfg.holster_down;

	float floor_z = body->floor_z;
	if( floor_z == 0.0f )
		floor_z = body->pos[2] - ( body->eye_height > 0.0f ? body->eye_height : cfg.drop_eye_height );

	bool grip[2] = { in->grip[0] > cfg.grip_th, in->grip[1] > cfg.grip_th };
	bool edge[2] = { grip[0] && !g_ws.grip_prev[0], grip[1] && !g_ws.grip_prev[1] };
	if( dt > 0.25f ) dt = 0.25f;   // o jogo travou: nao deixa a arma cair um andar

	// em que estado a arma NASCE (tem de ser na mao). Se o log disser outra coisa
	// logo no inicio, alguma coisa a largou antes de o jogador ve-la — foi assim que
	// se apresentou o "nenhuma arma aparece, nem na mao nem no coldre".
	{
		static bool born = false;
		if( !born )
		{
			born = true;
			vlog( "maos: arma nasce %s (estado inicial) | grip esq %.2f dir %.2f (limiar %.2f)\n",
				g_ws.state == VR_WS_HAND ? "NA MAO" : ( g_ws.state == VR_WS_HOLSTER ? "no coldre" : "no chao" ),
				( double )in->grip[0], ( double )in->grip[1], ( double )cfg.grip_th );
		}
	}

	// distancia de cada mao ate o "ponto de pega" do estado corrente
	float anchor[3];
	if( g_ws.state == VR_WS_HOLSTER || g_ws.state == VR_WS_GROUND )
	{
		if( g_ws.state == VR_WS_HOLSTER ) for( int c = 0; c < 3; c++ ) anchor[c] = holster[c];
		else for( int c = 0; c < 3; c++ ) anchor[c] = g_ws.ground[c];
	}
	else
	{
		// na mao: o ponto de pega e a propria palma de tras
		for( int c = 0; c < 3; c++ ) anchor[c] = H[g_ws.rear].pos[c];
	}

	// solta: perto da barriga vira coldre; longe, cai no chao
	auto release = [&]( void )
	{
		float d[3] = { H[g_ws.rear].pos[0]-holster[0], H[g_ws.rear].pos[1]-holster[1], H[g_ws.rear].pos[2]-holster[2] };
		float dist = sqrtf( v3_dot( d, d ));

		if( dist <= cfg.holster_radius ) g_ws.state = VR_WS_HOLSTER;
		else
		{
			g_ws.state = VR_WS_GROUND;
			for( int c = 0; c < 3; c++ ) g_ws.ground[c] = H[g_ws.rear].pos[c];
			g_ws.fall_v = 0.0f;
			g_ws.landed = false;
		}
	};

	if( g_ws.state == VR_WS_HAND )
	{
		// duas maos: as duas apertam e a de apoio esta perto da ancora de apoio
		if( g_ws.two_hand )
		{
			float d[3] = { H[1-g_ws.rear].pos[0]-anchor[0], H[1-g_ws.rear].pos[1]-anchor[1], H[1-g_ws.rear].pos[2]-anchor[2] };
			if( !grip[1-g_ws.rear] || sqrtf( v3_dot( d, d )) > cfg.two_hand_radius * cfg.grab_hyst )
				g_ws.two_hand = false;
		}
		else if( grip[0] && grip[1] )
		{
			float d[3] = { H[1-g_ws.rear].pos[0]-anchor[0], H[1-g_ws.rear].pos[1]-anchor[1], H[1-g_ws.rear].pos[2]-anchor[2] };
			if( H[1-g_ws.rear].ok && sqrtf( v3_dot( d, d )) <= cfg.two_hand_radius )
				g_ws.two_hand = true;
		}
		// SOLTA na BORDA do "alguem esta segurando" (`held`), nao em "grip em 0":
		// no 1o frame ninguem apertou ainda e a arma era largada no chao antes de o
		// jogador ve-la ("nenhuma arma aparece, nem na mao nem no coldre"). Pegar ja
		// exigia borda; largar tambem. De duas maos, largar so a de tras nao derruba
		// (a de apoio ainda segura): `held` e a UNIAO das maos que seguram, e nao a
		// mao de tras — se fosse por mao, a de apoio assumir a arma nao contaria.
		{
			bool held = g_ws.two_hand ? ( grip[0] || grip[1] ) : grip[g_ws.rear];
			if( g_ws.held && !held ) release();
			g_ws.held = held;
		}
	}
	else
	{
		// no coldre ou no chao: a primeira mao que apertar o grip E estiver perto pega
		g_ws.two_hand = false;
		for( int h = 0; h < 2; h++ )
		{
			if( !edge[h] || !H[h].ok ) continue;
			float d[3] = { H[h].pos[0]-anchor[0], H[h].pos[1]-anchor[1], H[h].pos[2]-anchor[2] };
			if( sqrtf( v3_dot( d, d )) <= cfg.grab_radius )
			{
				int from = g_ws.state;
				g_ws.state = VR_WS_HAND;
				g_ws.rear = h;
				g_ws.held = true;   // a mao que pega esta apertando (a borda de soltar parte daqui)
				vlog( "maos: peguei a arma com a mao %s (%s)\n", h ? "direita" : "esquerda",
					( from == VR_WS_GROUND ) ? "do chao" : "do coldre" );
				break;
			}
		}
		if( g_ws.state == VR_WS_GROUND && !g_ws.landed )
		{
			// cai ate o chao do jogador (a ABI nao da raycast: o "chao" e o dele)
			g_ws.fall_v += cfg.drop_gravity * dt;
			g_ws.ground[2] -= g_ws.fall_v * dt;
			if( g_ws.ground[2] <= floor_z + 1.0f )
			{
				g_ws.ground[2] = floor_z + 1.0f;
				g_ws.landed = true;
				g_ws.ground_fwd[2] = 0.0f;   // deita: o cano fica no plano do chao
				vlog( "maos: arma largada no chao (%.0f %.0f %.0f)\n",
					( double )g_ws.ground[0], ( double )g_ws.ground[1], ( double )g_ws.ground[2] );
			}
		}
	}
	(void)dt;

	g_ws.grip_prev[0] = grip[0];
	g_ws.grip_prev[1] = grip[1];
}

// ------------------------------------------------------------ frames -------
// Pose da mao SINTETICA (diagnostico sem headset). Existe uma SO copia de proposito:
// o `frame_build` (desenho) e o `vr_hands_update` (estado/mira) precisam da MESMA pose,
// senao a folha de contato passa a mostrar uma coisa e a mira a apontar para outra.
//
// As duas maos ficam DENTRO do alcance do braco (~22 un). Alvo fora do alcance obriga a
// cadeia a ficar reta, e braco reto visto de perto parece "esticado" na folha: o defeito
// seria do alvo, nao da IK (o relato `braco:` imprime alvo/alcance/cotovelo para separar
// os dois casos). A mao esquerda sai menos para o lado porque o ombro esquerdo ja esta a
// ~8 un dali — com o mesmo deslocamento da direita ela cairia fora do alcance.
static void synth_hand_fill( const vr_hands_body_t *body, vr_hands_hand_t *H, int h )
{
	const float fw = cfg.synth_dist, sd = ( h == 1 ? cfg.synth_side : cfg.synth_side * 0.40f );
	const float dn = cfg.synth_down;
	for( int c = 0; c < 3; c++ )
		H->pos[c] = body->pos[c] + body->fwd[c]*fw + body->rgt[c]*sd - body->up[c]*dn;
	memcpy( H->fwd, body->fwd, sizeof( H->fwd ));
	memcpy( H->rgt, body->rgt, sizeof( H->rgt ));
	memcpy( H->up,  body->up,  sizeof( H->up  ));
	H->ok = true;
}

// Constroi a cena: rig ancorado no corpo, IK dos dois bracos, referencia da mao,
// arma (na mao/coldre/chao), dedos e skin. `force_state` < 0 = usa o estado real
// (a folha de contato passa valores fixos); `force_grip` < 0 = usa o grip de entrada.
static void frame_build( hscene_t *s, const vr_hands_body_t *body, const vr_hands_in_t *in,
	const vr_hands_hand_t hands[2], int force_state, float force_grip )
{
	if( !g_ready || !g_arms.rest_world || g_arms.j_bicep[0] < 0 || g_arms.j_bicep[1] < 0 ) return;

	// ---- 0) maos efetivas (reais ou sinteticas, por mao) ----
	vr_hands_hand_t H[2];
	for( int h = 0; h < 2; h++ )
	{
		bool real = hands && hands[h].ok && !cfg.synth_hands;
		if( real ) H[h] = hands[h];
		else synth_hand_fill( body, &H[h], h );
		s->hands_ok[h] = H[h].ok;
	}

	float grip[2] = { in->grip[0], in->grip[1] };
	if( force_grip >= 0.0f ) grip[0] = grip[1] = force_grip;

	// ---- 1) rig: gira pelo corpo e ancora o osso `head` na posicao do corpo ----
	m4 Rz, Marm, t1;
	const float d2r = ( float )M_PI / 180.0f;
	float rig_fwd[3], rig_rgt[3], rig_up[3];
	{
		// o rig e do WRAD: ele tem a propria convencao (cfg.arms_axes) e o `rig_yaw`
		// gira o conjunto em relacao ao tronco
		float yaw = 0.0f;
		// yaw do tronco a partir da propria base do corpo (GoldSrc: fwd=(cp*cy, cp*sy, -sp))
		yaw = ( float )( atan2( body->fwd[1], body->fwd[0] ) / M_PI * 180.0 );
		const float ye = ( yaw + cfg.rig_yaw ) * d2r;
		const float cy2 = cosf( ye ), sy2 = sinf( ye );
		memset( Rz, 0, sizeof( m4 ));
		Rz[0]=cy2; Rz[1]=sy2; Rz[4]=-sy2; Rz[5]=cy2; Rz[10]=1.0f; Rz[15]=1.0f;
		(void)rig_fwd; (void)rig_rgt; (void)rig_up;
	}
	m4 Sc;
	memset( Sc, 0, sizeof( m4 ));
	Sc[0]=Sc[5]=Sc[10]=cfg.scale_arms; Sc[15]=1.0f;
	m4_mul( t1, Rz, g_conv_arms );
	m4_mul( Marm, t1, Sc );
	{
		float hp[3];
		m4_xform_point( Marm, g_arms.rest_world[g_arms.j_head], hp );
		for( int c = 0; c < 3; c++ ) Marm[12+c] = body->pos[c] - hp[c];
	}

	// ---- 2) pose corrente: comeca do repouso e sobrescreve a raiz ----
	m4 *cur = ( m4 * )malloc( ( size_t )g_arms.njoint * sizeof( m4 ));
	for( int i = 0; i < g_arms.njoint; i++ ) memcpy( cur[i], g_arms.joint[i].local, sizeof( m4 ));
	m4_mul( cur[g_arms.j_root], Marm, g_arms.rest_world[g_arms.j_root] );

	// ---- 3) para ONDE a mao aponta: a frente do controle. E a UNICA coisa do
	// controle que manda na rotacao da mao — o ROLL vem do braco (passo 6) ----
	int st = ( force_state >= 0 ) ? force_state : g_ws.state;
	int rear = g_ws.rear;
	if( rear < 0 || rear > 1 ) rear = 1;
	const float zero[3] = { 0.0f, 0.0f, 0.0f };

	// referencial de repouso da palma transposto (palma -> rig) de cada mao, e a
	// direcao que a mao deve apontar
	m4 BrT[2];
	float aim_dir[2][3];
	for( int h = 0; h < 2; h++ )
	{
		m4 Br;
		m4_from_axes( Br, g_arms.palm_fwd[h], g_arms.palm_left[h], g_arms.palm_up[h], zero );
		m4_rot_transpose( BrT[h], Br );
		memcpy( aim_dir[h], H[h].fwd, sizeof( aim_dir[h] ));
	}

	// DUAS MAOS: o cano vai da mao de tras para a de apoio (a de apoio esta a
	// frente), misturado com a frente da propria mao de tras. A correcao entra AQUI,
	// na DIRECAO e antes do IK: assim a mao de tras gira JUNTO com a arma (senao o
	// cano sairia torto em relacao a propria mao que o segura) — sem herdar o roll
	// do controle, que agora vem do braco.
	bool two = ( st == VR_WS_HAND ) && g_ws.two_hand && H[1-rear].ok;
	if( two )
	{
		float d[3] = { H[1-rear].pos[0]-H[rear].pos[0], H[1-rear].pos[1]-H[rear].pos[1],
			H[1-rear].pos[2]-H[rear].pos[2] };
		float dist = sqrtf( v3_dot( d, d ));
		if( dist > 1.0f )
		{
			float aim2[3] = { d[0]/dist, d[1]/dist, d[2]/dist };
			float bl = cfg.two_hand_blend, mix[3];
			for( int c = 0; c < 3; c++ ) mix[c] = H[rear].fwd[c]*( 1.0f-bl ) + aim2[c]*bl;
			v3_norm( mix );
			memcpy( aim_dir[rear], mix, sizeof( mix ));
		}
	}

	// dedos: a mao que SEGURA a arma fecha no cabo mesmo com o analogico baixo. A
	// folha `curl` passa um valor fixo e quer o valor CRU (sem esse bonus).
	float curl[2];
	for( int h = 0; h < 2; h++ )
	{
		bool holding = ( force_grip < 0.0f ) && ( st == VR_WS_HAND )
			&& ( h == rear || ( two && h == 1-rear ));
		curl[h] = holding ? fmaxf( grip[h], cfg.curl_fist ) : grip[h];
	}

	// ---- 5) IK: a mao de tras primeiro — a de apoio pode mirar a ARMA ----
	float support_target[3] = { 0.0f, 0.0f, 0.0f };
	bool ik_ok[2] = { false, false };
	s->wrist_slip[0] = s->wrist_slip[1] = -1.0f;   // -1 = a IK nao resolveu esta mao
	s->wrist_twist[0] = s->wrist_twist[1] = 0.0f;
	for( int pass = 0; pass < 2; pass++ )
	{
		int h = pass ? 1 - rear : rear;
		float target[3];
		if( pass == 1 && two ) for( int c = 0; c < 3; c++ ) target[c] = support_target[c];
		else for( int c = 0; c < 3; c++ ) target[c] = H[h].pos[c];

		const int jb = g_arms.j_bicep[h], jf = g_arms.j_fore[h], jw = g_arms.j_wrist[h];
		m4 Wb, Wf, Ww;
		cur_world( &g_arms, cur, jb, Wb );
		cur_world( &g_arms, cur, jf, Wf );
		cur_world( &g_arms, cur, jw, Ww );

		float e0[3] = { Wb[12], Wb[13], Wb[14] };
		float L1 = sqrtf( ( Wf[12]-e0[0] )*( Wf[12]-e0[0] ) + ( Wf[13]-e0[1] )*( Wf[13]-e0[1] ) + ( Wf[14]-e0[2] )*( Wf[14]-e0[2] ) );
		float L2 = sqrtf( ( Ww[12]-Wf[12] )*( Ww[12]-Wf[12] ) + ( Ww[13]-Wf[13] )*( Ww[13]-Wf[13] ) + ( Ww[14]-Wf[14] )*( Ww[14]-Wf[14] ) );
		if( L1 < 1e-4f || L2 < 1e-4f ) continue;

		float d[3] = { target[0]-e0[0], target[1]-e0[1], target[2]-e0[2] };
		float dist = sqrtf( v3_dot( d, d ));
		if( dist < 1e-4f ) continue;
		float n[3] = { d[0]/dist, d[1]/dist, d[2]/dist };
		// diagnostico: o alvo esta ao alcance? (dist > reach = braco necessariamente
		// esticado, e ai a culpa e do alvo, nao da cadeia)
		s->arm_target[h] = dist;
		s->arm_reach[h] = L1 + L2;
		float dmax = ( L1 + L2 )*0.999f, dmin = fabsf( L1 - L2 )*1.05f + 1e-3f;
		if( dist > dmax ) dist = dmax;
		if( dist < dmin ) dist = dmin;

		// cotovelo: circulo em torno do ombro, no plano do polo (para baixo)
		const float pole[3] = { 0.0f, 0.0f, -1.0f };
		const float aa = ( L1*L1 - L2*L2 + dist*dist ) / ( 2.0f*dist );
		float hh = L1*L1 - aa*aa; hh = hh > 0 ? sqrtf( hh ) : 0.0f;
		float pv[3] = { pole[0], pole[1], pole[2] };
		float dp = v3_dot( pv, n );
		for( int c = 0; c < 3; c++ ) pv[c] -= n[c]*dp;
		v3_norm( pv );
		float E1[3];
		for( int c = 0; c < 3; c++ ) E1[c] = e0[c] + n[c]*aa + pv[c]*hh;

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

		// antebraco: agora segue o braco; gira ate apontar o alvo
		m4 Wf2, Ww2;
		cur_world( &g_arms, cur, jf, Wf2 );
		cur_world( &g_arms, cur, jw, Ww2 );
		float e2[3] = { Wf2[12], Wf2[13], Wf2[14] };
		float rf[3] = { Ww2[12]-e2[0], Ww2[13]-e2[1], Ww2[14]-e2[2] };
		float nf[3] = { target[0]-e2[0], target[1]-e2[1], target[2]-e2[2] };
		v3_norm( rf ); v3_norm( nf );
		{
			// angulo do cotovelo: entre o braco (nu) e o antebraco (nf). 0 = esticado
			float dot2 = v3_dot( nu, nf );
			if( dot2 > 1.0f ) dot2 = 1.0f;
			if( dot2 < -1.0f ) dot2 = -1.0f;
			s->elbow_deg[h] = acosf( dot2 ) * 180.0f / ( float )M_PI;
		}
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

		// ---- 6) mao: DIRECAO do controle + ROLL do braco + a posicao da IK ----
		m4 Wwk;
		cur_world( &g_arms, cur, jw, Wwk );
		float wr[3] = { Wwk[12], Wwk[13], Wwk[14] };

		// O punho NATURAL deste rig: o roll sai do antebraco que a IK acabou de
		// resolver — `R(Wf3) . R(rest_world[jf])^T` leva o rig para o mundo com o
		// punho no repouso. Antes a mao era `Bc . Br^T`, o que OBRIGAVA o referencial
		// da palma a coincidir com o do controle: a diferenca entre "eixos do
		// controle" e "punho natural do rig" virava TORCAO do punho na tela — igual
		// nas duas maos (o mapa era o mesmo) e maior na direita, que ainda levava a
		// mistura do cano de duas maos. O controle manda na DIRECAO; o roll vem do
		// braco. `wrist_roll` devolve o roll do controle (1 = comportamento anterior).
		m4 Wf3r, RjT, Rwnat, Bw, Rw;
		memcpy( Wf3r, Wf3, sizeof( m4 ));
		Wf3r[12] = Wf3r[13] = Wf3r[14] = 0.0f;   // so a rotacao (a escala do rig fica nela)
		m4_rot_transpose( RjT, g_arms.rest_world[jf] );
		m4_mul( Rwnat, Wf3r, RjT );

		float nat_fwd[3], nat_up[3];
		m4_xform_dir( Rwnat, g_arms.palm_fwd[h], nat_fwd ); v3_norm( nat_fwd );
		m4_xform_dir( Rwnat, g_arms.palm_up[h],  nat_up );  v3_norm( nat_up );

		// diagnostico: quanta torcao o CONTROLE pedia no punho (0 = o controle ja
		// concorda com o punho natural do rig). E o numero do relato "punho girado".
		{
			float cx[3];
			v3_cross( cx, nat_up, H[h].up );
			float cs = sqrtf( v3_dot( cx, cx ));
			float ang = atan2f( cs, v3_dot( nat_up, H[h].up )) * 180.0f / ( float )M_PI;
			s->wrist_twist[h] = ( v3_dot( cx, aim_dir[h] ) < 0.0f ) ? -ang : ang;
		}

		// eixos da mao no mundo: frente = a direcao pedida, cima = o roll misturado
		// (o de cima tem de sair perpendicular a frente: Gram-Schmidt)
		float fw2[3], up2[3], lf2[3];
		memcpy( fw2, aim_dir[h], sizeof( fw2 )); v3_norm( fw2 );
		for( int c = 0; c < 3; c++ )
			up2[c] = nat_up[c]*( 1.0f-cfg.wrist_roll ) + H[h].up[c]*cfg.wrist_roll;
		float proj = v3_dot( up2, fw2 );
		for( int c = 0; c < 3; c++ ) up2[c] -= fw2[c]*proj;
		// o controle pode pedir uma direcao paralela ao "cima" natural e zerar o vetor
		// acima (rota de NaN no v3_norm). O eixo do repouso e perpendicular a direcao e
		// serve de reserva; os dois nao podem degenerar juntos (frente e cima sao
		// perpendiculares no repouso).
		if( v3_dot( up2, up2 ) < 1e-6f )
		{
			float tmp[3];
			v3_cross( tmp, fw2, nat_fwd );
			memcpy( up2, tmp, sizeof( up2 ));
		}
		v3_norm( up2 );
		v3_cross( lf2, up2, fw2 );               // esquerda = cima x frente (como no resto)
		m4_from_axes( Bw, fw2, lf2, up2, zero );
		m4_mul( Rw, Bw, BrT[h] );

		// A mao e o UNICO osso cujo mundo e escrito por fora da cadeia (o IK posiciona
		// o antebraco; o controle decide a rotacao da mao). O mundo que se quer e
		//   P_mao . R(repouso do punho)  -- a rotacao do repouso SEM a translacao
		// e nao `P_mao . rest_world[jw]`. A translacao do repouso e a POSICAO do punho
		// dentro do espaco do rig (~6 unidades, o rig e um corpo inteiro), e como o skin
		// e `cur_world . ibm` ela NAO cancela: entrava como `wr + s.Rw.p_repouso`, jogando
		// a malha da mao ~26 u (66 cm) para longe do punho. O skin esticava entre o
		// antebraco (correto) e a mao (deslocada): "antebraco gigante, mao la longe".
		// Sem ela, o skin do punho fica exatamente T(wr).S(s).Rw.T(-p_repouso) — roda a
		// mao em torno do proprio punho e poe o punho onde o IK o pos.
		m4 Ph, Rho, ip;
		m4_place( Ph, Rw, wr, cfg.scale_arms );
		memcpy( Rho, g_arms.rest_world[jw], sizeof( m4 ));
		Rho[12] = Rho[13] = Rho[14] = 0.0f;
		m4_inv( ip, Wf3 );
		m4_mul( t1, ip, Ph );
		m4_mul( cur[jw], t1, Rho );

		// onde o IK pos o punho: e contra isto que a malha e conferida no passo 9
		memcpy( s->wrist_ik[h], wr, sizeof( wr ));
		ik_ok[h] = true;

		// referencia da mao (para a arma e para a folha): centro da palma
		float ctr[3];
		m4_xform_point( Ph, g_arms.palm_ctr[h], ctr );
		for( int c = 0; c < 3; c++ ) s->palm_pos[h][c] = ctr[c];

		float ax[3], ay[3], az[3];
		m4_xform_dir( Ph, g_arms.palm_fwd[h], ax );
		m4_xform_dir( Ph, g_arms.palm_left[h], ay );
		m4_xform_dir( Ph, g_arms.palm_up[h], az );
		v3_norm( ax ); v3_norm( ay ); v3_norm( az );
		m4_from_axes( s->hand[h], ax, ay, az, ctr );

		// a mao de tras ja foi resolvida: a de apoio mira o ponto de apoio DA ARMA
		// (duas maos = a arma conduz a mao, que e o padrao dos jogos)
		if( pass == 0 && two && h == rear )
		{
			const float sf = cfg.support_fwd, ss = cfg.support_side, su = cfg.support_up;
			for( int c = 0; c < 3; c++ )
				support_target[c] = ctr[c] + ax[c]*sf - ay[c]*ss + az[c]*su;
		}
	}

	// ---- 7) arma: a MESMA montagem para a mao, o coldre e o chao ----
	// (o euler do cfg e resolvido no LOCAL de cada referencial — ver residual_matrix)
	s->weapon_ok = false;
	const float zz[3] = { 0.0f, 0.0f, 0.0f };
	{
		if( st == VR_WS_HAND )
		{
			// recuo: para tras no eixo do cano e a boca para cima (tudo no
			// referencial da mao, em unidades GoldSrc — nada de escala dobrada)
			float off[3] = { cfg.wpn_off[0] - cfg.recoil_kick * g_recoil, cfg.wpn_off[1], cfg.wpn_off[2] };
			mount_weapon( s->weapon, s->hand[rear], off, cfg.wpn_pitch + cfg.recoil_deg * g_recoil,
				cfg.wpn_yaw, cfg.wpn_roll, g_weapon_scale );
			s->weapon_ok = g_weapon.loaded;
		}
		else if( st == VR_WS_HOLSTER )
		{
			float holster[3];
			float bl[3] = { -body->rgt[0], -body->rgt[1], -body->rgt[2] };
			m4 Mh;
			for( int c = 0; c < 3; c++ )
				holster[c] = body->pos[c] + body->fwd[c]*cfg.holster_fwd
					+ body->rgt[c]*cfg.holster_side - body->up[c]*cfg.holster_down;
			m4_from_axes( Mh, body->fwd, bl, body->up, holster );
			mount_weapon( s->weapon, Mh, zz, cfg.holster_euler[0], cfg.holster_euler[1],
				cfg.holster_euler[2], g_weapon_scale );
			s->weapon_ok = g_weapon.loaded;
		}
		else
		{
			// largada: deitada no plano do chao do jogador (o cano fica horizontal)
			float fwd[3] = { g_ws.ground_fwd[0], g_ws.ground_fwd[1], 0.0f };
			if( sqrtf( v3_dot( fwd, fwd )) < 0.1f ) { fwd[0] = 1.0f; fwd[1] = 0.0f; }
			v3_norm( fwd );
			const float up[3] = { 0.0f, 0.0f, 1.0f };
			float lf[3];
			m4 Mh;
			v3_cross( lf, up, fwd );
			m4_from_axes( Mh, fwd, lf, up, g_ws.ground );
			mount_weapon( s->weapon, Mh, zz, 0.0f, 0.0f, 0.0f, g_weapon_scale );
			s->weapon_ok = g_weapon.loaded;
		}
	}

	if( s->weapon_ok )
	{
		const float gp[3] = { cfg.weapon_grip[0], cfg.weapon_grip[1], cfg.weapon_grip[2] };
		m4_xform_point( s->weapon, gp, s->grip_world );
		// a direcao do cano sai da COLUNA 0 da matriz (m[0] m[1] m[2]) em qualquer
		// estado: na mao serve para mirar, no coldre/chao serve para diagnostico
		for( int c = 0; c < 3; c++ ) s->aim[c] = s->weapon[c];
		v3_norm( s->aim );
		if( st == VR_WS_HAND )
		{
			float d[3];
			for( int c = 0; c < 3; c++ ) d[c] = s->grip_world[c] - s->palm_pos[rear][c];
			s->fit_dist = sqrtf( v3_dot( d, d ));   // ~0 = o encaixe resolveu mesmo
		}
	}

	// ---- 8) dedos: flexao derivada do repouso ----
	for( int h = 0; h < 2; h++ )
	{
		float c0 = curl[h];
		if( c0 < 0.0f ) c0 = 0.0f;
		if( c0 > 1.0f ) c0 = 1.0f;
		for( int f = 0; f < HFING; f++ )
			for( int k = 0; k < HJOINTS_PER_FING; k++ )
			{
				const int j = g_arms.j_finger[h][f][k];
				if( j < 0 ) continue;
				m4 Rc, t2;
				float deg = c0 * cfg.curl_max[k] * cfg.curl_w[f];
				m4_axis_rot( Rc, g_arms.fin_axis[h][f][k], deg );
				m4_mul( t2, g_arms.joint[j].local, Rc );
				memcpy( cur[j], t2, sizeof( m4 ));
			}
	}

	// ---- 9) skin com a pose corrente (1x por frame) ----
	m4 *skin = ( m4 * )malloc( ( size_t )g_arms.njoint * sizeof( m4 ));
	for( int i = 0; i < g_arms.njoint; i++ )
	{
		m4 w;
		cur_world( &g_arms, cur, i, w );
		m4_mul( skin[i], w, g_arms.joint[i].ibm );
	}

	// METRICA DE ACEITE da cadeia da mao: pega o ponto de REPOUSO do punho, passa pelo
	// skin e confere se ele caiu onde a IK pos o punho. O skin do punho e
	// T(punho).S(s).Rw.T(-repouso), entao o ponto de repouso tem de voltar exatamente
	// em `wrist_ik` — o numero TEM de ser 0.00. Nao e decoracao: foi assim que o bug da
	// translacao do repouso (malha da mao 26 u longe, antebraco esticado) apareceu.
	for( int h = 0; h < 2; h++ )
	{
		const int jw = g_arms.j_wrist[h];
		s->palm_slip[h] = -1.0f;
		if( !ik_ok[h] || jw < 0 ) continue;
		float p[3] = { g_arms.rest_world[jw][12], g_arms.rest_world[jw][13],
			g_arms.rest_world[jw][14] }, q[3];
		m4_xform_point( skin[jw], p, q );
		float d[3] = { q[0]-s->wrist_ik[h][0], q[1]-s->wrist_ik[h][1], q[2]-s->wrist_ik[h][2] };
		s->wrist_slip[h] = sqrtf( v3_dot( d, d ));

		// e o CENTRO DA PALMA da malha tem de cair onde esta o referencial da mao (que e
		// onde a arma ancora a empunhadura). O ponto absoluto do centro da palma no rig e
		// `palm_ctr` (relativo ao punho) + a posicao de repouso do punho.
		float pa[3] = { g_arms.palm_ctr[h][0] + p[0], g_arms.palm_ctr[h][1] + p[1],
			g_arms.palm_ctr[h][2] + p[2] };
		m4_xform_point( skin[jw], pa, q );
		float e[3] = { q[0]-s->palm_pos[h][0], q[1]-s->palm_pos[h][1], q[2]-s->palm_pos[h][2] };
		s->palm_slip[h] = sqrtf( v3_dot( e, e ));
	}

	free( s->cur ); free( s->skin );
	s->cur = cur; s->skin = skin; s->njoint = g_arms.njoint;

	for( int c = 0; c < 3; c++ )
		s->focus[c] = s->weapon_ok ? s->grip_world[c] : s->palm_pos[rear][c];
}

// ------------------------------------------------------------ publico ------
bool vr_hands_ready( void ) { return g_ready; }

// A arma tem de estar NA MAO para o gatilho valer? (o mod consulta no VR_Input)
bool vr_hands_enforce_armed( void ) { return g_ready && cfg.enforce_armed; }

void vr_hands_set_sheet( int mode )
{
	if( mode < 0 || mode > VR_SHEET_WALK ) return;
	g_sheet = mode;
	g_sheet_forced = true;   // o cfg nao desliga mais nesta sessao
}

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
	scene_free( &g_scene );
	g_ready = false;

	axes_from_spec( g_conv_arms, cfg.arms_axes );
	axes_from_spec( g_conv_weapon, cfg.weapon_axes );

	if( !load_gltf( &g_arms, vr_asset_path(cfg.arms_glb) )) return;
	if( g_arms.tex == 0 && cfg.arms_tex[0] )
	{
		unsigned char *b = NULL; int n = 0;
		if( file_read( vr_asset_path(cfg.arms_tex), &b, &n )) { g_arms.tex = tex_from_memory( b, n ); free( b ); }
	}
	char mtl[168];
	snprintf( mtl, sizeof( mtl ), "%s", vr_asset_path(cfg.weapon_obj) );
	char *dot = strrchr( mtl, '.' );
	if( dot ) snprintf( dot, ( size_t )( mtl + sizeof( mtl ) - dot ), ".mtl" );
	load_obj( &g_weapon, vr_asset_path(cfg.weapon_obj), mtl );

	// escala da arma resolvida UMA vez: comprimento real alvo (cm) / medido do
	// modelo. E o unico lugar de onde a escala sai (o desenho nao recalcula).
	g_weapon_scale = 0.0f;
	if( g_weapon.loaded && g_weapon.len[0] > 1e-4f && cfg.weapon_length_cm > 0.0f )
		g_weapon_scale = cfg.scale_weapon * ( cfg.weapon_length_cm / 2.54f ) / g_weapon.len[0];

	g_ready = g_arms.loaded;
	// sem a referencia da palma das DUAS maos o encaixe nao tem onde ancorar: melhor
	// desligar (o jogo segue com o viewmodel dele) do que desenhar com base zerada
	if( g_ready && ( !g_arms.palm_ok[0] || !g_arms.palm_ok[1] ))
	{
		vlog( "maos: o rig nao deu a referencia da palma das duas maos — subsistema desligado\n" );
		g_ready = false;
	}
	if( g_ready )
		vlog( "maos: escala da arma %.2f un./un. (%.1f cm / %.2f un. do modelo) — bracos %d tri / arma %d tri\n",
			( double )g_weapon_scale, ( double )cfg.weapon_length_cm,
			( double ) g_weapon.len[0], g_arms.ntri, g_weapon.ntri );
}

bool vr_hands_init( vr_hands_log_fn log )
{
	g_log = log;
	cfg_parse( vr_config_path("vr_hands.cfg") );
	if( !gl_load()) return false;

	g_ws.state = VR_WS_HAND;      // comeca com a arma na mao (ver docs/HANDS.md)
	g_ws.rear = 1;
	g_ws.ground_fwd[0] = 1.0f;
	vr_hands_reload();
	if( g_ready ) vlog( "maos: pronto — bracos %d tri / arma %d tri\n", g_arms.ntri, g_weapon.ntri );
	else vlog( "maos: indisponivel (asset nao carregou) — o mod segue como antes\n" );
	return g_ready;
}

void vr_hands_shutdown( void )
{
	mesh_clear( &g_arms );
	mesh_clear( &g_weapon );
	scene_free( &g_scene );
	g_ready = false;
}

void vr_hands_tick( void )
{
	struct stat st;
	if( stat( vr_config_path("vr_hands.cfg"), &st ) != 0 ) return;
	if( ( double )st.st_mtime != cfg_mtime )
	{
		cfg_mtime = ( double )st.st_mtime;
		cfg_parse( vr_config_path("vr_hands.cfg") );
		// releitura recarrega os assets: e o que faz "editar o cfg e ver o
		// resultado" valer para eixos, ancoras, escala e o modo da folha, sem
		// reiniciar o jogo (o custo e ler 450 KB do disco, 1x por edicao)
		vr_hands_reload();
		vlog( "maos: cfg relido (assets recarregados)\n" );
	}
}

void vr_hands_update( const vr_hands_body_t *body, const vr_hands_in_t *in,
	const vr_hands_hand_t hands[2], vr_hands_state_t *out )
{
	if( out ) memset( out, 0, sizeof( *out ));
	if( !g_ready || !body || !in ) return;

	// maos sinteticas (sem headset / `synth_hands`): o grip tambem e sintetico, e
	// vem do cfg — senao o grip em 0 soltaria a arma no 1o frame do diagnostico.
	// SO a mao de tras aperta: com as DUAS apertando, as duas maos sinteticas (3 un.
	// de distancia) entravam no estado de duas maos e a mao DIREITA ganhava sozinha a
	// mistura do cano — a folha mostrava um punho torto que nao existe no estado
	// natural, com o dobro da torcao da esquerda.
	vr_hands_in_t in2 = *in;
	if( !hands || cfg.synth_hands )
	{
		const int r = ( g_ws.rear == 0 ) ? 0 : 1;
		in2.grip[0] = ( r == 0 ) ? cfg.synth_grip : 0.0f;
		in2.grip[1] = ( r == 1 ) ? cfg.synth_grip : 0.0f;
	}

	g_time += ( in2.dt > 0.0f && in2.dt < 0.5f ) ? in2.dt : 0.016;

	// recuo visual: o 1o tiro sai na BORDA do gatilho; segurando, repete na cadencia
	// (o recuo REAL, que desvia o tiro, e do jogo — aqui e so o retorno visual)
	{
		static bool fire_prev;
		const double gap = cfg.recoil_interval > 0.01f ? cfg.recoil_interval : 0.09f;
		if( in->fire && ( !fire_prev || g_time >= g_recoil_next ))
		{
			g_recoil = 1.0f;
			g_recoil_next = g_time + gap;
		}
		fire_prev = in->fire;
	}

	// maos efetivas (para o estado; a cena resolve as sinteticas de novo)
	vr_hands_hand_t H[2];
	for( int h = 0; h < 2; h++ )
	{
		if( hands && hands[h].ok )
		{
			H[h] = hands[h];
			if( cfg.synth_hands ) H[h].ok = false;
		}
		else synth_hand_fill( body, &H[h], h );
	}

	// estado da arma (na mao / coldre / chao) e so DEPOIS a pose: a maquina usa as
	// posicoes cruas dos controles, entao nao ha dependencia circular com o IK
	weapon_step( body, &in2, H, in2.dt );
	frame_build( &g_scene, body, &in2, hands, -1, -1.0f );

	// o recuo decai 1x por FRAME (nao por olho, senao decairia duas vezes)
	g_recoil *= 0.80f;
	if( g_recoil < 0.001f ) g_recoil = 0.0f;

	if( out )
	{
		out->state = g_ws.state;
		out->rear_hand = g_ws.rear;
		out->two_hand = g_ws.two_hand;
		out->armed = ( g_ws.state == VR_WS_HAND );
		out->fit_dist = g_scene.fit_dist;
		out->scale = g_weapon_scale;
		if( g_scene.weapon_ok && out->armed )
			memcpy( out->aim, g_scene.aim, sizeof( out->aim ));
	}
}

// Desenha a cena ja resolvida (o braco entra IDENTIDADE: as matrizes de skin ja
// estao em mundo — cur[root] carrega o Marm).
static void draw_scene( const vr_hands_cam_t *cam, const hscene_t *s, bool arms, bool weapon,
	bool markers )
{
	if( !s || !s->skin ) return;

	float fwd[3], rgt[3], up[3];
	cam_basis( cam, fwd, rgt, up );
	const float light[3] = { fwd[0], fwd[1], fwd[2] };
	m4 IDENT; m4_ident( IDENT );

	for( int c = 0; c < 3; c++ ) { g_cam_org[c] = cam->origin[c]; g_vlo[c] = 1e9f; g_vhi[c] = -1e9f; }
	g_vcount = 0; g_min_dist = 1e9f;

	if( arms ) draw_mesh( &g_arms, ( const float * )IDENT, s->skin, light );
	if( weapon && s->weapon_ok ) draw_mesh( &g_weapon, s->weapon, NULL, light );

	if( markers && s->weapon_ok )
	{
		// marcadores SEM profundidade: a cruz da empunhadura e o eixo do cano ficam
		// visiveis por cima da arma (e o que prova que as ancoras estao certas)
		p_glDisable( GL_DEPTH_TEST );
		p_glLineWidth( 2.0f );

		const float g3[3] = { cfg.weapon_grip[0], cfg.weapon_grip[1], cfg.weapon_grip[2] };
		float ax[3], ay[3], az[3], o[3];
		m4_xform_point( s->weapon, g3, o );
		m4_xform_dir( s->weapon, ( const float[3] ){ 1, 0, 0 }, ax );
		m4_xform_dir( s->weapon, ( const float[3] ){ 0, 1, 0 }, ay );
		m4_xform_dir( s->weapon, ( const float[3] ){ 0, 0, 1 }, az );
		v3_norm( ax ); v3_norm( ay ); v3_norm( az );

		const float L = g_weapon.len[0] * g_weapon_scale;
		float a[3], b[3];
		for( int c = 0; c < 3; c++ ) { a[c] = o[c] - ax[c]*2.0f; b[c] = o[c] + ax[c]*2.0f; }
		draw_line( a, b, 1.0f, 0.2f, 0.2f );                     // vermelho: eixo do cano
		for( int c = 0; c < 3; c++ ) { a[c] = o[c] - ay[c]*1.5f; b[c] = o[c] + ay[c]*1.5f; }
		draw_line( a, b, 0.2f, 1.0f, 0.2f );                     // verde: lado
		for( int c = 0; c < 3; c++ ) { a[c] = o[c] - az[c]*1.5f; b[c] = o[c] + az[c]*1.5f; }
		draw_line( a, b, 0.3f, 0.5f, 1.0f );                     // azul: cima da arma
		for( int c = 0; c < 3; c++ ) { a[c] = o[c]; b[c] = o[c] + ax[c]*L; }
		draw_line( a, b, 1.0f, 1.0f, 1.0f );                     // branco: comprimento

		p_glLineWidth( 1.0f );
		p_glEnable( GL_DEPTH_TEST );
	}
}

void vr_hands_draw( const vr_hands_cam_t *cam )
{
	if( !cam || !cam->ok || cam->w <= 0 || !g_scene.skin ) return;
	// com a folha de contato ligada, quem desenha e ELA (a folha mostra a MESMA
	// pose em grade; desenhar por cima estragaria a grade)
	if( g_sheet != VR_SHEET_OFF ) return;

	apply_3d_state( cam, 0, 0, cam->w, cam->h );
	draw_scene( cam, &g_scene, true, true, false );
	restore_3d_state();

	if( g_draws < 3 )
	{
		vlog( "maos dbg: verts=%ld bbox GL (%.0f %.0f %.0f)..(%.0f %.0f %.0f) dist_min=%.1f | estado %s | encaixe %.2f un | escala %.2f un/un\n",
			g_vcount,
			( double )g_vlo[0], ( double )g_vlo[1], ( double )g_vlo[2],
			( double )g_vhi[0], ( double )g_vhi[1], ( double )g_vhi[2], ( double )g_min_dist,
			g_ws.state == VR_WS_HAND ? "na mao" : ( g_ws.state == VR_WS_HOLSTER ? "coldre" : "chao" ),
			( double )g_scene.fit_dist, ( double )g_weapon_scale );
		g_draws++;
	}
}

// ------------------------------------------------------ folha de contato ----
// Grade de vistas da MESMA pose resolvida, cada celula com limpeza propria (cor +
// profundidade) e camera orbitando o ponto de interesse. Sem headset, serve para
// decidir por imagem se o encaixe/escala/dedos/estados estao certos.
typedef struct { int cols, rows; const char *name; } sheet_geom_t;

static void cell_camera( vr_hands_cam_t *c, const float focus[3], float dist, float yaw, float pitch,
	int px, int py, int w, int h )
{
	float ang[3] = { pitch, yaw, 0.0f };
	float fwd[3], rgt[3], up[3];
	angles_basis( ang, fwd, rgt, up );
	for( int i = 0; i < 3; i++ ) c->origin[i] = focus[i] - fwd[i]*dist;
	memcpy( c->angles, ang, sizeof( ang ));
	const float half = 30.0f * ( float )M_PI / 180.0f * 0.5f;   // fov total ~30 graus
	c->fov[0] = -half; c->fov[1] = half; c->fov[2] = half; c->fov[3] = -half;
	c->w = w; c->h = h;
	c->ok = true;
	(void)px; (void)py;
}

void vr_hands_sheet( const vr_hands_cam_t *cam )
{
	if( !g_ready || !cam || !cam->ok || cam->w <= 0 || g_sheet == VR_SHEET_OFF ) return;
	if( !g_arms.rest_world ) return;

	// corpo sintetico a partir da propria camera: e um "jogador" parado onde a
	// camera esta, com as maos a frente (mesma pose que o modo de teste usava)
	vr_hands_body_t body;
	vr_hands_in_t in;
	float fwd[3], rgt[3], up[3];
	cam_basis( cam, fwd, rgt, up );
	memcpy( body.pos, cam->origin, sizeof( body.pos ));
	memcpy( body.fwd, fwd, sizeof( body.fwd ));
	memcpy( body.rgt, rgt, sizeof( body.rgt ));
	memcpy( body.up,  up,  sizeof( body.up  ));
	body.eye_height = 28.0f;
	body.floor_z = cam->origin[2] - 28.0f;
	memset( &in, 0, sizeof( in ));
	in.grip[0] = in.grip[1] = cfg.synth_grip;

	int cols = 4, rows = 3;
	int mode = g_sheet;
	if( mode == VR_SHEET_CURL ) rows = 2;
	if( mode == VR_SHEET_STATES ) { cols = 2; rows = 2; }
	if( mode == VR_SHEET_WALK )   { cols = 4; rows = 2; }

	const int cw = cam->w / cols, ch = cam->h / rows;

	// As vistas da folha sao RELATIVAS AO CORPO (yaw do tronco), nao ao mundo: com
	// o mundo, "yaw 0" cairia numa direcao qualquer do mapa e nao daria para dizer
	// se o cano esta para a frente do jogador ou nao. Assim, 0 = visto de frente
	// pelo jogador, 90 = do lado esquerdo dele, 180 = por tras.
	const float body_yaw = ( float )( atan2( body.fwd[1], body.fwd[0] ) * 180.0 / M_PI );

	p_glPushAttrib( GL_ALL_ATTRIB_BITS );
	p_glEnable( GL_SCISSOR_TEST );

	// o roteiro (`walk`) roda a maquina de verdade ENTRE as celulas, entao o estado
	// tem de sobreviver de um passo para o outro: ele e salvo UMA vez aqui e
	// devolvido no fim. Nos outros modos o estado e forcado por celula.
	const int  walk_state0 = g_ws.state;
	const int  walk_rear0 = g_ws.rear;
	const bool walk_two0 = g_ws.two_hand;
	const bool walk_held0 = g_ws.held;
	const bool walk_prev0[2] = { g_ws.grip_prev[0], g_ws.grip_prev[1] };

	for( int row = 0; row < rows; row++ )
	{
		for( int col = 0; col < cols; col++ )
		{
			// o que esta celula mostra
			float grip = cfg.synth_grip;
			int   state = VR_WS_HAND;
			float yaw = 0.0f, pitch = 15.0f;
			float dist = 26.0f;
			// modo walk: roteiro pela maquina de estados (maos explicitas por passo)
			vr_hands_hand_t Hcell[2];
			bool cell_hands = false;
			float gr[2] = { 0.0f, 0.0f };
			int   rep = 1;

			if( mode == VR_SHEET_FIT )
			{
				const float yaws[4] = { 0.0f, 90.0f, 180.0f, 270.0f };
				const float pits[3] = { -20.0f, 10.0f, 45.0f };
				yaw = yaws[col]; pitch = pits[2-row];
				dist = 34.0f;
			}
			else if( mode == VR_SHEET_WEAPON )
			{
				const float yaws[4] = { 0.0f, 90.0f, 180.0f, 270.0f };
				const float pits[3] = { -25.0f, 0.0f, 30.0f };
				yaw = yaws[col]; pitch = pits[2-row];
				dist = 14.0f;
			}
			else if( mode == VR_SHEET_CURL )
			{
				const float lv[4] = { 0.0f, 0.33f, 0.66f, 1.0f };
				grip = lv[col];
				yaw = ( row == 0 ) ? 35.0f : -35.0f;   // olha a mao dir / esq
				pitch = 25.0f;
				dist = 26.0f;
			}
			else if( mode == VR_SHEET_STATES )
			{
				state = ( row == 0 ) ? VR_WS_HAND : ( col == 0 ? VR_WS_HOLSTER : VR_WS_GROUND );
				grip = ( row == 0 && col == 1 ) ? 1.0f : cfg.synth_grip;
				yaw = 60.0f; pitch = 5.0f;
				// distancia maior: cabe o coldre na barriga E as maos livres acima
				dist = ( row == 0 && col == 1 ) ? 70.0f : ( row == 0 ? 50.0f : 55.0f );
				if( row == 0 && col == 1 )
				{
					// duas maos DE VERDADE: a de apoio vai a FRENTE da de tras, como
					// quem segura uma pistola com as duas maos — nao ao lado. Com as
					// duas lado a lado, o cano (mao de tras -> mao de apoio) apontava
					// para o lado e a folha mostrava um punho torto que nao existe.
					synth_hand_fill( &body, &Hcell[1], 1 );   // mao de tras: a direita
					memcpy( &Hcell[0], &Hcell[1], sizeof( Hcell[0] ));
					for( int c = 0; c < 3; c++ )
						Hcell[0].pos[c] += body.fwd[c]*cfg.support_fwd - body.rgt[c]*cfg.support_side;
					cell_hands = true;
				}
			}
			else if( mode == VR_SHEET_WALK )
			{
				// ROTEIRO: cada celula e um passo. As maos sao EXPLICITAS (a "de tras"
				// na posicao do passo, a outra ao lado) e a maquina de estados roda de
				// verdade — o log diz o estado de cada passo. E o aceite da Fase 3 sem
				// headset: pegar, guardar no coldre, largar no chao e juntar as maos.
				const int s = row * cols + col;
				float holster[3];

				for( int c = 0; c < 3; c++ )
					holster[c] = body.pos[c] + body.fwd[c]*cfg.holster_fwd
						+ body.rgt[c]*cfg.holster_side - body.up[c]*cfg.holster_down;

				float pR[3], pO[3];   // mao de tras / a outra, no mundo
				for( int c = 0; c < 3; c++ )
				{
					pR[c] = body.pos[c] + body.fwd[c]*24.0f + body.rgt[c]*5.5f - body.up[c]*9.0f;
					pO[c] = body.pos[c] + body.fwd[c]*24.0f - body.rgt[c]*5.5f - body.up[c]*9.0f;
				}

				switch( s )
				{
				case 0: gr[1] = 1.0f; break;                     // na mao (mao direita)
				case 1: break;                                   // solta longe do coldre -> cai
				case 2: rep = 25; break;                         // ... ate pousar no chao
				case 3:                                          // pega do chao com a ESQUERDA
					for( int c = 0; c < 3; c++ ) pO[c] = g_ws.ground[c];
					gr[0] = 1.0f;
					break;
				case 4:                                          // as duas maos (empilhadas)
					for( int c = 0; c < 3; c++ )
					{
						pR[c] = body.pos[c] + body.fwd[c]*20.0f - body.up[c]*9.0f;
						pO[c] = pR[c] + body.fwd[c]*2.0f - body.rgt[c]*1.4f;   // a de apoio A FRENTE
					}
					gr[0] = gr[1] = 1.0f;
					break;
				case 5:                                          // solta SO a mao de tras
					for( int c = 0; c < 3; c++ )
					{
						pR[c] = body.pos[c] + body.fwd[c]*20.0f - body.up[c]*9.0f;
						pO[c] = pR[c] + body.fwd[c]*2.0f - body.rgt[c]*1.4f;
					}
					gr[g_ws.rear] = 0.0f; gr[1 - g_ws.rear] = 1.0f;
					break;
				case 6:                                          // leva a barriga e solta: coldre
					for( int c = 0; c < 3; c++ ) pR[c] = holster[c];
					break;
				default:                                         // saca de novo, com a DIREITA
					for( int c = 0; c < 3; c++ ) pO[c] = holster[c];
					gr[1] = 1.0f;
					break;
				}

				for( int h = 0; h < 2; h++ )
				{
					const float *p = ( h == g_ws.rear ) ? pR : pO;
					memcpy( Hcell[h].pos, p, sizeof( Hcell[h].pos ));
					memcpy( Hcell[h].fwd, body.fwd, sizeof( Hcell[h].fwd ));
					memcpy( Hcell[h].rgt, body.rgt, sizeof( Hcell[h].rgt ));
					memcpy( Hcell[h].up,  body.up,  sizeof( Hcell[h].up  ));
					Hcell[h].ok = true;
				}
				cell_hands = true;
				state = -1;          // quem decide o estado e a maquina, nao a folha
				yaw = 55.0f; pitch = 10.0f; dist = 60.0f;
			}
			else
			{
				// (qualquer modo futuro cai aqui como a folha `states` de antes)
				state = VR_WS_HAND;
				yaw = 60.0f; pitch = 5.0f; dist = 50.0f;
			}

			// duas maos na celula (1,0) da folha de estados: as duas maos juntas
			vr_hands_in_t in2 = in;
			in2.grip[0] = ( state == VR_WS_HAND && mode == VR_SHEET_STATES && row == 0 && col == 1 ) ? 1.0f : grip;
			in2.grip[1] = grip;

			// estado forcado da celula (a maquina de estados nao roda aqui)
			int prev_state = g_ws.state, prev_rear = g_ws.rear;
			bool prev_two = g_ws.two_hand, prev_held = g_ws.held;
			if( mode == VR_SHEET_WALK )
			{
				// o roteiro roda a maquina de verdade, com o grip do passo
				in2.grip[0] = gr[0];
				in2.grip[1] = gr[1];
				for( int k = 0; k < rep; k++ ) weapon_step( &body, &in2, Hcell, 0.05f );
				if( g_draws < 2 )
					vlog( "maos: passo %d: grip esq %.2f dir %.2f -> %s | mao de tras %s%s\n",
						row * cols + col, ( double )gr[0], ( double )gr[1],
						g_ws.state == VR_WS_HAND ? "na mao" : ( g_ws.state == VR_WS_HOLSTER ? "no coldre" : "no chao" ),
						g_ws.rear ? "dir" : "esq", g_ws.two_hand ? " +2 maos" : "" );
			}
			else if( mode == VR_SHEET_STATES )
			{
				g_ws.state = state;
				g_ws.rear = 1;
				g_ws.two_hand = ( row == 0 && col == 1 );
				if( state == VR_WS_GROUND )
				{
					// no chao: cai na frente do jogador, ja deitada
					for( int c = 0; c < 3; c++ )
						g_ws.ground[c] = body.pos[c] + fwd[c]*20.0f - up[c]*24.0f;
					g_ws.ground[2] = body.floor_z + 1.0f;
					g_ws.ground_fwd[0] = fwd[0]; g_ws.ground_fwd[1] = fwd[1]; g_ws.ground_fwd[2] = 0.0f;
					g_ws.landed = true;
				}
			}

			hscene_t sc;
			memset( &sc, 0, sizeof( sc ));
			// a folha `curl` quer o valor CRU do grip (sem o bonus de punho fechado);
			// as outras querem a mao de verdade (com o punho fechado no cabo)
			frame_build( &sc, &body, &in2, cell_hands ? Hcell : NULL, state,
				( mode == VR_SHEET_CURL ) ? grip : -1.0f );
			// no roteiro o estado SEGUE para o proximo passo (a maquina roda de
			// verdade); nos outros modos a celula so empresta o estado e devolve
			if( mode != VR_SHEET_WALK )
			{
				g_ws.state = prev_state; g_ws.rear = prev_rear;
				g_ws.two_hand = prev_two; g_ws.held = prev_held;
			}

			if( !sc.skin ) continue;

			const int px = col * cw, py = cam->h - ( row + 1 ) * ch;   // GL: y de baixo

			p_glScissor( px, py, cw, ch );
			p_glClearColor( 0.30f, 0.32f, 0.35f, 1.0f );
			p_glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
			p_glDisable( GL_SCISSOR_TEST );

			vr_hands_cam_t cc;
			// enquadramento: a arma sozinha -> o MEIO dela; dedos -> a maos em teste
			float focus[3];
			memcpy( focus, sc.focus, sizeof( focus ));
			if( mode == VR_SHEET_WEAPON )
			{
				const float half = 0.5f * g_weapon.len[0] * g_weapon_scale;
				for( int c = 0; c < 3; c++ ) focus[c] += sc.aim[c] * half;
			}
			else if( mode == VR_SHEET_CURL )
			{
				// cada linha olha a palma da SUA mao (senao a arma da mao direita
				// aparece no enquadramento da esquerda)
				const int hc = ( row == 0 ) ? 1 : 0;
				memcpy( focus, sc.palm_pos[hc], sizeof( focus ));
			}
			cell_camera( &cc, focus, dist, body_yaw + yaw, pitch, px, py, cw, ch );
			apply_3d_state( &cc, px, py, cw, ch );

			const bool only_weapon = ( mode == VR_SHEET_WEAPON );
			draw_scene( &cc, &sc, !only_weapon, true, only_weapon );
			restore_3d_state();

			p_glEnable( GL_SCISSOR_TEST );
			scene_free( &sc );
		}
	}

	p_glDisable( GL_SCISSOR_TEST );
	p_glPopAttrib();

	if( mode == VR_SHEET_WALK )
	{
		// devolve o estado como estava antes do roteiro (o jogo real nao passou por ele)
		g_ws.state = walk_state0;
		g_ws.rear = walk_rear0;
		g_ws.grip_prev[0] = walk_prev0[0]; g_ws.grip_prev[1] = walk_prev0[1];
		g_ws.two_hand = walk_two0;
		g_ws.held = walk_held0;
	}

	if( g_draws < 2 )
	{
		static const char *nm[6] = { "off", "fit", "weapon", "curl", "states", "walk" };
		vlog( "maos: folha '%s' %dx%d | escala %.2f un/un | comprimento real %.1f cm | estado %s\n",
			nm[g_sheet], cols, rows, ( double )g_weapon_scale, ( double )cfg.weapon_length_cm,
			g_ws.state == VR_WS_HAND ? "na mao" : ( g_ws.state == VR_WS_HOLSTER ? "coldre" : "chao" ));
		// relato numerico do encaixe (a folha 'fit'/'weapon' e o aceite visual)
		hscene_t one;
		memset( &one, 0, sizeof( one ));
		frame_build( &one, &body, &in, NULL, VR_WS_HAND, cfg.synth_grip );
		if( one.skin )
		{
			float d[3] = { one.aim[0], one.aim[1], one.aim[2] };
			float hf[3];
			m4_xform_dir( one.hand[1], ( const float[3] ){ 1, 0, 0 }, hf );
			v3_norm( hf );
			float dot = v3_dot( d, hf );
			if( dot > 1.0f ) dot = 1.0f;
			if( dot < -1.0f ) dot = -1.0f;
			vlog( "maos: encaixe: cano x frente-da-mao = %.1f graus | empunhadura a %.2f un da palma | arma %.2f un (%.1f cm)\n",
				( double )( acosf( dot ) * 180.0f / ( float )M_PI ),
				( double )one.fit_dist, ( double )( g_weapon.len[0] * g_weapon_scale ),
				( double )( g_weapon.len[0] * g_weapon_scale * 2.54f ));
			// aceite da cadeia da mao: a malha tem de cair ONDE a matematica a pos
			vlog( "maos: mao: punho desliza %.2f/%.2f un | centro da palma %.2f/%.2f un (esq/dir) — 0.00 = malha e referenciais concordam\n",
				( double )one.wrist_slip[0], ( double )one.wrist_slip[1],
				( double )one.palm_slip[0], ( double )one.palm_slip[1] );
			// o alvo esta ao alcance? (cotovelo 0 = esticado; culpa do alvo, nao da cadeia)
			vlog( "maos: braco: alvo a %.1f/%.1f un do ombro | alcance %.1f/%.1f un | cotovelo dobrado %.0f/%.0f graus (0 = esticado)\n",
				( double )one.arm_target[0], ( double )one.arm_target[1],
				( double )one.arm_reach[0], ( double )one.arm_reach[1],
				( double )one.elbow_deg[0], ( double )one.elbow_deg[1] );
			// a torcao do punho: diferenca entre o roll que o CONTROLE reporta e o roll
			// que o braco daria; qual dos dois entra e o `wrist_roll`
			vlog( "maos: punho: controle x braco %+.0f/%+.0f graus (esq/dir) | wrist_roll %.2f -> entram %+.0f/%+.0f\n",
				( double )one.wrist_twist[0], ( double )one.wrist_twist[1],
				( double )cfg.wrist_roll,
				( double )( one.wrist_twist[0] * cfg.wrist_roll ),
				( double )( one.wrist_twist[1] * cfg.wrist_roll ));
			// a ALCA da arma tem de olhar para cima: no diagnostico o "controle" e o
			// corpo, entao a arma na mao tem de sair em pe (+1.00 na vertical do corpo,
			// 0.00 na direita). Era este o numero que faltava quando a arma apareceu de
			// cabeca para baixo: o encaixe (cano x frente) estava certo e a alca nao.
			{
				float gu[3];
				m4_xform_dir( one.hand[1], ( const float[3] ){ 0, 0, 1 }, gu );
				v3_norm( gu );
				vlog( "maos: arma: alca (cima) x cima-do-corpo = %+.2f | x direita-do-corpo = %+.2f | x frente-do-corpo = %+.2f (tem de dar +1.00 / 0.00 / 0.00)\n",
					( double )v3_dot( gu, body.up ), ( double )v3_dot( gu, body.rgt ),
					( double )v3_dot( gu, body.fwd ));
			}
			scene_free( &one );
		}

		// coldre: para que lado o cano aponta? (o sinal do pitch do holster_euler
		// depende da mao do referencial do corpo — este numero diz qual e, sem
		// depender de olhar a folha)
		hscene_t ho;
		memset( &ho, 0, sizeof( ho ));
		frame_build( &ho, &body, &in, NULL, VR_WS_HOLSTER, cfg.synth_grip );
		if( ho.skin && ho.weapon_ok )
		{
			vlog( "maos: coldre: cano x cima-do-corpo = %+.2f (cfg pitch %.0f) | cano x frente = %+.2f | cano x direita = %+.2f\n",
				( double )v3_dot( ho.aim, body.up ), ( double )cfg.holster_euler[0],
				( double )v3_dot( ho.aim, body.fwd ),
				( double )-v3_dot( ho.aim, body.rgt ));
			scene_free( &ho );
		}
		g_draws++;
	}
}

void vr_hands_status( char *buf, size_t n )
{
	if( !g_ready ) { snprintf( buf, n, "maos: off" ); return; }
	static const char *sn[3] = { "coldre", "na mao", "chao" };
	int st = g_ws.state; if( st < 0 || st > 2 ) st = 1;
	snprintf( buf, n, "maos: %s %s%s | esc %.2f | enc %.1f | br %dt wi %dt",
		sn[st], g_ws.rear ? "dir" : "esq", g_ws.two_hand ? " +2 maos" : "",
		( double )g_weapon_scale, ( double )g_scene.fit_dist, g_arms.ntri, g_weapon.ntri );
}
