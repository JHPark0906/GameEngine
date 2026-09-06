# Project build system

The build pipeline has two distinct phases:

1. CMake compiles a project application and links the `GameEngine` static library.
2. `ProjectBuilder` stages a validated, deployable package from the compiled application and the
   project's content root.

`GameBuilder` is the command-line front end that performs both phases.
The engine, Editor, Builder and tests share one repository and top-level CMake configuration.
No sample game or external game/server repository is required to build the engine and tools.

## Project contract

A buildable project is a source directory holding a `CMakeLists.txt` that calls
`gameengine_add_game_project(<TargetName> ...)`. GameBuilder accepts that directory through
`--project`, not a `.gameproject` file or a content-only directory. It looks for exactly one root
`.gameproject` first in `<source>/Content`, then in `<source>`. The descriptor's filename must match
its `projectName`; its directory is the asset root, so the project file, assets, and manifest share
the deployment root. The common CMake function defaults to `CONTENT_DIR Content`; use `CONTENT_DIR .`
when the descriptor and assets live beside `CMakeLists.txt`.

The descriptor's `sourceRootPath` is relative to the descriptor and defaults to `.`. Editor Build
and script creation use it to locate the source directory; a descriptor under `Content/` with code
one level above uses `"sourceRootPath": ".."`. GameBuilder does not read that field to locate code:
its caller supplies the source directory explicitly. The asset root does not move with this field.

The asset database packages the root `<ProjectName>.gameproject` and assets recognized by its
registered importers. Every Scene is staged as an asset, while paths declared in the project file are
additionally validated as registered Scene assets. Required backend shaders come from the compiled
application's engine runtime files and are not project assets.

## Package layout

```text
<output>/
  <Project>.exe
  <Project>.pdb              # when produced by the selected configuration
  <ProjectName>.gameproject
  Assets/AssetDatabase.json
  Scenes/...
  Resources/...
  Rendering/Direct3D/Shaders/...
  *.dll                      # compiled runtime dependencies, when present
```

Each build exclusively creates a unique sibling `<output>.build-<guid>` directory and assembles
the package in its `staging` child. Only this owned workspace is cleaned up; pre-existing
`<output>.staging` and `<output>.backup` paths are never reused or deleted. Output may not overlap
the source content, compiled executable, or runtime-artifact directory.

Publication starts only after the manifest and settings have been loaded back successfully. The
previous output moves to the workspace's `previous` child until publication succeeds. A failed
publication restores it; if restoration itself fails, the workspace is preserved and its recovery
path is logged. Source and validation failures leave the existing output intact.

## Target source provenance

[GameEngineProject.cmake](../../cmake/GameEngineProject.cmake) records the declaring source
directory's real path as `GAMEENGINE_PROJECT_SOURCE_<target>:INTERNAL` when registering a game
target. Each configure clears the preceding target-source entries before registering the current
targets, so removed targets cannot inherit stale provenance. `CMAKE_HOME_DIRECTORY` is not a
substitute: the top-level source can be the engine rather than the selected game.

[ProjectBuildSource.cpp](ProjectBuildSource.cpp) checks this metadata against `--project` before
compilation and again after a successful build. Missing metadata, invalid or missing directories,
and different checkouts stop packaging. Configure the tree for the intended project before retrying.

Both existing directories are canonicalized. Equal canonical paths pass without a file-ID query,
including on filesystems such as exFAT where that query is unavailable. If canonical paths differ,
`std::filesystem::equivalent` must establish that they identify the same directory; a mismatch or
query error is rejected. Path aliases are accepted only when one of these checks proves equality.

A build can trigger CMake regeneration. The post-build check prevents a changed game selection
from being packaged with the requested content, and GameBuilder rereads `GAMEENGINE_OUTPUT_ROOT`
so a changed output layout does not select an old executable.

## Command line

```text
GameBuilder.exe --project <source directory> --output <directory>
  [--configuration Debug|Release] [--build-dir <configured CMake build tree>]
  [--target-name <name>] [--cmake <cmake.exe>] [--single-file] [--issue-identities]
```

The tool takes the project directory's name as the CMake target and executable name unless
`--target-name` says otherwise. It builds that target with `cmake --build` in the configured build
tree. If `--build-dir` is absent, its default is `<project source parent>/build/vs`, independent of
the GameBuilder executable's location. Pass the engine's build tree explicitly for an external
game checkout. Configuration defaults to `Debug`, and target/configuration names allow only letters,
digits, underscores and hyphens. Products must follow
`GAMEENGINE_OUTPUT_ROOT/<configuration>/<target>/<target>.exe`. The selected CMake executable comes
from `--cmake`, the cache's `CMAKE_COMMAND`, Visual Studio, then `PATH`, in that order.

Configuring the tree is left to the developer. From the engine root,
`cmake --preset vs -DGAMEEDITOR_PROJECT_DIRECTORY=` creates `build/vs` without game-specific
Editor components. To package a game, first register its source directory explicitly. For a
separately prepared sibling `MyGame` project:

```powershell
cmake --preset vs -DGAMEEDITOR_PROJECT_DIRECTORY=../MyGame
cmake --build --preset release --target GameBuilder
.\x64\Release\Tools\GameBuilder.exe --project ..\MyGame --build-dir .\build\vs --configuration Release --target-name MyGame --output .\Builds\MyGame-Windows-x64
```

The game must provide its own `CMakeLists.txt` and valid content; see the project declaration in
the [root README](../../README.md#게임-프로젝트-연결).
The `debug` and `release` build presets use this tree. Its default output root is `x64`, but an
existing cache can override `GAMEENGINE_OUTPUT_ROOT`. `vs-no-pch` / `no-pch` use `build/vs-no-pch`
and `x64-no-pch/Debug`; the Ninja Debug/Release presets use separate `build/ninja-debug` and
`build/ninja-release` trees with `x64-ninja/<configuration>` outputs. GameBuilder itself is under
`<output root>/<configuration>/Tools`. See the [CLI README](../../GameBuilder/README.md) for complete
build and packaging commands.

`--output` must be outside the project source directory. Package validation additionally rejects
overlap with source content, the compiled executable and runtime artifacts. Missing asset identities
stop packaging by default. `--issue-identities` permits writing `.meta` files beside source assets;
it does not change source-provenance validation. CMake may also generate the project's component
schema during compilation.

`--single-file` embeds project content and engine runtime content in the executable. Native DLLs
remain beside it, with case-insensitive `.dll` matching, because the Windows loader cannot load
dependencies from the appended content pack. Keep those DLLs when distributing the package;
optional PDB symbols also remain separate.

## Regression checks

See [validation](../../Docs/VALIDATION.md) for the public default configuration and its recorded
verification status. Optional external skeletal input is separate from the default test set.

The `ProjectBuilder` suite covers packaging, source-provenance rules and editor build requests.
`GameEngineTests.ProjectBuildSourceCli` is a separate CTest test that invokes the actual CLI and
CMake regeneration, including wrong-checkout rejection, removed target metadata and changed output
roots. After configuring and completing the build, run both from the engine root:

```powershell
ctest --test-dir build/vs -C Debug -R "GameEngineTests\.(ProjectBuilder|ProjectBuildSourceCli)$" --output-on-failure
```

Use `-C Release` for a completed Release build, or `build/vs-no-pch -C Debug` for the no-PCH tree.
Do not build another configuration, run another Builder or use Editor Build during these checks:
the tests inspect generated schemas and staged products. Reconfigure each affected tree after
adding source files.
