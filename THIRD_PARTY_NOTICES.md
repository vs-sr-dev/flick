# Third-party components

## Ported from MAME

Several source files are C ports of MAME code (https://github.com/mamedev/mame),
distributed under the **BSD-3-Clause** license. They keep their original license and
copyright headers:

| File | MAME source | Copyright holders |
|-|-|-|
| `src/score7.c` | `src/devices/cpu/score/score.cpp`, `scorem.h` | Sandro Ronco (rotate instructions added in a local fork) |
| `src/ppu.c` | `src/devices/machine/spg290_ppu.cpp` | Sandro Ronco |
| `src/timer.c` | `src/devices/machine/spg290_timer.cpp`, driver `spg29x.cpp` | Sandro Ronco |
| `src/i2c.c` | `src/devices/machine/spg290_i2c.cpp`, `src/mame/tvgames/hyperscan_ctrl.cpp` | Sandro Ronco |
| `src/screen.c` | timing of `src/emu/screen.cpp`; TV registers of driver `spg29x.cpp` | Aaron Giles, Sandro Ronco |
| `src/machine.c` | memory map of driver `src/mame/tvgames/spg29x.cpp` | Sandro Ronco |
| `src/cdservo.c` | `src/devices/machine/spg290_cdservo.cpp` | Sandro Ronco |
| `src/cdimg.c` | sector layout of `src/lib/util/cdrom.cpp` (reimplemented for single-file cue/iso) | Aaron Giles, R. Belmont |
| `src/spu.c` | `src/devices/machine/spg290_spu.cpp` of the HS-LBA MAME fork; IMA ADPCM from `src/devices/sound/imaadpcm.cpp` | Sandro Ronco, ROG; Andrew Gardner, Aaron Giles |

The scheduler (`src/sched.c`) reimplements the timing rules of MAME's
`device_scheduler` (Aaron Giles) so that events land on the same instruction; it contains
no copied code.

BSD-3-Clause license text:

    Redistribution and use in source and binary forms, with or without modification, are
    permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this list of
       conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice, this list
       of conditions and the following disclaimer in the documentation and/or other
       materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors may be
       used to endorse or promote products derived from this software without specific
       prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
    EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
    MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
    THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
    OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
    STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
    THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## Linked at build time (not included)

- **SDL2** - zlib license (optional; the headless build needs nothing else).
