#version 330 core

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;
in float SphereMap;
out vec4 FragColor;

uniform vec4 materialDiffuse;
uniform vec4 materialSpecular;
uniform float materialShininess;

uniform sampler2D diffuseMap;
uniform bool hasTexture;

void main() {
    // 1. 获取基础颜色（有贴图用贴图，没贴图用材质颜色）
    vec4 baseColor = materialDiffuse;
    if (hasTexture) {
        // 强行取贴图，如果没拿到 uv，默认给 (0.5, 0.5) 防止全黑
        baseColor = texture(diffuseMap, TexCoord);
    }

    // 2. 环境光（带一点点冷色调，模拟天空光）
    vec3 ambient = vec3(0.3, 0.35, 0.45) * baseColor.rgb;

    // 3. 漫反射
    vec3 lightDir = normalize(vec3(-0.5, 1.0, 1.0)); // 左上方打光
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * baseColor.rgb;

    // 4. 高光（Blinn-Phong，比纯 Phong 更平滑好看）
    vec3 viewDir = normalize(-FragPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfDir), 0.0), materialShininess);
    vec3 specular = spec * materialSpecular.rgb;

    // 5. 组合并做 Gamma 校正（解决画面发灰、暗沉）
    // 球面反射贴图本身已包含环境光照，直接输出，避免高光把画面冲白
    vec3 result = ambient + diffuse + specular;
    result = pow(result, vec3(1.0 / 2.2)); // 提亮
    if (SphereMap > 0.5) {
        result = baseColor.rgb;
    }

    FragColor = vec4(result, baseColor.a);
}