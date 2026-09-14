#!/usr/bin/env bash
# Assemble this repository into a branch of a NEST checkout, ready to open as a
# pull request, and write the commit series out as patches.
#
# The files that go upstream are generated from the ones here rather than kept
# twice: the interface, the two examples and the test are copied, the NEST
# licence header is put on each, and the line saying the file is not part of
# NEST is taken off. Nothing is written to any NEST checkout you already have;
# the script clones one of its own.
#
# Usage:  upstream/assemble.sh [work-dir]
#
#   NEST_SOURCE  where to clone NEST from, default the local checkout
#   BRANCH       branch name to create, default cpp-api
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NEST_SOURCE="${NEST_SOURCE:-/home/magnus/Projects/nest/nest_master/nest-simulator}"
BRANCH="${BRANCH:-cpp-api}"
WORK="${1:-${HOME}/.cache/nest-cpp-pr}"
TREE="${WORK}/nest-simulator"

rm -rf "${WORK}"; mkdir -p "${WORK}"
git clone --quiet --no-hardlinks "${NEST_SOURCE}" "${TREE}"
cd "${TREE}"
git checkout --quiet -b "${BRANCH}"

# Copy one of our C++ files into the tree, with the NEST licence header on it.
install_source() {
  python3 "${HERE}/upstream/relicense.py" "$1" "$2"
}

echo "== 1/5 packaging"
git apply "${HERE}/packaging/install-kernel-library.patch"
git add -A
git commit --quiet -m "Install the kernel libraries, so C++ programs can link against NEST

An installed NEST ships the C++ headers of nestkernel, models and libnestutil
but none of the libraries, so the C++ API declared in nest.h cannot be linked
from outside the build tree. Install the three archives beside their headers and
add nest-config --kernel-libs, which names them in a linker group followed by
the external libraries. --libs is unchanged: an extension module is loaded into
a process that already holds the kernel and must not link it again."

echo "== 2/5 the interface"
install_source "${HERE}/include/nest/nest_api.h" "${TREE}/nestkernel/nest_api.h"
python3 - "${TREE}" <<'PY'
import pathlib, sys
tree = pathlib.Path(sys.argv[1])
p = tree / "nestkernel/CMakeLists.txt"
s = p.read_text()
assert "nest_api.h" not in s
old = "      nest.h nest_impl.h nest.cpp\n"
assert old in s, "the nestkernel source list has changed shape"
s = s.replace(old, old + "      nest_api.h\n", 1)
p.write_text(s)
PY
git add -A
git commit --quiet -m "Add nest_api.h, a C++ interface to the kernel API

nest.h declares the kernel API as free functions, and dictionary.h calls
Dictionary the type for the interface to Python and C++, but the only caller is
the Cython module: a C++ program has to manage init_nest and shutdown_nest by
hand, create nodes before it can give them parameters, know that slice_nc is
1-based and inclusive, and unpack an AnyVector to read one number back.

nest_api.h is a header-only layer in namespace nest::api that does those things
once. It adds no simulation behaviour: every call forwards to the kernel, so the
network is built in the same order and the same random numbers are drawn."

echo "== 3/5 the test"
install_source "${HERE}/tests/api_test.cpp" "${TREE}/testsuite/cpptests/nest_api_test.cpp"
python3 - "${TREE}" <<'PY'
import pathlib, sys
tree = pathlib.Path(sys.argv[1])
p = tree / "testsuite/cpptests/CMakeLists.txt"
s = p.read_text()
addition = '''
# The C++ API test drives a whole kernel, so it is its own executable rather
# than another suite inside run_all_cpptests, and it does not need Boost.
add_executable( run_nest_api_test nest_api_test.cpp )

target_link_libraries( run_nest_api_test nestkernel models OpenMP::OpenMP_CXX )

target_include_directories( run_nest_api_test PRIVATE
  ${PROJECT_SOURCE_DIR}/libnestutil
  ${PROJECT_BINARY_DIR}/libnestutil
  ${PROJECT_SOURCE_DIR}/models
  ${PROJECT_SOURCE_DIR}/nestkernel
  ${PROJECT_SOURCE_DIR}/thirdparty
  )

install( TARGETS run_nest_api_test RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} )
add_test( NAME nest_api_test COMMAND ${CMAKE_INSTALL_FULL_BINDIR}/run_nest_api_test )
'''
s = s.rstrip("\n") + "\n" + addition
p.write_text(s)
PY
git add -A
git commit --quiet -m "Test the C++ interface against a running kernel

run_nest_api_test creates nodes, reads and writes their status, builds
parameters and connections, simulates, and checks that the failures a caller can
provoke arrive as the NEST exception they should be. It is a plain program
rather than a Boost suite because it brings a kernel up, which the shared
run_all_cpptests binary does not."

echo "== 4/5 the examples"
install_source "${HERE}/brunel/brunel_alpha.cpp" "${TREE}/examples/cpp/brunel_alpha.cpp"
install_source "${HERE}/pd14/microcircuit.cpp" "${TREE}/examples/cpp/microcircuit.cpp"
install_source "${HERE}/pd14/microcircuit_params.hpp" "${TREE}/examples/cpp/microcircuit_params.h"
sed -i 's/microcircuit_params\.hpp/microcircuit_params.h/' "${TREE}/examples/cpp/microcircuit.cpp"
cp "${HERE}/upstream/examples_CMakeLists.txt" "${TREE}/examples/cpp/CMakeLists.txt"
cp "${HERE}/upstream/examples_README.md" "${TREE}/examples/cpp/README.md"
git add -A
git commit --quiet -m "Add C++ examples: the Brunel network and the microcircuit

Two of NEST's own PyNEST examples, written in C++ against nest_api.h and
building against an installed NEST through nest-config. Both produce spikes
identical to the Python they were translated from; the microcircuit reproduces
the reference data committed with the PyNEST version. Until now there was no C++
example anywhere in the tree."

echo "== 5/5 the documentation"
mkdir -p "${TREE}/doc/htmldoc/developer_space"
cp "${HERE}/upstream/cpp_api_doc.rst" "${TREE}/doc/htmldoc/developer_space/cpp_api.rst"
git add -A
git commit --quiet -m "Document how to drive NEST from C++

A page covering what nest_api.h offers, how to build against an installed NEST
with nest-config --kernel-libs, and which parts of the kernel API the interface
does not cover yet."

# Nothing in the generated tree may still point at this repository.
echo "== checking for stale references"
stale=0
for pattern in "nest_cpp" "pd14/" "brunel/check.sh" "docs/0" "build.sh" "this repository" "reference/"; do
  if grep -rn --fixed-strings "${pattern}" \
      "${TREE}/nestkernel/nest_api.h" "${TREE}/examples/cpp" "${TREE}/testsuite/cpptests/nest_api_test.cpp"; then
    echo "   stale reference: ${pattern}" >&2
    stale=1
  fi
done
[[ "${stale}" -eq 0 ]] || { echo "refusing to write patches with stale references" >&2; exit 1; }
echo "   none"

mkdir -p "${HERE}/upstream/patches"
rm -f "${HERE}/upstream/patches"/*.patch
git format-patch --quiet -o "${HERE}/upstream/patches" "$(git rev-parse HEAD~5)"..HEAD
echo
echo "branch ${BRANCH} in ${TREE}"
git log --oneline "$(git rev-parse HEAD~5)"..HEAD
echo
ls "${HERE}/upstream/patches"
