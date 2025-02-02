struct VSInput
{
    uint vertex_id: SV_VertexID;
};

struct VSOutput
{
    float4 position: SV_Position;
};

struct Vertex
{
	float3 position;
	float3 normal;
	float4 tangent;
	float2 uv;
};

[[vk::binding(0)]] StructuredBuffer<Vertex> vertex_buffer;

struct PushConstants
{
    float4x4 mvp;
};

[[vk::push_constant]]
PushConstants push_constants;

VSOutput vs_main(VSInput input)
{
    VSOutput output = (VSOutput)0;

    Vertex v = vertex_buffer[input.vertex_id];
    float4 position = mul(push_constants.mvp, float4(v.position, 1.0f));
    position.z = position.z * position.w / 10.0f; // Linearize
    output.position = position;

    return output;
}
