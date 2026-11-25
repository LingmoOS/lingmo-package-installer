# Lingmo Package Installer

A native Lingmo SDK application for opening local Debian `.deb` files. The
main workspace has no navigation sidebar: package information and its
draggable icon occupy the left half, and the virtual Lingmo OS installation
target occupies the right half. Both halves share the same opaque window
background; both icons use equal-sized tiles about 1.5 times their icon size,
with labels below. The package pane keeps a single “Package details” button;
the title is centered against the full window. The target is an action, not a
filesystem directory, and package files are never copied into `/` or another
system location.

## Installation backend

The application reads package control metadata with `dpkg-deb`, checks the
package architecture and installed version, then asks for confirmation before
creating a PackageKit D-Bus transaction. It enables PackageKit's interactive
transaction hint and D-Bus interactive authorization so the system PolicyKit
agent can request administrator authentication. Transaction status, percentage,
errors, cancellation, and completion come from PackageKit's D-Bus properties
and signals. PackageKit runs the transaction through its APT backend and
handles privileged DPKG work, dependency resolution, system authorization and
package-manager locking. The GUI itself never runs as root and never invokes
`dpkg -i`.

The local package's repository signature cannot generally be established from
the `.deb` file alone. The installer does not claim that a local package is
trusted; PackageKit and APT remain responsible for their trust checks and can
reject an untrusted package. Dependencies and conflicts are resolved by the
PackageKit APT backend during the transaction.

## Build and test

Install Lingmo SDK development files, CMake 3.22+, a C++20 compiler and a
PackageKit daemon with its APT backend, then run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

For parser-only tests without the desktop SDK:

```sh
cmake -S . -B build-core -DPACKAGE_INSTALLER_BUILD_UI=OFF -DBUILD_TESTING=ON
cmake --build build-core --parallel
ctest --test-dir build-core --output-on-failure
```

The desktop entry registers `application/vnd.debian.binary-package` and
`application/x-deb`; it accepts one or more package paths via `%F`.

## Current validation boundary

Automated tests cover metadata parsing and architecture matching. A successful
build or test does not prove a live PackageKit authorization prompt, APT
dependency transaction, desktop drag-and-drop, global status-bar menu display,
or real Lingmo compositor glass effect; those require a Lingmo desktop session.

GPL-3.0-or-later.
