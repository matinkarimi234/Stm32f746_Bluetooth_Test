namespace Connection.Bluetooth;

/// <summary>
/// Bluetooth Classic RFCOMM/SPP byte transport. No application framing is imposed.
/// Events can arrive on a background thread; UI consumers must marshal to UI.
/// </summary>
public sealed class BluetoothClassicClient : IAsyncDisposable
{
    private readonly IPlatformRfcomm _transport;
    private readonly SemaphoreSlim _lifecycle = new(1, 1);
    private readonly SemaphoreSlim _sendLock = new(1, 1);
    private readonly object _attemptGate = new();
    private CancellationTokenSource? _connectCts;
    private CancellationTokenSource? _receiveCts;
    private Task? _receiveTask;
    private int _connected;

    public bool IsConnected => Volatile.Read(ref _connected) != 0;
    public event EventHandler<byte[]>? DataReceived;
    public event EventHandler<string>? StatusChanged;

    internal BluetoothClassicClient(IPlatformRfcomm transport) => _transport = transport;

    public BluetoothClassicClient()
    {
#if ANDROID
        _transport = new AndroidRfcomm();
#elif WINDOWS
        _transport = new WindowsRfcomm();
#else
        throw new PlatformNotSupportedException("Android and Windows only.");
#endif
    }

    public Task<IReadOnlyList<BluetoothDeviceInfo>> GetPairedDevicesAsync(CancellationToken cancellationToken = default)
        => _transport.GetPairedDevicesAsync(cancellationToken);

    public async Task ConnectAsync(BluetoothDeviceInfo device, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(device);
        await _lifecycle.WaitAsync(cancellationToken).ConfigureAwait(false);
        using var attempt = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        lock (_attemptGate) _connectCts = attempt;
        try
        {
            await StopSessionAsync().ConfigureAwait(false);
            RaiseStatus($"Connecting to {device.Name}...");
            try
            {
                await _transport.OpenAsync(device, attempt.Token).ConfigureAwait(false);
                attempt.Token.ThrowIfCancellationRequested();
                _receiveCts = new CancellationTokenSource();
                Volatile.Write(ref _connected, 1);
                var receiveToken = _receiveCts.Token;
                _receiveTask = Task.Run(() => ReceiveLoopAsync(receiveToken));
                RaiseStatus($"RFCOMM connected: {device.Name}");
            }
            catch
            {
                await _transport.CloseAsync().ConfigureAwait(false);
                RaiseStatus("Connection failed");
                throw;
            }
        }
        finally
        {
            lock (_attemptGate) _connectCts = null;
            _lifecycle.Release();
        }
    }

    public async Task SendAsync(ReadOnlyMemory<byte> data, CancellationToken cancellationToken = default)
    {
        if (data.Length == 0) return;
        await _sendLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (!IsConnected) throw new IOException("RFCOMM is not connected.");
            await _transport.WriteAsync(data, cancellationToken).ConfigureAwait(false);
        }
        finally { _sendLock.Release(); }
    }

    public async Task DisconnectAsync()
    {
        // Interrupt a native Connect before waiting for lifecycle ownership.
        lock (_attemptGate) _connectCts?.Cancel();
        await _lifecycle.WaitAsync().ConfigureAwait(false);
        try { await StopSessionAsync().ConfigureAwait(false); }
        finally { _lifecycle.Release(); }
    }

    private async Task StopSessionAsync()
    {
        Volatile.Write(ref _connected, 0);
        _receiveCts?.Cancel();
        // Closing native socket releases a blocked read on Android/Windows.
        await _transport.CloseAsync().ConfigureAwait(false);
        if (_receiveTask is not null)
        {
            try { await _receiveTask.ConfigureAwait(false); }
            catch (Exception) { /* Error was already reported by receive loop. */ }
        }
        _receiveTask = null;
        _receiveCts?.Dispose();
        _receiveCts = null;
        RaiseStatus("Disconnected");
    }

    private async Task ReceiveLoopAsync(CancellationToken cancellationToken)
    {
        byte[] buffer = new byte[2048];
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                int read = await _transport.ReadAsync(buffer, cancellationToken).ConfigureAwait(false);
                if (read <= 0) throw new EndOfStreamException("Remote RFCOMM stream closed.");
                DataReceived?.Invoke(this, buffer.AsSpan(0, read).ToArray());
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested) { }
        catch (Exception ex)
        {
            if (!cancellationToken.IsCancellationRequested)
                RaiseStatus($"Receive stopped: {ex.Message}");
        }
        finally
        {
            Volatile.Write(ref _connected, 0);
            await _transport.CloseAsync().ConfigureAwait(false);
        }
    }

    private void RaiseStatus(string status) => StatusChanged?.Invoke(this, status);
    public async ValueTask DisposeAsync()
    {
        await DisconnectAsync().ConfigureAwait(false);
        _lifecycle.Dispose();
        _sendLock.Dispose();
    }
}
