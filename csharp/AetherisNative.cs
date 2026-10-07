using System.Runtime.InteropServices;

namespace Aestheris.Scripting;

internal static class AetherisNative
{
    private const string NativeLibrary = "aetheris3d";

    [DllImport(
        NativeLibrary,
        EntryPoint = "Aetheris_GetNodeHandle",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong GetNodeHandle(uint entityIndex);

    [DllImport(
        NativeLibrary,
        EntryPoint = "Aetheris_SetNodePosition",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern bool SetNodePosition(
        ulong nodeHandle,
        float x,
        float y,
        float z);

    [DllImport(
        NativeLibrary,
        EntryPoint = "Aetheris_SetNodeRotation",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern bool SetNodeRotation(
        ulong nodeHandle,
        float x,
        float y,
        float z,
        float w);

    [DllImport(
        NativeLibrary,
        EntryPoint = "Aetheris_SetNodeScale",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern bool SetNodeScale(
        ulong nodeHandle,
        float x,
        float y,
        float z);

    [DllImport(
        NativeLibrary,
        EntryPoint = "Aetheris_GetNodePosition",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern bool GetNodePosition(
        ulong nodeHandle,
        out float x,
        out float y,
        out float z);
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
