#include "sdl_present.hpp"

#include <SDL.h>
#include <dinput.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>
#include <windows.h>

#include "../touch/touch_controls.hpp"
#include "virtual_keys.hpp"

namespace th08
{
namespace modern
{
namespace touch
{
// The reconstructed sources read the game through two input paths: DirectInput
// keyboard scan codes and GetKeyboardState virtual keys. Writing both keeps the
// control layer independent of which path the game selected at startup.
void WriteVirtualKeys(const State &state, unsigned char *keyboardState)
{
    if (keyboardState == NULL) return;
    if (state.left) { keyboardState[DIK_LEFT] = 0x80; keyboardState[TH08_VK_LEFT] = 0x80; keyboardState[DIK_NUMPAD4] = 0x80; keyboardState[TH08_VK_NUMPAD4] = 0x80; }
    if (state.right) { keyboardState[DIK_RIGHT] = 0x80; keyboardState[TH08_VK_RIGHT] = 0x80; keyboardState[DIK_NUMPAD6] = 0x80; keyboardState[TH08_VK_NUMPAD6] = 0x80; }
    if (state.up) { keyboardState[DIK_UP] = 0x80; keyboardState[TH08_VK_UP] = 0x80; keyboardState[DIK_NUMPAD8] = 0x80; keyboardState[TH08_VK_NUMPAD8] = 0x80; }
    if (state.down) { keyboardState[DIK_DOWN] = 0x80; keyboardState[TH08_VK_DOWN] = 0x80; keyboardState[DIK_NUMPAD2] = 0x80; keyboardState[TH08_VK_NUMPAD2] = 0x80; }
    if (state.shoot) { keyboardState[DIK_Z] = 0x80; keyboardState['Z'] = 0x80; }
    if (state.bomb) { keyboardState[DIK_X] = 0x80; keyboardState['X'] = 0x80; }
    if (state.focus) { keyboardState[DIK_LSHIFT] = 0x80; keyboardState[DIK_RSHIFT] = 0x80; keyboardState[TH08_VK_SHIFT] = 0x80; }
    if (state.skip) { keyboardState[DIK_LCONTROL] = 0x80; keyboardState[DIK_RCONTROL] = 0x80; keyboardState[TH08_VK_CONTROL] = 0x80; }
    if (state.start) { keyboardState[DIK_RETURN] = 0x80; keyboardState[TH08_VK_RETURN] = 0x80; }
    if (state.menu) { keyboardState[DIK_ESCAPE] = 0x80; keyboardState[TH08_VK_ESCAPE] = 0x80; }
}
} // namespace touch

namespace
{
// Draws the control overlay with SDL's own primitives, so no extra GPU program
// is needed on the OpenGL ES devices Android provides.
class SdlOverlayCanvas : public touch::OverlayCanvas
{
  public:
    explicit SdlOverlayCanvas(SDL_Renderer *renderer) : renderer(renderer), width(0), height(0) {}
    virtual int Width() const { return width; }
    virtual int Height() const { return height; }
    virtual void FillCircle(int centerX, int centerY, int radius, unsigned rgba)
    {
        SetColor(rgba);
        const int limit = radius < 1 ? 1 : radius;
        for (int offset = -limit; offset <= limit; ++offset)
        {
            const int squared = limit * limit - offset * offset;
            int half = 0;
            while ((half + 1) * (half + 1) <= squared) ++half;
            SDL_Rect row;
            row.x = centerX - half;
            row.y = centerY + offset;
            row.w = half * 2 + 1;
            row.h = 1;
            SDL_RenderFillRect(renderer, &row);
        }
    }
    virtual void StrokeCircle(int centerX, int centerY, int radius, int, unsigned rgba)
    {
        SetColor(rgba);
        const int limit = radius < 1 ? 1 : radius;
        int x = limit, y = 0;
        int decision = 1 - limit;
        while (x >= y)
        {
            Plot(centerX + x, centerY + y);
            Plot(centerX - x, centerY + y);
            Plot(centerX + x, centerY - y);
            Plot(centerX - x, centerY - y);
            Plot(centerX + y, centerY + x);
            Plot(centerX - y, centerY + x);
            Plot(centerX + y, centerY - x);
            Plot(centerX - y, centerY - x);
            ++y;
            if (decision <= 0) decision += 2 * y + 1;
            else { --x; decision += 2 * (y - x) + 1; }
        }
    }
    virtual void FillRect(int x, int y, int rectWidth, int rectHeight, unsigned rgba)
    {
        SetColor(rgba);
        SDL_Rect rect;
        rect.x = x; rect.y = y; rect.w = rectWidth; rect.h = rectHeight;
        SDL_RenderFillRect(renderer, &rect);
    }
    void SetSize(int width_, int height_) { width = width_; height = height_; }

  private:
    void Plot(int x, int y)
    {
        SDL_Point point;
        point.x = x;
        point.y = y;
        SDL_RenderDrawPoint(renderer, point.x, point.y);
    }
    void SetColor(unsigned rgba)
    {
        SDL_SetRenderDrawColor(renderer, static_cast<Uint8>((rgba >> 16) & 0xff),
                               static_cast<Uint8>((rgba >> 8) & 0xff),
                               static_cast<Uint8>(rgba & 0xff),
                               static_cast<Uint8>((rgba >> 24) & 0xff));
    }
    SDL_Renderer *renderer;
    int width, height;
};
} // namespace

namespace
{
// /proc/self/status and /proc/self/oom_score feed the heartbeat, so a
// diagnostic export shows how much memory a killed session had reached: on a
// low-end phone that is the one number that separates a low-memory kill from
// another death. Android-only, like the heartbeat itself.
#if defined(__ANDROID__)
bool ReadProcStatusKb(const char *key, long *value)
{
    FILE *status = fopen("/proc/self/status", "r");
    if (status == NULL) return false;
    char line[256];
    bool found = false;
    const size_t keyLength = strlen(key);
    while (fgets(line, sizeof(line), status) != NULL)
    {
        if (strncmp(line, key, keyLength) == 0)
        {
            *value = strtol(line + keyLength + 1, NULL, 10);
            found = true;
            break;
        }
    }
    fclose(status);
    return found;
}

long ProcessOomScore()
{
    FILE *score = fopen("/proc/self/oom_score", "r");
    if (score == NULL) return -1;
    long value = -1;
    if (fscanf(score, "%ld", &value) != 1) value = -1;
    fclose(score);
    return value;
}
#endif

// Rewrites modern-heartbeat.txt once a second while the game presents.
// `status=running` doubles as a liveness mark: the boot path preserves a file
// that still says "running" (a session that never reached a clean shutdown),
// and main() rewrites it to "closed" when the game exits normally, so the
// export distinguishes a kill from a clean exit.
void WriteHeartbeat(const char *status, unsigned totalFrames, unsigned windowFrames, float fps)
{
    // Android-only: the heartbeat exists for device diagnosis through the log
    // export, and keeping it off the desktop builds leaves the host checks
    // free of stray files.
#if defined(__ANDROID__)
    FILE *heartbeat = fopen("modern-heartbeat.txt", "w");
    if (heartbeat == NULL) return;
    long rssKb = -1, peakKb = -1;
    ReadProcStatusKb("VmRSS:", &rssKb);
    ReadProcStatusKb("VmPeak:", &peakKb);
    fprintf(heartbeat,
            "status=%s epoch=%lld frames_total=%u window_frames=%u fps=%.1f rss_kb=%ld "
            "peak_rss_kb=%ld oom_score=%ld\n",
            status, static_cast<long long>(time(NULL)), totalFrames, windowFrames, fps, rssKb, peakKb,
            ProcessOomScore());
    fclose(heartbeat);
#else
    (void)status;
    (void)totalFrames;
    (void)windowFrames;
    (void)fps;
#endif
}
} // namespace

SdlSoftwarePresenter::SdlSoftwarePresenter()
    : window(NULL), renderer(NULL), frame(NULL), controlLayer(NULL), frameWidth(0),
      frameHeight(0), drawableWidth(0), drawableHeight(0), letterboxReady(false), hadFocus(true),
      fillScreen(false)
{
    memset(hadKeys, 0, sizeof(hadKeys));
}

SdlSoftwarePresenter::~SdlSoftwarePresenter()
{
    if (frame != NULL) SDL_DestroyTexture(frame);
    if (renderer != NULL) SDL_DestroyRenderer(renderer);
}

bool SdlSoftwarePresenter::Initialize(SDL_Window *window_, touch::Controls *controls)
{
    window = window_;
    controlLayer = controls;
    if (window == NULL) return false;
    // SDL keeps the SDL_Window object across resizes and rotations, so a second
    // bind only refreshes the pointers.
    if (renderer != NULL) return true;

    // The Android and desktop builds both create the window with an OpenGL ES
    // / OpenGL context, so an accelerated renderer can reuse it. Fall back to
    // the software renderer when no accelerated driver is available.
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (renderer == NULL)
    {
        fprintf(stderr, "th08-modern: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    letterboxReady = false;
    SetInputPresenter(this);
    int surfaceWidth = 0, surfaceHeight = 0;
    SDL_GetWindowSize(window, &surfaceWidth, &surfaceHeight);
    if (controlLayer != NULL) controlLayer->SetSurfaceSize(surfaceWidth, surfaceHeight);
    return true;
}

void SdlSoftwarePresenter::Invalidate()
{
    if (frame != NULL) SDL_DestroyTexture(frame);
    frame = NULL;
    frameWidth = frameHeight = 0;
    letterboxReady = false;
}

void SdlSoftwarePresenter::SurfaceResized(int width, int height)
{
    drawableWidth = width;
    drawableHeight = height;
    letterboxReady = false;
    if (controlLayer != NULL) controlLayer->SetSurfaceSize(width, height);
}

void SdlSoftwarePresenter::BeginFrame()
{
    PumpTouchEvents();
}

void SdlSoftwarePresenter::PumpTouchEvents()
{
    if (controlLayer == NULL) return;
    // Only the finger range is removed from the queue, so the game's own pump
    // still receives SDL_QUIT and the window focus events it translates into
    // WM_CLOSE and WM_ACTIVATEAPP. The three finger event constants are
    // contiguous, but their relative order differs between SDL releases (some
    // put motion above finger-up), so the upper bound is computed at compile
    // time instead of assumed. The window queues more events than a single
    // batch holds when the surface is created, so the touch range is drained
    // in batches; the cap only guards against a pathological producer on this
    // thread.
    const Uint32 fingerMax =
        SDL_FINGERUP > SDL_FINGERMOTION ? static_cast<Uint32>(SDL_FINGERUP)
                                        : static_cast<Uint32>(SDL_FINGERMOTION);
    enum { MaxBatches = 8 };
    for (int batch = 0; batch < MaxBatches; ++batch)
    {
        SDL_Event events[16];
        const int count = SDL_PeepEvents(events, 16, SDL_GETEVENT, SDL_FINGERDOWN, fingerMax);
        if (count <= 0) break;
        for (int index = 0; index < count; ++index)
        {
            const SDL_Event &event = events[index];
            switch (event.type)
            {
            case SDL_FINGERDOWN:
                controlLayer->FingerDown(static_cast<long>(event.tfinger.fingerId),
                                         event.tfinger.x, event.tfinger.y);
                break;
            case SDL_FINGERMOTION:
                controlLayer->FingerMove(static_cast<long>(event.tfinger.fingerId),
                                         event.tfinger.x, event.tfinger.y);
                break;
            case SDL_FINGERUP:
                controlLayer->FingerUp(static_cast<long>(event.tfinger.fingerId));
                break;
            default:
                break;
            }
        }
    }

    // Backgrounding resets the controls. Watching the focus flag instead of a
    // window event keeps the event queue untouched, and it also covers the
    // platforms that pause the app without a focus change.
    if (window != NULL)
    {
        const bool focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
        if (!focused && hadFocus) controlLayer->Reset();
        hadFocus = focused;
    }

    // A keyboard key or a controller button replaces the on-screen controls.
    // The virtual gamepad never touches SDL's keyboard state or button events,
    // so only real hardware reaches this check.
    if (DetectPhysicalInput()) controlLayer->NotifyPhysicalInput();
}

bool SdlSoftwarePresenter::DetectPhysicalInput()
{
    SDL_PumpEvents();

    // Keyboard: compare the live scancode state with the previous frame, so a
    // key press is seen even after the game's own pump drained its event.
    int keyCount = 0;
    const Uint8 *keys = SDL_GetKeyboardState(&keyCount);
    bool pressed = false;
    if (keys != NULL)
    {
        if (keyCount > KeyHistorySize) keyCount = KeyHistorySize;
        for (int index = 0; index < keyCount; ++index)
        {
            if (keys[index] != 0 && hadKeys[index] == 0) pressed = true;
            hadKeys[index] = keys[index];
        }
        for (int index = keyCount; index < KeyHistorySize; ++index) hadKeys[index] = 0;
    }
    if (pressed) return true;

    // Controller and joystick buttons are peeked, not consumed: the game's
    // message pump still owns those events.
    SDL_Event events[16];
    int count =
        SDL_PeepEvents(events, 16, SDL_PEEKEVENT, SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLERBUTTONUP);
    for (int index = 0; index < count; ++index)
        if (events[index].type == SDL_CONTROLLERBUTTONDOWN) return true;
    count = SDL_PeepEvents(events, 16, SDL_PEEKEVENT, SDL_JOYBUTTONDOWN, SDL_JOYBUTTONUP);
    for (int index = 0; index < count; ++index)
        if (events[index].type == SDL_JOYBUTTONDOWN) return true;

    // Key events are peeked as well: on some platforms the keyboard state
    // only reflects events after the backend pumped them, and the peek costs
    // nothing. Auto-repeat keeps a hidden overlay hidden, which is the
    // intended behaviour while a key is held.
    count = SDL_PeepEvents(events, 16, SDL_PEEKEVENT, SDL_KEYDOWN, SDL_KEYUP);
    for (int index = 0; index < count; ++index)
        if (events[index].type == SDL_KEYDOWN) return true;
    return false;
}

namespace
{
SdlSoftwarePresenter *g_inputPresenter;
} // namespace

void SetInputPresenter(SdlSoftwarePresenter *presenter) { g_inputPresenter = presenter; }

void ApplyTouchInput(unsigned char *keyboardState)
{
    if (g_inputPresenter == NULL) return;
    g_inputPresenter->ApplyVirtualKeys(keyboardState);
}

bool SdlSoftwarePresenter::AnyControlHeld() const
{
    return controlLayer != NULL && controlLayer->CurrentState().active;
}

void SdlSoftwarePresenter::ApplyVirtualKeys(unsigned char *keyboardState) const
{
    if (controlLayer == NULL) return;
    touch::WriteVirtualKeys(controlLayer->Poll(), keyboardState);
}

void SdlSoftwarePresenter::UpdateLetterbox()
{
    // Present already synced the output size before calling this.
    if (controlLayer != NULL) controlLayer->SetSurfaceSize(drawableWidth, drawableHeight);
    letterboxReady = true;
}

void SdlSoftwarePresenter::Present(const unsigned char *pixels, int width, int height, int pitch)
{
    if (renderer == NULL || pixels == NULL || width <= 0 || height <= 0) return;

    // Once a second the heartbeat file is rewritten with the frames so far,
    // the observed frame rate and the process memory; the first call writes
    // one immediately so even a sub-second session leaves a trace.
    {
        static unsigned heartbeatFrames = 0;
        static unsigned heartbeatTotal = 0;
        static Uint32 heartbeatStart = 0;
        ++heartbeatFrames;
        ++heartbeatTotal;
        const Uint32 heartbeatNow = SDL_GetTicks();
        if (heartbeatStart == 0)
        {
            heartbeatStart = heartbeatNow;
            WriteHeartbeat("running", heartbeatTotal, 0, 0.0f);
        }
        if (heartbeatNow - heartbeatStart >= 1000)
        {
            const Uint32 elapsed = heartbeatNow - heartbeatStart;
            WriteHeartbeat("running", heartbeatTotal, heartbeatFrames,
                           elapsed > 0 ? heartbeatFrames * 1000.0f / elapsed : 0.0f);
            heartbeatFrames = 0;
            heartbeatStart = heartbeatNow;
        }
    }

    // The drawable size is queried every frame instead of tracked through
    // window events, which keeps the resize handling off the game's queue.
    int outputWidth = 0, outputHeight = 0;
    SDL_GetRendererOutputSize(renderer, &outputWidth, &outputHeight);
    if (outputWidth != drawableWidth || outputHeight != drawableHeight)
    {
        SurfaceResized(outputWidth, outputHeight);
        Invalidate();
    }
    if (!letterboxReady) UpdateLetterbox();

    if (frame == NULL || frameWidth != width || frameHeight != height)
    {
        if (frame != NULL) SDL_DestroyTexture(frame);
        frame = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
                                  width, height);
        if (frame == NULL)
        {
            fprintf(stderr, "th08-modern: SDL_CreateTexture failed: %s\n", SDL_GetError());
            return;
        }
        // Nearest keeps the game's 384x448 pixels crisp on a phone screen.
        SDL_SetTextureScaleMode(frame, SDL_ScaleModeNearest);
        frameWidth = width;
        frameHeight = height;
    }

    if (pitch == width * 4)
    {
        SDL_UpdateTexture(frame, NULL, pixels, width * 4);
    }
    else
    {
        std::vector<unsigned char> staging(static_cast<size_t>(width) * height * 4);
        for (int y = 0; y < height; ++y)
            memcpy(&staging[static_cast<size_t>(y) * width * 4], pixels + static_cast<size_t>(y) * pitch,
                   static_cast<size_t>(width) * 4);
        SDL_UpdateTexture(frame, NULL, &staging[0], width * 4);
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    SDL_Rect destination;
    if (fillScreen)
    {
        // Full-screen stretch: every panel pixel shows the game, widening the
        // 4:3 picture on a wide phone; the setup screen can switch back to
        // the aspect-preserving letterbox below.
        destination.x = 0;
        destination.y = 0;
        destination.w = drawableWidth;
        destination.h = drawableHeight;
    }
    else
    {
    // Preserve the aspect ratio; on phones the scale is usually an integer, which
    // keeps the pixel grid regular.
    float scaleX = static_cast<float>(drawableWidth) / static_cast<float>(width);
    float scaleY = static_cast<float>(drawableHeight) / static_cast<float>(height);
    if (scaleX >= 1.0f && scaleY >= 1.0f)
    {
        const float snapped = floorf(scaleX < scaleY ? scaleX : scaleY);
        if (snapped >= 1.0f) { scaleX = snapped; scaleY = snapped; }
    }
    destination.w = static_cast<int>(static_cast<float>(width) * scaleX + 0.5f);
    destination.h = static_cast<int>(static_cast<float>(height) * scaleY + 0.5f);
    if (destination.w > drawableWidth) destination.w = drawableWidth;
    if (destination.h > drawableHeight) destination.h = drawableHeight;
    destination.x = (drawableWidth - destination.w) / 2;
    destination.y = (drawableHeight - destination.h) / 2;
    }
    SDL_RenderCopy(renderer, frame, NULL, &destination);

    DrawControls();
    SDL_RenderPresent(renderer);
}

void SdlSoftwarePresenter::DrawControls()
{
    if (controlLayer == NULL || renderer == NULL) return;
    SdlOverlayCanvas canvas(renderer);
    canvas.SetSize(drawableWidth, drawableHeight);
    controlLayer->Draw(canvas);
}

void SdlSoftwarePresenter::SetFillScreen(bool fill)
{
    fillScreen = fill;
}

namespace
{
// The setup activity persists the control options as key=value lines in the
// app's internal storage. On the desktop software build the same file is read
// from the working directory, which makes the layout testable without a phone.
bool TouchSettingsPath(char *buffer, size_t size)
{
#if defined(__ANDROID__)
    const char *internal = SDL_AndroidGetInternalStoragePath();
    if (internal == NULL) return false;
    snprintf(buffer, size, "%s/th08_touch_settings.ini", internal);
#else
    snprintf(buffer, size, "th08_touch_settings.ini");
#endif
    return true;
}

float ClampSetting(float value, float low, float high, float fallback)
{
    if (value < low || value > high) return fallback;
    return value;
}

// Unknown keys are ignored so a settings file written by a newer app version
// still loads in an older build.
void ApplySettingLine(char *line, touch::Settings *settings, bool *fillScreen)
{
    char *end = strchr(line, '\n');
    if (end != NULL) *end = '\0';
    end = strchr(line, '\r');
    if (end != NULL) *end = '\0';
    char *separator = strchr(line, '=');
    if (separator == NULL) return;
    *separator = '\0';
    const char *key = line;
    const char *value = separator + 1;

    if (strcmp(key, "stick_mode") == 0)
        settings->stickMode = atoi(value) != 0 ? touch::Settings::StickFloating
                                               : touch::Settings::StickFixed;
    else if (strcmp(key, "scale") == 0)
        settings->scale = ClampSetting(static_cast<float>(atof(value)), 0.5f, 2.0f, 1.0f);
    else if (strcmp(key, "opacity") == 0)
        settings->opacity = ClampSetting(static_cast<float>(atof(value)), 0.1f, 1.0f, 0.45f);
    else if (strcmp(key, "dead_zone") == 0)
        settings->deadZone = ClampSetting(static_cast<float>(atof(value)), 0.0f, 0.6f, 0.22f);
    else if (strcmp(key, "left_handed") == 0)
        settings->leftHanded = atoi(value) != 0;
    else if (strcmp(key, "stick_x") == 0)
        settings->stickX = ClampSetting(static_cast<float>(atof(value)), 0.05f, 0.95f, 0.16f);
    else if (strcmp(key, "stick_y") == 0)
        settings->stickY = ClampSetting(static_cast<float>(atof(value)), 0.05f, 0.95f, 0.72f);
    else if (strcmp(key, "vibration_on_bomb") == 0)
        settings->vibrationOnBomb = atoi(value) != 0;
    else if (strcmp(key, "display_mode") == 0)
        *fillScreen = strcmp(value, "fill") == 0;
}

void LoadTouchSettings(touch::Settings *settings, bool *fillScreen)
{
    char path[1024];
    if (!TouchSettingsPath(path, sizeof(path))) return;
    FILE *file = fopen(path, "r");
    if (file == NULL) return;
    char line[256];
    while (fgets(line, sizeof(line), file) != NULL)
        ApplySettingLine(line, settings, fillScreen);
    fclose(file);
}
} // namespace

namespace
{
// Owned by the platform presenter for the lifetime of the process.
touch::Controls *g_controls;
SdlSoftwarePresenter *g_windowPresenter;
} // namespace

void InstallSoftwarePresenterForWindow(SDL_Window *window)
{
    if (window == NULL) return;
    // Deliberately leaked: the presenter must outlive the game's device, and
    // the process ends when the game exits.
    if (g_windowPresenter == NULL)
    {
        g_controls = new touch::Controls;
        touch::Settings settings;
        bool fillScreen = false;
        LoadTouchSettings(&settings, &fillScreen);
        g_controls->Configure(settings);
        g_windowPresenter = new SdlSoftwarePresenter;
        g_windowPresenter->SetFillScreen(fillScreen);
        SetSoftwarePresenter(g_windowPresenter);
    }
    if (!g_windowPresenter->Initialize(window, g_controls))
        fprintf(stderr, "th08-modern: software presenter initialization failed\n");
}

touch::Controls *SoftwareTouchControls() { return g_controls; }
} // namespace modern
} // namespace th08
