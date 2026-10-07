namespace Aestheris.Scripting;

public static class ScriptRuntime
{
    private static PlayerController? playerController;

    public static void Initialize()
    {
        playerController = new PlayerController();
        playerController.OnStart();
    }

    public static void Update(float deltaTime)
    {
        playerController?.OnUpdate(deltaTime);
    }
}
