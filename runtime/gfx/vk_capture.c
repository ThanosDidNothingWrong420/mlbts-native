// mlbts-native, our own: the render targets of the capture frame (gfx_stubs.c gfx_capture_arm: F12, MLB_DRAWLOG=game)
// as PNGs, <profile>/shot-<frame>-<n>-<w>x<h>-fmt<NVN format>.png, so a frame's intermediate pictures (an HDR scene,
// exposure targets, UI layers) can be looked at, not just the presented one. Each colour target is blitted into an
// RGBA8 image (the blit converts float and packed formats; values above 1 clamp), copied to a host buffer and written.
// It waits for the queue to go idle first: a diagnosis path, taken once per capture.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "nvn_obj.h"
#include "vk.h"

void rt_write_png(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h);   // gfx_stubs.c

static int blit_src_ok(VkPhysicalDevice phys, VkFormat f) {
    VkFormatProperties p;
    vkGetPhysicalDeviceFormatProperties(phys, f, &p);
    return (p.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) != 0;
}

static void save_one(VkCtx *c, VkCommandPool pool, const VkTexImage *im, const char *path) {
    uint32_t w = im->width, h = im->height;
    VkImage tmp = VK_NULL_HANDLE;
    VmaAllocation tmp_a = NULL, buf_a = NULL;
    VkBuffer buf = VK_NULL_HANDLE;
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                            .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {w, h, 1}, .mipLevels = 1, .arrayLayers = 1,
                            .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
    VmaAllocationCreateInfo ia = {.usage = VMA_MEMORY_USAGE_AUTO};
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = (VkDeviceSize)w * h * 4,
                             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VmaAllocationCreateInfo ba = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                                  .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT};
    VmaAllocationInfo binfo;
    if (vmaCreateImage(c->vma, &ii, &ia, &tmp, &tmp_a, NULL) != VK_SUCCESS ||
        vmaCreateBuffer(c->vma, &bi, &ba, &buf, &buf_a, &binfo) != VK_SUCCESS || !binfo.pMappedData) {
        rt_log("capture: %s: no staging memory", path);
        goto done;
    }
    VkCommandBufferAllocateInfo ci = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
                                      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer cb;
    if (vkAllocateCommandBuffers(c->device, &ci, &cb) != VK_SUCCESS) goto done;
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    vkBeginCommandBuffer(cb, &begin);
    VkImageMemoryBarrier b0 = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                               .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                               .image = tmp, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b0);
    VkImageBlit region = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                          .srcOffsets = {{0, 0, 0}, {(int32_t)w, (int32_t)h, 1}},
                          .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                          .dstOffsets = {{0, 0, 0}, {(int32_t)w, (int32_t)h, 1}}};
    vkCmdBlitImage(cb, im->image, VK_IMAGE_LAYOUT_GENERAL, tmp, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region,
                   VK_FILTER_NEAREST);
    VkImageMemoryBarrier b1 = b0;
    b1.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, b1.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b1.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, b1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b1);
    VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {w, h, 1}};
    vkCmdCopyImageToBuffer(cb, tmp, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &copy);
    vkEndCommandBuffer(cb);
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb};
    vk_queue_lock();
    VkResult r = vkQueueSubmit(c->gfx_queue, 1, &si, VK_NULL_HANDLE);
    if (r == VK_SUCCESS) r = vkQueueWaitIdle(c->gfx_queue);
    vk_queue_unlock();
    vkFreeCommandBuffers(c->device, pool, 1, &cb);
    if (r != VK_SUCCESS) {
        rt_log("capture: %s: submit failed (%d)", path, (int)r);
        goto done;
    }
    vmaInvalidateAllocation(c->vma, buf_a, 0, VK_WHOLE_SIZE);
    uint8_t *px = binfo.pMappedData;
    for (uint64_t i = 3; i < (uint64_t)w * h * 4; i += 4) px[i] = 255;   // the picture, not its alpha
    rt_write_png(path, px, w, h);
    rt_log("capture: %s", path);
done:
    if (buf) vmaDestroyBuffer(c->vma, buf, buf_a);
    if (tmp) vmaDestroyImage(c->vma, tmp, tmp_a);
}

// The GPU's bytes of [addr, addr+n) (a mirror's device-local copy when the pool has one), copied through a host buffer.
// The queue must be idle (vk_drawq_report runs after vk_capture_targets). 0 when the range is not backed.
int vk_capture_read(uint64_t addr, void *out, uint32_t n) {
    VkCtx *c = vk_ctx();
    VkMemSpan sp;
    if (!c || !n || !vk_mem_resolve(addr, n, &sp)) return 0;
    VkBuffer buf = VK_NULL_HANDLE;
    VmaAllocation ba = NULL;
    VmaAllocationInfo info;
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = n, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                                  .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT};
    if (vmaCreateBuffer(c->vma, &bi, &ai, &buf, &ba, &info) != VK_SUCCESS || !info.pMappedData) return 0;
    VkCommandPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                  .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = c->gfx_family};
    VkCommandPool pool;
    int ok = 0;
    if (vkCreateCommandPool(c->device, &pi, NULL, &pool) == VK_SUCCESS) {
        VkCommandBufferAllocateInfo ci = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
                                          .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
        VkCommandBuffer cb;
        if (vkAllocateCommandBuffers(c->device, &ci, &cb) == VK_SUCCESS) {
            VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                              .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
            vkBeginCommandBuffer(cb, &begin);
            VkBufferCopy cp = {sp.offset, 0, n};
            vkCmdCopyBuffer(cb, sp.buffer, buf, 1, &cp);
            vkEndCommandBuffer(cb);
            VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb};
            vk_queue_lock();
            VkResult r = vkQueueSubmit(c->gfx_queue, 1, &si, VK_NULL_HANDLE);
            if (r == VK_SUCCESS) r = vkQueueWaitIdle(c->gfx_queue);
            vk_queue_unlock();
            if (r == VK_SUCCESS) {
                vmaInvalidateAllocation(c->vma, ba, 0, VK_WHOLE_SIZE);
                memcpy(out, info.pMappedData, n);
                ok = 1;
            }
        }
        vkDestroyCommandPool(c->device, pool, NULL);
    }
    vmaDestroyBuffer(c->vma, buf, ba);
    return ok;
}

void vk_capture_targets(const tex_t *const *ts, int n, uint64_t frame) {
    VkCtx *c = vk_ctx();
    if (!c || !n) return;
    VkCommandPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                  .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = c->gfx_family};
    VkCommandPool pool;
    if (vkCreateCommandPool(c->device, &pi, NULL, &pool) != VK_SUCCESS) return;
    vk_queue_lock();
    vkQueueWaitIdle(c->gfx_queue);   // the frame's rendering is done
    vk_queue_unlock();
    for (int i = 0; i < n; i++) {
        const VkTexImage *im = ts[i] ? vk_texture_get(ts[i]) : NULL;
        if (!im || im->image == VK_NULL_HANDLE || !(im->aspect & VK_IMAGE_ASPECT_COLOR_BIT) || im->samples > 1 ||
            im->type != VK_IMAGE_TYPE_2D || !blit_src_ok(c->phys, im->format))
            continue;
        char path[1200];
        snprintf(path, sizeof path, "%s/shot-%llu-%d-%ux%u-fmt%#x.png", rt_config()->profile, (unsigned long long)frame, i,
                 im->width, im->height, (unsigned)ts[i]->d.format);
        save_one(c, pool, im, path);
    }
    vkDestroyCommandPool(c->device, pool, NULL);
}
