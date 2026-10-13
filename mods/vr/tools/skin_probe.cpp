// skin_probe.cpp — roda o MESMO algoritmo de skin do vr_hands na CPU, sem GL,
// e imprime fatos: quantos ossos, quais raizes, e a bbox do resultado.
// Serve para separar "o skin esta errado" de "o desenho/projecao esta errado".
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CGLTF_IMPLEMENTATION
#include "third_party/cgltf.h"

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
static void m4_point( const m4 m, const float p[3], float out[3] )
{
	out[0] = m[0]*p[0] + m[4]*p[1] + m[8]*p[2]  + m[12];
	out[1] = m[1]*p[0] + m[5]*p[1] + m[9]*p[2]  + m[13];
	out[2] = m[2]*p[0] + m[6]*p[1] + m[10]*p[2] + m[14];
}

typedef struct { char name[64]; float local[16]; int parent; float ibm[16]; } joint_t;

static joint_t *J; static int NJ;

static void jworld( int idx, m4 out )
{
	if( idx < 0 ) { m4_ident( out ); return; }
	if( J[idx].parent >= 0 ) { m4 p; jworld( J[idx].parent, p ); m4_mul( out, p, J[idx].local ); }
	else memcpy( out, J[idx].local, sizeof( m4 ));
}

int main( int argc, char **argv )
{
	const char *path = argc > 1 ? argv[1] : "assets/arms.glb";
	cgltf_options opt; memset( &opt, 0, sizeof( opt ));
	cgltf_data *d = NULL;
	if( cgltf_parse_file( &opt, path, &d ) != cgltf_result_success ) { printf("parse falhou\n"); return 1; }
	if( cgltf_load_buffers( &opt, d, path ) != cgltf_result_success ) { printf("buffers falharam\n"); return 1; }

	printf("=== %s ===\n", path );
	printf("nos=%d malhas=%d skins=%d imagens=%d\n",
		(int)d->nodes_count, (int)d->meshes_count, (int)d->skins_count, (int)d->images_count );

	cgltf_skin *sk = &d->skins[0];
	NJ = (int)sk->joints_count;
	J = (joint_t *)calloc( NJ, sizeof( joint_t ));
	int roots = 0, orphan = 0;
	for( int i = 0; i < NJ; i++ )
	{
		cgltf_node *n = sk->joints[i];
		snprintf( J[i].name, sizeof( J[i].name ), "%s", n->name ? n->name : "?" );
		cgltf_node_transform_local( n, J[i].local );
		J[i].parent = -1;
		for( int k = 0; k < NJ; k++ ) if( sk->joints[k] == n->parent ) { J[i].parent = k; break; }
		if( J[i].parent < 0 ) roots++;
		if( J[i].parent < 0 && n->parent ) orphan++;
		if( sk->inverse_bind_matrices ) cgltf_accessor_read_float( sk->inverse_bind_matrices, i, J[i].ibm, 16 );
		else m4_ident( J[i].ibm );
	}
	printf("ossos=%d raizes=%d (com pai FORA da lista de ossos=%d)\n", NJ, roots, orphan );

	// mundo de alguns ossos-chave (a prova: isso tem de bater com o inspetor)
	const char *want[] = { "root", "shoulder.r", "bicep.r", "forearm.r", "wrist.r", "socket.r", "head" };
	for( size_t w = 0; w < sizeof(want)/sizeof(want[0]); w++ )
		for( int i = 0; i < NJ; i++ )
			if( !strcmp( J[i].name, want[w] ))
			{
				m4 m; jworld( i, m );
				printf("  %-12s mundo=(%+.3f %+.3f %+.3f) pai=%d(%s)\n",
					J[i].name, m[12], m[13], m[14], J[i].parent,
					J[i].parent >= 0 ? J[i].name : "-" );
			}

	// malha: skinning de todos os vertices
	cgltf_primitive *pr = &d->meshes[0].primitives[0];
	cgltf_accessor *ap=NULL,*aj=NULL,*aw=NULL;
	for( cgltf_size a = 0; a < pr->attributes_count; a++ )
	{
		if( pr->attributes[a].type == cgltf_attribute_type_position && pr->attributes[a].index == 0 ) ap = pr->attributes[a].data;
		if( pr->attributes[a].type == cgltf_attribute_type_joints && pr->attributes[a].index == 0 ) aj = pr->attributes[a].data;
		if( pr->attributes[a].type == cgltf_attribute_type_weights && pr->attributes[a].index == 0 ) aw = pr->attributes[a].data;
	}
	printf("verts=%d temJOINTS=%d temWEIGHTS=%d\n", (int)ap->count, aj!=NULL, aw!=NULL );

	m4 *skin = (m4 *)malloc( NJ * sizeof( m4 ));
	for( int i = 0; i < NJ; i++ ) { m4 w; jworld( i, w ); m4_mul( skin[i], w, J[i].ibm ); }

	float rlo[3]={1e9f,1e9f,1e9f}, rhi[3]={-1e9f,-1e9f,-1e9f};
	float slo[3]={1e9f,1e9f,1e9f}, shi[3]={-1e9f,-1e9f,-1e9f};
	int wsum_bad = 0; int jmax = -1;
	for( cgltf_size v = 0; v < ap->count; v++ )
	{
		float p[3]; cgltf_accessor_read_float( ap, v, p, 3 );
		cgltf_uint ji[4]={0,0,0,0}; float wt[4]={0,0,0,0};
		cgltf_accessor_read_uint( aj, v, ji, 4 );
		cgltf_accessor_read_float( aw, v, wt, 4 );
		float ws = wt[0]+wt[1]+wt[2]+wt[3];
		if( fabsf( ws - 1.0f ) > 0.01f ) wsum_bad++;
		float q[3]={0,0,0};
		for( int k = 0; k < 4; k++ )
		{
			if( wt[k] <= 0 ) continue;
			if( (int)ji[k] > jmax ) jmax = (int)ji[k];
			float t[3]; m4_point( skin[ji[k]], p, t );
			for( int c = 0; c < 3; c++ ) q[c] += wt[k]*t[c];
		}
		for( int c = 0; c < 3; c++ ) { if(p[c]<rlo[c])rlo[c]=p[c]; if(p[c]>rhi[c])rhi[c]=p[c]; }
		for( int c = 0; c < 3; c++ ) { if(q[c]<slo[c])slo[c]=q[c]; if(q[c]>shi[c])shi[c]=q[c]; }
	}
	printf("bbox REPOUSO : (%.3f %.3f %.3f) .. (%.3f %.3f %.3f)  tam=(%.3f %.3f %.3f)\n",
		rlo[0],rlo[1],rlo[2],rhi[0],rhi[1],rhi[2], rhi[0]-rlo[0],rhi[1]-rlo[1],rhi[2]-rlo[2]);
	printf("bbox SKINNED : (%.3f %.3f %.3f) .. (%.3f %.3f %.3f)  tam=(%.3f %.3f %.3f)\n",
		slo[0],slo[1],slo[2],shi[0],shi[1],shi[2], shi[0]-slo[0],shi[1]-slo[1],shi[2]-slo[2]);
	printf("soma de pesos != 1: %d verts | maior indice de osso usado: %d (NJ=%d)\n", wsum_bad, jmax, NJ );

	cgltf_free( d );
	return 0;
}
