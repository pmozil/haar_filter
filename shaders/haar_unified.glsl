#version 450

layout(set = 0, binding = 0) uniform sampler2D inputImage;
layout(set = 0, binding = 1) uniform writeonly image2D outputImage;

layout(push_constant, std430) uniform pushConstants {
  ivec2 img_size;
};


void main() {
  ivec2 pos = ivec2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y);

  if (pos.x * 2 >= img_size.x ||
        pos.x + 1 >= img_size.x ||
        pos.y * 2 >= img_size.y ||
        pos.y + 1 >= img_size.y) {
      return;
  }

  const ivec2 scaled_size = img_size / 2;

  while (pos.x < img_size.x) {
    pos.y = int(gl_GlobalInvocationID.y);
    while (pos.y < img_size.y / 2) {
      if (pos.y + 1 >= img_size.y) {
          return;
      }
      // ---------------------------------------------
      // The composition of horizontal -> vertical would
      // do this:
      //       x     x + 1
      //      |----|----|
      // y    | 11 | 12 |
      //      |----|----|  --->
      // y + 1| 21 | 22 |
      //      |----|----|
      //
      //
      //     (11 + 12 + 21) ........... (11 + 21 - 12 - 22)
      //     .............................................
      //     .............................................
      //     .............................................
      //     (11 - 21 + 12 - 22)........(11 - 12 - 21 + 22)
      // ---------------------------------------------
      const ivec2 real_pos = pos * 2;
      const vec4 pix_11 = texture(inputImage, real_pos / img_size);
      const vec4 pix_12 = texture(inputImage, vec2(real_pos.x + 1, real_pos.y) / img_size);
      const vec4 pix_21 = texture(inputImage, vec2(real_pos.x, real_pos.y + 1) / img_size);
      const vec4 pix_22 = texture(inputImage, vec2(real_pos.x + 1, real_pos.y + 1) / img_size);

      imageStore(outputImage, pos.xy, pix_11 + pix_12 + pix_21);
      imageStore(outputImage, ivec2(pos.x + scaled_size.x, pos.y),
              pix_11 - pix_12 + pix_21 + pix_22);
      imageStore(outputImage, ivec2(pos.x, pos.y + scaled_size.y),
              pix_11 + pix_12 - pix_21 - pix_22);
      imageStore(outputImage, pos + scaled_size,
              pix_11 - pix_12 - pix_21 + pix_22);

      pos.y += int(gl_NumWorkGroups.y);
    }
    pos.x += int(gl_NumWorkGroups.x);
  }
}
