#if ANDROID
using Android.App;
using Android.Bluetooth;
using Android.Content;
using Android.OS;
using Java.Util;

namespace Connection.Bluetooth;

internal sealed class AndroidRfcomm : IPlatformRfcomm
{
    private const string SppUuid = "00001101-0000-1000-8000-00805F9B34FB";
    private BluetoothSocket? _socket;

    private static BluetoothAdapter Adapter
    {
        get
        {
            BluetoothManager? manager = Application.Context.GetSystemService(Context.BluetoothService) as BluetoothManager;
            return manager?.Adapter ?? throw new InvalidOperationException("No Bluetooth adapter is available.");
        }
    }

    private static void EnsurePermission()
    {
        if (Build.VERSION.SdkInt >= BuildVersionCodes.S &&
            Application.Context.CheckSelfPermission(Android.Manifest.Permission.BluetoothConnect) != Android.Content.PM.Permission.Granted)
            throw new UnauthorizedAccessException("Nearby devices permission is required. Grant Bluetooth access in the app.");
    }

    public Task<IReadOnlyList<BluetoothDeviceInfo>> GetPairedDevicesAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        EnsurePermission();
        var adapter = Adapter;
        if (!adapter.IsEnabled) throw new InvalidOperationException("Enable Bluetooth in Android Settings first.");
        var found = adapter.BondedDevices?
            .Where(x => x is not null)
            .Select(x => new BluetoothDeviceInfo(x.Address ?? string.Empty, x.Name ?? "Unnamed device"))
            .OrderBy(x => x.Name, StringComparer.OrdinalIgnoreCase)
            .ToArray() ?? Array.Empty<BluetoothDeviceInfo>();
        return Task.FromResult<IReadOnlyList<BluetoothDeviceInfo>>(found);
    }

    public async Task OpenAsync(BluetoothDeviceInfo device, CancellationToken cancellationToken)
    {
        EnsurePermission();
        await CloseAsync().ConfigureAwait(false);
        var adapter = Adapter;
        if (!adapter.IsEnabled) throw new InvalidOperationException("Bluetooth is disabled.");
        var remote = adapter.GetRemoteDevice(device.Id) ?? throw new IOException("Device not found.");
        // Android's RFCOMM connect is blocking. Never call on the UI thread.
        var socket = remote.CreateRfcommSocketToServiceRecord(UUID.FromString(SppUuid))
            ?? throw new IOException("Unable to create RFCOMM socket.");
        _socket = socket;
        using var cancel = cancellationToken.Register(() => { try { socket.Close(); } catch { } });
        try { await Task.Run(() => socket.Connect(), cancellationToken).ConfigureAwait(false); }
        catch { await CloseAsync().ConfigureAwait(false); throw; }
    }

    public async Task<int> ReadAsync(byte[] buffer, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var stream = _socket?.InputStream ?? throw new IOException("RFCOMM socket closed.");
        return await Task.Run(() => stream.Read(buffer, 0, buffer.Length), cancellationToken).ConfigureAwait(false);
    }

    public async Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var stream = _socket?.OutputStream ?? throw new IOException("RFCOMM socket closed.");
        byte[] bytes = data.ToArray();
        await Task.Run(() => { stream.Write(bytes, 0, bytes.Length); stream.Flush(); }, cancellationToken).ConfigureAwait(false);
    }

    public Task CloseAsync()
    {
        var old = Interlocked.Exchange(ref _socket, null);
        if (old is not null)
        {
            try { old.Close(); } catch { }
            old.Dispose();
        }
        return Task.CompletedTask;
    }
}
#endif
