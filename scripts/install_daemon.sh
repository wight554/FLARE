#!/bin/bash
# FLARE Background Daemon Systemd Service Installer
set -e

# Colors for terminal output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0;68m'
NC='\033[0m' # No Color

echo -e "${BLUE}==================================================${NC}"
echo -e "${GREEN}      FLARE Background Daemon Installer            ${NC}"
echo -e "${BLUE}==================================================${NC}"

# Parse flags
NO_KLIPPER=false
for arg in "$@"; do
    if [ "$arg" == "--no-klipper" ]; then
        NO_KLIPPER=true
    fi
done

# Ensure script is run with sudo/root privileges
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: Please run this script with sudo or as root:${NC}"
    echo -e "  sudo bash $0 $*"
    exit 1
fi

# Resolve the non-root user that invoked sudo
REAL_USER=${SUDO_USER:-$(whoami)}
if [ "$REAL_USER" == "root" ]; then
    REAL_USER="pi" # Fallback guess for Raspberry Pi / standard Klipper images
fi

# Resolve project root directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo -e "Installing under user: ${YELLOW}${REAL_USER}${NC}"
echo -e "Project path:         ${YELLOW}${PROJECT_DIR}${NC}"

# 1. Install dependencies
echo -e "\n${BLUE}[1/4] Installing Python dependencies...${NC}"
if ! python3 -c "import serial" &>/dev/null; then
    echo -e "Installing 'pyserial' package..."
    # Attempt apt install first to avoid PEP 668 externally-managed environment blocks
    if command -v apt-get &>/dev/null; then
        apt-get update -y && apt-get install -y python3-serial || pip3 install pyserial --break-system-packages || pip3 install pyserial
    else
        pip3 install pyserial || true
    fi
else
    echo -e "${GREEN}pyserial is already installed.${NC}"
fi

# 2. Configure and copy systemd service file
echo -e "\n${BLUE}[2/4] Setting up systemd unit file...${NC}"
SERVICE_TEMPLATE="${SCRIPT_DIR}/flare_daemon.service"
SERVICE_DEST="/etc/systemd/system/flare_daemon.service"

if [ ! -f "$SERVICE_TEMPLATE" ]; then
    echo -e "${RED}Error: Service template not found at $SERVICE_TEMPLATE${NC}"
    exit 1
fi

# Generate customized service file
sed -e "s|{{USER}}|${REAL_USER}|g" \
    -e "s|{{DIR}}|${PROJECT_DIR}|g" \
    "$SERVICE_TEMPLATE" > "$SERVICE_DEST"

chmod 644 "$SERVICE_DEST"
echo -e "${GREEN}Systemd service file installed to $SERVICE_DEST${NC}"

# 3. Enable and start service
echo -e "\n${BLUE}[3/4] Enabling and starting flare_daemon...${NC}"
systemctl daemon-reload
systemctl enable flare_daemon.service
systemctl restart flare_daemon.service

# Verify service is active
if systemctl is-active --quiet flare_daemon.service; then
    echo -e "${GREEN}FLARE daemon is active and running successfully!${NC}"
else
    echo -e "${RED}Warning: FLARE daemon service failed to start cleanly.${NC}"
    echo -e "Check logs with: journalctl -u flare_daemon.service -n 50"
fi

# 4. Integrate Klipper settings
echo -e "\n${BLUE}[4/4] Finalizing integration...${NC}"
if [ "$NO_KLIPPER" = true ]; then
    echo -e "${YELLOW}Standalone Mode (--no-klipper) active.${NC}"
    echo -e "Skipping Klipper & Moonraker configurations."
else
    echo -e "${GREEN}Klipper Mode active.${NC}"
    
    # Install native MMU Mock extra for Mainsail/Fluidd
    KLIPPER_EXTRAS_DIR="/home/${REAL_USER}/klipper/klippy/extras"
    if [ ! -d "$KLIPPER_EXTRAS_DIR" ]; then
        if [ -d "/home/pi/klipper/klippy/extras" ]; then
            KLIPPER_EXTRAS_DIR="/home/pi/klipper/klippy/extras"
        elif [ -d "/home/klipper/klipper/klippy/extras" ]; then
            KLIPPER_EXTRAS_DIR="/home/klipper/klipper/klippy/extras"
        fi
    fi

    if [ -d "$KLIPPER_EXTRAS_DIR" ]; then
        TARGET_MMU="$KLIPPER_EXTRAS_DIR/mmu.py"
        if [ -f "$TARGET_MMU" ]; then
            if grep -q "FLARE MMU Mock" "$TARGET_MMU"; then
                echo -e "Found existing FLARE MMU Mock at $TARGET_MMU. Updating to latest version."
                cp "${PROJECT_DIR}/klipper/mmu.py" "$TARGET_MMU"
                chown "${REAL_USER}:${REAL_USER}" "$TARGET_MMU" || true
            else
                echo -e "${YELLOW}Warning: A custom mmu.py already exists at $TARGET_MMU but does NOT contain 'FLARE MMU Mock' (e.g. Happy Hare). Preserving it to avoid conflicts.${NC}"
            fi
        else
            echo -e "Installing FLARE MMU Mock to $TARGET_MMU"
            cp "${PROJECT_DIR}/klipper/mmu.py" "$TARGET_MMU"
            chown "${REAL_USER}:${REAL_USER}" "$TARGET_MMU" || true
        fi
    else
        echo -e "${YELLOW}Warning: Klipper extras directory not found. Skipping mmu.py installation.${NC}"
        echo -e "If Klipper is installed in a non-standard location, please copy 'klipper/mmu.py' to your 'klippy/extras/' directory manually."
    fi

    echo -e "Ensure Klipper config includes flare macro file."
    echo -e "Dashboard will be served on http://localhost:8088"
fi

echo -e "\n${GREEN}==================================================${NC}"
echo -e "${GREEN}             FLARE Service Ready!                 ${NC}"
echo -e "  - WebUI URL:     http://localhost:8088"
echo -e "  - View Logs:     journalctl -u flare_daemon.service -f"
echo -e "  - Start Service: systemctl start flare_daemon.service"
echo -e "  - Stop Service:  systemctl stop flare_daemon.service"
echo -e "${GREEN}==================================================${NC}"
