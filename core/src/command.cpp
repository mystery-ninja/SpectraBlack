#include <spectra/core/command.hpp>

#include <algorithm>
#include <utility>

namespace spectra {
namespace {

/// Find a node's parent and its index within it. `parent` comes back null for
/// a top-level composition, which is still a successful result.
struct Location {
    bool found{false};
    Node* parent{nullptr};
    std::size_t index{0};
};

Location locate(Scene& scene, std::string_view nodeId) {
    const auto& compositions = scene.compositions();
    for (std::size_t i = 0; i < compositions.size(); ++i) {
        if (compositions[i]->id() == nodeId) {
            return Location{.found = true, .parent = nullptr, .index = i};
        }
    }

    struct Search {
        static Location in(Node& parent, std::string_view id) {
            const auto& children = parent.children();
            for (std::size_t i = 0; i < children.size(); ++i) {
                if (children[i]->id() == id) {
                    return Location{.found = true, .parent = &parent, .index = i};
                }
                if (Location nested = in(*children[i], id); nested.found) {
                    return nested;
                }
            }
            return Location{};
        }
    };

    for (const auto& composition : compositions) {
        if (Location nested = Search::in(*composition, nodeId); nested.found) {
            return nested;
        }
    }
    return Location{};
}

}  // namespace

// ------------------------------------------------------------- CommandStack

void CommandStack::push(Scene& scene, std::unique_ptr<Command> command) {
    if (command == nullptr) {
        return;
    }

    command->apply(scene);

    if (canMerge_ && !undo_.empty()) {
        Command& top = *undo_.back();
        const std::string key = top.mergeKey();
        if (!key.empty() && key == command->mergeKey() && top.mergeWith(*command)) {
            redo_.clear();
            return;
        }
    }

    undo_.push_back(std::move(command));
    redo_.clear();
    canMerge_ = true;
    trim();
}

bool CommandStack::undo(Scene& scene) {
    if (undo_.empty()) {
        return false;
    }
    auto command = std::move(undo_.back());
    undo_.pop_back();
    command->revert(scene);
    redo_.push_back(std::move(command));
    canMerge_ = false;
    return true;
}

bool CommandStack::redo(Scene& scene) {
    if (redo_.empty()) {
        return false;
    }
    auto command = std::move(redo_.back());
    redo_.pop_back();
    command->apply(scene);
    undo_.push_back(std::move(command));
    canMerge_ = false;
    return true;
}

std::string CommandStack::undoLabel() const {
    return undo_.empty() ? std::string() : undo_.back()->label();
}

std::string CommandStack::redoLabel() const {
    return redo_.empty() ? std::string() : redo_.back()->label();
}

void CommandStack::clear() {
    undo_.clear();
    redo_.clear();
    canMerge_ = false;
}

void CommandStack::setLimit(std::size_t limit) {
    limit_ = std::max<std::size_t>(limit, 1);
    trim();
}

void CommandStack::trim() {
    if (undo_.size() <= limit_) {
        return;
    }
    const std::size_t excess = undo_.size() - limit_;
    undo_.erase(undo_.begin(), undo_.begin() + static_cast<std::ptrdiff_t>(excess));
}

// ------------------------------------------------------------ AddNodeCommand

AddNodeCommand::AddNodeCommand(std::string parentId, std::unique_ptr<Node> node)
    : parentId_(std::move(parentId)), held_(std::move(node)) {
    if (held_ != nullptr) {
        nodeId_ = held_->id();
        nodeName_ = held_->name;
    }
}

void AddNodeCommand::apply(Scene& scene) {
    if (held_ == nullptr) {
        return;
    }
    if (parentId_.empty()) {
        index_ = scene.compositions().size();
        scene.addComposition(std::move(held_));
        return;
    }
    Node* const parent = scene.find(parentId_);
    if (parent == nullptr || !parent->acceptsChildren()) {
        return;  // Nothing added, and we still own the node.
    }
    index_ = parent->children().size();
    parent->addChild(std::move(held_));
}

void AddNodeCommand::revert(Scene& scene) {
    const Location at = locate(scene, nodeId_);
    if (!at.found) {
        return;
    }
    held_ =
        at.parent == nullptr ? scene.removeComposition(at.index) : at.parent->removeChild(at.index);
}

std::string AddNodeCommand::label() const {
    return nodeName_.empty() ? "Add node" : "Add " + nodeName_;
}

// --------------------------------------------------------- RemoveNodeCommand

RemoveNodeCommand::RemoveNodeCommand(std::string nodeId) : nodeId_(std::move(nodeId)) {}

void RemoveNodeCommand::apply(Scene& scene) {
    const Location at = locate(scene, nodeId_);
    if (!at.found) {
        return;
    }
    index_ = at.index;
    parentId_ = at.parent != nullptr ? at.parent->id() : std::string();
    held_ =
        at.parent == nullptr ? scene.removeComposition(at.index) : at.parent->removeChild(at.index);
}

void RemoveNodeCommand::revert(Scene& scene) {
    if (held_ == nullptr) {
        return;
    }
    if (parentId_.empty()) {
        scene.insertComposition(index_, std::move(held_));
        return;
    }
    Node* const parent = scene.find(parentId_);
    if (parent != nullptr) {
        parent->insertChild(index_, std::move(held_));
    }
}

std::string RemoveNodeCommand::label() const {
    return held_ != nullptr && !held_->name.empty() ? "Delete " + held_->name : "Delete node";
}

// --------------------------------------------------------- RenameNodeCommand

RenameNodeCommand::RenameNodeCommand(std::string nodeId, std::string newName)
    : nodeId_(std::move(nodeId)), newName_(std::move(newName)) {}

void RenameNodeCommand::apply(Scene& scene) {
    Node* const node = scene.find(nodeId_);
    if (node == nullptr) {
        return;
    }
    if (!captured_) {
        oldName_ = node->name;
        captured_ = true;
    }
    node->name = newName_;
}

void RenameNodeCommand::revert(Scene& scene) {
    Node* const node = scene.find(nodeId_);
    if (node != nullptr && captured_) {
        node->name = oldName_;
    }
}

bool RenameNodeCommand::mergeWith(const Command& next) {
    newName_ = static_cast<const RenameNodeCommand&>(next).newName_;
    return true;
}

}  // namespace spectra
