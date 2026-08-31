# UGripper Runtime Refactor TODO

Temporary coordination note for the current maintenance pass. Remove or archive it after the approved items are completed.

## Fixed constraints

- Preserve runtime behavior, supervisor multiprocess topology, FIFO protocols, timing thresholds, error mapping, and episode schema.
- Do not modify any source under the pp-main parent repository; UGripper may only include/link its existing framework artifacts.
- Logger links `PP::utils` directly in pp-main/ARM builds.
- Reuse pp-main `DA::utils::SingleProcessInstance`.
- Long-term runtime namespace is `DA::Ugripper`.
- Public lifecycle/access APIs converge on pp-main-style `Initialize` / `Stop` / `Get` naming.
- Run host gates after each batch and ARM build/install/240 recording after meaningful integration batches.

## Completed

- [x] Split the former ~11k-line `record_runtime.cc` into responsibility-focused implementation files.
- [x] Keep each runtime implementation unit near or below 2k lines; episode validation is isolated.
- [x] Directly link real pp-main `PP::utils` on ARM and provide a source-identical host bridge.
- [x] Reuse pp-main single-process guard and harden invalid `--poll-ms` parsing.
- [x] Host build and `record_runtime` tests passed (`7/7`).
- [x] ARM package built and installed on 240; second-instance rejection passed.
- [x] 240 START/STOP produced `episode_20260831_0006`, online quality `success`; offline validation `WARN` only for existing start skew/Fays gaps.
- [x] Baseline commit: `2487a51 refactor(runtime): split domains and align pp-main utilities`.

## Approved follow-up items (1/3/4/5/6/7)

- [x] 1. Move device/recovery state ownership out of `RecordRuntime` into explicit state/coordinator components.
- [x] 3. Remove the `EpisodeManager -> RecordRuntime` friend/reverse dependency; pass narrow callbacks/data instead.
- [x] 4. Further split large support/episode-validation internals by cohesive helper responsibility, without fragmenting trivial code.
- [x] 5. Replace the oversized `RecordingOrchestrator::Dependencies` callback bag with smaller capability interfaces/ports.
- [x] 6. Normalize runtime configuration and CLI parsing boundaries while preserving accepted inputs and defaults.
- [ ] 7. Migrate runtime public code to `DA::Ugripper`; rename public lifecycle/access APIs to `Initialize` / `Stop` / `Get` style.

## Current execution order

1. [done] Parallel low-overlap extraction: device/recovery ownership, episode/support helper split, orchestrator ports, EpisodeManager dependency removal, CLI boundary.
2. [done] Integrate and pass host build/tests.
3. [next] Apply namespace and public API naming migration as one mechanical, reviewable batch.
4. Rebuild ARM package, install on 240, run second-instance and START/STOP recording gates, update docs/graph, and commit.
