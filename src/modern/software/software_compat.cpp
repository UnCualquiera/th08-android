// Software back end support for the shared compatibility helpers.
//
// d3dx8_compat.cpp reuses the same surface access hooks as the desktop GL back
// end, and EnemyManager.cpp calls the render audit hooks whenever
// TH08_MODERN_LINUX is defined. In the CPU back end the surface memory is
// always current, and the desktop-only framebuffer probes are simply disabled.
//
// The surface access hooks must be defined at global scope with the same
// linkage as the declarations in linux/d3d8_internal.hpp, which is what the
// shared D3DX translation unit expects.

#include "software_d3d8.hpp"

#include <d3d8.h>

#include "../../ZunResult.hpp"
#include "../linux/d3d8_internal.hpp"

// The desktop GL back end keeps its surfaces in GPU memory and needs these
// hooks to read and invalidate them. The CPU back end stores the pixels in the
// surface itself, so access is a pointer read and invalidation is a no-op.
bool th08_linux_surface_access(IDirect3DSurface8 *surfaceRaw, LinuxSurfaceAccess *access, bool)
{
    BYTE *pixels = NULL;
    UINT width = 0, height = 0, pitch = 0;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    if (!th08::modern::SoftwareSurfaceAccess(surfaceRaw, &pixels, &width, &height, &pitch, &format))
        return false;
    if (access == NULL) return false;
    access->pixels = pixels;
    access->width = width;
    access->height = height;
    access->pitch = pitch;
    access->format = format;
    return true;
}

void th08_linux_surface_changed(IDirect3DSurface8 *)
{
}

namespace th08
{
struct Enemy;

namespace modern
{
// The render audit reads back the GL framebuffer, which the CPU back end does
// not have. It stays disabled so the game runs without the desktop-only probe.
bool IsEnemyRenderAuditEnabled() { return false; }
ZunResult AuditEnemyPrimaryDraw(Enemy *) { return ZUN_SUCCESS; }
} // namespace modern
} // namespace th08
