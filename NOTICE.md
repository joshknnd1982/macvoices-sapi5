# Notices

The code written for this project is licensed under the MIT License (see [LICENSE](LICENSE)). The material
below is not covered by that licence and stays under its own terms.

## Apple's speech engine and voices

The voices are Apple's, from Mac OS X 10.5 Leopard, and so is the speech engine that runs them. The README says:

> This repository is **source code only**. Apple's speech engine and voice files are not
> here and never will be.

The setup installer on the Releases page installs them. They remain Apple's and are not covered by the MIT
License.

## panthera NVDA add-on family

The README credits the panthera NVDA add-on family for the engine host and the measured voice behaviour. That
project is [panthera-speech](https://github.com/tgeczy/panthera-speech) by Tamas Geczy, which is published under
the MIT License:

```
MIT License

Copyright (c) 2026 Tamas Geczy

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

That notice applies to what this project took from the add-on:

- `src/cmv_textproc.cpp` and `src/cmv_textproc.hpp` are, in the words of the header, a faithful C++ port of the
  Python that shipped with the add-on (`pantheranumbers.py`, `pantheraabbrev.py`, `pantherastress.py`, and the
  folding and splitting in `pantheradriver.py`); `tools/cmv_textproc_test.cpp` checks the port against outputs
  recorded from that Python.
- The per-voice loudness table in `src/cmv_voices.cpp` and `tools/render_samples.py`, the phrasing thresholds in
  `src/cmv_host.cpp` and the framed protocol in `src/cmv_host.hpp` follow the add-on's driver
  (`pantheradriver.py`).

The engine host, `panthera_host.exe`, is not part of this repository; it comes from that add-on family and is not
covered by the MIT License of this project.

## Source files taken from other projects

Some files in this repository are copies of, or adapted from, files in two SAPI 5 projects published on GitHub by
gozaltech: [espeak-ng-sapi](https://github.com/gozaltech/espeak-ng-sapi), which is published under the GPL-3.0,
and [BstSpeech-sapi](https://github.com/gozaltech/BstSpeech-sapi), which has no licence file. They are not covered
by the MIT License of this project and stay under their author's terms.

Copied, with renamed identifiers and small changes:

- `src/cmv_com.hpp` and `src/cmv_com.cpp`
- `src/cmv_datakey.hpp` and `src/cmv_datakey.cpp`
- `src/cmv_registry.hpp`
- `src/cmv_utils.hpp`

Adapted from the file of the same role in those projects:

- `src/cmv_registry.cpp`
- `src/cmv_enum_tokens.hpp` and `src/cmv_enum_tokens.cpp`
- `src/cmv_token.hpp` and `src/cmv_token.cpp`
- `src/cmv_engine.hpp`
- `src/cmv_main.cpp`
