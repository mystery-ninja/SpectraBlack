#pragma once

#include <string>
#include <utility>
#include <variant>

namespace spectra {

/// A value or an explanation of why there isn't one.
///
/// `core/` does not throw (docs/02-TECH-STACK.md), and `std::expected` is C++23
/// while we target C++20, so this stands in. Deliberately minimal: loading a
/// project either works or tells you what was wrong with the file.
template<typename T>
class Result {
public:
    static Result ok(T value) { return Result(std::move(value)); }

    static Result error(std::string message) { return Result(Failure{std::move(message)}); }

    [[nodiscard]] bool hasValue() const { return std::holds_alternative<T>(state_); }

    explicit operator bool() const { return hasValue(); }

    [[nodiscard]] T& value() { return std::get<T>(state_); }

    [[nodiscard]] const T& value() const { return std::get<T>(state_); }

    /// Empty when the result holds a value.
    [[nodiscard]] const std::string& error() const {
        static const std::string kNone;
        const auto* failure = std::get_if<Failure>(&state_);
        return failure != nullptr ? failure->message : kNone;
    }

private:
    struct Failure {
        std::string message;
    };

    explicit Result(T value) : state_(std::move(value)) {}

    explicit Result(Failure failure) : state_(std::move(failure)) {}

    std::variant<T, Failure> state_;
};

}  // namespace spectra
