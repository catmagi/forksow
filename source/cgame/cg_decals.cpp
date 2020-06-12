#include "qcommon/base.h"
#include "qcommon/qcommon.h"
#include "qcommon/span2d.h"
#include "cgame/cg_local.h"
#include "client/renderer/renderer.h"

static TextureBuffer decal_buffer;
static TextureBuffer decal_index_buffer;
static TextureBuffer decal_tile_buffer;

static u32 last_viewport_width, last_viewport_height;

// gets copied directly to GPU so packing order is important
struct Decal {
	Vec3 origin;
	float radius;
	Vec3 normal;
	float angle;
	Vec4 color;
	Vec4 uvwh;
};

STATIC_ASSERT( sizeof( Decal ) == 4 * 4 * sizeof( float ) );
STATIC_ASSERT( sizeof( Decal ) % alignof( Decal ) == 0 );

static constexpr u32 MAX_DECALS = 100000;
static constexpr u32 MAX_DECALS_PER_TILE = 100;

static Decal decals[ MAX_DECALS ];
static u32 num_decals;

void InitDecals() {
	decal_buffer = NewTextureBuffer( TextureBufferFormat_Floatx4, MAX_DECALS * sizeof( Decal ) / sizeof( Vec4 ) );

	last_viewport_width = U32_MAX;
	last_viewport_height = U32_MAX;
}

void ShutdownDecals() {
	DeleteTextureBuffer( decal_buffer );
	DeleteTextureBuffer( decal_index_buffer );
	DeleteTextureBuffer( decal_tile_buffer );
}

void AddDecal( Vec3 origin, Vec3 normal, float radius, float angle, StringHash name, Vec4 color ) {
	if( num_decals >= ARRAY_COUNT( decals ) )
		return;

	Decal * decal = &decals[ num_decals ];

	if( !TryFindDecal( name, &decal->uvwh ) ) {
		Com_GGPrint( "Material {} should have decal key", name );
		return;
	}

	decal->origin = origin;
	decal->normal = normal;
	decal->radius = radius;
	decal->angle = angle;
	decal->color = color;

	num_decals++;
}

struct DecalTile {
	u32 indices[ MAX_DECALS_PER_TILE ];
	u32 num_decals;
};

struct GPUDecalTile {
	u32 first_decal;
	u32 num_decals;
};

struct Frustum {
	Vec4 top;
	Vec4 bottom;
	Vec4 left;
	Vec4 right;
	Vec4 near;
	Vec4 far;
};

struct InfiniteSquarePyramid {
	Vec4 top;
	Vec4 bottom;
	Vec4 left;
	Vec4 right;
};

static Mat4 FinitePerspectiveProjection( float vertical_fov_degrees, float aspect_ratio, float near_plane, float far_plane ) {
	float tan_half_vertical_fov = tanf( DEG2RAD( vertical_fov_degrees ) / 2.0f );
	float epsilon = 2.4e-6f;

	return Mat4(
		1.0f / ( tan_half_vertical_fov * aspect_ratio ),
		0.0f,
		0.0f,
		0.0f,

		0.0f,
		1.0f / tan_half_vertical_fov,
		0.0f,
		0.0f,

		0.0f,
		0.0f,
		-( far_plane + near_plane ) / ( far_plane - near_plane ),
		-2.0f * far_plane * near_plane / ( far_plane - near_plane ),

		0.0f,
		0.0f,
		-1.0f,
		0.0f
	);
}

static Mat4 InvertPerspectiveProjection( const Mat4 & P ) {
	float a = P.col0.x;
	float b = P.col1.y;
	float c = P.col2.z;
	float d = P.col3.z;
	float e = P.col2.w;

	return Mat4(
		1.0f / a, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f / b, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f / e,
		0.0f, 0.0f, 1.0f / d, -c / ( d * e )
	);
}

Frustum FrustumFromProjection( Mat4 m ) {
	Frustum frustum;

	frustum.top = m.row3() - m.row1();
	frustum.bottom = m.row3() + m.row1();
	frustum.left = m.row3() + m.row0();
	frustum.right = m.row3() - m.row0();
	frustum.near = m.row3() + m.row2();
	frustum.far = m.row3() - m.row2();

	return frustum;
}

static Vec4 PlaneFromPoints( Vec3 a, Vec3 b, Vec3 c ) {
	Vec3 n = Normalize( Cross( b - a, c - a ) );
	return Vec4( n, Dot( a, n ) );
}

static InfiniteSquarePyramid PyramidFromPoints( Vec3 ntl, Vec3 ntr, Vec3 nbl, Vec3 nbr, Vec3 ftl, Vec3 ftr, Vec3 fbl, Vec3 fbr ) {
	InfiniteSquarePyramid pyramid;
	pyramid.top = PlaneFromPoints( ntr, ftr, ntl );
	pyramid.bottom = PlaneFromPoints( nbl, fbl, nbr );
	pyramid.left = PlaneFromPoints( ntl, ftl, nbl );
	pyramid.right = PlaneFromPoints( nbr, fbr, ntr );
	return pyramid;
}

static Vec3 ClosestPointOnAABB( MinMax3 aabb, Vec3 p ) {
	Vec3 r;
	for( int i = 0; i < 3; i++ ) {
		r[ i ] = Clamp( aabb.mins[ i ], p[ i ], aabb.maxs[ i ] );
	}
	return r;
}

static bool SphereOverlapsAABB( MinMax3 aabb, Vec3 origin, float radius ) {
	Vec3 closest = ClosestPointOnAABB( aabb, origin );
	return LengthSquared( closest - origin ) <= radius * radius;
}

static bool SphereFullyInfrontOfPlane( Vec4 plane, Vec3 origin, float radius ) {
	return Dot( plane.xyz(), origin ) - plane.w > radius;
}

static bool SphereOverlapsFrustum( InfiniteSquarePyramid frustum, Vec3 origin, float radius ) {
	return !( SphereFullyInfrontOfPlane( frustum.top, origin, radius ) ||
		SphereFullyInfrontOfPlane( frustum.bottom, origin, radius ) ||
		SphereFullyInfrontOfPlane( frustum.left, origin, radius ) ||
		SphereFullyInfrontOfPlane( frustum.right, origin, radius ) );
}

static MinMax3 Extend( MinMax3 bounds, Vec3 p ) {
	return MinMax3(
		Vec3( Min2( bounds.mins.x, p.x ), Min2( bounds.mins.y, p.y ), Min2( bounds.mins.z, p.z ) ),
		Vec3( Max2( bounds.maxs.x, p.x ), Max2( bounds.maxs.y, p.y ), Max2( bounds.maxs.z, p.z ) )
	);
}

static Vec3 Unproject( Vec4 v ) {
	return ( v / v.w ).xyz();
}

void UploadDecalBuffers() {
	decals[ 1 ].origin.y = 770 + sinf( 0.001f * Sys_Milliseconds() ) * 64;

	constexpr u32 tile_size = 16;
	u32 rows = frame_static.viewport_height / tile_size;
	u32 cols = frame_static.viewport_width / tile_size;

	if( frame_static.viewport_width != last_viewport_width || frame_static.viewport_height != last_viewport_height ) {
		decal_tile_buffer = NewTextureBuffer( TextureBufferFormat_U32x2, rows * cols );
		decal_index_buffer = NewTextureBuffer( TextureBufferFormat_U32, rows * cols * MAX_DECALS_PER_TILE );

		last_viewport_width = frame_static.viewport_width;
		last_viewport_height = frame_static.viewport_height;
	}

	Span2D< DecalTile > tiles = ALLOC_SPAN2D( sys_allocator, DecalTile, cols, rows );
	defer { free( tiles.ptr ); };

	Mat4 P = FinitePerspectiveProjection( frame_static.vertical_fov, frame_static.aspect_ratio, 4.0f, 10000.0f );
	Mat4 invP = InvertPerspectiveProjection( P );

	Vec2 clip_tile_size = tile_size / frame_static.viewport;

	for( u32 y = 0; y < rows; y++ ) {
		for( u32 x = 0; x < cols; x++ ) {
			DecalTile * tile = &tiles( x, y );
			tile->num_decals = 0;

			Vec2 t = Vec2( x, y ) * clip_tile_size;
			Vec2 t1 = Vec2( x + 1, y + 1 ) * clip_tile_size;

			Vec3 near_topleft = Unproject( invP * Vec4( Lerp( -1.0f, t.x, 1.0f ), Lerp( 1.0f, t.y, -1.0f ), -1.0f, 1.0f ) );
			Vec3 near_topright = Unproject( invP * Vec4( Lerp( -1.0f, t1.x, 1.0f ), Lerp( 1.0f, t.y, -1.0f ), -1.0f, 1.0f ) );
			Vec3 near_botleft = Unproject( invP * Vec4( Lerp( -1.0f, t.x, 1.0f ), Lerp( 1.0f, t1.y, -1.0f ), -1.0f, 1.0f ) );
			Vec3 near_botright = Unproject( invP * Vec4( Lerp( -1.0f, t1.x, 1.0f ), Lerp( 1.0f, t1.y, -1.0f ), -1.0f, 1.0f ) );

			Vec3 far_topleft = Unproject( invP * Vec4( Lerp( -1.0f, t.x, 1.0f ), Lerp( 1.0f, t.y, -1.0f ), 1.0f, 1.0f ) );
			Vec3 far_topright = Unproject( invP * Vec4( Lerp( -1.0f, t1.x, 1.0f ), Lerp( 1.0f, t.y, -1.0f ), 1.0f, 1.0f ) );
			Vec3 far_botleft = Unproject( invP * Vec4( Lerp( -1.0f, t.x, 1.0f ), Lerp( 1.0f, t1.y, -1.0f ), 1.0f, 1.0f ) );
			Vec3 far_botright = Unproject( invP * Vec4( Lerp( -1.0f, t1.x, 1.0f ), Lerp( 1.0f, t1.y, -1.0f ), 1.0f, 1.0f ) );

			InfiniteSquarePyramid tile_frustum = PyramidFromPoints(
				near_topleft, near_topright, near_botleft, near_botright,
				far_topleft, far_topright, far_botleft, far_botright
			);

			MinMax3 aabb = MinMax3::Empty();
			aabb = Extend( aabb, near_topleft );
			aabb = Extend( aabb, near_topright );
			aabb = Extend( aabb, near_botleft );
			aabb = Extend( aabb, near_botright );
			aabb = Extend( aabb, far_topleft );
			aabb = Extend( aabb, far_topright );
			aabb = Extend( aabb, far_botleft );
			aabb = Extend( aabb, far_botright );
			aabb.mins.z = -FLT_MAX;
			aabb.maxs.z = 0.0f;

			for( u32 i = 0; i < num_decals; i++ ) {
				Vec3 o = ( frame_static.V * Vec4( decals[ i ].origin, 1.0f ) ).xyz();
				if( SphereOverlapsAABB( aabb, o, decals[ i ].radius ) && SphereOverlapsFrustum( tile_frustum, o, decals[ i ].radius ) ) {
					tile->indices[ tile->num_decals ] = i;
					tile->num_decals++;

					if( tile->num_decals == ARRAY_COUNT( tile->indices ) ) {
						break;
					}
				}
			}
		}
	}

	Span2D< GPUDecalTile > gpu_tiles = ALLOC_SPAN2D( sys_allocator, GPUDecalTile, cols, rows );
	defer { free( gpu_tiles.ptr ); };

	u32 indices[ MAX_DECALS ];
	u32 num_indices = 0;
	for( u32 y = 0; y < rows; y++ ) {
		for( u32 x = 0; x < cols; x++ ) {
			const DecalTile * tile = &tiles( x, y );

			gpu_tiles( x, y ).first_decal = num_indices;
			gpu_tiles( x, y ).num_decals = tile->num_decals;

			for( u32 i = 0; i < tile->num_decals; i++ ) {
				indices[ num_indices ] = tile->indices[ i ];
				num_indices++;
			}

			Vec2 tl = Vec2( x, y ) * Vec2( tile_size );
			Vec2 br = Vec2( x + 1, y + 1 ) * Vec2( tile_size );
			Vec2 dims = br - tl;

			Vec4 color = vec4_white;
			if( tile->num_decals == 1 ) color = vec4_green;
			if( tile->num_decals == 2 ) color = vec4_red;
			color.w = ( ( x + y ) % 2 == 0 ) ? 0.4f : 0.25f;
			Draw2DBox( tl.x, tl.y, dims.x, dims.y, cgs.white_material, color );
		}
	}

	WriteTextureBuffer( decal_buffer, decals, num_decals * sizeof( decals[ 0 ] ) );
	WriteTextureBuffer( decal_index_buffer, indices, num_indices * sizeof( indices[ 0 ] ) );
	WriteTextureBuffer( decal_tile_buffer, gpu_tiles.ptr, gpu_tiles.num_bytes() );
}

void AddDecalsToPipeline( PipelineState * pipeline ) {
	pipeline->set_uniform( "u_Decal", UploadUniformBlock( s32( num_decals ) ) );
	pipeline->set_texture_buffer( "u_DecalData", decal_buffer );
}
