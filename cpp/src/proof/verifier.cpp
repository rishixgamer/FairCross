#include "faircross/proof/verifier.hpp"
#include <charconv>
#include <string_view>
#include <system_error>

namespace faircross {

Result<Ok> SingleBatchVerifier::verify(
    const BatchProofPublicInputs& expected_public_inputs,
    const BatchProof& proof
) {
    if (proof.proof_version != 1) {
        return DomainError{DomainErrorKind::OrderValidationFailed, "Unsupported proof version"};
    }

    if (expected_public_inputs.pre_state_root != proof.public_inputs.pre_state_root ||
        expected_public_inputs.post_state_root != proof.public_inputs.post_state_root ||
        expected_public_inputs.batch_header_hash != proof.public_inputs.batch_header_hash ||
        expected_public_inputs.oracle_snapshot_hash != proof.public_inputs.oracle_snapshot_hash ||
        expected_public_inputs.clearing_price != proof.public_inputs.clearing_price ||
        expected_public_inputs.cleared_volume != proof.public_inputs.cleared_volume) {
        return DomainError{DomainErrorKind::OrderValidationFailed, "PublicInputMismatch"};
    }

    // This validates the exact transparent certificate format emitted by the
    // in-tree constraint harness. It is intentionally not a cryptographic
    // proof verification step: without a witness or a selected proving backend,
    // a well-formed certificate remains forgeable and must not be a security
    // boundary (see docs/LIMITATIONS.md).
    const std::string_view proof_sv(
        reinterpret_cast<const char*>(proof.proof_bytes.data()), proof.proof_bytes.size());
    constexpr std::string_view prefix = "FC-R1CS-V1:constraints=";
    constexpr std::string_view suffix = ":public_inputs_len=5:status=SAT";
    if (!proof_sv.starts_with(prefix) || !proof_sv.ends_with(suffix) ||
        proof_sv.size() <= prefix.size() + suffix.size()) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "InvalidTransparentCertificate"};
    }

    const std::string_view count_text = proof_sv.substr(
        prefix.size(), proof_sv.size() - prefix.size() - suffix.size());
    size_t constraint_count = 0;
    const char* count_begin = count_text.data();
    const char* count_end = count_begin + count_text.size();
    const auto [parsed_end, parse_error] =
        std::from_chars(count_begin, count_end, constraint_count);
    if (parse_error != std::errc{} || parsed_end != count_end || constraint_count == 0) {
        return DomainError{DomainErrorKind::OrderValidationFailed,
                           "InvalidTransparentCertificate"};
    }

    return ok;
}

} // namespace faircross
