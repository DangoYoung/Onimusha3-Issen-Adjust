# 鬼武者 3 一闪模组

适用于《鬼武者 3》简体中文版 PC 1.0.0.1（32 位 x86）。两个插件可以单独安装，也可以一起使用。

| 模组 | 功能 | 源码 | 安装文件 |
| --- | --- | --- | --- |
| 一闪提示 | 在敌人身上用同心圆提示直接一闪时机。无需装备橙羽织。 | [IssenCue.cpp](src/Onimusha3.IssenCue.cpp) | [一闪提示 ASI](scripts/Onimusha3.IssenCue.asi) |
| 一闪补帧 | 向前延长过短的直接一闪窗口，保留窗口原来的结束帧。直接一闪和橙羽织白光共用补帧后的判定。 | [IssenWindow.cpp](src/Onimusha3.IssenWindow.cpp) | [一闪补帧 ASI](scripts/Onimusha3.IssenWindow.asi) · [INI](scripts/Onimusha3.IssenWindow.ini) |

## 一闪提示

- 敌人起手时，外环从最大半径开始收缩。两条细环标出判定窗口，大圆在动作结束时缩到最小半径。
- 外环进入两条细环之间，且游戏选中该敌人作为可直接一闪的目标时，外环变为金色。
- 成功发动一闪后，金环保持触发时的大小。目标存活时，金环跟随目标直到它开始下一次攻击。目标死亡时，金环在最后的位置停留 3 秒，末尾淡出。

启用补帧时，两条细环标出的窗口会随 `MinimumFrames` 调整。提示与补帧都适用于动画帧表中标记的直接一闪窗口。个别由独立脚本控制的机会仍由游戏处理。

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

已离线验证两份插件的 32 位导出。圆环的收缩、变色、成功一闪后的定格和死亡停留已通过独立 Direct3D 9 测试。游戏内的显示位置和实际触发时机待复测。
