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
#include <cwctype>
#include <type_traits>
#include <vector>

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
    constexpr std::uintptr_t ChainSetupThunk = 0x00405eac;
    constexpr BYTE OriginalChainSetupThunk[] = {0xe9, 0xdf, 0x31, 0x41, 0};
    constexpr std::uintptr_t NativeChainSetup = 0x00819090;
    constexpr std::uintptr_t PlayerTimerCallSite = 0x0081b0a2;
    constexpr BYTE OriginalPlayerTimerCall[] = {0xe8, 0xa9, 0xe3, 0xff, 0xff};
    constexpr std::uintptr_t NativePlayerTimer = 0x00819450;
    constexpr std::uintptr_t ChainInputCallSite = 0x0081b201;
    constexpr BYTE OriginalChainInputCall[] = {0xe8, 0xea, 0xdf, 0xff, 0xff};
    constexpr std::uintptr_t NativeChainInput = 0x008191f0;

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

    struct RingColors
    {
        D3DCOLOR edge, fill, line, halo;
    };

    struct CueSettings
    {
        bool direct = true;
        bool chain = true;
        bool directMiss = true;
        bool chainMiss = true;
        unsigned inputHoldMs = 2000;

        float innerRadius = 42.0f;
        float pixelsPerFrame = 7.0f;
        float ringWidth = 2.15f;
        float ringHalo = 1.10f;
        float bandEdgeWidth = 0.8f;
        float enemyHeight = 90.0f;
        float tallEnemyHeight = 170.0f;
        float playerHeight = 90.0f;
        unsigned nextWindowLeadFrames = 12;


        int missFrames = 10;
        float missRadius = 14.0f;
        float missWidth = 1.2f;
        float missHalo = 0.65f;
        float labelPixelSize = 1.8f;
        D3DCOLOR earlyColor = 0xff78e7fa;
        D3DCOLOR lateColor = 0xffff967b;
        D3DCOLOR missHaloColor = 0xd812191f;
        D3DCOLOR labelBackground = 0xd912191f;

        RingColors directColors[2] = {
            {0xb4ffdca4, 0x67e8b25e, 0xf2fff3d5, 0xbc151319},
            {0xdfffe29b, 0x84ffd05a, 0xffffd05a, 0xdc9a4505},
        };
        RingColors chainColors[2] = {
            {0xc8c6aaff, 0x786f56cb, 0xf2e0ceff, 0xbc181128},
            {0xfff1daff, 0x96ae79ff, 0xffedc3ff, 0xe05a279b},
        };
    };

    CueSettings settings;

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

    template<std::size_t N>
    bool SiblingIniPath(wchar_t (&path)[MAX_PATH], const wchar_t (&filename)[N])
    {
        const DWORD length = GetModuleFileNameW(cueModule, path, ARRAYSIZE(path));
        if (!length || length >= ARRAYSIZE(path))
            return false;

        auto* slash = std::wcsrchr(path, L'\\');
        if (!slash || ARRAYSIZE(path) - static_cast<std::size_t>(slash + 1 - path) < N)
            return false;

        std::memcpy(slash + 1, filename, sizeof(filename));
        return true;
    }

    class CueIni
    {
    public:
        explicit CueIni(const wchar_t* path) : path(path) {}

        template<typename T>
        void Number(const wchar_t* section, const wchar_t* key, T& setting, double min, double max) const
        {
            wchar_t text[48];
            if (!Read(section, key, text))
                return;

            wchar_t* end = nullptr;
            const double number = std::wcstod(text, &end);
            if (end == text || *end || !(number >= min && number <= max))
                return;

            if constexpr (std::is_integral_v<T>)
            {
                if (std::trunc(number) != number)
                    return;
            }
            setting = static_cast<T>(number);
        }

        void Color(const wchar_t* section, const wchar_t* key, D3DCOLOR& setting) const
        {
            wchar_t text[48];
            if (!Read(section, key, text) || std::wcslen(text) != 8 || !std::iswxdigit(text[0]))
                return;

            wchar_t* end = nullptr;
            const auto argb = std::wcstoul(text, &end, 16);
            if (end == text + 8)
                setting = argb;
        }

    private:
        bool Read(const wchar_t* section, const wchar_t* key, wchar_t (&text)[48]) const
        {
            const DWORD count = GetPrivateProfileStringW(section, key, L"", text, ARRAYSIZE(text), path);
            return count > 0 && count < ARRAYSIZE(text) - 1;
        }

        const wchar_t* path;
    };

    void ReadRingColors(const CueIni& ini, const wchar_t* section, RingColors (&colors)[2])
    {
        ini.Color(section, L"IdleEdge", colors[0].edge);
        ini.Color(section, L"IdleFill", colors[0].fill);
        ini.Color(section, L"IdleLine", colors[0].line);
        ini.Color(section, L"IdleHalo", colors[0].halo);
        ini.Color(section, L"ActiveEdge", colors[1].edge);
        ini.Color(section, L"ActiveFill", colors[1].fill);
        ini.Color(section, L"ActiveLine", colors[1].line);
        ini.Color(section, L"ActiveHalo", colors[1].halo);
    }

    CueSettings ReadCueSettings(const wchar_t* path)
    {
        CueSettings config;
        const CueIni ini(path);
        ini.Number(L"Display", L"Direct", config.direct, 0, 1);
        ini.Number(L"Display", L"Chain", config.chain, 0, 1);
        ini.Number(L"Display", L"DirectMiss", config.directMiss, 0, 1);
        ini.Number(L"Display", L"ChainMiss", config.chainMiss, 0, 1);

        ini.Number(L"Geometry", L"InnerRadius", config.innerRadius, 1, 300);
        ini.Number(L"Geometry", L"PixelsPerFrame", config.pixelsPerFrame, 0.5, 40);
        ini.Number(L"Geometry", L"RingWidth", config.ringWidth, 0.1, 20);
        ini.Number(L"Geometry", L"RingHalo", config.ringHalo, 0, 20);
        ini.Number(L"Geometry", L"BandEdgeWidth", config.bandEdgeWidth, 0, 10);
        ini.Number(L"Geometry", L"EnemyHeight", config.enemyHeight, -500, 500);
        ini.Number(L"Geometry", L"TallEnemyHeight", config.tallEnemyHeight, -500, 500);
        ini.Number(L"Geometry", L"PlayerHeight", config.playerHeight, -500, 500);

        ini.Number(L"Direct", L"NextWindowLeadFrames", config.nextWindowLeadFrames, 0, 120);
        // Existing INIs can supply the old near-miss duration as a default.
        ini.Number(L"NearMiss", L"HoldMs", config.inputHoldMs, 0, 60000);
        ini.Number(L"Display", L"InputHoldMs", config.inputHoldMs, 0, 60000);

        ini.Number(L"NearMiss", L"MaxFrames", config.missFrames, 0, 99);
        ini.Number(L"NearMiss", L"MinRadius", config.missRadius, 1, 300);
        ini.Number(L"NearMiss", L"StrokeWidth", config.missWidth, 0.1, 20);
        ini.Number(L"NearMiss", L"StrokeHalo", config.missHalo, 0, 20);
        ini.Number(L"NearMiss", L"LabelPixelSize", config.labelPixelSize, 0.5, 10);
        ini.Color(L"NearMiss", L"EarlyColor", config.earlyColor);
        ini.Color(L"NearMiss", L"LateColor", config.lateColor);
        ini.Color(L"NearMiss", L"HaloColor", config.missHaloColor);
        ini.Color(L"NearMiss", L"LabelBackground", config.labelBackground);

        ReadRingColors(ini, L"DirectColors", config.directColors);

        ReadRingColors(ini, L"ChainColors", config.chainColors);
        return config;
    }

    unsigned ReadMinimumFrames()
    {
        wchar_t path[MAX_PATH];
        if (!SiblingIniPath(path, L"Onimusha3.IssenWindow.ini"))
            return 0;

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

    unsigned RecoveryFrames()
    {
        return static_cast<unsigned>(std::ceil(settings.innerRadius / settings.pixelsPerFrame));
    }

    struct RingTiming
    {
        float frame;
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

        while (window)
        {
            auto next = NextWindow(frames, window.end, mask, minimum);
            while (next && next.begin == window.end)
            {
                window.end = next.end;
                next = NextWindow(frames, window.end, mask, minimum);
            }

            const int offset = OffsetFromWindow(frames.current, window);
            if (!nearestOffset || !selected || std::abs(offset) <= std::abs(selectedOffset))
            {
                selected = window;
                selectedOffset = offset;
            }
            if (next && !nearestOffset)
            {
                // Split short gaps between recovery and anticipation. In a
                // long gap, let the ring disappear before cueing the next hit.
                const unsigned gap = next.begin - window.end;
                const unsigned recovery = std::min(gap / 2, RecoveryFrames());
                const unsigned leadIn = std::min(gap, settings.nextWindowLeadFrames);
                const unsigned displayFrom = std::max(window.end + recovery, next.begin - leadIn);
                if (frames.current < displayFrom)
                    break;
            }
            window = next;
        }

        timing = {frames.sample, selected, selectedOffset == 0};
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

    WorldPoint ActorAnchor(const BYTE* actor)
    {
        WorldPoint point = Field<WorldPoint>(actor, 0x90);
        const float enemyHeight = actor[2] == 0x34 ? settings.tallEnemyHeight : settings.enemyHeight;
        point.y += actor == At(Player) ? settings.playerHeight : enemyHeight;
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
            const auto* deviceSettings = *reinterpret_cast<const DWORD* const*>(At(DeviceSettings));
            if (deviceSettings && deviceSettings[12])
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

    float RadiusAtFrame(float height, unsigned windowEnd, float frame)
    {
        const float scale = std::max(0.5f, height / 900.0f);
        const float framesToClose = static_cast<float>(windowEnd) - frame;
        return scale * std::max(0.0f, settings.innerRadius + framesToClose * settings.pixelsPerFrame);
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

    enum class RingStyle : std::uint8_t { Issen, Chain };

    void DrawWindowBand(IDirect3DDevice9* device, float x, float y, float height, FrameWindow window,
                        float scale, const RingColors& palette)
    {
        const float outer = RadiusAtFrame(height, window.end, static_cast<float>(window.begin));
        const float inner = RadiusAtFrame(height, window.end, static_cast<float>(window.end));
        // Keep the brighter edges completely inside the two frame boundaries.
        const float edgeWidth = std::min(settings.bandEdgeWidth * scale, (outer - inner) * 0.25f);
        const float radii[] = {outer, outer - edgeWidth * 0.5f, outer - edgeWidth,
                               inner + edgeWidth, inner + edgeWidth * 0.5f, inner};
        const D3DCOLOR colors[] = {palette.edge, palette.edge, palette.fill, palette.fill, palette.edge, palette.edge};
        DrawRingMesh(device, x, y, radii, colors);
    }

    void DrawFrameOffset(IDirect3DDevice9* device, float x, float y, float scale,
                         int offset, D3DCOLOR color)
    {
        // A five-pixel-high bitmap font keeps the label independent of fonts and textures.
        constexpr char Digits[][16] = {
            "###" "#.#" "#.#" "#.#" "###",
            ".#." "##." ".#." ".#." "###",
            "###" "..#" "###" "#.." "###",
            "###" "..#" "###" "..#" "###",
            "#.#" "#.#" "###" "..#" "..#",
            "###" "#.." "###" "..#" "###",
            "###" "#.." "###" "#.#" "###",
            "###" "..#" "..#" ".#." ".#.",
            "###" "#.#" "###" "#.#" "###",
            "###" "#.#" "###" "..#" "###",
        };
        constexpr char Minus[] = "..." "..." "###" "..." "...";
        constexpr char Plus[] = "..." ".#." "###" ".#." "...";
        const unsigned magnitude = static_cast<unsigned>(std::abs(offset));
        const bool twoDigits = magnitude >= 10;
        const char* glyphs[] = {offset < 0 ? Minus : Plus,
                                Digits[twoDigits ? magnitude / 10 : magnitude], Digits[magnitude % 10]};
        const unsigned glyphCount = twoDigits ? 3 : 2;
        const float pixel = std::max(1.25f, settings.labelPixelSize * scale);
        const float width = static_cast<float>(4 * glyphCount - 1) * pixel;
        const float left = std::floor(x - width * 0.5f);
        const float top = std::floor(y);
        std::array<Vertex, (1 + 3 * 15) * 6> vertices;
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
        rectangle(left - 2.0f * pixel, top - pixel, left + width + 2.0f * pixel, top + 6.0f * pixel,
                  settings.labelBackground);
        for (unsigned glyph = 0; glyph < glyphCount; ++glyph)
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

        void Draw(float x, float y, const D3DVIEWPORT9& viewport, const RingTiming& timing, bool accepted,
                  RingStyle style = RingStyle::Issen, int offset = 0)
        {
            const float height = static_cast<float>(viewport.Height);
            const float radius = RadiusAtFrame(height, timing.window.end, timing.frame);
            if ((!offset && radius <= 0.0f) || (!state && !Begin()))
                return;

            const float scale = std::max(0.75f, height / 900.0f);
            const auto& palette = (style == RingStyle::Chain ? settings.chainColors : settings.directColors)[accepted];
            DrawWindowBand(device, x, y, height, timing.window, scale, palette);
            if (offset)
                DrawMiss(x, y, viewport, std::max(settings.missRadius * scale, radius), scale, offset);
            else
                DrawRing(device, x, y, radius, settings.ringWidth * scale, settings.ringHalo * scale,
                         palette.line, palette.halo);
        }

    private:
        void DrawMiss(float x, float y, const D3DVIEWPORT9& viewport, float radius, float scale, int offset)
        {
            const auto color = offset < 0 ? settings.earlyColor : settings.lateColor;
            DrawRing(device, x, y, radius, settings.missWidth * scale, settings.missHalo * scale,
                     color, settings.missHaloColor, true);

            const float pixel = std::max(1.25f, settings.labelPixelSize * scale);
            const float left = static_cast<float>(viewport.X);
            const float top = static_cast<float>(viewport.Y);
            const float right = left + static_cast<float>(viewport.Width);
            const float height = static_cast<float>(viewport.Height);
            const float bottom = top + height;
            const float labelMargin = std::min(8.0f * pixel, (right - left) * 0.5f);
            const float clampedPixel = std::min(pixel, height / 7.0f);
            const float labelX = std::clamp(x + radius * 0.73f, left + labelMargin, right - labelMargin);
            const float labelY = std::clamp(y - radius * 0.73f - 5.0f * pixel,
                                            top + clampedPixel, bottom - 6.0f * clampedPixel);
            DrawFrameOffset(device, labelX, labelY, scale, offset, color);
        }

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

    struct FrozenCue
    {
        WorldPoint anchor;
        RingTiming timing;
        ULONGLONG expires;
        unsigned round;
        std::int8_t offset;
        RingStyle style;
    };

    struct EnemyRound
    {
        const EnemyNode* node;
        const BYTE* actor;
        const std::uint32_t* frames;
        unsigned frame;
        unsigned id;
        bool seen;
    };

    std::vector<FrozenCue> frozenCues;
    std::vector<EnemyRound> enemyRounds;
    unsigned nextRound;

    EnemyRound* FindEnemyRound(const EnemyNode* node)
    {
        const auto entry = std::find_if(enemyRounds.begin(), enemyRounds.end(),
            [node](const EnemyRound& round) { return round.node == node; });
        return entry == enemyRounds.end() ? nullptr : &*entry;
    }

    void ObserveEnemyRound(EnemyRound& round, const EnemyNode* node, const MotionFrames& frames)
    {
        if (round.actor != node->actor || round.frames != frames.flags || frames.current < round.frame)
        {
            round.actor = node->actor;
            round.frames = frames.flags;
            round.id = ++nextRound;
        }
        round.frame = frames.current;
    }

    FrozenCue* FindFrozenCue(unsigned round, FrameWindow window)
    {
        const auto entry = std::find_if(frozenCues.begin(), frozenCues.end(),
            [round, window](const FrozenCue& cue)
            {
                return cue.round == round && cue.timing.window.end == window.end;
            });
        return entry == frozenCues.end() ? nullptr : &*entry;
    }

    void FreezeCue(unsigned round, const WorldPoint& anchor, const RingTiming& timing, int offset, RingStyle style)
    {
        auto* previous = FindFrozenCue(round, timing.window);
        if (previous && previous->offset == 0)
            return;

        const auto expires = GetTickCount64() + settings.inputHoldMs;
        const auto frameOffset = static_cast<std::int8_t>(offset);
        if (previous)
        {
            // An additional press in this window updates the result at its
            // original position. A successful input remains the final result.
            previous->expires = expires;
            previous->timing = timing;
            previous->offset = frameOffset;
        }
        else
            frozenCues.push_back({anchor, timing, expires, round, frameOffset, style});
    }

    void FreezeEnemyCue(const EnemyNode* node, const MotionFrames& frames, const RingTiming& timing, int offset)
    {
        auto* round = FindEnemyRound(node);
        if (!round)
        {
            enemyRounds.push_back({node, nullptr, nullptr, 0, 0, false});
            round = &enemyRounds.back();
        }
        ObserveEnemyRound(*round, node, frames);
        FreezeCue(round->id, ActorAnchor(node->actor), timing, offset, RingStyle::Issen);
    }

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
            MotionFrames frames;
            RingTiming timing;
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
                !offset || std::abs(offset) > settings.missFrames)
                continue;

            auto* motion = Field<BYTE*>(enemy, 0x30);
            auto* activeFlags = reinterpret_cast<std::uint32_t*>(motion + 0xb4);
            if (!(*activeFlags & mask))
                candidates[count++] = {node, activeFlags, frames, timing, offset};
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
                FreezeEnemyCue(entry.node, entry.frames, entry.timing, entry.offset);
                return;
            }
        }
    }

    BYTE* __fastcall SelectFromInput(BYTE* player, BYTE* result)
    {
        auto* target = reinterpret_cast<SelectIssen>(At(IssenSelector))(player, result);
        if (!target && settings.directMiss && settings.missFrames)
            RecordNearMiss(player);
        return target;
    }

    constexpr unsigned ChainFlags = 0x22f8;
    constexpr DWORD ChainAttack = 0x04;
    constexpr DWORD ChainReady = 0x40;
    constexpr DWORD ChainRejected = 0x80;
    constexpr DWORD ChainAccepted = 0x100;
    constexpr DWORD ChainArmed = 0x800;
    constexpr unsigned ChainWidth = 0x44dc;
    constexpr unsigned ChainRemaining = 0x44de;
    constexpr unsigned ChainDelay = 0x44e2;
    constexpr unsigned PressedCommands = 0x4454;
    constexpr DWORD AttackPressed = 0x2000;
    constexpr ULONGLONG ChainIdleTimeoutMs = 500;

    struct ChainCue
    {
        WorldPoint pendingAnchor;
        ULONGLONG lastUpdate;
        const BYTE* player;
        unsigned round;
        unsigned width;
        int tick;
        int openingTick;
        int pendingPress;
    };

    ChainCue chainCue;

    void __fastcall SetupChainCue(BYTE* player, BYTE delay, unsigned continuation)
    {
        using Setup = void (__fastcall *)(BYTE*, BYTE, unsigned);
        reinterpret_cast<Setup>(At(NativeChainSetup))(player, delay, continuation);
        if (player != GetActivePlayer())
            return;

        chainCue = {};
        const unsigned width = Field<std::uint16_t>(player, ChainWidth);
        if (width)
        {
            chainCue.player = player;
            chainCue.round = ++nextRound;
            chainCue.width = width;
            chainCue.lastUpdate = GetTickCount64();
        }
    }

    void __fastcall UpdateChainCue(BYTE* player)
    {
        using Update = void (__thiscall *)(BYTE*);
        reinterpret_cast<Update>(At(NativePlayerTimer))(player);
        if (player != chainCue.player)
            return;

        const DWORD flags = Field<DWORD>(player, ChainFlags);
        if (!(flags & ChainAttack) || (flags & ChainAccepted))
        {
            chainCue = {};
            return;
        }

        chainCue.lastUpdate = GetTickCount64();
        const int tick = ++chainCue.tick;
        const int missFrames = settings.chainMiss ? settings.missFrames : 0;
        const bool attack = missFrames && (Field<DWORD>(player, PressedCommands) & AttackPressed);
        if (!chainCue.openingTick)
        {
            if (!(flags & ChainArmed))
            {
                // The game only starts its countdown once the strike connects.
                if (attack)
                {
                    chainCue.pendingPress = tick;
                    chainCue.pendingAnchor = ActorAnchor(player);
                }
                return;
            }

            // The last delay tick is closed. The last countdown tick is open
            // even after the game decrements the remaining count to zero.
            const auto delay = Field<std::uint16_t>(player, ChainDelay);
            const auto remaining = Field<std::uint16_t>(player, ChainRemaining);
            chainCue.openingTick = flags & ChainReady
                ? std::max(1, tick - (static_cast<int>(chainCue.width) - remaining - 1))
                : tick + delay + 1;
        }

        const int pressedAt = attack ? tick : chainCue.pendingPress;
        chainCue.pendingPress = 0;
        const FrameWindow window{0, chainCue.width};
        const int frame = tick - chainCue.openingTick;
        const int recovery = settings.chain ? static_cast<int>(RecoveryFrames()) : 0;
        if (frame >= static_cast<int>(window.end) + std::max(missFrames, recovery))
        {
            chainCue = {};
            return;
        }
        if (pressedAt)
        {
            const int pressedFrame = pressedAt - chainCue.openingTick;
            const int offset = pressedFrame < 0 ? pressedFrame
                : OffsetFromWindow(static_cast<unsigned>(pressedFrame), window);
            if (offset && std::abs(offset) <= missFrames)
            {
                const auto anchor = attack ? ActorAnchor(player) : chainCue.pendingAnchor;
                const RingTiming timing{static_cast<float>(pressedFrame), window, false};
                FreezeCue(chainCue.round, anchor, timing, offset, RingStyle::Chain);
            }
        }
    }

    void __fastcall RecordChainCommit(const BYTE* player)
    {
        if (!settings.chain || chainCue.player != player || !chainCue.openingTick ||
            !(Field<DWORD>(player, ChainFlags) & ChainAccepted) ||
            !(Field<DWORD>(player, PressedCommands) & AttackPressed))
            return;

        const FrameWindow window{0, chainCue.width};
        const int frame = chainCue.tick - chainCue.openingTick;
        if (frame >= 0 && static_cast<unsigned>(frame) < window.end)
        {
            const RingTiming timing{static_cast<float>(frame), window, true};
            FreezeCue(chainCue.round, ActorAnchor(player), timing, 0, RingStyle::Chain);
        }
    }

    BYTE* nativeChainInput;

    // The original handler receives the player in EDX and its caller still
    // needs EDX afterward. Preserve the original register and flag results.
    void __declspec(naked) ChainInputWithCue()
    {
        __asm
        {
            call dword ptr [nativeChainInput]
            pushfd
            pushad
            mov ecx, edx
            call RecordChainCommit
            popad
            popfd
            ret
        }
    }

    void DrawChainCue(Overlay& overlay, const D3DVIEWPORT9& viewport, const BYTE* player)
    {
        const DWORD flags = Field<DWORD>(player, ChainFlags);
        const FrameWindow window{0, chainCue.width};
        const int frame = chainCue.tick - chainCue.openingTick;
        if (chainCue.player != player || !chainCue.openingTick ||
            GetTickCount64() - chainCue.lastUpdate > ChainIdleTimeoutMs || (flags & ChainAccepted) ||
            frame >= static_cast<int>(chainCue.width + RecoveryFrames()) || FindFrozenCue(chainCue.round, window))
            return;

        float x, y;
        if (ProjectAnchor(ActorAnchor(player), viewport, x, y))
        {
            const bool open = (flags & (ChainReady | ChainRejected)) == ChainReady;
            const RingTiming timing{static_cast<float>(frame), window, open};
            overlay.Draw(x, y, viewport, timing, open, RingStyle::Chain);
        }
    }

    int __fastcall CommitWithCue(BYTE* player)
    {
        using Commit = int (__thiscall *)(BYTE*);
        const int result = reinterpret_cast<Commit>(At(IssenCommit))(player);
        if (!result || player != GetActivePlayer() || !settings.direct)
            return result;

        // The preceding native call stored the selected victim at +2254.
        const auto* enemy = Field<const BYTE*>(player, 0x2254);
        const unsigned level = *At(Difficulty);
        MotionFrames frames{};
        RingTiming timing{};
        if (!enemy || level >= ARRAYSIZE(DifficultyMasks) || !ReadMotion(enemy, frames) ||
            !CalculateTiming(frames, DifficultyMasks[level], EffectiveMinimumFrames(), timing) || !timing.open)
            return result;

        for (auto* node = EnemyList(); node; node = node->next)
        {
            if (node->actor == enemy)
            {
                FreezeEnemyCue(node, frames, timing, 0);
                break;
            }
        }
        return result;
    }

    void DrawEnemy(Overlay& overlay, const D3DVIEWPORT9& viewport, const EnemyNode* node,
                   std::uint32_t mask, unsigned minimum, bool selected)
    {
        const auto* enemy = node->actor;
        if (!enemy || Field<int>(enemy, 0x21e8) <= 0)
            return;

        auto* round = FindEnemyRound(node);
        if (!settings.direct && !round)
            return;

        MotionFrames frames{};
        const bool hasMotion = ReadMotion(enemy, frames);
        if (round)
        {
            ObserveEnemyRound(*round, node, frames);
            round->seen = true;
        }

        RingTiming timing{};
        if (!settings.direct || !hasMotion || !enemy[0xef] || !(enemy[0x0a] & 4) || !CalculateTiming(frames, mask, minimum, timing) ||
            (round && FindFrozenCue(round->id, timing.window)))
            return;

        float x, y;
        if (ProjectAnchor(ActorAnchor(enemy), viewport, x, y))
            overlay.Draw(x, y, viewport, timing, selected && timing.open);
    }

    void DrawFrozenCues(Overlay& overlay, const D3DVIEWPORT9& viewport)
    {
        enemyRounds.erase(std::remove_if(enemyRounds.begin(), enemyRounds.end(), [](EnemyRound& round)
        {
            const bool unseen = !round.seen;
            round.seen = false;
            return unseen;
        }), enemyRounds.end());

        const ULONGLONG now = GetTickCount64();
        for (const auto& cue : frozenCues)
        {
            float x, y;
            if (now >= cue.expires || !ProjectAnchor(cue.anchor, viewport, x, y))
                continue;

            overlay.Draw(x, y, viewport, cue.timing, cue.offset == 0, cue.style, cue.offset);
        }

        // Keep suppressing the live cue for a resolved window even if its
        // display time elapses before the animation advances to the next one.
        frozenCues.erase(std::remove_if(frozenCues.begin(), frozenCues.end(), [now](const FrozenCue& cue)
        {
            return now >= cue.expires && chainCue.round != cue.round &&
                   std::none_of(enemyRounds.begin(), enemyRounds.end(),
                       [&cue](const EnemyRound& round) { return round.id == cue.round; });
        }), frozenCues.end());
    }

    void DrawCues(IDirect3DDevice9* device)
    {
        if (*At(SkipFrame))
            return;

        auto* player = GetActivePlayer();
        if (!player)
        {
            frozenCues.clear();
            enemyRounds.clear();
            chainCue = {};
            return;
        }

        const unsigned level = *At(Difficulty);
        D3DVIEWPORT9 viewport{};
        if (level >= ARRAYSIZE(DifficultyMasks) || FAILED(device->GetViewport(&viewport)))
            return;

        const auto minimum = settings.direct ? EffectiveMinimumFrames() : 0;
        const auto* target = settings.direct ? GetIssenTarget(player) : nullptr;
        const EnemyNode* targetNode = nullptr;
        const auto mask = DifficultyMasks[level];

        Overlay overlay(device);
        for (auto* node = EnemyList(); node; node = node->next)
        {
            if (target && node->actor == target)
                targetNode = node;
            else
                DrawEnemy(overlay, viewport, node, mask, minimum, false);
        }
        if (targetNode)
            DrawEnemy(overlay, viewport, targetNode, mask, minimum, true);
        if (settings.chain)
            DrawChainCue(overlay, viewport, player);
        DrawFrozenCues(overlay, viewport);
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

    wchar_t iniPath[MAX_PATH];
    if (SiblingIniPath(iniPath, L"Onimusha3.IssenCue.ini"))
        settings = ReadCueSettings(iniPath);
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
    BYTE* chainSetupSite = At(ChainSetupThunk);
    BYTE* playerTimerSite = At(PlayerTimerCallSite);
    BYTE* chainInputSite = At(ChainInputCallSite);
    nativeChainInput = At(NativeChainInput);
    auto chainSetupJump = MakeCall(chainSetupSite, reinterpret_cast<std::uintptr_t>(&SetupChainCue));
    chainSetupJump[0] = 0xe9;
    const auto playerTimerCall = MakeCall(playerTimerSite, reinterpret_cast<std::uintptr_t>(&UpdateChainCue));
    const auto chainInputCall = MakeCall(chainInputSite, reinterpret_cast<std::uintptr_t>(&ChainInputWithCue));
    const bool chainSupported = (settings.chain || (settings.chainMiss && settings.missFrames)) &&
        !std::memcmp(chainSetupSite, OriginalChainSetupThunk, sizeof(OriginalChainSetupThunk)) &&
        !std::memcmp(playerTimerSite, OriginalPlayerTimerCall, sizeof(OriginalPlayerTimerCall)) &&
        !std::memcmp(chainInputSite, OriginalChainInputCall, sizeof(OriginalChainInputCall));

    Patch patches[] = {
        {site, replacement, sizeof(replacement), 0},
        {inputSite, inputCall.data(), inputCall.size(), 0},
        {commitSite, commitCall.data(), commitCall.size(), 0},
        {chainSetupSite, chainSetupJump.data(), chainSetupJump.size(), 0},
        {playerTimerSite, playerTimerCall.data(), playerTimerCall.size(), 0},
        {chainInputSite, chainInputCall.data(), chainInputCall.size(), 0},
    };
    ApplyPatches(patches, chainSupported ? ARRAYSIZE(patches) : 3);
}

BOOL WINAPI DllMain(HMODULE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        cueModule = instance;
    return TRUE;
}
