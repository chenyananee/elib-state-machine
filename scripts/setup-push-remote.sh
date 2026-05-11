#!/bin/bash
# Setup dual push remotes (GitHub + Gitee) for current repository

REPO_NAME=$(basename "$(git rev-parse --show-toplevel)")
GITHUB_URL="https://github.com/chenyananee/${REPO_NAME}.git"
GITEE_URL="https://gitee.com/chenyananee/${REPO_NAME}.git"

git remote set-url --add --push origin "$GITEE_URL"
git remote set-url --add --push origin "$GITHUB_URL"

echo "Dual push remotes configured:"
git remote -v | grep push
