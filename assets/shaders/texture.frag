#version 450

layout(set = 0, binding = 0) uniform sampler2D samplerColor;

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outFragColor;

void main() {
  const float gamma = 2.2;

  vec3 color = texture(samplerColor, inUV).rgb;
  vec3 mapped = color / (color + vec3(1.0));
  mapped = pow(mapped, vec3(1.0 / gamma));

  outFragColor = vec4(mapped, 1.0);
}