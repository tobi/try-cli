#!/bin/bash
# Fetch upstream specs from try repo
# Creates .try/ clone and symlinks upstream -> .try/spec
# If upstream exists (e.g., local override symlink), does nothing

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# If upstream already exists, nothing to do (allows local override)
if [ -e "upstream" ]; then
    exit 0
fi

# Clone if .try doesn't exist
if [ ! -d ".try" ]; then
    echo "Cloning try spec repo..."
    git clone https://github.com/tobi/try .try
fi

# Pull latest
echo "Updating specs..."
cd .try && git pull

# Create symlink
cd "$SCRIPT_DIR"
ln -s .try/spec upstream
echo "Linked upstream -> .try/spec"
