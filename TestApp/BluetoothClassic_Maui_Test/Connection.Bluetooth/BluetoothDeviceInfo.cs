namespace Connection.Bluetooth;

public sealed record BluetoothDeviceInfo(string Id, string Name)
{
    public string DisplayName => $"{Name}  ({Id})";
}
