// 《鬼武者3》简体中文版 PC 1.0.0.1 的直接一闪窗口补丁。
// 这是由 Ultimate ASI Loader 加载的 32 位 DLL，文件扩展名为 .asi。
// 插件在游戏进程里接管一处共用的判定入口。敌人动作的帧表仍作为原始数据使用，
// 每次查询只临时补上一闪标志。橙羽织的白光和玩家的直接一闪因此使用同一判定。
// 编译环境、反汇编地址来源和测试步骤见同目录的《鬼武者3一闪Mod制作教程》。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace
{
    // 这两个值来自本机 oni3.exe 的 PE 头。SizeOfImage 是加载到内存后的映像尺寸，
    // 单位为字节。初始化时先核对版本特征，再根据这些固定偏移访问游戏。
    constexpr DWORD GameTimestamp = 0x4364f2d3;
    constexpr DWORD GameImageSize = 0x03f00000;
    // RVA 是相对于模块基址的偏移。运行时用实际基址加 RVA，得到可访问的地址。
    // 本版本在反汇编中的默认基址是 0x00400000，三个 RVA 分别对应
    // 共用判定里的调用点 0x00818E89、难度字节 0x03CB47C4、多轨虚函数表 0x0091CCF8。
    constexpr DWORD IssenCheckRva = 0x00418e89;
    constexpr DWORD DifficultyRva = 0x038b47c4;
    constexpr DWORD BlendedMotionVtableRva = 0x0051ccf8;

    // 挂钩点处的三条原始 x86 指令，共 10 字节。逐字节核对后才允许改写，
    // 也确保跳转覆盖的是完整指令。左边是机器码，右边是反汇编出来的指令。
    constexpr BYTE OriginalCheck[] = {
        0x8b, 0xce,                         // mov ecx, esi
        0xff, 0xd0,                         // call eax
        0x8b, 0x97, 0xf0, 0x22, 0x00, 0x00  // mov edx, [edi + 22f0h]
    };

    // module 是插件自己的模块句柄，用来找到同名 INI。其余变量在安装挂钩时
    // 记录游戏内存地址或设置值，供每次判定直接读取。returnAddress 指向被覆盖指令之后。
    HMODULE module;
    const BYTE* difficulty;
    const void* blendedMotionVtable;
    BYTE* returnAddress;
    unsigned minimumFrames;

    // 函数指针的类型。实际函数由游戏从敌人对象的 +0x228C 取出。
    // x86 __thiscall 把 enemy 传入 ECX。返回值为一个字节，由原游戏代码继续解释。
    using IssenEvaluator = BYTE (__thiscall *)(BYTE* enemy);

    // 这里只回答「当前帧是否属于补出来的部分」，因此原窗口内的帧直接返回 false，
    // 仍由游戏以自己的原始标志判定。flags 是从 0 开始的逐帧数组，last 是最后一个
    // 可读的索引，mask 只保留当前难度所用的一闪位。
    //
    // 原窗口若是 [68, 70)，有效帧就是 68 和 69，70 是结束边界。设最低 5 帧时，
    // 用 70 减 5 得到新起点 65。当前帧只在 65 到 67 时需要补，这等价于
    // 「当前帧在原窗口前，且结束边界到当前帧的距离不超过最低帧数」。
    constexpr bool IsEarlierFrame(const std::uint32_t* flags, unsigned last,
                                  unsigned current, std::uint32_t mask, unsigned minimum)
    {
        // || 会从左到右求值。索引越界时，前半段已为真，后半段就不会读取数组。
        // 原本已有一闪标志或阈值不足 2 时，也无需补帧。
        if (current > last || minimum < 2 || (flags[current] & mask))
            return false;

        // 只有在 limit 之前开始的原窗口才可能把当前帧纳入其中。
        // 先跳过后续的空白帧，找到最近一段有效帧的起点。
        const unsigned limit = current + minimum;
        unsigned frame = current + 1;
        while (frame <= last && frame < limit && !(flags[frame] & mask))
            ++frame;
        // 在允许的范围内没有找到原始窗口。这也涵盖完全没有一闪标志的动作。
        if (frame > last || frame >= limit)
            return false;

        // 再越过连续的有效帧，找这段窗口后的第一帧。无需扫描到 limit 之后。
        while (frame <= last && frame < limit && (flags[frame] & mask))
            ++frame;

        // 走过数组末尾时，结束边界就是 last + 1，|| 左侧为真，不再访问数组。
        // 走到 limit 时要再看这一帧，位已清零表示窗口恰好在 limit 结束，
        // 位仍置上则表示结束得更晚，当前帧离窗口末端太远。
        return frame > last || !(flags[frame] & mask);
    }

    // 编译期测试用的模拟帧表。最低 5 帧时，单帧窗口从 [5, 6) 向前补到 [1, 6)，
    // 两帧窗口从 [10, 12) 补到 [7, 12)，原本 5 帧的 [16, 21) 照常。
    // static_assert 会在编译时核对过早、原本有效、无对应掩码、关闭补丁和窗口
    // 紧贴动画末尾等情况。下面的 25 表示最后一个合法索引，所以共有 26 帧。
    constexpr std::uint32_t Timing[] = {
        0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0
    };
    static_assert(!IsEarlierFrame(Timing, 25, 0, 1, 5));
    static_assert(IsEarlierFrame(Timing, 25, 1, 1, 5));
    static_assert(IsEarlierFrame(Timing, 25, 4, 1, 5));
    static_assert(!IsEarlierFrame(Timing, 25, 5, 1, 5));
    static_assert(!IsEarlierFrame(Timing, 25, 6, 1, 5));
    static_assert(IsEarlierFrame(Timing, 25, 7, 1, 5));
    static_assert(!IsEarlierFrame(Timing, 25, 15, 1, 5));
    static_assert(!IsEarlierFrame(Timing, 25, 21, 1, 5));
    static_assert(!IsEarlierFrame(Timing, 25, 23, 2, 5));
    static_assert(!IsEarlierFrame(Timing, 25, 4, 1, 0));
    static_assert(IsEarlierFrame(Timing, 11, 7, 1, 5));

    // 从一个已确认的游戏对象按字节偏移取字段。这里返回引用，所以既能读，
    // 也能用相同偏移写回。T 必须和反汇编中字段的大小一致，例如 BYTE 是 1 字节，
    // uint16_t 是 2 字节，uint32_t 和 32 位游戏中的指针是 4 字节。
    template<typename T>
    T& Field(BYTE* object, unsigned offset)
    {
        return *reinterpret_cast<T*>(object + offset);
    }

    // 动画播放器有数种实现。单轨与多轨都会提供帧表和索引，
    // 但游戏用来确认当前动画是否有效的字段不同。
    bool HasFrameTable(BYTE* motion)
    {
        // 这类游戏对象开头的第一个指针指向虚函数表，可用来辨认播放器类型。
        if (Field<const void*>(motion, 0) == blendedMotionVtable)
        {
            // 多轨播放器会把当前轨道的帧表和索引同步到 +A4、+C4、+C8。
            // +1FC 是选中的轨道编号，+1F8 是轨道总数。无有效轨道时，编号可能是
            // 0xFF，作为无符号字节读取时自然通不过小于轨道数的检查。
            // 初版在这里检查 +94，但多轨播放器可以一直把它留空，导致漏掉大量敌人。
            const auto channel = Field<BYTE>(motion, 0x1fc);
            return channel < Field<BYTE>(motion, 0x1f8);
        }
        // 单轨播放器会在 +94 保存当前动作记录的指针。
        return Field<const BYTE*>(motion, 0x94) != nullptr;
    }

    // 橙羽织显示和玩家发动直接一闪都会走到这条路径。它接收敌人和该敌人的
    // 原判定函数。__fastcall 让 x86 汇编入口用 ECX、EDX 分别传入这两个参数。
    BYTE __fastcall CheckIssen(BYTE* enemy, IssenEvaluator evaluate)
    {
        // 敌人 +30 存放动画播放器指针。当前动作无效时，继续执行原判定。
        auto* motion = Field<BYTE*>(enemy, 0x30);
        if (!motion || !HasFrameTable(motion))
            return evaluate(enemy);

        // 游戏难度编号从 0 到 3。编号 2 和 3 复用同一个逐帧标志位，
        // 编号 0 和 1 各用不同的位。未知编号留给原判定处理。
        constexpr std::uint32_t masks[] = {0x00200000, 0x04000000, 0x00100000, 0x00100000};
        const unsigned level = *difficulty;
        if (level >= ARRAYSIZE(masks))
            return evaluate(enemy);

        const auto mask = masks[level];
        // +B4 是本次判定读到的当前标志。+A4 指向动作的原始逐帧标志数组。
        // +C4 是当前帧索引，+C8 是最后一个合法索引，二者单位都是动画帧。
        auto& activeFlags = Field<std::uint32_t>(motion, 0xb4);
        const auto* frameFlags = Field<const std::uint32_t*>(motion, 0xa4);
        const auto current = Field<std::uint32_t>(motion, 0xc4);
        const auto last = Field<std::uint16_t>(motion, 0xc8);
        // 游戏已认为这一帧有效时，原样调用。帧表缺失或尚未到补帧区间时也一样。
        if ((activeFlags & mask) || !frameFlags ||
            !IsEarlierFrame(frameFlags, last, current, mask, minimumFrames))
            return evaluate(enemy);

        // |= 把当前难度的一闪位临时置为 1，保留当前标志的其他位。
        // 方向、距离、敌人状态和个别敌人的附加条件仍由原判定函数处理。
        // 返回后用 & ~mask 清掉刚加的位，并保留其他位的当前值。
        // 原始帧表从头到尾只读。用这一招也能把窗口限制在原窗口结束之前。
        activeFlags |= mask;
        const BYTE result = evaluate(enemy);
        activeFlags &= ~mask;
        return result;
    }

    // 挂钩落在游戏函数的中间。naked 让编译器省略普通函数入口和退出时自动生成的
    // 压栈、退栈指令，由下面的汇编按游戏现场的约定接好参数，最后跳回原位置。
    __declspec(naked) void IssenCheckHook()
    {
        __asm
        {
            mov edx, eax                 // EAX 是原判定函数地址，作为第 2 个参数。
            mov ecx, esi                 // ESI 是当前敌人对象，作为第 1 个参数。
            call CheckIssen              // 判定返回字节留在 AL，供游戏继续使用。
            mov edx, [edi + 22f0h]        // 补执行一条被覆盖的原指令，恢复 EDX 的值。
            jmp dword ptr [returnAddress] // 回到原函数中紧接被覆盖指令的那一条。
        }
    }

    // 配置在插件初始化时读一次。玩家修改阈值后，重启游戏即可使用新值。
    unsigned ReadMinimumFrames()
    {
        constexpr unsigned Default = 5;
        wchar_t path[MAX_PATH];
        const DWORD length = GetModuleFileNameW(module, path, ARRAYSIZE(path));
        if (!length || length >= ARRAYSIZE(path))
            return Default;

        // 取插件路径的最后一个点，把 .asi 改成 .ini。两个文件放在同一目录。
        // 写入需要 5 个宽字符的空间，包括结尾的空字符。
        auto* extension = std::wcsrchr(path, L'.');
        if (!extension || ARRAYSIZE(path) - (extension - path) < 5)
            return Default;
        std::memcpy(extension, L".ini", sizeof(L".ini"));

        // 先按字符串读取 [Issen] 段中的 MinimumFrames，再按十进制检查全串。
        // 空值、被截断的值和无效值都回退到默认 5。
        wchar_t value[16];
        const DWORD chars = GetPrivateProfileStringW(L"Issen", L"MinimumFrames", L"5",
                                                      value, ARRAYSIZE(value), path);
        if (!chars || chars >= ARRAYSIZE(value) - 1)
            return Default;

        wchar_t* end = nullptr;
        // end 会指向转换停止的位置。它等于 value 表示一个数字也没读到，
        // *end 非零表示还有尾巴。范围限制为 0–255，0 和 1 会关闭挂钩。
        const long frames = std::wcstol(value, &end, 10);
        if (end == value || *end || frames < 0 || frames > 255)
            return Default;
        return static_cast<unsigned>(frames);
    }
}

// ASI Loader 查找名为 InitializeASI 的 DLL 导出，因此这里使用 C 链接名。
// GameTimestamp 等检查保护的是本版本专用的 RVA 和对象偏移。
extern "C" __declspec(dllexport) void InitializeASI()
{
    // nullptr 表示取主程序 oni3.exe 的模块句柄。它在这里也是游戏映像的加载基址。
    // 这与 DllMain 收到的插件自己的模块句柄用途不同。
    auto* image = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    if (!image)
        return;

    // PE 从 DOS 头开始。e_lfanew 指向 NT 头，先确认签名和偏移在预期的
    // 映像头范围内，再读取机器架构、PE 类型、时间戳和映像尺寸。
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < sizeof(IMAGE_DOS_HEADER) ||
        dos->e_lfanew > 0x1000 - sizeof(IMAGE_NT_HEADERS32))
        return;
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        pe->FileHeader.TimeDateStamp != GameTimestamp ||
        pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        pe->OptionalHeader.SizeOfImage != GameImageSize)
        return;

    // 最后核对挂钩点的 10 个原始字节。版本不符、已有其他写入或重复初始化时，
    // 检查失败就直接退出，避免覆盖不同的指令。
    BYTE* site = image + IssenCheckRva;
    if (std::memcmp(site, OriginalCheck, sizeof(OriginalCheck)) != 0)
        return;

    // 至少要有 2 帧的下限才可能扩大一个非空窗口。0 和 1 因此保持原版运行。
    minimumFrames = ReadMinimumFrames();
    if (minimumFrames < 2)
        return;

    // 先准备 C++ 处理函数和汇编入口会使用的地址，再启用跳转。
    // E9 的近跳转占 5 字节，剩下的位置用 90（NOP）填满。
    difficulty = image + DifficultyRva;
    blendedMotionVtable = image + BlendedMotionVtableRva;
    returnAddress = site + sizeof(OriginalCheck);
    BYTE jump[sizeof(OriginalCheck)];
    std::memset(jump, 0x90, sizeof(jump));
    jump[0] = 0xe9;
    // E9 后的 4 字节是相对位移，计算基准是跳转指令的下一条地址 site + 5，
    // 所以把目标函数地址减去 site + 5，再把相对位移按 x86 字节序写进去。
    const auto displacement = reinterpret_cast<std::uintptr_t>(&IssenCheckHook) -
                              reinterpret_cast<std::uintptr_t>(site + 5);
    static_assert(sizeof(displacement) == 4, "Build the ASI for x86.");
    std::memcpy(jump + 1, &displacement, sizeof(displacement));

    // 游戏的代码页平时可执行但不可写。先临时允许写入，失败就退出。
    // 写入后刷新指令缓存，再恢复原有页面保护。
    DWORD oldProtection = 0;
    if (!VirtualProtect(site, sizeof(jump), PAGE_EXECUTE_READWRITE, &oldProtection))
        return;

    std::memcpy(site, jump, sizeof(jump));
    FlushInstructionCache(GetCurrentProcess(), site, sizeof(jump));
    DWORD unusedProtection = 0;
    VirtualProtect(site, sizeof(jump), oldProtection, &unusedProtection);
}

// Windows 加载插件 DLL 时调用这里。只保存插件句柄以定位 INI，
// 改写游戏指令的工作由 ASI Loader 随后调用 InitializeASI 来完成。
BOOL WINAPI DllMain(HMODULE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        module = instance;
    return TRUE;
}
