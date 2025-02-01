struct VSInput
{
    uint vertex_id: SV_VertexID;
};

struct VSOutput
{
    float4 position: SV_Position;
    float3 normal : NORMAL0;
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
[[vk::combinedImageSampler]][[vk::binding(1)]] Texture2D basecolor_texture;
[[vk::combinedImageSampler]][[vk::binding(1)]] SamplerState texture_sampler;

struct PushConstants
{
    float4x4 viewproj;
};

[[vk::push_constant]]
PushConstants push_constants;

VSOutput vs_main(VSInput input)
{
    VSOutput output = (VSOutput)0;

    Vertex v = vertex_buffer[input.vertex_id];
    float4 position = mul(push_constants.viewproj, float4(v.position, 1.0f));

    output.position = position;
    output.normal = v.normal;
    output.uv = v.uv;

    return output;
}

struct FSOutput
{
    float4 color : SV_Target0;
};

FSOutput fs_main(VSOutput input)
{
    FSOutput output = (FSOutput)0;

    float3 normal = normalize(input.normal);

    float4 basecolor = basecolor_texture.Sample(texture_sampler, input.uv);

    output.color = float4(basecolor);

    return output;
}