#include "qcommon/base.h"

void AddDecal( Vec3 origin, Vec3 normal, float radius, float angle, const Material * material, RGBA8 color ) {
}

[origin.x, origin.y, origin.z, radius] RGBA_Float
[normal.x, normal.y, normal.z, angle] RGBA_Float
[color.r, color.g, color.b, color.a] RGBA_U8_sRGB
[min_uv.u, min_uv.v, max_uv.u, max_uv.v] RGBA_Float
[half_pixel_size] R_Float
