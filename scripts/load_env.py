# Load optional .env file into build environment
import os
env_path = os.path.join(os.getcwd(), ".env")
if os.path.exists(env_path):
    with open(env_path) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith('#') and '=' in line:
                k, v = line.split('=', 1)
                Import("env")
                env.Append(BUILD_FLAGS=[f'-D{k.strip()}=\\"{v.strip()}\\"'])
