"""Create one private WPA2 key shared by the two firmware builds; never print it."""
from pathlib import Path
import secrets
root=Path(__file__).resolve().parents[1]
p=root/'private/pairing.h'
if p.exists():
 print('Existing private pairing retained.')
else:
 p.parent.mkdir(exist_ok=True)
 p.write_text('#pragma once\n#define VIBE_WIFI_SSID "StopWatch-C480"\n#define VIBE_WIFI_PASSWORD "'+secrets.token_hex(16)+'"\n')
 p.chmod(0o600)
 print('Created private pairing. Build and flash both endpoints with this same file.')
