#include "faircross/engine/allocation.hpp"
#include "faircross/engine/partition.hpp"
#include <map>
#include <algorithm>

namespace faircross {

namespace {

struct AtTheMoneyCandidate {
    const Order* order;
    uint64_t base_fill;
    uint64_t remainder;
};

} // namespace

Result<std::vector<OrderAllocation>> allocate_side(
    const std::vector<Order>& orders,
    Price clearing_price,
    Qty target_volume,
    Side target_side
) {
    if (target_volume.is_zero()) {
        std::vector<OrderAllocation> allocs;
        for (const auto& o : orders) {
            if (o.side == target_side) {
                allocs.push_back(OrderAllocation{
                    o.id,
                    o.account,
                    o.side,
                    o.price,
                    o.qty,
                    Qty::zero(),
                    o.seq
                });
            }
        }
        return allocs;
    }

    std::vector<const Order*> itm_orders;
    std::vector<const Order*> atm_orders;
    std::vector<const Order*> otm_orders;

    Qty q_itm = Qty::zero();
    Qty q_atm = Qty::zero();

    for (const auto& order : orders) {
        if (order.side != target_side) continue;

        switch (classify_order(order, clearing_price)) {
            case OrderMoneyness::InTheMoney: {
                auto res = q_itm.checked_add(order.qty);
                if (res.is_err()) return res;
                q_itm = res.value();
                itm_orders.push_back(&order);
                break;
            }
            case OrderMoneyness::AtTheMoney: {
                auto res = q_atm.checked_add(order.qty);
                if (res.is_err()) return res;
                q_atm = res.value();
                atm_orders.push_back(&order);
                break;
            }
            case OrderMoneyness::OutOfTheMoney: {
                otm_orders.push_back(&order);
                break;
            }
        }
    }

    std::map<uint64_t, uint64_t> itm_fill_map;
    uint64_t v_rem = 0;

    if (target_volume.as_raw() <= q_itm.as_raw()) {
        uint64_t rem_target = target_volume.as_raw();
        auto sorted_itm = itm_orders;
        std::sort(sorted_itm.begin(), sorted_itm.end(), [target_side](const Order* a, const Order* b) {
            if (target_side == Side::Buy) {
                if (a->price != b->price) return a->price > b->price;
                return a->seq < b->seq;
            } else {
                if (a->price != b->price) return a->price < b->price;
                return a->seq < b->seq;
            }
        });

        for (const auto* o : sorted_itm) {
            uint64_t fill = std::min(o->qty.as_raw(), rem_target);
            itm_fill_map[o->id.as_raw()] = fill;
            rem_target -= fill;
        }
        v_rem = 0;
    } else {
        for (const auto* o : itm_orders) {
            itm_fill_map[o->id.as_raw()] = o->qty.as_raw();
        }
        v_rem = target_volume.as_raw() - q_itm.as_raw();
    }

    std::map<uint64_t, uint64_t> atm_fill_map;
    if (v_rem > 0 && !atm_orders.empty()) {
        if (v_rem >= q_atm.as_raw()) {
            for (const auto* o : atm_orders) {
                atm_fill_map[o->id.as_raw()] = o->qty.as_raw();
            }
        } else {
            unsigned __int128 total_atm = q_atm.as_raw();
            unsigned __int128 v_rem_128 = v_rem;

            std::vector<AtTheMoneyCandidate> candidates;
            candidates.reserve(atm_orders.size());

            uint64_t base_sum = 0;
            for (const auto* o : atm_orders) {
                unsigned __int128 num = static_cast<unsigned __int128>(o->qty.as_raw()) * v_rem_128;
                uint64_t base_fill = static_cast<uint64_t>(num / total_atm);
                uint64_t remainder = static_cast<uint64_t>(num % total_atm);
                base_sum += base_fill;
                candidates.push_back(AtTheMoneyCandidate{o, base_fill, remainder});
            }

            size_t surplus = static_cast<size_t>(v_rem - base_sum);

            std::sort(candidates.begin(), candidates.end(), [](const AtTheMoneyCandidate& a, const AtTheMoneyCandidate& b) {
                if (a.remainder != b.remainder) return a.remainder > b.remainder;
                if (a.order->seq != b.order->seq) return a.order->seq < b.order->seq;
                return a.order->id.as_raw() < b.order->id.as_raw();
            });

            for (const auto& c : candidates) {
                uint64_t extra = (surplus > 0) ? (surplus--, 1ULL) : 0ULL;
                uint64_t total_fill = std::min(c.base_fill + extra, c.order->qty.as_raw());
                atm_fill_map[c.order->id.as_raw()] = total_fill;
            }
        }
    }

    std::vector<OrderAllocation> allocations;
    allocations.reserve(orders.size());

    for (const auto& order : orders) {
        if (order.side != target_side) continue;

        uint64_t fill = 0;
        switch (classify_order(order, clearing_price)) {
            case OrderMoneyness::InTheMoney: {
                auto it = itm_fill_map.find(order.id.as_raw());
                if (it != itm_fill_map.end()) fill = it->second;
                break;
            }
            case OrderMoneyness::AtTheMoney: {
                auto it = atm_fill_map.find(order.id.as_raw());
                if (it != atm_fill_map.end()) fill = it->second;
                break;
            }
            case OrderMoneyness::OutOfTheMoney: {
                fill = 0;
                break;
            }
        }

        allocations.push_back(OrderAllocation{
            order.id,
            order.account,
            order.side,
            order.price,
            order.qty,
            Qty::from_raw(fill),
            order.seq
        });
    }

    return allocations;
}

Result<BatchAllocation> allocate_batch(
    const Batch& batch,
    std::optional<Price> clearing_price,
    Qty target_volume
) {
    if (!clearing_price.has_value() || target_volume.is_zero()) {
        std::vector<OrderAllocation> allocations;
        for (const auto& o : batch.orders()) {
            allocations.push_back(OrderAllocation{
                o.id,
                o.account,
                o.side,
                o.price,
                o.qty,
                Qty::zero(),
                o.seq
            });
        }
        return BatchAllocation{
            std::nullopt,
            Qty::zero(),
            Qty::zero(),
            Qty::zero(),
            std::move(allocations)
        };
    }

    Price cp = clearing_price.value();
    auto buy_res = allocate_side(batch.orders(), cp, target_volume, Side::Buy);
    if (buy_res.is_err()) return buy_res;

    auto sell_res = allocate_side(batch.orders(), cp, target_volume, Side::Sell);
    if (sell_res.is_err()) return sell_res;

    const auto& buy_allocs = buy_res.value();
    const auto& sell_allocs = sell_res.value();

    uint64_t buy_sum = 0;
    for (const auto& a : buy_allocs) buy_sum += a.allocated_qty.as_raw();

    uint64_t sell_sum = 0;
    for (const auto& a : sell_allocs) sell_sum += a.allocated_qty.as_raw();

    std::map<uint64_t, OrderAllocation> buy_map;
    for (const auto& a : buy_allocs) buy_map[a.order_id.as_raw()] = a;

    std::map<uint64_t, OrderAllocation> sell_map;
    for (const auto& a : sell_allocs) sell_map[a.order_id.as_raw()] = a;

    std::vector<OrderAllocation> allocations;
    allocations.reserve(batch.len());

    for (const auto& order : batch.orders()) {
        auto bit = buy_map.find(order.id.as_raw());
        if (bit != buy_map.end()) {
            allocations.push_back(bit->second);
            buy_map.erase(bit);
        } else {
            auto sit = sell_map.find(order.id.as_raw());
            if (sit != sell_map.end()) {
                allocations.push_back(sit->second);
                sell_map.erase(sit);
            }
        }
    }

    return BatchAllocation{
        cp,
        target_volume,
        Qty::from_raw(buy_sum),
        Qty::from_raw(sell_sum),
        std::move(allocations)
    };
}

} // namespace faircross
