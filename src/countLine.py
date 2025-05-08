import sys


def count_lines(file_path):
    try:
        with open(file_path, "r") as file:
            lines = file.readlines()
            line_lengths = [len(line.strip()) for line in lines]
            # for id in range(len(line_lengths)):
            # if line_lengths[id] <= 100:
            # print(id)
            # print(lines[id].strip())
            # for id in range(1, 2):
            #     print(lines[id].strip())

            print(f"Total lines in {file_path}: {len(lines)}")
            # print(f"Line lengths: {line_lengths}")
            print(
                f"Average line length: {sum(line_lengths) / len(line_lengths) if line_lengths else 0}"
            )
    except FileNotFoundError:
        print(f"Error: File {file_path} not found.")
    except Exception as e:
        print(f"An error occurred: {e}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python countLine.py <file_path>")
    else:
        count_lines(sys.argv[1])
