#version 450

layout(set = 0, binding = 0) uniform sampler2D inputImage;
layout(set = 0, binding = 1) uniform writeonly image2D outputImage;

layout(push_constant, std430) uniform pushConstants {
  ivec2 img_size;
};

void haar_horiz(const ivec2 pos, const ivec2 size) {
  const ivec2 real_pos = ivec2(pos.x * 2, pos.y);
  const vec4 colorN = texture(inputImage, real_pos);
  const vec4 colorN_1 = texture(inputImage, ivec2(real_pos.x + 1, pos.y));

  imageStore(outputImage, pos.xy, colorN + colorN_1);
  imageStore(outputImage, ivec2(pos.x + size.x, pos.y),
      colorN - colorN_1);
}

void main() {
  ivec2 pos = ivec2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y);

  if (pos.x * 2 >= img_size.x ||
        pos.x + 1 >= img_size.x ||
        pos.y >= img_size.y) {
      return;
  }

  const ivec2 scaled_size = img_size / 2;

  while (pos.y < img_size.y) {
    pos.x = int(gl_GlobalInvocationID.x);
    while (pos.x < img_size.x / 2) {
      if (pos.x + 1 >= img_size.x) {
          return;
      }
      haar_horiz(pos, scaled_size);
      pos.x += int(gl_NumWorkGroups.x);
    }
    pos.y += int(gl_NumWorkGroups.y);
  }
}
