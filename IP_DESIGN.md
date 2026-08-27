# DeckLink IP 100G support

The subsystem now treats a DeckLink IP 100G sub-device as an IP channel while
leaving the existing SDI connector map and channel values intact. The IP
interfaces are optional: a device for which `QueryInterface` does not return
`IID_IDeckLinkIPExtensions` follows the existing SDI path.

## Flow lifecycle

During device discovery, each `IDeckLink` is probed for
`IDeckLinkIPExtensions`. If present, the subsystem gets its flow iterator and
examines at most six entries. A failed/null flow-attributes query terminates
the enumeration immediately. The flow direction attribute is recorded for
diagnostics only; it is not used to decide whether a flow is a sender or
receiver.

For capture (IP to host), opening an IP channel does the following:

1. Selects `bmdVideoConnectionEthernet` for video input.
2. Applies the configured PTP and Ethernet connector settings.
3. For each configured video, audio, or ancillary peer SDP, probes flows of
   that media type with `IDeckLinkIPFlowSetting::SetString` using
   `bmdDeckLinkIPFlowPeerSDP`. A flow that returns `S_OK` is treated as the
   receiver flow.
4. Enables the receiver flows.
5. Enters the normal capture path: `SetCallback` followed by
   `EnableVideoInput` with format detection.

Video peer SDP is required. Audio and ancillary peer SDPs are optional because
the current Nodos node ABI carries video frames only; when supplied, their
receiver flows are configured and enabled as well.

For playback (host to IP), opening an IP channel does the following:

1. Selects `bmdVideoConnectionEthernet` for video output and applies the
   configured Ethernet settings.
2. Calls the existing `EnableVideoOutput` path first.
3. Reads `bmdDeckLinkIPFlowSDP` from each flow's
   `IDeckLinkIPFlowStatus`. A media-type SDP containing the corresponding SDP
   media section identifies a sender flow.
4. Enables the sender flows, then leaves frame scheduling unchanged.

The sender SDP is available after `OpenChannel` succeeds through the appended
`nosDeckLinkSubsystem::GetIPFlowSDP` function. The caller supplies a character
buffer, and the result includes a null terminator.

## Settings

`nosDeckLinkSubsystem/Config/Settings.json` accepts the following additional
top-level field. The FlatBuffers fields were appended, so existing settings
files that contain only `sdi_port_mappings` remain valid.

```json
{
  "sdi_port_mappings": [],
  "ip_device_settings": [
    {
      "model_name": "DeckLink IP 100G",
      "persistent_id": -1,
      "ptp_domain": 127,
      "ethernet_connectors": [
        {
          "connector_index": 0,
          "use_dhcp": false,
          "static_local_ip_address": "192.168.10.20",
          "static_subnet_mask": "255.255.255.0",
          "static_gateway_ip_address": "192.168.10.1",
          "video_output_address": "239.10.10.20",
          "audio_output_address": "239.10.10.21",
          "ancillary_output_address": "239.10.10.22"
        }
      ],
      "video_peer_sdp": "",
      "audio_peer_sdp": "",
      "ancillary_peer_sdp": ""
    }
  ]
}
```

`model_name` matches the base model and its numbered sub-device suffix, so
`DeckLink IP 100G` matches `DeckLink IP 100G (1)` through `(8)`. Set
`persistent_id` to a non-negative value to narrow a setting to one device.
There are two Ethernet connector slots, numbered 0 and 1. The current SDK
exposes the connector-scoped address values as the parameterized Ethernet
configuration IDs; the subsystem uses those IDs with the connector index.

The PTP domain is validated to the SDK range 0-127 and defaults to 127. Each
peer SDP must be shorter than 1000 bytes. IP addresses configure the card's
internal media ports through the DeckLink SDK; they do not create or require a
Windows host NIC or host routing entry.

## Field-verified device quirks

1. A DeckLink IP 100G enumerates as eight sub-devices named `DeckLink IP 100G
   (1..8)`. Each exposes three sender and three receiver flows: video, audio,
   and ancillary.
2. After the last real flow, some firmware exposes a null-entry whose
   attributes query returns a null COM pointer. Calling `Next()` again can
   hang forever. Enumeration is therefore capped at six flows and stops on
   the first failed attributes query.
3. The SDK documents input direction as 1 and output as 0, but firmware can
   report the opposite in places. Receiver/sender selection uses SDP setting
   and status capability probes instead of trusting that enum.
4. Peer SDP is limited to fewer than 1000 bytes.
5. The PTP domain defaults to 127, matching the field network grandmaster.
   The card locks automatically when PTP is present on the link.
6. The QSFP media ports are card-internal 100G ports, not Windows NICs; all
   IP configuration goes through `IDeckLinkConfiguration`.
7. Callbacks execute on a driver thread. Existing plain callback objects with
   explicit `AddRef`/`Release` ownership are retained.
8. 1080p50 10-bit YUV 4:2:2 is a known working mode with GPM/TPN narrow
   packing, BT.709 or BT.2020, PT96, and ports 5004/10000.

## Current limitations

- IP configuration and input peer SDP are exposed through `Settings.json`; no
  new node pins were needed because IP channels are returned by the existing
  channel pin (`IP 1` through `IP 8`).
- The existing node graph transports video only. Audio and ancillary flows
  can be configured and enabled, but their packet payloads are not yet
  exposed as Nodos node data.
- Input peer SDP is currently supplied manually. Sender SDP is exposed after
  output opening, but no NMOS/SDP exchange layer is added.
