[[vk::binding(0)]] SamplerState LinearSampler;
[[vk::binding(1)]] SamplerState PointSampler;
[[vk::binding(2)]] Texture2D in_render_target;
[[vk::binding(3)]] Texture2D in_linear_depth;
[[vk::binding(4)]] RWTexture2D<float4> out_render_target;

#include "sss_config.hlsli"
#include "separable_sss.h"

struct PushConstants
{
    float2 dir;
    float sss_width;
    float2 resolution;
};

[[vk::push_constant]]
PushConstants push_constants;

[numthreads(8, 8, 1)]
void cs_main( uint3 thread_id : SV_DispatchThreadID )
{
    float2 uv = (thread_id.xy + 0.5) / push_constants.resolution;
    
    if (all(thread_id.xy < uint2(push_constants.resolution)))
    {
        float4 in_color = in_render_target[thread_id.xy];
        bool init_stencil = false;
        float4 out_color = SSSSBlurPS(uv, in_render_target, in_linear_depth, push_constants.sss_width, push_constants.dir, init_stencil);

        out_render_target[thread_id.xy] = out_color;
    }
}