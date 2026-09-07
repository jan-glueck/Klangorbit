# Third-party licenses

Klangorbit bundles third-party code and data for the Binaural (HRTF)
output format (`AmbisonicsDecoder::Mode::Binaural`, `Source/
BinauralDecoder.h/.cpp`, `Source/HrtfDataset.h/.cpp`). This file documents
each one's license and the exact attribution each requires, per that
feature's own requirement to check and document dataset licensing before
shipping it (see the CHANGELOG entry for the feature).

Everything else in this project is original code -- see the top-level
project files for the plugin's own licensing.

---

## libmysofa

**What it's used for:** parsing SOFA (AES69) HRTF files (`Source/
HrtfDataset.h/.cpp` wraps its C API). Vendored via CMake `FetchContent`,
pinned to `v1.3.5`, in `CMakeLists.txt`. Not bundled data -- source code,
compiled into the plugin binary (statically linked).

**Source:** <https://github.com/hoene/libmysofa>
**Copyright:** Copyright (c) 2016-2017, Symonics GmbH, Christian Hoene
**License:** BSD 3-Clause

```
BSD 3-Clause License

Copyright (c) 2016-2017, Symonics GmbH, Christian Hoene
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

3. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## MIT KEMAR HRTF measurements

**What it's used for:** the default bundled Binaural HRTF dataset
(`Assets/HRTF/kemar_44100.sofa`, embedded via `juce_add_binary_data` as
`BinaryData::kemar_44100_sofa`). SOFA-converted from MIT's original
measurement set.

**Source:** <https://sound.media.mit.edu/resources/KEMAR.html>
**Copyright:** Copyright 1994 by the MIT Media Laboratory
**Required attribution:** Bill Gardner and Keith Martin
**License terms (verbatim):**

> This data is Copyright 1994 by the MIT Media Laboratory. It is provided
> free with no restrictions on use, provided the authors are cited when
> the data is used in any research or commercial application.

**How Klangorbit satisfies this:** cited here, in the CHANGELOG entry for
the Binaural feature, and in-app (Output window's "HRTF Dataset" picker
labels this option "KEMAR (MIT Media Lab)", and the accompanying hint
text names Gardner & Martin).

---

## SADIE II HRTF database -- subject D1 (KU100)

**What it's used for:** an alternative bundled Binaural HRTF dataset
(`Assets/HRTF/sadie_d1_44100.sofa`, embedded via `juce_add_binary_data`
as `BinaryData::sadie_d1_44100_sofa`). Only the 44.1kHz SOFA variant of
subject D1 (a KU100 dummy-head measurement, not a human subject) is
bundled -- extracted from the full multi-sample-rate/multi-subject SADIE
II archive to keep the shipped size down.

**Source:** <https://www.york.ac.uk/sadie-project/database.html>
(database record: Zenodo, DOI 10.5281/zenodo.12092466)
**Copyright:** University of York
**License:** Apache License 2.0
**Recommended citation:** the database's own documentation asks that
academic use cite the associated Open Access paper, DOI
10.3390/app8112029 ("A Perceptual Evaluation of Individual and
Non-Individual HRTFs: A Case Study of the SADIE II Database").

The full Apache License 2.0 text is available at
<https://www.apache.org/licenses/LICENSE-2.0> and is not reproduced here
in full (it is long and standard) -- summary of the terms relevant to
this project's use: permissive, commercial-use-compatible, requires
retaining copyright/license notices in redistributed copies (satisfied by
this file) and does not require sharing this project's own source code.

**How Klangorbit satisfies this:** cited here and in the CHANGELOG entry
for the Binaural feature (with the DOI); the Output window's "HRTF
Dataset" picker labels this option "SADIE II -- D1, KU100 (University of
York)".

---

## Spherical Far Field HRIR Compilation of the Neumann KU 100 (TH Koeln)

**What it's used for:** an alternative bundled Binaural HRTF dataset
(`Assets/HRTF/ku100_48000.sofa`, embedded via `juce_add_binary_data` as
`BinaryData::ku100_48000_sofa`). The `HRIR_FULL2DEG.sofa` file from the
compilation below -- a dense, full-sphere 2-degree Gauss-Legendre grid
(16020 measurement points), natively 48kHz. Downloaded directly from the
Zenodo record (MD5 `aa48acb20c1fb8ff3d8de116107b73c2`, matching the
record's own published checksum); not resampled or otherwise modified.

**Source:** <https://zenodo.org/records/3928297>, DOI
10.5281/zenodo.3928297
**Author:** Benjamin Bernschütz (TH Köln, Institute of Communications
Engineering, Cologne, Germany; TU Berlin, Audio Communication Group,
Berlin, Germany)
**License:** CC BY 3.0, per the Zenodo record's own license field. (The
SOFA file's own embedded metadata additionally states "CC 3.0 BY-SA" --
noted here for completeness; Klangorbit follows the Zenodo record's
official license field as the authoritative one for redistribution
terms, and satisfies the stricter (ShareAlike/attribution) reading
either way, since this project's own source is open and this file is
redistributed unmodified with full attribution.)
**Recommended citation:** B. Bernschütz, "A Spherical Far Field HRIR /
HRTF Compilation of the Neumann KU 100," in *Proceedings of the 39th
DAGA*, 2013, pp. 592–595.

**How Klangorbit satisfies this:** cited here (with the DOI and the
recommended paper citation) and in the CHANGELOG entry that added this
dataset; the Output window's "HRTF Dataset" picker labels this option
"KU100 -- 2deg Grid (TH Koeln / Bernschuetz)".

---

## A note on custom SOFA files

The Binaural output format's "Custom SOFA file..." option
(`KlangorbitProcessor::loadCustomSofaFile()`) lets a user import their own
AES69/SOFA-format HRTF measurement. That data is never bundled with or
distributed by this project -- whatever license applies to a user-supplied
file is between the user and that file's own rights holder, entirely
outside this project's scope.
