#include "render/RenderAPI.h"
#include "Testing.h"

#include <iostream>
#include <memory>
#include <stdexcept>

using namespace ailo;

namespace {

// Needs a Vulkan device but no window. Returns null, so the tests are skipped, on machines without one.
std::unique_ptr<RenderAPI> makeRenderAPI() {
    try {
        return std::make_unique<RenderAPI>();
    } catch (const std::exception& e) {
        std::cout << "  skipped: no Vulkan device (" << e.what() << ")\n";
        return nullptr;
    }
}

// An image the test owns, as an XR runtime would.
struct OwnedImage {
    vk::Device device;
    vk::Image image;
    vk::DeviceMemory memory;

    OwnedImage(RenderAPI& api, vk::Format format, uint32_t width, uint32_t height, uint32_t layers, vk::ImageUsageFlags usage)
        : device(api.getDevice()) {
        vk::ImageCreateInfo info {};
        info.imageType = vk::ImageType::e2D;
        info.format = format;
        info.extent = vk::Extent3D { width, height, 1 };
        info.mipLevels = 1;
        info.arrayLayers = layers;
        info.samples = vk::SampleCountFlagBits::e1;
        info.tiling = vk::ImageTiling::eOptimal;
        info.usage = usage;
        info.sharingMode = vk::SharingMode::eExclusive;
        info.initialLayout = vk::ImageLayout::eUndefined;
        image = device.createImage(info);

        const auto requirements = device.getImageMemoryRequirements(image);
        const auto properties = api.getPhysicalDevice().getMemoryProperties();
        uint32_t typeIndex = UINT32_MAX;
        for (uint32_t i = 0; i < properties.memoryTypeCount; i++) {
            if ((requirements.memoryTypeBits & (1u << i)) &&
                (properties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal)) {
                typeIndex = i;
                break;
            }
        }
        REQUIRE(typeIndex != UINT32_MAX);
        memory = device.allocateMemory(vk::MemoryAllocateInfo { requirements.size, typeIndex });
        device.bindImageMemory(image, memory, 0);
    }

    ~OwnedImage() { destroy(); }

    void destroy() {
        if (image) {
            device.destroyImage(image);
            device.freeMemory(memory);
            image = nullptr;
            memory = nullptr;
        }
    }
};

constexpr auto kFormat = vk::Format::eR8G8B8A8Unorm;
constexpr uint32_t kSize = 64;

gpu::ExternalImageDesc describe(const OwnedImage& owned, uint8_t layers, vk::ImageUsageFlags usage) {
    return {
        .image = owned.image,
        .format = kFormat,
        .width = kSize,
        .height = kSize,
        .usage = usage,
        .aspect = vk::ImageAspectFlagBits::eColor,
        .layers = layers,
    };
}

void testImportCreatesViews() {
    auto api = makeRenderAPI();
    if (!api) return;

    const auto usage = vk::ImageUsageFlagBits::eColorAttachment;
    OwnedImage single(*api, kFormat, kSize, kSize, 1, usage);
    OwnedImage stereo(*api, kFormat, kSize, kSize, 2, usage);
    {
        auto texture = api->importTexture(describe(single, 1, usage));
        CHECK(!texture->ownsImage());
        CHECK(texture->image == single.image);
        CHECK(bool(texture->imageView));
        CHECK_EQ(texture->getLayerCount(), uint8_t(1));
        CHECK(!texture->getLayerView(0));
        CHECK(texture->getLayout(0) == vk::ImageLayout::eUndefined);

        auto array = api->importTexture(describe(stereo, 2, usage));
        CHECK_EQ(array->getLayerCount(), uint8_t(2));
        CHECK(bool(array->imageView));
        CHECK(bool(array->getLayerView(0)));
        CHECK(bool(array->getLayerView(1)));
        CHECK(array->getLayerView(0) != array->getLayerView(1));
        CHECK(!array->getLayerView(2));
    }

    auto withLayout = describe(single, 1, usage);
    withLayout.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
    auto texture = api->importTexture(withLayout);
    CHECK(texture->getLayout(0) == vk::ImageLayout::eColorAttachmentOptimal);

    gpu::ExternalImageDesc empty {};
    CHECK_THROWS(api->importTexture(empty));
}

// Dropping the texture releases only its views: the image is still usable and the owner destroys it.
void testImportDoesNotOwnImage() {
    auto api = makeRenderAPI();
    if (!api) return;

    const auto usage = vk::ImageUsageFlagBits::eColorAttachment;
    OwnedImage owned(*api, kFormat, kSize, kSize, 2, usage);
    for (int i = 0; i < 2; i++) {
        auto texture = api->importTexture(describe(owned, 2, usage));
        auto target = api->createRenderTarget({ texture.getHandle() }, {}, kSize, kSize);
        target.reset();
        texture.reset();
        api->waitIdle();
    }
    // With the validation layer this would report the image as destroyed twice, or its views as leaked.
    owned.destroy();
}

void testLayeredRenderTarget() {
    auto api = makeRenderAPI();
    if (!api) return;

    const auto usage = vk::ImageUsageFlagBits::eColorAttachment;
    OwnedImage owned(*api, kFormat, kSize, kSize, 2, usage);
    auto texture = api->importTexture(describe(owned, 2, usage));

    auto allLayers = api->createRenderTarget({ texture.getHandle() }, {}, kSize, kSize);
    CHECK_EQ(allLayers->layers, 2u);
    CHECK(allLayers->colors[0].get() == texture.get());

    auto oneLayer = api->createRenderTarget({ texture.getHandle() }, {}, kSize, kSize, 1, 1);
    CHECK_EQ(oneLayer->layers, 1u);

    // A layered pass: creates a 2-layer framebuffer and leaves the image in the layout OpenXR expects back.
    RenderPassDescription description {};
    description.color[0] = { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore };
    api->beginRenderPass(allLayers.getHandle(), description);
    api->endRenderPass();
    CHECK(texture->getLayout(0) == vk::ImageLayout::eColorAttachmentOptimal);

    // A sampled image ends the pass ready for sampling instead.
    const auto sampledUsage = usage | vk::ImageUsageFlagBits::eSampled;
    OwnedImage sampledOwned(*api, kFormat, kSize, kSize, 1, sampledUsage);
    auto sampled = api->importTexture(describe(sampledOwned, 1, sampledUsage));
    auto sampledTarget = api->createRenderTarget({ sampled.getHandle() }, {}, kSize, kSize);
    api->beginRenderPass(sampledTarget.getHandle(), description);
    api->endRenderPass();
    CHECK(sampled->getLayout(0) == vk::ImageLayout::eShaderReadOnlyOptimal);

    // Without a swapchain the recorded commands are never submitted, so the command buffer keeps the textures until
    // the RenderAPI goes. Destroying the images first is allowed since nothing that uses them was submitted.
    allLayers.reset();
    oneLayer.reset();
    sampledTarget.reset();
    texture.reset();
    sampled.reset();
    owned.destroy();
    sampledOwned.destroy();
}

}

void runRenderImportTests() {
    ailo::test::run("render_import/testImportCreatesViews", testImportCreatesViews);
    ailo::test::run("render_import/testImportDoesNotOwnImage", testImportDoesNotOwnImage);
    ailo::test::run("render_import/testLayeredRenderTarget", testLayeredRenderTarget);
}
