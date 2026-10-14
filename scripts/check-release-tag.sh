#!/usr/bin/env bash
set -euo pipefail

fail() { printf '%s\n' "$*" >&2; exit 1; }
[[ $# == 2 ]] || fail 'Usage: bash scripts/check-release-tag.sh TAG COMMIT'
tag=$1
number='(0|[1-9][0-9]*)'
[[ "$tag" =~ ^v${number}\.${number}\.${number}$ ]] || fail 'Expected a stable tag vMAJOR.MINOR.PATCH without leading zeroes.'

commit=$(git rev-parse --verify --end-of-options "$2^{commit}")
tag_commit=$(git rev-parse --verify "refs/tags/$tag^{commit}")
[[ "$tag_commit" == "$commit" ]] || fail 'Release tag no longer points to the checked commit.'
version=$(git show "$commit:VERSION")
[[ "$tag" == "v$version" ]] || fail 'Release tag disagrees with VERSION in the tagged commit.'
git merge-base --is-ancestor "$commit" refs/remotes/origin/main || fail 'Release commit must belong to main history.'
printf 'Validated %s at %s\n' "$tag" "$commit"
