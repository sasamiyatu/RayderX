#include "color.hlsli"

struct VSInput
{
    uint vertex_id: SV_VertexID;
};

struct VSOutput
{
    float4 position: SV_Position;
    float3 world_position : POSITION0;
};

struct Vertex
{
	float3 position;
	float3 normal;
	float4 tangent;
	float2 uv;
};

[[vk::binding(0)]] StructuredBuffer<Vertex> vertex_buffer;
[[vk::binding(1)]] SamplerState linear_sampler;
[[vk::binding(2)]] TextureCube envmap;

struct PushConstants
{
    float4x4 viewproj;
    float intensity;
};

[[vk::push_constant]]
PushConstants push_constants;

VSOutput vs_main(VSInput input)
{
    VSOutput output = (VSOutput)0;

    Vertex v = vertex_buffer[input.vertex_id];
    float4 position = mul(push_constants.viewproj, float4(v.position * 10.0f, 1.0f));

    output.position = position;
    output.world_position = v.position.xyz;

    return output;
}

struct FSInput
{
    float4 position: SV_Position;
    float3 world_position : POSITION0;
};

struct FSOutput
{
    float4 color : SV_Target0;
};
    
FSOutput fs_main(FSInput input)
{
    FSOutput output = (FSOutput)0;

    input.world_position.z = -input.world_position.z;
    float3 env = push_constants.intensity * envmap.Sample(linear_sampler, input.world_position).rgb;
    output.color = float4(env.rgb, 0);

    return output;
}