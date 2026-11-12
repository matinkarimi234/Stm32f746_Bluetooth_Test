# Bluetooth Classic Test — two-project .NET 10 MAUI solution

## Architecture

- **Connection.Bluetooth** — reusable, UI-independent Bluetooth Classic RFCOMM/SPP transport. Paired-device enumeration, connection, RX byte events, TX bytes, explicit disconnect. Android BluetoothSocket; Windows RFCOMM WinRT StreamSocket.
- **BluetoothTest.Maui** — thin Android/Windows UI. Refresh paired devices, select, connect, send UTF-8 or HEX, inspect RX HEX/text and events.

Does NOT implement BLE or Bluetooth GATT. The protocol is an unframed binary byte stream. Do not treat one RX event as one application frame; `Device.Protocol` in the Resistivity Meter application should continue doing framing.

## Build

Prerequisites: Visual Studio 2022 with .NET 10, .NET MAUI, Android SDK. Windows testing requires Windows 10/11 and matching SDK. `dotnet workload install maui` may be needed.

Open `BluetoothTest.sln` and select `BluetoothTest.Maui` as startup project. Select your Android device (recommended first) or Windows Machine. The project targets `net10.0-android` and, on Windows, `net10.0-windows10.0.19041.0`.

`dotnet build BluetoothTest.Maui/BluetoothTest.Maui.csproj -f net10.0-android`

The library targets Android and Windows only; it is deliberately not a net10.0 UI-neutral reference assembly (because it directly uses platform SDKs).

## Test steps

1. Pair your Android phone with the STM32 Bluetooth dongle in Android Settings. On Android 12+, grant **Nearby devices** permission when prompted.
2. Launch app, press **Refresh**. Choose paired device from picker.
3. Press **Connect**. This tests the standard Bluetooth Serial Port Profile service UUID `00001101-0000-1000-8000-00805F9B34FB`.
4. Once connected, send `Hello STM32` or enter `55 01 00 00 00 00 00 56` and press **Send HEX**. Inspect RX and STM32 diagnostics.
5. Disconnect and reconnect, then power-cycle the STM32 and retry.

### Important firmware requirement

Your STM32 test firmware currently supports USB HCI, pairing, ACL RX/TX, and early L2CAP SDP channel signaling. **It has not yet implemented SDP service registration and the RFCOMM SPP server.** Until these are in place, app discovery can work and pairing can succeed, but `Connect` can fail, or Windows may show no SPP service. This is EXPECTED; the test app cannot create the missing STM32 service.

For a quick real-world end-to-end test, next implement on STM32: finish L2CAP SDP channel, SDP ServiceSearchAttributeResponse advertising UUID 0x1101/channel 1, establish RFCOMM PSM 0x0003 channel, perform SABM/UA and PN/MSC credit negotiation, then echo RFCOMM user payload back to the phone. Add command_rx only after this transport-level test passes.

### Android permission & discovery strategy

This MVP lists **paired devices only**. It does not perform active radio discovery. Android 12+ requires BLUETOOTH_CONNECT permission; the UI asks for Nearby devices permission. Pair devices first in Android Settings. No location or BLUETOOTH_SCAN permission is needed for this paired-device workflow.

### Windows

The library queries paired BR/EDR BluetoothDevice instances, then queries SDP for the RFCOMM Serial Port service. The Windows package manifest includes Bluetooth capability; this app is configured as unpackaged (`WindowsPackageType=None`). On some environments additional packaged application capability setup is needed for WinRT RFCOMM access; test Windows after Android.

## Integration with Resistivity Meter

The reviewed app (master commit `91d3c3a`, 2026-11-10) has:
- `Device.Connection/DeviceConnection.cs`: abstract discovery / transport I/O and handshake
- `Connection.AndroidUsb/AndroidUsbConnection.cs`
- `Connection.WindowsVcp/WindowsVcpConnection.cs`
- `Device.Protocol` (framing/commands)

Keep `Device.Protocol` unchanged. Add `Connection.Bluetooth` as a project to the main solution and a **BluetoothDeviceConnection : DeviceConnection** adapter in the main app or in a new integration project. The adapter maps `DiscoverDevicesAsync`, `OpenDeviceAsync`, `ReadBytesAsync`, `WriteBytesAsync` and `CloseDeviceAsync` onto the reusable library. Add a transport selector instead of replacing the USB registration. Use one active `DeviceConnection` in `DeviceConnectionService`; share the existing handshake, `RequestRawAsync`, and synchronization code.

For production: design secure bonding and persistent link-key storage on the STM32, reconnect behavior, meaningful timeouts, input validation, and authentication policy. Just Works pairing has no MITM protection.

## Current validation

The source was generated from official platform API contracts and reviewed structurally, but **not compiled here** (this environment has no .NET 10/MAUI SDK or Windows/Android hardware). Treat the project as a hardware-test starter and report any exact compiler messages for corrections.
