# Attribution and license notices

Lian Port — Gravity Rush is a hobby project based on the upstream work below.
Credit does not imply endorsement. Existing copyright/SPDX headers and component
licenses remain authoritative and must be preserved when redistributing source
or builds. The GPL version 2 license text is retained in [LICENSE](LICENSE).

Files without an individual header are covered by this repository's GPL-2.0 license; upstream files retain the notices they originally carried.

| Upstream | Credit and use | License / retained notice |
|---|---|---|
| [SpartanPort / formerly gow3_pc](https://github.com/FelipePrado0/SpartanPort) | Felipe Prado and contributors; runtime, loader and renderer foundation | GNU GPL version 2; [retained upstream LICENSE](runtime/LICENSE). The original snapshot revision was not retained; this notice does not replace individual file terms. |
| [shadPS4](https://github.com/shadps4-emu/shadPS4) | shadPS4 Emulator Project and contributors; adapted video core, shader compiler and symbols | GPL-2.0-or-later as stated in source SPDX headers; [retained upstream GPL v2 text](runtime/gpu/shadps4/LICENSE). Individual third-party file terms remain applicable. |
| [gminustwo/minustwo](https://github.com/gminustwo/minustwo/tree/6d6ebce1346fa5f2f532a0ff3cd68321c13bbc0c) | gminustwo and contributors; adapted NGS2 audio mixer | GNU GPL version 2; adapted mixer header specifies GPL-2.0-or-later. See [NGS2 provenance](runtime/NGS2_UPSTREAM.md) and [GPL text](LICENSE). |
| [LucaPazienza/gravityrush_pc](https://github.com/LucaPazienza/gravityrush_pc/tree/9108d33eeeb25910ce484a64df535028fe8824e0) | LucaPazienza and contributors; rendering-scope guard adaptation | GPL-2.0-or-later; [adaptation and provenance](runtime/GRAVITY_RUNTIME_UPSTREAM.md), [GPL text](LICENSE). |
| [FreeType](https://freetype.org/) | David Turner, Robert Wilhelm, Werner Lemberg and contributors; font rendering | Dual-licensed under the FreeType Project License (FTL) or GNU GPL version 2. For GPL v2 distribution, use its GPL v2 option: FreeType explicitly states that FTL alone is incompatible with GPL v2. See [official license explanation](https://freetype.org/license.html). Retain FreeType's supplied GPLv2.TXT and other applicable notices in binary distributions. |

The original foundation license files are already retained at the paths above;
no replacement license text or new terms are introduced here. Additional component
credits and notices are listed in [REFERENCES.md](REFERENCES.md) and
[DEPENDENCIES.md](DEPENDENCIES.md). This notice is not a full dependency-license
or binary-distribution compliance certification.
