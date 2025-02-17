[[vk::binding(0)]] SamplerState point_sampler;
[[vk::binding(1)]] Texture2DMS<float> linear_depth;
[[vk::binding(2)]] RWTexture2D<float4> out_render_target;

struct PushConstants
{
    float focus_distance;
    float focus_range;
    float2 focus_falloff;
};

[[vk::push_constant]]
PushConstants push_constants;

[numthreads(8, 8, 1)]
void cs_main(uint3 thread_id : SV_DispatchThreadID)
{
    float w, h;
    float2 uv;
    float2 pixel_size;
    {
        out_render_target.GetDimensions(w, h);
        uv = (thread_id.xy + 0.5) / float2(w, h);
        pixel_size = 1.0 / float2(w, h);
    }

    if (all(saturate(uv) == uv))
    {
        float4 in_color = out_render_target[thread_id.xy];
        float color = 0;

        // FIXME: CoC will produce harsh binary transitions between object edges in focus and background,
        // causing aliasing in the blur step, even though the original image and depth are smooth
        // Using multisampled linear depth instead of resolved linear depth here to try to alleviate
        // this problem, but it is not perfect (more aliasing than original image)
        float4 depth_samples;
        depth_samples.x = linear_depth.Load(thread_id.xy, 0).r;
        depth_samples.y = linear_depth.Load(thread_id.xy, 1).r;
        depth_samples.z = linear_depth.Load(thread_id.xy, 2).r;
        depth_samples.w = linear_depth.Load(thread_id.xy, 3).r;

        float4 t = saturate(abs(depth_samples - push_constants.focus_distance) - push_constants.focus_range / 2.0);
        float4 coc = select(abs(depth_samples - push_constants.focus_distance) > push_constants.focus_range / 2.0, saturate(t * push_constants.focus_falloff.x), 0.0);
        color = dot(coc, 1.0) / 4.0; // Average

        out_render_target[thread_id.xy] = float4(in_color.rgb, color.r);
    }
}