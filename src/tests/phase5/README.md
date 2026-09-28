# Phase 5 pre-implementation contract tests

This directory contains executable specifications for Phase 5. They were
registered before the production services existed. All activating production
headers are now present, so the five contract bodies are compiled and executed
in every normal and sanitizer build. The `77` fallback remains only so the
specifications can describe an older or partially applied source tree.

This arrangement keeps the existing regression suite green without weakening
the Phase 5 requirements or leaving non-compiling tests in the normal build.
A Phase 5 stage is not complete if its corresponding contract test fails or is
reported as skipped.

## Activation map

| CTest | Activating header | Planned implementation stage |
|---|---|---|
| `phase5_metadata_contract_test` | `editor/song_metadata.h` | 5-0 |
| `phase5_library_contract_test` | `editor/library_service.h` | 5-1 |
| `phase5_playlist_policy_contract_test` | `editor/playlist_service.h` | 5-3 |
| `phase5_presentation_contract_test` | `editor/playback_presentation.h` | 5-4 |
| `phase5_integration_contract_test` | all four headers | 5-5 |

The tests intentionally use the public platform-neutral API. AppKit classes,
`CMucom`, `PCHDATA`, process current-directory changes, sleeps used as clocks,
and real audio devices are not permitted in these contract tests.

## Metadata cases

| ID | Case | Required result |
|---|---|---|
| `META-01` | all supported tags | title, author, composer, date, voice, pcm, and comment are extracted |
| `META-02` | missing title | raw title stays empty and display title falls back to the file stem |
| `META-03` | duplicate tag | the first value wins, matching `CMucom::GetInfoBufferByName` |
| `META-04` | unknown tag and macro-like line | unknown values do not corrupt known metadata |
| `META-05` | LF, CRLF, CR, mixed newline | identical metadata is returned |
| `META-06` | UTF-8, UTF-8 BOM, CP932, forced Shift_JIS | decode and metadata values are preserved |
| `META-07` | uppercase tag name | it is not silently treated as the lowercase runtime tag |
| `META-08` | NUL or undecodable input | a structured per-file error is returned |
| `META-09` | file larger than 16 MiB | the entry remains listable but preview is rejected |
| `META-10` | empty and whitespace-only value | the stored value is empty after tag separator trimming |

## Library cases

| ID | Case | Required result |
|---|---|---|
| `LIB-01` | child directories, MUC, N88, unrelated files | directories and supported songs are the only visible entries |
| `LIB-02` | `.muc`, `.MUC`, `.n88`, `.N88` | extension matching is ASCII case-insensitive |
| `LIB-03` | mixed-case names | directories sort first, then songs; both groups have deterministic ordering |
| `LIB-04` | dot files and dot directories | hidden entries are omitted by the initial profile |
| `LIB-05` | parent of current directory | parent availability and Back target are correct |
| `LIB-06` | root directory | parent navigation is unavailable and does not escape root |
| `LIB-07` | unreadable or malformed song | only that entry carries `file_error`; the scan succeeds |
| `LIB-08` | directory symlink | it may be listed but is never recursively traversed by a scan |
| `LIB-09` | refresh supersedes slow scan | the old generation completion is ignored |
| `LIB-10` | cancellation during scan | completion occurs at most once and no partial result becomes current |
| `LIB-11` | load compile request | absolute source and resource directory are set without `chdir` |
| `LIB-12` | process current directory differs | scan, metadata, and request resolution remain unchanged |

## Playlist and policy cases

| ID | Case | Required result |
|---|---|---|
| `LIST-01` | start from sorted folder entries | only MUC entries are copied into an immutable queue |
| `LIST-02` | one finite song | natural finish advances according to loop policy without duplicate play |
| `LIST-03` | multiple successful songs | playback follows queue order and wraps exactly once at the end |
| `LIST-04` | one compile failure between successes | the failed entry is recorded and the next valid entry starts |
| `LIST-05` | all entries fail | one bounded pass is made, then playlist state becomes Failed |
| `LIST-06` | stale load or compile completion | generation, entry ID, and content ID mismatch are ignored |
| `LIST-07` | ready prefetch at natural end | next-song provider returns it without performing I/O or compile |
| `LIST-08` | prefetch not ready at natural end | Finished is retained, then `PlayCompiledSong` starts it on completion |
| `LIST-09` | Next and Previous | correct target is selected; Previous wraps only when loop is enabled |
| `LIST-10` | explicit Stop | provider is detached and pending load/compile is cancelled |
| `LIST-11` | Editor or Browser takes ownership | playlist cancels itself and never stops the new owner |
| `LIST-12` | folder rescan while playing | immutable active queue is unchanged |
| `POLICY-01` | default policy | automatic advance and loop are on, with 90 seconds and 150 percent |
| `POLICY-02` | 0 time or 0 percent | that individual threshold is disabled |
| `POLICY-03` | negative or over-limit value | InvalidArgument is returned and the previous policy remains active |
| `POLICY-04` | time threshold | exactly the configured accumulated Playing time advances |
| `POLICY-05` | paused/buffering/device-lost time | it is excluded from accumulated Playing time |
| `POLICY-06` | count threshold | 64-bit absolute-count comparison advances at the exact boundary |
| `POLICY-07` | both thresholds | the first reached reason is recorded once |
| `POLICY-08` | fast-forward | wall-time threshold remains real time; count threshold follows interrupts |
| `POLICY-09` | reconnect | elapsed time resets because playback restarts from the beginning |
| `POLICY-10` | zero/unknown max count | percent threshold is inert; time threshold can still advance |

## Presentation cases

| ID | Case | Required result |
|---|---|---|
| `PRES-01` | A through K snapshot | exactly 11 stable rows are produced in channel order |
| `PRES-02` | channel fields | mute, voice, volume, detune, address, key, LFO, reverb, pan, and quantize map without truncation |
| `PRES-03` | note codes | C through B, sharp notation, octave, and key-on state are deterministic |
| `PRES-04` | pan values | blank, R, L, C, and unknown value formatting are deterministic |
| `PRES-05` | count header | absolute, current, maximum, loop, driver, state, speed, and diagnostics are preserved |
| `PRES-06` | SessionId changes | rows from the previous song are never displayed for the new session |
| `PRES-07` | Idle or Preparing | stale rows are cleared to placeholders |
| `PRES-08` | immutable snapshots | retaining an older presentation does not change after later updates |
| `PRES-09` | 15 Hz throttle | a fake clock proves no more than one periodic fan-out per interval |
| `PRES-10` | immediate state/error event | it bypasses the periodic channel throttle without duplicating rows |
| `PRES-11` | identical snapshot pointer | redundant table reload is coalesced |
| `PRES-12` | subscribe/unsubscribe during dispatch | callbacks are delivered at most once and removed observers are not called |

## Integration and lifetime cases

| ID | Case | Required result |
|---|---|---|
| `INT-01` | Editor, Browser, Playlist play requests | application-wide active playback count never exceeds one |
| `INT-02` | ownership switch during prefetch | stale playlist completion cannot reclaim playback |
| `INT-03` | direct play | no document window/model is required after the immutable request is created |
| `INT-04` | MUB export | bytes equal the compiled song and cancellation leaves no destination or temporary file |
| `INT-05` | monitor subscribers open/close repeatedly | playback state and diagnostic counters are not modified by presentation consumers |
| `INT-06` | app shutdown with scan/prefetch active | playlist stops before Coordinator/compiler/audio without callback-after-free or deadlock |
| `INT-07` | provider invokes completion while Stop races | play-intent generation accepts at most one winner |
| `INT-08` | error isolation | one bad metadata file or compile entry does not terminate Home or the playlist service |

## Completion rule

Before Phase 5 can be marked complete, all five contract executables must be
active rather than skipped, every case above must pass in Release, Debug,
ASan/UBSan, and TSan, and the manual scenarios in
`tests/manual/macos-gui-acceptance.md` must also pass on the real CoreAudio
device.
