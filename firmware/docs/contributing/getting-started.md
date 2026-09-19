# Getting Started

This guide helps you build and run InkAgent locally.

## Prerequisites

- PlatformIO Core (`pio`) or VS Code + PlatformIO IDE
- Python 3.8+
- `clang-format` **exactly 21** — `pip install clang-format==21.1.2`
- USB-C cable
- An InkAgent-supported reader for hardware testing (X3, X4, X4 Classic,
  X4 Pro, Seeed Sticky, M5Stack Paper Mono)

The clang-format version is pinned rather than a floor because its output
changes between major versions: on 22 or 23 you would reformat the tree and CI,
which runs 21, would reformat it back. `./bin/clang-format-fix` refuses anything
but 21 and tells you this. If you keep another clang-format on `PATH`, point the
script at the pinned one instead:

```sh
pip install clang-format==21.1.2
CLANG_FORMAT="$(python3 -c 'import clang_format,os;print(os.path.join(os.path.dirname(clang_format.__file__),"data","bin","clang-format"))')" ./bin/clang-format-fix
```

Verify:

```sh
clang-format --version   # must report 21
```

## Clone and initialize

```sh
git clone --recursive https://github.com/Ash20pk/InkAgent
cd InkAgent
```

If you already cloned without submodules:

```sh
git submodule update --init --recursive
```

Enable the repository-managed Git hooks (required once per clone):

```sh
git config core.hooksPath .githooks
chmod +x .githooks/pre-commit
```

## Build

```sh
pio run
```

## Flash

```sh
pio run --target upload
```

## First checks before opening a PR

```sh
./bin/clang-format-fix
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
pio run
```

## What to read next

- [Architecture Overview](./architecture.md)
- [Development Workflow](./development-workflow.md)
- [Testing and Debugging](./testing-debugging.md)
