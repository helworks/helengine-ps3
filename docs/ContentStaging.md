# PlayStation 3 content staging

The builder validates the complete manifest before clearing the previous content
stage. A missing file, null declaration, duplicate runtime path, invalid path,
SHA-256 mismatch, or cancellation during preflight leaves the previous stage
intact. Valid builds clear stale content and copy only declared artifacts in
deterministic path order.

Source and staging roots must be separate directories with neither containing
the other. Root comparisons use directory boundaries, so sibling names such as
`source` and `source-staged` are valid. Existing filesystem links at either
root, their ancestors, or declared source files are rejected; junctions must
not redirect source reads or disguise overlapping directories.

Paths remain content-relative. Rooted paths and dot traversal segments are
rejected using the shared packaged-path validator. Backslashes and repeated
separators normalize to forward slashes, and older manifests retain their
filename case. Normalized aliases still collide. Colon streams and trailing
dot/space aliases are rejected before Windows can map them to another file.

Editor receipts beginning with `sha256:` are checked against the source bytes,
with case-insensitive hexadecimal comparison. Legacy opaque content identities
remain accepted. Verification reads the file as a stream rather than buffering
the entire package.

This guarantee covers preflight rejection. Copying is not transactional: a disk
error, cancellation after refresh begins, or concurrent source mutation can
interrupt the new stage. The builder reports such failures and does not proceed
to native compilation.

Regression coverage is in `builder.tests/Ps3ContentStagerTests.cs`.
On 2026-10-07 all 19 focused cases passed, including real Windows junction
fixtures and SHA-256 receipt verification. Build outputs and logs are under
`C:/dev/helworks/builds/helengine-ps3/coord-20261007`; the test wrapper
`test-content-stager.ps1` sets the workspace-owned output and temporary paths.
No emulator or native SDK validation is needed for these managed staging changes.
