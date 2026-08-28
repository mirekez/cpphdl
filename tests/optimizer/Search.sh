#!/usr/bin/env bash

# Keep optimizer assertions usable on minimal build hosts.  ripgrep is faster
# when installed, but it is not a build dependency and GNU grep supports the
# extended regular expressions used by these tests.
if command -v rg >/dev/null 2>&1; then
    search_q()
    {
        rg -q "$@"
    }

    search_o()
    {
        rg -o "$@"
    }
else
    search_q()
    {
        grep -Eq "$@"
    }

    search_o()
    {
        grep -Eo "$@"
    }
fi
