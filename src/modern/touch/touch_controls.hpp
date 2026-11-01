#pragma once

// Platform-neutral virtual gamepad for the Android port.
//
// The module owns the whole on-screen control layer: layout, hit testing,
// multitouch bookkeeping, the digital direction filter and the overlay
// drawing. It has no SDL, Win32 or JNI dependency, so the same code drives the
// Android build and the host tests under tests/.

namespace th08
{
namespace modern
{
namespace touch
{
enum ControlId
{
    ControlNone = -1,
    ControlStick = 0,
    ControlShoot,
    ControlBomb,
    ControlFocus,
    ControlSkip,
    ControlStart,
    ControlMenu,
    ControlCount
};

// Digital gamepad state consumed by the platform input shim.
struct State
{
    State() : left(false), right(false), up(false), down(false), shoot(false),
              bomb(false), focus(false), skip(false), start(false), menu(false), active(false)
    {
    }
    bool left, right, up, down;
    bool shoot, bomb, focus, skip, start, menu;
    bool active; // at least one virtual control is currently held
};

// User-visible layout options. The defaults are tuned for a phone held in
// landscape; every field is persisted by the Java settings screen.
struct Settings
{
    Settings()
        : stickMode(StickFloating), scale(1.0f), opacity(0.45f), deadZone(0.22f),
          leftHanded(false), stickX(0.16f), stickY(0.72f), stickRadius(0.115f),
          buttonRadius(0.072f), buttonSpacing(1.62f), vibrationOnBomb(false)
    {
    }

    enum StickMode
    {
        StickFixed = 0,  // virtual stick always sits at stickX/stickY
        StickFloating = 1 // stick appears wherever the finger lands on the left half
    };

    int stickMode;
    float scale;        // multiplies every control radius
    float opacity;      // 0..1 overlay opacity
    float deadZone;     // 0..1 of the stick radius ignored around its centre
    bool leftHanded;    // swaps the stick and the button cluster
    float stickX;       // normalized anchor, measured from the left edge
    float stickY;       // normalized anchor, measured from the top edge
    float stickRadius;  // normalized by screen height
    float buttonRadius; // normalized by screen height
    float buttonSpacing; // button radius multiplier used to lay out the cluster
    bool vibrationOnBomb;
};

// Minimal drawing surface implemented by the presenter (SDL) and by the tests.
class OverlayCanvas
{
  public:
    virtual ~OverlayCanvas() {}
    virtual int Width() const = 0;
    virtual int Height() const = 0;
    virtual void FillCircle(int centerX, int centerY, int radius, unsigned rgba) = 0;
    virtual void StrokeCircle(int centerX, int centerY, int radius, int thickness, unsigned rgba) = 0;
    virtual void FillRect(int x, int y, int width, int height, unsigned rgba) = 0;
};

class Controls
{
  public:
    Controls();

    void Configure(const Settings &settings);
    const Settings &CurrentSettings() const { return settings; }
    Settings &MutableSettings() { return settings; }

    // Window size used to convert normalized finger positions into pixels.
    void SetSurfaceSize(int width, int height);

    // Physical input handling. The overlay hides while the player uses a
    // keyboard or a controller, and the next touch brings it back; that touch
    // only reveals the overlay and never presses a control.
    void SetOverlayVisible(bool visible);
    bool OverlayVisible() const { return overlayVisible; }
    void NotifyPhysicalInput();

    // SDL finger events. Positions are normalized with y growing downwards and
    // both axes in 0..1, matching SDL_TouchFingerEvent.
    void FingerDown(long fingerId, float x, float y);
    void FingerMove(long fingerId, float x, float y);
    void FingerUp(long fingerId);
    void FingerCanceled(long fingerId);
    void Reset();

    State Poll();
    State CurrentState() const { return state; }
    void Draw(OverlayCanvas &canvas);

    // Exposed for the host tests: center of a control in surface pixels.
    bool ControlCenter(ControlId id, int *x, int *y) const;
    int ControlRadius(ControlId id) const;

  private:
    struct Finger
    {
        Finger() : id(-1), control(ControlNone), x(0.0f), y(0.0f), stickOriginX(0.0f),
                   stickOriginY(0.0f) {}
        long id;
        ControlId control;
        float x, y;
        float stickOriginX, stickOriginY;
    };

    enum { MaxFingers = 10 };

    Finger fingers[MaxFingers];
    Settings settings;
    State state;
    bool overlayVisible;
    int surfaceWidth, surfaceHeight;
    float knobX, knobY;  // current stick knob position in pixels
    float holdX, holdY;  // raw stick vector in pixels, kept for the direction filter
    bool knobValid;
    bool leftLatched, rightLatched, upLatched, downLatched;
    bool bombPulse;      // set for one frame after a bomb press (optional haptics)
    bool bombPulsePending;

    Finger *FindFinger(long id);
    Finger *AcquireFinger();
    ControlId HitTest(float x, float y) const;
    float StickRadiusPixels() const;
    int StickCenterPixels(int *x, int *y) const;
    void UpdateStick(Finger *finger);
    void Recompute();
    bool ButtonHeld(ControlId id) const;
    unsigned ControlFillColor(ControlId id) const;
    unsigned ControlStrokeColor(ControlId id) const;
};
} // namespace touch
} // namespace modern
} // namespace th08
