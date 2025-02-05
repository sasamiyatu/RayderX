#include "color.hlsli"
#include "brdf.hlsli"
#include "matrix.hlsli"

struct VSInput
{
    uint vertex_id: SV_VertexID;
};

struct VSOutput
{
    float4 position: SV_Position;
    float3 world_position : POSITION0;
    float3 normal : NORMAL0;
    float4 tangent : TANGENT0;
    float2 uv : TEXCOORD0;
};

struct Vertex
{
	float3 position;
	float3 normal;
	float4 tangent;
	float2 uv;
};

struct Light {
    float3 position;
    float3 direction;
    float falloff_start;
    float falloff_width;
    float3 color;
    float attenuation;
    float far_plane;
    float bias;
    float4x4 view_projection;
};

[[vk::binding(0)]] StructuredBuffer<Vertex> vertex_buffer;
[[vk::binding(1)]] SamplerState anisotropic_sampler;
[[vk::binding(2)]] SamplerState LinearSampler;
[[vk::binding(3)]] SamplerState PointSampler;
[[vk::binding(4)]] Texture2D beckmann_texture;
[[vk::binding(5)]] Texture2D basecolor_texture;
[[vk::binding(6)]] Texture2D normal_texture;
[[vk::binding(7)]] Texture2D metallic_roughness_texture;
[[vk::binding(8)]] Texture2D occlusion_texture;
[[vk::binding(9)]] Texture2D sclera_blend_texture;
[[vk::binding(10)]] StructuredBuffer<Light> lights;
[[vk::binding(11)]] SamplerComparisonState shadow_sampler;
[[vk::binding(12)]] Texture2D shadowmaps[5];
[[vk::binding(13)]] TextureCube irradiance_texture;
[[vk::binding(14)]] TextureCube environment_texture;

#include "sss_config.hlsli"
#include "separable_sss.h"

struct PushConstants
{
    float4x4 viewproj;
    float4x4 model;
    float4x4 model_inverse;
    uint num_lights;
    float3 camera_pos;
    float ambient;
    float3 gaze_direction;
};

[[vk::push_constant]]
PushConstants push_constants;

VSOutput vs_main(VSInput input)
{
    VSOutput output = (VSOutput)0;

    Vertex v = vertex_buffer[input.vertex_id];
    float4 world_pos = mul(push_constants.model, float4(v.position, 1.0f));
    float3 world_normal = mul(push_constants.model, float4(v.normal, 0.0)).xyz;
    float4 world_tangent = float4(mul(push_constants.model, float4(v.tangent.xyz, 0.0)).xyz, v.tangent.w);
    float4 position = mul(push_constants.viewproj, world_pos);

    output.position = position;
    output.world_position = world_pos.xyz;
    output.normal = world_normal;
    output.tangent = world_tangent;
    output.uv = v.uv;

    return output;
}

struct FSInput
{
    float4 position: SV_Position;
    float3 world_position : POSITION0;
    float3 normal : NORMAL0;
    float4 tangent : TANGENT0;
    float2 uv : TEXCOORD0;
};

struct FSOutput
{
    float4 color : SV_Target0;
    float4 depth : SV_Target1;
};

float get_shadow(float3 world_pos, int i) 
{
    float4 shadow_pos = mul(lights[i].view_projection, float4(world_pos, 1.0));
    shadow_pos.xy /= shadow_pos.w;
    shadow_pos.z += lights[i].bias;
    shadow_pos.z /= lights[i].far_plane;
    return shadowmaps[i].SampleCmpLevelZero(shadow_sampler, shadow_pos.xy, shadow_pos.z).r;
}

float get_shadow_pcf(float3 world_pos, int i, int samples, float width) {
    float4 shadow_pos = mul(lights[i].view_projection, float4(world_pos, 1.0));
    shadow_pos.xy /= shadow_pos.w;
    shadow_pos.z += lights[i].bias;
    shadow_pos.z /= lights[i].far_plane;
    
    float w, h;
    shadowmaps[i].GetDimensions(w, h);

    float shadow = 0.0;
    float offset = (samples - 1.0) / 2.0;
    [unroll]
    for (float x = -offset; x <= offset; x += 1.0) 
    {
        [unroll]
        for (float y = -offset; y <= offset; y += 1.0) 
        {
            float2 pos = shadow_pos.xy + width * float2(x, y) / w;
            shadow += shadowmaps[i].SampleCmpLevelZero(shadow_sampler, pos, shadow_pos.z).r;
        }
    }

    shadow /= samples * samples;
    return shadow;
}

float3 fresnel_schlick(float3 f0, float f90, float NdotS) 
{
    return f0 + (f90 - f0) * pow(1.0f - NdotS, 5.0f);
}

// Kelemen and Szirmay-Kalos BRDF
// [Advanced Techniques for Realistic Real-Time Skin Rendering, Chapter 14. GPU Gems 3, 2007]
float specular_ksk(Texture2D beckmann_lut, SamplerState tex_sampler, float3 normal, float3 light, float3 view, float roughness, float fresnel_strength) {
    float3 half_vec = view + light;
    float3 H = normalize(half_vec);

    float NoL = max(dot(normal, light), 0.0);
    float NoH = max(dot(normal, H), 0.0);
    float HoV = abs(dot(H, view));

    float ph = pow(2.0 * beckmann_lut.SampleLevel(tex_sampler, float2(NoH, roughness), 0).r, 10.0);
    float f = lerp(0.25, fresnel_schlick(0.028, 1.0, HoV).x, fresnel_strength);
    float ksk = max(ph * f / dot(half_vec, half_vec), 0.0);

    return NoL * ksk;
}

float3 normal_map(Texture2D tex, SamplerState tex_sampler, float2 uv) 
{
    float3 normal = -1.0 + 2.0 * tex.Sample(tex_sampler, uv).rgb;
    return normalize(normal);
}

float4 sample_cubemap(TextureCube cubemap, float3 dir)
{
    dir.z = -dir.z;
    return cubemap.Sample(LinearSampler, dir);
}

float2 get_refract_uv(float3 normal, float3 front_normal, float3 view, float2 uv, float mask, float4x4 world_to_object)
{
    const float anterior_chamber_depth = 0.00323;
    const float radius = 0.1;
    const float height = anterior_chamber_depth * saturate( 1.0 - 18.4 * radius * radius ); 
#if 1
    const float ior = 1.41;

    // Refraction
    const float eta = 1 / ior;
    float NoV = dot(normal, view);
#if 0
    float w = eta * dot(normal, view);
    float k = sqrt( 1.0 + ( w - eta ) * ( w + eta ) );
    float3 refracted = ( w - k ) * normal - eta * view;
#else
    float3 refracted = refract(-view, normal, eta);
#endif

    float2 eye_uv = uv;

    float cos_alpha = dot(front_normal, -refracted);
    float dist = height / cos_alpha;
    float3 offset_w = dist * refracted;
    float2 offset_l = mul((float2x3)world_to_object, offset_w);

#if 0
    if (mask == 1)
        //printf("cos alpha: %f refr: (%f %f %f), nov: %f", cos_alpha, refracted.x, refracted.y, refracted.z, NoV);
        //printf("offset w: (%f, %f, %f)", offset_w.x, offset_w.y, offset_w.z);
        printf("offset l: (%f, %f)", offset_l.x, offset_l.y);
#endif

    eye_uv += float2(mask,-mask) * offset_l;
#else
    float2 view_l = mul(view, (float2x3) world_to_object);
    float2 offset = height * viewl_;
    offset.y = -offset.y;
    texcoord -= parallaxScale * offset;
#endif

    return eye_uv;
}

FSOutput fs_main(FSInput input)
{
    FSOutput output = (FSOutput)0;

    input.normal = normalize(input.normal);
    input.tangent.xyz = normalize(input.tangent.xyz);
    float3 bitangent = cross(input.normal, input.tangent.xyz) * input.tangent.w;
    float3x3 tbn = float3x3(input.tangent.xyz, bitangent, input.normal);

    float3 tangent_space_normal = normal_map(normal_texture, anisotropic_sampler, input.uv * 0.1);
    float3 normal = mul(tangent_space_normal, tbn);

    float3 view = normalize(push_constants.camera_pos - input.world_position);

    float2 uv2 = input.uv * 2.0 - 1.0;
    float l = length(uv2);
    float sclera_mask = smoothstep(0.18, 0.22, l);

    //normal = input.normal;
    normal = normalize(lerp(input.normal, normal, sclera_mask));

    float2 uv = get_refract_uv(normal, push_constants.gaze_direction, view, input.uv, 1 - sclera_mask, push_constants.model_inverse);

    float4 basecolor = basecolor_texture.Sample(anisotropic_sampler, uv);
    float3 metallic_roughness = metallic_roughness_texture.Sample(anisotropic_sampler, input.uv).rgb;
    float3 sclera_blend = sclera_blend_texture.Sample(anisotropic_sampler, input.uv).rgb;

    float darkening = smoothstep(0.0, 0.05, abs(l - 0.22));
    float darkening_strength = 0.3;
    basecolor.rgb *= (1 + darkening_strength * (darkening - 1));;

    float occlusion_strength = 1;
    float occlusion = (1 + occlusion_strength * (sclera_blend.r - 1));

    float NoV = abs(dot(normal, view)) + 1e-5f;

    const float specular_intensity = 1.88;
    const float specular_roughness = 0.3;
    const float specular_fresnel = 0.82;

    float intensity = metallic_roughness.b;
    float roughness = metallic_roughness.g;

    float3 radiance_iris = 0;
    float3 radiance_sclera = 0;

    for (uint i = 0; i < push_constants.num_lights; ++i)
    {
        Light l = lights[i];

        float3 light = l.position - input.world_position;
        float dist2 = dot(light, light);
        float dist = sqrt(dist2);
        light /= dist;

        float spot = dot(l.direction, light);

        if (spot > l.falloff_start)
        {        
            float curve = min(pow(dist / l.far_plane, 6.0), 1.0);
            float attenuation = lerp(1.0 / (1.0 + l.attenuation * dist2), 0.0, curve);
            spot = saturate((spot - l.falloff_start) / l.falloff_width);

            float3 f1 = l.color * attenuation * spot;
            float3 f2 = basecolor.rgb * f1;

            float diffuse = saturate(dot(input.normal, light));
            float specular = intensity * specular_ksk(beckmann_texture, LinearSampler, normal, light, view, roughness, specular_fresnel);

            float shadow = get_shadow_pcf(input.world_position, i, 3, 1.0);

            radiance_sclera += shadow * (f2 * diffuse + f1 * specular);
            radiance_iris += shadow * (f2 * diffuse);
        }
    }

    //float sclera_mask_binary = step(0.20, l);

    float3 radiance = lerp(radiance_iris, radiance_sclera, sclera_mask);
    radiance += push_constants.ambient * basecolor.rgb * saturate(sample_cubemap(irradiance_texture, input.normal).rgb);

    NoV = abs(dot(normal, view)) + 1e-5f;

    float3 f0 = 0.04;
    float3 R = reflect(-view, normal);
    float3 env_r = sample_cubemap(environment_texture, R).rgb * specularGGXReflectanceApprox(f0, roughness, NoV);

    radiance += push_constants.ambient * env_r;

    radiance *= occlusion;

    //radiance = lerp(0, radiance, sclera_mask_binary);
    //radiance = float3(uv, 0);
    //radiance = push_constants.gaze_direction * 0.5 + 0.5;

    float sss = smoothstep(0.22, 0.26, l);
    output.color = float4(radiance.rgb, sss);
    output.depth = 1.0 / input.position.w;

    return output;
}