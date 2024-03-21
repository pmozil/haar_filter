#include "libavutil/opt.h"
#include "libavfilter/vulkan_filter.h"
#include "libavfilter/vulkan_spirv.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/internal.h"
#include "libavfilter/video.h"
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
  } opts;
} HaarFilterContext;

static const char haar_filt[] = {
    C(0,  void haar_block(const ivec2 pos, const ivec2 scaled_size)       )
    C(0,  {                                                               )
    C(1,  const ivec2 real_pos = pos * 2;                                 )
    C(1,  const vec4 pix_11 = texture(inputImage, real_pos);              )
    C(1,  const vec4 pix_12 = texture(inputImage, ivec2(real_pos.x + 1, real_pos.y));     )
    C(1,  const vec4 pix_21 = texture(inputImage, ivec2(real_pos.x, real_pos.y + 1));     )
    C(1,  const vec4 pix_22 = texture(inputImage, ivec2(real_pos.x + 1, real_pos.y + 1)); )
    C(1,  imageStore(outputImage, pos.xy, pix_11 + pix_12 + pix_21);                      )
    C(1,  imageStore(outputImage, ivec2(pos.x + scaled_size.x, pos.y), pix_11 - pix_12 + pix_21 + pix_22);)
    C(1,  imageStore(outputImage, ivec2(pos.x, pos.y + scaled_size.y, pix_11 + pix_12 - pix_21 - pix_22));)
    C(1,  imageStore(outputImage, pos + scaled_size, pix_11 - pix_12 - pix_21 + pix_22);)
    C(0,}                                                                 )
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
    RET(ff_vk_exec_pool_init(vkctx, &s->qf, &s->e, s->qf.nb_queues*4, 0, 0, 0, NULL));
    RET(ff_vk_init_sampler(vkctx, &s->sampler, 1, VK_FILTER_LINEAR));
    RET(ff_vk_shader_init(&s->pl, &s->shd, "haar_conmpute",
                          VK_SHADER_STAGE_COMPUTE_BIT, 0));
    shd = &s->shd;

    ff_vk_shader_set_compute_sizes(shd, 32, 1, 1);

    desc = (FFVulkanDescriptorSetBinding []) {
        {
            .name       = "inputImage",
            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            .samplers   = DUP_SAMPLER(s->sampler),
        },
        {
            .name       = "outputImage",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .mem_layout = ff_vk_shader_rep_fmt(s->vkctx.output_format),
            .mem_quali  = "writeonly",
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };

    RET(ff_vk_pipeline_descriptor_set_add(vkctx, &s->pl, shd, desc, 2, 0, 0));

    GLSLC(0, layout(push_constant, std430) uniform pushConstants {        );
    GLSLC(1,    ivec2 img_szei;                                           );
    GLSLC(0, };                                                           );
    GLSLC(0,                                                              );

    ff_vk_add_push_constant(&s->pl, 0, sizeof(s->opts),
                            VK_SHADER_STAGE_COMPUTE_BIT);

    GLSLD(   haar_filt                                                            );
    GLSLC(0, void main()                                                          );
    GLSLC(0, {                                                                    );
    GLSLC(1, ivec2 pos = ivec2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y); );
    GLSLC(1, if (pos.x * 2 >= img_size.x ||                                       );
    GLSLC(2,         pos.x + 1 >= img_size.x ||                                   );
    GLSLC(2,         pos.y * 2 >= img_size.y ||                                   );
    GLSLC(2,         pos.y + 1 >= img_size.y) {                                   );
    GLSLC(2,             return;                                                  );
    GLSLC(1, }                                                                    );
    GLSLC(1, const ivec2 scaled_size = img_size / 2;                              );
    GLSLC(1, while (pos.x < img_size.x) {                                         );
    GLSLC(2,     pos.y = int(gl_GlobalInvocationID.y);                            );
    GLSLC(2,     while (pos.y < img_size.y / 2) {                                 );
    GLSLC(3,         if (pos.y + 1 >= img_size.y) {                               );
    GLSLC(4,             return;                                                  );
    GLSLC(3,         }                                                            );
    GLSLC(3,         haar_block(pos, scaled_size);                                );
    GLSLC(3,         imageStore(outputImage, pos.xy, pix_11 + pix_12 + pix_21);   );
    GLSLC(3,         imageStore(outputImage, ivec2(pos.x + scaled_size.x, pos.y), );
    GLSLC(4,                 pix_11 - pix_12 + pix_21 + pix_22);                  );
    GLSLC(3,         imageStore(outputImage, ivec2(pos.x, pos.y + scaled_size.y), );
    GLSLC(4,                 pix_11 + pix_12 - pix_21 - pix_22);                  );
    GLSLC(3,         imageStore(outputImage, pos + scaled_size,                   );
    GLSLC(4,                 pix_11 - pix_12 - pix_21 + pix_22);                  );
    GLSLC(3,         pos.y += int(gl_NumWorkGroups.y);                            );
    GLSLC(2,       }                                                              );
    GLSLC(2,       pos.x += int(gl_NumWorkGroups.x);                              );
    GLSLC(1,     }                                                                );
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

    s->opts.size[0] = outlink->w;
    s->opts.size[1] = outlink->h;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    if (!s->initialized)
        RET(init_filter(ctx, in));

    RET(ff_vk_filter_process_simple(&s->vkctx, &s->e, &s->pl,
                                    out, in, s->sampler, &s->opts, sizeof(s->opts)));
    err = av_frame_copy_props(out, in);
    if (err < 0)
        goto fail;

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);

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
        vk->DestroySampler(vkctx->hwctx->act_dev, s->sampler,
                           vkctx->hwctx->alloc);

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
    .description = NULL_IF_CONFIG_SMALL("Apply haar wavelet transform to video"),
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
