#version 450

layout(location = 0) out vec3 plantColor;
layout(location = 1) out vec3 plantNormal;
layout(location = 2) out float viewDistance;

layout(push_constant) uniform Push {
    float time;
    float aspect;
    float cameraX;
    float cameraY;
    float cameraZ;
    float yaw;
    float pitch;
    float moving;
    float plantMode;
} pc;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float terrain(vec2 p) {
    float h = sin(p.x * 0.075 + sin(p.y * 0.031) * 0.8) * 2.6;
    h += sin(p.y * 0.052 + p.x * 0.027) * 1.7;
    h += sin(p.x * 0.19 + sin(p.y * 0.11)) * 0.42;
    h += cos(p.y * 0.16 - p.x * 0.06) * 0.3;
    h -= 3.0 * exp(-pow((p.x + 1.5) / 8.5, 2.0));
    float ridges = 0.5 + 0.5 * sin(p.x * 0.018 + sin(p.y * 0.009) * 1.7 +
                                   sin(p.x * 0.041 + p.y * 0.013) * 0.42 + cos(p.y * 0.027 - p.x * 0.011) * 0.3);
    float nearFade = smoothstep(110.0, 230.0, -p.y);
    float farFade = 1.0 - smoothstep(390.0, 510.0, -p.y);
    h += nearFade * farFade * (4.0 + pow(ridges, 1.35) * 22.0);
    return h;
}

vec4 projectWorld(vec3 world, out float depth) {
    vec3 forward = normalize(vec3(sin(pc.yaw) * cos(pc.pitch), sin(pc.pitch),
                                  -cos(pc.yaw) * cos(pc.pitch)));
    vec3 right = vec3(cos(pc.yaw), 0.0, sin(pc.yaw));
    vec3 up = normalize(cross(right, forward));
    vec3 relative = world - vec3(pc.cameraX, pc.cameraY, pc.cameraZ);
    float z = dot(relative, forward);
    float nearPlane = 0.1;
    float farPlane = 500.0;
    depth = (farPlane / (farPlane - nearPlane)) * z -
            (farPlane * nearPlane / (farPlane - nearPlane));
    return vec4(dot(relative, right) / (pc.aspect * 0.58),
                -dot(relative, up) / 0.58, depth, z);
}

void main() {
    float distanceToCamera;
    vec3 world;
    vec3 color;
    vec3 normal;

    if (pc.plantMode < 0.75) {
        int nearLayer = pc.plantMode < 0.25 ? 1 : 0;
        int gridSize = nearLayer == 1 ? 240 : 384;
        int bladesPerClump = nearLayer == 1 ? 4 : 2;
        float spacing = nearLayer == 1 ? 0.19 : 0.78;
        int bladeId = gl_VertexIndex / 3;
        int corner = gl_VertexIndex % 3;
        int cellId = bladeId / bladesPerClump;
        int bladeInClump = bladeId % bladesPerClump;
        ivec2 cell = ivec2(cellId % gridSize, cellId / gridSize);
        ivec2 cameraCell = ivec2(floor(vec2(pc.cameraX, pc.cameraZ) / spacing));
        ivec2 worldCell = cameraCell + cell - ivec2(gridSize / 2);
        vec2 rootXZ = (vec2(worldCell) + 0.5) * spacing;
        rootXZ += (vec2(hash(vec2(worldCell) + 2.0), hash(vec2(worldCell) + 9.0)) - 0.5) * spacing * 0.7;
        float groundHeight = terrain(rootXZ);

        float angle = hash(vec2(worldCell) + float(bladeInClump) * 13.7) * 6.2831853;
        float height = nearLayer == 1 ? mix(0.62, 1.35, hash(vec2(worldCell) + float(bladeInClump) * 7.1))
                          : mix(0.42, 0.86, hash(vec2(worldCell) + float(bladeInClump) * 7.1));
        height *= 1.0 - smoothstep(2.0, 12.0, groundHeight) * 0.78;
        float width = nearLayer == 1 ? mix(0.035, 0.065, hash(vec2(worldCell) + float(bladeInClump) * 3.3))
                         : mix(0.075, 0.13, hash(vec2(worldCell) + float(bladeInClump) * 3.3));
        vec2 direction = vec2(cos(angle), sin(angle));
        vec2 side = vec2(-direction.y, direction.x);
        rootXZ += direction * (0.025 + float(bladeInClump) * 0.014);
        vec2 wind = vec2(sin(pc.time * 1.3 + rootXZ.x * 0.7 + rootXZ.y * 0.3),
                 cos(pc.time * 1.1 + rootXZ.y * 0.6)) * (nearLayer == 1 ? 0.18 : 0.10);
        vec3 root = vec3(rootXZ.x, groundHeight, rootXZ.y);
        vec3 tip = root + vec3(direction.x * height * 0.27 + wind.x,
                               height,
                               direction.y * height * 0.27 + wind.y);
        if (corner == 0) world = tip;
        else world = root + vec3(side.x * width * (corner == 1 ? -0.5 : 0.5),
                                 0.0,
                                 side.y * width * (corner == 1 ? -0.5 : 0.5));

        float tint = hash(vec2(worldCell) + float(bladeInClump) * 17.0);
        color = mix(vec3(0.09, 0.23, 0.055), vec3(0.31, 0.43, 0.12), tint);
        color = mix(color, vec3(0.24, 0.29, 0.22), smoothstep(3.0, 14.0, groundHeight) * 0.72);
        color += vec3(0.12, 0.15, 0.035) * float(corner == 0);
        normal = normalize(vec3(side.x, width / height, side.y));
    } else {
        const int gridSize = 88;
        const int verticesPerFlower = 330;
        const float spacing = 1.1;
        int flowerId = gl_VertexIndex / verticesPerFlower;
        int localVertex = gl_VertexIndex % verticesPerFlower;
        ivec2 cell = ivec2(flowerId % gridSize, flowerId / gridSize);
        ivec2 cameraCell = ivec2(floor(vec2(pc.cameraX, pc.cameraZ) / spacing));
        ivec2 worldCell = cameraCell + cell - ivec2(gridSize / 2);
        vec2 flowerXZ = (vec2(worldCell) + 0.5) * spacing;
        flowerXZ += (vec2(hash(vec2(worldCell) + 31.0), hash(vec2(worldCell) + 47.0)) - 0.5) * spacing * 0.72;
        float ground = terrain(flowerXZ);
        float height = mix(0.72, 1.18, hash(vec2(worldCell) + 12.0));
        float rotation = hash(vec2(worldCell) + 63.0) * 6.2831853;
        float species = hash(vec2(worldCell) + 81.0);
        vec3 palette = species < 0.34 ? vec3(0.96, 0.78, 0.23) :
                       species < 0.56 ? vec3(0.94, 0.88, 0.78) :
                       species < 0.78 ? vec3(0.91, 0.48, 0.67) : vec3(0.72, 0.56, 0.84);
        float density = hash(vec2(worldCell) + 99.0);
        bool hiddenFlower = density < 0.18;
        vec2 stemSide = vec2(cos(rotation), sin(rotation)) * 0.03;
        vec3 head = vec3(flowerXZ.x, ground + height, flowerXZ.y);

        vec3 flowerNormal = normalize(vec3(0.0, 0.34, 0.94));
        vec3 baseRight = vec3(cos(rotation), 0.0, sin(rotation));
        vec3 flowerRight = normalize(baseRight - flowerNormal * dot(baseRight, flowerNormal));
        vec3 flowerUp = normalize(cross(flowerNormal, flowerRight));
        if (localVertex < 6) {
            int stemPlane = localVertex / 3;
            int corner = localVertex % 3;
            vec3 root = vec3(flowerXZ.x, ground, flowerXZ.y);
            vec3 stemTop = head + vec3(0.0, -0.035, 0.0);
            vec2 crossSide = stemPlane == 0 ? stemSide : vec2(-stemSide.y, stemSide.x);
            vec3 stemLeft = root + vec3(-crossSide.x, height * 0.91, -crossSide.y);
            vec3 stemRight = root + vec3(crossSide.x, height * 0.91, crossSide.y);
            world = corner == 0 ? stemTop : (corner == 1 ? stemLeft : stemRight);
            color = vec3(0.12, 0.31, 0.065);
            normal = normalize(vec3(stemSide.y, 0.15, -stemSide.x));
        } else if (localVertex < 18) {
            int leafVertex = localVertex - 6;
            int leaf = leafVertex / 6;
            int corner = leafVertex % 6;
            float direction = leaf == 0 ? -1.0 : 1.0;
            vec3 leafBase = vec3(flowerXZ.x, ground + height * 0.38, flowerXZ.y);
            vec3 leafTip = leafBase + vec3(direction * cos(rotation) * 0.19, 0.12,
                                           direction * sin(rotation) * 0.19);
            vec3 leafNear = leafBase + vec3(direction * cos(rotation) * 0.075, 0.085,
                                            direction * sin(rotation) * 0.075);
            vec3 leafFar = leafBase + vec3(direction * cos(rotation) * 0.11, 0.035,
                                           direction * sin(rotation) * 0.11);
            world = corner == 0 || corner == 3 ? leafBase :
                    corner == 1 || corner == 4 ? leafNear :
                    corner == 2 ? leafTip : leafFar;
            color = vec3(0.12, 0.34, 0.065);
            normal = normalize(vec3(0.0, 0.78, 0.62));
        } else if (localVertex < 306) {
            int petalVertex = localVertex - 18;
            int petal = petalVertex / 48;
            int segmentVertex = petalVertex % 48;
            int segment = segmentVertex / 6;
            int corner = segmentVertex % 6;
            float angle = rotation + float(petal) * 1.0471976;
            vec3 radial = flowerRight * cos(angle) + flowerUp * sin(angle);
            vec3 tangent = -flowerRight * sin(angle) + flowerUp * cos(angle);
            float t0 = float(segment) / 8.0;
            float t1 = float(segment + 1) / 8.0;
            float width0 = 0.072 * pow(max(0.0, sin(t0 * 3.1415926)), 0.62);
            float width1 = 0.072 * pow(max(0.0, sin(t1 * 3.1415926)), 0.62);
            float curve0 = sin(t0 * 3.1415926) * 0.018;
            float curve1 = sin(t1 * 3.1415926) * 0.018;
            vec3 left0 = head + radial * (0.035 + t0 * 0.205) - tangent * width0 + flowerUp * curve0;
            vec3 right0 = head + radial * (0.035 + t0 * 0.205) + tangent * width0 + flowerUp * curve0;
            vec3 left1 = head + radial * (0.035 + t1 * 0.205) - tangent * width1 + flowerUp * curve1;
            vec3 right1 = head + radial * (0.035 + t1 * 0.205) + tangent * width1 + flowerUp * curve1;
            world = corner == 0 ? left0 : corner == 1 ? left1 : corner == 2 ? right1 :
                    corner == 3 ? left0 : corner == 4 ? right1 : right0;
            color = palette;
            normal = normalize(cross(flowerRight, flowerUp));
        } else {
            int centerVertex = localVertex - 306;
            int segment = centerVertex / 3;
            int corner = centerVertex % 3;
            float angle = float(segment) * 0.7853982;
            vec3 radial = flowerRight * cos(angle) + flowerUp * sin(angle);
            world = corner == 0 ? head + flowerUp * 0.12 + flowerNormal * 0.045 :
                head + flowerUp * 0.095 + flowerNormal * 0.045 + radial * 0.072;
            color = vec3(1.0, 0.66, 0.035);
            normal = vec3(0.0, 1.0, 0.0);
        }
        if (hiddenFlower) world.y -= 3.0;
    }

    float depth;
    gl_Position = projectWorld(world, depth);
    viewDistance = length(world - vec3(pc.cameraX, pc.cameraY, pc.cameraZ));
    plantColor = color;
    plantNormal = normal;
}