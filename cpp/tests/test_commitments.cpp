#include "test_framework.hpp"
#include "faircross/commitments/sha256.hpp"
#include "faircross/commitments/order_commitment.hpp"
#include "faircross/commitments/merkle.hpp"
#include "faircross/commitments/batch_commitment.hpp"

using namespace faircross;

TEST_CASE(test_sha256_standard_vectors) {
    // Empty string
    std::string empty = "";
    auto hash_empty = Sha256::hash(reinterpret_cast<const uint8_t*>(empty.data()), empty.size());
    REQUIRE_EQ(hash_empty.to_hex(), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    // "abc"
    std::string abc = "abc";
    auto hash_abc = Sha256::hash(reinterpret_cast<const uint8_t*>(abc.data()), abc.size());
    REQUIRE_EQ(hash_abc.to_hex(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE(test_merkle_tree_proof_roundtrip_and_tampering) {
    const std::vector<size_t> sizes = {1, 2, 3, 4, 5, 7, 8, 15, 16};
    for (size_t n : sizes) {
        std::vector<Commitment> leaves;
        for (size_t i = 0; i < n; ++i) {
            std::array<uint8_t, 32> b{};
            b[0] = static_cast<uint8_t>(i + 1);
            leaves.push_back(Commitment::from_bytes(b));
        }

        MerkleTree tree(leaves);
        Commitment root = tree.root();

        for (size_t i = 0; i < n; ++i) {
            auto proof_opt = tree.generate_proof(i);
            REQUIRE(proof_opt.has_value());
            REQUIRE(proof_opt->verify(root, leaves[i]));

            // Mutated leaf fails
            std::array<uint8_t, 32> bad_bytes{};
            bad_bytes[0] = 0xFF;
            Commitment bad_leaf(bad_bytes);
            REQUIRE(!proof_opt->verify(root, bad_leaf));
        }
    }
}
