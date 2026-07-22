#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;

layout(location = 0) out vec3 outWorldPosition;
layout(location = 2) out vec2 outUv;

layout(set = 0, binding = 0, std140) uniform Scene
{
    layout(offset = 0) mat4 view;
    layout(offset = 64) mat4 projection;
    layout(offset = 416) vec4 ocean;
} scene;

layout(set = 0, binding = 1) uniform sampler2D displacementMap;

layout(push_constant) uniform Tile
{
    vec4 offset;
} tile;

void main()
{
    vec4 displacement = texture(displacementMap, inUv);

    vec3 position = vec3(
        inPosition.x * scene.ocean.x + tile.offset.x,
        displacement.y * scene.ocean.y,
        inPosition.z * scene.ocean.x + tile.offset.y);
    position.xz += displacement.xz;

    outWorldPosition = position;
    outUv = inUv;
    gl_Position = scene.projection * scene.view * vec4(position, 1.0);
}
