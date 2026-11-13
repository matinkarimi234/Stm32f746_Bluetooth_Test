# Android / STM32 SPP connection debugging

The working tree was inspected on branch `feature/bluetooth-spp-bonds`, based on
`f05f0c8`. Existing uncommitted SPP, bonding, ACL, linker, and UI work was retained.
The build/test checks below are separate from hardware verification. After the
fixes, the user reported that the phone/board setup is working. Repeated reconnect,
long transfers, and bond persistence still need their individual hardware tests.

## What was wrong

Pairing establishes security credentials. It does not establish an RFCOMM serial
stream. Android's socket error is a consequence of the stream not opening, not a
specific diagnosis of the failing protocol stage.

* `Core/Src/bt_acl_diag.c`, `BT_ACL_Process`: on Bulk IN NOTREADY the receiver
  scheduled a new receive. The actual HAL in
  `Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_hcd.c`,
  `HCD_HC_IN_IRQHandler`, already reactivates control/bulk IN after NAK. A packet
  arriving between that NAK observation and resubmission could have its DONE
  status and byte count overwritten. The receiver now keeps polling the same
  transfer. Interrupt IN remains different: its NAK path halts and needs another
  submission. USB reception also stays alive between Bluetooth connections.
* `Core/Src/usbh_bluetooth.c`, `BT_HCI_QueueCommand` / `BT_ProcessHCICommand`:
  an earlier state machine could enqueue a command over an OK result before a
  later owner consumed it. A pairing reply could remain in WAIT with the wrong
  opcode. Results are now held through the next complete scheduler pass.
  Completed controller errors are released after observation; USB transport
  errors remain visible rather than being blindly retried.
* `BT_QueuePairReply` previously discarded any security request while another
  reply was pending. Four replies can now wait behind the active one. The
  existing explicit development pairing policy is unchanged. Accept Connection
  Request now uses role parameter 1, matching its existing no-role-switch comment.
* `Core/Src/bt_spp.c`, `BT_SPP_Send` / default `BT_SPP_DataReceived`: echo bytes
  were discarded when peer transmit credits were zero. A bounded 1024-byte serial
  queue now holds accepted bytes until credits arrive, reserves control queue
  capacity, and limits renewed receive credits to available space. Oversized
  application writes return 0 atomically. Echo overflow has its own counter.
* SDP search previously scanned substrings and accepted any matching UUID from a
  multi-UUID pattern. It now checks complete 16/32/128-bit UUIDs and AND matching,
  binds continuation offsets to the active request, rejects invalid handles, and
  respects the peer L2CAP MTU when splitting attribute responses. The existing
  record's protocol-descriptor lengths and server channel 1 were checked by tests;
  they were not assumed to be the original failure.
* `Core/Src/bt_acl_tx.c` preserves a still-live USB OUT buffer during disconnect
  before dropping old queued data, including reconnects that reuse the same HCI
  handle. Endpoint toggles are not reset on ordinary Bluetooth disconnection.
* `Core/Src/bt_bonds.c`, `BT_Bonds_Process`: recovering an erased journal hole
  followed by occupied slots could lead to writing an occupied next slot. Every
  candidate is now checked for full erasure. Failed writes retain pending data
  for a fresh-slot retry. Full journals stay full without automatic erasure.
  Saving is deferred while connected, accepting a link, or servicing HCI commands.
* `Connection.Bluetooth/AndroidRfcomm.cs` converts socket-close errors caused by
  cancellation into cancellation. `BluetoothClassicClient.cs` lets Disconnect
  cancel an in-progress Connect, captures the session token, and closes transport
  resources when the receive loop exits. The secure SPP UUID is unchanged:
  `00001101-0000-1000-8000-00805F9B34FB`.

The earlier observation with SDP traffic and the latest observation with only an
Information Request are different attempts. The receive race is reproduced by a
host regression, and can explain intermittent progress, but the precise cause of
that phone's final timeout still needs a new hardware trace. The existing
Extended Features response (type 2, result 0, four zero feature bytes) is valid for
this Basic Mode implementation; advertising unsupported ERTM/fixed channels is
not a justified fix. See the [Bluetooth L2CAP specification](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-62/out/en/host/logical-link-control-and-adaptation-protocol-specification.html).

## Keil watch setup

`MDK-ARM/watch.ini` adds Watch 1 (HCI/security/bonds) and Watch 2
(SDP/RFCOMM/ACL/trace) without deleting existing watch expressions. It contains no
execution commands or writes. The local ignored `.uvoptx` target initialization
file is set to `.\watch.ini`. If Keil was already open, select that file under
**Options for Target > Debug > Initialization File**, or run this in the debugger
Command window, with the project directory as working directory:

```text
INCLUDE .\watch.ini
```

For a different working directory, use the full quoted path. WatchSet syntax is
from [Keil's WatchSet documentation](https://www.keil.com/support/man/docs/uv4cl/uv4cl_cm_watchset.htm).
Rebuild/download the updated firmware before loading the expressions. Expand
`BT_SPP_Debug` and `BT_SPP_Trace`. Enable periodic window updates while the board
runs. Avoid breakpoints/halting during pairing or connecting: the USB host and
Bluetooth signaling depend on the running main loop.

| Stage / watch expression | Expected during a successful test |
| --- | --- |
| `BT_InitState`, `BT_SSPState`, `BT_ScanState` | 9, 7, 5 respectively |
| `BT_ACL_TX_BufferState` | 3 (controller buffer sizes known) |
| `BT_Connected`, `BT_ConnState`, `BT_ConnectionHandle` | 1, 4, valid handle (not 0xFFFF) |
| `BT_RemoteBD_ADDR` | Selected phone's address in display order |
| `BT_AuthCompleteStatus`, `BT_AuthState` | 0, 4 after an observed successful authentication |
| `BT_ConnectionEncrypted` | 1 after the secure RFCOMM socket enables encryption |
| `BT_SPP_Debug.info_type`, `.info_result` | 2, 0 if Extended Features was requested |
| `BT_SPP_SDPState` | 1 while configuring, then 2; may return to 0 normally after discovery |
| `BT_SPP_SDP_OurConfigOK`, `BT_SPP_SDP_PeerConfigOK` | Both 1 while SDP channel is open |
| `BT_SPP_SDPPackets`, `BT_SPP_SDPResponses` | Increase during discovery (phone caching can alter the sequence) |
| `BT_SPP_Debug.sdp_request`, `.sdp_response`, `.sdp_tid` | Often 0x06 / 0x07; 0x02 / 0x03 and 0x04 / 0x05 are also supported |
| `BT_SPP_RFCOMMState` | 2 (L2CAP open) |
| `BT_SPP_Debug.rfcomm_our_config`, `.rfcomm_peer_config` | Both 1 |
| `BT_SPP_Debug.sabm_rx`, `.ua_tx`, `.pn_rx`, `.msc_rx` | Increase as mux/DLC negotiation progresses |
| `BT_SPP_SerialOpen` | 1; this is the serial-ready indicator, unlike `BT_Connected` |
| `BT_SPP_DLCI` | Normally 2 for server channel 1 when phone initiates |
| `BT_SPP_RxBytes`, `BT_SPP_TxBytes` | Both increase by the number of echoed bytes |
| `BT_SPP_Debug.serial_pending`, `.queue_depth` | Return to 0 after sending, assuming peer grants credits |
| `BT_SPP_Debug.echo_drops`, `BT_SPP_QueueDrops` | Stay 0 in the test |
| `BT_ACL_TX_FragmentCount`, `BT_ACL_TX_USBDoneCount`, `BT_ACL_TX_CompletedPacketCount` | Increase; need not match immediately because completion is asynchronous |
| `BT_ACL_USBErrorCount`, `BT_ACL_L2CAPErrorCount`, `BT_ACL_TX_ErrorCount` | Stay 0 |
| `BT_Bonds_Reused` | Increases on a Link Key Request satisfied from saved bonds |
| `BT_Bonds_Dirty`, `BT_Bonds_Writes` | Dirty clears and writes increase after a new bond is saved while disconnected |

Counters are cumulative since dongle initialization/MCU restart, with SPP and HCI
trace history retained across ordinary ACL disconnects. Use before/after deltas.
Some last-status fields intentionally retain 0xFF until an event is observed.
`BT_SPP_TxBytes` counts bytes accepted by the RFCOMM frame queue, not proof that
Android received them. Controller completed packets likewise do not prove that
Android accepted SDP or RFCOMM.

The 32-slot `BT_SPP_Trace` ring stores sequence, direction (0 RX / 1 queued TX),
protocol (1 L2CAP signaling / 2 SDP / 3 RFCOMM), code, identifier, CID, length,
and status. For RFCOMM, identifier holds the DLCI; for SDP, status holds transaction
ID (or an SDP error on error responses). For L2CAP RX, status holds its first
16-bit argument. The latest slot is `(BT_SPP_TraceNext + 31) % 32`; sort by
sequence to read across wraparound. TX signaling and SDP events mean a response
was queued. `BT_SPP_Debug.last_close`: 0x05 ACL link reset, 0x06 L2CAP disconnect,
0x43 RFCOMM DISC. `BT_DisconnectReason` retains the controller reason. The separate
`BT_TraceCode/Opcode/Status` arrays retain HCI event history. No trace stores keys;
raw event watch buffers are suppressed/redacted where keys could appear.

## Next hardware test

1. Build and program the firmware. For bond preservation, use sector erase for
   the application image, not full-chip erase. The existing scatter layout keeps
   the application below `0x080C0000`; sector 7 holds the bond journal. Ordinary
   reconnect/reset does not erase it. A deliberately full journal requires an
   explicit maintenance operation, not an automatic erase.
2. Install the rebuilt Android app. Start the MCU with its dongle, run freely,
   and select the paired Resistivity Meter. Try the existing bond first.
3. Press Connect once. The app has a 20-second timeout. On success verify
   `BT_SPP_SerialOpen == 1` and the UI says RFCOMM connected.
4. Send ASCII `Hello STM32` (11 bytes), then HEX `00 01 7F 80 FE FF` (6 bytes).
   The app should receive the exact bytes in each case. RFCOMM is a byte stream:
   one send can be split across multiple receive log entries.
5. Disconnect/reconnect at least five times. Check errors/drops and byte deltas.
   Cancel one pending Connect with Disconnect and check a subsequent attempt.
6. After first pairing, disconnect and wait for `BT_Bonds_Dirty == 0`. Restart
   the MCU without reprogramming, reconnect, and inspect `BT_Bonds_Reused`.
   Do not erase bonds to investigate an ordinary SPP failure. If authentication
   specifically reports a stale/missing key, address that separately.
7. If Connect fails, keep the MCU running until the phone times out, then inspect
   the retained rings. Capture `BT_SPP_Debug`, `BT_SPP_Trace`, HCI trace arrays,
   `BT_DisconnectReason`, the configuration-response diagnostics, and ACL counters.
   If it still stops at Information Response, obtain Android Bluetooth HCI snoop
   output for that same attempt to check peer receipt/authentication and the next
   phone action. Do not combine counters from different attempts.

## Validation and limits

Run native tests from the repository root:

```powershell
# Requires Python and Zig. A portable compiler can be installed in ignored bin/:
python -m pip install --target Firmware/Tests/bin/python_deps ziglang
& Firmware/Tests/run_tests.ps1

dotnet run --project TestApp/BluetoothClassic_Maui_Test/Connection.Bluetooth.Tests/Connection.Bluetooth.Tests.csproj
```

Native tests compile the production SDP/RFCOMM and ACL modules with USB mocks.
The HCI test extracts the exact production command/pairing functions and mocks
only their surrounding state/transport; it does not simulate the full controller.
The bond test maps a test-only flash region and rejects programming occupied
words. Covered cases include the NAK/DONE loss race, asymmetric CIDs, both config
directions, Information Responses, SDP record lengths/continuation/UUID matching,
known FCS vectors, PN/MSC, credit-starved exact byte echo, disconnect/reconnect,
ACL fragmentation/reassembly/controller credits/OUT retry, pending OUT at handle
reuse, command-result ownership, queued pairing replies, and journal recovery.
The original NAK receive behavior was independently restored in a temporary test
copy and failed the regression; the corrected production version passes.
The .NET test checks pending-connect cancellation, cleanup, exact writes, and
repeated sessions with a mocked transport.

Local validation results: all four native suites and the .NET lifecycle suite
pass. The final Keil ARM Compiler 5.06 build has zero errors and one existing
unused-variable warning (`BT_EchoTestQueued` in `main.c`). The complete MAUI
Android/Windows build has zero errors and two Android platform-analyzer warnings
(CA1416 on the existing Nearby Devices permission declarations/check). The map
confirms load region maximum `0x000C0000`; the bond sector is outside that region.
`Firmware/Tests/bin/keil-build.log` retains the final firmware build output. Native mocks do not verify dongle radio behavior, USB bus timing, phone
service acceptance, or on-device echo. This remains a bounded single-ACL,
single-service Basic Mode server, without ERTM, multiple serial DLCs, arbitrary
large MCC messages, or a complete Bluetooth qualification test suite. If a USB
pipe actually stalls/errors, the error remains visible and needs recovery rather
than assuming success. The user has confirmed that the hardware setup now works; the native tests
do not independently verify that report or the remaining hardware test cases.

Baseline commit message:

```text
Fix Bluetooth ACL receive race and SPP reconnect/echo handling; add Keil diagnostics
```
