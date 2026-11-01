#pragma once

// CPU implementation of the reduced Direct3D 8 surface used by the
// reconstructed game sources.
//
// Android devices expose OpenGL ES through SDL, which has no fixed-function
// pipeline, so the desktop GL mapping in src/modern/linux/d3d8_compat.cpp
// cannot be reused there. This backend keeps the same reduced D3D8 object
// model but rasterizes into ordinary memory and hands finished frames to a
// presenter. Because it needs no GPU API it is also usable from the desktop
// software build and from the host checks under tests/.

#include <d3d8.h>

namespace th08
{
namespace modern
{
// Sink for the frames produced by the software back end.
class SoftwarePresenter
{
  public:
    virtual ~SoftwarePresenter() {}

    // One finished frame. `pixels` is tightly packed RGBA8 with the first row
    // at the top, `pitch` its stride in bytes.
    virtual void Present(const unsigned char *pixels, int width, int height, int pitch) = 0;

    // Called once per frame before Present() so the platform layer can pump
    // input events and refresh its overlay state.
    virtual void BeginFrame() {}

    // Called when the output surface changes size (rotation, resize, keyboard
    // insets). Used to map normalized finger positions.
    virtual void SurfaceResized(int, int) {}
};

// Direct3DCreate8 replacement. `presenter` must outlive the returned object.
IDirect3D8 *CreateSoftwareDirect3D(SoftwarePresenter *presenter);

// Installs the process-wide presenter used by the Direct3DCreate8 export that
// the reconstructed sources call. The platform layer installs it during
// startup, before the game creates its device.
void SetSoftwarePresenter(SoftwarePresenter *presenter);
SoftwarePresenter *SoftwarePresenterInstance();

// Exposes the CPU surface memory for the shared D3DX compatibility helpers.
bool SoftwareSurfaceAccess(IDirect3DSurface8 *surface, BYTE **pixels, UINT *width, UINT *height,
                           UINT *pitch, D3DFORMAT *format);
} // namespace modern
} // namespace th08
