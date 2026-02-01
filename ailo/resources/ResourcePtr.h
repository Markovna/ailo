#pragma once
#include "render/RenderPrimitive.h"

namespace ailo {

template<typename T, typename... Args>
std::shared_ptr<T> make_resource(Engine& engine, Args&&... args) {
    return std::shared_ptr<T>(new T(std::forward<Args>(args)...), [&engine](T* ptr) {
        ptr->destroy(engine);
    });
}

}
