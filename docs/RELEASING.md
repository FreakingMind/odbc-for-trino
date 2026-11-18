# Releasing

Releases are made from tags of the form `vX.Y.Z` ([Semantic Versioning](https://semver.org/)).

1. Review the changes since the previous release and pick the version: a
   breaking change to DSN options or behaviour needs a new major version
   (minor while the version is 0.x).
2. Set the version in `CMakeLists.txt` (`project(... VERSION X.Y.Z)`, which
   the driver reports as `SQL_DRIVER_VER`) and in `install/TrinoODBC_x64.wxs`
   and `install/TrinoODBC_x86.wxs` (`Version`; Windows Installer upgrades
   need it to grow). Move the *Unreleased* entries in
   [CHANGELOG.md](../CHANGELOG.md) under the new version with the release
   date and add its compare link. Commit as `chore(release): vX.Y.Z`.
3. For larger changes, test the release builds: Excel on Windows (32 and 64
   bit as needed) and Excel for Mac with the universal build.
4. Push the tag: `git tag vX.Y.Z && git push origin vX.Y.Z`. The *Make a
   Release* workflow builds the Windows installers (MSI) and the macOS
   installer (`TrinoODBC-X.Y.Z.pkg`) and creates a draft release with them.
5. Edit the draft: paste the changelog section, then publish it.
