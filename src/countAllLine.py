import os
import sys


def count_lines_in_file(file_path):
    try:
        with open(file_path, "r") as file:
            return len(file.readlines())
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
        return 0


def count_lines_in_folder(folder_path):
    total_lines = 0
    for root, _, files in os.walk(folder_path):
        for file in files:
            if file.endswith((".cpp", ".c")):
                file_path = os.path.join(root, file)
                lines = count_lines_in_file(file_path)
                print(f"{file_path}: {lines} lines")
                total_lines += lines
    print(f"Total lines in all .cpp and .c files: {total_lines}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python countFolderLines.py <folder_path>")
    else:
        count_lines_in_folder(sys.argv[1])
