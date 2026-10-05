# Phase 6 contract tests

Phase 6 adds text tools, a portable PCM bank builder, and general-purpose
export UI. The six CTests below were registered before the new services were
implemented. An absent activating header returns 77 and CTest reports Skip.
Once a header is added, its test body compiles and runs. Phase 6 is not complete
while any of its contract tests is skipped or failing.

## Current status (2026-10-05)

| CTest | Activating header | Current result |
|---|---|---|
| `phase6_text_transform_contract_test` | `editor/text_transform_service.h` | Pass |
| `phase6_voice_append_contract_test` | voice append and text transform headers | Skip |
| `phase6_pcm_bank_contract_test` | `editor/pcm_bank_service.h` | Skip |
| `phase6_format_validator_contract_test` | `editor/export_format_validator.h` | Skip |
| `phase6_export_operation_test` | existing `editor/export_service.h` | Pass |
| `phase6_integration_contract_test` | text transform, PCM bank, and validator headers | Skip |

Text transform service is implemented, including N88 removal, G-channel
conversion, missing metadata insertion, and N88 export. Apply uses an atomic
DocumentService document/revision check; unchanged text does not add a revision.
The macOS Tools menu provides N88 removal with original/converted previews,
Cancel, and a single Undo/Redo operation restoring the previous selection.
Real GUI acceptance remains pending.

Release, Debug, ASan/UBSan, and TSan: 34 registered, 30 passed, 4 skipped;
GUI builds succeed in all four configurations. The four skipped executables
still compile their fallback branches. The existing 28 Phase 0–5 tests remain
active. Voice append, PCM bank, validator, and integration are not implemented.

Run `ctest --test-dir build --output-on-failure -R '^phase6_'` for this phase.
All tests use separate CTest working directories. They may create uniquely
named temporary directories and remove only their own fixtures.

## Public API contract used by the tests

The tests define the minimum platform-neutral surface for the new services:

- `TextTransformService::Preview(DocumentSnapshot, TextTransformRequest)`
  returns transformed UTF-8 text without mutating the document.
  `Apply(DocumentService&, preview)` rejects a stale document/revision and
  performs a single document revision change. The request supports
  `RemoveN88LineNumbers`, `ConvertGChannelQ`, `AddMetadataTags`, and
  `ExportN88Basic`, with metadata values and line-number options.
- `VoiceAppendService::Preview(DocumentSnapshot, CompiledSong,
  VoiceBankSnapshot)` returns the same preview type. `CompiledSong` exposes
  owned `used_voice_numbers` to this service. The preview is bound to the
  compiled document ID and revision.
- `PcmBankService::BuildFromDataDirectory(path)` and `BuildFromList(path)`
  return a bank with owned bytes. `Save(bank, path)` writes it atomically.
  Errors include the offending input path.
- `ArtifactValidator::Validate(bytes, ExportFormat, expectedFrames)` parses
  WAV, VGM, and S98 independently of their writers.

These are contract choices for Phase 6 implementation; production APIs do not
exist yet. If an API is deliberately redesigned, update the tests and this
contract together, preserving the observable behaviors below.

## Text transform coverage

| Case | Assertion |
|---|---|
| N88 removal | decimal line numbers and the first apostrophe are removed; MML and later apostrophes remain |
| malformed N88 | mixed numbered and unnumbered source fails without losing text |
| preview/apply | preview is read-only; apply changes one revision and dirty state |
| stale preview | applying after a new edit or to another document returns Conflict and preserves text |
| concurrent apply | exactly one of two changed previews commits; the other returns Conflict |
| unchanged apply | unchanged text preserves revision |
| malformed input | invalid UTF-8, missing N88 apostrophe, and multiline tag values are rejected |
| G channel | only G-channel `q` commands change; other channels, tags, comments, and uppercase `Q` remain |
| tag insertion | existing title wins, missing composer/PCM are added, and a second run is idempotent |
| N88 export | start/increment and blank lines are reflected; export/removal round trip restores text |
| invalid numbering | zero increment and overflowing final line number fail |

The AppKit acceptance additionally checks that Apply is one native Undo/Redo
step, preview cancellation leaves the document untouched, and selection/dirty
state are restored by Undo. Those behaviors require a real editor UI run.

## Voice append coverage

The contract uses a real 8192-byte voice bank. It checks deduplication and
ascending order of used voice numbers, preservation of the original MML,
read-only preview, one-revision apply, idempotent reapply, empty usage, invalid
voice numbers, and rejection of a different document ID. The implementation
stage must also verify a generated definition compiles in the intended driver
mode; that scenario belongs in the integration test when the final format is
known.

## PCM bank coverage

The test synthesizes DATA, VOICE._1, ADPCM, WAV, and list files. It checks the
1024-byte header, 32-byte entries, little-endian address/length fields, body
bytes, path resolution relative to the list, a Unicode path, and unchanged
process current directory. It checks one and 32 entries, rejects 33, rejects
an oversized body, identifies a missing entry file, missing DATA, a missing
list input, and a truncated WAV. The integration test passes a generated bank
through compile and verifies it is embedded in the resulting MUB.

Additional format cases to add as the WAV/ADPCM decoder contract is finalized:
8/16-bit mono/stereo support, unknown RIFF chunks, odd-byte padding, and
ADPCM length/hash. The service must reject unsupported formats explicitly.

## Export and parser coverage

`phase6_export_operation_test` runs now against `ExportService`. It checks
Unicode/space paths, progress operation/document/revision IDs, monotonic frame
counts, the exact final frame count, one completion, cancellation while the
render worker is active, preservation of an existing destination, and no
partial file after cancellation or invalid argument/I/O errors.

The validator contract exports real one-second WAV, VGM, and S98 fixtures,
then mutates them. It rejects wrong magic, truncation, WAV size/rate errors,
VGM offset/sample-count/end errors, and S98 offset/device-table/end errors.
It is separate from the production writer and does not use Windows golden
bytes. The integration contract exercises PCM bank creation, tag transform,
compile, embedded PCM MUB export, WAV export, and structural validation.

## Completion gate

All six Phase 6 tests must be active and pass with the existing suite in
Release, Debug, ASan/UBSan, and TSan. The nine `GUI-TOOL`/`GUI-EXPORT` items in
`tests/manual/macos-gui-acceptance.md` must have real GUI evidence, including
Undo/Redo, save panel cancellation, progress cancellation, and errors. No
test-created output may remain in the source tree. Windows byte equality and
Windows loading are outside this phase's gate.
