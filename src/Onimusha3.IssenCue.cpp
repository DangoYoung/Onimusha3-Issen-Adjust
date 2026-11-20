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
        if (current == last)
            sample = static_cast<float>(last + 1);
        else if (blended)
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

    struct RingTiming
    {
        float progress;
        float opening;
        float closing;
        bool open;
    };

    bool CalculateTiming(const MotionFrames& frames, std::uint32_t mask, unsigned minimum, RingTiming& timing)
    {
        FrameWindow window = NextWindow(frames, 0, mask, minimum);
        if (!window)
            return false;

        const float count = static_cast<float>(frames.count);
        const float minimumSpan = count * 0.16f;
        float extraTotal = 0.0f;
        float extraElapsed = 0.0f;
        float opening = 0.0f;
        float closing = 0.0f;
        FrameWindow selected{};

        while (window)
        {
            auto next = NextWindow(frames, window.end, mask, minimum);
            while (next && next.begin == window.end)
            {
                window.end = next.end;
                next = NextWindow(frames, window.end, mask, minimum);
            }

            // Keep short windows legible. Stretch their part of the radial
            // scale for the whole animation, so changing guide pairs never
            // makes the moving ring jump outward.
            const float begin = static_cast<float>(window.begin);
            const float end = static_cast<float>(window.end);
            const float span = end - begin;
            const float extra = std::max(minimumSpan - span, 0.0f);
            extraElapsed += extra * std::clamp((frames.sample - begin) / span, 0.0f, 1.0f);
            if (frames.current >= selected.end)
            {
                selected = window;
                opening = begin + extraTotal;
                closing = end + extraTotal + extra;
            }
            extraTotal += extra;
            window = next;
        }

        const float total = count + extraTotal;
        timing = {
            std::clamp((frames.sample + extraElapsed) / total, 0.0f, 1.0f),
            opening / total,
            closing / total,
            frames.current >= selected.begin && frames.current < selected.end,
        };
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
    constexpr float FullTurn = 6.28318530718f;
    constexpr float MaxRadiusFraction = 0.061f;
    constexpr float MinRadiusFraction = 0.014f;

    struct UnitPoint { float x, y; };
    std::array<UnitPoint, CircleSteps> circle;

    constexpr auto MakeStrokeIndices()
    {
        std::array<WORD, CircleSteps * (StrokeLayers - 1) * 6> indices{};
        unsigned index = 0;
        for (unsigned step = 0; step < CircleSteps; ++step)
        {
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

    constexpr auto StrokeIndices = MakeStrokeIndices();

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

    void DrawRing(IDirect3DDevice9* device, float x, float y, float radius,
                  float width, float halo, D3DCOLOR lineColor, D3DCOLOR haloColor)
    {
        struct Vertex { float x, y, z, rhw; D3DCOLOR color; };

        const float halfWidth = width * 0.5f;
        const float offsets[] = {halfWidth + halo + 1.0f, halfWidth + halo, halfWidth,
                                 -halfWidth, -halfWidth - halo, -halfWidth - halo - 1.0f};
        const auto transparent = haloColor & 0x00ffffff;
        const D3DCOLOR colors[] = {transparent, haloColor, lineColor, lineColor, haloColor, transparent};
        std::array<Vertex, CircleSteps * StrokeLayers> vertices;
        for (unsigned step = 0; step < CircleSteps; ++step)
        {
            for (unsigned layer = 0; layer < StrokeLayers; ++layer)
            {
                const float distance = radius + offsets[layer];
                vertices[step * StrokeLayers + layer] = {
                    x + circle[step].x * distance - 0.5f,
                    y + circle[step].y * distance - 0.5f,
                    0.0f, 1.0f, colors[layer],
                };
            }
        }
        device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, static_cast<UINT>(vertices.size()),
                                       static_cast<UINT>(StrokeIndices.size() / 3),
                                       StrokeIndices.data(), D3DFMT_INDEX16,
                                       vertices.data(), sizeof(Vertex));
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
            if (!state && !Begin())
                return;

            const float outer = std::max(10.0f, height * MaxRadiusFraction);
            const float inner = std::max(5.0f, height * MinRadiusFraction);
            const auto radius = [=](float progress) { return outer - (outer - inner) * progress; };
            const float scale = std::max(0.75f, height / 900.0f);

            // The opening and closing guide strokes remain fixed during each
            // window. The heavier moving stroke reads on both light and dark terrain.
            const auto guide = ApplyOpacity(0xceeee5ca, opacity);
            const auto guideHalo = ApplyOpacity(0x800e0e12, opacity);
            DrawRing(device, x, y, radius(timing.opening), 1.05f * scale, 0.65f * scale, guide, guideHalo);
            DrawRing(device, x, y, radius(timing.closing), 1.05f * scale, 0.65f * scale, guide, guideHalo);
            DrawRing(device, x, y, radius(timing.progress), 2.15f * scale, 1.10f * scale,
                      ApplyOpacity(accepted ? 0xffffd05a : 0xf2fff3d5, opacity),
                      ApplyOpacity(accepted ? 0xdc9a4505 : 0xbc151319, opacity));
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

    struct HeldCue
    {
        const EnemyNode* node;
        const BYTE* actor;
        const std::uint32_t* attackFlags;
        unsigned lastFrame;
        RingTiming timing;
        WorldPoint anchor;
        ULONGLONG expires;
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
        *cue = {node, enemy, frames.flags, frames.current, timing, EnemyAnchor(enemy), 0, true, false};
    }

    int __fastcall CommitWithCue(BYTE* player)
    {
        using Commit = int (__thiscall *)(BYTE*);
        const int result = reinterpret_cast<Commit>(At(IssenCommit))(player);
        // The preceding native call stored the selected victim at +2254.
        // This call succeeds only when the attack dispatcher commits to Issen.
        if (result && player == At(Player))
            HoldCue(Field<const BYTE*>(player, 0x2254));
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
        cue->lastFrame = frames.current;
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
            return;
        }

        const unsigned level = *At(Difficulty);
        D3DVIEWPORT9 viewport{};
        if (level >= ARRAYSIZE(DifficultyMasks) || FAILED(device->GetViewport(&viewport)))
            return;

        const auto minimum = EffectiveMinimumFrames();
        const auto* target = GetIssenTarget(player);
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
        DrawHeldCues(overlay, viewport);
    }

    HRESULT WINAPI EndSceneWithCue(IDirect3DDevice9* device)
    {
        DrawCues(device);
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
    replacement[1] = 0xe8; // call rel32
    const auto relative = reinterpret_cast<std::uintptr_t>(&EndSceneWithCue) -
                          reinterpret_cast<std::uintptr_t>(site + 6);
    static_assert(sizeof(relative) == 4, "Build this ASI for x86.");
    std::memcpy(replacement + 2, &relative, sizeof(relative));

    // Replace only the call made by the direct-Issen input dispatcher.
    BYTE* commitSite = At(CommitCallSite);
    BYTE commitReplacement[sizeof(OriginalCommitCall)] = {0xe8};
    const auto commitRelative = reinterpret_cast<std::uintptr_t>(&CommitWithCue) -
                                reinterpret_cast<std::uintptr_t>(commitSite + sizeof(commitReplacement));
    std::memcpy(commitReplacement + 1, &commitRelative, sizeof(commitRelative));

    DWORD renderProtection = 0;
    if (!VirtualProtect(site, sizeof(replacement), PAGE_EXECUTE_READWRITE, &renderProtection))
        return;

    DWORD commitProtection = 0;
    DWORD unused = 0;
    if (VirtualProtect(commitSite, sizeof(commitReplacement), PAGE_EXECUTE_READWRITE, &commitProtection))
    {
        std::memcpy(site, replacement, sizeof(replacement));
        std::memcpy(commitSite, commitReplacement, sizeof(commitReplacement));
        FlushInstructionCache(GetCurrentProcess(), site, sizeof(replacement));
        FlushInstructionCache(GetCurrentProcess(), commitSite, sizeof(commitReplacement));
        VirtualProtect(commitSite, sizeof(commitReplacement), commitProtection, &unused);
    }
    VirtualProtect(site, sizeof(replacement), renderProtection, &unused);
}

BOOL WINAPI DllMain(HMODULE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        cueModule = instance;
    return TRUE;
}
