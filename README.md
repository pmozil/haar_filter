# 2D Haar filter

For the ffmpeg GSoC submission, I have to write a qualification task - a 2D Haar wavelet transform in GLSL.

I was not sure if it meant just writing shaders or writing the whole Haas filter, so I did both :P

The filter is in `include/` and `src/` and the glsl in `shaders/`

## Algorithm explanation
A 2D Haar transform is just a composition of two 1D haar transforms: one vertical and one horizontal.

Since the shaders should likely be compute shaders, I used the global variables for compute shaders in vulkan.

It's possible to jsut do that in one pass, so I wrote  that)

## Compiling the filter
First, copy the `src/vf_haar_filter_vulkan.c` to your `ffmpeg/libavfilter` directory.
```bash
cp src/vf_haar_filter_vulkan.c libavfilter/
```
Now, apply the patch in the `ffmpeg directory`
```bash
git apply src/add_filter.patch
```
Reconfigure with vulkan
```bash
./configure --disable-doc --disable-shared --enable-static --disable-ffplay --disable-ffprobe --enable-vulkan --enable-libshaderc
```
Compile
```bash
make -j$(nproc)
```
And run the filter, lol
```bash
./ffmpeg \
    -init_hw_device vulkan=vk:0 \
    -filter_hw_device vk \
    -i <input> \
    -filter_complex format=rgba,hwupload,haar_vulkan,hwdownload,format=rgba \
    <output>
```

## Block-wise Haar wavelet
```glsl
#version 450

layout(set = 0, binding = 0) uniform sampler2D inputImage;
layout(set = 0, binding = 1) uniform writeonly image2D outputImage;

layout(push_constant, std430) uniform pushConstants { ivec2 img_size; };

void haar_block(const ivec2 pos, const ivec2 scaled_size) {
  const ivec2 real_pos = pos * 2;
  const vec3 pix_11 = texture(inputImage[0], real_pos).rgb;
  const vec3 pix_12 =
      texture(inputImage[0], ivec2(real_pos.x + 1, real_pos.y)).rgb;
  const vec3 pix_21 =
      texture(inputImage[0], ivec2(real_pos.x, real_pos.y + 1)).rgb;
  const vec3 pix_22 =
      texture(inputImage[0], ivec2(real_pos.x + 1, real_pos.y + 1)).rgb;
  imageStore(outputImage[0], pos, vec4(pix_11 + pix_12 + pix_21, 1.0));
  imageStore(outputImage[0], ivec2(pos.x + scaled_size.x, pos.y),
             vec4(pix_11 - pix_12 + pix_21 + pix_22, 1.0));
  imageStore(outputImage[0], ivec2(pos.x, pos.y + scaled_size.y),
             vec4(pix_11 + pix_12 - pix_21 - pix_22, 1.0));
  imageStore(outputImage[0], pos + scaled_size,
             vec4(pix_11 - pix_12 - pix_21 + pix_22, 1.0));
}

void main() {
  ivec2 pos = ivec2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y);

  if (pos.x * 2 >= img_size.x || pos.x + 1 >= img_size.x ||
      pos.y * 2 >= img_size.y || pos.y + 1 >= img_size.y) {
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
      haar_block(pos, scaled_size);

      pos.y += int(gl_NumWorkGroups.y);
    }
    pos.x += int(gl_NumWorkGroups.x);
  }
}
```

To simplify reading shaders, I'll also add them here:
## Horizontal Haar wavelet
```glsl
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
```

## Vertical Haar wavelet
```glsl
#version 450

layout(set = 0, binding = 0) uniform sampler2D inputImage;
layout(set = 0, binding = 1) uniform writeonly image2D outputImage;

layout(push_constant, std430) uniform pushConstants {
  ivec2 img_size;
};

void haar_vert(const ivec2 pos, const ivec2 size) {
  const ivec2 real_pos = ivec2(pos.x, pos.y * 2);
  const vec4 colorN = texture(inputImage, real_pos);
  const vec4 colorN_1 = texture(inputImage, ivec2(real_pos.x, pos.y + 1));

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

  while (pos.x < img_size.x) {
    pos.y = int(gl_GlobalInvocationID.y);
    while (pos.y < img_size.y / 2) {
      if (pos.y + 1 >= img_size.y) {
          return;
      }
      haar_vert(pos, scaled_size);
      pos.y += int(gl_NumWorkGroups.y);
    }
    pos.x += int(gl_NumWorkGroups.x);
  }
}
```
