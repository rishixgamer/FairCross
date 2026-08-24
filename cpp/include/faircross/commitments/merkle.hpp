#pragma once

#include <vector>
#include <optional>
#include "faircross/commitments/sha256.hpp"

namespace faircross {

enum class StepSide {
    Left,
    Right,
};

struct MerkleStep {
    StepSide side;
    Commitment sibling;

    auto operator<=>(const MerkleStep&) const = default;
};

struct MerkleProof {
    size_t leaf_index;
    std::vector<MerkleStep> path;

    bool verify(const Commitment& expected_root, const Commitment& leaf) const {
        Commitment current = leaf;
        for (const auto& step : path) {
            if (step.side == StepSide::Left) {
                current = Sha256Scheme::combine_nodes(step.sibling, current);
            } else {
                current = Sha256Scheme::combine_nodes(current, step.sibling);
            }
        }
        return current == expected_root;
    }

    auto operator<=>(const MerkleProof&) const = default;
};

class MerkleTree {
public:
    MerkleTree() = default;
    explicit MerkleTree(std::vector<Commitment> leaves);

    [[nodiscard]] Commitment root() const;
    [[nodiscard]] size_t leaf_count() const noexcept { return leaves_.size(); }
    [[nodiscard]] const std::vector<Commitment>& leaves() const noexcept { return leaves_; }

    [[nodiscard]] std::optional<MerkleProof> generate_proof(size_t leaf_index) const;

private:
    std::vector<Commitment> leaves_;
    std::vector<std::vector<Commitment>> layers_;
};

} // namespace faircross
