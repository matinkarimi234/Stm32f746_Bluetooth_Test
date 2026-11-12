namespace Connection.Bluetooth;

internal interface IPlatformRfcomm
{
    Task<IReadOnlyList<BluetoothDeviceInfo>> GetPairedDevicesAsync(CancellationToken cancellationToken);
    Task OpenAsync(BluetoothDeviceInfo device, CancellationToken cancellationToken);
    Task<int> ReadAsync(byte[] buffer, CancellationToken cancellationToken);
    Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken cancellationToken);
    Task CloseAsync();
}
