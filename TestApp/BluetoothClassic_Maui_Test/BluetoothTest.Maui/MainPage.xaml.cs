using System.Text;
using Connection.Bluetooth;
using Microsoft.Maui.ApplicationModel;

namespace BluetoothTest.Maui;

public partial class MainPage : ContentPage
{
    private readonly BluetoothClassicClient _client;
    private readonly List<string> _messages = new();
    private const int MaxLogLines = 180;

    public MainPage(BluetoothClassicClient client)
    {
        InitializeComponent();
        _client = client;
        _client.StatusChanged += (_, status) => MainThread.BeginInvokeOnMainThread(() => { StatusLabel.Text = status; AddLog(status); });
        _client.DataReceived += (_, data) => MainThread.BeginInvokeOnMainThread(() => AddLog($"RX {data.Length} byte(s) HEX: {Convert.ToHexString(data)} | TEXT: {Encoding.UTF8.GetString(data)}"));
    }

    private async Task EnsurePermissionAsync()
    {
#if ANDROID
        if (Android.OS.Build.VERSION.SdkInt >= Android.OS.BuildVersionCodes.S)
        {
            var result = await Permissions.RequestAsync<BluetoothConnectPermission>();
            if (result != PermissionStatus.Granted) throw new UnauthorizedAccessException("Nearby devices permission denied.");
        }
#endif
    }

    private async void OnRefreshClicked(object sender, EventArgs e)
    {
        try
        {
            await EnsurePermissionAsync();
            var devices = await _client.GetPairedDevicesAsync();
            DevicePicker.ItemsSource = devices.ToList();
            AddLog($"Found {devices.Count} paired device(s).");
        }
        catch (Exception ex) { AddLog("Discovery failed: " + ex.Message); }
    }

    private async void OnConnectClicked(object sender, EventArgs e)
    {
        if (DevicePicker.SelectedItem is not BluetoothDeviceInfo chosen)
        {
            AddLog("Select a paired device first.");
            return;
        }
        try
        {
            await EnsurePermissionAsync();
            await _client.ConnectAsync(chosen);
        }
        catch (Exception ex) { AddLog("Connect failed: " + ex.Message); }
    }

    private async void OnDisconnectClicked(object sender, EventArgs e)
    {
        try { await _client.DisconnectAsync(); }
        catch (Exception ex) { AddLog("Disconnect failed: " + ex.Message); }
    }

    private async void OnSendTextClicked(object sender, EventArgs e)
    {
        await SendBytesAsync(Encoding.UTF8.GetBytes(TxEditor.Text ?? string.Empty));
    }

    private async void OnSendHexClicked(object sender, EventArgs e)
    {
        try
        {
            string input = TxEditor.Text ?? string.Empty;
            var hex = new string(input.Where(c => !char.IsWhiteSpace(c) && c != ':' && c != '-').ToArray());
            await SendBytesAsync(Convert.FromHexString(hex));
        }
        catch (FormatException ex) { AddLog("Invalid hexadecimal input: " + ex.Message); }
    }

    private async Task SendBytesAsync(byte[] data)
    {
        try
        {
            await _client.SendAsync(data);
            AddLog($"TX {data.Length} byte(s): {Convert.ToHexString(data)}");
        }
        catch (Exception ex) { AddLog("Send failed: " + ex.Message); }
    }

    private void OnClearClicked(object sender, EventArgs e)
    {
        _messages.Clear();
        LogEditor.Text = string.Empty;
    }

    private void AddLog(string text)
    {
        _messages.Add($"{DateTime.Now:HH:mm:ss.fff}  {text}");
        if (_messages.Count > MaxLogLines) _messages.RemoveAt(0);
        LogEditor.Text = string.Join(Environment.NewLine, _messages);
    }
}
