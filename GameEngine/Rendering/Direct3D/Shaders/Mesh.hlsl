#define MAX_LIGHTS 3

struct Light
{
    // 방향광: xyz가 빛이 나아가는 방향, w = 0. 점광: xyz가 위치, w = 1.
    float4 positionOrDirection;
    // rgb는 세기를 곱한 색, w는 점광의 range.
    float4 colorAndRange;
};

cbuffer MeshConstants : register(CONSTANT_SLOT)
{
    float4x4 worldViewProjection;
    float4x4 world;
    float4x4 normalToWorld;
    // 알베도 텍스처에 곱해질 재질 색이다. 기본값은 흰색이라 곱해도 텍스처를 바꾸지 않는다.
    float4 tint;
    float4 ambientAndLightCount;
    Light lights[MAX_LIGHTS];
};

Texture2D albedoTexture : register(TEXTURE_SLOT);
SamplerState albedoSampler : register(SAMPLER_SLOT);

struct VS_INPUT
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 textureCoordinate : TEXCOORD;
};

struct VS_OUTPUT
{
    float4 position : SV_POSITION;
    float3 worldPosition : POSITION;
    float3 normal : NORMAL;
    float2 textureCoordinate : TEXCOORD;
};

VS_OUTPUT VS(VS_INPUT input)
{
    VS_OUTPUT output;
    output.position = mul(float4(input.position, 1.0f), worldViewProjection);
    output.worldPosition = mul(float4(input.position, 1.0f), world).xyz;
    output.normal = normalize(mul(float4(input.normal, 0.0f), normalToWorld).xyz);
    output.textureCoordinate = input.textureCoordinate;
    return output;
}

float4 PS(VS_OUTPUT input) : SV_Target
{
    const float3 normal = normalize(input.normal);
    float3 lighting = ambientAndLightCount.rgb;
    const int lightCount = (int) ambientAndLightCount.w;
    [unroll]
    for (int i = 0; i < MAX_LIGHTS; ++i)
    {
        if (i >= lightCount)
        {
            break;
        }
        const Light light = lights[i];
        float3 toLight;
        float attenuation;
        if (light.positionOrDirection.w > 0.5f)
        {
            const float3 offset = light.positionOrDirection.xyz - input.worldPosition;
            const float distance = length(offset);
            toLight = offset / max(distance, 0.0001f);
            // range에서 0이 되는 부드러운 감쇠: (1 - (d/r)^4)^2 / (1 + d^2). 물리 역제곱을
            // range 안에서 잘라 낸 흔한 꼴이다.
            const float ratio = distance / light.colorAndRange.w;
            const float window = saturate(1.0f - ratio * ratio * ratio * ratio);
            attenuation = window * window / (1.0f + distance * distance);
        }
        else
        {
            toLight = -normalize(light.positionOrDirection.xyz);
            attenuation = 1.0f;
        }
        lighting += light.colorAndRange.rgb * (saturate(dot(normal, toLight)) * attenuation);
    }
    return albedoTexture.Sample(albedoSampler, input.textureCoordinate) * tint *
        float4(lighting, 1.0f);
}
