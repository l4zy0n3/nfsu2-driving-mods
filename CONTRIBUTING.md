# Contributing

Keep changes within the scope in `docs/FEATURES.md`. Add a focused regression test for nontrivial behavior. Run `make verify` and `make package`; never mark a live hardware check passed from a unit test.

Do not commit game binaries/assets, save files, prefixes, third-party mod binaries, extracted proprietary code, personal paths, controller GUIDs or telemetry dumps. Use synthetic fixtures and small source-level explanations of observed interfaces. Describe required game/dependency hashes when modifying a hook. Keep build guards, owner checks, rollback and freshness validation intact.

Contributions and distributed derivatives use GPL-3.0-or-later. Preserve applicable third-party notices. Issues should include the version, relevant dependency hashes, OS/Proton version, controller transport and minimal reproduction steps. Remove personal paths and identifiers from logs.
