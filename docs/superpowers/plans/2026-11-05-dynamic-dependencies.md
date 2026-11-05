# Dynamic dependencies and v0.1.1 release

> **For agentic workers:** Use superpowers:executing-plans to implement this plan inline.

**Goal:** Remove tracked `tools/` and `third_party/` files, download dependencies during packaging, and publish tag `v0.1.1`.

**Architecture:** Extend the existing `scripts/fetch-deps.ps1` draft. Keep npm manifests under `scripts/lsp/`; retrieve pinned official binary archives with SHA256 checks into ignored directories. `package.ps1` prepares dependencies before configuring CMake, while the release workflow continues to build the pushed tag.

**Tech stack:** PowerShell, CMake/CTest, npm lockfiles, Qt 6.12, GitHub Actions.

**Spec:** User request in this chat: remove both directories from the repository, dynamically download their contents when packaging, commit and push, then create and push `v0.1.1` to publish a release.

## Constraints and choices

- Reuse the current checkout and preserve existing icon changes and the dependency script draft.
- Remove the directories from the current Git tree, preserving history and ignored local caches.
- Retain Mihomo 1.19.33, Wintun 0.14.1, YAML Language Server 2.0.0, and the fixed VC++ redistributable; use official Inno Setup 6.7.4 with its matching Chinese translation.
- Pin Node.js 22.18.0 in CI to match the runtime notice.
- Centralize provisioning in the packaging entry point so local and CI packaging use the same behavior.
- Compare approaches: workflow-only downloads leave local packaging broken; copying binaries into another tracked directory defeats the request. A shared download script covers both workflows.

## Tasks

- [x] Prove the existing draft cannot complete (missing npm manifests), then relocate the manifests, finish and test dependency preparation.
- [x] Integrate preparation into packaging, require the language server, update version/docs/ignore rules, and untrack both directories.
- [x] Verify downloader failure handling, a clean-source dependency install, all C++ tests, packaged application checks, ZIP, installer, and release notes.
- [ ] Obtain an independent review; commit and push the verified change, create annotated tag `v0.1.1`, and push it.
- [ ] Monitor Actions to completion and verify all public release assets and checksums.

## Review focus

- A clean clone must not depend on old binaries or npm installations.
- Corrupt archives and failed downloads must fail before extraction or packaging.
- A cached installation missing its language file or server must be repaired.
- A failed dependency setup must stop packaging immediately.
- Both tag and project version must be `0.1.1`; no existing release tag is moved.
