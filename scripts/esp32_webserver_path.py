Import("env")
import os

# Add the WebServer library include path using OS-native path resolution.
# This is needed on Windows where -I${platformio.packages_dir}/... in build_flags
# does not expand correctly (backslashes / spaces break the compiler flag).
framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
if framework_dir:
    webserver_include = os.path.join(framework_dir, "libraries", "WebServer", "src")
    if os.path.isdir(webserver_include):
        env.Append(CPPPATH=[webserver_include])
