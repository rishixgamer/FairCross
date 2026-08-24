#pragma once

// Renders execution results as the CLI's `--json` output.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "faircross/util/json_writer.hpp"
#include "faircross/engine/transition.hpp"
#include "faircross/commitments/commitment.hpp"
#include "faircross/proof/recursive/statement.hpp"

namespace faircross::cli {

inline const char* side_name(Side s) { return s == Side::Buy ? "buy" : "sell"; }

/// Byte strings are emitted as JSON arrays of integers, not as hex strings.
/// The published schema fixes the encoding, not merely the value.
inline void write_bytes(json::Writer& w, const uint8_t* data, size_t len) {
    w.begin_array();
    for (size_t i = 0; i < len; ++i) w.value_u64(data[i]);
    w.end_array();
}

inline void field_commitment(json::Writer& w, const std::string& key, const Commitment& c) {
    w.key(key);
    write_bytes(w, c.bytes.data(), c.bytes.size());
}

inline void field_byte_string(json::Writer& w, const std::string& key,
                              const std::vector<uint8_t>& bytes) {
    w.key(key);
    write_bytes(w, bytes.data(), bytes.size());
}

inline void write_order(json::Writer& w, const Order& o) {
    w.begin_object();
    w.field_u64("id", o.id.as_raw());
    w.field_u64("account", o.account.as_raw());
    w.field_u64("instrument", o.instrument.as_raw());
    w.field_string("side", side_name(o.side));
    w.field_u64("price", o.price.as_raw());
    w.field_u64("qty", o.qty.as_raw());
    w.field_u64("seq", o.seq);
    w.end_object();
}

inline void write_ledger(json::Writer& w, const Ledger& ledger) {
    w.begin_object();
    w.key("accounts");
    w.begin_object();
    for (const auto& [id, acc] : ledger.accounts()) {
        w.key(std::to_string(id.as_raw()));
        w.begin_object();
        w.field_u64("account_id", id.as_raw());
        w.field_u128("cash", acc.cash().as_raw());
        w.key("inventory");
        w.begin_object();
        for (const auto& [inst, qty] : acc.inventory()) {
            w.field_u64(std::to_string(inst.as_raw()), qty.as_raw());
        }
        w.end_object();
        w.end_object();
    }
    w.end_object();
    w.end_object();
}

inline void write_execution_result(json::Writer& w,
                                   const Batch& batch,
                                   const BatchExecutionResult& exec) {
    w.begin_object();

    w.key("batch");
    w.begin_object();
    w.field_u64("batch_id", batch.batch_id().as_raw());
    w.field_u64("instrument", batch.instrument().as_raw());
    w.key("orders");
    w.begin_array();
    for (const Order& o : batch.orders()) write_order(w, o);
    w.end_array();
    w.end_object();

    const auto cp = exec.clearing_outcome.clearing_price;
    const std::optional<uint64_t> cp_raw =
        cp.has_value() ? std::optional<uint64_t>(cp->as_raw()) : std::nullopt;

    w.key("clearing_outcome");
    w.begin_object();
    w.field_optional_u64("clearing_price", cp_raw);
    w.field_u64("executable_volume", exec.clearing_outcome.executable_volume.as_raw());
    w.end_object();

    w.key("allocation");
    w.begin_object();
    const auto acp = exec.allocation.clearing_price;
    w.field_optional_u64("clearing_price",
                         acp.has_value() ? std::optional<uint64_t>(acp->as_raw()) : std::nullopt);
    w.field_u64("target_volume", exec.allocation.target_volume.as_raw());
    w.field_u64("total_buy_allocated", exec.allocation.total_buy_allocated.as_raw());
    w.field_u64("total_sell_allocated", exec.allocation.total_sell_allocated.as_raw());
    w.key("allocations");
    w.begin_array();
    for (const OrderAllocation& a : exec.allocation.allocations) {
        w.begin_object();
        w.field_u64("order_id", a.order_id.as_raw());
        w.field_u64("account", a.account.as_raw());
        w.field_string("side", side_name(a.side));
        w.field_u64("limit_price", a.limit_price.as_raw());
        w.field_u64("original_qty", a.original_qty.as_raw());
        w.field_u64("allocated_qty", a.allocated_qty.as_raw());
        w.field_u64("seq", a.seq);
        w.end_object();
    }
    w.end_array();
    w.end_object();

    w.key("fills");
    w.begin_array();
    for (const Fill& f : exec.fills) {
        w.begin_object();
        w.field_u64("fill_idx", f.fill_id);
        w.field_u64("order_id", f.order_id.as_raw());
        w.field_u64("account_id", f.account_id.as_raw());
        w.field_u64("instrument_id", f.instrument_id.as_raw());
        w.field_string("side", side_name(f.side));
        w.field_u64("execution_price", f.execution_price.as_raw());
        w.field_u64("fill_qty", f.fill_qty.as_raw());
        w.field_u128("consideration", f.consideration.as_raw());
        w.end_object();
    }
    w.end_array();

    w.key("post_state");
    write_ledger(w, exec.post_state);

    w.end_object();
}

inline void write_public_inputs(json::Writer& w, const BatchProofPublicInputs& pi) {
    w.begin_object();
    field_commitment(w, "pre_state_root", pi.pre_state_root);
    field_commitment(w, "post_state_root", pi.post_state_root);
    field_commitment(w, "batch_header_hash", pi.batch_header_hash);
    field_commitment(w, "oracle_snapshot_hash", pi.oracle_snapshot_hash);
    w.field_u64("clearing_price", pi.clearing_price);
    w.field_u64("cleared_volume", pi.cleared_volume);
    w.end_object();
}

inline void write_batch_proof(json::Writer& w,
                              const BatchProofPublicInputs& pi,
                              const BatchProof& proof) {
    w.begin_object();
    w.field_u64("proof_version", proof.proof_version);
    w.key("public_inputs");
    write_public_inputs(w, pi);
    field_byte_string(w, "proof_bytes", proof.proof_bytes);
    w.end_object();
}

inline void write_running_state(json::Writer& w, const RunningState& state) {
    w.begin_object();
    field_commitment(w, "ledger_root", state.ledger_root);
    field_commitment(w, "history_accumulator", state.history_accumulator);
    w.field_u64("batch_id", state.batch_id.as_raw());
    w.field_u64("timestamp_nanos", state.timestamp_nanos);
    w.field_u64("instrument_id", state.instrument_id.as_raw());
    w.end_object();
}

} // namespace faircross::cli
