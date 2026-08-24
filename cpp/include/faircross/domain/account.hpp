#pragma once

#include <array>
#include <map>
#include <vector>
#include "faircross/domain/primitives.hpp"

namespace faircross {

/// Participant account state tracking available cash and asset inventories.
class AccountState {
public:
    AccountState() : id_(0), cash_(Money::zero()) {}
    explicit AccountState(AccountId id, Money initial_cash = Money::zero())
        : id_(id), cash_(initial_cash) {}

    [[nodiscard]] AccountId id() const noexcept { return id_; }

    /// 256-bit blinding salt for this account's ledger commitment leaf (ADR-011).
    ///
    /// Zero means unblinded: the resulting state root is binding but not hiding,
    /// and is brute-forceable from the published conservation totals.
    [[nodiscard]] const std::array<uint8_t, 32>& commitment_salt() const noexcept {
        return commitment_salt_;
    }

    void set_commitment_salt(const std::array<uint8_t, 32>& salt) { commitment_salt_ = salt; }

    /// Whether this account carries a blinding salt. An all-zero salt is treated
    /// as absent rather than valid, so an unblinded ledger is detectable.
    [[nodiscard]] bool is_blinded() const noexcept {
        return commitment_salt_ != std::array<uint8_t, 32>{};
    }
    [[nodiscard]] Money cash() const noexcept { return cash_; }

    [[nodiscard]] Qty inventory_of(InstrumentId inst) const noexcept {
        auto it = inventory_.find(inst);
        if (it != inventory_.end()) {
            return it->second;
        }
        return Qty::zero();
    }

    const std::map<InstrumentId, Qty>& inventory() const noexcept { return inventory_; }

    Result<Ok> credit_cash(Money amount) {
        auto res = cash_.checked_add(amount);
        if (res.is_err()) return res;
        cash_ = res.value();
        return ok;
    }

    Result<Ok> debit_cash(Money amount) {
        auto res = cash_.checked_sub(amount);
        if (res.is_err()) return PrimitiveError{PrimitiveErrorKind::InsufficientBalance, "Cash"};
        cash_ = res.value();
        return ok;
    }

    Result<Ok> credit_inventory(InstrumentId inst, Qty amount) {
        Qty current = inventory_of(inst);
        auto res = current.checked_add(amount);
        if (res.is_err()) return res;
        inventory_[inst] = res.value();
        return ok;
    }

    Result<Ok> debit_inventory(InstrumentId inst, Qty amount) {
        Qty current = inventory_of(inst);
        auto res = current.checked_sub(amount);
        if (res.is_err()) return PrimitiveError{PrimitiveErrorKind::InsufficientBalance, "Inventory"};
        if (res.value().is_zero()) {
            inventory_.erase(inst);
        } else {
            inventory_[inst] = res.value();
        }
        return ok;
    }

    auto operator<=>(const AccountState&) const = default;

private:
    AccountId id_;
    Money cash_;
    std::map<InstrumentId, Qty> inventory_;
    std::array<uint8_t, 32> commitment_salt_{};
};

/// Multi-account ledger mapping AccountId to AccountState.
class Ledger {
public:
    Ledger() = default;

    AccountState& get_or_create_account(AccountId id) {
        auto it = accounts_.find(id);
        if (it == accounts_.end()) {
            it = accounts_.emplace(id, AccountState(id, Money::zero())).first;
        }
        return it->second;
    }

    const AccountState* get_account(AccountId id) const {
        auto it = accounts_.find(id);
        if (it == accounts_.end()) return nullptr;
        return &it->second;
    }

    void insert_account(AccountState state) {
        accounts_.insert_or_assign(state.id(), std::move(state));
    }

    const std::map<AccountId, AccountState>& accounts() const noexcept { return accounts_; }

    /// Mutable access for blinding; see `blind_ledger` (ADR-011).
    std::map<AccountId, AccountState>& accounts_mut() noexcept { return accounts_; }

    /// Whether every account carries a blinding salt.
    [[nodiscard]] bool is_fully_blinded() const noexcept {
        if (accounts_.empty()) return false;
        for (const auto& [id, acc] : accounts_) {
            if (!acc.is_blinded()) return false;
        }
        return true;
    }

    Result<Money> total_cash() const {
        Money sum = Money::zero();
        for (const auto& [_, acc] : accounts_) {
            auto res = sum.checked_add(acc.cash());
            if (res.is_err()) return res;
            sum = res.value();
        }
        return sum;
    }

    Result<Qty> total_inventory_of(InstrumentId inst) const {
        Qty sum = Qty::zero();
        for (const auto& [_, acc] : accounts_) {
            auto res = sum.checked_add(acc.inventory_of(inst));
            if (res.is_err()) return res;
            sum = res.value();
        }
        return sum;
    }

    auto operator<=>(const Ledger&) const = default;

private:
    std::map<AccountId, AccountState> accounts_;
};

} // namespace faircross
