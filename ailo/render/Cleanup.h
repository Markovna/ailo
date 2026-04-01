#pragma once

#include <functional>
#include <vector>

#include "Program.h"
#include "RenderAPI.h"

namespace ailo {

class RenderAPI;

class Cleanup {
public:
    explicit Cleanup(RenderAPI* renderApi) : m_renderApi(renderApi)
    { }

    ~Cleanup();

    template <typename HandleType>
    Handle<HandleType> add(Handle<HandleType> handle);

private:
    struct overloads {
        static void destroy(RenderAPI* api, const Handle<gpu::Program>& handle) { api->destroyProgram(handle); }
        static void destroy(RenderAPI* api, const Handle<gpu::Buffer>& handle) { api->destroyBuffer(handle); }
        static void destroy(RenderAPI* api, const Handle<gpu::VertexBufferLayout>& handle) { api->destroyVertexBufferLayout(handle); }
        static void destroy(RenderAPI* api, const Handle<gpu::DescriptorSet>& handle) { api->destroyDescriptorSet(handle); }
        static void destroy(RenderAPI* api, const Handle<gpu::DescriptorSetLayout>& handle) { api->destroyDescriptorSetLayout(handle); }
        static void destroy(RenderAPI* api, const Handle<gpu::Texture>& handle) { api->destroyTexture(handle); }
        static void destroy(RenderAPI* api, const Handle<gpu::RenderTarget>& handle) { api->destroyRenderTarget(handle); }
    };

private:
    RenderAPI* m_renderApi;
    std::vector<std::function<void(RenderAPI*)>> m_callbacks;
};

inline Cleanup::~Cleanup() {
    for (auto& callback : m_callbacks) {
        callback(m_renderApi);
    }
}

template <typename HandleType>
Handle<HandleType> Cleanup::add(Handle<HandleType> handle) {
    m_callbacks.push_back([handle](auto* api) {
        overloads::destroy(api, handle);
    });

    return handle;
}

}
