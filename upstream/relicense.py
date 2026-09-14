#!/usr/bin/env python3
"""Copy one of this repository's C++ files into a NEST checkout.

The NEST licence header goes on the front, the file's own leading comment block
is kept after it, and the line saying the file is not part of NEST is dropped,
since in the destination it is.

Usage:  relicense.py <source> <destination>
"""
import pathlib
import sys

LICENCE = """/*
 *  {name}
 *
 *  This file is part of NEST.
 *
 *  Copyright (C) 2004 The NEST Initiative
 *
 *  NEST is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  NEST is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with NEST.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

"""

# This repository's comments point at this repository. In the NEST tree they
# have to point at NEST. Anything left over is caught by the guard in
# assemble.sh, which fails rather than shipping a stale path.
REPLACEMENTS = [
    ("nest_cpp/nest.hpp", "nest_api.h"),
    ("microcircuit_params.hpp", "microcircuit_params.h"),
    ("docs/02_the_api.md", "doc/htmldoc/developer_space/cpp_api.rst"),
    (
        "Scope is what the models in this repository need, and nothing speculative.\n"
        "Every entry point here exists because a model directory uses it, and the\n"
        "check for that model pins its behaviour. See doc/htmldoc/developer_space/cpp_api.rst for what is\n"
        "missing and why that is the right size.",
        "Scope is what the examples in examples/cpp need, and nothing speculative.\n"
        "See doc/htmldoc/developer_space/cpp_api.rst for what is not covered and\n"
        "why that is the right size.",
    ),
    ("Requires C++20. Link against a NEST build tree; see build.sh.", "Requires C++20."),
    (
        "Compare with brunel_alpha_raw.cpp, which is the same network\n"
        "written against the kernel API directly, and with\n"
        "brunel_alpha_ref.py, which is the upstream Python example.\n"
        "All three build the same network in the same order, so they draw the same\n"
        "random numbers and must produce the same spikes; brunel/check.sh enforces\n"
        "that.",
        "The C++ counterpart of pynest/examples/brunel_alpha_nest.py. Both build the\n"
        "same network in the same order, so they draw the same random numbers and\n"
        "produce the same spikes, byte for byte.",
    ),
    (
        "Compare with reference/, which is the upstream PyNEST implementation of the\n"
        "same model. Both build the same network in the same order with the same\n"
        "parameters, so they draw the same random numbers and must produce the same\n"
        "spikes; pd14/check.sh enforces that.",
        "The C++ counterpart of the PyNEST microcircuit. Both build the same network\n"
        "in the same order with the same parameters, so they draw the same random\n"
        "numbers and produce the same spikes: this program reproduces the files in\n"
        "the PyNEST version's reference_data directory exactly.",
    ),
    ("the sequence has to follow reference/network.py statement for statement", "the sequence has to follow the PyNEST version's network.py statement for statement"),
    ("reference/run_ref.py --derived uses", "the PyNEST version's runner prints"),
    ("as reference/network.py writes them", "as the PyNEST version's network.py writes them"),
    (
        "They are part of what pd14/check.sh compares: identical IDs mean the two\n"
        "  // programs created the same nodes in the same order.",
        "Identical IDs mean the two programs created the same nodes in the same\n"
        "  // order.",
    ),
    (
        "Every value and every formula is taken from the PyNEST microcircuit example\n"
        "vendored in reference/ (see reference/PROVENANCE.md).",
        "Every value and every formula is taken from the PyNEST microcircuit example.",
    ),
    (
        "The two model directories check that the interface builds the right network.\n"
        "This checks the interface itself:",
        "The examples in examples/cpp check that the interface builds the right\n"
        "network. This checks the interface itself:",
    ),
]

source = pathlib.Path(sys.argv[1])
destination = pathlib.Path(sys.argv[2])
lines = source.read_text().splitlines()

if lines[0].strip() != "/*":
    raise SystemExit(f"{source}: expected a leading comment block")
end = next(i for i, line in enumerate(lines) if i > 0 and line.strip() == "*/")

# The file's own description, minus its first line (the old file name), minus
# the sentence that only makes sense outside the NEST tree.
description = []
for line in lines[1:end]:
    text = line[4:] if line.startswith(" *  ") else line.lstrip(" *")
    if "not part of NEST" in text:
        continue
    description.append(text.rstrip())
description = description[1:]  # the file name line, replaced by the one above
while description and not description[0]:
    description.pop(0)
while description and not description[-1]:
    description.pop()
# Removing a line can leave two blank comment lines where there was one.
collapsed = []
for line in description:
    if line or (collapsed and collapsed[-1]):
        collapsed.append(line)
description = collapsed

# Once on the description as plain text, where the multi-line phrases match,
# and once on the finished file, for the comments inside the code.
joined = "\n".join(description)
for old, new in REPLACEMENTS:
    joined = joined.replace(old, new)
description = joined.split("\n")

body = ["/*"] + [f" *  {t}" if t else " *" for t in description] + [" */", ""]
destination.parent.mkdir(parents=True, exist_ok=True)
text = LICENCE.format(name=destination.name) + "\n".join(body) + "\n" + "\n".join(lines[end + 1 :]).lstrip("\n") + "\n"
for old, new in REPLACEMENTS:
    text = text.replace(old, new)
destination.write_text(text)
