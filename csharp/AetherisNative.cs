using System.Runtime.InteropServices;

namespace Aestheris.Scripting;

internal static class AetherisNative
{
    private const string NativeLibrary = "aetheris3d";

    [DllImport(
        NativeLibrary,
        EntryPoint = "Aetheris_GetNodeHandle",
        CallingConvention = CallingConvention.Cdecl)]
    private static extern ulong NativeGetNodeHandle(uint entityIndex);

    [DllImport(
        NativeLibrary,
        EntryPoint = "Aetheris_SetNodePosition",
        CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeSetNodePosition(
        ulong nodeHandle,
        float x,
        float y,
        float z);

    [DllImport(
        NativeLibrary,
        EntryPoint = "Aetheris_SetNodeRotation",
        CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeSetNodeRotation(
        ulong nodeHandle,
        float x,
        float y,
        float z,
        float w);

    [DllImport(
        NativeLibrary,
        EntryPoint = "Aetheris_SetNodeScale",
        CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeSetNodeScale(
        ulong nodeHandle,
        float x,
        float y,
        float z);

    [DllImport(
        NativeLibrary,
        EntryPoint = "Aetheris_GetNodePosition",
        CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeGetNodePosition(
        ulong nodeHandle,
        out float x,
        out float y,
        out float z);

    internal static ulong GetNodeHandle(
        uint entityIndex)
    {
        return NativeGetNodeHandle(entityIndex);
    }

    internal static bool SetNodePosition(
        ulong nodeHandle,
        float x,
        float y,
        float z)
    {
        return NativeSetNodePosition(
            nodeHandle,
            x,
            y,
            z) != 0;
    }

    internal static bool SetNodeRotation(
        ulong nodeHandle,
        float x,
        float y,
        float z,
        float w)
    {
        return NativeSetNodeRotation(
            nodeHandle,
            x,
            y,
            z,
            w) != 0;
    }

    internal static bool SetNodeScale(
        ulong nodeHandle,
        float x,
        float y,
        float z)
    {
        return NativeSetNodeScale(
            nodeHandle,
            x,
            y,
            z) != 0;
    }

    internal static bool GetNodePosition(
        ulong nodeHandle,
        out float x,
        out float y,
        out float z)
    {
        return NativeGetNodePosition(
            nodeHandle,
            out x,
            out y,
            out z) != 0;
    }
}

internal static class AetherisInput
{
    private const string NativeLibrary = "aetheris3d";

    [DllImport(
        NativeLibrary,
        EntryPoint = "Aetheris_GetTouchState",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern int GetTouchState(
        out float deltaX,
        out float deltaY);
}
