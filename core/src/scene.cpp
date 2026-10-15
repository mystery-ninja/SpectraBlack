#include <spectra/core/scene.hpp>

#include <algorithm>
#include <string>

namespace spectra {

Scene::Scene() = default;
Scene::~Scene() = default;

Node* Scene::addComposition(std::unique_ptr<Node> composition) {
    return insertComposition(compositions_.size(), std::move(composition));
}

Node* Scene::insertComposition(std::size_t index, std::unique_ptr<Node> composition) {
    if (composition == nullptr) {
        return nullptr;
    }
    index = std::min(index, compositions_.size());
    Node* const bare = composition.get();
    compositions_.insert(compositions_.begin() + static_cast<std::ptrdiff_t>(index),
                         std::move(composition));
    return bare;
}

std::unique_ptr<Node> Scene::removeComposition(std::size_t index) {
    if (index >= compositions_.size()) {
        return nullptr;
    }
    auto at = compositions_.begin() + static_cast<std::ptrdiff_t>(index);
    auto taken = std::move(*at);
    compositions_.erase(at);
    return taken;
}

Node* Scene::find(std::string_view id) {
    for (const auto& composition : compositions_) {
        if (Node* const hit = composition->find(id)) {
            return hit;
        }
    }
    return nullptr;
}

const Node* Scene::find(std::string_view id) const {
    return const_cast<Scene*>(this)->find(id);
}

std::string Scene::makeUniqueId(std::string_view prefix) const {
    std::string base(prefix);
    if (base.empty()) {
        base = "node";
    }
    for (int suffix = 1;; ++suffix) {
        std::string candidate = base + "_" + std::to_string(suffix);
        if (find(candidate) == nullptr) {
            return candidate;
        }
    }
}

}  // namespace spectra
