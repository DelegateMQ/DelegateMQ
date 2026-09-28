-- dmq.lua
-- @see https://github.com/DelegateMQ/DelegateMQ
-- Wireshark dissector for the DelegateMQ remote delegate wire format.
--
-- Every DelegateMQ message on UDP or TCP starts with an 8-byte header, all
-- fields big-endian (see src/delegate-mq/port/transport/common/DmqHeader.h):
--
--   offset 0  uint16  marker     0xAA55
--   offset 2  uint16  remote ID  DelegateRemoteId; 0 = ACK
--   offset 4  uint16  sequence   per-sender sequence number
--   offset 6  uint16  length     payload length in bytes (0 for an ACK)
--   offset 8  payload            serializer output (not decoded here)
--
-- An ACK is a bare header with remote ID 0 that echoes the sequence number of
-- the message it acknowledges.
--
-- UDP carries one message per datagram. TCP is a byte stream of back-to-back
-- messages. The serial transport adds a CRC and is not handled here.
--
-- Topic names: DelegateMQ has no discovery protocol, so remote IDs can't be
-- mapped to topic names from the traffic alone. Load a table that sets
-- entries in the global `dmq_topics`, e.g. dmq_topics[100] = "cell/cmd/run".
-- A remote ID is a channel that may carry several topics, so an entry is a
-- free-form label shown as the Topic field. See README.md.

local dmq = Proto("dmq", "DelegateMQ")
-- TCP needs stream reassembly, so it gets its own entry point. Messages still
-- show as "DelegateMQ" with the same dmq.* fields.
local dmq_tcp = Proto("dmq_tcp", "DelegateMQ over TCP")

local MARKER = 0xAA55
local HEADER_SIZE = 8
local ACK_REMOTE_ID = 0

-- Filled by topic table files (see README.md). Declared here as well so
-- load order between this file and the table files doesn't matter.
dmq_topics = dmq_topics or {}

local f = dmq.fields
f.marker  = ProtoField.uint16("dmq.marker", "Marker", base.HEX)
f.id      = ProtoField.uint16("dmq.id", "Remote ID", base.DEC)
f.seq     = ProtoField.uint16("dmq.seq", "Sequence", base.DEC)
f.length  = ProtoField.uint16("dmq.length", "Payload Length", base.DEC)
f.topic   = ProtoField.string("dmq.topic", "Topic")
f.ack     = ProtoField.bool("dmq.ack", "ACK")
f.payload = ProtoField.bytes("dmq.payload", "Payload")

local ef_bad_marker = ProtoExpert.new("dmq.bad_marker", "Invalid DelegateMQ marker",
    expert.group.MALFORMED, expert.severity.ERROR)
local ef_truncated = ProtoExpert.new("dmq.truncated", "Payload shorter than header length",
    expert.group.MALFORMED, expert.severity.ERROR)
local ef_trailing = ProtoExpert.new("dmq.trailing", "Extra bytes after payload",
    expert.group.MALFORMED, expert.severity.WARN)
local ef_ack_payload = ProtoExpert.new("dmq.ack_payload", "ACK with non-zero payload length",
    expert.group.PROTOCOL, expert.severity.WARN)
dmq.experts = { ef_bad_marker, ef_truncated, ef_trailing, ef_ack_payload }

dmq.prefs.udp_ports = Pref.range("UDP ports", "", "UDP ports to decode as DelegateMQ, e.g. 5010-5013", 65535)
dmq.prefs.tcp_ports = Pref.range("TCP ports", "", "TCP ports to decode as DelegateMQ", 65535)
dmq.prefs.heuristic = Pref.bool("Detect on any UDP port",
    true, "Recognize DelegateMQ on any UDP port by its 0xAA55 marker")

-- Dissect one complete message starting at the beginning of tvb.
-- Returns the number of bytes consumed.
local function dissect_message(tvb, pinfo, tree)
    local marker = tvb(0, 2):uint()
    local id     = tvb(2, 2):uint()
    local seq    = tvb(4, 2):uint()
    local length = tvb(6, 2):uint()
    local is_ack = (id == ACK_REMOTE_ID)
    local topic  = dmq_topics[id]

    local subtree = tree:add(dmq, tvb(0, math.min(tvb:len(), HEADER_SIZE + length)))

    local marker_item = subtree:add(f.marker, tvb(0, 2))
    if marker ~= MARKER then
        marker_item:add_proto_expert_info(ef_bad_marker)
    end

    subtree:add(f.id, tvb(2, 2))
    subtree:add(f.ack, is_ack):set_generated()
    if topic then
        subtree:add(f.topic, topic):set_generated()
    end
    subtree:add(f.seq, tvb(4, 2))

    local length_item = subtree:add(f.length, tvb(6, 2))
    if is_ack and length ~= 0 then
        length_item:add_proto_expert_info(ef_ack_payload)
    end

    local available = tvb:len() - HEADER_SIZE
    if length > available then
        length_item:add_proto_expert_info(ef_truncated)
    end
    local payload_len = math.min(length, available)
    if payload_len > 0 then
        subtree:add(f.payload, tvb(HEADER_SIZE, payload_len))
    end

    -- Info column summary
    local info
    if is_ack then
        info = string.format("ACK seq=%d", seq)
    else
        info = string.format("ID=%d%s seq=%d len=%d", id,
            topic and (" (" .. topic .. ")") or "", seq, length)
    end
    subtree:append_text(", " .. info)
    pinfo.cols.protocol = "DMQ"
    pinfo.cols.info:append((tostring(pinfo.cols.info) ~= "" and " | " or "") .. info)

    return HEADER_SIZE + payload_len
end

-- UDP: one message per datagram.
local function dissect_udp(tvb, pinfo, tree)
    if tvb:len() < HEADER_SIZE then
        return 0
    end
    pinfo.cols.info:clear()
    local consumed = dissect_message(tvb, pinfo, tree)
    if consumed < tvb:len() then
        tree:add_proto_expert_info(ef_trailing,
            string.format("%d extra bytes after payload", tvb:len() - consumed))
    end
    return tvb:len()
end

-- TCP: reassemble the stream into messages using the header's length field.
local function tcp_message_len(tvb, pinfo, offset)
    return HEADER_SIZE + tvb(offset + 6, 2):uint()
end

local function dissect_tcp_message(tvb, pinfo, tree)
    dissect_message(tvb, pinfo, tree)
    return tvb:len()
end

function dmq.dissector(tvb, pinfo, tree)
    return dissect_udp(tvb, pinfo, tree)
end

function dmq_tcp.dissector(tvb, pinfo, tree)
    pinfo.cols.info:clear()
    dissect_tcp_pdus(tvb, tree, HEADER_SIZE, tcp_message_len, dissect_tcp_message)
    return tvb:len()
end

-- Heuristic: claim a UDP datagram if it starts with the marker and its length
-- field is consistent with the datagram size.
local function heuristic_udp(tvb, pinfo, tree)
    if not dmq.prefs.heuristic then
        return false
    end
    if tvb:len() < HEADER_SIZE or tvb(0, 2):uint() ~= MARKER then
        return false
    end
    local id     = tvb(2, 2):uint()
    local length = tvb(6, 2):uint()
    if HEADER_SIZE + length ~= tvb:len() then
        return false
    end
    if id == ACK_REMOTE_ID and length ~= 0 then
        return false
    end
    dissect_udp(tvb, pinfo, tree)
    pinfo.conversation = dmq
    return true
end

dmq:register_heuristic("udp", heuristic_udp)

-- Explicit port registration from preferences (Decode As also works).
local registered_udp, registered_tcp = "", ""
function dmq.prefs_changed()
    local udp_table = DissectorTable.get("udp.port")
    local tcp_table = DissectorTable.get("tcp.port")
    if registered_udp ~= "" then udp_table:remove(registered_udp, dmq) end
    if registered_tcp ~= "" then tcp_table:remove(registered_tcp, dmq_tcp) end
    registered_udp = dmq.prefs.udp_ports
    registered_tcp = dmq.prefs.tcp_ports
    if registered_udp ~= "" then udp_table:add(registered_udp, dmq) end
    if registered_tcp ~= "" then tcp_table:add(registered_tcp, dmq_tcp) end
end

DissectorTable.get("udp.port"):add_for_decode_as(dmq)
DissectorTable.get("tcp.port"):add_for_decode_as(dmq_tcp)
