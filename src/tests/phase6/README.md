# Phase 6 contract tests

Phase 6 adds text tools, a portable PCM bank builder, and general-purpose
export UI. The original six CTests were registered before the new services were
implemented. An absent activating header returns 77 and CTest reports Skip.
Once a header is added, its test body compiles and runs. Phase 6 is not complete
while any of its contract tests is skipped or failing.

## Current status (2026-10-06)

| CTest | Activating header | Current result |
|---|---|---|
| `phase6_text_transform_contract_test` | `editor/text_transform_service.h` | Pass |
| `phase6_g_channel_contract_test` | existing text transform and document services | Pass |
| `phase6_metadata_tag_contract_test` | text transform, metadata, document, and compiler services | Pass |
| `phase6_n88_export_contract_test` | N88 export, text transform, document, and compiler services | Pass |
| `phase6_voice_usage_contract_test` | existing compiler service; owned usage field | Pass |
| `phase6_voice_append_contract_test` | voice append and text transform headers | Pass |
| `phase6_pcm_wave_contract_test` | existing `Adpcm` and independent test oracle | Pass |
| `phase6_pcm_bank_contract_test` | `editor/pcm_bank_service.h` | Pass |
| `phase6_format_validator_contract_test` | `editor/export_format_validator.h` | Skip |
| `phase6_export_operation_test` | existing `editor/export_service.h` | Pass |
| `phase6_integration_contract_test` | text transform, PCM bank, and validator headers | Skip |

Text transform service is implemented, including N88 removal, G-channel
conversion, missing metadata insertion, and N88 export. Apply uses an atomic
DocumentService document/revision check; unchanged text does not add a revision.
The macOS Tools menu provides N88 removal and G-channel conversion with
original/converted previews, Cancel, and a single Undo/Redo operation restoring
the previous selection. The G-channel menu and preview/Cancel/Apply/Undo/Redo
path have been exercised in the real GUI, including restoration of a selected
non-G line. The remaining acceptance matrix (saved documents, dirty state,
multiple windows, and conflict) is pending.

Release, Debug, ASan/UBSan, and TSan each have 40 registered tests:
38 Pass, 2 Skip, 0 Fail. Voice usage, voice append, PCM bank, and the portable
pcmtool CLI execute against production code. Format validator and Phase 6
integration remain skipped because the validator is unimplemented.

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

Text transform, voice append, and PCM bank APIs are implemented. The artifact
validator remains a contract for later implementation. If its API is deliberately
redesigned, update its test and this contract together while preserving behavior.

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

### G-channel transform GUI entry (`GUI-TOOL-02`)

The platform-neutral transformation and Tools action are implemented. The
action uses the same preview, stale-revision check, single Undo step, and error
presentation as N88 removal. No PCM bank, export writer, or new audio ownership
is involved in this step.

`phase6_g_channel_contract_test` is active now. Its cases form the automated
part of the pre-implementation plan:

| ID | Input or action | Required result |
|---|---|---|
| `GCH-01` | G with spaces/tabs and `q0`, `q-2`, `q+3`, `q4` | only eligible lower-case commands become `@` |
| `GCH-02` | A–F, H–K, lower-case g, `Gmacro`, `Gq` | every non-G line remains byte-for-byte the same |
| `GCH-03` | quoted `q`, semicolon comment, tag, upper-case Q, bare q, `qx` | protected/non-command text remains unchanged |
| `GCH-04` | preview, Apply, second preview/Apply | preview is read-only; first Apply changes one revision and dirty state; second is a no-op |
| `GCH-05` | mixed LF/CRLF/CR, UTF-8 BOM, CP932 Japanese comment | decoded text changes only at target commands; saved encoding and exact line endings remain |
| `GCH-06` | numbered N88 source | direct G conversion is a no-op; remove numbers then convert changes only G commands |
| `GCH-07` | stale preview and another document | Conflict is returned and both documents retain their current text |
| `GCH-08` | replace one `@8` in the real `sampl1.muc` G line with `q8` | transformation restores the original source and it compiles with embedded PCM |

The GUI acceptance part uses a saved `.muc`, a dirty `.muc`, and a numbered
`.n88` with the same lines. Select some text before opening Tools → Convert G
Channel q to @. Verify Original/Preview, then Cancel: text, selection,
revision, and dirty indicator remain unchanged. Apply once and check the
expected G lines; one Undo restores text, selection, and dirty state, and one
Redo reapplies it. Repeat with two open editor windows to check that the
preview is applied only to its source document. Edit the source while a
preview is open and confirm Apply reports a conflict without overwriting the
new edit. For `.n88`, remove line numbers first and then run this transform.
Compile the converted MML with the macOS compiler and record the result. The
automated compiler case uses a valid packaged song because a short synthetic
G-channel line can omit driver parameters and fail for unrelated reasons.

Passing the CTest alone does not mark `GUI-TOOL-02` complete. The menu action is
present and the basic real-GUI flow passed on 2026-10-06, but the rest of the
acceptance matrix above remains open. The GUI test uses the application's
standard Undo manager; the C++ service cannot validate AppKit selection or Undo
grouping.

### Metadata-tag GUI entry (`GUI-TOOL-03`)

The GUI entry is implemented. Windows inserts `title`, `composer`,
`author`, `voice`, `pcm`, `date`, and `comment`; the macOS metadata parser and
compiler recognize the exact lower-case spellings. The UI should collect the
missing fields, omit fields left empty, show existing canonical fields without
overwriting them, and use the shared Original/Preview → Apply/Cancel → one
Undo/Redo flow. It must not create empty tags implicitly. A numbered N88
source must have its line numbers removed before this MUC-oriented operation.

`phase6_metadata_tag_contract_test` is the platform-neutral pre-implementation
contract. It has the following cases:

| ID | Fixture or action | Required result |
|---|---|---|
| `TAG-01` | all seven canonical fields on a source with `#mucom88` | requested order after the leading tag block; parser reads every value, including Japanese text and paths with spaces |
| `TAG-02` | existing first/duplicate tags and duplicate request keys | existing values win, first requested missing value wins, second Apply has no revision change |
| `TAG-03` | upper-case `#TITLE` only | preserve it but add usable lower-case `#title`; runtime parser resolves the new value |
| `TAG-04` | empty/invalid names, LF/CR/NUL or invalid UTF-8 in values | preview fails atomically and leaves document/revision unchanged; empty request is a no-op |
| `TAG-05` | stale preview or another document | Conflict without overwriting either document |
| `TAG-06` | UTF-8 BOM/CRLF, CP932/Japanese, mixed LF/CRLF/CR | preserve encoding and every original line ending; new line uses preferred ending |
| `TAG-07` | leading tag without a final newline; numbered N88 composition | insert a separator and tags correctly; strip N88 numbers before tagging |
| `TAG-08` | remove composer from packaged `sampl1.muc`, then re-add | existing title remains, compiler reports restored composer and emits PCM-bearing MUB |
| `TAG-09` | saved/dirty document; discard preview; request only an existing title | text, encoded bytes, revision, saved revision/content ID, encoding, endings, path and dirty state remain unchanged |
| `TAG-10` | two inserted tags; LF/CRLF/CR, mixed CRLF majority, mixed prepend, unterminated MML, empty source | exact encoded bytes and per-line ending vector match independently decoded expected bytes; existing endings stay attached to original lines; additions use the original preferred ending; one revision |
| `TAG-11` | change encoding or newline style after preview | Apply returns Conflict and preserves the changed settings, text, encoded bytes, revision and dirty state |
| `TAG-12` | two threads Apply the same changed preview | exactly one succeeds, the other returns Conflict; one title and one revision increment |
| `TAG-13` | malformed optional ending vectors (empty, short, Mixed, out-of-range) | InvalidData without modifying text, revision, encoding, endings or dirty state |
| `TAG-14` | Apply → inverse preview → redo on saved and dirty mixed-newline documents | exact original bytes and original dirty/content ID restored; redo restores transformed bytes; each update adds one revision |


Every case prints its TAG ID before running; the newline matrix also prints
its fixture name. Cases continue after assertion failures so one failed
behavior does not conceal later checks. These are active tests against the
current public API, with no expected-failure flag or activating-header skip.
No new preview/newline API is prescribed by these tests: the oracle is exact
saved bytes and DocumentSnapshot state. Implementation may choose its API
while preserving these observations.

Implementation run (2026-10-06): all 14 cases pass in Release, Debug,
ASan/UBSan and TSan. TAG-03 also covers mixed-case names, indented tag-like
lines, and longer unrelated names to match the runtime parser. TAG-13 and
TAG-14 verify the new optional-ending API and the service-side inverse used
by native Undo/Redo. TAG-09 tests service-side discard; native Cancel and
Undo evidence is recorded separately.

The first run on 2026-10-06 compiled but failed at `TAG-03` and the mixed
newline subcase of `TAG-06`. `TextTransformService` currently folds tag names
to lower case when detecting existing tags, whereas `MetadataService` only
recognizes exact lower-case names. `DocumentService` keeps original newline
styles by line index, so inserting a line into mixed-newline text shifts the
styles of subsequent original lines. Both behaviors were corrected on 2026-10-06; all assertions now pass
without skips or expected-failure flags.

The GUI acceptance uses a saved UTF-8 MUC with `#mucom88`, an existing title,
missing composer/author/voice/pcm/date/comment, and a second CP932 or mixed-
newline document. Check field collection and empty-field omission; original/
preview and the rule that existing canonical tags win; Cancel preserving text,
selection, revision, and dirty state; Apply as one Undo/Redo step; and that two
windows cannot cross-apply. Check an upper-case-only tag is not mistaken for a
runtime tag, N88 is stripped before use, and the resulting saved MUC compiles.
The C++ contract cannot validate AppKit form defaults, selection, or Undo.


### N88-BASIC source export (`GUI-TOOL-07`)

The native export action is implemented using N88ExportService and the
existing ExportN88Basic transformation. It has fewer new service dependencies than
voice append or PCM bank. Capture the editing snapshot, collect start/increment,
show a read-only numbered preview, then save an isolated output document;
do not Apply numbered text to the editing document. Native export must preserve
the source text, selection, revision, dirty state, location and Undo history.
The saved format is numbered text, not tokenized BASIC. Current numbering
contract uses nonnegative signed-int start and positive signed-int increment;
this is not a claim about a physical N88 interpreter's accepted number range.

`phase6_n88_export_contract_test` tests production N88ExportService::Save
against actual files. The service validates source document/revision,
protects the original source path (including symlinks/hardlinks), and uses an
isolated output DocumentService for atomic writes. Its Preview rejects
already-numbered source. The GUI exposes settings → numbered preview →
NSSavePanel, with integer validation and explicit encoding choice.

| ID | Fixture/action | Expected observable result |
|---|---|---|
| `N88-01` | three lines, defaults 1000/10 and custom 7/3 | exact numbered text; preview keeps source ID/revision and never mutates source |
| `N88-02` | leading/interior blank lines, spaces/tabs, apostrophes, quoted text, comments | only numbering/apostrophe prefix is added; export/remove round trip preserves all source text |
| `N88-03` | empty, newline only, no final newline, final newline, double final newline | no invented trailing line; empty source produces empty output; exact terminal newlines |
| `N88-04` | INT_MAX single line; last number exactly INT_MAX; overflow by one | exact-boundary outputs succeed; overflow fails without partial application |
| `N88-05` | negative start, zero/negative increment, malformed UTF-8, NUL | InvalidData and unchanged source snapshot |
| `N88-06` | discard preview on saved and dirty documents | text, source bytes, revision, saved revision/content ID, encoding, endings, location and dirty state unchanged |
| `N88-07` | UTF-8 Japanese tags, mixed CRLF/LF/CR, Unicode/space output path | exact saved bytes, original endings preserved, saved output reopens as N88 and removes back to original text; editing document unchanged |
| `N88-08` | UTF-8 BOM and CRLF, .bas destination | exactly one BOM; exact numbered CRLF bytes and reopen/removal round trip |
| `N88-09` | Japanese CP932 and CRLF | exact legacy bytes with ASCII prefixes; reopen/removal restores decoded original source |
| `N88-10` | emoji to CP932 over existing destination; missing output directory | UnsupportedEncoding/IoError; existing destination preserved; failed output document unchanged; no new partial files/directories |
| `N88-11` | packaged sampl1.muc → numbered text → remove → compile | source text and compiled MUB bytes equal original; embedded PCM remains; original document unchanged |
| `N88-12` | disk source A c, dirty buffer A d, separate N88 export | output contains A d; original disk file remains A c; editing snapshot remains dirty and otherwise unchanged |
| `N88-13` | edited source or another document after preview | Conflict, no output file, current source unchanged |
| `N88-14` | source path, lexical alias, symlink and hardlink | InvalidArgument, original bytes and source snapshot unchanged |
| `N88-15` | empty path, unknown encoding, already-numbered source | explicit errors; no output for invalid encoding; no double numbering |


Tests create a uniquely named temporary directory and remove only that directory
through RAII. They check the process current directory remains unchanged.
The new test passes in Release, Debug, ASan/UBSan and TSan. Full Release suite:
37 registered, 33 Pass, 4 Skip, 0 Fail. The 15-case contract now covers the production service. Native save panel Cancel, custom numbering, output bytes, selection
preservation and original Undo history were exercised; the full GUI matrix remains
open as recorded in tests/manual/macos-gui-acceptance.md.

## Next priority: used FM voice append (`GUI-TOOL-04`)

Text transforms and N88 output now have GUI entries. Used FM voice append is
the next implementation unit because it reuses VoiceService snapshots and
TextTransformService preview/Apply/Undo. It needs no PCM bank builder or
artifact validator. CMucom exposes GetUseVoiceMax/GetUseVoiceNum, but the
current compiler boundary does not publish owned usage information.
The compiler must copy usage from the compiled artifact/runtime at the correct
lifecycle point; scanning every source `@` command is not sufficient because
PSG/rhythm/PCM commands use the same syntax.

`phase6_voice_usage_contract_test` is active now, without an activating-header
skip. A dependent C++17 detection branch reports the absent field as a real
assertion failure while compiling against the current public API. Once the
field exists, the same test checks its content and ownership.

| ID | Fixture/action | Expected result |
|---|---|---|
| `VOICE-USAGE-01` | compile packaged sampl1.muc | owned `std::vector<int> used_voice_numbers`; unique set equals 31,78,93,106,108,159; exclude PSG/rhythm/PCM numbers 0,1,4,8,11; all numbers in 0..255 |
| `VOICE-USAGE-02` | compile sampl2 with same compiler | retained first result still contains sampl1 usage |
| `VOICE-USAGE-03` | destroy compiler then inspect retained result | owned usage remains valid and identical |

`phase6_voice_append_contract_test` now runs VoiceAppendService::Preview(snapshot, song,
bank), the owned usage field, and the existing TextTransformPreview type.
Synthetic songs explicitly select DriverMode::Mucom88.

| ID | Input/action | Expected result |
|---|---|---|
| `VAP-01` | usage 78,31,78; preview → Apply → repeat | one definition per used voice, ascending order; original prefix retained; read-only preview with matching ID/revision; first Apply one revision/dirty, repeat no-op |
| `VAP-02` | empty usage; existing explicit @31 definition different from bank | empty usage no-op; preserve explicit definition byte-for-byte and append only missing @78 |
| `VAP-03` | -1/256 mixed with valid usage; wrong compiled document/revision; edit after preview | invalid number gives InvalidData atomically; mismatches/stale Apply give Conflict; current text/settings preserved |
| `VAP-04` | synthetic 8192-byte bank with distinct parameters in each stored operator | independent numeric parser reads FB/AL and four AR/DR/SR/RR/SL/TL/KS/ML/DT rows; exact expected values distinguish storage order 1,3,2,4 from logical order 1,2,3,4; bank unchanged |
| `VAP-05` | voices 0 and 255 | one complete 38-number definition each; neither boundary dropped |
| `VAP-06` | mixed CRLF/LF/CR and unterminated source; Apply then inverse preview | original text/endings retained; separator when needed, new lines use preferred ending; inverse restores exact original bytes and clean state |
| `VAP-07` | voice name containing quote/brace/control/NUL/non-ASCII byte; malformed source; AR=32 | name is safe UTF-8 with no NUL or extra closing brace and intact parameters; invalid source/tone rejected as InvalidData without mutations |
| `VAP-08` | compile sampl1 → append from actual usage → recompile | exactly six FM definitions and no PSG/rhythm/PCM definitions; generated MUCOM syntax compiles; total count/title/embedded PCM preserved |
| `VAP-09` | BOM/CRLF and CP932 Japanese source; Unknown/MucomDotNet driver | encoding and exact original byte prefix preserved after one revision; unsupported drivers return UnsupportedDriver without mutation |

Definition whitespace and safe-name spelling are not fixed byte goldens.
Numeric parameter checks use an independent parser, and VAP-08 checks actual
compiler acceptance. The compiler-facing usage set is derived from the known
FM channels in the packaged fixture, not from a generic regex over source.
Classic definition syntax has no explicit AM field. Missing used tones with
AM enabled return UnsupportedFormat atomically; existing inline definitions
are preserved. VAP-07 checks this rejection. VAP-08 also compiles and recompiles
fixtures with explicit Mucom88, Mucom88E, and Mucom88EM modes.

Validation (2026-10-06): all four GUI builds and full suites pass (35 Pass,
3 Skip, 0 Fail). Expanded driver fixtures also pass in all four configurations.
Native GUI evidence covers six-voice preview, Cancel, Apply, one Undo/Redo,
recompile, and repeat Apply without an extra Undo entry. GUI testing found
and fixed a dangling preview reference in the asynchronous sheet and an empty
Undo group on unchanged Apply. The remaining manual matrix is pending.

## PCM bank implementation and coverage (2026-10-06)

Portable PCM bank service is shared by GUI-TOOL-05 (DATA) and GUI-TOOL-06
(list). `editor/pcm_bank_service.h` provides const methods
`PcmBankService::BuildFromDataDirectory(path)` and `BuildFromList(path)`,
returning `ServiceResult<PcmBankArtifact>`. Artifact bytes and absolute input
paths are owned. `Save(artifact, path)` returns `ServiceResult<string>` and
validates header ranges before atomic temporary-file replacement.
Input paths, including symlinks/hardlinks, cannot be output destinations.
Builders never write intermediate files or change the process cwd.

The portable CMake `pcmtool` target uses this service:

```sh
build/pcmtool --list samples.txt --output bank.bin
build/pcmtool --data directory-containing-DATA --output bank.bin
```

CLI usage errors return 2; build/save failures return 1 with message/path.
`pcmtool_cli_contract_test` verifies Unicode/space input paths, exact bank
body, failure preservation, source overwrite rejection, and argument errors
with an invalid SDL driver to ensure conversion needs no audio device.

The output is a 1024-byte header (32 slots of 32 bytes), followed by an ADPCM
body. Fields at +28/+30 are little-endian start/length in four-byte units.
Raw samples are zero-padded to four bytes, and length includes that padding.
The aligned body must be strictly below 0x40000 bytes: 0x3fffc is accepted;
0x40000 cannot be represented as one 16-bit length and is rejected. The same
limit applies cumulatively, including padding. Empty input is an error.

Lists are UTF-8 (optional BOM), one unquoted path per nonblank line, with LF,
CRLF, or CR and an optional terminal newline. Blank whitespace-only lines
are ignored. Spaces inside paths are preserved. Relative paths use the list
parent; absolute paths remain absolute. Order and repeated paths are retained.
`.adpcm`/`.bin` are raw input; `.wav` is converted (case insensitive).
Unknown extensions return UnsupportedFormat. ASCII list basenames are clipped
to 16 bytes and space-padded in the header. Unicode directory paths are covered;
non-ASCII list display-name bytes use `?`; DATA name bytes are preserved.
Unicode input and output filesystem paths are supported.

DATA is exactly 1024 bytes. Nonzero source address ranges define occupied
slots; retain sparse slot numbers and names/options while rebuilding body
start/length. Empty slots require no VOICE file. Occupied slots require
VOICE._(slot+1), with bytes matching the declared range. Missing occupied
files fail the whole build instead of silently producing an incomplete bank.

WAV contract: RIFF/WAVE, PCM format 1, 16-bit mono/stereo, nonzero sample rate,
consistent blockAlign/byteRate, whole nonempty frames, valid bounded chunks.
Unknown chunks and odd-size padding are supported. Conversion uses 16000 Hz
and 32-byte ADPCM padding, matching the existing converter. 8-bit, IEEE float,
and >2 channels return UnsupportedFormat. Structural corruption returns
InvalidData. Builds create no intermediate `_adpcm.bin` or destination file. Input reads
are bounded: list 1 MiB, WAV 16 MiB, raw samples by bank capacity. Resampled
output size is checked before allocating the converter buffer.

| ID | Fixture/action | Expected result |
|---|---|---|
| PCM-01 | 5-byte raw + 4-byte raw list | exact body including three zero pad bytes; start/length 0/2 and 2/1 |
| PCM-02 | raw lengths 1,2,3,4,7,8,9 | ceil(length/4) units; aligned body, no truncation |
| PCM-03 | two entries and unused slots | basename/space-padded header and 16-byte name truncation; remaining 30 slots zero; independent range oracle passes |
| PCM-04 | build, retain result, build different list | earlier bytes owned and unchanged; inputs and file inventory unchanged |
| PCM-05 | BOM, blank lines, LF/CRLF/CR, terminal LF absent, spaces/absolute path | same one-slot result; Unicode list directory resolves correctly |
| PCM-06 | duplicate path lines | two ordered slots, no deduplication |
| PCM-07 | empty/blank/NUL/invalid UTF-8 list, missing list/sample | InvalidData or NotFound; owned result empty; missing sample path and `line 2` diagnostic |
| PCM-08 | zero-length raw, unknown extension | InvalidData / UnsupportedFormat with failing input path |
| PCM-09 | 32 / 33 entries | all 32 slots correctly addressed / atomic InvalidData, no silent truncation |
| PCM-10 | 0x3fffc / 0x40000 / 0x40004 bytes; aggregate at limit | exact largest accepted bank / InvalidData; padding included in limit |
| PCM-11 | DATA with occupied slots 1 and 3 | sparse mapping retained; VOICE._3 remains slot 3; names/options/input unchanged |
| PCM-12 | DATA with all 32 slots | valid complete bank; slot 32 start/length 31/1 |
| PCM-13 | occupied VOICE missing or length mismatch | NotFound / InvalidData, path, empty result |
| PCM-14 | missing DATA; lengths 0/1023/1025; reversed addresses; no occupied entries | NotFound / InvalidData; no process cwd changes |
| PCM-15 | 64 silent frames at 16000 Hz, 16-bit mono/stereo | exact 32 ADPCM bytes of 0x08, length field 8 |
| PCM-16 | odd-size JUNK with pad; uppercase .WAV | same exact decoded bank; no temporary files |
| PCM-17 | truncation/magic/oversized RIFF or chunk/float/8-bit/3ch/zero rate/bad align or byteRate/partial frame/no data | classified errors and failing path; empty result and unchanged inventory |
| PCM-18 | Save to Unicode path replacing existing output | exact artifact bytes, owned artifact unchanged |
| PCM-19 | Save truncated artifact or out-of-range header | InvalidData; prior destination and directory inventory retained |
| PCM-20 | Save onto input/list, input symlink/hardlink, directory, absent parent | refuse source overwrite; retain inputs/sentinel; no partial output or implicit directories |
| PCM-21 | Save → native compiler with PCM channel | successful compile; embedded MUB PCM bytes exactly equal generated bank without requiring format-validator header |
| PCM-22 | empty/NUL input paths; 1 Hz expansion; 8000/44100 Hz silence | InvalidArgument; capacity rejection before decode allocation; independent resampling size/bytes checks |

`phase6_pcm_wave_contract_test` runs now against existing Adpcm. It checks
four mono/stereo/JUNK combinations against the independent silence oracle,
seven malformed/unsupported inputs, and corruption rejection by the test bank
oracle. It does not establish that the future builder is implemented.
`phase6_pcm_bank_contract_test` is now active and all PCM-01–22 cases pass.
The native compile round trip exposed a pre-existing unbounded half-width
kana table lookup for other Japanese UTF-8. The lookup now stops at the table
boundary. Compile service regressions cover non-kana and first/last kana.

Validation: all four GUI/CLI builds and full 40-test suites pass with 38 Pass,
2 Skip, 0 Fail. After PCM-22 and input-path guards were added, the affected
PCM/WAV/compile/CLI targets also pass in all four configurations.
Native GUI evidence covers mixed raw/WAV preview, DATA sparse slots 1/3,
preview/save-panel Cancel, Unicode output with exact byte inspection, missing
sample line/path, and successful GUI compile with the generated bank.
The remaining manual matrix is pending.

All test inputs and outputs use RAII temporary directories; filesystem reads
use explicit paths, and production process cwd must never change. Save failure
checks are deterministic (directory/missing parent) rather than unreliable
permission bits under privileged test users. Real disk-full/mid-write fault
injection and non-ASCII header names remain follow-up cases.

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

All eleven Phase 6 tests must be active and pass with the existing suite in
Release, Debug, ASan/UBSan, and TSan. The nine `GUI-TOOL`/`GUI-EXPORT` items in
`tests/manual/macos-gui-acceptance.md` must have real GUI evidence, including
Undo/Redo, save panel cancellation, progress cancellation, and errors. No
test-created output may remain in the source tree. Windows byte equality and
Windows loading are outside this phase's gate.
