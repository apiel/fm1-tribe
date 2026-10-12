#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the GitHub Pages site for release VERSION (tools/make_pages.py) and push it to the gh-pages
# branch of origin, replacing what was there (the branch holds only the generated site).
# GitHub: Settings > Pages > Build and deployment > Deploy from a branch > gh-pages, folder / (root).
#   needs: emcc (source ~/emsdk/emsdk_env.sh), the release package (below), and the screenshots (tests/run_tests.sh, then tools/gen_manual_figures.py)
#   tools/publish_pages.sh 0.1-beta          (after ./build.sh --release 0.1-beta)
set -e
cd "$(dirname "$0")/.."
V="${1:?usage: tools/publish_pages.sh VERSION}"
PKG="build/tribe-$V.fwsc"
[ -f "$PKG" ] || { echo "no $PKG: run ./build.sh --release $V first"; exit 1; }
sh web/emu/build.sh
python3 tools/make_pages.py "$PKG" "$V" build/pages
REMOTE="$(git remote get-url origin)"
SHA="$(git rev-parse --short HEAD)"
cd build/pages
rm -rf .git
git init -q -b gh-pages
git add -A
git commit -q -m "Site for TRIBE $V (from $SHA)"
git push -q -f "$REMOTE" gh-pages
rm -rf .git
# ask GitHub Pages to build it now: a push alone has left the first deploy stuck on "building"
REPO="$(printf %s "$REMOTE" | sed -E 's#(git@github.com:|https://github.com/)##; s#\.git$##')"
gh api -X POST "repos/$REPO/pages/builds" >/dev/null 2>&1 || echo "(could not ask GitHub Pages to build: check the repository's Pages settings)"
echo "pushed the site for $V to gh-pages"
