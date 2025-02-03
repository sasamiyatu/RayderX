#include "color.hlsli"
#include "tonemap_operators.hlsli"

#define TONEMAP_LINEAR 0
#define TONEMAP_REINHARD 1
#define TONEMAP_FILMIC 2

#define TONEMAP_OPERATOR TONEMAP_LINEAR

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

#if TONEMAP_OPERATOR == TONEMAP_LINEAR
    color.rgb = push_constants.exposure * color.rgb;
#elif TONEMAP_OPERATOR == TONEMAP_REINHARD
    color.rgb = reinhard(push_constants.exposure * color.rgb);
#elif TONEMAP_OPERATOR == TONEMAP_FILMIC
    color.rgb = 2.0f * filmic(push_constants.exposure * color.rgb);
    float3 white_scale = 1.0f / filmic(11.2);
    color.rgb *= white_scale;
#else
    #error "No tonemap operator set!"
#endif

    color.rgb = linear_to_srgb(color.rgb);
    out_render_target[thread_id.xy] = color;
}