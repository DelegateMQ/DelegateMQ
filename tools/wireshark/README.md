# DelegateMQ Wireshark Dissector

A [Wireshark](https://www.wireshark.org/) Lua plugin that decodes DelegateMQ remote delegate traffic on UDP and TCP.

Without it, DelegateMQ packets show as raw UDP/TCP data. With it, each message shows its header fields, an ACK flag, an optional topic label and the payload, and Wireshark's filters and statistics work on those fields.

```
DelegateMQ, ID=103 (cell/status/run) seq=42 len=22
    Marker: 0xaa55
    Remote ID: 103
    [ACK: False]
    [Topic: cell/status/run]
    Sequence: 42
    Payload Length: 22
    Payload: 0a1f...
```

## What It Decodes

Every DelegateMQ message on a network transport starts with an 8-byte header, big-endian (`src/delegate-mq/port/transport/common/DmqHeader.h`):

| Offset | Field | Notes |
|--------|-------|-------|
| 0 | Marker (`uint16`) | Always `0xAA55` |
| 2 | Remote ID (`uint16`) | `DelegateRemoteId`. `0` is an ACK |
| 4 | Sequence (`uint16`) | Per-sender sequence number. An ACK echoes the sequence it acknowledges |
| 6 | Length (`uint16`) | Payload length in bytes. `0` for an ACK |
| 8 | Payload | Serializer output (`serialize`, MessagePack, ...) |

- **UDP:** one message per datagram.
- **TCP:** a stream of back-to-back messages. The dissector reassembles messages that span TCP segments.
- **Payloads are not decoded.** The wire carries no type information, so the payload is shown as bytes.
- **Not supported:** the serial transport (it adds a CRC and is rarely captured by Wireshark), and transports with their own framing (ZeroMQ, NNG, MQTT).

## Install

1. Find your personal Lua plugins folder: **Help > About Wireshark > Folders > Personal Lua Plugins**. Typically:
   - Windows: `%APPDATA%\Wireshark\plugins`
   - Linux / macOS: `~/.local/lib/wireshark/plugins`
2. Copy `dmq.lua` into it.
3. Optional: copy a topic table next to it (see [Topic Labels](#topic-labels)).
4. Restart Wireshark, or use **Analyze > Reload Lua Plugins** (`Ctrl+Shift+L`).

Check it loaded: **Analyze > Enabled Protocols** lists `DMQ` and `DMQ_TCP`.

## Finding the Traffic

- **UDP, no setup:** a heuristic recognizes DelegateMQ on any UDP port by the `0xAA55` marker and a length field matching the datagram size. Turn it off under **Edit > Preferences > Protocols > DMQ > Detect on any UDP port** if it ever claims unrelated traffic.
- **UDP or TCP, fixed ports:** set **Edit > Preferences > Protocols > DMQ > UDP ports / TCP ports** (e.g. `5010-5013`).
- **One-off:** right-click a packet, **Decode As...**, and pick `DMQ` (UDP) or `DMQ_TCP` (TCP).

TCP has no heuristic: set the TCP port preference or use Decode As.

**Capturing on localhost** (e.g. all nodes on one PC, like Cellutron): on Windows this needs Npcap's loopback adapter, installed with Wireshark when "Support loopback traffic" is checked. Capture on **Adapter for loopback traffic capture**. On Linux, capture on `lo`.

## Display Filters

| Filter | Shows |
|--------|-------|
| `dmq` | All DelegateMQ messages |
| `dmq && !dmq.ack` | Data messages only |
| `dmq.ack` | ACKs only |
| `dmq.id == 103` | One remote ID |
| `dmq.topic contains "heartbeat"` | Messages whose topic label matches (needs a topic table) |
| `dmq.seq == 42` | A message and its ACK |
| `_ws.expert && dmq` | Malformed messages (see below) |

**Statistics > Conversations** and **Statistics > I/O Graphs** (with a `dmq.id == N` filter) show traffic per peer and per remote ID over time.

## Warnings

The dissector flags problems in Wireshark's **Expert Information**:

| Warning | Meaning |
|---------|---------|
| Invalid DelegateMQ marker | First two bytes aren't `0xAA55`. On TCP, usually means the stream lost sync |
| Payload shorter than header length | Header length claims more bytes than the packet holds |
| Extra bytes after payload | UDP datagram longer than header plus payload |
| ACK with non-zero payload length | Remote ID 0 with a payload |

## Topic Labels

DelegateMQ has no discovery protocol, so nothing on the wire says which topic a remote ID carries (unlike DDS, whose discovery traffic lets Wireshark name topics automatically). Supply the mapping with a small Lua file that fills the global `dmq_topics` table:

```lua
dmq_topics = dmq_topics or {}
dmq_topics[100] = "cell/cmd/run"
dmq_topics[103] = "cell/status/run"
```

Copy it into the same plugins folder as `dmq.lua`. Load order doesn't matter.

A remote ID is a channel, not a topic. One ID can carry several topics (Cellutron's controller sends five sensor topics on one ID), so an entry is a free-form label.

Example: [`example/cellutron/wireshark/cellutron_topics.lua`](../../example/cellutron/wireshark/cellutron_topics.lua). Write one per project from its `RID_*` constants and the topics each node sends on them.

## Limitations

- Payloads are shown as bytes, not decoded fields.
- Topic labels need a hand-written table per project.
- Only the header-framed network transports are decoded (UDP, TCP).
- Tested against a mock of the Wireshark Lua API, not yet against a live Wireshark capture.
