#include "software_d3d8.hpp"

#include <algorithm>
#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

// CPU back end for the reduced Direct3D 8 surface used by the game sources.
//
// Android exposes OpenGL ES through SDL and has no fixed-function pipeline, so
// the desktop GL mapping in src/modern/linux/d3d8_compat.cpp cannot be reused.
// This file keeps the same reduced D3D8 object model and the same state
// semantics, but rasterizes into memory and hands finished frames to a
// presenter.
//
// The transform, fog coordinate, texture-stage and EffectiveColor() behaviour
// deliberately mirror src/modern/linux/d3d8_compat.cpp so the two back ends
// produce the same image for the same device state.

namespace th08
{
namespace modern
{
namespace
{
struct FormatInfo
{
    UINT bytes;
    int redShift, greenShift, blueShift, alphaShift;
    int redBits, greenBits, blueBits, alphaBits;
};

const FormatInfo &Format(D3DFORMAT format)
{
    static const FormatInfo unsupported = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    switch (format)
    {
    case D3DFMT_A8R8G8B8: { static const FormatInfo value = {4, 16, 8, 0, 24, 8, 8, 8, 8}; return value; }
    case D3DFMT_X8R8G8B8: { static const FormatInfo value = {4, 16, 8, 0, 0, 8, 8, 8, 0}; return value; }
    case D3DFMT_R8G8B8: { static const FormatInfo value = {3, 16, 8, 0, 0, 8, 8, 8, 0}; return value; }
    case D3DFMT_R5G6B5: { static const FormatInfo value = {2, 11, 5, 0, 0, 5, 6, 5, 0}; return value; }
    case D3DFMT_X1R5G5B5: { static const FormatInfo value = {2, 10, 5, 0, 0, 5, 5, 5, 0}; return value; }
    case D3DFMT_A1R5G5B5: { static const FormatInfo value = {2, 10, 5, 0, 15, 5, 5, 5, 1}; return value; }
    case D3DFMT_A4R4G4B4: { static const FormatInfo value = {2, 8, 4, 0, 12, 4, 4, 4, 4}; return value; }
    default: return unsupported;
    }
}

UINT FormatBytes(D3DFORMAT format) { return Format(format).bytes; }

// Expands an N bit channel to 8 bits by bit replication, which is what the
// desktop GL back end gets from the sampler.
int ExpandChannel(unsigned value, int bits)
{
    if (bits <= 0) return 255;
    if (bits >= 8) return static_cast<int>(value & 0xffu);
    const unsigned replicated = (value << (8 - bits)) | (value >> bits);
    return static_cast<int>(replicated & 0xffu);
}

void DecodePixel(const BYTE *source, D3DFORMAT format, BYTE *rgba)
{
    const FormatInfo &info = Format(format);
    if (info.bytes == 0) { rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0; return; }

    UINT raw = 0;
    if (info.bytes == 2) raw = static_cast<unsigned>(source[0]) | (static_cast<unsigned>(source[1]) << 8);
    else if (info.bytes == 3)
        raw = static_cast<unsigned>(source[0]) | (static_cast<unsigned>(source[1]) << 8) |
              (static_cast<unsigned>(source[2]) << 16);
    else
        raw = static_cast<unsigned>(source[0]) | (static_cast<unsigned>(source[1]) << 8) |
              (static_cast<unsigned>(source[2]) << 16) | (static_cast<unsigned>(source[3]) << 24);

    const unsigned red = (raw >> info.redShift) & ((1u << info.redBits) - 1u);
    const unsigned green = (raw >> info.greenShift) & ((1u << info.greenBits) - 1u);
    const unsigned blue = (raw >> info.blueShift) & ((1u << info.blueBits) - 1u);
    rgba[0] = static_cast<BYTE>(ExpandChannel(red, info.redBits));
    rgba[1] = static_cast<BYTE>(ExpandChannel(green, info.greenBits));
    rgba[2] = static_cast<BYTE>(ExpandChannel(blue, info.blueBits));
    rgba[3] = info.alphaBits > 0
                  ? static_cast<BYTE>(ExpandChannel((raw >> info.alphaShift) & ((1u << info.alphaBits) - 1u),
                                                    info.alphaBits))
                  : static_cast<BYTE>(255);
}

void EncodePixel(BYTE *destination, D3DFORMAT format, const BYTE *rgba)
{
    const FormatInfo &info = Format(format);
    if (info.bytes == 0) return;

    const unsigned red = static_cast<unsigned>(rgba[0]) >> (8 - (info.redBits > 0 ? info.redBits : 8));
    const unsigned green = static_cast<unsigned>(rgba[1]) >> (8 - (info.greenBits > 0 ? info.greenBits : 8));
    const unsigned blue = static_cast<unsigned>(rgba[2]) >> (8 - (info.blueBits > 0 ? info.blueBits : 8));
    unsigned raw = (red << info.redShift) | (green << info.greenShift) | (blue << info.blueShift);
    if (info.alphaBits > 0)
        raw |= ((static_cast<unsigned>(rgba[3]) >> (8 - info.alphaBits)) & ((1u << info.alphaBits) - 1u))
               << info.alphaShift;

    if (info.bytes == 2)
    {
        destination[0] = static_cast<BYTE>(raw & 0xffu);
        destination[1] = static_cast<BYTE>((raw >> 8) & 0xffu);
    }
    else if (info.bytes == 3)
    {
        destination[0] = static_cast<BYTE>(raw & 0xffu);
        destination[1] = static_cast<BYTE>((raw >> 8) & 0xffu);
        destination[2] = static_cast<BYTE>((raw >> 16) & 0xffu);
    }
    else
    {
        destination[0] = static_cast<BYTE>(raw & 0xffu);
        destination[1] = static_cast<BYTE>((raw >> 8) & 0xffu);
        destination[2] = static_cast<BYTE>((raw >> 16) & 0xffu);
        destination[3] = static_cast<BYTE>((raw >> 24) & 0xffu);
    }
}

void Identity(D3DMATRIX *matrix)
{
    memset(matrix, 0, sizeof(*matrix));
    matrix->_11 = matrix->_22 = matrix->_33 = matrix->_44 = 1.0f;
}

UINT VertexCount(D3DPRIMITIVETYPE type, UINT primitiveCount)
{
    if (type == D3DPT_POINTLIST) return primitiveCount;
    if (type == D3DPT_LINELIST) return primitiveCount * 2;
    if (type == D3DPT_LINESTRIP) return primitiveCount + 1;
    if (type == D3DPT_TRIANGLELIST) return primitiveCount * 3;
    return primitiveCount + 2;
}

bool TextureOperationUsesTexture(DWORD operation, DWORD argument1, DWORD argument2)
{
    if (operation == D3DTOP_DISABLE) return false;
    if (operation == D3DTOP_SELECTARG1) return (argument1 & D3DTA_SELECTMASK) == D3DTA_TEXTURE;
    return (argument1 & D3DTA_SELECTMASK) == D3DTA_TEXTURE ||
           (argument2 & D3DTA_SELECTMASK) == D3DTA_TEXTURE;
}

int Multiply(int a, int b) { return (a * b + 127) / 255; }

int BlendArgument(DWORD function, const int source[4], const int destination[4])
{
    switch (function)
    {
    case D3DBLEND_ZERO: return 0;
    case D3DBLEND_ONE: return 255;
    case D3DBLEND_SRCCOLOR: return source[0];
    case D3DBLEND_INVSRCCOLOR: return 255 - source[0];
    case D3DBLEND_SRCALPHA: return source[3];
    case D3DBLEND_INVSRCALPHA: return 255 - source[3];
    case D3DBLEND_DESTALPHA: return destination[3];
    case D3DBLEND_INVDESTALPHA: return 255 - destination[3];
    case D3DBLEND_DESTCOLOR: return destination[0];
    case D3DBLEND_INVDESTCOLOR: return 255 - destination[0];
    case D3DBLEND_SRCALPHASAT:
    {
        // min(As, 1 - Ad) applied to every colour channel.
        const int inverse = 255 - destination[3];
        return source[3] < inverse ? source[3] : inverse;
    }
    default: return 255;
    }
}

class SoftwareTexture;

class SoftwareSurface : public IDirect3DSurface8
{
  public:
    SoftwareSurface(UINT width_, UINT height_, D3DFORMAT format_, bool backbuffer_,
                    SoftwareTexture *owner_)
        : refs(1), width(width_), height(height_), format(format_), backbuffer(backbuffer_),
          owner(owner_), locked(false), lockedReadOnly(false)
    {
        pitch = width * FormatBytes(format);
        pixels.assign(static_cast<size_t>(pitch) * height, 0);
    }
    ULONG AddRef() { return ++refs; }
    ULONG Release() { ULONG value = --refs; if (value == 0) delete this; return value; }
    HRESULT GetDesc(D3DSURFACE_DESC *description)
    {
        if (description == NULL) return E_INVALIDARG;
        memset(description, 0, sizeof(*description));
        description->Format = format;
        description->Type = D3DRTYPE_SURFACE;
        description->Pool = backbuffer ? D3DPOOL_DEFAULT : D3DPOOL_SYSTEMMEM;
        description->Size = static_cast<UINT>(pixels.size());
        description->Width = width;
        description->Height = height;
        return S_OK;
    }
    HRESULT LockRect(D3DLOCKED_RECT *lockedRect, const RECT *rect, DWORD flags)
    {
        if (lockedRect == NULL || locked) return E_INVALIDARG;
        const UINT left = rect != NULL && rect->left > 0 ? static_cast<UINT>(rect->left) : 0;
        const UINT top = rect != NULL && rect->top > 0 ? static_cast<UINT>(rect->top) : 0;
        if (left >= width || top >= height) return E_INVALIDARG;
        locked = true;
        lockedReadOnly = (flags & D3DLOCK_READONLY) != 0;
        lockedRect->Pitch = static_cast<INT>(pitch);
        lockedRect->pBits = &pixels[static_cast<size_t>(top) * pitch + left * FormatBytes(format)];
        return S_OK;
    }
    HRESULT UnlockRect()
    {
        if (!locked) return E_INVALIDARG;
        locked = false;
        lockedReadOnly = false;
        return S_OK;
    }
    HRESULT GetDC(HDC *dc)
    {
        // The game never takes a device context; keep the call harmless.
        if (dc == NULL) return E_INVALIDARG;
        *dc = const_cast<void *>(static_cast<const void *>(this));
        return S_OK;
    }
    HRESULT ReleaseDC(HDC) { return S_OK; }

    ULONG refs;
    UINT width, height, pitch;
    D3DFORMAT format;
    bool backbuffer;
    SoftwareTexture *owner;
    bool locked, lockedReadOnly;
    std::vector<BYTE> pixels;
};

class SoftwareTexture : public IDirect3DTexture8
{
  public:
    SoftwareTexture(UINT width, UINT height, D3DFORMAT format) : refs(1), priority(0)
    {
        surface = new SoftwareSurface(width, height, format, false, this);
    }
    ~SoftwareTexture()
    {
        surface->owner = NULL;
        surface->Release();
    }
    ULONG AddRef() { return ++refs; }
    ULONG Release() { ULONG value = --refs; if (value == 0) delete this; return value; }
    DWORD SetPriority(DWORD value) { DWORD previous = priority; priority = value; return previous; }
    void PreLoad() {}
    HRESULT GetLevelDesc(UINT level, D3DSURFACE_DESC *description)
    {
        return level == 0 ? surface->GetDesc(description) : E_INVALIDARG;
    }
    HRESULT GetSurfaceLevel(UINT level, IDirect3DSurface8 **result)
    {
        if (level != 0 || result == NULL) return E_INVALIDARG;
        surface->AddRef();
        *result = surface;
        return S_OK;
    }
    HRESULT LockRect(UINT level, D3DLOCKED_RECT *lockedRect, const RECT *rect, DWORD flags)
    {
        return level == 0 ? surface->LockRect(lockedRect, rect, flags) : E_INVALIDARG;
    }
    HRESULT UnlockRect(UINT level)
    {
        return level == 0 ? surface->UnlockRect() : E_INVALIDARG;
    }

    ULONG refs;
    DWORD priority;
    SoftwareSurface *surface;
};

class SoftwareVertexBuffer : public IDirect3DVertexBuffer8
{
  public:
    explicit SoftwareVertexBuffer(UINT size) : refs(1), bytes(size) {}
    ULONG AddRef() { return ++refs; }
    ULONG Release() { ULONG value = --refs; if (value == 0) delete this; return value; }
    HRESULT Lock(UINT offset, UINT size, BYTE **data, DWORD)
    {
        if (data == NULL || offset > bytes.size()) return E_INVALIDARG;
        if (size == 0) size = static_cast<UINT>(bytes.size() - offset);
        if (offset + size > bytes.size()) return E_INVALIDARG;
        *data = bytes.empty() ? NULL : &bytes[offset];
        return S_OK;
    }
    HRESULT Unlock() { return S_OK; }

    ULONG refs;
    std::vector<BYTE> bytes;
};

// One vertex in homogeneous window coordinates: x, y, z and every attribute
// are pre-divided by w so the rasterizer can interpolate linearly in screen
// space and divide once per pixel.
struct Vertex
{
    Vertex() : x(0.0f), y(0.0f), z(0.0f), w(1.0f), fog(0.0f), u(0.0f), v(0.0f),
               r(1.0f), g(1.0f), b(1.0f), a(1.0f) {}
    float x, y, z, w;
    float fog;
    float u, v;
    float r, g, b, a;
};

struct ClippedVertex
{
    float x, y, z, w;
    float fog, u, v, r, g, b, a;
};

class SoftwareDevice : public IDirect3DDevice8
{
  public:
    SoftwareDevice(SoftwarePresenter *presenter_, HWND window_, const D3DPRESENT_PARAMETERS &parameters)
        : refs(1), presenter(presenter_), window(window_), backbuffer(NULL), texture(NULL),
          vertexBuffer(NULL), fvf(0), streamStride(0), framebufferReady(false), presentCount(0)
    {
        memset(renderStates, 0, sizeof(renderStates));
        memset(textureStates, 0, sizeof(textureStates));
        Identity(&world);
        Identity(&view);
        Identity(&projection);
        Identity(&textureTransform);
        framebufferReady = ResetInternal(parameters);
        renderStates[D3DRS_TEXTUREFACTOR] = 0xffffffffu;
        renderStates[D3DRS_SRCBLEND] = D3DBLEND_SRCALPHA;
        renderStates[D3DRS_DESTBLEND] = D3DBLEND_INVSRCALPHA;
        renderStates[D3DRS_ZWRITEENABLE] = TRUE;
        renderStates[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
        renderStates[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
        textureStates[D3DTSS_COLOROP] = D3DTOP_MODULATE;
        textureStates[D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        textureStates[D3DTSS_COLORARG2] = D3DTA_DIFFUSE;
        textureStates[D3DTSS_ALPHAOP] = D3DTOP_MODULATE;
        textureStates[D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        textureStates[D3DTSS_ALPHAARG2] = D3DTA_DIFFUSE;
        textureStates[D3DTSS_ADDRESSU] = D3DTADDRESS_WRAP;
        textureStates[D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
        textureStates[D3DTSS_MINFILTER] = D3DTEXF_POINT;
        textureStates[D3DTSS_MAGFILTER] = D3DTEXF_POINT;
    }
    ~SoftwareDevice()
    {
        if (texture != NULL) texture->Release();
        if (vertexBuffer != NULL) vertexBuffer->Release();
        if (backbuffer != NULL) backbuffer->Release();
    }

    bool Ready() const { return backbuffer != NULL && framebufferReady; }

    ULONG AddRef() { return ++refs; }
    ULONG Release() { ULONG value = --refs; if (value == 0) delete this; return value; }
    HRESULT TestCooperativeLevel() { return S_OK; }
    HRESULT Reset(D3DPRESENT_PARAMETERS *parameters)
    {
        if (parameters == NULL) return E_INVALIDARG;
        framebufferReady = ResetInternal(*parameters);
        return framebufferReady ? S_OK : E_FAIL;
    }
    HRESULT Present(const RECT *, const RECT *, HWND, const RGNDATA *)
    {
        if (backbuffer == NULL || presenter == NULL) return E_FAIL;

        const int width = static_cast<int>(backbuffer->width);
        const int height = static_cast<int>(backbuffer->height);
        if (width <= 0 || height <= 0) return S_OK;

        if (static_cast<int>(frame.size()) != width * height * 4)
            frame.resize(static_cast<size_t>(width) * height * 4);
        const UINT bytes = FormatBytes(backbuffer->format);
        if (backbuffer->format == D3DFMT_A8R8G8B8 ||
            backbuffer->format == D3DFMT_X8R8G8B8)
        {
            const bool alpha = backbuffer->format == D3DFMT_A8R8G8B8;
            for (int y = 0; y < height; ++y)
            {
                const BYTE *sourceRow = &backbuffer->pixels[static_cast<size_t>(y) * backbuffer->pitch];
                BYTE *targetRow = &frame[static_cast<size_t>(y) * width * 4];
                for (int x = 0; x < width; ++x)
                {
                    const BYTE *source = sourceRow + x * 4;
                    BYTE *target = targetRow + x * 4;
                    target[0] = source[2];
                    target[1] = source[1];
                    target[2] = source[0];
                    target[3] = alpha ? source[3] : 255;
                }
            }
        }
        else
        {
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    DecodePixel(&backbuffer->pixels[static_cast<size_t>(y) * backbuffer->pitch + x * bytes],
                                backbuffer->format, &frame[(y * width + x) * 4]);
        }

        ++presentCount;
        if (presenter != NULL) presenter->BeginFrame();
        presenter->Present(&frame[0], width, height, width * 4);
        return S_OK;
    }
    HRESULT GetBackBuffer(UINT index, D3DBACKBUFFER_TYPE, IDirect3DSurface8 **result)
    {
        if (index != 0 || result == NULL || backbuffer == NULL) return E_INVALIDARG;
        backbuffer->AddRef();
        *result = backbuffer;
        return S_OK;
    }
    HRESULT CreateTexture(UINT width, UINT height, UINT, DWORD, D3DFORMAT format, D3DPOOL,
                          IDirect3DTexture8 **result)
    {
        if (result == NULL || width == 0 || height == 0) return E_INVALIDARG;
        if (format == D3DFMT_UNKNOWN) format = D3DFMT_A8R8G8B8;
        *result = new(std::nothrow) SoftwareTexture(width, height, format);
        return *result != NULL ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT CreateVertexBuffer(UINT size, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer8 **result)
    {
        if (result == NULL) return E_INVALIDARG;
        *result = new(std::nothrow) SoftwareVertexBuffer(size);
        return *result != NULL ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT CreateRenderTarget(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE, BOOL,
                               IDirect3DSurface8 **result)
    {
        return CreateSurface(width, height, format, result);
    }
    HRESULT CreateImageSurface(UINT width, UINT height, D3DFORMAT format, IDirect3DSurface8 **result)
    {
        return CreateSurface(width, height, format, result);
    }
    HRESULT CopyRects(IDirect3DSurface8 *sourceRaw, const RECT *sourceRects, UINT count,
                      IDirect3DSurface8 *destinationRaw, const POINT *destinationPoints)
    {
        SoftwareSurface *source = static_cast<SoftwareSurface *>(sourceRaw);
        SoftwareSurface *destination = static_cast<SoftwareSurface *>(destinationRaw);
        if (source == NULL || destination == NULL) return E_INVALIDARG;
        if (source->format != destination->format) return E_NOTIMPL;
        if (count == 0) count = 1;
        const UINT bytes = FormatBytes(source->format);
        for (UINT index = 0; index < count; ++index)
        {
            RECT rect;
            if (sourceRects != NULL) rect = sourceRects[index];
            else
            {
                rect.left = 0; rect.top = 0;
                rect.right = static_cast<LONG>(source->width);
                rect.bottom = static_cast<LONG>(source->height);
            }
            POINT point;
            point.x = destinationPoints != NULL ? destinationPoints[index].x : 0;
            point.y = destinationPoints != NULL ? destinationPoints[index].y : 0;
            if (point.x < 0 || point.y < 0 || rect.left < 0 || rect.top < 0) continue;
            UINT copyWidth = rect.right > rect.left ? static_cast<UINT>(rect.right - rect.left) : 0;
            UINT copyHeight = rect.bottom > rect.top ? static_cast<UINT>(rect.bottom - rect.top) : 0;
            if (static_cast<UINT>(point.x) + copyWidth > destination->width)
                copyWidth = destination->width - static_cast<UINT>(point.x);
            if (static_cast<UINT>(point.y) + copyHeight > destination->height)
                copyHeight = destination->height - static_cast<UINT>(point.y);
            for (UINT y = 0; y < copyHeight; ++y)
                memcpy(&destination->pixels[static_cast<size_t>(point.y + y) * destination->pitch +
                                            static_cast<UINT>(point.x) * bytes],
                       &source->pixels[static_cast<size_t>(rect.top + y) * source->pitch +
                                       static_cast<UINT>(rect.left) * bytes],
                       copyWidth * bytes);
        }
        return S_OK;
    }
    HRESULT BeginScene() { return S_OK; }
    HRESULT EndScene() { return S_OK; }
    HRESULT Clear(DWORD, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float depth, DWORD)
    {
        if (backbuffer == NULL) return E_FAIL;

        if (flags & D3DCLEAR_TARGET)
        {
            int left = static_cast<int>(viewport.X);
            int top = static_cast<int>(viewport.Y);
            int right = static_cast<int>(viewport.Width);
            int bottom = static_cast<int>(viewport.Height);
            if (rects != NULL)
            {
                left = rects[0].x1;
                top = rects[0].y1;
                right = rects[0].x2;
                bottom = rects[0].y2;
            }
            if (left < 0) left = 0;
            if (top < 0) top = 0;
            if (right > static_cast<int>(backbuffer->width)) right = static_cast<int>(backbuffer->width);
            if (bottom > static_cast<int>(backbuffer->height)) bottom = static_cast<int>(backbuffer->height);

            BYTE rgba[4];
            rgba[0] = static_cast<BYTE>((color >> 16) & 0xff);
            rgba[1] = static_cast<BYTE>((color >> 8) & 0xff);
            rgba[2] = static_cast<BYTE>(color & 0xff);
            rgba[3] = static_cast<BYTE>((color >> 24) & 0xff);
            const UINT bytes = FormatBytes(backbuffer->format);
            for (int y = top; y < bottom; ++y)
            {
                BYTE *row = &backbuffer->pixels[static_cast<size_t>(y) * backbuffer->pitch];
                for (int x = left; x < right; ++x) EncodePixel(&row[x * bytes], backbuffer->format, rgba);
            }
        }
        if (flags & D3DCLEAR_ZBUFFER)
        {
            std::fill(depthBuffer.begin(), depthBuffer.end(), static_cast<WORD>(depth * 65535.0f));
        }
        return S_OK;
    }
    HRESULT SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix)
    {
        if (matrix == NULL) return E_INVALIDARG;
        if (state == D3DTS_WORLD) world = *matrix;
        else if (state == D3DTS_VIEW) view = *matrix;
        else if (state == D3DTS_PROJECTION) projection = *matrix;
        else if (state == D3DTS_TEXTURE0) textureTransform = *matrix;
        return S_OK;
    }
    HRESULT SetViewport(const D3DVIEWPORT8 *value)
    {
        if (value == NULL) return E_INVALIDARG;
        viewport = *value;
        return S_OK;
    }
    HRESULT GetViewport(D3DVIEWPORT8 *value)
    {
        if (value == NULL) return E_INVALIDARG;
        *value = viewport;
        return S_OK;
    }
    HRESULT SetRenderState(D3DRENDERSTATETYPE state, DWORD value)
    {
        if (static_cast<UINT>(state) < 256) renderStates[state] = value;
        return S_OK;
    }
    HRESULT SetTexture(DWORD stage, IDirect3DTexture8 *value)
    {
        if (stage != 0) return S_OK;
        SoftwareTexture *next = static_cast<SoftwareTexture *>(value);
        if (next != NULL) next->AddRef();
        if (texture != NULL) texture->Release();
        texture = next;
        return S_OK;
    }
    HRESULT SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value)
    {
        if (stage == 0 && static_cast<UINT>(state) < 32) textureStates[state] = value;
        return S_OK;
    }
    HRESULT SetVertexShader(DWORD value) { fvf = value; return S_OK; }
    HRESULT SetStreamSource(UINT stream, IDirect3DVertexBuffer8 *value, UINT stride)
    {
        if (stream != 0) return E_INVALIDARG;
        SoftwareVertexBuffer *next = static_cast<SoftwareVertexBuffer *>(value);
        if (next != NULL) next->AddRef();
        if (vertexBuffer != NULL) vertexBuffer->Release();
        vertexBuffer = next;
        streamStride = stride;
        return S_OK;
    }
    HRESULT DrawPrimitive(D3DPRIMITIVETYPE type, UINT startVertex, UINT primitiveCount)
    {
        if (vertexBuffer == NULL || streamStride == 0) return E_FAIL;
        const UINT offset = startVertex * streamStride;
        const UINT count = VertexCount(type, primitiveCount);
        if (static_cast<size_t>(offset) + static_cast<size_t>(count) * streamStride > vertexBuffer->bytes.size())
            return E_INVALIDARG;
        return Draw(type, primitiveCount, &vertexBuffer->bytes[offset], streamStride);
    }
    HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT primitiveCount, const void *data, UINT stride)
    {
        return data != NULL ? Draw(type, primitiveCount, static_cast<const BYTE *>(data), stride)
                            : E_INVALIDARG;
    }
    HRESULT GetDeviceCaps(D3DCAPS8 *caps)
    {
        if (caps == NULL) return E_INVALIDARG;
        memset(caps, 0, sizeof(*caps));
        caps->DeviceType = D3DDEVTYPE_HAL;
        caps->PresentationIntervals = D3DPRESENT_INTERVAL_ONE | D3DPRESENT_INTERVAL_IMMEDIATE;
        caps->DevCaps = D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_HWRASTERIZATION |
                        D3DDEVCAPS_TEXTURESYSTEMMEMORY | D3DDEVCAPS_TEXTUREVIDEOMEMORY |
                        D3DDEVCAPS_TLVERTEXSYSTEMMEMORY | D3DDEVCAPS_TLVERTEXVIDEOMEMORY;
        caps->MaxTextureWidth = caps->MaxTextureHeight = 4096;
        caps->MaxTextureBlendStages = 1;
        caps->MaxSimultaneousTextures = 1;
        caps->MaxPrimitiveCount = 0x100000;
        caps->MaxStreams = 1;
        caps->MaxStreamStride = 256;
        caps->TextureOpCaps = D3DTEXOPCAPS_ADD | D3DTEXOPCAPS_MODULATE | D3DTEXOPCAPS_SELECTARG1;
        return S_OK;
    }
    HRESULT ResourceManagerDiscardBytes(DWORD) { return S_OK; }

  private:
    HRESULT CreateSurface(UINT width, UINT height, D3DFORMAT format, IDirect3DSurface8 **result)
    {
        if (result == NULL || width == 0 || height == 0) return E_INVALIDARG;
        if (format == D3DFMT_UNKNOWN) format = D3DFMT_A8R8G8B8;
        *result = new(std::nothrow) SoftwareSurface(width, height, format, false, NULL);
        return *result != NULL ? S_OK : E_OUTOFMEMORY;
    }
    bool ResetInternal(const D3DPRESENT_PARAMETERS &parameters)
    {
        const UINT width = parameters.BackBufferWidth != 0 ? parameters.BackBufferWidth : 640;
        const UINT height = parameters.BackBufferHeight != 0 ? parameters.BackBufferHeight : 480;
        D3DFORMAT format = parameters.BackBufferFormat;
        if (format == D3DFMT_UNKNOWN) format = D3DFMT_X8R8G8B8;

        if (backbuffer != NULL) backbuffer->Release();
        backbuffer = new(std::nothrow) SoftwareSurface(width, height, format, true, NULL);
        if (backbuffer == NULL) return false;
        depthBuffer.assign(static_cast<size_t>(width) * height, 0xffff);
        viewport.X = viewport.Y = 0;
        viewport.Width = width;
        viewport.Height = height;
        viewport.MinZ = 0.0f;
        viewport.MaxZ = 1.0f;
        return true;
    }

    void TransformPosition(const float *position, bool transformed, Vertex *vertex) const
    {
        if (transformed)
        {
            // Matches the desktop GL back end: RHW carries the reciprocal w and
            // the 0.5 bias moves sampling onto D3D pixel centres.
            const float w = position[3] != 0.0f ? 1.0f / position[3] : 1.0f;
            vertex->x = (position[0] + 0.5f) * w;
            vertex->y = (position[1] + 0.5f) * w;
            vertex->z = position[2] * w;
            vertex->w = w;
            vertex->fog = 0.0f;
            return;
        }

        float vector[4] = {position[0], position[1], position[2], 1.0f};
        const D3DMATRIX *matrices[3] = {&world, &view, &projection};
        float fog = 0.0f;
        for (int index = 0; index < 3; ++index)
        {
            const D3DMATRIX &matrix = *matrices[index];
            float next[4];
            next[0] = vector[0] * matrix._11 + vector[1] * matrix._21 + vector[2] * matrix._31 +
                      vector[3] * matrix._41;
            next[1] = vector[0] * matrix._12 + vector[1] * matrix._22 + vector[2] * matrix._32 +
                      vector[3] * matrix._42;
            next[2] = vector[0] * matrix._13 + vector[1] * matrix._23 + vector[2] * matrix._33 +
                      vector[3] * matrix._43;
            next[3] = vector[0] * matrix._14 + vector[1] * matrix._24 + vector[2] * matrix._34 +
                      vector[3] * matrix._44;
            memcpy(vector, next, sizeof(vector));
            if (index == 1) fog = fabsf(vector[2]);
        }
        const float w = vector[3];
        vertex->x = (viewport.X + 0.5f) * w + (vector[0] + w) * viewport.Width * 0.5f;
        vertex->y = (viewport.Y + 0.5f) * w + (w - vector[1]) * viewport.Height * 0.5f;
        vertex->z = viewport.MinZ * w + vector[2] * (viewport.MaxZ - viewport.MinZ);
        vertex->w = w;
        vertex->fog = fog;
    }

    // Same folding of the texture factor into the vertex colour as the desktop
    // GL back end, used whenever the texture stage ignores the texture.
    int EffectiveRed(unsigned diffuse) const
    {
        if (textureUsesStage()) return static_cast<int>((diffuse >> 16) & 0xff);
        const DWORD factor = renderStates[D3DRS_TEXTUREFACTOR];
        const DWORD modifier =
            (textureStates[D3DTSS_COLORARG2] & D3DTA_SELECTMASK) == D3DTA_TFACTOR ? factor : diffuse;
        int rgb = static_cast<int>(modifier & 0x00ffffffu);
        if (textureStates[D3DTSS_COLOROP] == D3DTOP_SELECTARG1 &&
            (textureStates[D3DTSS_COLORARG1] & D3DTA_SELECTMASK) == D3DTA_TEXTURE)
            rgb = 0x00ffffff;
        return (rgb >> 16) & 0xff;
    }

    int EffectiveAlpha(unsigned diffuse) const
    {
        if (textureUsesStage()) return static_cast<int>((diffuse >> 24) & 0xff);
        const DWORD factor = renderStates[D3DRS_TEXTUREFACTOR];
        const DWORD modifier =
            (textureStates[D3DTSS_ALPHAARG2] & D3DTA_SELECTMASK) == D3DTA_TFACTOR ? factor : diffuse;
        int alpha = static_cast<int>(modifier & 0xff000000u);
        if (textureStates[D3DTSS_ALPHAOP] == D3DTOP_SELECTARG1 &&
            (textureStates[D3DTSS_ALPHAARG1] & D3DTA_SELECTMASK) == D3DTA_TEXTURE)
            alpha = 0xff000000;
        return (alpha >> 24) & 0xff;
    }

    bool textureUsesStage() const
    {
        if (texture == NULL || texture->surface == NULL) return false;
        return TextureOperationUsesTexture(textureStates[D3DTSS_COLOROP],
                                           textureStates[D3DTSS_COLORARG1],
                                           textureStates[D3DTSS_COLORARG2]) ||
               TextureOperationUsesTexture(textureStates[D3DTSS_ALPHAOP],
                                           textureStates[D3DTSS_ALPHAARG1],
                                           textureStates[D3DTSS_ALPHAARG2]);
    }

    void SampleTexture(float u, float v, BYTE *rgba) const
    {
        const SoftwareSurface *source = texture->surface;
        const int width = static_cast<int>(source->width);
        const int height = static_cast<int>(source->height);
        const UINT bytes = FormatBytes(source->format);
        const DWORD addressU = textureStates[D3DTSS_ADDRESSU];
        const DWORD addressV = textureStates[D3DTSS_ADDRESSV];
        const bool linear =
            textureStates[D3DTSS_MINFILTER] == D3DTEXF_LINEAR ||
            textureStates[D3DTSS_MAGFILTER] == D3DTEXF_LINEAR;

        // D3D8 texel centres sit at integers, so texel i covers [i-0.5, i+0.5).
        const float fu = u * static_cast<float>(width);
        const float fv = v * static_cast<float>(height);
        int x0 = static_cast<int>(floorf(fu));
        int y0 = static_cast<int>(floorf(fv));

        if (addressU == D3DTADDRESS_CLAMP)
        {
            if (x0 < 0) x0 = 0;
            if (x0 > width - 1) x0 = width - 1;
        }
        else if (x0 < 0 || x0 >= width)
        {
            x0 = ((x0 % width) + width) % width;
        }
        if (addressV == D3DTADDRESS_CLAMP)
        {
            if (y0 < 0) y0 = 0;
            if (y0 > height - 1) y0 = height - 1;
        }
        else if (y0 < 0 || y0 >= height)
        {
            y0 = ((y0 % height) + height) % height;
        }

        DecodePixel(&source->pixels[static_cast<size_t>(y0) * source->pitch + x0 * bytes],
                    source->format, rgba);
        if (!linear) return;

        // Bilinear taps. Only wrap-safe neighbours are needed: the game samples
        // inside sprite extents, and linear filtering is used for scaled
        // sprites where the edge taps clamp.
        int x1 = x0 + 1, y1 = y0 + 1;
        if (addressU == D3DTADDRESS_CLAMP)
        {
            if (x1 < 0) x1 = 0;
            if (x1 > width - 1) x1 = width - 1;
        }
        else if (x1 < 0 || x1 >= width)
        {
            x1 = ((x1 % width) + width) % width;
        }
        if (addressV == D3DTADDRESS_CLAMP)
        {
            if (y1 < 0) y1 = 0;
            if (y1 > height - 1) y1 = height - 1;
        }
        else if (y1 < 0 || y1 >= height)
        {
            y1 = ((y1 % height) + height) % height;
        }

        BYTE t00[4], t10[4], t01[4], t11[4];
        DecodePixel(&source->pixels[static_cast<size_t>(y0) * source->pitch + x0 * bytes],
                    source->format, t00);
        DecodePixel(&source->pixels[static_cast<size_t>(y0) * source->pitch + x1 * bytes],
                    source->format, t10);
        DecodePixel(&source->pixels[static_cast<size_t>(y1) * source->pitch + x0 * bytes],
                    source->format, t01);
        DecodePixel(&source->pixels[static_cast<size_t>(y1) * source->pitch + x1 * bytes],
                    source->format, t11);
        const float fracX = fu - floorf(fu), fracY = fv - floorf(fv);
        for (int channel = 0; channel < 4; ++channel)
        {
            const float top = t00[channel] + (t10[channel] - t00[channel]) * fracX;
            const float bottom = t01[channel] + (t11[channel] - t01[channel]) * fracX;
            rgba[channel] = static_cast<BYTE>(top + (bottom - top) * fracY + 0.5f);
        }
    }

    // D3DTOP_MODULATE / SELECTARG1 over the argument sources used by the game.
    void CombineStage(DWORD operation, DWORD argument1, DWORD argument2, const BYTE *texel,
                      const int primary[4], const int factor[4], int out[4]) const
    {
        int source1[4], source2[4];
        for (int channel = 0; channel < 4; ++channel)
        {
            switch (argument1 & D3DTA_SELECTMASK)
            {
            case D3DTA_TEXTURE: source1[channel] = texel[channel]; break;
            case D3DTA_TFACTOR: source1[channel] = factor[channel]; break;
            default: source1[channel] = primary[channel]; break;
            }
            switch (argument2 & D3DTA_SELECTMASK)
            {
            case D3DTA_TEXTURE: source2[channel] = texel[channel]; break;
            case D3DTA_TFACTOR: source2[channel] = factor[channel]; break;
            default: source2[channel] = primary[channel]; break;
            }
        }

        switch (operation)
        {
        case D3DTOP_SELECTARG1:
            memcpy(out, source1, sizeof(int) * 4);
            break;
        case D3DTOP_DISABLE:
            // The desktop back end leaves the primary colour untouched when the
            // stage ignores the texture; mirror that here.
            memcpy(out, primary, sizeof(int) * 4);
            break;
        default:
            for (int channel = 0; channel < 4; ++channel) out[channel] = Multiply(source1[channel], source2[channel]);
            break;
        }
    }

    void ShadeTriangle(const Vertex &v0, const Vertex &v1, const Vertex &v2)
    {
        if (backbuffer == NULL) return;

        const double x0 = v0.x / v0.w, y0 = v0.y / v0.w;
        const double x1 = v1.x / v1.w, y1 = v1.y / v1.w;
        const double x2 = v2.x / v2.w, y2 = v2.y / v2.w;

        const double area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
        if (area == 0.0 || area != area) return;

        int left = static_cast<int>(floorf(static_cast<float>(x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2))));
        int right = static_cast<int>(ceilf(static_cast<float>(x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2))));
        int top = static_cast<int>(floorf(static_cast<float>(y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2))));
        int bottom = static_cast<int>(ceilf(static_cast<float>(y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2))));

        // Clip to the viewport, which the desktop back end emulates with a GL
        // scissor rectangle.
        if (left < static_cast<int>(viewport.X)) left = static_cast<int>(viewport.X);
        if (top < static_cast<int>(viewport.Y)) top = static_cast<int>(viewport.Y);
        if (right > static_cast<int>(viewport.X + viewport.Width)) right = static_cast<int>(viewport.X + viewport.Width);
        if (bottom > static_cast<int>(viewport.Y + viewport.Height)) bottom = static_cast<int>(viewport.Y + viewport.Height);
        if (left < 0) left = 0;
        if (top < 0) top = 0;
        if (right > static_cast<int>(backbuffer->width)) right = static_cast<int>(backbuffer->width);
        if (bottom > static_cast<int>(backbuffer->height)) bottom = static_cast<int>(backbuffer->height);
        if (left >= right || top >= bottom) return;

        const bool useTexture = textureUsesStage();
        const DWORD alphaFunc = renderStates[D3DRS_ALPHAFUNC];
        const int alphaRef = static_cast<int>(renderStates[D3DRS_ALPHAREF] & 0xff);
        const bool alphaTest = renderStates[D3DRS_ALPHATESTENABLE] != 0;
        const bool alphaBlend = renderStates[D3DRS_ALPHABLENDENABLE] != 0;
        const DWORD srcBlend = renderStates[D3DRS_SRCBLEND];
        const DWORD destBlend = renderStates[D3DRS_DESTBLEND];
        const bool depthTest = renderStates[D3DRS_ZENABLE] != 0;
        const bool depthWrite = renderStates[D3DRS_ZWRITEENABLE] != 0;
        const DWORD depthFunc = renderStates[D3DRS_ZFUNC];

        const bool fogEnabled = renderStates[D3DRS_FOGENABLE] != 0 &&
                                renderStates[D3DRS_FOGVERTEXMODE] == D3DFOG_LINEAR;
        float fogStart = 0.0f, fogEnd = 0.0f;
        memcpy(&fogStart, &renderStates[D3DRS_FOGSTART], sizeof(fogStart));
        memcpy(&fogEnd, &renderStates[D3DRS_FOGEND], sizeof(fogEnd));
        const DWORD fogColor = renderStates[D3DRS_FOGCOLOR];
        const int fogRed = static_cast<int>((fogColor >> 16) & 0xff);
        const int fogGreen = static_cast<int>((fogColor >> 8) & 0xff);
        const int fogBlue = static_cast<int>(fogColor & 0xff);
        const float fogRange = fogEnd - fogStart;

        const DWORD factorValue = renderStates[D3DRS_TEXTUREFACTOR];
        const int factor[4] = {static_cast<int>((factorValue >> 16) & 0xff),
                               static_cast<int>((factorValue >> 8) & 0xff),
                               static_cast<int>(factorValue & 0xff),
                               static_cast<int>((factorValue >> 24) & 0xff)};

        const UINT bytes = FormatBytes(backbuffer->format);
        const double invW0 = v0.w != 0.0f ? 1.0 / v0.w : 0.0;
        const double invW1 = v1.w != 0.0f ? 1.0 / v1.w : 0.0;
        const double invW2 = v2.w != 0.0f ? 1.0 / v2.w : 0.0;

        // Edge functions are evaluated incrementally in double precision; float
        // accumulation drifts once sprite coordinates leave the 16 bit range.
        // `orientation` folds the winding into the edge values so both triangle
        // orders are accepted, matching the game's D3DCULL_NONE setting.
        const double orientation = area > 0.0 ? 1.0 : -1.0;
        // Barycentric weights come from the oriented edges divided by the area
        // magnitude, so they stay positive inside either winding.
        const double inverseArea = 1.0 / (area > 0.0 ? area : -area);
        const double edgeStep0 = orientation * -(y1 - y0);
        const double edgeStep1 = orientation * -(y2 - y1);
        const double edgeStep2 = orientation * -(y0 - y2);

        // D3D8's top-left fill rule. Adjacent sprites and the two halves of a
        // quad share edges; without this rule those pixels are shaded twice,
        // which shows up as a blended seam through backgrounds and sprites.
        // The classification uses the winding-normalized direction so a quad
        // drawn in either orientation still fills exactly the same pixels.
        const double inclusive0 = (orientation * (y1 - y0)) > 0.0 ||
                                  ((orientation * (y1 - y0)) == 0.0 && (orientation * (x1 - x0)) < 0.0);
        const double inclusive1 = (orientation * (y2 - y1)) > 0.0 ||
                                  ((orientation * (y2 - y1)) == 0.0 && (orientation * (x2 - x1)) < 0.0);
        const double inclusive2 = (orientation * (y0 - y2)) > 0.0 ||
                                  ((orientation * (y0 - y2)) == 0.0 && (orientation * (x0 - x2)) < 0.0);

        for (int py = top; py < bottom; ++py)
        {
            const double sampleY = py + 0.5;
            const double sampleX = left + 0.5;
            double edge0 = orientation * ((x1 - x0) * (sampleY - y0) - (y1 - y0) * (sampleX - x0));
            double edge1 = orientation * ((x2 - x1) * (sampleY - y1) - (y2 - y1) * (sampleX - x1));
            double edge2 = orientation * ((x0 - x2) * (sampleY - y2) - (y0 - y2) * (sampleX - x2));

            for (int px = left; px < right; ++px)
            {
                const bool in0 = edge0 > 0.0 || (edge0 == 0.0 && inclusive0);
                const bool in1 = edge1 > 0.0 || (edge1 == 0.0 && inclusive1);
                const bool in2 = edge2 > 0.0 || (edge2 == 0.0 && inclusive2);
                if (in0 && in1 && in2)
                {
                    const double b0 = edge1 * inverseArea;
                    const double b1 = edge2 * inverseArea;
                    const double b2 = edge0 * inverseArea;
                    const double denominator = b0 * invW0 + b1 * invW1 + b2 * invW2;

                    if (denominator > 1e-9)
                    {
                        const double u = (b0 * v0.u * invW0 + b1 * v1.u * invW1 + b2 * v2.u * invW2) / denominator;
                        const double v = (b0 * v0.v * invW0 + b1 * v1.v * invW1 + b2 * v2.v * invW2) / denominator;
                        const double red = (b0 * v0.r * invW0 + b1 * v1.r * invW1 + b2 * v2.r * invW2) / denominator;
                        const double green = (b0 * v0.g * invW0 + b1 * v1.g * invW1 + b2 * v2.g * invW2) / denominator;
                        const double blue = (b0 * v0.b * invW0 + b1 * v1.b * invW1 + b2 * v2.b * invW2) / denominator;
                        const double alpha = (b0 * v0.a * invW0 + b1 * v1.a * invW1 + b2 * v2.a * invW2) / denominator;
                        const double fog = (b0 * v0.fog * invW0 + b1 * v1.fog * invW1 + b2 * v2.fog * invW2) / denominator;
                        const double depth = (b0 * v0.z * invW0 + b1 * v1.z * invW1 + b2 * v2.z * invW2) / denominator;

                        int primary[4];
                        primary[0] = static_cast<int>(red * 255.0f + 0.5f);
                        primary[1] = static_cast<int>(green * 255.0f + 0.5f);
                        primary[2] = static_cast<int>(blue * 255.0f + 0.5f);
                        primary[3] = static_cast<int>(alpha * 255.0f + 0.5f);
                        for (int channel = 0; channel < 4; ++channel)
                        {
                            if (primary[channel] < 0) primary[channel] = 0;
                            if (primary[channel] > 255) primary[channel] = 255;
                        }

                        int color[4];
                        memcpy(color, primary, sizeof(color));
                        if (useTexture)
                        {
                            BYTE texel[4];
                            SampleTexture(static_cast<float>(u), static_cast<float>(v), texel);
                            if (TextureOperationUsesTexture(textureStates[D3DTSS_COLOROP],
                                                            textureStates[D3DTSS_COLORARG1],
                                                            textureStates[D3DTSS_COLORARG2]))
                                CombineStage(textureStates[D3DTSS_COLOROP],
                                             textureStates[D3DTSS_COLORARG1],
                                             textureStates[D3DTSS_COLORARG2], texel, primary, factor, color);
                            if (TextureOperationUsesTexture(textureStates[D3DTSS_ALPHAOP],
                                                            textureStates[D3DTSS_ALPHAARG1],
                                                            textureStates[D3DTSS_ALPHAARG2]))
                            {
                                int result[4];
                                CombineStage(textureStates[D3DTSS_ALPHAOP],
                                             textureStates[D3DTSS_ALPHAARG1],
                                             textureStates[D3DTSS_ALPHAARG2], texel, primary, factor, result);
                                color[3] = result[3];
                            }
                        }

                        if (alphaTest && !Compare(alphaFunc, color[3], alphaRef)) continue;

                        if (fogEnabled && fogRange != 0.0f)
                        {
                            float factorFog = static_cast<float>((fog - fogStart) / fogRange);
                            if (factorFog < 0.0f) factorFog = 0.0f;
                            if (factorFog > 1.0f) factorFog = 1.0f;
                            color[0] = static_cast<int>(color[0] + (fogRed - color[0]) * factorFog + 0.5f);
                            color[1] = static_cast<int>(color[1] + (fogGreen - color[1]) * factorFog + 0.5f);
                            color[2] = static_cast<int>(color[2] + (fogBlue - color[2]) * factorFog + 0.5f);
                        }

                        const size_t offset = static_cast<size_t>(py) * backbuffer->pitch +
                                              static_cast<size_t>(px) * bytes;
                        BYTE *destination = &backbuffer->pixels[offset];

                        if (depthTest)
                        {
                            WORD stored = depthBuffer[static_cast<size_t>(py) * backbuffer->width + px];
                            const WORD incoming = DepthToUInt16(depth);
                            if (!CompareDepth(depthFunc, incoming, stored)) continue;
                            if (depthWrite) depthBuffer[static_cast<size_t>(py) * backbuffer->width + px] = incoming;
                        }

                        BYTE rgba[4];
                        rgba[0] = static_cast<BYTE>(color[0] < 0 ? 0 : (color[0] > 255 ? 255 : color[0]));
                        rgba[1] = static_cast<BYTE>(color[1] < 0 ? 0 : (color[1] > 255 ? 255 : color[1]));
                        rgba[2] = static_cast<BYTE>(color[2] < 0 ? 0 : (color[2] > 255 ? 255 : color[2]));
                        rgba[3] = static_cast<BYTE>(color[3] < 0 ? 0 : (color[3] > 255 ? 255 : color[3]));

                        if (alphaBlend)
                        {
                            BYTE existingBytes[4];
                            DecodePixel(destination, backbuffer->format, existingBytes);
                            int existing[4];
                            for (int channel = 0; channel < 4; ++channel)
                                existing[channel] = static_cast<int>(existingBytes[channel]);
                            int sourceColor[4];
                            for (int channel = 0; channel < 4; ++channel)
                                sourceColor[channel] = static_cast<int>(rgba[channel]);
                            const int sourceFactor = BlendArgument(srcBlend, sourceColor, existing);
                            const int destinationFactor = BlendArgument(destBlend, sourceColor, existing);
                            for (int channel = 0; channel < 4; ++channel)
                            {
                                const int mixed = Multiply(sourceColor[channel], sourceFactor) +
                                                 Multiply(existing[channel], destinationFactor);
                                rgba[channel] = static_cast<BYTE>(mixed < 0 ? 0 : (mixed > 255 ? 255 : mixed));
                            }
                        }

                        EncodePixel(destination, backbuffer->format, rgba);
                    }
                }
                edge0 += edgeStep0;
                edge1 += edgeStep1;
                edge2 += edgeStep2;
            }
        }
    }

    static bool Compare(DWORD function, int source, int reference)
    {
        switch (function)
        {
        case D3DCMP_NEVER: return false;
        case D3DCMP_LESS: return source < reference;
        case D3DCMP_EQUAL: return source == reference;
        case D3DCMP_LESSEQUAL: return source <= reference;
        case D3DCMP_GREATER: return source > reference;
        case D3DCMP_NOTEQUAL: return source != reference;
        case D3DCMP_GREATEREQUAL: return source >= reference;
        default: return true;
        }
    }

    static WORD DepthToUInt16(double depth)
    {
        double scaled = depth * 65535.0;
        if (scaled < 0.0) scaled = 0.0;
        if (scaled > 65535.0) scaled = 65535.0;
        return static_cast<WORD>(scaled + 0.5);
    }

    static bool CompareDepth(DWORD function, WORD source, WORD reference)
    {
        switch (function)
        {
        case D3DCMP_NEVER: return false;
        case D3DCMP_LESS: return source < reference;
        case D3DCMP_EQUAL: return source == reference;
        case D3DCMP_LESSEQUAL: return source <= reference;
        case D3DCMP_GREATER: return source > reference;
        case D3DCMP_NOTEQUAL: return source != reference;
        case D3DCMP_GREATEREQUAL: return source >= reference;
        default: return true;
        }
    }

    // Sutherland-Hodgman clip against the near plane in homogeneous space, which
    // is where D3D8 performs its z clipping.
    void ClipTriangle(const Vertex &a, const Vertex &b, const Vertex &c)
    {
        ClippedVertex polygon[4];
        int count = 0;
        const Vertex *input[3] = {&a, &b, &c};
        for (int index = 0; index < 3; ++index)
        {
            const Vertex &vertex = *input[index];
            ClippedVertex converted;
            converted.x = vertex.x; converted.y = vertex.y;
            converted.z = vertex.z; converted.w = vertex.w;
            converted.fog = vertex.fog;
            converted.u = vertex.u; converted.v = vertex.v;
            converted.r = vertex.r; converted.g = vertex.g;
            converted.b = vertex.b; converted.a = vertex.a;
            polygon[count++] = converted;
        }

        ClippedVertex output[8];
        int outputCount = 0;
        for (int index = 0; index < count; ++index)
        {
            const ClippedVertex &current = polygon[index];
            const ClippedVertex &next = polygon[(index + 1) % count];
            const bool currentInside = current.w > kNearW;
            const bool nextInside = next.w > kNearW;
            if (currentInside) output[outputCount++] = current;
            if (currentInside != nextInside)
            {
                const float denominator = current.w - next.w;
                if (fabsf(denominator) > 1e-9f)
                {
                    const float t = (kNearW - current.w) / denominator;
                    ClippedVertex created;
                    created.x = current.x + (next.x - current.x) * t;
                    created.y = current.y + (next.y - current.y) * t;
                    created.z = current.z + (next.z - current.z) * t;
                    created.w = kNearW;
                    created.fog = current.fog + (next.fog - current.fog) * t;
                    created.u = current.u + (next.u - current.u) * t;
                    created.v = current.v + (next.v - current.v) * t;
                    created.r = current.r + (next.r - current.r) * t;
                    created.g = current.g + (next.g - current.g) * t;
                    created.b = current.b + (next.b - current.b) * t;
                    created.a = current.a + (next.a - current.a) * t;
                    output[outputCount++] = created;
                }
            }
        }
        if (outputCount < 3) return;

        Vertex fan[8];
        for (int index = 0; index < outputCount; ++index)
        {
            fan[index].x = output[index].x;
            fan[index].y = output[index].y;
            fan[index].z = output[index].z;
            fan[index].w = output[index].w;
            fan[index].fog = output[index].fog;
            fan[index].u = output[index].u;
            fan[index].v = output[index].v;
            fan[index].r = output[index].r;
            fan[index].g = output[index].g;
            fan[index].b = output[index].b;
            fan[index].a = output[index].a;
        }
        for (int index = 1; index + 1 < outputCount; ++index) ShadeTriangle(fan[0], fan[index], fan[index + 1]);
    }

    HRESULT Draw(D3DPRIMITIVETYPE type, UINT primitiveCount, const BYTE *data, UINT stride)
    {
        if (backbuffer == NULL) return E_FAIL;
        const UINT count = VertexCount(type, primitiveCount);
        const bool transformed = (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;

        UINT offset = transformed ? 16 : 12;
        if (fvf & D3DFVF_NORMAL) offset += 12;
        if (fvf & D3DFVF_PSIZE) offset += 4;
        const bool hasDiffuse = (fvf & D3DFVF_DIFFUSE) != 0;
        const UINT colorOffset = offset;
        if (hasDiffuse) offset += 4;
        if (fvf & D3DFVF_SPECULAR) offset += 4;
        const bool hasTexture = (fvf & D3DFVF_TEXCOUNT_MASK) != 0;
        const UINT textureOffset = offset;

        std::vector<Vertex> vertices(count);
        for (UINT index = 0; index < count; ++index)
        {
            const BYTE *vertex = data + index * stride;
            Vertex &target = vertices[index];
            TransformPosition(reinterpret_cast<const float *>(vertex), transformed, &target);

            if (hasDiffuse)
            {
                const D3DCOLOR color = *reinterpret_cast<const D3DCOLOR *>(vertex + colorOffset);
                target.r = EffectiveRed(color) / 255.0f;
                target.g = ((color >> 8) & 0xff) / 255.0f;
                target.b = (color & 0xff) / 255.0f;
                target.a = EffectiveAlpha(color) / 255.0f;
            }
            else
            {
                target.r = EffectiveRed(0xffffffffu) / 255.0f;
                target.g = ((0xffffffffu >> 8) & 0xff) / 255.0f;
                target.b = (0xffffffffu & 0xff) / 255.0f;
                target.a = EffectiveAlpha(0xffffffffu) / 255.0f;
            }

            if (hasTexture)
            {
                const float *uv = reinterpret_cast<const float *>(vertex + textureOffset);
                float u = uv[0], v = uv[1];
                if (!transformed)
                {
                    u = uv[0] * textureTransform._11 + uv[1] * textureTransform._21 + textureTransform._31;
                    v = uv[0] * textureTransform._12 + uv[1] * textureTransform._22 + textureTransform._32;
                }
                target.u = u;
                target.v = v;
            }
        }

        switch (type)
        {
        case D3DPT_TRIANGLELIST:
            for (UINT index = 0; index + 2 < count; index += 3)
                ClipTriangle(vertices[index], vertices[index + 1], vertices[index + 2]);
            break;
        case D3DPT_TRIANGLESTRIP:
            for (UINT index = 0; index + 2 < count; ++index)
            {
                if ((index & 1) == 0) ClipTriangle(vertices[index], vertices[index + 1], vertices[index + 2]);
                else ClipTriangle(vertices[index + 1], vertices[index], vertices[index + 2]);
            }
            break;
        case D3DPT_TRIANGLEFAN:
            for (UINT index = 1; index + 1 < count; ++index)
                ClipTriangle(vertices[0], vertices[index], vertices[index + 1]);
            break;
        case D3DPT_POINTLIST:
            for (UINT index = 0; index < count; ++index)
            {
                Vertex single = vertices[index];
                single.w = 1.0f;
                single.x += 0.5f;
                single.y += 0.5f;
                ShadeTriangle(single, single, single);
            }
            break;
        case D3DPT_LINELIST:
            for (UINT index = 0; index + 1 < count; index += 2) ShadeLine(vertices[index], vertices[index + 1]);
            break;
        case D3DPT_LINESTRIP:
            for (UINT index = 0; index + 1 < count; ++index) ShadeLine(vertices[index], vertices[index + 1]);
            break;
        default:
            return E_INVALIDARG;
        }
        return S_OK;
    }

    // Lines are rasterized as a one pixel wide Bresenham walk with the start
    // vertex's colour; the game only uses lines for simple debug shapes.
    void ShadeLine(const Vertex &from, const Vertex &to)
    {
        int x0 = static_cast<int>(from.x / from.w), y0 = static_cast<int>(from.y / from.w);
        int x1 = static_cast<int>(to.x / to.w), y1 = static_cast<int>(to.y / to.w);
        const int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        const int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int error = dx + dy;
        for (int guard = 0; guard < 1 << 16; ++guard)
        {
            if (x0 >= 0 && y0 >= 0 && x0 < static_cast<int>(backbuffer->width) &&
                y0 < static_cast<int>(backbuffer->height))
            {
                Vertex corner = from;
                corner.w = 1.0f;
                corner.x = static_cast<float>(x0) + 0.5f;
                corner.y = static_cast<float>(y0) + 0.5f;
                corner.u = 0.5f;
                corner.v = 0.5f;
                ShadeTriangle(corner, corner, corner);
            }
            if (x0 == x1 && y0 == y1) break;
            const int doubled = 2 * error;
            if (doubled >= dy) { if (x0 == x1) break; error += dy; x0 += sx; }
            if (doubled <= dx) { if (y0 == y1) break; error += dx; y0 += sy; }
        }
    }

    static const float kNearW;

    ULONG refs;
    SoftwarePresenter *presenter;
    HWND window;
    SoftwareSurface *backbuffer;
    SoftwareTexture *texture;
    SoftwareVertexBuffer *vertexBuffer;
    DWORD fvf;
    UINT streamStride;
    bool framebufferReady;
    unsigned long presentCount;
    std::vector<WORD> depthBuffer;
    std::vector<BYTE> frame;
    DWORD renderStates[256];
    DWORD textureStates[32];
    D3DMATRIX world, view, projection, textureTransform;
    D3DVIEWPORT8 viewport;
};

const float SoftwareDevice::kNearW = 1e-5f;

class SoftwareDirect3D : public IDirect3D8
{
  public:
    explicit SoftwareDirect3D(SoftwarePresenter *presenter_) : refs(1), presenter(presenter_) {}
    ULONG AddRef() { return ++refs; }
    ULONG Release() { ULONG value = --refs; if (value == 0) delete this; return value; }
    HRESULT GetAdapterDisplayMode(UINT, D3DDISPLAYMODE *mode)
    {
        if (mode == NULL) return E_INVALIDARG;
        mode->Width = 640;
        mode->Height = 480;
        mode->RefreshRate = 60;
        mode->Format = D3DFMT_X8R8G8B8;
        return S_OK;
    }
    HRESULT CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD, D3DRESOURCETYPE, D3DFORMAT)
    {
        return S_OK;
    }
    HRESULT CreateDevice(UINT, D3DDEVTYPE, HWND window, DWORD, D3DPRESENT_PARAMETERS *parameters,
                         IDirect3DDevice8 **result)
    {
        if (window == NULL || parameters == NULL || result == NULL) return E_INVALIDARG;
        // The reconstructed game creates this interface before it creates its
        // window, exactly like the original, where the call depends on no
        // window. This back end binds its presenter together with the window
        // (CreateWindowExA installs it), so the presenter is resolved here,
        // at device creation time, when the window certainly exists.
        SoftwarePresenter *bound = presenter != NULL ? presenter : SoftwarePresenterInstance();
        if (bound == NULL)
        {
            fprintf(stderr, "th08-modern: no software presenter was installed\n");
            return E_FAIL;
        }
        SoftwareDevice *device = new(std::nothrow) SoftwareDevice(bound, window, *parameters);
        if (device == NULL) return E_OUTOFMEMORY;
        if (!device->Ready()) { delete device; *result = NULL; return E_FAIL; }
        *result = device;
        return S_OK;
    }

  private:
    ULONG refs;
    SoftwarePresenter *presenter;
};
} // namespace

namespace
{
SoftwarePresenter *g_presenter;
} // namespace

IDirect3D8 *CreateSoftwareDirect3D(SoftwarePresenter *presenter)
{
    return new(std::nothrow) SoftwareDirect3D(presenter);
}

void SetSoftwarePresenter(SoftwarePresenter *presenter) { g_presenter = presenter; }

SoftwarePresenter *SoftwarePresenterInstance() { return g_presenter; }

bool SoftwareSurfaceAccess(IDirect3DSurface8 *surface, BYTE **pixels, UINT *width, UINT *height,
                           UINT *pitch, D3DFORMAT *format)
{
    if (surface == NULL || pixels == NULL) return false;
    SoftwareSurface *target = static_cast<SoftwareSurface *>(surface);
    *pixels = target->pixels.empty() ? NULL : &target->pixels[0];
    if (width != NULL) *width = target->width;
    if (height != NULL) *height = target->height;
    if (pitch != NULL) *pitch = target->pitch;
    if (format != NULL) *format = target->format;
    return true;
}
} // namespace modern
} // namespace th08

extern "C" IDirect3D8 *Direct3DCreate8(UINT sdkVersion)
{
    // The game calls this before it creates its window, matching the original
    // where the Direct3D object is independent of any window, so a missing
    // presenter is not an error here: the platform layer installs the presenter
    // together with the window, and the device resolves it at creation time.
    if (sdkVersion != D3D_SDK_VERSION) return NULL;
    return th08::modern::CreateSoftwareDirect3D(th08::modern::SoftwarePresenterInstance());
}
