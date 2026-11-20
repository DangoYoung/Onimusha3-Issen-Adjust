# 鬼武者 3 一闪模组

适用于《鬼武者 3》简体中文版 PC 1.0.0.1（32 位 x86）。两个插件可以单独安装，也可以一起使用。

| 模组 | 功能 | 源码 | 安装文件 |
| --- | --- | --- | --- |
| 一闪提示 | 用收缩圆环和填色环带提示直接一闪时机，并显示接近窗口的早按或晚按。无需装备橙羽织。 | [IssenCue.cpp](src/Onimusha3.IssenCue.cpp) | [一闪提示 ASI](scripts/Onimusha3.IssenCue.asi) |
| 一闪补帧 | 向前延长过短的直接一闪窗口，保留窗口原来的结束帧。直接一闪和橙羽织白光共用补帧后的判定。 | [IssenWindow.cpp](src/Onimusha3.IssenWindow.cpp) | [一闪补帧 ASI](scripts/Onimusha3.IssenWindow.asi) · [INI](scripts/Onimusha3.IssenWindow.ini) |

## 一闪提示

- 外环从敌人动作的第 0 帧开始匀速收缩。半透明环带的外缘标记窗口开启，内缘标记窗口关闭。以 900 像素画面高度为基准，最后一个窗口的内圈半径为 42 像素，每帧收缩 7 像素，环带厚度与有效帧数严格成正比。最后一个窗口关闭后，外环继续匀速收缩，直到半径归零或当前动作结束。
- 外环进入环带，且游戏选中该敌人作为可直接一闪的目标时，外环变为金色。
- 成功发动一闪后，金环保持触发时的大小。目标存活时，金环跟随目标直到它开始下一次攻击。目标死亡时，金环在最后的位置停留 3 秒，末尾淡出。

如果攻击指令与窗口相差不超过 5 个敌人动画帧，并通过游戏的距离、朝向和锁定等目标检查，敌人身上会留下一个细虚线圆。青色 `-2` 表示早了 2 帧，珊瑚色 `+1` 表示比最后一个有效帧晚了 1 帧。画面只保留最近一次符合条件的提示，约 1.8 秒后消失。

启用补帧时，环带边界和误按帧差都按 `MinimumFrames` 调整后的窗口计算。提示与补帧都适用于动画帧表中标记的直接一闪窗口。个别由独立脚本控制的机会仍由游戏处理。

## 安装与卸载

1. 退出游戏。确认游戏根目录已有支持 `InitializeASI` 的 32 位 ASI Loader，例如以 `dinput8.dll` 安装的 [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)。
2. 在文件页面用 **Download raw file** 下载需要的 `.asi`，放入游戏根目录下的 `scripts` 文件夹。要启用补帧，把 `Onimusha3.IssenWindow.ini` 放在同一目录并按下面的说明设置。
3. 卸载时先退出游戏，再把对应的 `.asi` 移出 `scripts`。

## 补帧设置

打开 `scripts/Onimusha3.IssenWindow.ini`，修改 `[Issen]` 下的 `MinimumFrames`，然后重启游戏。默认值为 `0`，使用游戏原有的一闪时机。配置缺失或无效时也使用 `0`。设为 `2`–`255` 可启用补帧并指定最低动画帧数，例如 `3`。设为 `0` 或 `1` 可关闭补帧。

## 从源码编译

安装 MSVC C++ Build Tools 和 Windows SDK。在 **x86 Native Tools Command Prompt for VS** 中进入仓库根目录。以下命令把编译文件放到临时目录。

```bat
mkdir "%TEMP%\Oni3-Issen-Build"
copy "src\Onimusha3.IssenCue.cpp" "%TEMP%\Oni3-Issen-Build\"
copy "src\Onimusha3.IssenWindow.cpp" "%TEMP%\Oni3-Issen-Build\"
cd /d "%TEMP%\Oni3-Issen-Build"
cl /nologo /W4 /WX /O2 /MT /LD /std:c++17 /EHsc /utf-8 Onimusha3.IssenCue.cpp /link /OUT:Onimusha3.IssenCue.asi /DYNAMICBASE /NXCOMPAT
cl /nologo /W4 /WX /O2 /MT /LD /std:c++17 /EHsc /utf-8 /arch:IA32 Onimusha3.IssenWindow.cpp /link /OUT:Onimusha3.IssenWindow.asi /DYNAMICBASE /NXCOMPAT
```

编译成功后，按上述步骤安装生成的 ASI。补帧的实现过程和源码导读见[一闪补帧制作教程](docs/issen-window-tutorial.md)。

已离线验证两份插件的 32 位导出。圆环比例、填色边界、误按提示、成功一闪后的定格和死亡停留已通过离线判定与独立 Direct3D 9 测试。游戏内的显示位置和实际触发时机待复测。
