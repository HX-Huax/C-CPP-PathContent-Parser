import random
from os import path
import clang.cindex


def get_node_key(cursor):
    """生成节点的唯一标识键"""
    extent = cursor.extent
    start = extent.start
    end = extent.end
    start_file = start.file.name if start.file else None
    end_file = end.file.name if end.file else None
    file_name = start_file or end_file or "unknown"
    return (
        file_name,
        start.line,
        start.column,
        end.line,
        end.column,
        cursor.kind,
        cursor.spelling,
    )


def parse_ast(file_path):
    """解析AST并返回节点信息"""
    clang.cindex.Config.set_library_file("/usr/lib/libclang.so")
    idx = clang.cindex.Index.create()
    compile_args = ["-std=c++11", "-fsyntax-only"]
    ast = idx.parse(file_path, args=compile_args, options=0)

    parent_map = {}  # 节点键到父节点键的映射
    nodes = []  # 所有节点键列表
    node_details = {}  # 节点详细信息存储

    def traverse(cursor, parent_key=None):
        """递归遍历AST构建关系"""
        current_key = get_node_key(cursor)
        if current_key in parent_map:
            return

        # 记录当前节点信息
        node_details[current_key] = (
            cursor.kind,
            cursor.spelling,
            cursor.location,
            cursor.extent,
        )

        parent_map[current_key] = parent_key
        nodes.append(current_key)

        # 递归处理子节点
        for child in cursor.get_children():
            traverse(child, current_key)

    traverse(ast.cursor)
    return nodes, parent_map, node_details


def find_ast_path(start_key, end_key, parent_map):
    """查找两个节点间的AST路径"""

    def get_ancestor_path(key):
        path = []
        while key is not None:
            path.append(key)
            key = parent_map.get(key)
        return path

    start_path = get_ancestor_path(start_key)
    end_path = get_ancestor_path(end_key)

    # 寻找最后一个共同祖先
    common_ancestor = None
    i, j = len(start_path) - 1, len(end_path) - 1
    while i >= 0 and j >= 0 and start_path[i] == end_path[j]:
        common_ancestor = start_path[i]
        i -= 1
        j -= 1

    if not common_ancestor:
        return []

    # 构建完整路径
    return start_path[: i + 1] + end_path[: j + 1][::-1]


def generate_paths(nodes, parent_map, node_details, path_max=5, path_count=10):
    """生成随机路径集合"""
    paths = []
    while len(paths) < path_count:
        # 随机选择节点对
        start, end = random.sample(nodes, 2)
        full_path = find_ast_path(start, end, parent_map)

        if not full_path:
            continue

        # 路径分割处理
        for i in range(0, len(full_path), path_max):
            chunk = full_path[i : i + path_max]
            if len(chunk) < 2:  # 忽略单节点路径
                continue
            paths.append(chunk)
            if len(paths) >= path_count:
                break

    # 转换节点键为可读信息
    result = []
    for path in paths[:path_count]:
        formatted = []
        for key in path:
            kind, spelling, loc, _ = node_details[key]
            file_part = loc.file.name if loc.file else "unknown"
            line_part = loc.line
            formatted.append(f"{kind.name} ({spelling}) @ {file_part}:{line_part}")
        result.append(" -> ".join(formatted))
    return result


if __name__ == "__main__":
    # 配置参数
    cpp_file = "/home/hx/Dev/Project/CAM/ProProcess/examples/cpp/example.cpp"
    output_file = "ast_paths.txt"
    max_length = 5
    required_paths = 10

    # 执行流程
    node_keys, parent_relations, node_info = parse_ast(cpp_file)
    ast_paths = generate_paths(
        node_keys, parent_relations, node_info, max_length, required_paths
    )

    # 写入输出文件
    with open(output_file, "w") as f:
        for idx, path in enumerate(ast_paths, 1):
            f.write(f"Path {idx}:\n{path}\n\n")
    print(f"Generated {len(ast_paths)} AST paths in {output_file}")
