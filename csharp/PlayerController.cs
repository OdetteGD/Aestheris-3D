using System;

namespace Aestheris.Scripting;

public sealed class PlayerController : AestherisComponent
{
    private float yawDegrees;
    private float targetScale = 1.0f;
    private float velocityX;
    private float velocityZ;

    public override void OnStart()
    {
        NodeHandle = FindNodeHandle(4u);

        if (NodeHandle == 0u)
        {
            Console.WriteLine("Aetheris PlayerController: target node unavailable.");
            return;
        }

        if (AetherisNative.GetNodePosition(
                NodeHandle,
                out _,
                out _,
                out _))
        {
            Console.WriteLine(
                $"Aetheris PlayerController started on node {NodeHandle}.");
        }
    }

    public override void OnUpdate(float deltaTime)
    {
        if (NodeHandle == 0u || deltaTime <= 0.0f)
        {
            return;
        }

        int phase = AetherisInput.GetTouchState(
            out float deltaX,
            out float deltaY);

        if (phase == 0)
        {
            velocityX *= MathF.Pow(0.05f, deltaTime);
            velocityZ *= MathF.Pow(0.05f, deltaTime);

            if (MathF.Abs(velocityX) > 0.0001f ||
                MathF.Abs(velocityZ) > 0.0001f)
            {
                ApplyVelocity(deltaTime);
            }

            return;
        }

        velocityX = deltaX * 5.0f;
        velocityZ = -deltaY * 5.0f;

        if (!AetherisNative.GetNodePosition(
                NodeHandle,
                out float positionX,
                out float positionY,
                out float positionZ))
        {
            return;
        }

        positionX += velocityX * deltaTime;
        positionZ += velocityZ * deltaTime;

        yawDegrees += deltaX * 0.45f;
        targetScale = Math.Clamp(
            targetScale + (-deltaY * 0.0025f),
            0.35f,
            2.5f);

        float radians = yawDegrees * (MathF.PI / 180.0f);
        float half = radians * 0.5f;
        float halfSine = MathF.Sin(half);
        float halfCosine = MathF.Cos(half);

        AetherisNative.SetNodePosition(
            NodeHandle,
            positionX,
            positionY,
            positionZ);

        AetherisNative.SetNodeRotation(
            NodeHandle,
            0.0f,
            halfSine,
            0.0f,
            halfCosine);

        AetherisNative.SetNodeScale(
            NodeHandle,
            targetScale,
            targetScale,
            targetScale);
    }

    private void ApplyVelocity(float deltaTime)
    {
        if (!AetherisNative.GetNodePosition(
                NodeHandle,
                out float x,
                out float y,
                out float z))
        {
            return;
        }

        AetherisNative.SetNodePosition(
            NodeHandle,
            x + velocityX * deltaTime,
            y,
            z + velocityZ * deltaTime);
    }
}
