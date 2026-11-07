# PlatformIO post-build script: pass OTA_PASSWORD from include/secrets.h to the espota uploader,
# so the OTA password is defined in exactly one place (the same one the firmware is built from).
#
# Used by [env:nodemcu-32s-ota] in platformio.ini via:  extra_scripts = post:scripts/ota_auth.py

import os
import re

Import("env")  # noqa: F821 - provided by PlatformIO/SCons

secrets_path = os.path.join(env.subst("$PROJECT_DIR"), "include", "secrets.h")  # noqa: F821

try:
    with open(secrets_path, encoding="utf-8") as f:
        match = re.search(r'^\s*#define\s+OTA_PASSWORD\s+"([^"]*)"', f.read(), re.MULTILINE)
except FileNotFoundError:
    match = None

if match and match.group(1):
    password = match.group(1)
    if not re.fullmatch(r"[A-Za-z0-9]+", password):
        print("ota_auth.py: OTA_PASSWORD in include/secrets.h must be letters and digits only")
        env.Exit(1)  # noqa: F821
    env.Append(UPLOADERFLAGS=["--auth=" + password])  # noqa: F821
else:
    print("ota_auth.py: no OTA_PASSWORD found in include/secrets.h - uploading without a password")
