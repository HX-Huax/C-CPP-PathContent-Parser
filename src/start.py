import subprocess


def run_astminer():
    jar_path = "/home/hx/Dev/Project/astminer/build/shadow/astminer.jar"
    config_path = "/home/hx/Dev/Project/astminer/config.yaml"
    command = ["java", "-jar", jar_path, config_path]

    try:
        result = subprocess.run(
            command,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        print("Command Output:")
        print(result.stdout)
    except subprocess.CalledProcessError as e:
        print("Error occurred while running astminer:")
        print(e.stderr)


if __name__ == "__main__":
    run_astminer()
