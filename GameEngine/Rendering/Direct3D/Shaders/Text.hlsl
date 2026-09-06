cbuffer TextConstants : register(CONSTANT_SLOT)
{
    float4x4 worldViewProjection;
    float4 tint;
    // xy는 UV 배율, zw는 UV 오프셋이다. 글리프 quad가 아틀라스 페이지의 자기 사각형만 샘플한다.
    float4 uvTransform;
};

Texture2D<float> glyphAtlas : register(TEXTURE_SLOT);
SamplerState glyphSampler : register(SAMPLER_SLOT);

struct VS_INPUT
{
    float2 position : POSITION;
    float2 textureCoordinate : TEXCOORD;
};

struct VS_OUTPUT
{
    float4 position : SV_POSITION;
    float2 textureCoordinate : TEXCOORD;
};

VS_OUTPUT VS(VS_INPUT input)
{
    VS_OUTPUT output;
    output.position = mul(float4(input.position, 0.0f, 1.0f), worldViewProjection);
    output.textureCoordinate = input.textureCoordinate * uvTransform.xy + uvTransform.zw;
    return output;
}

float4 PS(VS_OUTPUT input) : SV_Target
{
    // 아틀라스는 글자색이 아니라 단일 채널 피복률이다. 색은 tint에서 받고,
    // 피복률은 알파에만 곱해 가장자리의 부분 투명도를 보존한다.
    const float coverage = glyphAtlas.Sample(glyphSampler, input.textureCoordinate);
    return float4(tint.rgb, tint.a * coverage);
}
