#include "qcommon/base.h"
#include "cgame/cg_local.h"

struct Spray {
	Vec3 origin;
	Vec3 normal;
	float radius;
	StringHash material;
	s64 spawn_time;
};

constexpr static s64 SPRAY_DURATION = 60000;

static Spray sprays[ 1024 ];
static u32 num_sprays;

void InitSprays() {
	num_sprays = 0;
}

void AddSpray( Vec3 origin, Vec3 normal, StringHash material ) {
	if( num_sprays == ARRAY_COUNT( sprays ) )
		return;

	Spray spray;
	spray.origin = origin;
	spray.normal = normal;
	spray.material = material;
	spray.radius = random_uniform_float( &cls.rng, 28.0f, 36.0f );
	spray.spawn_time = cls.gametime;

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

		AddDecal( spray->origin, spray->normal, spray->radius, 0.0f, spray->material, vec4_white );
	}
}
