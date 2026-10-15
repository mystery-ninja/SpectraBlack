#pragma once

#include <spectra/core/node.hpp>
#include <spectra/core/property.hpp>
#include <spectra/core/registry.hpp>
#include <spectra/core/scene.hpp>
#include <spectra/core/types.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace spectra {

/// One undoable edit.
///
/// Inverse operations rather than scene snapshots. Snapshots are simpler to
/// write, but then undo memory scales with scene size, and a 500-step history
/// of a large project gets expensive fast. See docs/06-PROJECT-FORMAT.md.
class Command {
public:
    virtual ~Command() = default;

    Command() = default;
    Command(const Command&) = delete;
    Command& operator=(const Command&) = delete;
    Command(Command&&) = delete;
    Command& operator=(Command&&) = delete;

    virtual void apply(Scene& scene) = 0;
    virtual void revert(Scene& scene) = 0;

    /// Identity used for coalescing. Empty means this command never merges.
    ///
    /// Two commands may merge only when their keys are equal. Every key
    /// carries a per-command-type prefix, so equality also establishes that
    /// the other command has the same dynamic type — which is how merging
    /// works without RTTI, since `dynamic_cast` is unavailable to us by house
    /// rule (docs/02-TECH-STACK.md).
    [[nodiscard]] virtual std::string mergeKey() const { return {}; }

    /// Absorb a later command with a matching `mergeKey`.
    ///
    /// This is what stops one slider drag from becoming 200 undo steps. Only
    /// called when the keys match, so implementations may assume the type.
    virtual bool mergeWith(const Command& next) {
        (void)next;
        return false;
    }

    /// Shown in the undo menu.
    [[nodiscard]] virtual std::string label() const = 0;
};

/// Undo/redo history.
class CommandStack {
public:
    /// Run a command and push it. If it merges into the previous one, no new
    /// entry appears. Pushing always clears the redo branch.
    void push(Scene& scene, std::unique_ptr<Command> command);

    bool undo(Scene& scene);
    bool redo(Scene& scene);

    [[nodiscard]] bool canUndo() const { return !undo_.empty(); }

    [[nodiscard]] bool canRedo() const { return !redo_.empty(); }

    [[nodiscard]] std::string undoLabel() const;
    [[nodiscard]] std::string redoLabel() const;

    [[nodiscard]] std::size_t undoDepth() const { return undo_.size(); }

    [[nodiscard]] std::size_t redoDepth() const { return redo_.size(); }

    void clear();

    /// Stop the next `push` from merging into the current top. Call it when a
    /// drag ends, so the next drag starts a fresh undo step.
    void breakMerge() { canMerge_ = false; }

    void setLimit(std::size_t limit);

    [[nodiscard]] std::size_t limit() const { return limit_; }

private:
    void trim();

    std::vector<std::unique_ptr<Command>> undo_;
    std::vector<std::unique_ptr<Command>> redo_;
    bool canMerge_{false};
    std::size_t limit_{500};
};

/// Resolve a node's property by name. Null if the node, the property, or the
/// type does not match.
///
/// Commands look properties up through this on every apply rather than holding
/// a pointer, so that deleting a node and undoing the delete does not leave
/// earlier commands pointing at freed memory.
template<typename T>
[[nodiscard]] Property<T>*
resolveProperty(Scene& scene, std::string_view nodeId, std::string_view propertyName) {
    Node* const node = scene.find(nodeId);
    if (node == nullptr) {
        return nullptr;
    }
    const PropertyRegistry registry = node->describeProperties();
    const PropertyDesc* const desc = registry.find(propertyName);
    return desc != nullptr ? propertyCast<T>(*desc) : nullptr;
}

/// Change a property's static value — typing in a field, or dragging a slider.
///
/// Consecutive changes to the same property merge into one undo step, keeping
/// the value from before the drag began.
template<typename T>
class SetPropertyCommand final : public Command {
public:
    SetPropertyCommand(std::string nodeId, std::string propertyName, T newValue)
        : nodeId_(std::move(nodeId)), propertyName_(std::move(propertyName)),
          newValue_(std::move(newValue)) {}

    void apply(Scene& scene) override {
        Property<T>* const property = resolveProperty<T>(scene, nodeId_, propertyName_);
        if (property == nullptr) {
            return;
        }
        if (!captured_) {
            oldValue_ = property->staticValue;
            captured_ = true;
        }
        property->staticValue = newValue_;
    }

    void revert(Scene& scene) override {
        Property<T>* const property = resolveProperty<T>(scene, nodeId_, propertyName_);
        if (property != nullptr && captured_) {
            property->staticValue = oldValue_;
        }
    }

    [[nodiscard]] std::string mergeKey() const override {
        return "setProperty:" + nodeId_ + "/" + propertyName_;
    }

    bool mergeWith(const Command& next) override {
        // Guaranteed by the stack: keys matched, so this is the same type
        // editing the same property.
        newValue_ = static_cast<const SetPropertyCommand<T>&>(next).newValue_;
        return true;
    }

    [[nodiscard]] std::string label() const override { return "Change " + propertyName_; }

private:
    std::string nodeId_;
    std::string propertyName_;
    T oldValue_{};
    T newValue_{};
    bool captured_{false};
};

/// Add a node under a parent, or as a top-level composition when `parentId`
/// is empty.
class AddNodeCommand final : public Command {
public:
    AddNodeCommand(std::string parentId, std::unique_ptr<Node> node);

    void apply(Scene& scene) override;
    void revert(Scene& scene) override;
    [[nodiscard]] std::string label() const override;

private:
    std::string parentId_;
    std::string nodeId_;
    std::string nodeName_;
    std::size_t index_{0};
    std::unique_ptr<Node> held_;  ///< Owns the node while the command is undone.
};

/// Remove a node, keeping it alive in the history so redo restores it exactly.
class RemoveNodeCommand final : public Command {
public:
    explicit RemoveNodeCommand(std::string nodeId);

    void apply(Scene& scene) override;
    void revert(Scene& scene) override;
    [[nodiscard]] std::string label() const override;

private:
    std::string nodeId_;
    std::string parentId_;
    std::size_t index_{0};
    std::unique_ptr<Node> held_;
};

/// Rename a node. Consecutive renames of the same node merge, so typing a name
/// one character at a time is still one undo step.
class RenameNodeCommand final : public Command {
public:
    RenameNodeCommand(std::string nodeId, std::string newName);

    void apply(Scene& scene) override;
    void revert(Scene& scene) override;
    bool mergeWith(const Command& next) override;

    [[nodiscard]] std::string mergeKey() const override { return "renameNode:" + nodeId_; }

    [[nodiscard]] std::string label() const override { return "Rename"; }

private:
    std::string nodeId_;
    std::string oldName_;
    std::string newName_;
    bool captured_{false};
};

}  // namespace spectra
