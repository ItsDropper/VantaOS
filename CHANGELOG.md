# Changelog

All notable changes to VantaOS are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). VantaOS is
pre-1.0 and has no stable ABI, so versions are not yet tagged; entries are
grouped under **Unreleased** until the first release.

## [Unreleased]

### Added
- NVX (NVIDIA GA10x) diagnostics: `nvx status|survey|vbios|fw`, VBIOS PROM
  reader, GSP firmware SHA-256 manifest verification, WPR sizing policy
  (reference-informed, unverified).
- NVX independence policy and a hardware-knowledge database with an analyzer
  for `nvx survey` logs.
- Documentation overhaul: index, getting started, glossary, style guide, NVX
  overview, VFIO passthrough testing guide, Markdown link checker and CI.
- Repository community files: changelog, support policy, code owners,
  Dependabot configuration, issue-template config.

### Changed
- GPU bring-up status consolidated into `docs/gpu/FRAMEWORK.md`.
- `.gitignore` covers disk images, logs, firmware and host-tool artifacts.

### Not yet established
- Any hardware-verified GPU bring-up or accelerated rendering. See the
  [status table](README.md#status).
