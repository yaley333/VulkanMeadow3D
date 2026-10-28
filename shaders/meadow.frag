#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 outColor;
layout(push_constant) uniform Push {
	float time;
	float aspect;
	float cameraX;
	float cameraY;
	float cameraZ;
	float yaw;
	float pitch;
	float moving;
} pc;

float hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x),
			   mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

float terrain(vec2 p) {
	float rolling = sin(p.x * 0.075 + sin(p.y * 0.031) * 0.8) * 2.6;
	rolling += sin(p.y * 0.052 + p.x * 0.027) * 1.7;
	rolling += sin(p.x * 0.19 + sin(p.y * 0.11)) * 0.42;
	rolling += cos(p.y * 0.16 - p.x * 0.06) * 0.3;
	float valley = -3.0 * exp(-pow((p.x + 1.5) / 8.5, 2.0));
	float ridges = 0.5 + 0.5 * sin(p.x * 0.018 + sin(p.y * 0.009) * 1.7 +
							  sin(p.x * 0.041 + p.y * 0.013) * 0.42 + cos(p.y * 0.027 - p.x * 0.011) * 0.3);
	float mountainFade = smoothstep(110.0, 230.0, -p.y) * (1.0 - smoothstep(390.0, 510.0, -p.y));
	float mountains = mountainFade * (4.0 + pow(ridges, 1.35) * 22.0);
	return rolling + valley + mountains;
}

vec3 sky(vec3 ray) {
	float elevation = clamp(ray.y * 0.5 + 0.5, 0.0, 1.0);
	vec3 color = mix(vec3(0.76, 0.80, 0.78), vec3(0.31, 0.52, 0.68),
					 smoothstep(-0.02, 0.58, ray.y));
	color = mix(color, vec3(0.70, 0.75, 0.74), exp(-max(ray.y, 0.0) * 5.0) * 0.28);
	vec3 sunDirection = normalize(vec3(-0.42, 0.58, -0.70));
	float sunGlow = pow(max(dot(ray, sunDirection), 0.0), 72.0);
	color += vec3(0.9, 0.68, 0.38) * sunGlow * 0.34;
	return mix(color, vec3(0.36, 0.46, 0.47), (1.0 - elevation) * 0.06);
}

void main() {
	vec2 screen = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	float cp = cos(pc.pitch);
	vec3 forward = normalize(vec3(sin(pc.yaw) * cp, sin(pc.pitch), -cos(pc.yaw) * cp));
	vec3 right = vec3(cos(pc.yaw), 0.0, sin(pc.yaw));
	vec3 up = normalize(cross(right, forward));
	vec3 ray = normalize(forward + right * screen.x * pc.aspect * 0.58 + up * screen.y * 0.58);
	vec3 cameraPosition = vec3(pc.cameraX, pc.cameraY, pc.cameraZ);
	vec3 color = sky(ray);

	gl_FragDepth = 1.0;
	if (ray.y < 0.42) {
		float distanceAlongRay = 0.0;
		float previousDistance = 0.0;
		float hitDistance = -1.0;
		for (int i = 0; i < 360; ++i) {
			vec3 point = cameraPosition + ray * distanceAlongRay;
			float heightDifference = point.y - terrain(point.xz);
			if (heightDifference < 0.0) {
				float low = previousDistance;
				float high = distanceAlongRay;
				for (int j = 0; j < 5; ++j) {
					float middle = (low + high) * 0.5;
					vec3 testPoint = cameraPosition + ray * middle;
					if (testPoint.y < terrain(testPoint.xz)) high = middle;
					else low = middle;
				}
				hitDistance = (low + high) * 0.5;
				break;
			}
			previousDistance = distanceAlongRay;
			float safeStep = heightDifference / max(-ray.y, 0.07) * 0.3;
			distanceAlongRay += clamp(safeStep, 0.22, 7.0);
			if (distanceAlongRay > 510.0) break;
		}

		if (hitDistance > 0.0) {
			vec3 point = cameraPosition + ray * hitDistance;
			float epsilon = 0.16;
			float center = terrain(point.xz);
			vec3 normal = normalize(vec3(terrain(point.xz - vec2(epsilon, 0.0)) - terrain(point.xz + vec2(epsilon, 0.0)),
										 epsilon * 2.0,
										 terrain(point.xz - vec2(0.0, epsilon)) - terrain(point.xz + vec2(0.0, epsilon))));

			float variation = noise(point.xz * 0.055);
			float groundDetail = noise(point.xz * 1.8);
			vec3 grass = mix(vec3(0.09, 0.18, 0.045), vec3(0.24, 0.32, 0.09), variation);
			grass *= mix(0.86, 1.12, groundDetail);

			vec3 sunDirection = normalize(vec3(-0.42, 0.82, -0.35));
			float diffuse = max(dot(normal, sunDirection), 0.0);
			float light = 0.34 + diffuse * 0.72;
			float highland = smoothstep(4.0, 13.0, center) * smoothstep(24.0, 100.0, hitDistance);
			vec3 ground = grass * light;
			ground = mix(ground, vec3(0.30, 0.34, 0.31) * light, highland * 0.82);
			ground += vec3(0.13, 0.12, 0.075) * pow(max(dot(normal, sunDirection), 0.0), 12.0) * 0.12;
			float haze = 1.0 - exp(-hitDistance * 0.0045);
			color = mix(ground, vec3(0.58, 0.65, 0.64), haze * 0.72);
			vec3 forward = normalize(vec3(sin(pc.yaw) * cos(pc.pitch), sin(pc.pitch),
									  -cos(pc.yaw) * cos(pc.pitch)));
			float viewDistance = hitDistance * dot(ray, forward);
			float nearPlane = 0.1;
			float farPlane = 500.0;
			gl_FragDepth = (farPlane / (farPlane - nearPlane)) -
						   (farPlane * nearPlane / ((farPlane - nearPlane) * viewDistance));
		}
	}

	outColor = vec4(color, 1.0);
}
