#version 450

layout(set = 0, binding = 0) uniform image2D image;

layout(push_constant, std430) uniform pushConstants {
  ivec2 img_size;
  int iterations;
};

void run_iteration(const ivec2 size) {
  ivec2 pos = ivec2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y);

  if (pos.x * 2 >= size.x ||
        pos.x + 1 >= size.x ||
        pos.y * 2 >= size.y ||
        pos.y + 1 >= size.y) {
      return;
  }

  const ivec2 scaled_size = size / 2;

  while (pos.x < scaled_size.x) {
    pos.y = int(gl_GlobalInvocationID.y);
    while (pos.y < scaled_size.y / 2) {
      if (pos.y + 1 >= scaled_size.y) {
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
      const vec4 pix_11 = imageLoad(image, real_pos);
      const vec4 pix_12 = imageLoad(image, vec2(real_pos.x + 2, real_pos.y));
      const vec4 pix_21 = imageLoad(image, vec2(real_pos.x, real_pos.y + 1));
      const vec4 pix_22 = imageLoad(image, vec2(real_pos.x + 1, real_pos.y + 1));

      imageStore(image, pos.xy, pix_11 + pix_12 + pix_21);
      imageStore(image, ivec2(pos.x + scaled_size.x, pos.y),
              pix_11 - pix_12 + pix_21 + pix_22);
      imageStore(image, ivec2(pos.x, pos.y + scaled_size.y),
              pix_11 + pix_12 - pix_21 - pix_22);
      imageStore(image, pos + scaled_size,
              pix_11 - pix_12 - pix_21 + pix_22);

      pos.y += int(gl_NumWorkGroups.y);
    }
    pos.x += int(gl_NumWorkGroups.x);
  }
}

void main() {
    ivec2 size = ivec2(img_size);
    for (int i = 0; i < iterations; i++) {
        run_iteration(size);
        size /= 2;
    }
}
