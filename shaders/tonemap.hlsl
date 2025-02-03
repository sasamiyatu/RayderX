#include "color.hlsli"

[[vk::binding(0)]] RWTexture2D<float4> in_render_target;
[[vk::binding(1)]] RWTexture2D<float4> out_render_target;

struct PushConstants
{
    float exposure;
};

[[vk::push_constant]]
PushConstants push_constants;

[numthreads(8, 8, 1)]
void tonemap( uint3 thread_id : SV_DispatchThreadID )
{
    int w, h;
    in_render_target.GetDimensions(w, h);

    if (any(thread_id.xy >= uint2(w, h))) 
        return;

    float4 color = in_render_target[thread_id.xy];

    color.rgb = push_constants.exposure * color.rgb;
    color.rgb = linear_to_srgb(color.rgb);

    out_render_target[thread_id.xy] = color;
}