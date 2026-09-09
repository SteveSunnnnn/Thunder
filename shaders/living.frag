#version 460

layout(location = 0) in vec3 localPosition;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 tangentX = dFdx(localPosition);
    vec3 tangentY = dFdy(localPosition);
    vec3 normal = normalize(cross(tangentX, tangentY));
    if (!gl_FrontFacing) normal = -normal;

    vec2 facade = vec2(localPosition.x * 18.0, localPosition.y * 18.0);
    vec2 cell = fract(facade);
    float frame = max(step(cell.x, 0.12) + step(0.88, cell.x),
                      step(cell.y, 0.14) + step(0.86, cell.y));
    float windowMask = 1.0 - clamp(frame, 0.0, 1.0);
    float mortarX = 1.0 - smoothstep(0.035, 0.065, abs(fract(facade.x * 1.7) - 0.5));
    float mortarY = 1.0 - smoothstep(0.035, 0.065, abs(fract(facade.y * 3.2) - 0.5));
    float mortar = clamp(mortarX * mortarY, 0.0, 1.0);

    vec3 masonry = mix(vec3(0.20, 0.14, 0.095), vec3(0.36, 0.25, 0.15),
                       0.5 + 0.5 * sin(localPosition.y * 47.0 + localPosition.x * 29.0));
    masonry = mix(masonry, vec3(0.48, 0.43, 0.34), mortar * 0.28);
    vec3 glass = mix(vec3(0.055, 0.10, 0.13), vec3(0.33, 0.24, 0.12),
                     step(0.66, fract(localPosition.x * 91.0 + localPosition.y * 37.0)));
    vec3 albedo = mix(masonry, glass, windowMask * 0.72);

    vec3 sunDirection = normalize(vec3(-0.45, 0.30, 0.84));
    float diffuse = max(dot(normal, sunDirection), 0.0);
    float edge = pow(1.0 - abs(normal.z), 2.0);
    vec3 color = albedo * (0.30 + diffuse * 0.78) + edge * vec3(0.035, 0.045, 0.055);
    outColor = vec4(color, 1.0);
}
