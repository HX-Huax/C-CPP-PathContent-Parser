import os
import shutil
import subprocess
import tempfile
from clang.cindex import Index, Config

# 设置libclang库路径（若需要）
Config.set_library_path("/usr/lib/libclang.so")


def clean_and_generate_ast(source_path, build_dir=None):
    # 创建临时目录处理文件
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_source = os.path.join(tmp_dir, os.path.basename(source_path))
        shutil.copy(source_path, tmp_source)

        # 使用IWYU清理头文件
        try:
            iwyu_cmd = ["include-what-you-use"]
            if build_dir:
                iwyu_cmd += ["-p", build_dir]
            iwyu_cmd.append(tmp_source)
            # 捕获IWYU输出并修复头文件
            result = subprocess.run(
                iwyu_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
            )
            if result.returncode != 0:
                print(f"IWYU警告：{result.stderr}")
            # 使用fix_includes.py处理输出
            fix_cmd = [
                "iwyu-fix-includes",
                "--nocomments",
                "--reorder",
                "--nosafe_headers",
            ]
            subprocess.run(fix_cmd, input=result.stdout, text=True, cwd=tmp_dir)
        except Exception as e:
            print(f"IWYU处理失败：{e}")
            return None

        # 使用clang-tidy清理未使用的变量和宏
        try:
            tidy_cmd = ["clang-tidy", "--fix", "--checks=*unused*", tmp_source]
            if build_dir:
                tidy_cmd += ["-p", build_dir]
            subprocess.run(tidy_cmd, check=True)
        except subprocess.CalledProcessError as e:
            print(f"clang-tidy错误：{e}")
            return None

        # 生成AST
        index = Index.create()
        translation_unit = index.parse(tmp_source)
        return translation_unit


# 示例使用
if __name__ == "__main__":
    source_file = "/home/hx/Dev/Project/CAM/ProProcess/examples/cpp/example.cpp"
    # build_directory = 'build'  # 指向包含compile_commands.json的目录
    ast = clean_and_generate_ast(source_file)
    if ast:
        print("AST生成成功！")
        # 遍历AST节点（示例）
        for node in ast.cursor.walk_preorder():
            if node.kind.is_declaration():
                print(f"声明：{node.spelling} ({node.location})")
