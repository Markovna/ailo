#include "Transform.h"
#include "Hierarchy.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include <entt/entt.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace ailo;

namespace {

constexpr float kEpsilon = 1e-4f;

bool near(float a, float b) { return std::abs(a - b) <= kEpsilon; }

bool near(const glm::vec3& a, const glm::vec3& b) {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

bool near(const glm::mat4& a, const glm::mat4& b) {
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            if (!near(a[c][r], b[c][r])) return false;
    return true;
}

// q and -q are the same rotation.
bool sameRotation(const glm::quat& a, const glm::quat& b) {
    return near(std::abs(glm::dot(a, b)), 1.0f);
}

bool finite(const Transform& t) {
    for (int i = 0; i < 3; i++)
        if (!std::isfinite(t.position[i]) || !std::isfinite(t.scale[i])) return false;
    for (int i = 0; i < 4; i++)
        if (!std::isfinite(t.rotation[i])) return false;
    return true;
}

Transform sample() {
    return {
        .position = { 1.0f, -2.0f, 3.5f },
        .rotation = glm::angleAxis(glm::radians(37.0f), glm::normalize(glm::vec3(1.0f, 2.0f, -0.5f))),
        .scale = { 2.0f, 0.5f, 3.0f },
    };
}

void testDefaultIsIdentity() {
    assert(Transform {}.toMatrix() == glm::mat4(1.0f));
}

void testTrsMatchesGlmComposition() {
    const Transform t = sample();
    const glm::mat4 expected =
        glm::translate(glm::mat4(1.0f), t.position) * glm::mat4_cast(t.rotation) * glm::scale(glm::mat4(1.0f), t.scale);
    assert(near(t.toMatrix(), expected));
}

void testTrsIsAffine() {
    assert(isAffine(sample().toMatrix()));
    assert(isAffine(sample().toMatrix() * Transform { .position = { 5, 6, 7 } }.toMatrix()));
    assert(!isAffine(glm::perspective(glm::radians(60.0f), 1.5f, 0.1f, 100.0f)));
}

void testRoundTrip() {
    const Transform t = sample();
    const Transform back = Transform::fromMatrix(t.toMatrix());
    assert(near(back.position, t.position));
    assert(near(back.scale, t.scale));
    assert(sameRotation(back.rotation, t.rotation));
}

// A mirrored basis is folded into scale.x; the matrix must still round-trip exactly.
void testNegativeScale() {
    Transform t = sample();
    t.scale = { 1.0f, -2.0f, 1.5f };
    const glm::mat4 m = t.toMatrix();
    const Transform back = Transform::fromMatrix(m);
    assert(back.scale.x < 0.0f);
    assert(near(back.toMatrix(), m));
}

// Shear can't be represented: translation survives, the rotation stays orthonormal.
void testShearIsDropped() {
    glm::mat4 m = sample().toMatrix();
    m[1] += m[0] * 0.5f;  // shear Y along X
    const Transform back = Transform::fromMatrix(m);
    assert(finite(back));
    assert(near(back.position, glm::vec3(m[3])));
    assert(near(glm::length(back.rotation), 1.0f));
    assert(isAffine(back.toMatrix()));
}

void testZeroScaleIsFinite() {
    Transform t = sample();
    t.scale = { 0.0f, 1.0f, 0.0f };
    const Transform back = Transform::fromMatrix(t.toMatrix());
    assert(finite(back));
    assert(near(back.scale, glm::vec3(0.0f, 1.0f, 0.0f)));
    assert(near(back.position, t.position));
}

// The sandbox character: glm::scale(0.01) then glm::rotate(90°, Y).
void testSandboxCharacterDecomposes() {
    glm::mat4 m = glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
    m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const Transform t = Transform::fromMatrix(m);
    assert(near(t.scale, glm::vec3(0.01f)));
    assert(sameRotation(t.rotation, glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f))));
    assert(near(t.toMatrix(), m));
}

bool near(const Transform& a, const Transform& b) {
    return near(a.position, b.position) && near(a.scale, b.scale) && sameRotation(a.rotation, b.rotation);
}

bool orthogonalBasis(const glm::mat4& m) {
    const glm::vec3 x(m[0]), y(m[1]), z(m[2]);
    return near(glm::dot(x, y), 0.0f) && near(glm::dot(y, z), 0.0f) && near(glm::dot(z, x), 0.0f);
}

const Transform kRotatedChild {
    .position = { 0.5f, 1.0f, -2.0f },
    .rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
    .scale = { 1.0f, 3.0f, 0.5f },
};

void testComposeMatchesMatricesForUniformScale() {
    Transform parent = sample();
    parent.scale = glm::vec3(2.5f);
    const Transform world = compose(parent, kRotatedChild);
    assert(near(world.toMatrix(), parent.toMatrix() * kRotatedChild.toMatrix()));
}

// Under a non-uniform parent the matrix product shears a rotated child; compose doesn't.
void testComposeHasNoShear() {
    const Transform parent = sample();
    assert(!orthogonalBasis(parent.toMatrix() * kRotatedChild.toMatrix()));
    const Transform world = compose(parent, kRotatedChild);
    assert(orthogonalBasis(world.toMatrix()));
    assert(isAffine(world.toMatrix()));
}

// Position is scaled along the parent's axes, scale multiplies per component on the child's own axes.
void testComposeScalesPerComponent() {
    const Transform parent { .position = { 10, 0, 0 }, .scale = { 2, 1, 1 } };
    const Transform child {
        .position = { 1, 1, 0 },
        .rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1)),
        .scale = { 1, 4, 1 },
    };
    const Transform world = compose(parent, child);
    assert(near(world.position, glm::vec3(12, 1, 0)));
    assert(near(world.scale, glm::vec3(2, 4, 1)));
    assert(sameRotation(world.rotation, child.rotation));
}

void testRelativeToInvertsCompose() {
    const Transform parent = sample();
    assert(near(relativeTo(parent, compose(parent, kRotatedChild)), kRotatedChild));
    assert(near(compose(parent, relativeTo(parent, kRotatedChild)), kRotatedChild));

    Transform mirrored = parent;
    mirrored.scale = { -1.0f, 2.0f, 0.5f };
    assert(near(relativeTo(mirrored, compose(mirrored, kRotatedChild)), kRotatedChild));
}

// Documents why the hierarchy must compose top-down.
void testComposeIsNotAssociative() {
    const Transform a = sample();
    const Transform b = kRotatedChild;
    const Transform c { .position = { 1, 2, 3 } };
    assert(!near(compose(compose(a, b), c).position, compose(a, compose(b, c)).position));
}

void testRelativeToZeroScaleUsesFallback() {
    Transform parent = sample();
    parent.scale = { 0.0f, 1.0f, 2.0f };
    const Transform fallback { .position = { 7, 7, 7 }, .scale = { 3, 3, 3 } };
    const Transform local = relativeTo(parent, compose(parent, kRotatedChild), fallback);
    assert(finite(local));
    assert(near(local.position.x, 7.0f) && near(local.scale.x, 3.0f));
    assert(near(local.position.y, kRotatedChild.position.y) && near(local.scale.y, kRotatedChild.scale.y));
    assert(near(local.position.z, kRotatedChild.position.z) && near(local.scale.z, kRotatedChild.scale.z));
}

// -----------------------------------------------------------------------------------------------------------------
// Hierarchy
// -----------------------------------------------------------------------------------------------------------------

struct Tree {
    entt::registry reg;

    entt::entity node(const Transform& local = {}, entt::entity parent = entt::null) {
        const auto e = reg.create();
        reg.emplace<TransformComponent>(e, local);
        if (parent != entt::null) reparent(e, parent, hierarchy::Keep::Local);
        return e;
    }

    void reparent(entt::entity child, entt::entity parent, hierarchy::Keep keep = hierarchy::Keep::World) {
        [[maybe_unused]] const bool linked = hierarchy::setParent(reg, child, parent, keep);
        assert(linked);
    }

    const TransformComponent& operator[](entt::entity e) const { return reg.get<TransformComponent>(e); }

    std::vector<entt::entity> children(entt::entity e) const {
        std::vector<entt::entity> out;
        hierarchy::forEachChild(reg, e, [&](entt::entity c) { out.push_back(c); });
        return out;
    }

    // Links are consistent and every world == compose(parent.world, local).
    void check() const {
        for (auto [e, tc] : reg.view<const TransformComponent>().each()) {
            if (tc.prevSibling() != entt::null) {
                assert((*this)[tc.prevSibling()].nextSibling() == e);
            } else if (tc.parent() != entt::null) {
                assert((*this)[tc.parent()].firstChild() == e);
            }
            if (tc.nextSibling() != entt::null) assert((*this)[tc.nextSibling()].prevSibling() == e);
            hierarchy::forEachChild(reg, e, [&](entt::entity c) { assert((*this)[c].parent() == e); });

            if (tc.parent() == entt::null) {
                assert(near(tc.world(), tc.local()));
            } else {
                assert(near(tc.world(), compose((*this)[tc.parent()].world(), tc.local())));
            }
        }
    }
};

void testSetLocalPropagatesToGrandchildren() {
    Tree t;
    const auto root = t.node();
    const auto child = t.node(kRotatedChild, root);
    const auto grandchild = t.node({ .position = { 0, 1, 0 } }, child);

    hierarchy::setLocal(t.reg, root, sample());
    t.check();
    assert(near(t[grandchild].world(), compose(compose(sample(), kRotatedChild), { .position = { 0, 1, 0 } })));
}

void testSetWorldKeepsChildrenLocal() {
    Tree t;
    const auto root = t.node(sample());
    const auto middle = t.node(kRotatedChild, root);
    const auto leaf = t.node({ .position = { 1, 2, 3 } }, middle);

    const Transform target { .position = { -4, 5, 6 }, .rotation = glm::angleAxis(0.3f, glm::vec3(1, 0, 0)) };
    hierarchy::setWorld(t.reg, middle, target);
    t.check();
    assert(near(t[middle].world(), target));
    assert(near(t[middle].local(), relativeTo(sample(), target)));
    assert(near(t[leaf].local(), Transform { .position = { 1, 2, 3 } }));
    assert(near(t[leaf].world(), compose(target, t[leaf].local())));
}

void testSetParentKeepWorldOrLocal() {
    Tree t;
    const auto a = t.node(sample());
    const auto b = t.node({ .position = { 3, 0, 0 }, .scale = { 1, 2, 1 } });
    const auto node = t.node(kRotatedChild, a);
    const auto leaf = t.node({ .position = { 0, 0, 1 } }, node);

    const Transform nodeWorld = t[node].world();
    const Transform leafWorld = t[leaf].world();
    t.reparent(node, b, hierarchy::Keep::World);
    t.check();
    assert(near(t[node].world(), nodeWorld));
    assert(near(t[leaf].world(), leafWorld));
    assert(t.children(a).empty() && t.children(b) == std::vector({ node }));

    t.reparent(node, a, hierarchy::Keep::Local);
    t.check();
    assert(near(t[node].world(), compose(sample(), t[node].local())));

    t.reparent(node, entt::null);
    t.check();
    assert(t[node].parent() == entt::null);
    assert(near(t[node].local(), t[node].world()));
}

void testSetParentRejectsCycles() {
    Tree t;
    const auto root = t.node();
    const auto child = t.node(kRotatedChild, root);
    const auto grandchild = t.node({}, child);

    assert(!hierarchy::setParent(t.reg, root, root));
    assert(!hierarchy::setParent(t.reg, root, grandchild));
    assert(!hierarchy::setParent(t.reg, child, grandchild));
    t.check();
    assert(t[root].parent() == entt::null && t[child].parent() == root && t[grandchild].parent() == child);
}

void testChildrenKeepAppendOrder() {
    Tree t;
    const auto root = t.node();
    const auto c0 = t.node({}, root);
    const auto c1 = t.node({}, root);
    const auto c2 = t.node({}, root);
    assert(t.children(root) == std::vector({ c0, c1, c2 }));

    t.reparent(c1, entt::null);
    assert(t.children(root) == std::vector({ c0, c2 }));
    t.reparent(c0, entt::null);
    assert(t.children(root) == std::vector({ c2 }));
    t.reparent(c1, root);
    assert(t.children(root) == std::vector({ c2, c1 }));
    t.check();
}

void testForEachDescendantIsPreOrder() {
    Tree t;
    const auto root = t.node();
    const auto a = t.node({}, root);
    const auto a0 = t.node({}, a);
    const auto a1 = t.node({}, a);
    const auto a1x = t.node({}, a1);
    const auto b = t.node({}, root);

    std::vector<entt::entity> visited;
    hierarchy::forEachDescendant(t.reg, root, [&](entt::entity e) { visited.push_back(e); });
    assert(visited == std::vector({ a, a0, a1, a1x, b }));

    visited.clear();
    hierarchy::forEachDescendant(t.reg, a1, [&](entt::entity e) { visited.push_back(e); });
    assert(visited == std::vector({ a1x }));
}

void testDestroyOrphansChildren() {
    Tree t;
    const auto root = t.node(sample());
    const auto c0 = t.node({}, root);
    const auto middle = t.node(kRotatedChild, root);
    const auto c2 = t.node({}, root);
    const auto leaf = t.node({ .position = { 1, 0, 0 } }, middle);
    const Transform leafWorld = t[leaf].world();

    t.reg.destroy(middle);
    t.check();
    assert(t.children(root) == std::vector({ c0, c2 }));
    assert(t[leaf].parent() == entt::null);
    assert(near(t[leaf].world(), leafWorld));

    t.reg.remove<TransformComponent>(c0);
    t.check();
    assert(t.children(root) == std::vector({ c2 }));
}

void testDestroySubtree() {
    Tree t;
    const auto root = t.node();
    const auto keep = t.node({}, root);
    const auto doomed = t.node({}, root);
    const auto doomedChild = t.node({}, doomed);
    t.node({}, doomedChild);

    hierarchy::destroySubtree(t.reg, doomed);
    t.check();
    assert(!t.reg.valid(doomed) && !t.reg.valid(doomedChild));
    assert(t.children(root) == std::vector({ keep }));
    assert(t.reg.storage<TransformComponent>().size() == 2);
}

// Deep enough that a recursive propagation would overflow the stack.
void testDeepChainAndClear() {
    Tree t;
    constexpr int kDepth = 100000;
    const auto root = t.node();
    auto last = root;
    for (int i = 0; i < kDepth; i++) last = t.node({ .position = { 0, 1, 0 } }, last);

    hierarchy::setLocal(t.reg, root, { .position = { 5, 0, 0 } });
    assert(near(t[last].world().position, glm::vec3(5, kDepth, 0)));

    t.reg.clear();
    assert(t.reg.storage<TransformComponent>().empty());
}

// -----------------------------------------------------------------------------------------------------------------
// EnTT behaviour pins (EnTT 3.16, default swap-and-pop storage). The transform hierarchy relies on each of these.
//
// - Iteration runs over the packed array from back to front: iteration position k is packed index size-1-k.
//   Code must think in iteration order ("slots") and never in packed indices.
// - A newly emplaced element is appended to the packed array, so it iterates FIRST. Adding a child therefore
//   breaks parent-before-child order just like a removal does: every structural change needs a re-sort.
// - Removal swaps the packed back (the first-iterated element) into the hole.
// - registry.sort<T>(compare, entt::insertion_sort{}) orders iteration by `compare`, and is stable.
// - registry.sort<To, From>() gives To the same iteration order as From, moving components with their entities.
// - Storage iterators are random access: storage.begin()[k] is the component at iteration position k.
// - on_update fires on patch/replace/emplace_or_replace only, never on writes through get<T>() references.
// - on_destroy fires while the component still exists. registry.clear() fires every on_destroy before it removes
//   any element of that pool.
// -----------------------------------------------------------------------------------------------------------------

struct Depth { int value = 0; };
struct Payload { entt::entity owner = entt::null; };

std::vector<entt::entity> iterationOrder(const entt::sparse_set& set) {
    return { set.begin(), set.end() };
}

std::vector<int> iteratedDepths(entt::registry& reg) {
    std::vector<int> depths;
    for (const auto& depth : reg.storage<Depth>()) {
        depths.push_back(depth.value);
    }
    return depths;
}

std::vector<entt::entity> createWithDepths(entt::registry& reg, std::initializer_list<int> depths) {
    std::vector<entt::entity> created;
    for (int depth : depths) {
        created.push_back(reg.create());
        reg.emplace<Depth>(created.back(), depth);
    }
    return created;
}

void sortByDepth(entt::registry& reg) {
    reg.sort<Depth>([](const Depth& a, const Depth& b) { return a.value < b.value; }, entt::insertion_sort {});
}

void testIterationIsReversePacked() {
    entt::registry reg;
    const auto created = createWithDepths(reg, { 0, 1, 2, 3, 4 });
    const entt::sparse_set& set = reg.storage<Depth>();
    const size_t n = created.size();

    const auto order = iterationOrder(set);
    for (size_t k = 0; k < n; k++) {
        assert(set.index(created[k]) == k);         // packed order is insertion order
        assert(order[k] == created[n - 1 - k]);     // iteration is the reverse of it
    }
    assert(order.front() == created.back());        // the newest element iterates first
}

void testStorageIteratorIsRandomAccess() {
    entt::registry reg;
    createWithDepths(reg, { 3, 1, 4, 1, 5, 9, 2, 6 });
    auto& storage = reg.storage<Depth>();
    const entt::sparse_set& set = storage;
    const auto n = static_cast<std::ptrdiff_t>(storage.size());

    auto components = storage.begin();
    auto entities = set.begin();
    for (std::ptrdiff_t k = 0; k < n; k++) {
        assert(&components[k] == &storage.get(entities[k]));
        assert(&*(components + k) == &components[k]);
        assert(set.index(entities[k]) == static_cast<size_t>(n - 1 - k));
    }
}

void testSortByDepthIsStable() {
    entt::registry reg;
    createWithDepths(reg, { 2, 0, 1, 0, 2, 1, 0 });

    auto expected = iterationOrder(reg.storage<Depth>());
    std::ranges::stable_sort(expected, {}, [&](entt::entity e) { return reg.get<Depth>(e).value; });

    sortByDepth(reg);
    assert(std::ranges::is_sorted(iteratedDepths(reg)));
    assert(iterationOrder(reg.storage<Depth>()) == expected);
}

void testNewElementBreaksOrder() {
    entt::registry reg;
    const auto created = createWithDepths(reg, { 0, 0, 1 });
    sortByDepth(reg);

    // A child added below the first root: appended to the packed array, so it iterates before every parent.
    const auto child = reg.create();
    reg.emplace<Depth>(child, 1);
    assert(iterationOrder(reg.storage<Depth>()).front() == child);
    assert(!std::ranges::is_sorted(iteratedDepths(reg)));

    sortByDepth(reg);
    assert(std::ranges::is_sorted(iteratedDepths(reg)));
}

void testRemovalBreaksOrder() {
    entt::registry reg;
    createWithDepths(reg, { 2, 1, 0, 2, 1, 0 });
    sortByDepth(reg);
    assert(iteratedDepths(reg) == std::vector<int>({ 0, 0, 1, 1, 2, 2 }));

    // Removing the last-iterated element swaps the first-iterated root into its place.
    reg.erase<Depth>(iterationOrder(reg.storage<Depth>()).back());
    assert(iteratedDepths(reg) == std::vector<int>({ 0, 1, 1, 2, 0 }));
}

void testSortAsFollowsAnotherPool() {
    entt::registry reg;
    const auto created = createWithDepths(reg, { 2, 0, 1, 0, 2, 1 });
    // Emplace Payload in a different packed order than Depth.
    for (auto it = created.rbegin(); it != created.rend(); ++it) {
        reg.emplace<Payload>(*it, *it);
    }

    sortByDepth(reg);
    reg.sort<Payload, Depth>();

    const auto order = iterationOrder(reg.storage<Depth>());
    assert(iterationOrder(reg.storage<Payload>()) == order);

    auto payloads = reg.storage<Payload>().begin();
    for (size_t k = 0; k < order.size(); k++) {
        assert(payloads[static_cast<std::ptrdiff_t>(k)].owner == order[k]);   // components moved with entities
    }
}

struct UpdateCounter {
    int calls = 0;
    void onUpdate(entt::registry&, entt::entity) { calls++; }
};

void testOnUpdateFiresOnlyOnPatchAndReplace() {
    entt::registry reg;
    UpdateCounter counter;
    reg.on_update<Depth>().connect<&UpdateCounter::onUpdate>(counter);

    const auto e = reg.create();
    reg.emplace<Depth>(e, 1);
    assert(counter.calls == 0);

    reg.get<Depth>(e).value = 2;                     // raw writes are invisible to signals
    assert(counter.calls == 0);

    reg.patch<Depth>(e, [](Depth& d) { d.value = 3; });
    assert(counter.calls == 1);
    reg.replace<Depth>(e, 4);
    assert(counter.calls == 2);
    reg.emplace_or_replace<Depth>(e, 5);
    assert(counter.calls == 3);

    reg.on_update<Depth>().disconnect(&counter);
    reg.patch<Depth>(e);
    assert(counter.calls == 3);
}

struct DestroyProbe {
    int calls = 0;
    bool componentsAlive = true;
    std::vector<entt::entity> all;

    void onDestroy(entt::registry& reg, entt::entity e) {
        calls++;
        componentsAlive = componentsAlive && reg.all_of<Depth>(e);
        for (auto other : all) {
            componentsAlive = componentsAlive && reg.all_of<Depth>(other);
        }
    }
};

void testOnDestroyRunsBeforeRemoval() {
    entt::registry reg;
    DestroyProbe probe;
    probe.all = createWithDepths(reg, { 0, 1, 2 });
    reg.on_destroy<Depth>().connect<&DestroyProbe::onDestroy>(probe);

    // destroy(): the destroyed entity still has its component inside the handler.
    const auto extra = reg.create();
    reg.emplace<Depth>(extra, 0);
    reg.destroy(extra);
    assert(probe.calls == 1);
    assert(probe.componentsAlive);

    // clear(): every handler runs while the whole pool is still intact.
    reg.clear();
    assert(probe.calls == 4);
    assert(probe.componentsAlive);
    assert(reg.storage<Depth>().empty());
}

}

int main() {
    testDefaultIsIdentity();
    testTrsMatchesGlmComposition();
    testTrsIsAffine();
    testRoundTrip();
    testNegativeScale();
    testShearIsDropped();
    testZeroScaleIsFinite();
    testSandboxCharacterDecomposes();
    testComposeMatchesMatricesForUniformScale();
    testComposeHasNoShear();
    testComposeScalesPerComponent();
    testRelativeToInvertsCompose();
    testComposeIsNotAssociative();
    testRelativeToZeroScaleUsesFallback();

    testSetLocalPropagatesToGrandchildren();
    testSetWorldKeepsChildrenLocal();
    testSetParentKeepWorldOrLocal();
    testSetParentRejectsCycles();
    testChildrenKeepAppendOrder();
    testForEachDescendantIsPreOrder();
    testDestroyOrphansChildren();
    testDestroySubtree();
    testDeepChainAndClear();

    testIterationIsReversePacked();
    testStorageIteratorIsRandomAccess();
    testSortByDepthIsStable();
    testNewElementBreaksOrder();
    testRemovalBreaksOrder();
    testSortAsFollowsAnotherPool();
    testOnUpdateFiresOnlyOnPatchAndReplace();
    testOnDestroyRunsBeforeRemoval();

    std::cout << "All transform tests passed" << std::endl;
    return 0;
}
