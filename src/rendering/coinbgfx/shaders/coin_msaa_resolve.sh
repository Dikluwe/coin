// Shared by the shaderc SPIR-V/HLSL path and the raw desktop GLSL wrapper.
void main()
{
    ivec2 counts = coinMsaaCounts();
    int colorCount = counts.x;
    int depthCount = counts.y;
    if (u_depthInfo.z < 0.5)
    {
        gl_FragColor = vec4(colorCount == depthCount ? float(colorCount) : -1.0, 0.0, 0.0, 1.0);
    }
    else
    {
        ivec2 pixel = ivec2(gl_FragCoord.xy);
        vec4 color = vec4(0.0);
        float depth = 1.0;
        for (int sampleIndex = 0; sampleIndex < 4; ++sampleIndex)
        {
            if (sampleIndex < int(u_depthInfo.w))
            {
                if (u_depthInfo.z < 1.5)
                    color += texelFetch(s_msaaColor, pixel, sampleIndex);
                else
                    depth = min(depth, texelFetch(s_msaaDepth, pixel, sampleIndex).r);
            }
        }
        gl_FragColor = u_depthInfo.z < 1.5 ? color / u_depthInfo.w : vec4(depth, 0.0, 0.0, 1.0);
    }
}
