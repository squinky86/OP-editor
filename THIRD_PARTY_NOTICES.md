# Third-party notices

OpenPsalm Editor uses Qt 6 Core, GUI, Widgets, Multimedia, Network, and Test. Release
builds use the open-source Qt distribution under the GNU General Public License
version 3 option. OpenPsalm Editor itself is licensed under the GNU Affero
General Public License version 3 or later; its complete corresponding source,
including the scripts needed to rebuild and statically relink the Windows
binary, is published with each release tag.

The Windows build does not use a commercial or prebuilt static Qt SDK. GitHub
Actions builds Qt from source through the vcpkg manifest at the repository root
and the pinned vcpkg baseline recorded there. The exact static and host triplets
are in `packaging/vcpkg-triplets/`. vcpkg downloads the unmodified upstream Qt
source archives and records each component's copyright file in its installed
tree.

- Qt source and license information: <https://code.qt.io/cgit/qt/qt5.git/>
- Qt open-source licensing: <https://www.qt.io/development/open-source-lgpl-obligations>
- Reproducible dependency recipe: `vcpkg.json`
- Windows static-link settings: `packaging/vcpkg-triplets/x64-windows-static-release.cmake`

OpenPsalm Editor uses libzip to inspect and extract downloaded OP-songs ZIP
archives. libzip is distributed under the BSD 3-Clause License. Release builds
obtain its unmodified source and license through the pinned vcpkg manifest.

- libzip project and source: <https://libzip.org/>
- libzip license: <https://github.com/nih-at/libzip/blob/main/LICENSE>

The packaged copies of this notice, OpenPsalm Editor's `LICENSE`, and the public
release-tag source are intended to travel together. No Qt source is modified by
this project. If that changes, the corresponding patch and modified Qt source
offer must be added here before distributing another binary.
