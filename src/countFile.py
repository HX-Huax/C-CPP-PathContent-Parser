import os


def count_code_files_and_lines(path):
    c_cpp_files_count = 0
    total_lines = 0

    for root, _, files in os.walk(path):
        for file in files:
            if file.endswith((".c", ".cpp")):
                c_cpp_files_count += 1
                file_path = os.path.join(root, file)
                try:
                    with open(file_path, "r", encoding="utf-8") as f:
                        lines = f.readlines()
                        total_lines += len(lines)
                except Exception as e:
                    print(f"Error reading file {file_path}: {e}")

    return c_cpp_files_count, total_lines


if __name__ == "__main__":
    path = input("Enter the directory path to scan: ").strip()
    if not os.path.exists(path):
        print("The specified path does not exist.")
    else:
        files_count, lines_count = count_code_files_and_lines(path)
        print(f"Number of .c and .cpp files: {files_count}")
        print(f"Total lines of code: {lines_count}")
