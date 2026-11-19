// Issen cue for the Simplified Chinese PC 1.0.0.1 release.
// Build from an x86 Native Tools prompt:
// cl /nologo /W4 /WX /O2 /MT /LD /std:c++17 /EHsc Onimusha3.IssenCue.cpp gdi32.lib /link /OUT:Onimusha3.IssenCue.asi
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace
{
    constexpr std::uintptr_t ImageBase = 0x00400000;
    constexpr DWORD GameTimestamp = 0x4364f2d3;
    constexpr DWORD GameImageSize = 0x03f00000;

    constexpr std::uintptr_t MainEndScene = 0x0041f187;
    constexpr BYTE OriginalEndScene[] = {0x8b, 0x16, 0x8b, 0x82, 0xa8, 0, 0, 0, 0x56, 0xff, 0xd0};
    constexpr std::uintptr_t IssenSelector = 0x00407522;
    constexpr BYTE SelectorThunk[] = {0xe9, 0x09, 0x19, 0x41, 0};
    constexpr std::uintptr_t WorldToScreen = 0x00408e9f;
    constexpr BYTE ProjectionThunk[] = {0xe9, 0xec, 0x37, 0x42, 0};

    constexpr std::uintptr_t Player = 0x03caad40;
    constexpr std::uintptr_t EnemyListCursor = 0x042398f0;
    constexpr std::uintptr_t GameFlags = 0x0418a10c;
    constexpr std::uintptr_t SkipFrame = 0x031b9bfe;
    constexpr std::uintptr_t WidescreenProjectionSite = 0x00433bca;
    constexpr std::uintptr_t DeviceSettings = 0x04260a0c;

    constexpr int TextureSize = 128;
    constexpr float CueHeightFraction = 0.11f;
    constexpr float EnemyHeight = 90.0f;
    constexpr float TallEnemyHeight = 170.0f;

    BYTE* gameImage;
    std::array<DWORD, TextureSize * TextureSize> cuePixels;

    BYTE* At(std::uintptr_t address)
    {
        return gameImage + (address - ImageBase);
    }

    template<typename T>
    const T& Field(const BYTE* actor, unsigned offset)
    {
        return *reinterpret_cast<const T*>(actor + offset);
    }

    using SelectIssen = BYTE* (__fastcall *)(BYTE* player, BYTE* result);

    BYTE* GetIssenTarget()
    {
        auto* player = At(Player);
        const BYTE character = player[2];
        const BYTE state = player[8]; // Normal gameplay uses actor state 1.

        // The original cue restricts this lookup to the playable characters.
        // Honor the attack dispatcher's direct-Issen restrictions too.
        if (*At(SkipFrame) || (*reinterpret_cast<const DWORD*>(At(GameFlags)) & 0x1000) ||
            character == 3 || character > 4 || state != 1 ||
            !Field<void*>(player, 0x30) || Field<int>(player, 0x21e8) < 0 ||
            (Field<DWORD>(player, 0x4444) & 0x40000) || (Field<DWORD>(player, 0x22f4) & 0x40000))
            return nullptr;

        // This is the selector used by the attack and the orange haori. Its
        // internal evaluator automatically runs IssenWindow when present.
        auto& cursor = *reinterpret_cast<void**>(At(EnemyListCursor));
        const auto savedCursor = cursor;
        BYTE result = 0xfe;
        auto* enemy = reinterpret_cast<SelectIssen>(At(IssenSelector))(player, &result);
        cursor = savedCursor;
        return result != 0xfe ? enemy : nullptr;
    }

    struct alignas(16) WorldPoint
    {
        float x, y, z, w;
    };

    struct ScreenPoint
    {
        int x, y, z, unused;
    };

    using ProjectPoint = int (__fastcall *)(ScreenPoint*, const WorldPoint*, float* inverseDepth);

    bool ProjectEnemy(const BYTE* enemy, const D3DVIEWPORT9& viewport, float& x, float& y)
    {
        // The game's own projection uses the current camera and rejects points
        // outside its clip volume. Its output is in 12.4 fixed-point GS units.
        WorldPoint position = Field<WorldPoint>(enemy, 0x90);
        position.y += enemy[2] == 0x34 ? TallEnemyHeight : EnemyHeight;
        ScreenPoint projected{};
        float inverseDepth = 0.0f;
        if (!reinterpret_cast<ProjectPoint>(At(WorldToScreen))(&projected, &position, &inverseDepth) ||
            !(inverseDepth > 0.0f))
            return false;

        constexpr float ScreenCenter = 2048.0f * 16.0f;
        constexpr float ScreenWidth = 512.0f * 16.0f;
        constexpr float ScreenHeight = 224.0f * 16.0f;
        const float offsetX = (static_cast<float>(projected.x) - ScreenCenter) / ScreenWidth;
        const float offsetY = (static_cast<float>(projected.y) - ScreenCenter) / ScreenHeight;
        float width = static_cast<float>(viewport.Width);
        const float height = static_cast<float>(viewport.Height);

        // The installed WidescreenFix preserves the central 4:3 horizontal
        // scale for its 16:9 windowed mode. The fullscreen projection is stock.
        const auto wideDifference = viewport.Width * 9LL - viewport.Height * 16LL;
        if (At(WidescreenProjectionSite)[0] == 0xe9 && wideDifference >= -9 && wideDifference <= 9)
        {
            const auto* settings = *reinterpret_cast<const DWORD* const*>(At(DeviceSettings));
            if (settings && settings[12])
                width = height * (4.0f / 3.0f);
        }

        x = static_cast<float>(viewport.X) + static_cast<float>(viewport.Width) * 0.5f + offsetX * width;
        y = static_cast<float>(viewport.Y) + (0.5f + offsetY) * height;
        return x >= static_cast<float>(viewport.X) && y >= static_cast<float>(viewport.Y) &&
               x < static_cast<float>(viewport.X + viewport.Width) &&
               y < static_cast<float>(viewport.Y + viewport.Height);
    }

    using GlyphMask = std::array<BYTE, TextureSize * TextureSize>;

    bool ReadGlyph(HDC dc, const wchar_t* face, GlyphMask& mask)
    {
        const HFONT font = CreateFontW(-94, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, GB2312_CHARSET,
                                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                       DEFAULT_PITCH, face);
        if (!font)
            return false;

        const HGDIOBJ previous = SelectObject(dc, font);
        constexpr wchar_t Letter[] = L"\u95ea";
        const MAT2 identity = {{0, 1}, {0, 0}, {0, 0}, {0, 1}};
        GLYPHMETRICS metrics{};
        WORD glyph = 0xffff;
        std::array<BYTE, TextureSize * TextureSize> bitmap;

        DWORD bytes = GDI_ERROR;
        if (previous && previous != HGDI_ERROR &&
            GetGlyphIndicesW(dc, Letter, 1, &glyph, GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR &&
            glyph != 0xffff)
        {
            bytes = GetGlyphOutlineW(dc, glyph, GGO_GRAY8_BITMAP | GGO_GLYPH_INDEX, &metrics,
                                     static_cast<DWORD>(bitmap.size()), bitmap.data(), &identity);
        }
        if (previous && previous != HGDI_ERROR)
            SelectObject(dc, previous);
        DeleteObject(font);

        constexpr UINT MaxGlyphExtent = TextureSize - 16;
        if (!bytes || bytes == GDI_ERROR || !metrics.gmBlackBoxX || !metrics.gmBlackBoxY ||
            metrics.gmBlackBoxX > MaxGlyphExtent || metrics.gmBlackBoxY > MaxGlyphExtent)
            return false;

        const UINT pitch = (metrics.gmBlackBoxX + 3u) & ~3u;
        if (bytes < pitch * metrics.gmBlackBoxY)
            return false;
        const UINT left = (TextureSize - metrics.gmBlackBoxX) / 2;
        const UINT top = (TextureSize - metrics.gmBlackBoxY) / 2;
        for (UINT y = 0; y < metrics.gmBlackBoxY; ++y)
        {
            const auto* src = bitmap.data() + y * pitch;
            auto* dst = mask.data() + (y + top) * TextureSize + left;
            std::copy_n(src, metrics.gmBlackBoxX, dst);
        }
        return true;
    }

    struct Pixel
    {
        int r = 0;
        int g = 0;
        int b = 0;
        int a = 0;

        void Blend(int red, int green, int blue, int alpha)
        {
            const int rest = 255 - alpha;
            r = (red * alpha + r * rest + 127) / 255;
            g = (green * alpha + g * rest + 127) / 255;
            b = (blue * alpha + b * rest + 127) / 255;
            a = alpha + (a * rest + 127) / 255;
        }

        DWORD Argb() const
        {
            return D3DCOLOR_ARGB(a, r, g, b);
        }
    };

    bool CreateCuePixels()
    {
        const HDC dc = CreateCompatibleDC(nullptr);
        if (!dc)
            return false;
        GlyphMask mask{};
        const bool found = ReadGlyph(dc, L"KaiTi", mask) || ReadGlyph(dc, L"Microsoft YaHei", mask) ||
                           ReadGlyph(dc, L"SimHei", mask);
        DeleteDC(dc);
        if (!found)
            return false;

        for (int y = 7; y < TextureSize - 7; ++y)
        {
            for (int x = 7; x < TextureSize - 7; ++x)
            {
                int glow = 0;
                int border = 0;
                for (int dy = -7; dy <= 7; ++dy)
                {
                    for (int dx = -7; dx <= 7; ++dx)
                    {
                        const int distanceSquared = dx * dx + dy * dy;
                        const int coverage = mask[(y + dy) * TextureSize + x + dx];
                        if (distanceSquared <= 49)
                            glow = std::max(glow, coverage * (49 - distanceSquared) * 160 / (64 * 49));
                        if (distanceSquared <= 5)
                            border = std::max(border, coverage * 235 / 64);
                    }
                }

                const int index = y * TextureSize + x;
                const int gold = std::clamp((y - 22) * 100 / 84, 0, 100);
                Pixel pixel;
                pixel.Blend(255, 102, 12, glow);
                pixel.Blend(165, 57, 8, border);
                pixel.Blend(255, 249 - gold / 2, 176 - gold, mask[index] * 255 / 64);
                cuePixels[index] = pixel.Argb();
            }
        }
        return true;
    }

    IDirect3DTexture9* UploadCue(IDirect3DDevice9* device)
    {
        IDirect3DTexture9* texture = nullptr;
        // The texture lives only during this draw. It cannot retain the game's
        // old device or obstruct a reset after switching display modes.
        if (FAILED(device->CreateTexture(TextureSize, TextureSize, 1, 0, D3DFMT_A8R8G8B8,
                                         D3DPOOL_MANAGED, &texture, nullptr)))
            return nullptr;
        D3DLOCKED_RECT locked{};
        if (FAILED(texture->LockRect(0, &locked, nullptr, 0)))
        {
            texture->Release();
            return nullptr;
        }
        auto* row = static_cast<BYTE*>(locked.pBits);
        for (int y = 0; y < TextureSize; ++y, row += locked.Pitch)
            std::memcpy(row, cuePixels.data() + y * TextureSize, TextureSize * sizeof(DWORD));
        if (SUCCEEDED(texture->UnlockRect(0)))
            return texture;

        texture->Release();
        return nullptr;
    }

    void DrawCue(IDirect3DDevice9* device, float x, float y, float size)
    {
        auto* texture = UploadCue(device);
        if (!texture)
            return;

        IDirect3DStateBlock9* state = nullptr;
        if (SUCCEEDED(device->CreateStateBlock(D3DSBT_ALL, &state)))
        {
            // The game uses a state cache. Restore the device to what the cache
            // expects before returning to its EndScene and next frame.
            device->SetVertexShader(nullptr);
            device->SetPixelShader(nullptr);
            device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
            device->SetTexture(0, texture);
            device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
            device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
            device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
            device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
            device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
            device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
            device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
            device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
            device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
            device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
            device->SetRenderState(D3DRS_FOGENABLE, FALSE);
            device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
            device->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0f);
            device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
            device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
            device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
            device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
            device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

            struct Vertex { float x, y, z, rhw, u, v; };
            const float left = x - size * 0.5f - 0.5f;
            const float top = y - size * 0.5f - 0.5f;
            const Vertex quad[] = {
                {left,        top,        0.0f, 1.0f, 0.0f, 0.0f},
                {left + size, top,        0.0f, 1.0f, 1.0f, 0.0f},
                {left,        top + size, 0.0f, 1.0f, 0.0f, 1.0f},
                {left + size, top + size, 0.0f, 1.0f, 1.0f, 1.0f},
            };
            device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));
            state->Apply();
            state->Release();
        }
        texture->Release();
    }

    HRESULT WINAPI EndSceneWithCue(IDirect3DDevice9* device)
    {
        const auto* target = GetIssenTarget();
        D3DVIEWPORT9 viewport{};
        float x, y;
        if (target && SUCCEEDED(device->GetViewport(&viewport)) && ProjectEnemy(target, viewport, x, y))
            DrawCue(device, x, y, static_cast<float>(viewport.Height) * CueHeightFraction);

        return device->EndScene();
    }

    bool IsSupportedEdition(const BYTE* image)
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < sizeof(IMAGE_DOS_HEADER) ||
            dos->e_lfanew > 0x1000 - sizeof(IMAGE_NT_HEADERS32))
            return false;

        const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
        return pe->Signature == IMAGE_NT_SIGNATURE && pe->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
               pe->FileHeader.TimeDateStamp == GameTimestamp &&
               pe->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
               pe->OptionalHeader.SizeOfImage == GameImageSize;
    }
}

extern "C" __declspec(dllexport) void InitializeASI()
{
    auto* image = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    if (!image || !IsSupportedEdition(image))
        return;

    gameImage = image;
    BYTE* site = At(MainEndScene);
    if (std::memcmp(site, OriginalEndScene, sizeof(OriginalEndScene)) ||
        std::memcmp(At(IssenSelector), SelectorThunk, sizeof(SelectorThunk)) ||
        std::memcmp(At(WorldToScreen), ProjectionThunk, sizeof(ProjectionThunk)) ||
        !CreateCuePixels())
        return;

    // Replace the game's final COM call with an ABI-compatible call that draws
    // first and returns the original HRESULT. ESI holds the device at this site.
    BYTE replacement[sizeof(OriginalEndScene)];
    std::memset(replacement, 0x90, sizeof(replacement));
    replacement[0] = 0x56; // push esi
    replacement[1] = 0xe8; // call rel32
    const auto relative = reinterpret_cast<std::uintptr_t>(&EndSceneWithCue) -
                          reinterpret_cast<std::uintptr_t>(site + 6);
    static_assert(sizeof(relative) == 4, "Build this ASI for x86.");
    std::memcpy(replacement + 2, &relative, sizeof(relative));

    DWORD oldProtection = 0;
    if (!VirtualProtect(site, sizeof(replacement), PAGE_EXECUTE_READWRITE, &oldProtection))
        return;
    std::memcpy(site, replacement, sizeof(replacement));
    FlushInstructionCache(GetCurrentProcess(), site, sizeof(replacement));
    DWORD unused = 0;
    VirtualProtect(site, sizeof(replacement), oldProtection, &unused);
}

