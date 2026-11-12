#if ANDROID
using Microsoft.Maui.ApplicationModel;

namespace BluetoothTest.Maui;

public sealed class BluetoothConnectPermission : Permissions.BasePlatformPermission
{
    public override (string androidPermission, bool isRuntime)[] RequiredPermissions =>
        new[] { (Android.Manifest.Permission.BluetoothConnect, true) };
}
#endif
