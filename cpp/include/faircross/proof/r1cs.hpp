#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include "faircross/domain/error.hpp"

namespace faircross {

/// Coefficient and accumulator type for linear combinations.
///
/// The 128-bit width is load-bearing rather than
/// defensive: `synthesize_clearing_optimality_constraints` emits a constant
/// term of `demand * supply`, a product of two 64-bit unsigned values that overflows 64 bits
/// for realistic batches.
using Scalar = __int128;
using UScalar = unsigned __int128;

/// Explicitly wrapping addition and multiplication.
///
/// Signed overflow is undefined behaviour in C++ and this project builds with
/// `-fsanitize=undefined`, so the arithmetic is performed on the unsigned
/// counterpart, where wraparound is defined, and converted back. Conversion of
/// an out-of-range unsigned value to a signed type is modular as of C++20.
[[nodiscard]] inline constexpr Scalar wrapping_add(Scalar a, Scalar b) noexcept {
    return static_cast<Scalar>(static_cast<UScalar>(a) + static_cast<UScalar>(b));
}

[[nodiscard]] inline constexpr Scalar wrapping_mul(Scalar a, Scalar b) noexcept {
    return static_cast<Scalar>(static_cast<UScalar>(a) * static_cast<UScalar>(b));
}

/// Renders a 128-bit scalar, which the standard streams cannot format.
[[nodiscard]] inline std::string scalar_to_string(Scalar value) {
    if (value == 0) return "0";
    const bool negative = value < 0;
    // Negate through the unsigned type so the most negative value is representable.
    UScalar magnitude = negative ? (~static_cast<UScalar>(value) + 1) : static_cast<UScalar>(value);
    std::string digits;
    while (magnitude > 0) {
        digits.push_back(static_cast<char>('0' + static_cast<int>(magnitude % 10)));
        magnitude /= 10;
    }
    if (negative) digits.push_back('-');
    return std::string(digits.rbegin(), digits.rend());
}

enum class VariableKind {
    One,
    Public,
    Witness,
};

struct Variable {
    VariableKind kind;
    size_t index;

    static Variable one() { return Variable{VariableKind::One, 0}; }
    static Variable public_var(size_t idx) { return Variable{VariableKind::Public, idx}; }
    static Variable witness_var(size_t idx) { return Variable{VariableKind::Witness, idx}; }

    auto operator<=>(const Variable&) const = default;
};

struct LinearCombination {
    std::vector<std::pair<Variable, Scalar>> terms;

    static LinearCombination zero() { return LinearCombination{}; }
    static LinearCombination from_var(Variable v) {
        LinearCombination lc;
        lc.terms.emplace_back(v, Scalar{1});
        return lc;
    }
    static LinearCombination from_constant(Scalar c) {
        LinearCombination lc;
        lc.terms.emplace_back(Variable::one(), c);
        return lc;
    }

    void add_term(Variable v, Scalar coeff) {
        if (coeff != 0) {
            terms.emplace_back(v, coeff);
        }
    }

    [[nodiscard]] Scalar evaluate(const std::vector<uint64_t>& public_inputs,
                                  const std::vector<uint64_t>& witness) const {
        Scalar sum = 0;
        for (const auto& [var, coeff] : terms) {
            Scalar val = 0;
            switch (var.kind) {
                case VariableKind::One: val = 1; break;
                case VariableKind::Public:
                    val = (var.index < public_inputs.size())
                              ? static_cast<Scalar>(public_inputs[var.index])
                              : Scalar{0};
                    break;
                case VariableKind::Witness:
                    val = (var.index < witness.size())
                              ? static_cast<Scalar>(witness[var.index])
                              : Scalar{0};
                    break;
            }
            sum = wrapping_add(sum, wrapping_mul(coeff, val));
        }
        return sum;
    }
};

struct R1CSConstraint {
    LinearCombination a;
    LinearCombination b;
    LinearCombination c;
    std::string label;
};

class ConstraintSystem {
public:
    ConstraintSystem() : num_public_(0), num_witness_(0) {}

    Variable alloc_public() {
        return Variable::public_var(num_public_++);
    }

    Variable alloc_witness() {
        return Variable::witness_var(num_witness_++);
    }

    void enforce(LinearCombination a, LinearCombination b, LinearCombination c, std::string label) {
        constraints_.push_back(R1CSConstraint{std::move(a), std::move(b), std::move(c), std::move(label)});
    }

    void enforce_equal(LinearCombination lc1, const LinearCombination& lc2, std::string label) {
        for (const auto& [v, coeff] : lc2.terms) {
            lc1.add_term(v, -coeff);
        }
        enforce(std::move(lc1), LinearCombination::from_var(Variable::one()), LinearCombination::zero(), std::move(label));
    }

    Result<Ok> is_satisfied(const std::vector<uint64_t>& public_inputs, const std::vector<uint64_t>& witness) const {
        for (const auto& constraint : constraints_) {
            const Scalar val_a = constraint.a.evaluate(public_inputs, witness);
            const Scalar val_b = constraint.b.evaluate(public_inputs, witness);
            const Scalar val_c = constraint.c.evaluate(public_inputs, witness);

            if (wrapping_mul(val_a, val_b) != val_c) {
                return DomainError{
                    DomainErrorKind::OrderValidationFailed,
                    "ConstraintNotSatisfied: " + constraint.label + " (expected " +
                        scalar_to_string(val_c) + ", got " +
                        scalar_to_string(wrapping_mul(val_a, val_b)) + ")"};
            }
        }
        return ok;
    }

    [[nodiscard]] size_t num_constraints() const noexcept { return constraints_.size(); }
    [[nodiscard]] size_t num_public() const noexcept { return num_public_; }
    [[nodiscard]] size_t num_witness() const noexcept { return num_witness_; }
    [[nodiscard]] const std::vector<R1CSConstraint>& constraints() const noexcept { return constraints_; }

private:
    size_t num_public_;
    size_t num_witness_;
    std::vector<R1CSConstraint> constraints_;
};

} // namespace faircross
