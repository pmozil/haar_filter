#include "include/vulkan_haar_enc.h"

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
  RET(ff_vk_shader_init(&s->pl_vert, &s->shd_vert, "haar_vert_compute",
                        VK_SHADER_STAGE_COMPUTE_BIT, 0));
  shd = &s->shd_vert;

  ff_vk_shader_set_compute_sizes(shd, 32, 1, 1);

  desc = (FFVulkanDescriptorSetBinding[]){
      {
          .name = "input_img",
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .dimensions = 2,
          .elems = 1,
          .stages = VK_SHADER_STAGE_COMPUTE_BIT,
          .samplers = DUP_SAMPLER(s->sampler),
      },
      {
          .name = "output_img",
          .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          .mem_layout = ff_vk_shader_rep_fmt(s->vkctx.output_format),
          .mem_quali = "writeonly",
          .dimensions = 2,
          .elems = 1,
          .stages = VK_SHADER_STAGE_COMPUTE_BIT,
      },
  };

  RET(ff_vk_pipeline_descriptor_set_add(vkctx, &s->pl_vert, shd, desc, 2, 0, 0));

  GLSLC(0, layout(push_constant, std430) uniform pushConstants {        );
  GLSLC(1,    ivec2 img_size;                                           );
  GLSLC(0, };                                                           );
  GLSLC(0,                                                              );

  ff_vk_add_push_constant(&s->pl_vert, 0, sizeof(s->opts),
            VK_SHADER_STAGE_COMPUTE_BIT);

  GLSLC(0, );

  GLSLD(haar_vert);
  GLSLC(0, void main() { );
  GLSLC(1,   ivec2 pos = ivec2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y); );
  GLSLC(0,  );
  GLSLC(1,   if (pos.y * 2 >= img_size.y || );
  GLSLC(2,         pos.y + 1 >= img_size.y || );
  GLSLC(2,         pos.x >= img_size.x) { );
  GLSLC(2,       return; );
  GLSLC(1,   } );
  GLSLC(0,  );
  GLSLC(1,   const ivec2 scaled_size = img_size / 2; );
  GLSLC(0,  );
  GLSLC(1,   while (pos.x < img_size.x) { );
  GLSLC(2,     pos.y = int(gl_GlobalInvocationID.y); );
  GLSLC(2,     while (pos.y < img_size.y / 2) { );
  GLSLC(3,       if (pos.y + 1 >= img_size.y) { );
  GLSLC(4,           return; );
  GLSLC(3,       } );
  GLSLC(3,       haar_vert(pos, scaled_size); );
  GLSLC(3,       pos.y += int(gl_NumWorkGroups.y); );
  GLSLC(2,     } );
  GLSLC(2,     pos.x += int(gl_NumWorkGroups.x;); );
  GLSLC(1,   } );
  GLSLC(0, } );

  RET(spv->compile_shader(spv, ctx, &s->shd_horiz, &spv_data, &spv_len, "main",
                          &spv_opaque));
  RET(ff_vk_shader_create(vkctx, &s->shd_horiz, spv_data, spv_len, "main"));

  RET(ff_vk_init_compute_pipeline(vkctx, &s->pl_horiz, &s->shd_horiz));
  RET(ff_vk_exec_pipeline_register(vkctx, &s->e, &s->pl_horiz));

  RET(ff_vk_shader_init(&s->pl_horiz, &s->shd_horiz, "haar_horiz_compute",
                        VK_SHADER_STAGE_COMPUTE_BIT, 0));
  shd = &s->shd_horiz;

  ff_vk_shader_set_compute_sizes(shd, 32, 1, 1);

  RET(ff_vk_pipeline_descriptor_set_add(vkctx, &s->pl_horiz, shd, desc, 2, 0, 0));

  GLSLC(0, layout(push_constant, std430) uniform pushConstants {        );
  GLSLC(1,    ivec2 img_size;                                           );
  GLSLC(0, };                                                           );
  GLSLC(0,                                                              );

  ff_vk_add_push_constant(&s->pl_horiz, 0, sizeof(s->opts),
            VK_SHADER_STAGE_COMPUTE_BIT);

  GLSLC(0, );

  GLSLD(haar_horiz);
  GLSLC(0, void main() { );
  GLSLC(1,   ivec2 pos = ivec2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y); );
  GLSLC(0,  );
  GLSLC(1,   if (pos.x * 2 >= img_size.x || );
  GLSLC(2,         pos.x + 1 >= img_size.x || );
  GLSLC(2,         pos.y >= img_size.y) { );
  GLSLC(2,       return; );
  GLSLC(1,   } );
  GLSLC(0,  );
  GLSLC(1,   const ivec2 scaled_size = img_size / 2; );
  GLSLC(0,  );
  GLSLC(1,   while (pos.y < img_size.y { ));
  GLSLC(2,     pos.x = int(gl_GlobalInvocationID.x); );
  GLSLC(2,     while (pos.x < img_size.x / 2) { );
  GLSLC(3,       if (pos.x + 1 >= img_size.x) { );
  GLSLC(4,           return; );
  GLSLC(3,       } );
  GLSLC(3,       haar_evrt(pos, scaled_size); );
  GLSLC(3,       pos.x += int(gl_NumWorkGroups.x); );
  GLSLC(2,     } );
  GLSLC(2,     pos.y += int(gl_NumWorkGroups.y); );
  GLSLC(1,   } );
  GLSLC(0, } );

  RET(spv->compile_shader(spv, ctx, &s->shd_horiz, &spv_data, &spv_len, "main",
                          &spv_opaque));
  RET(ff_vk_shader_create(vkctx, &s->shd_horiz, spv_data, spv_len, "main"));

  RET(ff_vk_init_compute_pipeline(vkctx, &s->pl_horiz, &s->shd_horiz));
  RET(ff_vk_exec_pipeline_register(vkctx, &s->e, &s->pl_horiz));

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
    AVFrame *out_tmp = NULL;
    AVFilterContext *ctx = link->dst;
    HaarFilterContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    s->opts.size[0] = outlink->w;
    s->opts.size[1] = outlink->h;

    out_tmp = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out_tmp) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    if (!s->initialized)
        RET(init_filter(ctx, in));

    RET(ff_vk_filter_process_simple(&s->vkctx, &s->e, &s->pl_vert,
                                    out_tmp, in, s->sampler, &s->opts, sizeof(s->opts)));
    err = av_frame_copy_props(out_tmp, in);
    if (err < 0)
        goto fail;

    av_frame_free(&in);

    RET(ff_vk_filter_process_simple(&s->vkctx, &s->e, &s->pl_vert,
                                    out, out_tmp, s->sampler, &s->opts, sizeof(s->opts)));
    err = av_frame_copy_props(out, out_tmp);
    if (err < 0)
        goto fail;

    av_frame_free(&out_tmp);

    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&in);
    av_frame_free(&out_tmp);
    av_frame_free(&out);
    return err;
}

static void haar_vulkan_uninit(AVFilterContext *avctx) {
    HaarFilterContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;

    ff_vk_exec_pool_free(vkctx, &s->e);

    ff_vk_pipeline_free(vkctx, &s->pl_vert);
    ff_vk_shader_free(vkctx, &s->shd_vert);

    ff_vk_pipeline_free(vkctx, &s->pl_horiz);
    ff_vk_shader_free(vkctx, &s->shd_horiz);

    if (s->sampler)
        vk->DestroySampler(vkctx->hwctx->act_dev, s->sampler,
                           vkctx->hwctx->alloc);

    ff_vk_uninit(&s->vkctx);

    s->initialized = 0;
}
