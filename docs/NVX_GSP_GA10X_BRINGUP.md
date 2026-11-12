# Legacy NVX GA10x bring-up documentation

This path is retained for existing links. Its former implementation notes are
superseded and intentionally removed so that old claims do not conflict with
the maintained status.

Use these current documents instead:

- [GA10x implementation and startup sequence](gpu/nvx/GA10X_BRINGUP.md)
- [RTX 3060 readiness gates and evidence requirements](gpu/nvx/RTX3060_READINESS.md)
- [Remaining GSP integration gaps](gpu/nvx/GSP_BOOTSTRAP_GAPS.md)
- [Manual diagnostic and firmware-preflight commands](gpu/nvx/BRINGUP.md)
- [NVX architecture and source ownership](NVX_ARCHITECTURE.md)

Current summary: NVX contains a callable GA10x startup transaction that
sequences FWSEC/FRTS, SEC2 Booter Load, RISC-V readiness, MSGQ linking and
GSP-RM initialization. The transaction is not invoked by the boot path or
current shell commands, has no dedicated end-to-end host test, and has not
been validated on physical GA106 hardware. Do not use this legacy file as a
readiness claim.
