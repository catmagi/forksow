#include <math.h>

#include "qcommon/base.h"
#include "cgame/cg_local.h"

struct Spray {
	Vec3 origin;
	Vec3 normal;
	float radius;
	float angle;
	StringHash material;
	s64 spawn_time;
};

constexpr static s64 SPRAY_DURATION = 60000;

static Spray sprays[ 1024 ];
static u32 num_sprays;

void InitSprays() {
	num_sprays = 0;
}

// must match the GLSL OrthonormalBasis
static void OrthonormalBasis( Vec3 v, Vec3 * tangent, Vec3 * bitangent ) {
	float s = copysignf( 1.0f, v.z );
	float a = -1.0f / ( s + v.z );
	float b = v.x * v.y * a;

	*tangent = Vec3( 1.0f + s * v.x * v.x * a, s * b, -s * v.x );
	*bitangent = Vec3( b, s + v.y * v.y * a, -v.y );
}

void AddSpray( Vec3 origin, Vec3 normal, Vec3 up, StringHash material ) {
	if( num_sprays == ARRAY_COUNT( sprays ) )
		return;

	Spray spray;
	spray.origin = origin;
	spray.normal = normal;
	spray.material = material;
	spray.radius = random_uniform_float( &cls.rng, 32.0f, 48.0f );
	spray.spawn_time = cls.gametime;

	Vec3 left = Cross( normal, up );
	Vec3 decal_up = Normalize( Cross( left, normal ) );

	Vec3 tangent, bitangent;
	OrthonormalBasis( normal, &tangent, &bitangent );

	spray.angle = -atan2( Dot( decal_up, tangent ), Dot( decal_up, bitangent ) );
	spray.angle += random_float11( &cls.rng ) * DEG2RAD( 10.0f );

	sprays[ num_sprays ] = spray;
	num_sprays++;
}

void DrawSprays() {
	for( u32 i = 0; i < num_sprays; i++ ) {
		Spray * spray = &sprays[ i ];

		if( spray->spawn_time + SPRAY_DURATION < cls.gametime ) {
			num_sprays--;
			Swap2( spray, &sprays[ num_sprays ] );
			i--;
			continue;
		}

		AddDecal( spray->origin, spray->normal, spray->radius, spray->angle, spray->material, vec4_white );
	}
}
