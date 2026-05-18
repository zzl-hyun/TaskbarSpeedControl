using System.Threading;

namespace TaskbarAutoHideSpeed;

public sealed class SingleInstance : IDisposable
{
    private readonly Mutex _mutex;
    private readonly bool _createdNew;

    public SingleInstance(string name)
    {
        _mutex = new Mutex(initiallyOwned: true, name: name, createdNew: out _createdNew);
    }

    public bool TryEnter() => _createdNew;

    public void Dispose()
    {
        if (_createdNew)
        {
            _mutex.ReleaseMutex();
        }

        _mutex.Dispose();
    }
}
