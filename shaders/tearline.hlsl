#include "color.hlsli"
#include "brdf.hlsli"

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

[[vk::binding(0)]] StructuredBuffer<Vertex> vertex_buffer;
[[vk::binding(1)]] SamplerState anisotropic_sampler;
[[vk::binding(2)]] SamplerState LinearSampler;
[[vk::binding(3)]] Texture2D normal_texture;
[[vk::binding(4)]] Texture2D linear_depth_texture;
[[vk::binding(5)]] TextureCube environment_texture;

struct PushConstants
{
    float4x4 viewproj;
    float4x4 model;
    float3 camera_pos;
    float ambient;
    float2 pixel_size;
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
};

float4 sample_cubemap(TextureCube cubemap, float3 dir)
{
    dir.z = -dir.z;
    return cubemap.Sample(LinearSampler, dir);
}

float3 normal_map(Texture2D tex, SamplerState tex_sampler, float2 uv) 
{
    float3 normal = -1.0 + 2.0 * tex.Sample(tex_sampler, uv).rgb;
    return normalize(normal);
}

// We have a better approximation of the off specular peak
// but due to the other approximations we found this one performs better .
// N is the normal direction
// R is the mirror vector
// This approximation works fine for G smith correlated and uncorrelated
float3 getSpecularDominantDir ( float3 N , float3 R , float roughness )
{
    float smoothness = saturate (1 - roughness );
    float lerpFactor = smoothness * ( sqrt ( smoothness ) + roughness );
    // The result is not normalized as we fetch in a cubemap
    return lerp (N , R , lerpFactor );
}

FSOutput fs_main(FSInput input)
{
    FSOutput output = (FSOutput)0;

    input.normal = normalize(input.normal);
    float3 bitangent = cross(input.normal, input.tangent.xyz) * input.tangent.w;
    float3x3 tbn = float3x3(input.tangent.xyz, bitangent, input.normal);
    float3 tangent_space_normal = normal_map(normal_texture, anisotropic_sampler, input.uv * 0.1);
    //float3 normal = mul(tangent_space_normal, tbn);
    float3 normal = input.normal;
    

    float3 view = normalize(push_constants.camera_pos - input.world_position);
    float NoV = abs(dot(normal, view)) + 1e-5f;

    float current_linear_depth = 1.0 / input.position.w;
    float2 screen_uv = input.position.xy * push_constants.pixel_size;
    //screen_uv.y = 1 - screen_uv.y;
    float sample_depth = linear_depth_texture.SampleLevel(LinearSampler, screen_uv, 0);

    float diff = sample_depth  - current_linear_depth;

    float alpha = smoothstep(0.0, 0.001, diff);

    float3 f0 = 0.04;
    const float roughness = 0.0;
    float3 R = reflect(-view, normal);
    float3 env_r = sample_cubemap(environment_texture, R).rgb * specularGGXReflectanceApprox(f0, roughness, NoV);
    float3 radiance = alpha * push_constants.ambient * env_r;
    if (diff < 0) radiance = float3(1, 0, 1);
    output.color = float4(radiance, alpha);

    return output;
}