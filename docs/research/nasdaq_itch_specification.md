# Nasdaq TotalView-ITCH 5.0 Specification Research & Replay Scope

## 1. Specification Provenance & Official Source

- **Protocol**: Nasdaq TotalView-ITCH Version 5.0
- **Official Specification Reference**: *Nasdaq TotalView-ITCH 5.0 Specification*, Nasdaq Global Data Products, Updated Version 5.0.
- **Data Model**: Direct Level-3 order-by-order market data feed providing full deterministic order book state reconstruction.
- **Physical Framing**:
  - Offline MoldUDP64 / Dump files: Each packet begins with a 2-byte Big-Endian unsigned integer `length: std::uint16_t` defining the length of the payload message, followed by the message bytes.
  - Endianness: Strictly **Big-Endian** (Network Byte Order) for all multi-byte integer and timestamp fields.
  - Price Encoding: 4-byte unsigned integer representing price in fixed-point with 4 implied decimal places ($1000000 \to \$100.0000$). No floating point operations.
  - Timestamp Encoding: 6-byte (48-bit) Big-Endian unsigned integer measuring integer nanoseconds since midnight UTC ($0 \dots 86,400 \times 10^9 - 1$).

---

## 2. Minimal Message Subset for Offline L3 Book & BBO Reconstruction

To reconstruct the continuous Level-3 order book, calculate synthetic frequent-batch intake, and compute real-time Reference Best Bid/Offer (BBO) snapshots without live networking, we require exactly 8 core message types:

| Message Type | Name | Wire Length (excl. 2B prefix) | Purpose in FairCross Replay |
|---|---|---|---|
| `'S'` | **System Event** | 11 bytes | System start/end of day markers and session boundaries. |
| `'R'` | **Stock Directory** | 38 bytes | Maps `stock_locate: std::uint16_t` to 8-character ASCII ticker symbols (e.g. `AAPL`, `MSFT`). |
| `'A'` | **Add Order (No MPID)** | 35 bytes | Enters a new resting limit order into the Level-3 book with explicit `order_reference_number`. |
| `'F'` | **Add Order (With MPID)**| 39 bytes | Same as `'A'` with 4-byte participant attribution. |
| `'E'` | **Order Executed** | 30 bytes | Decrements resting order `shares` upon fill. |
| `'C'` | **Order Executed with Price** | 35 bytes | Execution fill with explicit execution price tick. |
| `'X'` | **Order Cancel** | 22 bytes | Partial lot cancellation; decrements resting order shares. |
| `'D'` | **Order Delete** | 18 bytes | Complete cancellation; removes order from book. |
| `'U'` | **Order Replace** | 34 bytes | Atomic cancellation of `orig_ref` and insertion of `new_ref` with updated price/shares. |

---

## 3. Binary Field Layouts & Endianness

### 3.1 Common Message Header
- `length`: 2 bytes (`std::uint16_t`, Big-Endian) [in file-stream framing]
- `message_type`: 1 byte (`std::uint8_t`, ASCII character)
- `stock_locate`: 2 bytes (`std::uint16_t`, Big-Endian)
- `tracking_number`: 2 bytes (`std::uint16_t`, Big-Endian)
- `timestamp`: 6 bytes (`std::array<std::uint8_t, 6>`, Big-Endian 48-bit unsigned integer nanoseconds)

### 3.2 Add Order Message (`'A'`)
```
Offset  Field                Type      Description
0       Type                 1 Byte    'A'
1       Stock Locate         2 Bytes   std::uint16_t (Big-Endian)
3       Tracking Number      2 Bytes   std::uint16_t (Big-Endian)
5       Timestamp            6 Bytes   48-bit unsigned (Big-Endian nanoseconds)
11      Order Reference Num  8 Bytes   std::uint64_t (Big-Endian)
19      Buy/Sell Indicator   1 Byte    'B' = Buy, 'S' = Sell
20      Shares               4 Bytes   std::uint32_t (Big-Endian)
24      Stock Symbol         8 Bytes   ASCII (Left-aligned, space-padded)
32      Price                4 Bytes   std::uint32_t (Big-Endian, 4 decimal fixed point)
Total: 36 Bytes (Type + 35 payload bytes)
```

### 3.3 Order Executed Message (`'E'`)
```
Offset  Field                Type      Description
0       Type                 1 Byte    'E'
1       Stock Locate         2 Bytes   std::uint16_t (Big-Endian)
3       Tracking Number      2 Bytes   std::uint16_t (Big-Endian)
5       Timestamp            6 Bytes   48-bit unsigned (Big-Endian nanoseconds)
11      Order Reference Num  8 Bytes   std::uint64_t (Big-Endian)
19      Executed Shares      4 Bytes   std::uint32_t (Big-Endian)
23      Match Number         8 Bytes   std::uint64_t (Big-Endian)
Total: 31 Bytes (Type + 30 payload bytes)
```

### 3.4 Order Cancel Message (`'X'`)
```
Offset  Field                Type      Description
0       Type                 1 Byte    'X'
1       Stock Locate         2 Bytes   std::uint16_t (Big-Endian)
3       Tracking Number      2 Bytes   std::uint16_t (Big-Endian)
5       Timestamp            6 Bytes   48-bit unsigned (Big-Endian nanoseconds)
11      Order Reference Num  8 Bytes   std::uint64_t (Big-Endian)
19      Canceled Shares      4 Bytes   std::uint32_t (Big-Endian)
Total: 23 Bytes (Type + 22 payload bytes)
```

### 3.5 Order Delete Message (`'D'`)
```
Offset  Field                Type      Description
0       Type                 1 Byte    'D'
1       Stock Locate         2 Bytes   std::uint16_t (Big-Endian)
3       Tracking Number      2 Bytes   std::uint16_t (Big-Endian)
5       Timestamp            6 Bytes   48-bit unsigned (Big-Endian nanoseconds)
11      Order Reference Num  8 Bytes   std::uint64_t (Big-Endian)
Total: 19 Bytes (Type + 18 payload bytes)
```

### 3.6 Order Replace Message (`'U'`)
```
Offset  Field                Type      Description
0       Type                 1 Byte    'U'
1       Stock Locate         2 Bytes   std::uint16_t (Big-Endian)
3       Tracking Number      2 Bytes   std::uint16_t (Big-Endian)
5       Timestamp            6 Bytes   48-bit unsigned (Big-Endian nanoseconds)
11      Original Order Ref   8 Bytes   std::uint64_t (Big-Endian)
19      New Order Ref        8 Bytes   std::uint64_t (Big-Endian)
27      Shares               4 Bytes   std::uint32_t (Big-Endian)
31      Price                4 Bytes   std::uint32_t (Big-Endian, 4 decimal fixed point)
Total: 35 Bytes (Type + 34 payload bytes)
```

---

## 4. Architecture & Clean Separation

- **Strict Offline Scope**: Replay operates exclusively on offline file streams and byte slices; no network sockets, multicast receivers, or live connectivity dependencies are introduced.
- **Zero Floating Point**: All prices and cash balances mapped from ITCH 5.0 are translated directly to integer ticks and atomic Money units.
- **Deterministic Level-3 Book State**: Bids and asks maintain deterministic price-time priority structures for reference pricing.
