#!/bin/bash
# Zombie Shooter Installer
# One-liner: curl -sSL https://raw.githubusercontent.com/xGODFATHERxHBRx/zombie-shooter/main/install.sh | bash

set -e

echo "🧟 Installing Zombie Shooter..."

# Download the .deb package
wget -O /tmp/zombie-shooter.deb https://raw.githubusercontent.com/xGODFATHERxHBRx/Zombie-shooter-in-the-terminal-/main/zombie-shooter_1.0_amd64.deb

# Intall it
sudo apt install /tmp/zombie-shooter.deb -y

# Clean up
rm /tmp/zombie-shooter.deb

echo "✅ Zombie Shooter installed successfully!"
echo "Type 'zombie-shooter' to play."
