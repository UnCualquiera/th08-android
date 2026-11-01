#pragma once

// SDL presenter for the CPU back end.
//
// Owns the SDL window/renderer pair, uploads each software frame and draws the
// on-screen controls on top of it. The same object serves the Android build
// and the desktop software build, so the presentation path is only written
// once.

#include "software_d3d8.hpp"
#include "../touch/touch_controls.hpp"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

class SdlSoftwarePresenter;

namespace th08
{
namespace modern
{
class SdlSoftwarePresenter : public SoftwarePresenter
{
  public:
    SdlSoftwarePresenter();
    ~SdlSoftwarePresenter();

    // Creates the renderer for `window`, which the platform layer already owns.
    bool Initialize(SDL_Window *window, touch::Controls *controls);

    // Drops the cached texture, for example after a rotation or a resize.
    void Invalidate();

    virtual void Present(const unsigned char *pixels, int width, int height, int pitch);
    virtual void BeginFrame();
    virtual void SurfaceResized(int width, int height);

    // Routes SDL touch events to the control layer. Called from BeginFrame, but
    // also usable directly when the platform polls events itself.
    void PumpTouchEvents();

    // True when a keyboard key or a controller/joystick button went down since
    // the previous call; the overlay hides on it.
    bool DetectPhysicalInput();

    // Mirrors the current control state into the virtual keyboard the game
    // reads, so the reconstructed input path needs no change.
    void ApplyVirtualKeys(unsigned char *keyboardState) const;

    // True when any virtual control is held; the platform layer uses it to
    // request a redraw of the overlay.
    bool AnyControlHeld() const;

    // Stretches the game frame over the whole drawable instead of keeping the
    // 4:3 aspect ratio with side bars, per the setup screen's option.
    void SetFillScreen(bool fill);

  private:
    SdlSoftwarePresenter(const SdlSoftwarePresenter &);
    SdlSoftwarePresenter &operator=(const SdlSoftwarePresenter &);

    void PumpWindowEvents();
    void DrawControls();
    void UpdateLetterbox();

    enum { KeyHistorySize = 512 };

    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *frame;
    touch::Controls *controlLayer;
    int frameWidth, frameHeight;
    int drawableWidth, drawableHeight;
    bool letterboxReady;
    bool hadFocus;
    bool fillScreen;
    unsigned char hadKeys[KeyHistorySize];
};

// Feeds the virtual gamepad into the platform keyboard state so the
// reconstructed input paths see the touch controls.
void ApplyTouchInput(unsigned char *keyboardState);

// Creates and installs the platform presenter for the SDL window created by
// the Win32 shim, loading the control options the setup activity persisted.
void InstallSoftwarePresenterForWindow(SDL_Window *window);

// Control layer owned by the installed presenter, for settings queries and the
// host tests. NULL until a window is installed.
touch::Controls *SoftwareTouchControls();

// Installs the presenter that owns the control layer.
void SetInputPresenter(SdlSoftwarePresenter *presenter);
} // namespace modern
} // namespace th08

// Virtual key codes the control layer writes into the platform keyboard state.
// Declared here so the input shim and the presenter cannot drift apart.
namespace th08
{
namespace modern
{
namespace touch
{
// Fills the DirectInput (dinput.h) and Win32 (windows.h) slots the game reads.
void WriteVirtualKeys(const State &state, unsigned char *keyboardState);
} // namespace touch
} // namespace modern
} // namespace th08
