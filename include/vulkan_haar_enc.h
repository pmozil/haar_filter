#ifndef VULKAN_HAAR_H
#define VULKAN_HAAR_H

#include "libavutil/opt.h"
#include "libavfilter/vulkan_filter.h"
#include "libavfilter/vulkan_spirv.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/internal.h"
#include "libavfilter/video.h"

typedef struct HaarFilterContext {
  FFVulkanContext vkctx;

  int initialized;
  FFVkExecPool e;
  FFVkQueueFamilyCtx qf;
  VkSampler sampler;

  FFVulkanPipeline pl_vert;
  FFVkSPIRVShader shd_vert;

  FFVulkanPipeline pl_horiz;
  FFVkSPIRVShader shd_horiz;

  struct {
    int32_t size[2];
  } opts;
} HaarFilterContext;

static const char haar_horiz[] = {
    C(0, void haar_horiz(const ivec2 pos, const ivec2 size) {                       )
    C(1,   const ivec2 real_pos = ivec2(pos.x * 2, pos.y);                          )
    C(1,   const vec4 colorN = texture(inputImage, real_pos);                       )
    C(1,   const vec4 colorN_1 = texture(inputImage, ivec2(real_pos.x + 1, pos.y)); )
    C(0,                                                                            )
    C(1,   imageStore(outputImage, pos.xy, colorN + colorN_1);                      )
    C(1,   imageStore(outputImage, ivec2(pos.x + size.x, pos.y),                    )
    C(2,       colorN - colorN_1);                                                  )
    C(0, }                                                                          )
};

static const char haar_vert[] = {
    C(0, void haar_vert(const ivec2 pos, const ivec2 size) {                        )
    C(1,   const ivec2 real_pos = ivec2(pos.x, pos.y * 2);                          )
    C(1,   const vec4 colorN = texture(inputImage, real_pos);                       )
    C(1,   const vec4 colorN_1 = texture(inputImage, ivec2(real_pos.x, pos.y + 1)); )
    C(0,                                                                            )
    C(1,   imageStore(outputImage, pos.xy, colorN + colorN_1);                      )
    C(1,   imageStore(outputImage, ivec2(pos.x, pos.y + size.y),                    )
    C(2,       colorN - colorN_1);                                                  )
    C(0, }                                                                          )
};


static av_cold int init_filter(AVFilterContext *ctx, AVFrame *in);
static int haar_vulkan_filter_frame(AVFilterLink *link, AVFrame *in);
static void haar_vulkan_uninit(AVFilterContext *avctx);

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

#endif
