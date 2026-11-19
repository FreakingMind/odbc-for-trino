# Contributing

Thanks for your interest! Bug reports, fixes, tests and documentation are all
welcome. No contributor agreement is needed: by submitting a pull request you
agree that your contribution is licensed under the
[Apache License 2.0](LICENSE), like the rest of the project.

## Reporting problems

Open an issue with the operating system, client application (e.g. Excel 16.x
for Mac), driver version, DSN settings (without secrets) and the relevant part
of a driver log with `loglevel=Trace`. For security problems see
[SECURITY.md](SECURITY.md) instead.

## Making changes

1. Build and run the unit tests as described in the [development guide](docs/DEVELOPMENT.md).
   Run the integration tests against the Docker environment when you change
   driver behaviour.
2. Add or update tests for every change (gtest, under `test/`).
3. Keep the code buildable on both macOS and Windows. Platform-specific code
   goes behind small interfaces in `src/platform/<os>/`, not `#ifdef`s in the
   ODBC entry points.
4. Never hardcode host names, credentials or environment-specific settings:
   make them DSN or connection-string options.
5. Format with the repository's `.clang-format` (CI checks it) and keep
   clang-tidy and compiler warnings clean for the code you touch: CI runs
   clang-tidy on the lines a pull request changes and fails on any finding.
6. Write commit messages in the [Conventional Commits](https://www.conventionalcommits.org/)
   format, `type(scope): summary`, with a body that explains why. Types:
   `feat`, `fix`, `docs`, `test`, `ci`, `build`, `refactor`, `perf`,
   `chore`; scopes are optional (for example `config`, `macos`, `windows`,
   `tls`, `installer`). Update [CHANGELOG.md](CHANGELOG.md) under
   *Unreleased* for user-visible changes.

## Pull requests

All changes, including the maintainer's, go through pull requests: `main` is
protected and only accepts pull requests whose CI passes on macOS and
Windows. Work on a branch named after the change (`feat/password-auth`,
`fix/date-columns`, `docs/readme`), keep each pull request focused on one
change and give it a Conventional Commits title: pull requests are squash
merged, so the title becomes the commit on `main`.

Describe how you tested the change, including the client application if it is
about Excel or Power BI behaviour.

## Code of conduct

This project follows the [Contributor Covenant](CODE_OF_CONDUCT.md).
