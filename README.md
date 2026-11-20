# 鬼武者 3 一闪模组

适用于《鬼武者 3》简体中文版 PC 1.0.0.1（32 位 x86）。两个插件可以单独安装，也可以一起使用。

| 模组 | 功能 | 源码 | 安装文件 |
| --- | --- | --- | --- |
| 一闪提示 | 用收缩圆环提示直接一闪和连锁一闪，并显示早按或晚按的帧差。无需装备橙羽织。 | [IssenCue.cpp](src/Onimusha3.IssenCue.cpp) | [一闪提示 ASI](scripts/Onimusha3.IssenCue.asi) |
| 一闪补帧 | 向前延长过短的直接一闪窗口，保留窗口原来的结束帧。直接一闪和橙羽织白光共用补帧后的判定。 | [IssenWindow.cpp](src/Onimusha3.IssenWindow.cpp) | [一闪补帧 ASI](scripts/Onimusha3.IssenWindow.asi) · [INI](scripts/Onimusha3.IssenWindow.ini) |

## 一闪提示

### 直接一闪

- 外环从敌人动作的第 0 帧开始匀速收缩。半透明环带的外缘标记窗口开启，内缘标记窗口关闭。以 900 像素画面高度为基准，每个窗口的内圈半径是 42 像素，每帧收缩 7 像素，环带厚度与有效帧数成正比。
- 一次攻击有多个独立窗口时，每个窗口各画一段。后续窗口最多提前 12 个动画帧显示，长间隔中外环缩到零后会暂时消失。
- 外环进入环带，且游戏选中该敌人作为可直接一闪的目标时，外环变为金色。成功发动一闪后，金环保持触发时的大小。目标存活时，金环跟随目标直到它开始下一次攻击。目标死亡时，金环在最后的位置停留 3 秒，末尾淡出。

启用补帧时，直接一闪环带和早晚帧差使用 `MinimumFrames` 调整后的窗口。补帧连成一段的窗口会合并绘制。个别由独立脚本控制的直接一闪机会仍由游戏处理。

### 连锁一闪

- 一闪命中并开始连锁倒计时后，在主角位置绘制紫色环带。窗口开启时外环变亮，窗口关闭后外环缩到零消失。
- 每段连锁窗口固定在它第一次绘制的位置。连锁成功后，下一段以届时的主角位置为圆心。连锁环使用游戏原生计时器。

### 早按和晚按

符合条件的攻击指令与窗口相差不超过 10 帧时，会显示细虚线圈和帧差。青色 `-2` 表示早了 2 帧，珊瑚色 `+1` 表示比最后一个有效帧晚了 1 帧。直接一闪仍使用游戏的目标检查。两种一闪共用最近一次符合条件的提示。虚线圈和数字完整显示 2 秒，再用 0.18 秒淡出，主圈消失后仍按自己的计时显示。

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

已离线验证两份插件的 32 位导出。多窗口收缩、早晚帧差、成功定格与死亡停留已通过离线判定和独立 Direct3D 9 测试。连锁圆环的倒计时、位置固定和连续出圈已通过模拟时序的渲染测试。游戏内的显示位置和实际触发时机待复测。
