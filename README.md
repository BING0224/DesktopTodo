# DesktopTodo v0.1

一个轻量的 Windows 桌面待办组件，基于 C#、.NET 8 和 WPF。界面采用浅色半透明面板，使用与应用图标一致的蓝色、青绿色。
## Screenshot

![DesktopTodo](Assets/screenshot.png)DesktopTodo/Assets/screenshot.png

## 已实现

- 添加、勾选完成、删除待办
- 双击待办文字进行原位编辑，长内容在编辑时自动换行
- 圆角无边框界面；待办较多时显示细滚动条
- 不显示在任务栏和 Alt+Tab 切换列表中
- 使用 `Win + D` 显示桌面时留在桌面上；切回普通应用后位于应用窗口下方
- 自由拖动和八方向缩放
- 接近屏幕右侧、底部时自动吸附
- 首次启动默认尺寸为 426 × 460，位于桌面右下角
- 一键恢复 426 × 460 默认尺寸和右下角位置
- 一键打开本地 JSON 数据文件夹
- 可在设置菜单中开启或关闭开机自启
- 自动保存待办、完成状态、窗口位置和尺寸
- 再次启动时恢复上次状态
- JSON 无法解析时备份损坏文件并使用默认数据，避免程序无法启动

## 运行

系统要求：Windows 10/11。使用源码运行或打包时，需要安装 [.NET 8 SDK](https://dotnet.microsoft.com/download/dotnet/8.0)。

双击根目录的 `run.cmd`。也可以用 Visual Studio 2022 打开 `DesktopTodo/DesktopTodo.csproj` 后运行。

## 发布为单个 EXE

双击 `publish-win-x64.cmd`。完成后可执行文件位于：

```text
publish\win-x64\DesktopTodo.exe
```

该发布方式包含 .NET 运行时，将 `DesktopTodo.exe` 复制到其他 Windows x64 电脑即可运行，无需另行安装 .NET。发布目录中的 `.pdb` 是调试文件，运行时不需要。

## 使用方法

- 拖动顶部 `TODO` 区域移动窗口。
- 拖动窗口边缘或四角缩放。
- 点击右上角齿轮打开设置菜单。
- 设置菜单中可切换开机自启、打开 `%LocalAppData%\DesktopTodo`，或恢复默认窗口。
- 点击“添加待办”后输入内容，按 `Enter` 添加，按 `Esc` 取消。
- 添加待办时也可以点击输入框右侧的 `×` 取消。
- 双击待办文字可编辑；按 `Enter` 或点击别处保存，按 `Esc` 取消。
- 编辑长待办时，文字会根据窗口宽度自动换行；待办列表超出窗口时，可用鼠标滚轮或右侧细滚动条查看。
- 勾选待办后文字会变淡并添加删除线。
- 鼠标移到待办上，点击右侧 `×` 删除。
- 点击右上角 `×` 退出程序。

## 本地数据

数据保存在：

```text
%LocalAppData%\DesktopTodo\
├── todos.json
└── settings.json
```

程序完全离线，不需要账号或服务器。

首次运行使用默认窗口尺寸与位置；之后会优先恢复 `settings.json` 中保存的状态。升级版本后想应用新的默认尺寸，请点击“设置 → 恢复默认窗口”，不需要删除待办数据。
## License

DesktopTodo is licensed under the GNU General Public License v3.0.

You are free to:
- Use the software
- Study the source code
- Modify the software
- Redistribute modified versions

Any redistributed version must also be released under GPL v3.
