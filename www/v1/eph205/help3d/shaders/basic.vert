#version 330 core

layout(location = 0) in vec3 osg_Vertex;
layout(location = 1) in vec3 osg_Normal;
in vec2 osg_MultiTexCoord0;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;
out float SphereMap;

uniform mat4 osg_ModelViewMatrix;
uniform mat4 osg_ProjectionMatrix;
uniform mat3 osg_NormalMatrix;
uniform bool useSphereMap;

void main() {
    vec4 mvPos = osg_ModelViewMatrix * vec4(osg_Vertex, 1.0);
    FragPos = mvPos.xyz;
    Normal = normalize(osg_NormalMatrix * osg_Normal);
    SphereMap = useSphereMap ? 1.0 : 0.0;
    if (useSphereMap) {
        // 顶点没有 UV 的反射贴图模型：模拟固定管线的 GL_SPHERE_MAP
        vec3 r = reflect(normalize(mvPos.xyz), Normal);
        float m = 2.0 * sqrt(r.x * r.x + r.y * r.y
                             + (r.z + 1.0) * (r.z + 1.0));
        TexCoord = r.xy / m + 0.5;
    } else {
        TexCoord = osg_MultiTexCoord0;
    }
    gl_Position = osg_ProjectionMatrix * mvPos;
}