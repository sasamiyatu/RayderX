#pragma once

float3 reinhard(float3 x)
{
    return (x / (1 + x));
}

float3 filmic(float3 x) {
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    float W = 11.2;
    return ((x*(A*x+C*B)+D*E) / (x*(A*x+B)+D*F)) - E / F;
}

float3 agx(float3 color)
{
    float3x3 agxMatrix = float3x3(
        0.842479062253094, 0.0784335999999992, 0.0792237451477643,
        0.0423282422610123, 0.878468636469772, 0.0791661274605434,
        0.0423756549057051, 0.0784336, 0.879142973793104
    );
    float3x3 inverseAgxMatrix = float3x3(
        1.196879029273987, -0.098020888864994, -0.099029742181301,
        -0.052896849811077, 1.151903152465820, -0.098961174488068,
        -0.052971635013819, -0.098043456673622, 1.151073694229126
    );

    color = mul(agxMatrix, color);

    float2 range = float2(-12.47393, 4.026069);
    color = saturate((log2(color) - range.x) / (range.y - range.x));

    // Sigmoid
    float3 x = color - 0.6060606;
    bool3 c = x < 0;
    float3 x3 = x*x*x;
    float3 denom = pow(select(c, -59.507875, 69.8627891 * rsqrt(rsqrt(x))) * x3 + 1.0, select(c, -0.3333333, -0.3076923));
    color = 2.0 * x * denom + 0.5;

    color = mul(inverseAgxMatrix, color);
    return pow(color, 2.2);
}