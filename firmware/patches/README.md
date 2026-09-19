# Vendored patches against `freeink-sdk`

`freeink-sdk` is a submodule of a repository we do not control
(`Free-Ink/freeink-sdk`), so a change we need there cannot simply be committed
and the pointer bumped. Until each of these lands upstream, it is carried here
and reapplied to the submodule working tree at build time by
`scripts/patch_freeink_sdk.py`.

Without this, the firmware does not compile from a clean checkout — the symbols
live only in a working copy on one machine, which is how CI came to fail on a
tree that built fine locally.

| Patch | What it adds | Upstream status |
|---|---|---|
| `freeink-sdk.patch` | `SecureClient::lastConnectError` / `lastConnectStage`, and a CA-load failure that reports itself where it happens rather than surfacing as `ASN_NO_SIGNER_E` mid-handshake. Plus the `Uc8279Driver` change. | not yet submitted |

## When a patch lands upstream

Bump the submodule pointer to the commit that carries it and delete the patch
here. The script is a no-op once the change is already present, so the order of
those two steps does not matter.

## Regenerating

With the submodule working tree in the state you want:

```sh
git -C freeink-sdk diff > patches/freeink-sdk.patch
```

Keep it a plain `git diff` against the pinned submodule commit — the script
applies it with `git apply` and relies on the reverse-check to stay idempotent.
