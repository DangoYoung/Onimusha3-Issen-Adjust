# 鬼武者 3 一闪模组

适用于《鬼武者 3》简体中文版 PC 1.0.0.1（32 位 x86）。两个插件可以单独安装，也可以一起使用。

| 模组 | 功能 | 源码 | 安装文件 |
| --- | --- | --- | --- |
| 一闪提示 | 用收缩圆环提示直接一闪和连锁一闪，并显示早按或晚按的帧差。无需装备橙羽织。 | [IssenCue.cpp](src/Onimusha3.IssenCue.cpp) | [一闪提示 ASI](scripts/Onimusha3.IssenCue.asi) · [INI](scripts/Onimusha3.IssenCue.ini) |
| 一闪补帧 | 向前延长过短的直接一闪窗口，保留窗口原来的结束帧。直接一闪和橙羽织白光共用补帧后的判定。 | [IssenWindow.cpp](src/Onimusha3.IssenWindow.cpp) | [一闪补帧 ASI](scripts/Onimusha3.IssenWindow.asi) · [INI](scripts/Onimusha3.IssenWindow.ini) |

## 一闪提示

### 直接一闪

- 半透明环带的外缘标记窗口开启，内缘标记窗口关闭。外环进入最大半径以内时开始显示，并持续匀速收缩。以 900 像素画面高度为基准，最大半径默认是 300 像素，窗口内圈半径默认是 42 像素，通常每帧收缩 7 像素。特别宽的判定窗口会等比例缩放，保留全部有效判定帧。
- 一次攻击有多个独立窗口时，每个窗口各画一段。后续窗口最多提前 12 个动画帧显示，长间隔中外环缩到零后会暂时消失。
- 提示随敌人移动。外环进入环带，且游戏选中该敌人作为可直接一闪的目标时，外环变为金色。

启用补帧时，直接一闪环带和早晚帧差使用 `MinimumFrames` 调整后的窗口。补帧连成一段的窗口会合并绘制。个别由独立脚本控制的直接一闪机会仍由游戏处理。

### 连锁一闪

一闪命中并开始原生连锁倒计时后，紫色提示随主角移动。窗口开启时外环变亮。连锁成功后，下一段会随主角继续显示。每段提示使用游戏原生计时器。

### 按键定格

符合记录条件的早按、晚按和成功一闪都会让收缩圈与判定环带一起定格在原地，并在同一时间消失。默认保留 2 秒。早晚按用细虚线和数字显示帧差。青色 `-2` 表示早了 2 帧，珊瑚色 `+1` 表示比最后一个有效帧晚了 1 帧。默认记录窗口前后各 10 帧。另一段窗口可以继续显示，已定格的提示按各自的时间结束。

直接一闪早晚按使用游戏的目标筛选。游戏选中直接一闪目标后，如果目标绑定或一闪提交失败，会在按键位置定格白色虚线圈，也使用同一保留时间。连锁早晚按根据攻击指令和连锁计时器计算，连锁成功由原生输入处理确认。

## 安装与卸载

1. 退出游戏。确认游戏根目录已有支持 `InitializeASI` 的 32 位 ASI Loader，例如以 `dinput8.dll` 安装的 [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)。
2. 在文件页面用 **Download raw file** 下载需要的 `.asi` 和对应的 `.ini`，放入游戏根目录下的 `scripts` 文件夹。
3. 卸载时先退出游戏，再把对应的 `.asi` 移出 `scripts`。

## 提示设置

编辑游戏目录下的 `scripts/Onimusha3.IssenCue.ini`，重启游戏后生效。

- `[Display]` 可以分别开关直接一闪、连锁一闪和各自的早晚按提示。`DirectRejected` 控制直接一闪失败的白色虚线圈。`InputHoldMs=2000` 控制每组定格提示保留的毫秒数。设为 `0` 时，记录到按键就立即隐藏这组提示。
- `[Geometry]` 可以调整半径、每帧收缩量、线宽和显示位置。`MaxRadius=300` 是默认半径上限，设为 `0` 可解除限制。`[Direct]` 的 `NextWindowLeadFrames` 控制后续窗口最多提前几帧显示。
- `[NearMiss]` 的 `MaxFrames` 调整记录范围，也可设置虚线尺寸、数字大小和颜色。`[DirectColors]` 与 `[ChainColors]` 分别控制两种一闪的配色，`[DirectColors]` 下的 `RejectedLine` 设置白色虚线圈的颜色。

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

已验证两份插件的 32 位导出。多窗口判定、跟随与定格、早晚帧差、目标绑定与提交失败提示、连锁换段、统一保留时间和半径上限已通过模拟时序的 Direct3D 9 测试。游戏内的显示位置和触发时机待复测。
