[[vk::binding(0)]] SamplerState LinearSampler;
[[vk::binding(1)]] SamplerState PointSampler;
[[vk::binding(2)]] Texture2D in_render_target;
[[vk::binding(3)]] Texture2D in_linear_depth;

#include "separable_sss.h"

struct PushConstants
{
    float2 dir;
    float sss_width;
};

[[vk::push_constant]]
PushConstants push_constants;

struct VSInput
{
    uint vertex_id: SV_VertexID;
};

struct VSOutput
{
    float4 position: SV_Position;
    float2 uv : TEXCOORD0;
};

VSOutput vs_main(VSInput input)
{
    VSOutput output = (VSOutput)0;
    
    float2 uv = float2((input.vertex_id << 1) & 2, input.vertex_id & 2);
    output.position = float4(uv * 2.0f + -1.0f, 0.0f, 1.0f);
    uv.y = 1 - uv.y;
    output.uv = uv;

    return output;
}

struct FSInput
{
    float4 position: SV_Position;
    float2 uv : TEXCOORD0;
};

struct FSOutput
{
    float4 color : SV_Target0;
};

FSOutput fs_main(FSInput input)
{
    FSOutput output = (FSOutput)0;

    float4 in_color = in_render_target.Sample(LinearSampler, input.uv);

    bool init_stencil = false;

    float4 out_color = SSSSBlurPS(input.uv, in_render_target, in_linear_depth, push_constants.sss_width, push_constants.dir, init_stencil);

    output.color = float4(out_color);

    return output;
}