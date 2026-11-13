using Connection.Bluetooth;

static void Check(bool value) { if (!value) throw new Exception("Lifecycle assertion failed"); }
var device = new BluetoothDeviceInfo("test", "Mock RFCOMM");
var transport = new FakeTransport();
await using var client = new BluetoothClassicClient(transport);
var pending = client.ConnectAsync(device);
await transport.OpenStarted.Task.WaitAsync(TimeSpan.FromSeconds(2));
await client.DisconnectAsync().WaitAsync(TimeSpan.FromSeconds(2));
try { await pending; throw new Exception("Expected cancelled connect"); }
catch (OperationCanceledException) { }
Check(!client.IsConnected);
transport.BlockOpen = false;
for (int i = 0; i < 3; i++)
{
    await client.ConnectAsync(device);
    Check(client.IsConnected);
    await client.SendAsync(new byte[] { 0, 1, 128, 255 });
    Check(transport.LastWrite.SequenceEqual(new byte[] { 0, 1, 128, 255 }));
    await client.DisconnectAsync().WaitAsync(TimeSpan.FromSeconds(2));
    Check(!client.IsConnected);
}
using (var cancel = new CancellationTokenSource())
{
    transport.BlockOpen = true;
    pending = client.ConnectAsync(device, cancel.Token);
    cancel.Cancel();
    try { await pending; throw new Exception("Expected timeout cancellation"); }
    catch (OperationCanceledException) { }
}
Check(!client.IsConnected);
Console.WriteLine("Client: disconnect cancels pending connect, cancellation cleanup, exact writes, repeated sessions passed");

sealed class FakeTransport : IPlatformRfcomm
{
    public bool BlockOpen = true;
    public TaskCompletionSource OpenStarted = new(TaskCreationOptions.RunContinuationsAsynchronously);
    public byte[] LastWrite = [];
    private TaskCompletionSource<int> _read = new(TaskCreationOptions.RunContinuationsAsynchronously);
    public Task<IReadOnlyList<BluetoothDeviceInfo>> GetPairedDevicesAsync(CancellationToken ct) => Task.FromResult<IReadOnlyList<BluetoothDeviceInfo>>([]);
    public async Task OpenAsync(BluetoothDeviceInfo device, CancellationToken ct)
    {
        OpenStarted.TrySetResult();
        if (BlockOpen) await Task.Delay(Timeout.Infinite, ct);
        ct.ThrowIfCancellationRequested();
        _read = new(TaskCreationOptions.RunContinuationsAsynchronously);
    }
    public Task<int> ReadAsync(byte[] b, CancellationToken ct) => _read.Task.WaitAsync(ct);
    public Task WriteAsync(ReadOnlyMemory<byte> bytes, CancellationToken ct) { LastWrite = bytes.ToArray(); return Task.CompletedTask; }
    public Task CloseAsync() { _read.TrySetResult(0); return Task.CompletedTask; }
}
