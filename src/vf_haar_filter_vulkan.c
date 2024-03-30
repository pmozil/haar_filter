#include "libavfilter/avfilter.h"
#include "libavfilter/internal.h"
#include "libavfilter/video.h"
#include "libavfilter/vulkan_filter.h"
#include "libavfilter/vulkan_spirv.h"
#include "libavutil/opt.h"
#include <math.h>

typedef struct HaarFilterContext {
  FFVulkanContext vkctx;

  int initialized;
  FFVkExecPool e;
  FFVkQueueFamilyCtx qf;
  VkSampler sampler;

  FFVulkanPipeline pl;
  FFVkSPIRVShader shd;

  struct {
    int32_t size[2];
    int32_t iteration;
  } opts;
} HaarFilterContext;

static const char haar_filt[] = {
    C(0, void haar_block(const ivec2 pos, const ivec2 scaled_size)                                                        )
    C(0, {                                                                                                                )
    C(1,  const ivec2 real_pos = pos * 2;                                                                                 )
    C(1,  const vec3 pix_11 = texture(inputImage[0], real_pos).rgb;                                                       )
    C(1,  const vec3 pix_12 = texture(inputImage[0], ivec2(real_pos.x + 1, real_pos.y)).rgb;                              )
    C(1,  const vec3 pix_21 = texture(inputImage[0], ivec2(real_pos.x, real_pos.y + 1)).rgb;                              )
    C(1,  const vec3 pix_22 = texture(inputImage[0], ivec2(real_pos.x + 1, real_pos.y + 1)).rgb;                          )
    C(1,  imageStore(outputImage[0], pos, vec4(pix_11 + pix_12 + pix_21, 1.0));                                           )
    C(1,  imageStore(outputImage[0], ivec2(pos.x + scaled_size.x, pos.y), vec4(pix_11 - pix_12 + pix_21 + pix_22, 1.0));  )
    C(1,  imageStore(outputImage[0], ivec2(pos.x, pos.y + scaled_size.y), vec4(pix_11 + pix_12 - pix_21 - pix_22, 1.0));  )
    C(1,  imageStore(outputImage[0], pos + scaled_size, vec4(pix_11 - pix_12 - pix_21 + pix_22, 1.0));                    )
    C(0,}                                                                                                                 )
};

static const char copy_block[] = {
    C(0, void copy_pixels()                             )
    C(0, {                                                                                      )
    C(1, for (int x = img_size.x + int(gl_GlobalInvocationID.x); x < img_size.x * 2; x += int(gl_NumWorkGroups.x)) {                )
    C(2,     for (int y = int(gl_GlobalInvocationID.y); y < img_size.y * 2; y += int(gl_NumWorkGroups.y)) {                         )
    C(3,     const vec3 col = texture(inputImage[0], ivec2(x, y)).rgb;                          )
    C(3,     imageStore(outputImage[0], ivec2(x, y), vec4(col, 1.0));                           )
    C(2,     }                                                                                  )
    C(1, }                                                                                      )
    C(1, for (int x = int(gl_GlobalInvocationID.x); x < img_size.x; x += int(gl_NumWorkGroups.x)) {                         )
    C(2,     for (int y = img_size.y + int(gl_GlobalInvocationID.y); y < img_size.y * 2; y += int(gl_NumWorkGroups.y)) {            )
    C(3,     const vec3 col = texture(inputImage[0], ivec2(x, y)).rgb;                          )
    C(3,     imageStore(outputImage[0], ivec2(x, y), vec4(col, 1.0));                           )
    C(2,     }                                                                                  )
    C(1, }                                                                                      )
    C(0,}                                                                                       )
};

static const char apply_haar_filt[] = {
  C(0, void apply_filter(const ivec2 im_size)                               )
  C(0, {                                                                    )
  // C(1, if (pos.x * 2 >= im_size.x || pos.x + 1 >= im_size.x || pos.y * 2 >= im_size.y || pos.y + 1 >= im_size.y) {)
  // C(2,             return;                                                  )
  // C(1, }                                                                    )
  C(1, const ivec2 scaled_size = im_size / 2;                               )
  C(1, ivec2 pos = ivec2(scaled_size.x - gl_GlobalInvocationID.x, scaled_size.y - gl_GlobalInvocationID.y); )
  C(1, while (pos.x >= 0) {                                      )
  C(2,     pos.y = int(scaled_size.y - gl_GlobalInvocationID.y);                            )
  C(2,     while (pos.y >= 0) {                                  )
  C(3,         if (pos.y + 1 >= im_size.y) {                                )
  C(4,             return;                                                  )
  C(3,         }                                                            )
  C(3,         haar_block(pos, scaled_size);                                )
  C(3,         pos.y -= int(gl_NumWorkGroups.y);                            )
  C(2,       }                                                              )
  C(2,       pos.x -= int(gl_NumWorkGroups.x);                              )
  C(1,     }                                                                )
  C(0, }                                                                    )
};

static av_cold int init_filter(AVFilterContext *ctx, AVFrame *in) {
  int err;
  uint8_t *spv_data;
  size_t spv_len;
  void *spv_opaque = NULL;
  HaarFilterContext *s = ctx->priv;
  FFVulkanContext *vkctx = &s->vkctx;
  const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);
  FFVkSPIRVShader *shd;
  FFVkSPIRVCompiler *spv;
  FFVulkanDescriptorSetBinding *desc;

  spv = ff_vk_spirv_init();
  if (!spv) {
    av_log(ctx, AV_LOG_ERROR, "Unable to initialize SPIR-V compiler!\n");
    return AVERROR_EXTERNAL;
  }

  ff_vk_qf_init(vkctx, &s->qf, VK_QUEUE_COMPUTE_BIT);
  RET(ff_vk_exec_pool_init(vkctx, &s->qf, &s->e, s->qf.nb_queues * 4, 0, 0, 0,
                           NULL));
  RET(ff_vk_init_sampler(vkctx, &s->sampler, 1, VK_FILTER_LINEAR));
  RET(ff_vk_shader_init(&s->pl, &s->shd, "haar_compute",
                        VK_SHADER_STAGE_COMPUTE_BIT, 0));
  shd = &s->shd;

  ff_vk_shader_set_compute_sizes(shd, 32, 16, 1);

  desc = (FFVulkanDescriptorSetBinding[]){
      {
          .name = "inputImage",
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .dimensions = 2,
          .elems = planes,
          .stages = VK_SHADER_STAGE_COMPUTE_BIT,
          .samplers = DUP_SAMPLER(s->sampler),
      },
      {
          .name = "outputImage",
          .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          .mem_layout = ff_vk_shader_rep_fmt(s->vkctx.output_format),
          .mem_quali = "writeonly",
          .dimensions = 2,
          .elems = planes,
          .stages = VK_SHADER_STAGE_COMPUTE_BIT,
      },
  };

  RET(ff_vk_pipeline_descriptor_set_add(vkctx, &s->pl, shd, desc, 2, 0, 0));

  GLSLC(0, layout(push_constant, std430) uniform pushConstants {        );
  GLSLC(1,    ivec2 img_size;                                           );
  GLSLC(1,    int iteration;                                            );
  GLSLC(0, };                                                           );
  GLSLC(0,                                                              );

  ff_vk_add_push_constant(&s->pl, 0, sizeof(s->opts),
                          VK_SHADER_STAGE_COMPUTE_BIT);

  GLSLD(   haar_filt                                                            );
  GLSLD(   apply_haar_filt                                                      );
  GLSLD(   copy_block                                                           );
  GLSLC(0, void main()                                                          );
  GLSLC(0, {                                                                    );
  GLSLC(1, ivec2 pos = ivec2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y); );
  GLSLC(1, if (pos.x * 2 >= img_size.x || pos.x + 1 >= img_size.x || pos.y * 2 >= img_size.y || pos.y + 1 >= img_size.y) {);
  GLSLC(2,             return;                                                  );
  GLSLC(1, }                                                                    );
  GLSLC(1, apply_filter(img_size);                                              );
  GLSLC(1, if (iteration != 0) {                                                );
  GLSLC(2,     copy_pixels();                                                   );
  GLSLC(1, }                                                                    );
  // GLSLC(1, apply_filter(img_size / 2);                                              );
  // GLSLC(1, apply_filter(img_size / 4);                                              );
  GLSLC(0, }                                                                    );

  RET(spv->compile_shader(spv, ctx, &s->shd, &spv_data, &spv_len, "main",
                          &spv_opaque));
  RET(ff_vk_shader_create(vkctx, &s->shd, spv_data, spv_len, "main"));

  RET(ff_vk_init_compute_pipeline(vkctx, &s->pl, &s->shd));
  RET(ff_vk_exec_pipeline_register(vkctx, &s->e, &s->pl));

  s->initialized = 1;
fail:
  if (spv_opaque)
    spv->free_shader(spv, &spv_opaque);
  if (spv)
    spv->uninit(&spv);

  return err;
}

static int haar_vulkan_filter_frame(AVFilterLink *link, AVFrame *in) {
  int err;
  AVFrame *out = NULL;
  AVFilterContext *ctx = link->dst;
  HaarFilterContext *s = ctx->priv;
  AVFilterLink *outlink = ctx->outputs[0];

  if (!s->initialized)
    RET(init_filter(ctx, in));

  s->opts.size[0] = outlink->w;
  s->opts.size[1] = outlink->h;
  s->opts.iteration = 0;

  out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
  if (!out) {
    err = AVERROR(ENOMEM);
    goto fail;
  }

  RET(ff_vk_filter_process_simple(&s->vkctx, &s->e, &s->pl, out, in, s->sampler,
                                  &s->opts, sizeof(s->opts)));
  s->opts.size[0] /= 2;
  s->opts.size[1] /= 2;
  s->opts.iteration = 1;
  RET(ff_vk_filter_process_simple(&s->vkctx, &s->e, &s->pl, in, out, s->sampler,
                                  &s->opts, sizeof(s->opts)));
  err = av_frame_copy_props(out, in);
  if (err < 0)
    goto fail;

  av_frame_free(&out);

  return ff_filter_frame(outlink, in);

fail:
  av_frame_free(&in);
  av_frame_free(&out);
  return err;
}
static void haar_vulkan_uninit(AVFilterContext *avctx) {
  HaarFilterContext *s = avctx->priv;
  FFVulkanContext *vkctx = &s->vkctx;
  FFVulkanFunctions *vk = &vkctx->vkfn;

  ff_vk_exec_pool_free(vkctx, &s->e);

  ff_vk_pipeline_free(vkctx, &s->pl);
  ff_vk_shader_free(vkctx, &s->shd);

  if (s->sampler)
    vk->DestroySampler(vkctx->hwctx->act_dev, s->sampler, vkctx->hwctx->alloc);

  ff_vk_uninit(&s->vkctx);

  s->initialized = 0;
}

static const AVOption haar_vulkan_options[] = {
    {NULL},
};

AVFILTER_DEFINE_CLASS(haar_vulkan);

static const AVFilterPad haar_vulkan_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &haar_vulkan_filter_frame,
        .config_props = &ff_vk_filter_config_input,
    },
};

static const AVFilterPad haar_vulkan_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vk_filter_config_output,
    },
};

const AVFilter ff_vf_haar_vulkan = {
    .name = "haar_vulkan",
    .description =
        NULL_IF_CONFIG_SMALL("Apply haar wavelet transform to video"),
    .priv_size = sizeof(HaarFilterContext),
    .init = &ff_vk_filter_init,
    .uninit = &haar_vulkan_uninit,
    FILTER_INPUTS(haar_vulkan_inputs),
    FILTER_OUTPUTS(haar_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .priv_class = &haar_vulkan_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
    .flags = AVFILTER_FLAG_HWDEVICE,
};
