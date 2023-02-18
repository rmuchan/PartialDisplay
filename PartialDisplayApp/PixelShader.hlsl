Texture2D _Screen : register(t0);
SamplerState _Sampler : register(s0);

float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD) : SV_TARGET
{
    return _Screen.Sample(_Sampler, uv);
}