# tracktion_engine — Ondinha fork

This is a fork of [Tracktion Engine](https://github.com/Tracktion/tracktion_engine), the C++ / JUCE library for building
sequencers and DAWs. It is maintained for [Ondinha](https://github.com/ondinha), a DAW with mixing tools built in.

The fork is upstream plus a short list of small, targeted fixes. Each fix is also proposed upstream, so the
list should shrink over time. If you aren't building Ondinha, you probably want
[the original](https://github.com/Tracktion/tracktion_engine).

## Branches

| Branch | What it is |
| --- | --- |
| `ondinha` | **The branch to use.** Upstream `develop` plus the patches below, rebased on each upstream update. |
| `develop`, `master` | Untouched mirrors of upstream. |

Because `ondinha` is rebased, its history is rewritten. Pin a commit (as a git submodule does), not the branch tip.

## Using it

```sh
git submodule add -b ondinha https://github.com/ondinha/tracktion_engine.git external/tracktion_engine
git submodule update --init --recursive
```

```cmake
add_subdirectory(external/tracktion_engine/modules/juce)
add_subdirectory(external/tracktion_engine/modules)
target_link_libraries(MyApp PRIVATE tracktion::tracktion_engine)
```

The API is upstream's, and the [upstream documentation](https://tracktion.github.io/tracktion_engine/),
[examples](../examples) and [tutorials](../tutorials) all apply.

## Patches

| # | Area | Fix | Upstream PR |
| --- | --- | --- | --- |
| — | — | *none yet: `ondinha` is currently identical to upstream 5ee811e (2026-10-01)* | — |

Planned, in priority order:
- **Side-chain:** external VST3 compressors that ignore their side-chain key when hosted in the engine but work when hosted directly in JUCE (FabFilter Pro-C 2).
- **Track outputs:** saved by track ID instead of track number, so routing survives adding and removing tracks, and undo.
- **Parameters:** live plug-in parameters follow state changes and undo/redo; volume, pan and send setters are undoable.
- **Built-in synths:** an optional deterministic voice phase, so renders are bit-identical.
- **Rendering:** a per-plug-in "keep real-time during render" flag; automation put back on its curves after a render.

## Contributing a patch

1. Branch from `ondinha`, keep one fix per commit, and prefix the message with the area (`Side-chain: ...`, matching upstream's style).
2. Include a test or a reproduction in the commit message.
3. Open the PR here against `ondinha`, and open the same change against `Tracktion/tracktion_engine` `develop`.
4. Add a row to the table above with the upstream PR link.

## Updating from upstream

```sh
git fetch upstream            # upstream = https://github.com/Tracktion/tracktion_engine.git
git rebase upstream/develop ondinha
git push --force-with-lease origin ondinha
```

Drop any patch that upstream has merged.

## License

Same as upstream: Tracktion Engine is dual-licensed under **GPLv3 (or later)** or a
[commercial licence](https://www.tracktion.com/develop/tracktion-engine) from Tracktion. JUCE (in `modules/juce`)
is dual-licensed under **AGPLv3** or a commercial JUCE licence. See [LICENSE.md](../LICENSE.md). This fork's patches
are under the same terms. Forking does not change the licence.

Tracktion Engine is © Tracktion Corporation. This fork is not affiliated with or endorsed by Tracktion.
