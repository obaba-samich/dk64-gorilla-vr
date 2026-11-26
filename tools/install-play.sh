#!/usr/bin/env bash
# Puts a `dk64vr` command in ~/.local/bin that runs tools/play.sh from any folder.
set -euo pipefail
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$HOME/.local/bin"
ln -sfn "$repo/tools/play.sh" "$HOME/.local/bin/dk64vr"
echo "installed: $HOME/.local/bin/dk64vr -> $repo/tools/play.sh"
case ":$PATH:" in
    *":$HOME/.local/bin:"*) echo "run it with: dk64vr" ;;
    *) echo "add ~/.local/bin to your PATH, or run $HOME/.local/bin/dk64vr" ;;
esac
