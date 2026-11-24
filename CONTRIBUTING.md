# Contributing to Ekron Realms

Thank you for your interest in contributing to Ekron Realms! We welcome contributions from the community and are excited to work with you to improve the game. Please read the following guidelines to get started.

## Table of Contents

1. [Code of Conduct](#code-of-conduct)
2. [How Can I Contribute?](#how-can-i-contribute)
    - [Reporting Bugs](#reporting-bugs)
    - [Suggesting Features](#suggesting-features)
    - [Submitting Changes](#submitting-changes)
3. [Development Setup](#development-setup)
4. [Pull Request Process](#pull-request-process)
5. [License](#license)

## Code of Conduct

This project adheres to the Contributor Covenant [Code of Conduct](CODE_OF_CONDUCT.md). By participating, you are expected to uphold this code. Please report unacceptable behavior to [pdvicente@gleentech.com](mailto:pdvicente@gleentech.com).

## How Can I Contribute?

### Reporting Bugs

If you find a bug, please report it by opening an issue in the GitHub repository. Make sure to include:

- A clear and descriptive title.
- A detailed description of the problem.
- Steps to reproduce the issue.
- Any relevant screenshots or logs.

### Suggesting Features

We are always open to new ideas! To suggest a feature:

1. Check if the feature is already suggested or being worked on by searching the issues.
2. If not, create a new issue with the following information:
   - A clear and descriptive title.
   - A detailed description of the feature.
   - The benefits of the feature.
   - Any additional context or screenshots.

### Submitting Changes

1. Fork the repository and create your branch from `main`.
2. Make your changes, ensuring they adhere to the project's coding standards.
3. Test your changes thoroughly.
4. Commit your changes with clear and descriptive commit messages.
5. Push your changes to your forked repository.
6. Open a pull request (PR) against the `main` branch of the original repository.

## Development Setup

The game is the C++17 desktop client (`sys/desktop/`, SDL2 + OpenGL) and the Android and iOS clients, on top of the Realms engine (`realms/`: C++ and a Free Pascal core) and the Free Pascal Quake 2 game module (`game/`). [BUILD.md](BUILD.md) has the details for every platform, including Windows 11, Android and iOS.

1. **Clone the repository**
   ```bash
   git clone https://github.com/kaytestudio/ekron-realms.git
   cd ekron-realms
   ```

2. **Install the tools**: a C++17 compiler, CMake 3.16+, Ninja and SDL2, plus Free Pascal 3.2 for the Pascal core and the game module (optional: without it the C++ core is used and some tests are skipped). For example on Debian/Ubuntu:
   ```bash
   sudo apt-get install -y build-essential cmake ninja-build libsdl2-dev libgl1-mesa-dev fpc
   ```

3. **Build and test**
   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ctest --test-dir build --output-on-failure
   ./build/ekron-realms --assets sys/android/app/src/main/assets
   ```

Lazarus is only needed for the Lazarus projects under `Projects/`.

## Pull Request Process

1. Ensure your changes pass the existing tests (`ctest`, and the checks listed under *Tests* in [README.md](README.md)) and add tests for new code. GitHub Actions builds and tests pull requests on Linux, macOS, Windows, Android and iOS.
2. Submit your PR to the `main` branch and ensure the PR description clearly describes the changes and the related issue (if applicable).
3. One of the maintainers will review your PR. Please address any feedback promptly.
4. Once approved, your PR will be merged into the `main` branch.

## License

By contributing to Ekron Realms, you agree that your contributions will be licensed under the GNU Affero General Public License v3.0 (AGPL-3.0). Third-party code and assets keep their own licenses; see [CONTRIBUTORS.md](CONTRIBUTORS.md).

For more details, please refer to the [LICENSE](LICENSE) file.

---

We appreciate your efforts to contribute to Ekron Realms. Together, we can make this game even better! If you have any questions or need further assistance, feel free to reach out to the maintainers. Happy coding!