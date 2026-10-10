// language: GLSL 440, file: ambient.frag, runtime: Qt shader package, target: native Qt Quick scene graph
#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 frag_color;
layout(std140, binding = 0) uniform Parameters {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    vec2 resolution;
    vec2 pointer;
    float pointer_presence;
    vec3 base_color;  // the theme's darkest wave color
    vec3 tint_color;  // added per unit of light; negative on light themes
};

float hash_value(vec2 point) {
    point = fract(point * vec2(123.34, 456.21));
    point += dot(point, point + 45.32);
    return fract(point.x * point.y);
}

float value_noise(vec2 point) {
    vec2 cell = floor(point);
    vec2 local = fract(point);
    local = local * local * (3.0 - 2.0 * local);
    return mix(mix(hash_value(cell), hash_value(cell + vec2(1.0, 0.0)), local.x),
               mix(hash_value(cell + vec2(0.0, 1.0)), hash_value(cell + vec2(1.0)), local.x), local.y);
}

float field_noise(vec2 point) {
    float field = 0.0;
    float weight = 0.5;
    mat2 rotation = mat2(0.80, 0.60, -0.60, 0.80);
    for (int octave = 0; octave < 4; ++octave) {
        field += value_noise(point) * weight;
        point = rotation * point * 2.03 + vec2(2.7, 6.1);
        weight *= 0.5;
    }
    return field;
}

void main() {
    vec2 uv = qt_TexCoord0;
    vec2 aspect = vec2(resolution.x / max(resolution.y, 1.0), 1.0);
    vec2 space = (uv - 0.5) * aspect;
    float drift = time * 0.075;
    vec2 warp = vec2(field_noise(space * 1.7 + vec2(drift, -drift * 0.7)),
                     field_noise(space * 1.7 + vec2(7.2 - drift * 0.6, 3.1 + drift)));
    float field = field_noise(space * 2.4 + warp * 1.9 + vec2(-drift, drift * 0.5));

    vec2 focus = vec2(0.70 + sin(time * 0.13) * 0.16, 0.12 + cos(time * 0.11) * 0.18);
    vec2 distance_to_focus = (uv - focus) * vec2(0.85, 1.30);
    float light = exp(-dot(distance_to_focus, distance_to_focus) * 3.4);
    float fog = smoothstep(0.32, 0.78, field);
    float contour = pow(1.0 - abs(sin(field * 17.0 + space.x * 0.8 - time * 0.12)), 10.0);
    float edge = smoothstep(0.0, 0.9, length(space));
    float glow = exp(-dot((uv - pointer) * aspect, (uv - pointer) * aspect) * 9.0);

    float luminance = 0.024 + fog * light * 0.11 + contour * light * 0.019;
    luminance += edge * fog * 0.022 + glow * pointer_presence * 0.025;
    // Stationary grain avoids flicker while the larger light field moves.
    luminance += (hash_value(floor(uv * resolution * 0.5)) - 0.5) / 255.0;
    frag_color = vec4(base_color + tint_color * luminance, 1.0) * qt_Opacity;
}
