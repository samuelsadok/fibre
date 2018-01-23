#!/bin/bash
set -euo pipefail

ln -sf "$(realpath lightctl.py)" /usr/bin/lightctl
cp systemd/* /etc/systemd/system/

#sudo systemctl stop lightd
cp build/lightd /usr/bin/

echo "Installation successful. You might want to:"
echo "  sudo systemctl enable lightd"
echo "  sudo systemctl start lightd"
echo "  sudo systemctl enable lights-off.timer"
echo "  sudo systemctl start light-off.timer"
