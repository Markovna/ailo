#include "Scene.h"

ailo::Scene::Scene()
    : m_registry(), m_singleEntity(m_registry.create()) {
}
