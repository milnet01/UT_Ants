#!/usr/bin/env bash
# Every standard function a file calls has its header included by that file,
# or by its paired header (Foo.cpp and Foo.h).
#
# Why: libstdc++ and libc++ reach <bit> and <algorithm> through other headers,
# and MSVC's library does not. So a missing include builds on both Linux legs
# and fails only on Windows -- and when the Windows test machine is
# unreachable, the push gate lets it go and GitHub is the first to object.
# Runs 36574630875 (std::bit_ceil) and 36575289692 (std::all_of) did exactly
# that. This check needs no compiler, so it catches the case on every leg.
#
# The list is FUNCTIONS, deliberately. A type such as std::size_t or
# std::string_view reaches a file through the project header whose API names
# it, which is legitimate and which MSVC builds; a function almost never does.
#
# Usage: scripts/std-includes.sh [file...]   (default: every tracked source)
set -euo pipefail

declare -A header=()
add() {
    local h=$1
    shift
    for name in "$@"; do header[$name]=$h; done
}
add bit bit_ceil bit_floor bit_width popcount countl_zero countl_one \
    countr_zero countr_one has_single_bit bit_cast rotl rotr byteswap
add algorithm all_of any_of none_of for_each find find_if find_if_not \
    find_end find_first_of adjacent_find count count_if mismatch equal \
    search copy copy_if copy_n copy_backward move_backward fill fill_n \
    transform generate generate_n remove remove_if remove_copy replace \
    replace_if unique reverse rotate shuffle sort stable_sort partial_sort \
    nth_element is_sorted lower_bound upper_bound equal_range binary_search \
    merge min_element max_element minmax_element minmax clamp \
    lexicographical_compare next_permutation partition stable_partition
add numeric accumulate reduce transform_reduce inner_product partial_sum \
    inclusive_scan exclusive_scan adjacent_difference iota gcd lcm midpoint
add cstring memcpy memmove memcmp memset memchr strlen strcmp strncmp strchr
add charconv from_chars to_chars
add format format format_to vformat
add memory make_unique make_shared

if [[ $# -gt 0 ]]; then
    files=("$@")
else
    mapfile -t files < <(git ls-files '*.cpp' '*.h' | grep -vE '^third_party/')
fi
if [[ ${#files[@]} -eq 0 ]]; then
    printf 'std-includes: no tracked sources found -- this checked nothing.\n' >&2
    exit 2
fi

includes() { grep -qE "^[[:space:]]*#[[:space:]]*include[[:space:]]*<$2>" "$1"; }

failures=0
for f in "${files[@]}"; do
    pair=
    [[ $f == *.cpp && -f ${f%.cpp}.h ]] && pair=${f%.cpp}.h
    # A name in a // comment is not a call; sed drops the comment first.
    while IFS= read -r name; do
        h=${header[$name]:-}
        [[ -z $h ]] && continue
        includes "$f" "$h" && continue
        [[ -n $pair ]] && includes "$pair" "$h" && continue
        printf '   %s uses std::%s but does not include <%s>\n' "$f" "$name" "$h" >&2
        failures=$((failures + 1))
    done < <(sed 's|//.*||' "$f" | grep -oE 'std::[a-z_0-9]+[[:space:]]*[(<]' |
        sed -E 's/^std::([a-z_0-9]+).*/\1/' | sort -u)
done

if [[ $failures -gt 0 ]]; then
    printf 'ci: %d standard function(s) used without their header -- MSVC will not find them.\n' "$failures" >&2
    exit 1
fi
printf '   %d source files, every listed standard function has its header.\n' "${#files[@]}"
