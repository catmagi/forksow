#include "qcommon/base.h"
#include "qcommon/qcommon.h"
#include "client/renderer/renderer.h"

static TextureBuffer decal_buffer;

// gets copied directly to GPU so packing order is important
struct Decal {
	Vec3 origin;
	float radius;
	Vec3 normal;
	float angle;
	Vec4 color;
	Vec4 uvwh;
};

STATIC_ASSERT( sizeof( Decal ) % sizeof( Vec4 ) == 0 );
STATIC_ASSERT( sizeof( Decal ) % alignof( Decal ) == 0 );

static constexpr u32 MAX_DECALS = 100000;

static Decal decals[ MAX_DECALS ];
static u32 num_decals;

void InitDecals() {
	decal_buffer = NewTextureBuffer( TextureBufferFormat_Floatx4, MAX_DECALS * sizeof( Decal ) / sizeof( Vec4 ) );
}

void ShutdownDecals() {
	DeleteTextureBuffer( decal_buffer );
}

void AddDecal( Vec3 origin, Vec3 normal, float radius, float angle, StringHash name, Vec4 color ) {
	if( num_decals >= ARRAY_COUNT( decals ) )
		return;

	Decal * decal = &decals[ num_decals ];

	if( !TryFindDecal( name, &decal->uvwh ) ) {
		Com_Printf( "Material %s should have decal key\n", name.str );
		return;
	}

	decal->origin = origin;
	decal->normal = normal;
	decal->radius = radius;
	decal->angle = angle;
	decal->color = color;

	num_decals++;
}

void UploadDecalBuffer() {
	decals[ 1 ].origin.y = 770 + sinf( 0.001f * Sys_Milliseconds() ) * 64;
	WriteTextureBuffer( decal_buffer, decals, num_decals * sizeof( decals[ 0 ] ) );
}

TextureBuffer DecalsBuffer() {
	return decal_buffer;
}

UniformBlock DecalsUniformBlock() {
	return UploadUniformBlock( s32( num_decals ) );
}
