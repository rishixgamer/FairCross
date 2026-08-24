# FairCross Market Quality Metrics Specification

## 1. Overview & Scope

This specification defines the market quality and execution metrics used in FairCross simulations and microstructure experiments. All metrics are computed strictly using integer / fixed-point arithmetic (permille basis points, integer ticks, and atomic cash units).

> **Disclaimer on Realism**: These metrics measure structural performance and execution efficiency within the FairCross frequent-batch mechanism. They do not claim to forecast empirical financial profitability or real-world trader utility.

---

## 2. Mathematical Metric Definitions

### 2.1 Volume and Fill Rates
1. **Total Submitted Volume**:
   $$V_{\text{submitted}} = \sum_{i \in \text{Orders}} \text{Qty}(i)$$
2. **Total Cleared Volume**:
   $$V^*_{\text{total}} = \sum_{k=1}^K V^*_k$$
3. **Aggregate Fill Rate (Permille)**:
   $$\text{FillRatePermille} = \left\lfloor \frac{2 \times V^*_{\text{total}}}{V_{\text{submitted}}} \times 1000 \right\rfloor$$
   *(Factor of 2 accounts for two-sided crossing: 1 lot executed matches 1 buy lot and 1 sell lot).*

### 2.2 Logical Execution Latency (Time-to-Fill)
For an order $i$ submitted at logical time $t_{\text{arrival}}(i)$ and executed in batch $k$ with cutoff $T_k$:
$$\Delta t_{\text{delay}}(i) = T_k - t_{\text{arrival}}(i)$$
- **Mean Execution Delay**:
  $$\overline{\Delta t} = \left\lfloor \frac{1}{|\text{Fills}|} \sum_{i \in \text{Fills}} \Delta t_{\text{delay}}(i) \right\rfloor$$
- **Max Execution Delay**:
  $$\Delta t_{\max} = \max_{i \in \text{Fills}} \Delta t_{\text{delay}}(i)$$

### 2.3 Reference Price Deviation & Implementation Shortfall
When an order $i$ arrives at a benchmark reference mid price $P_{\text{ref}}(i)$ and clears at auction price $P^*$:
1. **Execution Slippage (Signed Ticks)**:
   $$\text{SlippageTicks}(i) = \begin{cases} P^* - P_{\text{ref}}(i) & \text{if Buy} \\ P_{\text{ref}}(i) - P^* & \text{if Sell} \end{cases}$$
2. **Implementation Shortfall (Atomic Money)**:
   $$\text{Shortfall}(i) = \text{FillQty}(i) \times |P^* - P_{\text{ref}}(i)|$$
   $$\text{TotalShortfall} = \sum_{i \in \text{Fills}} \text{Shortfall}(i)$$
