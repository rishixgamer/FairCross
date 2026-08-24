#pragma once

#include <string>
#include <string_view>
#include <variant>
#include <stdexcept>
#include <utility>

namespace faircross {

struct Ok {
    auto operator<=>(const Ok&) const = default;
};

inline constexpr Ok ok{};

enum class PrimitiveErrorKind {
    ZeroValue,
    ArithmeticOverflow,
    InsufficientBalance,
    InvalidSide,
};

struct PrimitiveError {
    PrimitiveErrorKind kind;
    std::string context;

    std::string to_string() const {
        switch (kind) {
            case PrimitiveErrorKind::ZeroValue:
                return "ZeroValue: " + context + " must be strictly positive";
            case PrimitiveErrorKind::ArithmeticOverflow:
                return "ArithmeticOverflow in " + context;
            case PrimitiveErrorKind::InsufficientBalance:
                return "InsufficientBalance for " + context;
            case PrimitiveErrorKind::InvalidSide:
                return "InvalidSide: " + context;
        }
        return "Unknown PrimitiveError";
    }
};

enum class DomainErrorKind {
    OrderValidationFailed,
    DuplicateAccount,
    AccountNotFound,
    InstrumentMismatch,
    DuplicateOrderId,
};

struct DomainError {
    DomainErrorKind kind;
    std::string message;

    std::string to_string() const {
        return message;
    }
};

template <typename T>
class Result {
public:
    Result(T val) : storage_(std::move(val)) {}
    Result(PrimitiveError err) : storage_(std::move(err)) {}
    Result(DomainError err) : storage_(std::move(err)) {}
    Result(std::string msg) : storage_(DomainError{DomainErrorKind::OrderValidationFailed, std::move(msg)}) {}

    template <typename U>
    Result(const Result<U>& other) {
        if (other.is_err()) {
            storage_ = DomainError{DomainErrorKind::OrderValidationFailed, other.error_message()};
        } else {
            throw std::logic_error("Cannot convert ok Result<U> to Result<T> implicitly");
        }
    }

    bool is_ok() const noexcept { return std::holds_alternative<T>(storage_); }
    bool is_err() const noexcept { return !is_ok(); }

    const T& value() const { return std::get<T>(storage_); }
    T& value() { return std::get<T>(storage_); }

    const T& unwrap() const {
        if (is_err()) {
            throw std::runtime_error("Called unwrap on Error Result: " + error_message());
        }
        return std::get<T>(storage_);
    }

    T unwrap_or(T default_val) const {
        if (is_ok()) return std::get<T>(storage_);
        return default_val;
    }

    std::string error_message() const {
        if (std::holds_alternative<PrimitiveError>(storage_)) {
            return std::get<PrimitiveError>(storage_).to_string();
        } else if (std::holds_alternative<DomainError>(storage_)) {
            return std::get<DomainError>(storage_).to_string();
        }
        return "Not an error";
    }

private:
    std::variant<T, PrimitiveError, DomainError> storage_;
};

} // namespace faircross
