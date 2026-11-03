// Lavalampe – weiche Blasen, die im Takt der Musik wachsen.
// Die Maus zieht die nächste Blase zu sich.

float4 mainImage(float2 fragCoord) {
    float2 uv = fragCoord / iResolution.y;
    float  w  = iResolution.x / iResolution.y;
    float  t  = iTime * 0.15;
    float  field = 0.0;
    float2 m = iMouse.xy / iResolution.y;

    [unroll] for (int i = 0; i < 9; i++) {
        float fi = i;
        float2 c = float2(w * (0.12 + 0.76 * frac(fi * 0.618)),
                          0.5 + 0.42 * sin(t * (0.6 + fi * 0.13) + fi * 2.1));
        c.x += 0.08 * sin(t * 1.3 + fi);
        c = lerp(c, m, iMouse.w * 0.25 * exp(-length(c - m) * 4.0));
        float r = 0.07 + 0.03 * sin(fi * 1.7) + 0.05 * band(i);
        float2 d = uv - c;
        field += r * r / max(dot(d, d), 1e-4);
    }

    float3 bgTop = float3(0.10, 0.02, 0.12), bgBot = float3(0.02, 0.01, 0.05);
    float3 col = lerp(bgTop, bgBot, fragCoord.y / iResolution.y);
    float blob = smoothstep(0.95, 1.05, field);
    float3 lava = lerp(float3(1.0, 0.35, 0.1), float3(1.0, 0.1, 0.45), saturate(fragCoord.y / iResolution.y));
    lava *= 0.75 + 0.25 * saturate(field - 1.0) + 0.3 * iAudio.x;
    col = lerp(col, lava, blob);
    col += float3(1.0, 0.3, 0.3) * 0.08 * saturate(field) * (1.0 - blob);   // Glühen am Rand
    return float4(col, 1.0);
}
