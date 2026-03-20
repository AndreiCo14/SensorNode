"""
PlatformIO pre-build script: generates FW_BUILD as YYYYMMDDNN.

Reads/writes .build_counter (format: YYYYMMDD:N) at the project root.
Counter increments only when OTA_RELEASE=1 is set in the environment
(i.e. when called from ota-upload.sh). Plain 'pio run' reuses the last
counter value so the build number doesn't change on every dev build.
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
    release = os.environ.get("OTA_RELEASE") == "1"

    stored_date, stored_n = today, 0
    if os.path.exists(counter_file):
        try:
            d, _, n = open(counter_file).read().strip().partition(":")
            stored_date, stored_n = d, int(n)
        except (ValueError, OSError):
            pass

    if release:
        if stored_date == today:
            counter = stored_n + 1
        else:
            counter = 1
        with open(counter_file, "w") as f:
            f.write("{}:{}".format(today, counter))
    else:
        # Dev build: reuse last counter without incrementing
        counter = stored_n if stored_date == today else 0

    version = "{}{:02d}".format(today, counter)
    os.environ["FW_BUILD_VERSION"] = version
    return version


_version = get_build_version()
print("version.py: FW_BUILD = {}".format(_version))
env.Append(CPPDEFINES=[("FW_BUILD", '\\"' + _version + '\\"')])  # noqa: F821
