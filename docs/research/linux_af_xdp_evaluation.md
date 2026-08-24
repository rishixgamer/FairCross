# Linux AF_XDP Ingestion Extension Evaluation

## 1. Technical Overview of AF_XDP

AF_XDP (eXpress Data Path address family / `XSK`) is a Linux kernel subsystem enabling zero-copy, kernel-bypass network packet acquisition directly into user-space memory rings (`UMEM`).

### Key Subsystems & Mechanics:
- **UMEM Ring Buffers**: Pinned DMA-mapped memory pool partitioned into fixed packet buffers (typically 2KB or 4KB).
- **Four Ring Queues**:
  1. `Fill Ring`: User-space deposits empty UMEM descriptor addresses for NIC RX.
  2. `Rx Ring`: NIC driver posts completed incoming packet descriptors (address, length) directly to user-space.
  3. `Tx Ring`: User-space enqueues egress packets.
  4. `Completion Ring`: NIC driver notifies user-space of completed transmissions.
- **Operating Modes**:
  - `XDP_ZEROCOPY` (Native Driver): Hardware NIC DMA writes directly into UMEM (sub-microsecond kernel bypass).
  - `XDP_COPY` (Generic SKB): Driver or kernel SKB layer copies packet into UMEM (software fallback).

---

## 2. Hardware, Kernel, and Driver Prerequisites

To achieve genuine zero-copy line rate on ITCH market data feeds, the target platform must satisfy:
1. **Operating System**: Linux with kernel $\ge 5.4$ (preferably $\ge 5.15$ or $\ge 6.1$ for multi-buffer support).
2. **Supported NIC Hardware & Drivers**:
   - Intel Ethernet 700 Series / 800 Series (`i40e`, `ice`, `ixgbe`)
   - Mellanox ConnectX-4 / 5 / 6 (`mlx5_core`)
   - Broadcom NetXtreme-E (`bnxt_en`)
3. **System Privileges & Tuning**:
   - `CAP_NET_ADMIN` and `CAP_SYS_ADMIN` capabilities for BPF map attaching and socket binding.
   - Pinned CPU isolation (`isolcpus`, `taskset`) with disabled CPU frequency scaling.
   - Hugepages (`hugetlbfs`) for UMEM buffer backing.

---

## 3. Host Environment Limitation & Decision

### Environment Assessment:
- **Current Host**: Apple Darwin (`aarch64-apple-darwin`, macOS).
- **Subsystem Availability**: The Darwin kernel lacks XDP, eBPF network hooks, and AF_XDP socket subsystems.

### Evaluation Policy:
FairCross does not claim constant-time, sub-microsecond, line-rate, or zero-copy performance without
measurements on appropriate hardware. If suitable hardware is unavailable, the extension remains
deferred rather than being represented by simulated performance claims.

### Recommendation & Decision: **DEFERRED (NO-GO for Host Workspace)**
1. **No Mocking/Faking**: We will not introduce synthetic or stubbed AF_XDP wrappers that simulate zero-copy behavior on non-Linux platforms.
2. **Clean Boundary Preservation**: The market-data streaming parser (`ItchStreamParser`) accepts
   contiguous byte buffers and can consume bytes copied from an AF_XDP UMEM descriptor ring on a
   suitable Linux host.
3. **Future Test Plan**: Bare-metal Linux testing should use a reviewed AF_XDP integration on
   dedicated 10GbE/25GbE hardware with PCAP replay or a hardware packet generator.
