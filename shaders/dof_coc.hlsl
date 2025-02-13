[[vk::binding(0)]] SamplerState point_sampler;
[[vk::binding(1)]] Texture2D linear_depth;
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
        float4 color = 0;
        float depth = linear_depth.SampleLevel(point_sampler, uv, 0).r;
        if (abs(depth - push_constants.focus_distance) > push_constants.focus_range / 2.0) 
        {
            if (depth - push_constants.focus_distance > 0.0) 
            {
                float t = saturate(abs(depth - push_constants.focus_distance) - push_constants.focus_range / 2.0);
                color = saturate(t * push_constants.focus_falloff.x); 
            } 
            else 
            {
                float t = saturate(abs(depth - push_constants.focus_distance) - push_constants.focus_range / 2.0);
                color = saturate(t * push_constants.focus_falloff.y);
            }
        } 

        out_render_target[thread_id.xy] = float4(in_color.rgb, color.r);
    }
}