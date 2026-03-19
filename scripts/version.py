"""
PlatformIO pre-build script: generates FW_BUILD as YYYYMMDDNN.

Reads/writes .build_counter (format: YYYYMMDD:N) at the project root.
The counter resets to 1 on a new day, increments on each build run.
Multiple environments built in one pio invocation share the same version
via os.environ so the counter only increments once per run.
"""
Import("env")  # noqa: F821
import os
import datetime


def get_build_version():
    if "FW_BUILD_VERSION" in os.environ:
        return os.environ["FW_BUILD_VERSION"]

    today = datetime.date.today().strftime("%Y%m%d")
    counter_file = os.path.join(env.subst("$PROJECT_DIR"), ".build_counter")  # noqa: F821

    counter = 1
    if os.path.exists(counter_file):
        try:
            date_str, _, n_str = open(counter_file).read().strip().partition(":")
            if date_str == today:
                counter = int(n_str) + 1
        except (ValueError, OSError):
            pass

    with open(counter_file, "w") as f:
        f.write("{}:{}".format(today, counter))

    version = "{}{:02d}".format(today, counter)
    os.environ["FW_BUILD_VERSION"] = version
    return version


_version = get_build_version()
print("version.py: FW_BUILD = {}".format(_version))
env.Append(CPPDEFINES=[("FW_BUILD", '\\"' + _version + '\\"')])  # noqa: F821
