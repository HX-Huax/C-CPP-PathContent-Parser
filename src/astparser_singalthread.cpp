#include <filesystem> // 需要 C++17 支持
#include <fstream>
#include <iostream>
#include <string>
#include <tree_sitter/api.h>
#include <vector>

// 声明 Tree-sitter 语言库函数（需提前编译安装对应语言库）
extern "C" TSLanguage *tree_sitter_c();
extern "C" TSLanguage *tree_sitter_cpp();

/**
 * @brief 递归遍历语法树节点
 * @param node 当前遍历的节点
 * @param source 源代码内容（用于获取节点文本）
 * @param depth 当前遍历深度（用于缩进显示）
 */
void traverse_ast(TSNode node, const std::string &source, int depth = 0) {
  // 获取节点信息
  const char *node_type = ts_node_type(node);
  TSPoint start_point = ts_node_start_point(node);
  TSPoint end_point = ts_node_end_point(node);
  uint32_t start_byte = ts_node_start_byte(node);
  uint32_t end_byte = ts_node_end_byte(node);

  // 提取节点对应的源代码文本
  std::string node_text = source.substr(start_byte, end_byte - start_byte);

  // 打印节点信息（带缩进）
  std::cout << std::string(depth * 2, ' ') << "[" << node_type << "] "
            << "Lines: " << start_point.row + 1 << "-" << end_point.row + 1
            << ", Text: \"" << node_text << "\"" << std::endl;

  // 递归遍历子节点
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; ++i) {
    TSNode child = ts_node_child(node, i);
    traverse_ast(child, source, depth + 1);
  }
}

/**
 * @brief 解析单个文件
 * @param file_path 文件路径
 * @param language 对应的 Tree-sitter 语言解析器
 */
void parse_file(const std::filesystem::path &file_path, TSLanguage *language) {
  // 读取文件内容
  std::ifstream file(file_path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    std::cerr << "无法打开文件: " << file_path << std::endl;
    return;
  }

  // 获取文件大小并读取内容
  size_t file_size = file.tellg();
  file.seekg(0, std::ios::beg);
  std::vector<char> buffer(file_size + 1, 0);
  if (!file.read(buffer.data(), file_size)) {
    std::cerr << "读取文件失败: " << file_path << std::endl;
    return;
  }
  std::string source(buffer.data(), file_size);

  // 创建解析器并设置语言
  TSParser *parser = ts_parser_new();
  ts_parser_set_language(parser, language);

  // 生成语法树
  TSTree *tree =
      ts_parser_parse_string(parser, nullptr, source.c_str(), source.size());
  TSNode root_node = ts_tree_root_node(tree);

  // 检查语法错误
  if (ts_node_has_error(root_node)) {
    std::cerr << "文件包含语法错误: " << file_path << std::endl;
  }

  // 遍历并打印 AST
  std::cout << "\n======== 解析文件: " << file_path << " ========\n";
  traverse_ast(root_node, source);

  // 清理资源
  ts_tree_delete(tree);
  ts_parser_delete(parser);
}

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "用法: " << argv[0] << " <目标目录>" << std::endl;
    return 1;
  }

  // 获取目标目录路径
  std::filesystem::path root_path(argv[1]);
  if (!std::filesystem::exists(root_path)) {
    std::cerr << "目录不存在: " << root_path << std::endl;
    return 1;
  }

  // 递归遍历目录
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(root_path)) {
    if (entry.is_regular_file()) {
      const auto &path = entry.path();
      std::string ext = path.extension().string();

      // 根据扩展名选择对应解析器
      TSLanguage *lang = nullptr;
      if (ext == ".c") {
        lang = tree_sitter_c();
      } else if (ext == ".cpp" || ext == ".cc" || ext == ".cxx") {
        lang = tree_sitter_cpp();
      }

      // 仅处理 C/C++ 文件
      if (lang != nullptr) {
        parse_file(path, lang);
      }
    }
  }

  return 0;
}
