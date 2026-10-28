#version 450

layout(location = 0) in vec3 plantColor;
layout(location = 1) in vec3 plantNormal;
layout(location = 2) in float viewDistance;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 normal = normalize(plantNormal);
    vec3 lightDirection = normalize(vec3(-0.42, 0.82, -0.35));
    float diffuse = max(dot(normal, lightDirection), 0.0);
    float backLight = max(dot(normal, normalize(vec3(0.35, 0.55, 0.7))), 0.0);
    float light = 0.50 + diffuse * 0.56 + backLight * 0.16;
    vec3 color = plantColor * light + vec3(0.075, 0.055, 0.024) * backLight;
    float fog = smoothstep(72.0, 190.0, viewDistance);
    color = mix(color, vec3(0.48, 0.57, 0.43), fog * 0.72);
    outColor = vec4(color, 1.0);
}