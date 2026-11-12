
using Microsoft.Extensions.DependencyInjection;

namespace BluetoothTest.Maui;

public partial class App : Application
{
    private readonly IServiceProvider _services;

    public App(IServiceProvider services)
    {
        InitializeComponent();
        _services = services;
    }

    protected override Window CreateWindow(IActivationState? activationState)
    {
        MainPage page = _services.GetRequiredService<MainPage>();
        return new Window(page);
    }
}
