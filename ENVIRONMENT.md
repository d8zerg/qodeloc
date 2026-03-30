# Environment Baseline

This file is the reproducible toolchain baseline for the local QodeLoc setup.
Update it whenever a required tool is upgraded.

| Tool | Minimum | Verified version | Verification command |
| --- | --- | --- | --- |
| Clang | 17+ | Ubuntu clang version 20.1.8 (0ubuntu4) | `clang --version | head -n 1` |
| CMake | 3.28+ | cmake version 3.31.6 | `cmake --version | head -n 1` |
| Ninja | current stable | 1.12.1 | `ninja --version` |
| Conan | 2.x | Conan version 2.25.1 | `conan --version` |
| Node.js | 20+ | v24.14.1 | `node --version` |
| Python | 3.12 | Python 3.13.7 | `python3 --version` |
| CUDA Toolkit | 12.x | CUDA compilation tools, release 12.4, V12.4.131 | `nvcc --version | tail -n 2` |
| Git | current stable | git version 2.51.0 | `git --version` |
| Docker Engine | current stable | Docker version 29.3.1, build c2be9cc | `docker --version` |
| Docker Compose | v2+ | Docker Compose version v5.1.0 | `docker compose version` |
| GNU Make | 4.4+ | GNU Make 4.4.1 | `make --version | head -n 1` |
| CTest | bundled with CMake | ctest version 3.31.6 | `ctest --version | head -n 1` |
| GoogleTest / GoogleMock | 1.17.0-1 | googletest 1.17.0-1 | `dpkg -s googletest` |
| clang-format | LLVM 20.1.8 | Ubuntu clang-format version 20.1.8 (0ubuntu4) | `clang-format --version` |
| clang-tidy | LLVM 20.1.8 | Ubuntu LLVM version 20.1.8 | `clang-tidy --version | head -n 1` |

## Notes

- The listed versions were captured from the development machine on 2026-03-30.
- `docker compose` is required for the local dev stack described in `infra/docker-compose.yml`.
- `CTest` with `GoogleTest` and `GoogleMock` is the baseline for unit, integration, and end-to-end tests.
- `clang-format` and `clang-tidy` are the baseline formatting and static-analysis tools for the C++ codebase.
