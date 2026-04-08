Import("env")
import os

print("=== esp32_webserver_path.py: running ===")

# $FRAMEWORK_DIR is the SCons variable PlatformIO sets to the Arduino framework root.
# Use env.subst() rather than get_package_dir() — more reliable cross-platform.
framework_dir = env.subst("$FRAMEWORK_DIR")

# Sanity-check: subst() returns the literal string when the variable is undefined
if not framework_dir or framework_dir.startswith("$"):
    # Fallback: PlatformIO Python API
    try:
        framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
        print(f"  Using PlatformIO API: {framework_dir}")
    except Exception as e:
        print(f"  PlatformIO API failed: {e}")
        framework_dir = None
else:
    print(f"  FRAMEWORK_DIR = {framework_dir}")

if framework_dir:
    webserver_src = os.path.join(framework_dir, "libraries", "WebServer", "src")
    if os.path.isdir(webserver_src):
        env.Append(CPPPATH=[webserver_src])
        print(f"  Added to CPPPATH: {webserver_src}")
    else:
        print(f"  WARNING: directory not found: {webserver_src}")
else:
    print("  ERROR: could not determine FRAMEWORK_DIR")

print("=== esp32_webserver_path.py: done ===")
