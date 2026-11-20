using UnityEngine;
using UnityEngine.InputSystem;

// Viewer only. Does not modify the supplied city generator or its random state.
public sealed class CityFlyCamera : MonoBehaviour
{
    public float speed = 22f;
    private float yaw, pitch;
    private int moveId = -1, lookId = -1;
    private Vector2 moveOrigin, lookPrevious;

    private void Start()
    {
        yaw = transform.eulerAngles.y;
        pitch = transform.eulerAngles.x;
        Application.targetFrameRate = 60;
    }

    private void OnDisable() { moveId = lookId = -1; }

    private void Update()
    {
        Vector3 input = Vector3.zero;
        var keyboard = Keyboard.current;
        if (keyboard != null)
        {
            input.x = (keyboard.dKey.isPressed ? 1 : 0) - (keyboard.aKey.isPressed ? 1 : 0);
            input.z = (keyboard.wKey.isPressed ? 1 : 0) - (keyboard.sKey.isPressed ? 1 : 0);
            input.y = (keyboard.eKey.isPressed ? 1 : 0) - (keyboard.qKey.isPressed ? 1 : 0);
        }
        var mouse = Mouse.current;
        if (mouse != null && mouse.rightButton.isPressed)
        {
            Vector2 delta = mouse.delta.ReadValue();
            yaw += delta.x * .12f; pitch -= delta.y * .12f;
        }
        var screen = Touchscreen.current;
        bool moving = false, looking = false;
        if (screen != null)
        {
            foreach (var touch in screen.touches)
            {
                if (!touch.press.isPressed) continue;
                int id = touch.touchId.ReadValue();
                Vector2 p = touch.position.ReadValue();
                bool began = touch.phase.ReadValue() == UnityEngine.InputSystem.TouchPhase.Began;
                if (began && p.x < Screen.width * .35f && moveId < 0)
                { moveId = id; moveOrigin = p; }
                else if (began && p.x > Screen.width * .65f && lookId < 0)
                { lookId = id; lookPrevious = p; }
                if (id == moveId)
                {
                    moving = true;
                    Vector2 axis = Vector2.ClampMagnitude((p - moveOrigin) / (Screen.height * .16f), 1);
                    input.x += axis.x; input.z += axis.y;
                }
                else if (id == lookId)
                {
                    looking = true;
                    Vector2 delta = p - lookPrevious;
                    yaw += delta.x * .12f; pitch -= delta.y * .12f; lookPrevious = p;
                }
                else if (p.x >= Screen.width * .35f && p.x <= Screen.width * .65f)
                    input.y += p.y > Screen.height * .5f ? 1 : -1;
            }
        }
        if (!moving) moveId = -1;
        if (!looking) lookId = -1;
        pitch = Mathf.Clamp(pitch, -89, 89);
        transform.rotation = Quaternion.Euler(pitch, yaw, 0);
        Vector3 horizontal = Quaternion.Euler(0, yaw, 0) * new Vector3(input.x, 0, input.z);
        Vector3 direction = Vector3.ClampMagnitude(horizontal + Vector3.up * input.y, 1);
        transform.position += direction * speed * Time.deltaTime;
    }

    private void OnGUI()
    {
        float scale = Mathf.Max(.65f, Screen.height / 600f);
        GUI.matrix = Matrix4x4.Scale(new Vector3(scale, scale, 1));
        float w = Screen.width / scale, h = Screen.height / scale;
        GUI.Box(new Rect(12, 12, Mathf.Min(w - 24, 650), 58),
            "ГОРОД ИЗ ИСХОДНОГО C# · СВОБОДНЫЙ ПОЛЁТ\nПК: WASD, Q/E, правая кнопка мыши · телефон: зоны ниже");
        GUI.Box(new Rect(12, h - 100, w * .30f, 85), "СЛЕВА: ДВИЖЕНИЕ\nКоснитесь и ведите палец");
        GUI.Box(new Rect(w * .68f, h - 100, w * .30f - 12, 85), "СПРАВА: ОБЗОР\nКоснитесь и ведите палец");
        GUI.Box(new Rect(w * .38f, 90, w * .24f, 55), "ВВЕРХ\nУдерживайте центр сверху");
        GUI.Box(new Rect(w * .38f, h - 80, w * .24f, 65), "ВНИЗ\nУдерживайте центр снизу");
    }
}
