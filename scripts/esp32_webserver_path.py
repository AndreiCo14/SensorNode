Import("env")
import os

print("=== esp32_webserver_path.py: running ===")

# env.subst("$FRAMEWORK_DIR") may not be set yet in pre: scripts;
# fall back to the PlatformIO package API.
framework_dir = env.subst("$FRAMEWORK_DIR")
if not framework_dir or framework_dir.startswith("$"):
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
        # Use CCFLAGS, NOT CPPPATH.
        # The espressif32 platform builder replaces CPPPATH after pre: scripts run,
        # so CPPPATH additions are silently discarded.  CCFLAGS is never replaced,
        # so the -I flag survives into the actual compiler invocation.
        flag = "-I" + webserver_src.replace("\\", "/")
        env.Append(CCFLAGS=[flag])
        print(f"  Added to CCFLAGS: {flag}")
    else:
        print(f"  WARNING: directory not found: {webserver_src}")
else:
    print("  ERROR: could not determine framework_dir")

print("=== esp32_webserver_path.py: done ===")
