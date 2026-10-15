#pragma once

#include <spectra/core/node.hpp>
#include <spectra/core/types.hpp>

#include <memory>
#include <string>
#include <vector>

namespace spectra {

struct ProjectSettings {
    std::string name{"Untitled"};
    int width{1920};
    int height{1080};
    double frameRate{60.0};
    Seconds duration{60.0};
    std::string colorSpace{"linear-srgb"};
};

/// How the audio engine should analyse the track. Stored in the project so a
/// scene looks the same on someone else's machine.
struct AnalysisSettings {
    int bandCount{256};
    std::string bandScale{"mel"};
    float attack{0.08f};
    float decay{0.35f};
    std::string normalization{"track"};

    /// Spectral tilt compensation: `flat`, `pink` or `perceptual`. Music has
    /// roughly 1/f energy, so without this the bass dominates everything —
    /// the root of Avee's bass behaviour. See docs/04-AUDIO-ENGINE.md.
    std::string tilt{"perceptual"};
};

struct BeatGrid {
    double bpm{0.0};     ///< Zero means not yet estimated.
    double offset{0.0};  ///< Seconds from the start to the first beat.
};

struct AudioSettings {
    std::string source;  ///< Relative path from the project file.
    AnalysisSettings analysis;
    BeatGrid beatGrid;
};

/// A whole project: settings, audio, and the compositions.
class Scene {
public:
    Scene();
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    ProjectSettings project;
    AudioSettings audio;

    [[nodiscard]] const std::vector<std::unique_ptr<Node>>& compositions() const {
        return compositions_;
    }

    Node* addComposition(std::unique_ptr<Node> composition);
    std::unique_ptr<Node> removeComposition(std::size_t index);
    Node* insertComposition(std::size_t index, std::unique_ptr<Node> composition);

    /// Depth-first search across every composition.
    [[nodiscard]] Node* find(std::string_view id);
    [[nodiscard]] const Node* find(std::string_view id) const;

    /// An id not currently in use, derived from `prefix`.
    [[nodiscard]] std::string makeUniqueId(std::string_view prefix) const;

    /// Top-level keys from a newer build, preserved across a round trip.
    std::shared_ptr<JsonExtras> extras;

private:
    std::vector<std::unique_ptr<Node>> compositions_;
};

}  // namespace spectra
