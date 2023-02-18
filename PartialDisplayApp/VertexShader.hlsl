float4 _Config : register(c0);  // [X Offset] [Y Offset] [X Scale] [Y Scale]

struct V2P
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

V2P main(float2 position : POSITION)
{
    V2P v2p;
    v2p.position = float4(position * _Config.zw + _Config.xy, 0, 1);
    v2p.uv = float2(1.0 + position.x, 1.0 - position.y) / 2;
    return v2p;
}