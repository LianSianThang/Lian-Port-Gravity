# Gravity Rush runtime reference attribution

The execution-state rendering-scope guard in `vk_scheduler.cpp` and
`vk_render_scope.h` is adapted from
[LucaPazienza/gravityrush_pc](https://github.com/LucaPazienza/gravityrush_pc),
revision `9108d33eeeb25910ce484a64df535028fe8824e0`, retrieved 2026-11-03.
Its GPL-2.0-or-later license is compatible with this project's license.
Preserve this attribution and the shadPS4 file headers when redistributing.

The upstream scheduler documents an NVIDIA `vkCmdEndRendering` null+0x130
fault, matching the local candidate-13 reproduction. It tracks scopes as
commands execute, drops unmatched ends, closes a previous scope before a
replacement begin, and closes any remaining scope before command-buffer end.
The local adaptation extracts that state into a shared, tested helper and
logs bounded recovery messages. This prevents invalid scope sequences from
being forwarded; a recovery message still indicates a sequencing issue.

The presenter drain added locally is also present in this reference. Its
effect on the local failure remains under investigation. Upstream's full-story
playthrough claim applies to CUSA01113 and is not verification of CUSA01130.

Reference snapshots, commit metadata and upstream LICENSE are preserved under
the sibling `references/reference_gravityrush_pc` folder outside the
`Lian-Port-Gravity` repository. No upstream executable or game data was downloaded.
