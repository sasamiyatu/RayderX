#include "thread_group_tiling.hlsl"

[[vk::binding(0)]] SamplerState linear_sampler;
[[vk::binding(1)]] Texture2D in_render_target;
[[vk::binding(2)]] RWTexture2D<float4> out_render_target;

struct PushConstants
{
    float2 step;
    uint2 dispatch_size;
};

[[vk::push_constant]]
PushConstants push_constants;

[numthreads(8, 8, 1)]
void cs_main(uint3 thread_id : SV_DispatchThreadID, uint3 group_thread_id : SV_GroupThreadID, uint3 group_id : SV_GroupID)
{
    float w, h;
    float2 uv;
    float2 pixel_size;
    //uint2 tid = ThreadGroupTilingX(push_constants.dispatch_size, uint2(8, 8), 64, group_thread_id.xy, group_id.xy);
    uint2 tid = thread_id.xy;
    {
        out_render_target.GetDimensions(w, h);
        uv = (tid.xy + 0.5) / float2(w, h);
        pixel_size = 1.0 / float2(w, h);
    }

    if (all(saturate(uv) == uv))
    {
        const float offsets[] = { -1.7688, -1.1984, -0.8694, -0.6151, -0.3957, -0.1940, 0.1940, 0.3957, 0.6151, 0.8694, 1.1984, 1.7688 };
        const float n = 12.0;

        float4 color = in_render_target.SampleLevel(linear_sampler, uv, 0);
        float coc = color.a;

        float sum = 1.0;

        for (int i = 0; i < int(n); i++) {
            float4 tap = in_render_target.SampleLevel(linear_sampler, uv + push_constants.step * offsets[i] * coc, 0);
            float tap_coc = tap.a;

            float contribution = tap_coc > coc?  1.0f : tap_coc;
            color += contribution * tap;
            sum += contribution;
        }

        out_render_target[tid.xy] = float4(color.rgb / sum, coc);
    }
}