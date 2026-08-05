# vendor/netbsd-rump — NetBSD rump kernel import

This directory is where `scripts/import-rump.sh` writes the
imported NetBSD source subtrees. It's empty by default because
the imported source is many hundreds of MB and doesn't belong in
git as-is.

## What gets imported

Only the subtrees needed for network:

- `sys/rump/librump/`        — rump kernel core + `rumpuser` header
- `sys/rump/net/librumpnet/` — network subsystem entry point
- `sys/net/`                 — link layer, if_ethersubr, routing
- `sys/netinet/`             — IPv4, TCP, UDP, ICMP
- `sys/netinet6/`            — IPv6 (imported but not gated on until Phase 5)
- `sys/kern/`                — subset needed by the above (mbuf, uipc_*, etc)
- `sys/sys/`                 — public kernel headers
- `common/lib/libc/`         — string/mem primitives the kernel uses

Total: ~40-80 MB depending on what pulls in transitively.

## Pinning

Use a specific NetBSD release, not `-current`. Recommended:

```
UPSTREAM_URL=https://github.com/NetBSD/src
UPSTREAM_REF=netbsd-10-1-RELEASE     # or a specific commit sha
```

Import script hashes the tarball and stores the sha256 in
`IMPORT_MANIFEST.txt` so re-imports are reproducible.

## Layout after import

```
vendor/netbsd-rump/
├── IMPORT_MANIFEST.txt         # upstream URL + ref + sha256
├── sys/
│   ├── rump/
│   ├── net/
│   ├── netinet/
│   ├── netinet6/
│   ├── kern/            # only the files we need
│   └── sys/
├── common/
│   └── lib/libc/        # only the pieces we need
└── amigaos_patches/     # local patches applied post-import
    ├── 0001-remove-vfs-references.patch
    ├── 0002-newlib-time-h-collision.patch
    └── README.md
```

## Import workflow

1. Run `scripts/import-rump.sh` (currently a stub — writes this
   README pointer instead of actually importing).
2. Verify the manifest matches the pinned ref.
3. Run per-patch apply from `amigaos_patches/`.
4. Update the top-level Makefile to include the imported .c
   files in per-phase source lists.

## Why not git submodule / subtree?

- Git submodules make CI/tooling fragile for the working tree
  size involved.
- Full NetBSD is ~5 GB; we want a small subset only.
- We'll almost certainly maintain local patches, and merging
  those with submodule updates is painful.

The tarball-import-and-patch pattern is what FreeBSD does for
its own OpenSSL/libarchive vendor imports; well-trodden.
