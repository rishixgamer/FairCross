#include "faircross/commitments/merkle.hpp"

namespace faircross {

MerkleTree::MerkleTree(std::vector<Commitment> leaves) : leaves_(std::move(leaves)) {
    if (leaves_.empty()) {
        return;
    }

    layers_.push_back(leaves_);
    std::vector<Commitment> current_layer = leaves_;

    while (current_layer.size() > 1) {
        std::vector<Commitment> next_layer;
        next_layer.reserve((current_layer.size() + 1) / 2);

        for (size_t i = 0; i < current_layer.size(); i += 2) {
            if (i + 1 < current_layer.size()) {
                next_layer.push_back(Sha256Scheme::combine_nodes(current_layer[i], current_layer[i + 1]));
            } else {
                next_layer.push_back(Sha256Scheme::combine_nodes(current_layer[i], Sha256Scheme::empty_node()));
            }
        }
        layers_.push_back(next_layer);
        current_layer = std::move(next_layer);
    }
}

Commitment MerkleTree::root() const {
    if (leaves_.empty()) {
        return Sha256Scheme::empty_node();
    }
    return layers_.back()[0];
}

std::optional<MerkleProof> MerkleTree::generate_proof(size_t leaf_index) const {
    if (leaf_index >= leaves_.size()) {
        return std::nullopt;
    }

    std::vector<MerkleStep> path;
    size_t idx = leaf_index;

    for (size_t layer_idx = 0; layer_idx + 1 < layers_.size(); ++layer_idx) {
        const auto& layer = layers_[layer_idx];
        bool is_right_child = (idx % 2 == 1);

        if (is_right_child) {
            Commitment sibling = layer[idx - 1];
            path.push_back(MerkleStep{StepSide::Left, sibling});
        } else {
            Commitment sibling = (idx + 1 < layer.size()) ? layer[idx + 1] : Sha256Scheme::empty_node();
            path.push_back(MerkleStep{StepSide::Right, sibling});
        }
        idx /= 2;
    }

    return MerkleProof{leaf_index, std::move(path)};
}

} // namespace faircross
