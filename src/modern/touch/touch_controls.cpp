#include "touch_controls.hpp"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace th08
{
namespace modern
{
namespace touch
{
namespace
{
const float Pi = 3.14159265358979323846f;

float Clamp(float value, float low, float high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

float Distance(float ax, float ay, float bx, float by)
{
    const float dx = ax - bx, dy = ay - by;
    return sqrtf(dx * dx + dy * dy);
}

// Absolute difference between two angles, in radians, folded to 0..pi.
float AngleDistance(float a, float b)
{
    float difference = fabsf(a - b);
    while (difference > Pi) difference = fabsf(difference - 2.0f * Pi);
    return difference;
}

unsigned AlphaColor(unsigned rgb, float alpha)
{
    const int value = static_cast<int>(Clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (static_cast<unsigned>(value) << 24) | (rgb & 0x00ffffffu);
}
} // namespace

Controls::Controls()
    : overlayVisible(true), surfaceWidth(640), surfaceHeight(480), knobX(0.0f), knobY(0.0f),
      holdX(0.0f), holdY(0.0f), knobValid(false), leftLatched(false), rightLatched(false),
      upLatched(false), downLatched(false), bombPulse(false), bombPulsePending(false)
{
    for (int index = 0; index < MaxFingers; ++index) fingers[index].id = -1;
}

void Controls::SetOverlayVisible(bool visible)
{
    overlayVisible = visible;
    if (!visible) Reset();
}

void Controls::NotifyPhysicalInput()
{
    // A keyboard key or a controller button replaces the on-screen controls;
    // the next touch brings the overlay back.
    if (overlayVisible) SetOverlayVisible(false);
}

void Controls::Configure(const Settings &next)
{
    settings = next;
    settings.scale = Clamp(settings.scale, 0.5f, 2.0f);
    settings.opacity = Clamp(settings.opacity, 0.1f, 1.0f);
    settings.deadZone = Clamp(settings.deadZone, 0.05f, 0.6f);
    settings.stickRadius = Clamp(settings.stickRadius, 0.05f, 0.25f);
    settings.buttonRadius = Clamp(settings.buttonRadius, 0.03f, 0.15f);
    settings.buttonSpacing = Clamp(settings.buttonSpacing, 1.2f, 2.4f);
    settings.stickX = Clamp(settings.stickX, 0.02f, 0.48f);
    settings.stickY = Clamp(settings.stickY, 0.25f, 0.98f);
}

void Controls::SetSurfaceSize(int width, int height)
{
    if (width <= 0 || height <= 0) return;
    if (surfaceWidth == width && surfaceHeight == height) return;
    surfaceWidth = width;
    surfaceHeight = height;
    knobValid = false;
    // A rotation changes every hit region; drop stale pointers so a finger that
    // is still down does not immediately activate an unrelated control.
    Reset();
}

float Controls::StickRadiusPixels() const
{
    return settings.stickRadius * settings.scale * static_cast<float>(surfaceHeight);
}

int Controls::StickCenterPixels(int *x, int *y) const
{
    int centerX = static_cast<int>(settings.stickX * surfaceWidth + 0.5f);
    int centerY = static_cast<int>(settings.stickY * surfaceHeight + 0.5f);
    if (settings.leftHanded) centerX = surfaceWidth - centerX;
    if (x != NULL) *x = centerX;
    if (y != NULL) *y = centerY;
    return static_cast<int>(StickRadiusPixels() + 0.5f);
}

int Controls::ControlRadius(ControlId id) const
{
    const float base = settings.buttonRadius * settings.scale * static_cast<float>(surfaceHeight);
    if (id == ControlStick) return static_cast<int>(StickRadiusPixels() + 0.5f);
    if (id == ControlSkip || id == ControlStart || id == ControlMenu)
        return static_cast<int>(base * 0.66f + 0.5f);
    return static_cast<int>(base + 0.5f);
}

bool Controls::ControlCenter(ControlId id, int *x, int *y) const
{
    const float r = settings.buttonRadius * settings.scale * static_cast<float>(surfaceHeight);
    const float step = r * settings.buttonSpacing;
    const float small = r * 0.66f;
    const float marginRight = r * 0.85f;
    const float marginBottom = r * 1.05f;
    const float marginTop = static_cast<float>(surfaceHeight) * 0.11f;

    float px = 0.0f, py = 0.0f;
    switch (id)
    {
    case ControlStick:
        StickCenterPixels(x, y);
        return true;
    case ControlShoot:
        px = static_cast<float>(surfaceWidth) - marginRight;
        py = static_cast<float>(surfaceHeight) - marginBottom;
        break;
    case ControlBomb:
        px = static_cast<float>(surfaceWidth) - marginRight - step;
        py = static_cast<float>(surfaceHeight) - marginBottom - step * 0.20f;
        break;
    case ControlFocus:
        px = static_cast<float>(surfaceWidth) - marginRight - step * 0.10f;
        py = static_cast<float>(surfaceHeight) - marginBottom - step;
        break;
    case ControlSkip:
        px = static_cast<float>(surfaceWidth) - marginRight - small;
        py = marginTop;
        break;
    case ControlStart:
        px = static_cast<float>(surfaceWidth) - marginRight - small - step * 1.15f;
        py = marginTop;
        break;
    case ControlMenu:
        px = static_cast<float>(surfaceWidth) - marginRight - small - step * 2.45f;
        py = marginTop;
        break;
    default:
        return false;
    }

    if (settings.leftHanded) px = static_cast<float>(surfaceWidth) - px;
    if (x != NULL) *x = static_cast<int>(px + 0.5f);
    if (y != NULL) *y = static_cast<int>(py + 0.5f);
    return true;
}

ControlId Controls::HitTest(float x, float y) const
{
    // Buttons win over the stick: they are smaller targets placed on top of the
    // movement half when the two zones overlap on narrow screens.
    const ControlId order[6] = {ControlShoot, ControlBomb, ControlFocus, ControlSkip,
                                ControlStart, ControlMenu};
    for (int index = 0; index < 6; ++index)
    {
        int cx = 0, cy = 0;
        ControlCenter(order[index], &cx, &cy);
        const float radius = static_cast<float>(ControlRadius(order[index])) * 1.35f + 6.0f;
        if (Distance(x, y, static_cast<float>(cx), static_cast<float>(cy)) <= radius)
            return order[index];
    }

    if (settings.stickMode == Settings::StickFixed)
    {
        int cx = 0, cy = 0;
        const int radius = StickCenterPixels(&cx, &cy);
        const float reach = static_cast<float>(radius) * 1.6f + 8.0f;
        if (Distance(x, y, static_cast<float>(cx), static_cast<float>(cy)) <= reach)
            return ControlStick;
        return ControlNone;
    }

    return x < static_cast<float>(surfaceWidth) * 0.5f ? ControlStick : ControlNone;
}

Controls::Finger *Controls::FindFinger(long id)
{
    for (int index = 0; index < MaxFingers; ++index)
        if (fingers[index].id == id) return &fingers[index];
    return NULL;
}

Controls::Finger *Controls::AcquireFinger()
{
    for (int index = 0; index < MaxFingers; ++index)
        if (fingers[index].id < 0) return &fingers[index];
    return NULL;
}

void Controls::FingerDown(long id, float x, float y)
{
    // A touch while the overlay is hidden only brings it back; the finger
    // that revealed it must not press a control by accident.
    if (!overlayVisible)
    {
        overlayVisible = true;
        return;
    }
    if (FindFinger(id) != NULL) FingerUp(id);
    Finger *finger = AcquireFinger();
    if (finger == NULL) return;

    const float px = x * static_cast<float>(surfaceWidth);
    const float py = y * static_cast<float>(surfaceHeight);
    const ControlId control = HitTest(px, py);

    finger->id = id;
    finger->control = control;
    finger->x = px;
    finger->y = py;
    finger->stickOriginX = px;
    finger->stickOriginY = py;

    if (control == ControlStick && settings.stickMode == Settings::StickFloating)
    {
        finger->stickOriginX = px;
        finger->stickOriginY = py;
        UpdateStick(finger);
    }
    else if (control == ControlStick)
    {
        int cx = 0, cy = 0;
        StickCenterPixels(&cx, &cy);
        finger->stickOriginX = static_cast<float>(cx);
        finger->stickOriginY = static_cast<float>(cy);
        UpdateStick(finger);
    }
    else if (control == ControlBomb)
    {
        bombPulsePending = true;
    }
    Recompute();
}

void Controls::FingerMove(long id, float x, float y)
{
    Finger *finger = FindFinger(id);
    if (finger == NULL) return;
    finger->x = x * static_cast<float>(surfaceWidth);
    finger->y = y * static_cast<float>(surfaceHeight);

    if (finger->control != ControlStick)
    {
        // Sliding between buttons re-targets the same finger instead of
        // dropping the press, and sliding into the movement half promotes it to
        // the stick.
        const ControlId target = HitTest(finger->x, finger->y);
        if (target != finger->control)
        {
            finger->control = target;
            if (target == ControlBomb) bombPulsePending = true;
            if (target == ControlStick)
            {
                finger->stickOriginX = finger->x;
                finger->stickOriginY = finger->y;
                UpdateStick(finger);
            }
        }
        Recompute();
        return;
    }

    // Sliding off a button in fixed-stick mode promotes the finger to the
    // stick, which keeps diagonal movement possible with a thumb.
    if (settings.stickMode == Settings::StickFixed && HitTest(finger->x, finger->y) == ControlNone)
    {
        finger->stickOriginX = finger->x;
        finger->stickOriginY = finger->y;
    }
    UpdateStick(finger);
    Recompute();
}

void Controls::FingerUp(long id)
{
    Finger *finger = FindFinger(id);
    if (finger == NULL) return;
    finger->id = -1;
    finger->control = ControlNone;
    Recompute();
}

void Controls::FingerCanceled(long id)
{
    FingerUp(id);
}

void Controls::Reset()
{
    for (int index = 0; index < MaxFingers; ++index)
    {
        fingers[index].id = -1;
        fingers[index].control = ControlNone;
    }
    knobValid = false;
    holdX = holdY = 0.0f;
    leftLatched = rightLatched = upLatched = downLatched = false;
    Recompute();
}

void Controls::UpdateStick(Finger *finger)
{
    const float radius = StickRadiusPixels();
    float dx = finger->x - finger->stickOriginX;
    float dy = finger->y - finger->stickOriginY;
    const float length = sqrtf(dx * dx + dy * dy);
    if (radius > 1.0f && length > radius) { dx *= radius / length; dy *= radius / length; }

    knobX = finger->stickOriginX + dx;
    knobY = finger->stickOriginY + dy;
    knobValid = true;
    holdX = dx;
    holdY = dy;
}

bool Controls::ButtonHeld(ControlId id) const
{
    for (int index = 0; index < MaxFingers; ++index)
        if (fingers[index].id >= 0 && fingers[index].control == id) return true;
    return false;
}

void Controls::Recompute()
{
    const float radius = StickRadiusPixels();
    const float dead = settings.deadZone;

    // The stick only reports movement while a finger owns it.
    const Finger *stick = NULL;
    for (int index = 0; index < MaxFingers; ++index)
        if (fingers[index].id >= 0 && fingers[index].control == ControlStick) stick = &fingers[index];
    if (stick == NULL) knobValid = false;
    // A wide hysteresis band keeps a diagonal from flickering when a thumb rests
    // exactly on a 45 degree boundary.
    const float strong = dead + (1.0f - dead) * 0.55f;

    bool left = false, right = false, up = false, down = false;
    if (knobValid && radius > 1.0f)
    {
        const float nx = holdX / radius, ny = holdY / radius;
        const float magnitude = sqrtf(nx * nx + ny * ny);
        if (magnitude >= dead)
        {
            const float angle = atan2f(ny, nx);
            bool decided = false;
            if (magnitude < strong)
            {
                // Weak deflection: keep the latched direction while the finger
                // stays inside its cone.
                const float cone = Pi / 4.0f * 0.75f;
                if (leftLatched && AngleDistance(angle, Pi) <= cone) { left = true; decided = true; }
                else if (rightLatched && AngleDistance(angle, 0.0f) <= cone) { right = true; decided = true; }
                else if (upLatched && AngleDistance(angle, -Pi / 2.0f) <= cone) { up = true; decided = true; }
                else if (downLatched && AngleDistance(angle, Pi / 2.0f) <= cone) { down = true; decided = true; }
            }
            if (!decided)
            {
                // Nearest 8-way direction. Sector 0 is centred on 180 degrees
                // (left) and the index grows counter-clockwise in screen space,
                // where y grows downwards.
                const float sector = Pi / 4.0f;
                const int index = static_cast<int>((angle + Pi) / sector + 0.5f) % 8;
                switch (index)
                {
                case 0: left = true; break;
                case 1: up = true; left = true; break;
                case 2: up = true; break;
                case 3: up = true; right = true; break;
                case 4: right = true; break;
                case 5: down = true; right = true; break;
                case 6: down = true; break;
                default: down = true; left = true; break;
                }
            }
        }
    }

    leftLatched = left; rightLatched = right; upLatched = up; downLatched = down;

    state.left = left;
    state.right = right;
    state.up = up;
    state.down = down;
    state.shoot = ButtonHeld(ControlShoot);
    state.bomb = ButtonHeld(ControlBomb);
    state.focus = ButtonHeld(ControlFocus);
    state.skip = ButtonHeld(ControlSkip);
    state.start = ButtonHeld(ControlStart);
    state.menu = ButtonHeld(ControlMenu);
    state.active = left || right || up || down || state.shoot || state.bomb || state.focus ||
                   state.skip || state.start || state.menu;

    bombPulse = bombPulsePending;
    bombPulsePending = false;
}

State Controls::Poll()
{
    return state;
}

unsigned Controls::ControlFillColor(ControlId id) const
{
    switch (id)
    {
    case ControlShoot: return 0xff4050u;
    case ControlBomb: return 0xff4060u;
    case ControlFocus: return 0x40a040u;
    default: return 0x808080u;
    }
}

unsigned Controls::ControlStrokeColor(ControlId id) const
{
    switch (id)
    {
    case ControlShoot: return 0xff8080u;
    case ControlBomb: return 0xff80c0u;
    case ControlFocus: return 0x80e080u;
    default: return 0xe0e0e0u;
    }
}

void Controls::Draw(OverlayCanvas &canvas)
{
    // Hidden while a keyboard or controller is in use; the game frame shows
    // through untouched.
    if (!overlayVisible) return;
    const float base = settings.opacity;
    const int width = canvas.Width(), height = canvas.Height();
    if (width != surfaceWidth || height != surfaceHeight) SetSurfaceSize(width, height);

    // Movement half.
    if (settings.stickMode == Settings::StickFloating)
    {
        Finger *stick = NULL;
        for (int index = 0; index < MaxFingers; ++index)
            if (fingers[index].id >= 0 && fingers[index].control == ControlStick) stick = &fingers[index];
        if (stick != NULL)
        {
            const int radius = ControlRadius(ControlStick);
            canvas.StrokeCircle(static_cast<int>(stick->stickOriginX),
                                static_cast<int>(stick->stickOriginY), radius,
                                2, AlphaColor(0xffffffu, base * 0.55f));
            canvas.FillCircle(static_cast<int>(knobX), static_cast<int>(knobY),
                              static_cast<int>(radius * 0.42f), AlphaColor(0xffffffu, base * 0.75f));
        }
    }
    else
    {
        int cx = 0, cy = 0;
        const int radius = StickCenterPixels(&cx, &cy);
        canvas.FillCircle(cx, cy, radius, AlphaColor(0x000000u, base * 0.22f));
        canvas.StrokeCircle(cx, cy, radius, 2, AlphaColor(0xffffffu, base * 0.50f));
        const int knob = static_cast<int>(radius * 0.42f);
        const int knobCenterX = knobValid ? static_cast<int>(knobX) : cx;
        const int knobCenterY = knobValid ? static_cast<int>(knobY) : cy;
        canvas.FillCircle(knobCenterX, knobCenterY, knob,
                          AlphaColor(0xffffffu, knobValid ? base * 0.70f : base * 0.35f));
    }

    // Button cluster.
    const ControlId buttons[6] = {ControlShoot, ControlBomb, ControlFocus, ControlSkip,
                                  ControlStart, ControlMenu};
    for (int index = 0; index < 6; ++index)
    {
        const ControlId id = buttons[index];
        int cx = 0, cy = 0;
        if (!ControlCenter(id, &cx, &cy)) continue;
        const int radius = ControlRadius(id);
        const bool held = ButtonHeld(id);
        canvas.FillCircle(cx, cy, radius,
                          AlphaColor(held ? ControlFillColor(id) : 0x000000u,
                                     base * (held ? 0.55f : 0.20f)));
        canvas.StrokeCircle(cx, cy, radius, 2,
                            AlphaColor(ControlStrokeColor(id), base * (held ? 1.0f : 0.65f)));

        // Small glyphs keep the cluster readable without any bitmap assets.
        if (id == ControlStart)
            canvas.FillCircle(cx, cy, static_cast<int>(radius * 0.28f), AlphaColor(0xffffffu, base * 0.9f));
        else if (id == ControlMenu)
            canvas.FillRect(cx - radius / 2, cy - 1, radius, 2, AlphaColor(0xffffffu, base * 0.9f));
        else if (id == ControlSkip)
        {
            canvas.FillRect(cx - radius / 2, cy - radius / 3, radius, 2, AlphaColor(0xffffffu, base * 0.9f));
            canvas.FillRect(cx - radius / 2, cy + radius / 4, radius, 2, AlphaColor(0xffffffu, base * 0.9f));
        }
    }
}
} // namespace touch
} // namespace modern
} // namespace th08
