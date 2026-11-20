// Issen cue for the Simplified Chinese PC 1.0.0.1 release.
// Build from an x86 Native Tools prompt:
// cl /nologo /W4 /WX /O2 /MT /LD /std:c++17 /EHsc Onimusha3.IssenCue.cpp /link /OUT:Onimusha3.IssenCue.asi
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace
{
    constexpr std::uintptr_t ImageBase = 0x00400000;
    constexpr DWORD GameTimestamp = 0x4364f2d3;
    constexpr DWORD GameImageSize = 0x03f00000;

    constexpr std::uintptr_t MainEndScene = 0x0041f187;
    constexpr BYTE OriginalEndScene[] = {0x8b, 0x16, 0x8b, 0x82, 0xa8, 0, 0, 0, 0x56, 0xff, 0xd0};
    constexpr std::uintptr_t IssenSelector = 0x00407522;
    constexpr BYTE SelectorThunk[] = {0xe9, 0x09, 0x19, 0x41, 0};
    constexpr std::uintptr_t InputSelectCallSite = 0x0081a6b4;
    constexpr BYTE OriginalInputSelectCall[] = {0xe8, 0x69, 0xce, 0xbe, 0xff};
    constexpr std::uintptr_t CommitCallSite = 0x0081a6ce;
    constexpr BYTE OriginalCommitCall[] = {0xe8, 0x6f, 0xea, 0xbe, 0xff};
    constexpr std::uintptr_t IssenCommit = 0x00409142;
    constexpr BYTE CommitThunk[] = {0xe9, 0x29, 0x4d, 0x41, 0};
    constexpr std::uintptr_t WorldToScreen = 0x00408e9f;
    constexpr BYTE ProjectionThunk[] = {0xe9, 0xec, 0x37, 0x42, 0};

    constexpr std::uintptr_t Player = 0x03caad40;
    constexpr std::uintptr_t EnemyListHead = 0x04239810;
    constexpr std::uintptr_t Difficulty = 0x03cb47c4;
    constexpr std::uintptr_t IssenWindowSite = 0x00818e89;
    constexpr std::uintptr_t BlendedMotionVtable = 0x0091ccf8;
    constexpr std::uintptr_t EnemyListCursor = 0x042398f0;
    constexpr std::uintptr_t GameFlags = 0x0418a10c;
    constexpr std::uintptr_t SkipFrame = 0x031b9bfe;
    constexpr std::uintptr_t WidescreenProjectionSite = 0x00433bca;
    constexpr std::uintptr_t DeviceSettings = 0x04260a0c;

    constexpr float EnemyHeight = 90.0f;
    constexpr float TallEnemyHeight = 170.0f;
    constexpr ULONGLONG HoldAfterDeathMs = 3000;
    constexpr ULONGLONG FadeAfterDeathMs = 180;
    constexpr int MaxMissFrames = 5;
    constexpr ULONGLONG MissCueMs = 1800;

    constexpr std::uint32_t DifficultyMasks[] = {0x00200000, 0x04000000, 0x00100000, 0x00100000};

    HMODULE cueModule;
    BYTE* gameImage;
    unsigned configuredMinimumFrames;
    bool usesIssenWindow;

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

    BYTE* GetActivePlayer()
    {
        auto* player = At(Player);
        const BYTE character = player[2];
        const BYTE state = player[8]; // Normal gameplay uses actor state 1.

        // The original cue restricts this lookup to the playable characters.
        if ((*reinterpret_cast<const DWORD*>(At(GameFlags)) & 0x1000) ||
            character == 3 || character > 4 || state != 1 ||
            !Field<void*>(player, 0x30) || Field<int>(player, 0x21e8) < 0)
            return nullptr;

        return player;
    }

    const BYTE* GetIssenTarget(BYTE* player)
    {
        if ((Field<DWORD>(player, 0x4444) & 0x40000) || (Field<DWORD>(player, 0x22f4) & 0x40000))
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

    unsigned ReadMinimumFrames()
    {
        wchar_t path[MAX_PATH];
        const DWORD length = GetModuleFileNameW(cueModule, path, ARRAYSIZE(path));
        if (!length || length >= ARRAYSIZE(path))
            return 0;

        auto* slash = std::wcsrchr(path, L'\\');
        constexpr wchar_t ConfigName[] = L"Onimusha3.IssenWindow.ini";
        if (!slash || ARRAYSIZE(path) - static_cast<std::size_t>(slash + 1 - path) < ARRAYSIZE(ConfigName))
            return 0;
        std::memcpy(slash + 1, ConfigName, sizeof(ConfigName));

        wchar_t value[16];
        const DWORD chars = GetPrivateProfileStringW(L"Issen", L"MinimumFrames", L"",
                                                      value, ARRAYSIZE(value), path);
        if (!chars || chars >= ARRAYSIZE(value) - 1)
            return 0;
        wchar_t* end = nullptr;
        const long frames = std::wcstol(value, &end, 10);
        return end != value && !*end && frames >= 2 && frames <= 255 ? static_cast<unsigned>(frames) : 0;
    }

    unsigned EffectiveMinimumFrames()
    {
        if (!configuredMinimumFrames || usesIssenWindow)
            return configuredMinimumFrames;

        // Test the installed jump, so ASI load order, a disabled patch, and
        // an INI left behind after removal all give the correct baseline.
        const BYTE* site = At(IssenWindowSite);
        constexpr BYTE Padding[] = {0x90, 0x90, 0x90, 0x90, 0x90};
        if (site[0] != 0xe9 || std::memcmp(site + 5, Padding, sizeof(Padding)))
            return 0;

        const auto windowModule = GetModuleHandleW(L"Onimusha3.IssenWindow.asi");
        if (!windowModule)
            return 0;
        std::uintptr_t relative;
        std::memcpy(&relative, site + 1, sizeof(relative));
        const auto* destination = reinterpret_cast<const void*>(
            reinterpret_cast<std::uintptr_t>(site + 5) + relative);
        MEMORY_BASIC_INFORMATION region{};
        usesIssenWindow = VirtualQuery(destination, &region, sizeof(region)) &&
                          region.AllocationBase == windowModule;
        return usesIssenWindow ? configuredMinimumFrames : 0;
    }

    struct MotionFrames
    {
        const std::uint32_t* flags;
        unsigned count;
        unsigned current;
        float sample;
    };

    bool ReadMotion(const BYTE* enemy, MotionFrames& frames)
    {
        const auto* motion = Field<const BYTE*>(enemy, 0x30);
        if (!motion)
            return false;

        const bool blended = Field<const void*>(motion, 0) == At(BlendedMotionVtable);
        const unsigned channel = blended ? Field<BYTE>(motion, 0x1fc) : 0;
        if (blended ? channel >= Field<BYTE>(motion, 0x1f8) : !Field<const void*>(motion, 0x94))
            return false;

        // The single player and the selected blend track share these fields.
        const auto* flags = Field<const std::uint32_t*>(motion, 0xa4);
        const unsigned current = Field<std::uint32_t>(motion, 0xc4);
        const unsigned last = Field<std::uint16_t>(motion, 0xc8);
        if (!flags || current > last)
            return false;

        float sample = static_cast<float>(current);
        if (blended)
        {
            // The source track stores eight fraction bits. Use them when the
            // source still agrees with the index copied into the shared fields.
            const auto* tracks = Field<const BYTE*>(motion, 0x1e0);
            if (tracks)
            {
                const auto fixed = Field<std::uint32_t>(tracks + channel * 0x310, 0x2e0);
                if ((fixed >> 8) == current)
                    sample += static_cast<float>(fixed & 0xff) / 256.0f;
            }
        }
        frames = {flags, last + 1, current, sample};
        return true;
    }

    struct FrameWindow
    {
        unsigned begin;
        unsigned end; // Exclusive.

        explicit operator bool() const { return begin < end; }
    };

    FrameWindow NextWindow(const MotionFrames& frames, unsigned scan, std::uint32_t mask, unsigned minimum)
    {
        unsigned first = scan;
        while (first < frames.count && !(frames.flags[first] & mask))
            ++first;
        unsigned end = first;
        while (end < frames.count && (frames.flags[end] & mask))
            ++end;
        if (first == end)
            return {first, end};

        // IssenWindow extends each run back from its end. The previous run
        // stops its lookahead, so an extension can only fill the preceding gap.
        const unsigned earliest = end > minimum ? end - minimum : 0;
        return {std::max(scan, std::min(first, earliest)), end};
    }

    struct RingPosition
    {
        float frame;
        unsigned lastWindowEnd;
    };

    struct RingTiming
    {
        RingPosition position;
        FrameWindow window;
        bool open;
    };

    int OffsetFromWindow(unsigned current, FrameWindow window)
    {
        if (current < window.begin)
            return static_cast<int>(current) - static_cast<int>(window.begin);
        if (current >= window.end)
            return static_cast<int>(current) - static_cast<int>(window.end - 1);
        return 0;
    }

    bool CalculateTiming(const MotionFrames& frames, std::uint32_t mask, unsigned minimum,
                         RingTiming& timing, int* nearestOffset = nullptr)
    {
        FrameWindow window = NextWindow(frames, 0, mask, minimum);
        if (!window)
            return false;

        FrameWindow selected{};
        int selectedOffset = 0;
        unsigned lastWindowEnd = 0;

        while (window)
        {
            auto next = NextWindow(frames, window.end, mask, minimum);
            while (next && next.begin == window.end)
            {
                window.end = next.end;
                next = NextWindow(frames, window.end, mask, minimum);
            }

            lastWindowEnd = window.end;
            const int offset = OffsetFromWindow(frames.current, window);
            const bool selectWindow = nearestOffset
                ? !selected || std::abs(offset) <= std::abs(selectedOffset)
                : frames.current >= selected.end;
            if (selectWindow)
            {
                selected = window;
                selectedOffset = offset;
            }
            window = next;
        }

        timing = {{frames.sample, lastWindowEnd}, selected, selectedOffset == 0};
        if (nearestOffset)
            *nearestOffset = selectedOffset;
        return true;
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

    WorldPoint EnemyAnchor(const BYTE* enemy)
    {
        WorldPoint point = Field<WorldPoint>(enemy, 0x90);
        point.y += enemy[2] == 0x34 ? TallEnemyHeight : EnemyHeight;
        return point;
    }

    bool ProjectAnchor(const WorldPoint& position, const D3DVIEWPORT9& viewport, float& x, float& y)
    {
        // The game's own projection uses the current camera and rejects points
        // outside its clip volume. Its output is in 12.4 fixed-point GS units.
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

    constexpr unsigned CircleSteps = 72;
    constexpr unsigned StrokeLayers = 6;
    constexpr unsigned DashPeriod = 6;
    constexpr unsigned VisibleDashSteps = 4;
    constexpr float FullTurn = 6.28318530718f;
    constexpr float RadiusAtLastWindowEnd = 42.0f; // Pixels at 900p.
    constexpr float RadiusPerFrame = 7.0f;

    struct UnitPoint { float x, y; };
    std::array<UnitPoint, CircleSteps> circle;

    template<bool Dashed>
    constexpr auto MakeStrokeIndices()
    {
        constexpr unsigned steps = Dashed ? CircleSteps / DashPeriod * VisibleDashSteps : CircleSteps;
        std::array<WORD, steps * (StrokeLayers - 1) * 6> indices{};
        unsigned index = 0;
        for (unsigned step = 0; step < CircleSteps; ++step)
        {
            if (Dashed && step % DashPeriod >= VisibleDashSteps)
                continue;
            const unsigned next = (step + 1) % CircleSteps;
            for (unsigned layer = 0; layer + 1 < StrokeLayers; ++layer)
            {
                const auto a = static_cast<WORD>(step * StrokeLayers + layer);
                const auto b = static_cast<WORD>(next * StrokeLayers + layer);
                indices[index++] = a;
                indices[index++] = b;
                indices[index++] = a + 1;
                indices[index++] = a + 1;
                indices[index++] = b;
                indices[index++] = b + 1;
            }
        }
        return indices;
    }

    constexpr auto StrokeIndices = MakeStrokeIndices<false>();
    constexpr auto DashedStrokeIndices = MakeStrokeIndices<true>();

    float RadiusAtFrame(float height, unsigned lastWindowEnd, float frame)
    {
        // Anchor the line at the final window's closing frame. Recovery
        // remains visible until the radius reaches zero or this action ends.
        const float scale = std::max(0.5f, height / 900.0f);
        const float framesToClose = static_cast<float>(lastWindowEnd) - frame;
        return scale * std::max(0.0f, RadiusAtLastWindowEnd + framesToClose * RadiusPerFrame);
    }

    struct Vertex { float x, y, z, rhw; D3DCOLOR color; };

    void PrepareCircle()
    {
        for (unsigned step = 0; step < CircleSteps; ++step)
        {
            const float angle = FullTurn * static_cast<float>(step) / CircleSteps;
            circle[step] = {std::cos(angle), std::sin(angle)};
        }
    }

    D3DCOLOR ApplyOpacity(D3DCOLOR color, unsigned opacity)
    {
        return (color & 0x00ffffff) | (((color >> 24) * opacity / 255) << 24);
    }

    void DrawRingMesh(IDirect3DDevice9* device, float x, float y,
                      const float (&radii)[StrokeLayers], const D3DCOLOR (&colors)[StrokeLayers],
                      bool dashed = false)
    {
        std::array<Vertex, CircleSteps * StrokeLayers> vertices;
        for (unsigned step = 0; step < CircleSteps; ++step)
        {
            for (unsigned layer = 0; layer < StrokeLayers; ++layer)
            {
                const float distance = radii[layer];
                vertices[step * StrokeLayers + layer] = {
                    x + circle[step].x * distance - 0.5f,
                    y + circle[step].y * distance - 0.5f,
                    0.0f, 1.0f, colors[layer],
                };
            }
        }
        const auto* indices = dashed ? DashedStrokeIndices.data() : StrokeIndices.data();
        const auto count = dashed ? DashedStrokeIndices.size() : StrokeIndices.size();
        device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, static_cast<UINT>(vertices.size()),
                                       static_cast<UINT>(count / 3), indices, D3DFMT_INDEX16,
                                       vertices.data(), sizeof(Vertex));
    }

    void DrawRing(IDirect3DDevice9* device, float x, float y, float radius,
                  float width, float halo, D3DCOLOR lineColor, D3DCOLOR haloColor, bool dashed = false)
    {
        if (radius <= 0.0f)
            return;

        const float outer = radius + width * 0.5f;
        const float inner = radius - width * 0.5f;
        const float radii[] = {outer + halo + 1.0f, outer + halo, outer,
                               std::max(0.0f, inner), std::max(0.0f, inner - halo),
                               std::max(0.0f, inner - halo - 1.0f)};
        const auto transparent = haloColor & 0x00ffffff;
        const D3DCOLOR colors[] = {transparent, haloColor, lineColor, lineColor, haloColor, transparent};
        DrawRingMesh(device, x, y, radii, colors, dashed);
    }

    void DrawWindowBand(IDirect3DDevice9* device, float x, float y, float outer, float inner,
                        float scale, bool accepted, unsigned opacity)
    {
        // Keep the brighter edges completely inside the two frame boundaries.
        const float edgeWidth = std::min(0.8f * scale, (outer - inner) * 0.25f);
        const float radii[] = {outer, outer - edgeWidth * 0.5f, outer - edgeWidth,
                               inner + edgeWidth, inner + edgeWidth * 0.5f, inner};
        const auto edge = ApplyOpacity(accepted ? 0xdfffe29b : 0xb4ffdca4, opacity);
        const auto fill = ApplyOpacity(accepted ? 0x84ffd05a : 0x67e8b25e, opacity);
        const D3DCOLOR colors[] = {edge, edge, fill, fill, edge, edge};
        DrawRingMesh(device, x, y, radii, colors);
    }

    void DrawFrameOffset(IDirect3DDevice9* device, float x, float y, float scale,
                         int offset, D3DCOLOR color, unsigned opacity)
    {
        // A five-pixel-high bitmap font keeps this small label independent of
        // fonts and textures. The only supported magnitudes are 1 through 5.
        constexpr char Glyphs[][16] = {
            ".#." "##." ".#." ".#." "###",
            "###" "..#" "###" "#.." "###",
            "###" "..#" "###" "..#" "###",
            "#.#" "#.#" "###" "..#" "..#",
            "###" "#.." "###" "..#" "###",
            "..." "..." "###" "..." "...",
            "..." ".#." "###" ".#." "...",
        };
        static_assert(MaxMissFrames == 5);
        const char* glyphs[] = {Glyphs[offset < 0 ? 5 : 6], Glyphs[std::abs(offset) - 1]};
        const float pixel = std::max(1.25f, 1.8f * scale);
        const float left = std::floor(x - 3.5f * pixel);
        const float top = std::floor(y);
        std::array<Vertex, (1 + 2 * 15) * 6> vertices;
        unsigned count = 0;
        const auto rectangle = [&](float x0, float y0, float x1, float y1, D3DCOLOR fill)
        {
            const Vertex a{x0 - 0.5f, y0 - 0.5f, 0.0f, 1.0f, fill};
            const Vertex b{x1 - 0.5f, y0 - 0.5f, 0.0f, 1.0f, fill};
            const Vertex c{x0 - 0.5f, y1 - 0.5f, 0.0f, 1.0f, fill};
            const Vertex d{x1 - 0.5f, y1 - 0.5f, 0.0f, 1.0f, fill};
            for (const auto& v : {a, b, c, c, b, d})
                vertices[count++] = v;
        };
        rectangle(left - 2.0f * pixel, top - pixel, left + 9.0f * pixel, top + 6.0f * pixel,
                  ApplyOpacity(0xd912191f, opacity));
        for (unsigned glyph = 0; glyph < ARRAYSIZE(glyphs); ++glyph)
        {
            for (unsigned row = 0; row < 5; ++row)
            {
                for (unsigned column = 0; column < 3; ++column)
                {
                    if (glyphs[glyph][3 * row + column] == '#')
                    {
                        const float x0 = left + static_cast<float>(4 * glyph + column) * pixel;
                        const float y0 = top + static_cast<float>(row) * pixel;
                        rectangle(x0, y0, x0 + pixel, y0 + pixel, color);
                    }
                }
            }
        }
        device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, count / 3, vertices.data(), sizeof(Vertex));
    }

    class Overlay
    {
    public:
        explicit Overlay(IDirect3DDevice9* device) : device(device) {}

        ~Overlay()
        {
            if (state)
            {
                state->Apply();
                state->Release();
            }
        }

        Overlay(const Overlay&) = delete;
        Overlay& operator=(const Overlay&) = delete;

        void Draw(float x, float y, float height, const RingTiming& timing, bool accepted, unsigned opacity = 255)
        {
            const auto& position = timing.position;
            const unsigned lastEnd = position.lastWindowEnd;
            const float radius = RadiusAtFrame(height, lastEnd, position.frame);
            if (radius <= 0.0f || (!state && !Begin()))
                return;

            const float scale = std::max(0.75f, height / 900.0f);
            DrawWindowBand(device, x, y, RadiusAtFrame(height, lastEnd, static_cast<float>(timing.window.begin)),
                           RadiusAtFrame(height, lastEnd, static_cast<float>(timing.window.end)),
                           scale, accepted, opacity);
            DrawRing(device, x, y, radius, 2.15f * scale, 1.10f * scale,
                      ApplyOpacity(accepted ? 0xffffd05a : 0xf2fff3d5, opacity),
                      ApplyOpacity(accepted ? 0xdc9a4505 : 0xbc151319, opacity));
        }

        void DrawMiss(float x, float y, const D3DVIEWPORT9& viewport,
                      const RingPosition& position, int offset, unsigned opacity)
        {
            if (!state && !Begin())
                return;

            const float height = static_cast<float>(viewport.Height);
            const float scale = std::max(0.75f, height / 900.0f);
            const float radius = RadiusAtFrame(height, position.lastWindowEnd, position.frame);
            const auto color = ApplyOpacity(offset < 0 ? 0xff78e7fa : 0xffff967b, opacity);
            DrawRing(device, x, y, radius, 1.2f * scale, 0.65f * scale, color,
                     ApplyOpacity(0xd812191f, opacity), true);

            const float pixel = std::max(1.25f, 1.8f * scale);
            const float left = static_cast<float>(viewport.X);
            const float top = static_cast<float>(viewport.Y);
            const float right = left + static_cast<float>(viewport.Width);
            const float bottom = top + height;
            const float labelX = std::clamp(x + radius * 0.73f, left + 6.0f * pixel, right - 6.0f * pixel);
            const float labelY = std::clamp(y - radius * 0.73f - 5.0f * pixel, top + pixel, bottom - 6.0f * pixel);
            DrawFrameOffset(device, labelX, labelY, scale, offset, color, opacity);
        }

    private:
        bool Begin()
        {
            if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &state)))
                return false;

            // The game caches device state. Restore all the state changed by
            // this untextured overlay before handing the D3D9 scene back.
            device->SetVertexShader(nullptr);
            device->SetPixelShader(nullptr);
            device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            device->SetTexture(0, nullptr);
            device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
            device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
            device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
            device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
            device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
            device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
            device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
            device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
            device->SetRenderState(D3DRS_FOGENABLE, FALSE);
            device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
            device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
            device->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0f);
            device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
            device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
            device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
            device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            return true;
        }

        IDirect3DDevice9* device;
        IDirect3DStateBlock9* state = nullptr;
    };

    // The selector scans the type-2 actor list. Walking the same list provides
    // upcoming windows even when a timing query has no valid target yet.
    struct EnemyNode
    {
        const EnemyNode* next;
        const BYTE* actor;
    };

    const EnemyNode* EnemyList()
    {
        return *reinterpret_cast<const EnemyNode* const*>(At(EnemyListHead));
    }

    struct MissCue
    {
        const EnemyNode* node;
        const BYTE* actor;
        RingPosition position;
        int offset;
        ULONGLONG expires;
    };

    MissCue missCue;

    bool HasIssenHitTarget(const BYTE* enemy)
    {
        // After selection the input dispatcher requires an active target
        // and a hit region with one of these flags before it can commit.
        if (!enemy || enemy[8] != 1 || !enemy[0xef] || !(enemy[0x0a] & 4) ||
            Field<int>(enemy, 0x21e8) <= 0)
            return false;

        const unsigned count = Field<BYTE>(enemy, 0x108);
        for (unsigned index = 0; index < count; ++index)
        {
            const auto* region = Field<const BYTE*>(enemy, 0x110 + 4 * index);
            if (region && (Field<DWORD>(region, 4) & 0xf0000000))
                return true;
        }
        return false;
    }

    void RecordNearMiss(BYTE* player)
    {
        const unsigned level = *At(Difficulty);
        if (level >= ARRAYSIZE(DifficultyMasks) || player != GetActivePlayer() ||
            (Field<DWORD>(player, 0x22f4) & 0x40000))
            return;

        const auto mask = DifficultyMasks[level];
        const auto minimum = EffectiveMinimumFrames();
        struct Candidate
        {
            const EnemyNode* node;
            std::uint32_t* activeFlags;
            RingPosition position;
            int offset;
        };
        std::array<Candidate, 32> candidates;
        unsigned count = 0;

        for (auto* node = EnemyList(); node && count < candidates.size(); node = node->next)
        {
            const auto* enemy = node->actor;
            MotionFrames frames{};
            RingTiming timing{};
            int offset = 0;
            if (!HasIssenHitTarget(enemy) || !ReadMotion(enemy, frames) ||
                !CalculateTiming(frames, mask, minimum, timing, &offset) ||
                !offset || std::abs(offset) > MaxMissFrames)
                continue;

            auto* motion = Field<BYTE*>(enemy, 0x30);
            auto* activeFlags = reinterpret_cast<std::uint32_t*>(motion + 0xb4);
            if (!(*activeFlags & mask))
                candidates[count++] = {node, activeFlags, timing.position, offset};
        }

        if (!count)
            return;

        // The actual input has already failed. For this separate query only,
        // give near-window candidates the timing bit so the original selector
        // can apply the game's distance, facing, lock-on and enemy checks.
        // The temporary bit is always removed before returning the real result.
        for (unsigned index = 0; index < count; ++index)
            *candidates[index].activeFlags |= mask;
        const auto* candidate = GetIssenTarget(player);
        for (unsigned index = 0; index < count; ++index)
            *candidates[index].activeFlags &= ~mask;

        for (unsigned index = 0; index < count; ++index)
        {
            const auto& entry = candidates[index];
            if (entry.node->actor == candidate)
            {
                missCue = {entry.node, candidate, entry.position, entry.offset, GetTickCount64() + MissCueMs};
                return;
            }
        }
    }

    BYTE* __fastcall SelectFromInput(BYTE* player, BYTE* result)
    {
        auto* target = reinterpret_cast<SelectIssen>(At(IssenSelector))(player, result);
        if (!target)
            RecordNearMiss(player);
        return target;
    }

    void DrawMissCue(Overlay& overlay, const D3DVIEWPORT9& viewport, const EnemyNode* node)
    {
        if (!missCue.node)
            return;

        const ULONGLONG now = GetTickCount64();
        if (!node || now >= missCue.expires || Field<int>(node->actor, 0x21e8) <= 0)
        {
            missCue = {};
            return;
        }

        const ULONGLONG remaining = missCue.expires - now;
        const unsigned opacity = static_cast<unsigned>(std::min<ULONGLONG>(remaining * 255 / 180, 255));
        float x, y;
        const auto anchor = EnemyAnchor(node->actor);
        if (ProjectAnchor(anchor, viewport, x, y))
            overlay.DrawMiss(x, y, viewport, missCue.position, missCue.offset, opacity);
    }

    struct HeldCue
    {
        WorldPoint anchor;
        ULONGLONG expires;
        RingTiming timing;
        const EnemyNode* node;
        const BYTE* actor;
        const std::uint32_t* attackFlags;
        std::uint16_t lastFrame;
        bool onOriginalAttack;
        bool seenAlive;
    };

    std::array<HeldCue, 32> heldCues;

    HeldCue* FindLiveCue(const EnemyNode* node)
    {
        for (auto& cue : heldCues)
        {
            if (cue.node == node && cue.actor == node->actor && !cue.expires)
                return &cue;
        }
        return nullptr;
    }

    void HoldCue(const BYTE* enemy)
    {
        if (!enemy)
            return;

        const unsigned level = *At(Difficulty);
        MotionFrames frames{};
        RingTiming timing{};
        if (level >= ARRAYSIZE(DifficultyMasks) || !ReadMotion(enemy, frames) ||
            !CalculateTiming(frames, DifficultyMasks[level], EffectiveMinimumFrames(), timing))
            return;

        auto* node = EnemyList();
        while (node && node->actor != enemy)
            node = node->next;
        if (!node)
            return;

        auto* cue = FindLiveCue(node);
        if (!cue)
        {
            // Free slots precede expired cues. Keep every live cue unless the
            // fixed storage is full and the game has no expired cue to reuse.
            const auto slot = std::min_element(heldCues.begin(), heldCues.end(),
                [](const HeldCue& left, const HeldCue& right) {
                    const ULONGLONG live = ~ULONGLONG{0};
                    const auto priority = [=](const HeldCue& entry) {
                        return !entry.node ? ULONGLONG{0} : entry.expires ? entry.expires : live;
                    };
                    return priority(left) < priority(right);
                });
            if (slot->node && !slot->expires)
                return;
            cue = &*slot;
        }
        *cue = {EnemyAnchor(enemy), 0, timing, node, enemy, frames.flags,
                static_cast<std::uint16_t>(frames.current), true, false};
    }

    int __fastcall CommitWithCue(BYTE* player)
    {
        using Commit = int (__thiscall *)(BYTE*);
        const int result = reinterpret_cast<Commit>(At(IssenCommit))(player);
        // The preceding native call stored the selected victim at +2254.
        // This call succeeds only when the attack dispatcher commits to Issen.
        if (result && player == At(Player))
        {
            const auto* target = Field<const BYTE*>(player, 0x2254);
            if (missCue.actor == target)
                missCue = {};
            HoldCue(target);
        }
        return result;
    }

    bool TrackHeldCue(const EnemyNode* node, const MotionFrames& frames, bool timedAttack)
    {
        auto* cue = FindLiveCue(node);
        if (!cue)
            return false;

        const bool sameAttack = cue->onOriginalAttack && frames.flags == cue->attackFlags &&
                                frames.current >= cue->lastFrame;
        if (timedAttack && !sameAttack)
        {
            *cue = {};
            return false;
        }

        // Idle and hit reactions leave the frozen radius intact. A restart
        // on the same animation table counts as a new attack when it wraps.
        cue->onOriginalAttack = sameAttack;
        cue->lastFrame = static_cast<std::uint16_t>(frames.current);
        cue->anchor = EnemyAnchor(node->actor);
        cue->seenAlive = true;
        return true;
    }

    void DrawEnemy(Overlay& overlay, const D3DVIEWPORT9& viewport, const EnemyNode* node,
                   std::uint32_t mask, unsigned minimum, bool selected)
    {
        const auto* enemy = node->actor;
        if (!enemy || Field<int>(enemy, 0x21e8) <= 0)
            return;

        MotionFrames frames{};
        RingTiming timing{};
        const bool timedAttack = ReadMotion(enemy, frames) && CalculateTiming(frames, mask, minimum, timing);
        if (TrackHeldCue(node, frames, timedAttack) || !timedAttack ||
            !enemy[0xef] || !(enemy[0x0a] & 4))
            return;

        float x, y;
        const auto anchor = EnemyAnchor(enemy);
        if (ProjectAnchor(anchor, viewport, x, y))
            overlay.Draw(x, y, static_cast<float>(viewport.Height), timing, selected && timing.open);
    }

    void DrawHeldCues(Overlay& overlay, const D3DVIEWPORT9& viewport)
    {
        const ULONGLONG now = GetTickCount64();
        for (auto& cue : heldCues)
        {
            if (!cue.node)
                continue;
            // A missing actor can already have been freed. Keep the last
            // world position without accessing the stored actor or node.
            if (!cue.seenAlive && !cue.expires)
                cue.expires = now + HoldAfterDeathMs;
            cue.seenAlive = false;
            if (cue.expires && now >= cue.expires)
            {
                cue = {};
                continue;
            }

            unsigned opacity = 255;
            if (cue.expires && cue.expires - now < FadeAfterDeathMs)
                opacity = static_cast<unsigned>((cue.expires - now) * 255 / FadeAfterDeathMs);

            float x, y;
            if (ProjectAnchor(cue.anchor, viewport, x, y))
                overlay.Draw(x, y, static_cast<float>(viewport.Height), cue.timing, true, opacity);
        }
    }

    void DrawCues(IDirect3DDevice9* device)
    {
        if (*At(SkipFrame))
            return;

        auto* player = GetActivePlayer();
        if (!player)
        {
            heldCues = {};
            missCue = {};
            return;
        }

        const unsigned level = *At(Difficulty);
        D3DVIEWPORT9 viewport{};
        if (level >= ARRAYSIZE(DifficultyMasks) || FAILED(device->GetViewport(&viewport)))
            return;

        const auto minimum = EffectiveMinimumFrames();
        const auto* target = GetIssenTarget(player);
        const EnemyNode* targetNode = nullptr;
        const EnemyNode* missNode = nullptr;
        const auto mask = DifficultyMasks[level];

        Overlay overlay(device);
        for (auto* node = EnemyList(); node; node = node->next)
        {
            if (node == missCue.node && node->actor == missCue.actor)
                missNode = node;
            if (target && node->actor == target)
                targetNode = node;
            else
                DrawEnemy(overlay, viewport, node, mask, minimum, false);
        }
        if (targetNode)
            DrawEnemy(overlay, viewport, targetNode, mask, minimum, true);
        DrawHeldCues(overlay, viewport);
        DrawMissCue(overlay, viewport, missNode);
    }

    HRESULT WINAPI EndSceneWithCue(IDirect3DDevice9* device)
    {
        DrawCues(device);
        return device->EndScene();
    }

    std::array<BYTE, 5> MakeCall(const BYTE* site, std::uintptr_t target)
    {
        std::array<BYTE, 5> call{0xe8};
        const auto distance = target - reinterpret_cast<std::uintptr_t>(site + call.size());
        static_assert(sizeof(distance) == 4, "Build this ASI for x86.");
        std::memcpy(call.data() + 1, &distance, sizeof(distance));
        return call;
    }

    struct Patch
    {
        BYTE* address;
        const BYTE* bytes;
        std::size_t size;
        DWORD protection;
    };

    void ApplyPatches(Patch* patches, std::size_t count)
    {
        std::size_t unlocked = 0;
        for (; unlocked < count; ++unlocked)
        {
            auto& patch = patches[unlocked];
            if (!VirtualProtect(patch.address, patch.size, PAGE_EXECUTE_READWRITE, &patch.protection))
                break;
        }

        if (unlocked == count)
        {
            for (std::size_t index = 0; index < count; ++index)
            {
                const auto& patch = patches[index];
                std::memcpy(patch.address, patch.bytes, patch.size);
                FlushInstructionCache(GetCurrentProcess(), patch.address, patch.size);
            }
        }

        while (unlocked)
        {
            auto& patch = patches[--unlocked];
            DWORD unused = 0;
            VirtualProtect(patch.address, patch.size, patch.protection, &unused);
        }
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
        std::memcmp(At(InputSelectCallSite), OriginalInputSelectCall, sizeof(OriginalInputSelectCall)) ||
        std::memcmp(At(CommitCallSite), OriginalCommitCall, sizeof(OriginalCommitCall)) ||
        std::memcmp(At(IssenCommit), CommitThunk, sizeof(CommitThunk)))
        return;

    configuredMinimumFrames = ReadMinimumFrames();
    PrepareCircle();

    // Replace the game's final COM call with an ABI-compatible call that draws
    // first and returns the original HRESULT. ESI holds the device at this site.
    BYTE replacement[sizeof(OriginalEndScene)];
    std::memset(replacement, 0x90, sizeof(replacement));
    replacement[0] = 0x56; // push esi
    const auto renderCall = MakeCall(site + 1, reinterpret_cast<std::uintptr_t>(&EndSceneWithCue));
    std::memcpy(replacement + 1, renderCall.data(), renderCall.size());

    BYTE* inputSite = At(InputSelectCallSite);
    BYTE* commitSite = At(CommitCallSite);
    const auto inputCall = MakeCall(inputSite, reinterpret_cast<std::uintptr_t>(&SelectFromInput));
    const auto commitCall = MakeCall(commitSite, reinterpret_cast<std::uintptr_t>(&CommitWithCue));
    Patch patches[] = {
        {site, replacement, sizeof(replacement), 0},
        {inputSite, inputCall.data(), inputCall.size(), 0},
        {commitSite, commitCall.data(), commitCall.size(), 0},
    };
    ApplyPatches(patches, ARRAYSIZE(patches));
}

BOOL WINAPI DllMain(HMODULE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        cueModule = instance;
    return TRUE;
}
