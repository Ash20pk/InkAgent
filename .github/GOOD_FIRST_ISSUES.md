# Drafted good first issues

Staging list. Each of these is a real gap found in the tree, written the way a
newcomer needs it: **the problem, the file to start in, and how to check the
result.** Open them as issues and label `good first issue`; delete the entry
here once it is filed.

Numbers below were measured on `main` at `553cc3f`. Re-check before filing.

---

## Translations — the largest open gap, and no hardware needed

`firmware/lib/I18n/translations/english.yaml` has **579 keys**. Every other
language is behind it, some by a lot. Format and workflow:
[`docs/i18n.md`](../firmware/docs/i18n.md),
[`docs/translators.md`](../firmware/docs/translators.md).

| Language | File | Keys | Missing |
|---|---|---|---|
| Finnish | `finnish.yaml` | 297 | ~282 |
| Danish | `danish.yaml` | 327 | ~252 |
| Dutch | `dutch.yaml` | 327 | ~252 |
| Romanian | `romanian.yaml` | 327 | ~252 |
| Bulgarian | `bulgarian.yaml` | 430 | ~149 |
| Belarusian, Bosnian, Czech, French, Hungarian, Indonesian, and others | — | 440 | ~139 |

**One issue per language**, titled e.g. *"Finnish translation is ~282 keys
behind English"*. Each should say: diff the file against `english.yaml`, add the
missing keys, keep key order matching English so future diffs stay readable.

*How to check:* `cd firmware && pio run -e inkagent-check` regenerates the C++
from YAML and will fail on a malformed file. A missing key falls back to
English, so a partial contribution is safe to merge.

---

## 1. A script that reports translation coverage

There is no way to see how far behind a language is without counting by hand —
which is how the table above was produced, and why nobody knows the numbers.

*Start in:* `firmware/scripts/`, next to `gen_i18n.py` which already parses
these files. Add `i18n_coverage.py` printing per-language key counts and the
names of missing keys.

*How to check:* run it; the totals should match the table above.

*Why it matters:* it turns "translations need work" into 33 concrete, claimable
tasks, and it can run in CI as a report.

---

## 2. `device-sim` has no README

The simulator is the main way to contribute without owning a reader, and it is
undocumented outside a comment at the top of `sim.js`. `CONTRIBUTING.md` now
points people at it.

*Start in:* a new `device-sim/README.md`. Cover: `pair`, `ask`, `reset`, the
flags (`--relay`, `--kind`, `--book`, `--chapter`, `--pct`, `--text`, `--file`,
`--budget`), what `state.json` holds, and the `INK_RELAY` environment variable.

*How to check:* follow your own README from a clean clone against a local relay.

---

## 3. Opening a highlight goes to the wrong place

Known limitation, recorded in `firmware/docs/ROADMAP.md` §6 P3: confirming an
entry in Highlights opens the book **at its own saved position, not at the
bookmark**. Travelling to a bookmark from outside the reader needs a position to
pass through `goToReader`, which currently cannot carry one.

*Start in:* `firmware/src/activities/highlights/`, and the `goToReader`
signature.

*How to check:* host tests in `firmware/test/chapter_position` and
`firmware/test/page_link` cover position handling. Final confirmation needs a
device — say so in the PR rather than implying otherwise.

*Size:* touches a shared navigation signature, so agree the approach in the
issue before writing it.

---

## 4. The reading-pace baseline is a hardcoded guess

`ROADMAP.md` §6 P2: pace is compared against a **fixed 25 s/page assumption**
rather than a baseline learned from the reader. That makes the behavioural
feature nearly meaningless for anyone unusually fast or slow — and that feature
steers which part of a passage the agent asks about.

*Start in:* `firmware/src/activities/reader/` where pace is computed, and
`firmware/src/activities/stats/` where per-reader history now lives (Analytics
already persists what a baseline needs).

*How to check:* `firmware/test/reading_pace` and `firmware/test/reading_stats`.

*Note:* no new data may leave the device — the baseline is local, and only the
derived ratio goes out. See Settings › InkAgent › *What leaves this device*.

---

## 5. "Which board do I have?" is missing from troubleshooting

The release notes and the bug template both ask people to name their board, and
there is nowhere that tells them how. Five images, and flashing the wrong one is
a bad first experience.

*Start in:* `firmware/docs/troubleshooting.md`.

*How to check:* the distinguishing marks are in `platformio.ini` (MCU, PSRAM,
touch controller, USB-MSC) and in the SDK board profiles. Photos welcome if you
own one.

---

## 6. More example manifest apps

`firmware/examples/apps/` ships very few, and the format is the project's main
extension surface. Manifests are declarative — no control flow, fixed data
sources, row kinds `text`, `para`, `kv`, `rule`, `logo`.

*Start in:* [`docs/APP_DEVELOPMENT.md`](../firmware/docs/APP_DEVELOPMENT.md) and
the existing examples.

*How to check:* the relay's validator (`relay/src/manifest.js`) rejects anything
the format does not have; `cd relay && npm test`.

*Good ones are ones that terminate* — no feed, no unread count, nothing that
notifies.

---

## 7. Hyphenation patterns for more languages

Reader typography is in scope and this is pure data.

*Start in:* [`docs/hyphenation-trie-format.md`](../firmware/docs/hyphenation-trie-format.md),
`firmware/scripts/generate_hyphenation_trie.py`,
`firmware/scripts/update_hyphenation.sh`.

*How to check:* `firmware/test/hyphenation_eval` scores break quality; flash
size matters, so report the delta.

---

## Needs hardware

Not first issues, but the highest-value thing an owner can do. `ROADMAP.md` §10
lists ten verifications that are blocked only on someone pressing buttons —
including the **first real test of CA pinning**, which is the one failure that
would be invisible until a device silently stops reaching the relay.

Worth filing as a single tracking issue labelled `needs-hardware`, so an arriving
owner has something concrete to claim.
