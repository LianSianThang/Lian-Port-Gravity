# Fast progression testing

Checkpoint coverage shortens debugging; it does not establish a complete
fresh-save playthrough. A late save proves only that its state can load.

Catalog local saves with `python app/scripts/checkpoints.py`. Save metadata
currently includes Episode 1, Episode 2 (including completed), Episode 3 and
Episode 3 completed at 01:07:11. There are no known later checkpoints here.

The completed-Episode-3 copy was loaded in a separate candidate-17 process.
The home and manhole tutorials advanced and closed, the sewer/home gameplay
scene rendered, and Esc opened the populated map with the next story marker.
No fatal fault or rendering-scope recovery was found in that short run.
The process was then deliberately closed. This verifies checkpoint resume
and tutorial-to-gameplay/menu flow; Episode 4 itself has not been tested.
Evidence: local `app/logs/completed_episode3_transition_20261103.log`.

Make a separate replay profile before testing:

```powershell
python app/scripts/checkpoints.py --copy-profile app/user/controls10_replay_20261102
```

The tool copies savedata only, checks source/copy hashes and creates a unique
`app/user/replay_TIMESTAMP` folder with a manifest. Keep the launcher's main
profile unchanged. Use the copied profile as the runtime's `--user` argument.
For repeatable controller routes, the opt-in `SPARTAN_PAD_FILE` interface and
`app/scripts/controller_probe.py` record pulses. Mouse capture must be disabled
only in such controller-driven diagnostic runs; normal play keeps it enabled.

For each episode boundary, record:

| Check | Required evidence |
|---|---|
| Load | Correct checkpoint metadata and rendered scene |
| Transition | End-of-episode scene finishes; next controllable scene renders |
| Input | Movement, camera, gravity and required contextual action respond |
| Audio | Voices/music continue, with no decoder failure |
| Save/reload | New checkpoint saves and resumes in another process |
| Stability | No fatal fault, stuck fade or validation error during the route |

Record the runtime hash, isolated profile, route and logs for each result.
Absence of a crash log does not prove correct progression. A watchdog warning
alone does not prove a black screen. Mark untested boundaries as untested.
After checkpoint coverage, one continuous fresh-save playthrough through the
ending remains the final acceptance check. Do not ship diagnostic saves.
