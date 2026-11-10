# Repository Guidelines

Read `README.md` first. It holds the project goal, current status, working agreements, and design notes. When you finish a stage, update its current status section.

## Project Structure

This Windows C++17 project uses a Direct3D 12 Hello Triangle to study the transition from DX11 to DX12. `src/main.cpp` contains the Win32 entry point and renderer. Start with `HelloTriangleApp::Initialize()` for device and pipeline setup and `Render()` for command recording, presentation, and synchronization. `shaders/` holds the vertex and pixel shaders and their shared `Common.hlsli` declarations. There are no separate test or asset directories. Keep generated files under `x64/` and `Computer.6f1c3a52/` out of version control.

## Build and Run

Use Visual Studio 18 with the MSVC v145 toolset and Windows SDK. From Developer PowerShell at the repository root, run:

```powershell
MSBuild.exe .\ComputerGraphicsDX12.sln /p:Configuration=Debug /p:Platform=x64 /m /v:minimal
.\x64\Debug\ComputerGraphicsDX12.exe
```

MSBuild is not on `PATH` in a plain shell. Use `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe` there. If `clang-format` is missing, use `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe`.

The first command builds the app and compiles HLSL with FXC (Shader Model 5.1). The second launches the sample. For an optimized build, change `Debug` to `Release` in both commands. MSBuild writes `.cso` files beside the executable, and the app loads them from that directory. Register new `.hlsl` entry points as `FxCompile` items with the appropriate `ShaderType` in `ComputerGraphicsDX12.vcxproj`. Register shared `.hlsli` files as `None` items and use `main` for shader entry points.

## Coding Style and Learning Scope

Format changed C++ with `clang-format -i --style=file src/main.cpp`. `.clang-format` specifies four spaces, no tabs, and an 80-column limit. Follow the existing `PascalCase` for types and functions, `camelCase` for locals, `m_` for members, and `k` for constants. Preserve the setup order and wait for GPU completion before resetting the command allocator. Work through one requested renderer feature at a time and agree on its design with the learner before implementing it. Ask before copying any Part 2 course files into this project.

## Testing and Review

There is no automated test suite or coverage target. Build and run Debug, confirm the colored triangle renders and the window closes cleanly, and check stderr for `[D3D12 WARNING]` or `[D3D12 ERROR]` messages with the debug layer active. Use short, imperative commit subjects. In pull requests, describe the change, record the build and runtime checks, link related issues when available, and include a screenshot for visible rendering changes.
