#pragma once

#include <nlohmann/json.hpp>

namespace spectra {

/// Keys from a project file that this build does not understand.
///
/// Kept verbatim and written back out on save. Someone opening a newer
/// project in an older build, saving, and sending it back must not silently
/// destroy the parts their version had no concept of — and that is nearly
/// impossible to retrofit once files exist in the wild, so it is here from the
/// first version. See docs/06-PROJECT-FORMAT.md.
///
/// Avee does the opposite: `ElementsFactory.getTypeName` writes `"unk"` for an
/// element it cannot identify, destroying it.
class JsonExtras {
public:
    JsonExtras() = default;

    explicit JsonExtras(nlohmann::json value) : data(std::move(value)) {}

    nlohmann::json data = nlohmann::json::object();
};

}  // namespace spectra
