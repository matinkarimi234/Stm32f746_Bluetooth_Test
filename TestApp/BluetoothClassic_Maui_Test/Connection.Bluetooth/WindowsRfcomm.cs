#if WINDOWS
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Rfcomm;
using Windows.Devices.Enumeration;
using Windows.Networking.Sockets;
using Windows.Storage.Streams;

namespace Connection.Bluetooth;

internal sealed class WindowsRfcomm : IPlatformRfcomm
{
    private StreamSocket? _socket;
    private DataReader? _reader;
    private DataWriter? _writer;
    private RfcommDeviceService? _service;
    private BluetoothDevice? _device;

    public async Task<IReadOnlyList<BluetoothDeviceInfo>> GetPairedDevicesAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var devices = await DeviceInformation.FindAllAsync(BluetoothDevice.GetDeviceSelectorFromPairingState(true));
        cancellationToken.ThrowIfCancellationRequested();
        return devices.Select(d => new BluetoothDeviceInfo(d.Id, string.IsNullOrWhiteSpace(d.Name) ? "Unnamed device" : d.Name))
            .OrderBy(d => d.Name, StringComparer.OrdinalIgnoreCase)
            .ToArray();
    }

    public async Task OpenAsync(BluetoothDeviceInfo entry, CancellationToken cancellationToken)
    {
        await CloseAsync();
        cancellationToken.ThrowIfCancellationRequested();
        try
        {
            _device = await BluetoothDevice.FromIdAsync(entry.Id) ?? throw new IOException("Bluetooth device unavailable.");
            cancellationToken.ThrowIfCancellationRequested();
            var services = await _device.GetRfcommServicesForIdAsync(RfcommServiceId.SerialPort, BluetoothCacheMode.Uncached);
            if (services.Error != BluetoothError.Success || services.Services.Count == 0)
                throw new IOException("No Serial Port Profile (UUID 0x1101) SDP service found. STM32 firmware must advertise SDP + RFCOMM first.");
            _service = services.Services[0];
            _socket = new StreamSocket();
            using var registration = cancellationToken.Register(() => { try { _socket?.Dispose(); } catch { } });
            await _socket.ConnectAsync(_service.ConnectionHostName, _service.ConnectionServiceName, SocketProtectionLevel.BluetoothEncryptionAllowNullAuthentication);
            cancellationToken.ThrowIfCancellationRequested();
            _reader = new DataReader(_socket.InputStream) { InputStreamOptions = InputStreamOptions.Partial };
            _writer = new DataWriter(_socket.OutputStream);
        }
        catch { await CloseAsync(); throw; }
    }

    public async Task<int> ReadAsync(byte[] buffer, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var reader = _reader ?? throw new IOException("RFCOMM stream closed.");
        uint count = await reader.LoadAsync((uint)buffer.Length);
        cancellationToken.ThrowIfCancellationRequested();
        if (count == 0) return 0;
        byte[] bytes = new byte[count];
        reader.ReadBytes(bytes);
        bytes.CopyTo(buffer, 0);
        return bytes.Length;
    }

    public async Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var writer = _writer ?? throw new IOException("RFCOMM stream closed.");
        writer.WriteBytes(data.ToArray());
        uint stored = await writer.StoreAsync();
        cancellationToken.ThrowIfCancellationRequested();
        if (stored != data.Length) throw new IOException($"Only {stored}/{data.Length} bytes stored.");
    }

    public Task CloseAsync()
    {
        try { _socket?.Dispose(); } catch { }
        _socket = null;
        try { _reader?.Dispose(); } catch { }
        _reader = null;
        try { _writer?.Dispose(); } catch { }
        _writer = null;
        try { _service?.Dispose(); } catch { }
        _service = null;
        try { _device?.Dispose(); } catch { }
        _device = null;
        return Task.CompletedTask;
    }
}
#endif
