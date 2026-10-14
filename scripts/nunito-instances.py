#!/usr/bin/env python3
# gob2native - make the static Nunito fonts in third_party/fonts/nunito from the variable one
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: scripts/nunito-instances.py 'Nunito[wght].ttf' third_party/fonts/nunito
#
# Nunito is published only as a variable font (github.com/googlefonts/nunito,
# fonts/variable/Nunito[wght].ttf) whose default instance is ExtraLight, and
# ImGui's rasterizer (stb_truetype) draws only the default instance. This
# writes Nunito-Regular.ttf (weight 400) and Nunito-Bold.ttf (700), subset
# to Latin, Latin-1, Latin Extended-A, Cyrillic and common punctuation,
# without hinting. Needs fontTools (pip install fonttools).

import sys

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

src, outdir = sys.argv[1], sys.argv[2]
for name, weight in [('Regular', 400), ('Bold', 700)]:
    font = instancer.instantiateVariableFont(TTFont(src), {'wght': weight}, updateFontNames=True)
    opts = subset.Options()
    opts.layout_features = ['kern']
    opts.name_IDs = ['*']
    opts.notdef_outline = True
    opts.hinting = False
    sub = subset.Subsetter(opts)
    sub.populate(unicodes=list(range(0x20, 0x7f)) + list(range(0xa0, 0x180)) + list(range(0x400, 0x460)) +
                 list(range(0x2010, 0x2028)) +
                 [0x2030, 0x2039, 0x203a, 0x20ac, 0x2122, 0x2190, 0x2192, 0x2193, 0x2713, 0x25b6])
    sub.subset(font)
    out = f'{outdir}/Nunito-{name}.ttf'
    font.save(out)
    print(out, font['name'].getDebugName(4))
