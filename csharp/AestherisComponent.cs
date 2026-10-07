namespace Aestheris.Scripting;

public abstract class AestherisComponent
{
    public ulong NodeHandle { get; internal set; }

    public virtual void OnStart()
    {
    }

    public virtual void OnUpdate(float deltaTime)
    {
    }

    protected static ulong FindNodeHandle(uint entityIndex)
    {
        return AetherisNative.GetNodeHandle(entityIndex);
    }
}
