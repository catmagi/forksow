#include "include/uniforms.glsl"
#include "include/common.glsl"
#include "include/skinning.glsl"
#include "include/dither.glsl"
#include "include/fog.glsl"

v2f vec3 v_Position;
v2f vec3 v_Normal;
v2f vec2 v_TexCoord;

#if VERTEX_COLORS
v2f vec4 v_Color;
#endif

#if APPLY_SOFT_PARTICLE
v2f float v_Depth;
#endif

#if VERTEX_SHADER

in vec4 a_Position;
in vec3 a_Normal;
in vec4 a_Color;
in vec2 a_TexCoord;

vec2 ApplyTCMod( vec2 uv ) {
	mat3x2 m = transpose( mat2x3( u_TextureMatrix[ 0 ], u_TextureMatrix[ 1 ] ) );
	return ( m * vec3( uv, 1.0 ) ).xy;
}

void main() {
	vec4 Position = a_Position;
	vec3 Normal = a_Normal;
	vec2 TexCoord = a_TexCoord;

#if SKINNED
	Skin( Position, Normal );
#endif

	v_Position = ( u_M * Position ).xyz;
	v_Normal = mat3( u_M ) * Normal;
	v_TexCoord = ApplyTCMod( a_TexCoord );

#if VERTEX_COLORS
	v_Color = sRGBToLinear( a_Color );
#endif

	gl_Position = u_P * u_V * u_M * Position;

#if APPLY_SOFT_PARTICLE
	vec4 modelPos = u_V * u_M * Position;
	v_Depth = -modelPos.z;
#endif
}

#else

out vec4 f_Albedo;

uniform sampler2D u_BaseTexture;
uniform sampler2D u_DecalAtlas;

#if APPLY_SOFT_PARTICLE
#include "include/softparticle.glsl"
uniform sampler2D u_DepthTexture;
#endif

#if APPLY_DECALS
layout( std140 ) uniform u_Decal {
	vec4 u_DecalXYWH;
};
#endif

float proj_frac( vec3 p, vec3 o, vec3 d ) {
	return dot( p - o, d ) / dot( d, d );
}

void orthonormal_basis( vec3 v, out vec3 tangent, out vec3 bitangent ) {
	float s = step( v.z, 0.0 ) * 2.0 - 1.0;
	float a = -1.0 / (s + v.z);
	float b = v.x * v.y * a;

	tangent = vec3(1.0 + s * v.x * v.x * a, s * b, -s * v.x);
	bitangent = vec3(b, s + v.y * v.y * a, -v.y);
}

void main() {
#if APPLY_DRAWFLAT
	vec4 diffuse = vec4( 0.0, 0.0, 0.0, 1.0 );
#else
	vec4 color = sRGBToLinear( u_MaterialColor );

#if VERTEX_COLORS
	color *= v_Color;
#endif

	vec4 diffuse = texture( u_BaseTexture, v_TexCoord ) * color;
#endif

#if ALPHA_TEST
	if( diffuse.a < u_AlphaCutoff )
		discard;
#endif

#if APPLY_SOFT_PARTICLE
	float softness = FragmentSoftness( v_Depth, u_DepthTexture, gl_FragCoord.xy, u_NearClip );
	diffuse *= mix(vec4(1.0), vec4(softness), u_BlendMix.xxxy);
#endif

#if APPLY_DECALS
	vec3 decal_origin = vec3( -720.0, 770.0, 320.0 );
	vec3 decal_normal = normalize( vec3( 1.0, 0.0, 1.0 ) );
	vec4 decal_color = vec4( 1.0, 1.0, 1.0, 1.0 );
	float decal_radius = 32.0;
	float decal_angle = 0.0;
	float decal_plane_dist = dot( decal_origin, decal_normal );

	if( distance( decal_origin, v_Position ) < decal_radius ) {
		vec3 basis_u;
		vec3 basis_v;
		orthonormal_basis( decal_normal, basis_u, basis_v );
		basis_u *= decal_radius * 2.0;
		basis_v *= decal_radius * 2.0;
		vec3 bottom_left = decal_origin - ( basis_u + basis_v ) * 0.5;

		vec2 uv = vec2( proj_frac( v_Position, bottom_left, basis_u ), proj_frac( v_Position, bottom_left, basis_v ) );
		uv = u_DecalXYWH.xy + u_DecalXYWH.zw * uv;

		vec4 sample = texture( u_DecalAtlas, uv );
		diffuse.rgb += sample.rgb * sample.a * decal_color.rgb * decal_color.a * max( 0.0, dot( v_Normal, decal_normal ) );
	}
#endif

#if APPLY_FOG
	diffuse.rgb = Fog( diffuse.rgb, length( v_Position - u_CameraPos ) );
	diffuse.rgb += Dither();
#endif

	f_Albedo = LinearTosRGB( diffuse );
}

#endif
