#version 450

layout(set = 0, binding = 0) in sampler2D inputImage;
layout(set = 0, binding = 1) out image2D outputImage;

layout(push_constant, std430) uniform pushConstants {
  ivec2 img_size;
};

void haar_vert(const ivec2 pos) {
  const ivec2 real_pos = ivec2(pos.x, pos.y * 2);
  const ivec3 colorN = texture(inputImage, real_pos);
  const ivec3 colorN_1 = texture(inputImage, ivec2(real_pos.x, pos.y + 1));

  imageStore(image, pos.xy, colorN + colorN_1);
  imageStore(image, ivec2(pos.x, pos.y + size.y),
      colorN - color_N_1);
}

void main() {
  ivec2 pos = ivec2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y);

  if (pos.y * 2 >= img_size.y ||
        pos.y + 1 >= img_size.y ||
        pos.x >= img_size.x) {
      return;
  }

  const ivec2 scaled_size = img_size / 2;

  while (pos.x < img_size.x) {
    pos.y = gl_GlobalInvocationID.y;
    while (pos.y < img_size.y / 2) {
      if (pos.y + 1 >= img_size.y) {
          return;
      }
      haar_evrt(pos, scaled_size);
      pos.y += gl_NumWorkGroups.y;
    }
    pos.x += gl_NumWorkGroups.x;
  }
}
