#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <threads.h>
#include <tree_sitter/api.h>
#include <vector>
// 声明 Tree-sitter 语言库
extern "C" TSLanguage *tree_sitter_c();
extern "C" TSLanguage *tree_sitter_cpp();
constexpr bool SHOW_FILE_NAME = true; // 显示文件名
constexpr bool SHOW_POSITION = true;  // 显示行列位置
constexpr char INDENT_CHAR = ' ';     // 缩进字符
constexpr int INDENT_SIZE = 2;        // 缩进量
constexpr int MAX_DEPTH = 1200;
int PATH_CONTEXT_LENGTH = 200; // 最长路径上下文长度
unsigned int path_vocab_hash = 1;
unsigned int type_vocab_hash = 1;
unsigned int token_vocab_hash = 1;
// 全局同步工具
std::mutex cout_mutex;                        // 控制台输出锁
std::mutex queue_mutex;                       // 任务队列锁
std::mutex path_mutex;                        // 路径队列锁
std::mutex token_mutex;                       // 词汇表锁
std::mutex type_mutex;                        // 词汇表锁
std::mutex path_vocab_mutex;                  // 路径词汇表锁
std::queue<std::filesystem::path> file_queue; // 文件路径队列
std::queue<std::vector<TSNode>> path_queue;   // 路径队列
std::atomic<int> files_processed{0};          // 已处理文件计数器
std::atomic<int> slock{1};
std::unordered_map<std::string, unsigned int> token_vocab;
std::unordered_map<std::string, unsigned int> type_vocab;
struct VecrtorHash {
  template <typename T> size_t operator()(const std::vector<T> &vec) const {
    return std::hash<std::string>()(std::string(vec.begin(), vec.end()));
  }
};
std::unordered_map<std::vector<unsigned int>, unsigned int, VecrtorHash>
    path_vocab;
size_t total_files = 0; // 总文件计数器
template <typename T> T min(T a, T b) { return a < b ? a : b; }
namespace utils {
bool is_leaf(TSNode node) { return ts_node_named_child_count(node) == 0; }
bool is_comment(TSNode node) {
  return std::string(ts_node_type(node)) == "comment";
}
bool is_line_comment(TSNode node) {
  return std::string(ts_node_type(node)) == "line_comment";
}
bool is_block_comment(TSNode node) {
  return std::string(ts_node_type(node)) == "block_comment";
}
bool is_whitespace(TSNode node) {
  std::string type = ts_node_type(node);
  return type.find("whitespace") != std::string::npos ||
         type.find("newline") != std::string::npos;
}
} // namespace utils

inline bool isRealNode(TSNode node) {
  return (!ts_node_is_null(node) && utils::is_leaf(node) &&
          !utils::is_comment(node) && !utils::is_line_comment(node) &&
          !utils::is_block_comment(node) && !utils::is_whitespace(node) &&
          !ts_node_is_error(node));
}

void traverse_ast(TSNode node, const std::function<void(TSNode)> &callback) {
  callback(node);
  uint32_t child_count = ts_node_named_child_count(node);
  for (uint32_t i = 0; i < child_count; ++i) {
    TSNode child = ts_node_named_child(node, i);
    traverse_ast(child, callback);
  }
}
// 修改NodeType定义,去除空格以及将组合词拆分为子token
void cleanNodeType(std::string &type) {
  type.erase(std::remove_if(type.begin(), type.end(),
                            [](unsigned char c) { return std::isspace(c); }),
             type.end());
  std::replace(type.begin(), type.end(), '_', '|');
  return;
}

std::string node_type_to_string(TSNode node) {
  if (ts_node_is_null(node)) {
    return "null";
  } else {
    std::string tmp = ts_node_type(node);
    tmp.erase(std::remove_if(tmp.begin(), tmp.end(),
                             [](unsigned char c) { return std::isspace(c); }),
              tmp.end());
    std::replace(tmp.begin(), tmp.end(), '_', '|');
    return std::move(tmp);
  }
}
/**
 * @brief 线程安全的AST遍历输出
 * @param node 当前节点
 * @param source 源代码
 * @param depth 缩进深度
 * @param file_path 当前文件路径
 */
void safe_traverse_ast(TSNode node, const std::string &source, int depth,
                       const std::filesystem::path &file_path) {
  // 获取节点信息（线程安全部分）
  const char *node_type = ts_node_type(node);
  TSPoint start_point = ts_node_start_point(node);
  uint32_t start_byte = ts_node_start_byte(node);
  uint32_t end_byte = ts_node_end_byte(node);
  std::string node_text = source.substr(start_byte, end_byte - start_byte);

  // 加锁输出
  {
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cout << "[" << file_path.filename() << "] "
              << std::string(depth * 2, ' ') << "[" << node_type << "] "
              << "L" << start_point.row + 1 << ":" << start_point.column + 1
              << " \"" << node_text << "\"\n";
  }

  // 递归处理子节点（无锁操作）
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; ++i) {
    TSNode child = ts_node_child(node, i);
    safe_traverse_ast(child, source, depth + 1, file_path);
  }
}
/**
 * 不输出源代码的AST遍历
 * @param node 当前节点
 * @param file_path 当前文件路径
 * @param depth 缩进深度
 */
void simplified_traverse(TSNode node, const std::filesystem::path &file_path,
                         int depth = 0) {
  const char *node_type = ts_node_type(node);
  TSPoint start = ts_node_start_point(node);
  {
    std::lock_guard<std::mutex> lock(cout_mutex);
    // 输出文件名（仅第一层节点显示）
    if (depth == 0 && SHOW_FILE_NAME) {
      std::cout << "[" << file_path.filename().string() << "]\n";
    }
    // 构建节点描述
    std::string node_desc;
    node_desc += std::string(depth * INDENT_SIZE, INDENT_CHAR);
    node_desc += node_type;
    // 添加位置信息
    if (SHOW_POSITION) {
      node_desc += " @ L" + std::to_string(start.row + 1) + ":C" +
                   std::to_string(start.column + 1);
    }
    node_desc +=
        "@child_count" + std::to_string(ts_node_named_child_count(node));
    std::cout << node_desc << "\n";
  }
  // 递归处理子节点
  uint32_t child_count = ts_node_named_child_count(node);
  for (uint32_t i = 0; i < child_count; ++i) {
    TSNode child = ts_node_named_child(node, i);
    simplified_traverse(child, file_path, depth + 1);
  }
}

/**
 * @brief 从指定叶节点出发随机游走，直到遇见另一叶节点，过程中构建游走路径
 *
 */
// TODO: 添加词汇表生成过程
void random_path_traverse(TSNode node, TSNode lastnode, std::string &path,
                          const std::string &source, std::mt19937 gen,
                          int depth, const TSNode root,
                          bool can_parent = true) {
  // 获得具名子节点计数
  uint32_t child_count = ts_node_named_child_count(node);
  // 递归出口,若当前节点无具名子节点,则记录该节点对应原始字符串并返回
  if (child_count == 0) {
    path += node_type_to_string(node);
    path += ',';
    path += source.substr(ts_node_start_byte(node),
                          ts_node_end_byte(node) - ts_node_start_byte(node));
    return;
  }
  if (depth > MAX_DEPTH) {
    path += "[MAX_DEPTH] ";
    return;
  }
  if (ts_node_is_error(node)) {
    return;
  }
  std::uniform_int_distribution<int> dir(0, 1);
  std::uniform_int_distribution<uint32_t> child_selector(0, child_count - 1);
  TSNode next_node;
  uint32_t random_index = child_selector(gen);
  uint32_t new_random_index = random_index;
  // 随机决定向父节点或是子节点游走(0:父节点,1:子节点)
  if (((dir(gen) == 0 && can_parent && ts_node_eq(node, root)) ||
       (child_count == 1 && can_parent &&
        ts_node_eq(lastnode, ts_node_named_child(node, 0)))) &&
      node.id != node.tree) {

    next_node = ts_node_parent(node);
    if (ts_node_is_named(node)) {
      path += node_type_to_string(node);
      path += ',';
    }
  } else {
    next_node = ts_node_named_child(node, random_index);
    if (ts_node_eq(next_node, lastnode) &&
        (ts_node_named_child_count(node) > 1)) {
      for (; new_random_index == random_index;
           new_random_index = child_selector(gen))
        ;
      next_node = ts_node_named_child(node, new_random_index);
    }
    can_parent = false;
    if (ts_node_is_named(node)) {
      path += node_type_to_string(node);
      path += ',';
    }
  }
  random_path_traverse(next_node, node, path, source, gen, depth + 1, root,
                       can_parent);
}
/**
 * @brief
 * 遍历AST，并根据指定的PathContext长度对超长路径截断。同时由Width控制在AST树上遍历时两叶节点间的宽度距离
 * @param node 当前节点
 * @param file_path 当前文件路径
 * @param depth 当前节点深度
 * @param source 源代码
 */
void random_traverse(TSNode node, const std::filesystem::path &file_path,
                     const std::string &source, const TSNode root,
                     std::mt19937 gen, int depth = 0) {
  const char *node_type = ts_node_type(node);

  std::unique_lock<std::mutex> lock(cout_mutex, std::defer_lock);
  if (depth == 0 && SHOW_FILE_NAME) {
    lock.lock();
    std::cout << file_path.filename().string() << " ";
    lock.unlock();
  }
  if (depth > MAX_DEPTH) {
    lock.lock();
    std::cout << "[MAX_DEPTH] ";
    lock.unlock();
    return;
  }
  uint32_t child_count = ts_node_named_child_count(node);
  if (child_count == 0 && !ts_node_is_null(node) && (!ts_node_eq(node, root))) {
    std::string path;
    if (!ts_node_is_error(node) && isRealNode(node)) {
      std::string tmp =
          source.substr(ts_node_start_byte(node),
                        ts_node_end_byte(node) - ts_node_start_byte(node));
      cleanNodeType(tmp);
      path += tmp;
      path += ",";
      path += node_type_to_string(node);
      path += ",";
      random_path_traverse(ts_node_parent(node), node, path, source, gen, 0,
                           root);
    }
    lock.lock();
    if (path.length() > PATH_CONTEXT_LENGTH) {
      int length = path.length();
      for (int i = 0;
           i < (length + PATH_CONTEXT_LENGTH - 1) / PATH_CONTEXT_LENGTH; i++) {
        std::cout << path.substr(i * PATH_CONTEXT_LENGTH,
                                 min<int>(PATH_CONTEXT_LENGTH,
                                          length - i * PATH_CONTEXT_LENGTH))
                  << " ";
      }

    } else if (path.length() > 0) {
      std::cout << path << " ";
    }
    lock.unlock();
  }
  for (uint32_t i = 0; i < child_count; ++i) {
    TSNode child = ts_node_named_child(node, i);
    random_traverse(child, file_path, source, root, gen, depth + 1);
  }
}

TSNode find_lca(TSNode source, TSNode targer) {
  std::vector<TSNode> path1, path2;
  // 获取节点信息
  TSNode current = source;
  if (ts_node_eq(source, targer)) {
    return source;
  }
  while (!ts_node_is_null(current)) {
    path1.emplace_back(current);
    current = ts_node_parent(current);
  }

  current = targer;
  while (!ts_node_is_null(current)) {
    path2.emplace_back(current);
    current = ts_node_parent(current);
  }
  TSNode lca = TSNode{};
  int i = path1.size() - 1;
  int j = path2.size() - 1;
  while (i >= 0 && j >= 0 && ts_node_eq(path1[i], path2[2])) {
    lca = path1[i];
    i--;
    j--;
  }
  return lca;
}

/**
 * @brief lca path extractor
 */
std::string lca_path_traverse(TSNode root,
                              const std::filesystem::path &file_path,
                              const std::string souce, std::mt19937 gen,
                              int path_width = 200) {
  std::vector<TSNode> leaves;
  traverse_ast(root, [&](TSNode node) {
    if (isRealNode(node) && !ts_node_eq(node, root)) {
      leaves.emplace_back(node);
    }
  });
  int leaves_count = leaves.size();
  std::string result;
  std::unique_lock<std::mutex> lock_token(token_mutex, std::defer_lock);
  std::unique_lock<std::mutex> lock_path(path_vocab_mutex, std::defer_lock);
  std::unique_lock<std::mutex> lock_type(type_mutex, std::defer_lock);
  if (leaves_count >= 2) {
    {
      result += (file_path.filename().string() + " ");
    }
    std::uniform_int_distribution<int> isGen(0, 1);
    for (int i = 0; i < leaves_count; i++) {
      for (int j = i + 1; j < min(leaves_count, i + path_width); j++) {
        if (isGen(gen) == 0) {
          continue;
        } else {

          std::vector<TSNode> path;
          TSNode node1 = leaves[i];
          TSNode node2 = leaves[j];
          if (!ts_node_is_null(node1) && !ts_node_is_null(node2)) {
            TSNode lca = find_lca(node1, node2);
            TSNode current = node1;
            while (!ts_node_eq(current, lca)) {
              if (ts_node_is_named(current) && (!ts_node_is_error(current))) {
                path.push_back(current);
              }
              current = ts_node_parent(current);
            }
            std::vector<TSNode> rpath;
            current = node2;
            while (!ts_node_eq(current, lca)) {
              if (ts_node_is_named(current) && (!ts_node_is_error(current))) {
                rpath.push_back(current);
              }
              current = ts_node_parent(current);
            }

            path.insert(path.end(), rpath.rbegin(), rpath.rend());
            // TODO: 修改为向输出队列添加内容以实现处理和输出解耦
            //  {
            //    std::lock_guard<std::mutex> lock(path_mutex);
            //    path_queue.emplace(std::move(path));
            //  }
            //
            //
            //
            // 输出路径
            {
              std::string path_str;
              std::string token1;
              std::string token2;
              unsigned int token1_hash = 0;
              unsigned int token2_hash = 0;
              std::vector<unsigned int> path_int;
              for (auto it = path.begin(); it != path.end(); it++) {
                TSNode node = *it;
                if (it == path.begin()) {
                  if (!ts_node_is_error(node)) {
                    token1 = souce.substr(ts_node_start_byte(node),
                                          ts_node_end_byte(node) -
                                              ts_node_start_byte(node));
                    cleanNodeType(token1);
                    lock_token.lock();
                    if (token_vocab[token1] == 0) {
                      token_vocab[token1] = token_vocab_hash++;
                    }
                    token1_hash = token_vocab[token1];
                    lock_token.unlock();
                  }
                }
                std::string tmp;
                tmp = node_type_to_string(node);
                lock_type.lock();
                if (type_vocab[tmp] == 0) {
                  type_vocab[tmp] = type_vocab_hash++;
                }
                path_int.push_back(type_vocab[tmp]);
                lock_type.unlock();
                if (it == path.end() - 1) {
                  token2 = souce.substr(ts_node_start_byte(node),
                                        ts_node_end_byte(node) -
                                            ts_node_start_byte(node));
                  cleanNodeType(token2);
                  lock_token.lock();
                  if (token_vocab[token2] == 0) {
                    token_vocab[token2] = token_vocab_hash++;
                  }
                  token2_hash = token_vocab[token2];
                  lock_token.unlock();
                  std::string tmp;
                  tmp = node_type_to_string(node);
                  lock_type.lock();
                  if (type_vocab[tmp] == 0) {
                    type_vocab[tmp] = type_vocab_hash++;
                  }
                  path_int.push_back(type_vocab[tmp]);
                  lock_type.unlock();
                  lock_path.lock();
                  if (path_vocab[path_int] == 0) {
                    path_vocab[path_int] = path_vocab_hash++;
                  }
                  path_str = std::to_string(path_vocab[path_int]);
                  lock_path.unlock();
                }
              }
              // cleanNodeType(path_str);
              result += (std::to_string(token1_hash) + ',' + path_str + ',' +
                         std::to_string(token2_hash) + ' ');
            }
          }
        }
      }
    }
  }
  return std::move(result);
}

/**
 * @brief 线程安全的文件解析函数
 * @param lang 语言解析器
 */
// void worker_thread(TSLanguage *lang) {
void worker_thread() {
  while (true) {
    std::filesystem::path file_path;

    // 从队列获取任务
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      if (file_queue.empty())
        return;
      file_path = file_queue.front();
      file_queue.pop();
    }

    // 读取文件内容
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
      std::lock_guard<std::mutex> lock(cout_mutex);
      std::cerr << "无法打开文件: " << file_path << "\n";
      continue;
    }

    std::string source((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());

    // 创建独立解析器实例（每个线程独立）
    TSParser *parser = ts_parser_new();
    TSLanguage *lang;
    thread_local std::mt19937 gen(std::random_device{}());
    if (file_path.extension().string() == ".c") {
      lang = tree_sitter_c();
    } else {
      lang = tree_sitter_cpp();
    }
    ts_parser_set_language(parser, lang);
    TSTree *tree =
        ts_parser_parse_string(parser, nullptr, source.c_str(), source.size());
    TSNode root = ts_tree_root_node(tree);

    // 处理AST
    // if (ts_node_has_error(root)) {
    //   if (eslock == 0) {
    //     std::lock_guard<std::mutex> lock(cout_mutex);
    //     std::cerr << "[语法错误] " << file_path << "\n";
    //   }
    // } else {
    // safe_traverse_ast(root, source, 0, file_path);
    // simplified_traverse(root, file_path, 0);
    // random_traverse(root, file_path, source, root, gen, 0);
    std::string tmp;
    tmp = lca_path_traverse(root, file_path, source, gen, 200);
    {
      std::lock_guard<std::mutex> lock(cout_mutex);
      std::cout << tmp << "\n";
    }
    // }

    // 清理资源
    ts_tree_delete(tree);
    ts_parser_delete(parser);

    // 更新计数器
    int processed = files_processed.fetch_add(1, std::memory_order_relaxed);
    processed++;
    {
      std::lock_guard<std::mutex> lock(cout_mutex);
      std::clog << "\r已处理文件: " << processed << "/" << total_files << " ("
                << (processed * 100) / total_files << "%)";
      std::clog.flush();
    }
  }
}

void output_thread() {
  std::unique_lock<std::mutex> lock(queue_mutex, std::defer_lock);
  std::unique_lock<std::mutex> lock2(cout_mutex, std::defer_lock);
  while (true) {
    std::vector<TSNode> path;
    {
      if (path_queue.empty())
        continue;
      path = path_queue.front();
      path_queue.pop();
    }
    // 输出路径
    for (auto it = path.begin(); it != path.end(); it++) {
      std::string type = ts_node_type(*it);
      cleanNodeType(type);
      if (it == path.begin()) {
        std::string name;
        std::cout << name;
        std::cout << type;
      }
      std::cout << " ";
    }
  }
}

int main(int argc, char **argv) {
  std::ios::sync_with_stdio(false);
  std::cin.tie(nullptr);
  std::cout.tie(nullptr);
  if (argc > 3) {
    std::cerr << "用法: " << argv[0] << " <目标目录>\n";
    return 1;
  }
  if (argc == 3) {
    PATH_CONTEXT_LENGTH = std::stoi(argv[2]);
  }
  // 收集目标文件
  const std::filesystem::path root_path(argv[1]);
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(root_path)) {
    if (entry.is_regular_file()) {
      const auto &path = entry.path();
      std::string ext = path.extension().string();

      // 识别C/C++文件
      if (ext == ".c" || ext == ".cpp" || ext == ".cc" || ext == ".cxx") {
        std::lock_guard<std::mutex> lock(queue_mutex);
        file_queue.push(path);
      }
    }
  }

  total_files = file_queue.size();
  if (total_files == 0) {
    std::cerr << "未找到C/C++文件\n";
    return 1;
  }

  // 根据硬件并发数创建线程
  const unsigned num_threads = std::thread::hardware_concurrency();
  std::vector<std::thread> threads;
  // TSLanguage *cpp_lang = tree_sitter_cpp(); // 预获取语言对象
  // TSLanguage *c_lang = tree_sitter_c();   // 预获取语言对象
  std::clog << "启动" << num_threads << "个线程处理" << total_files
            << "个文件..." << std::endl;

  // 创建工作线程
  for (unsigned i = 0; i < num_threads; ++i) {
    // threads.emplace_back(worker_thread, cpp_lang);
    threads.emplace_back(
        worker_thread); // 由workthread内部决定使用的解析语言和解析器
  }
  // threads.emplace_back(output_thread);
  // 等待所有线程完成
  for (auto &t : threads) {
    t.join();
  }
  const std::filesystem::path output_dir = root_path / "out";
  std::filesystem::create_directory(output_dir);
  {
    std::ofstream token_vocab_file(output_dir / "token_vocab.txt");
    for (const auto &entry : token_vocab) {
      token_vocab_file << entry.first << " " << entry.second << "\n";
    }
  }
  {
    std::ofstream type_vocab_file(output_dir / "type_vocab.txt");
    for (const auto &entry : type_vocab) {
      type_vocab_file << entry.first << " " << entry.second << "\n";
    }
  }
  {
    std::ofstream path_vocab_file(output_dir / "path_vocab.txt");
    for (const auto &entry : path_vocab) {
      for (const auto &v : entry.first) {
        path_vocab_file << v << ",";
      }
      path_vocab_file << " " << entry.second << "\n";
    }
  }
  // worker_thread(); // 由workthread内部决定使用的解析语言和解析器
  std::clog << "\n处理完成！已处理文件数: " << files_processed << "/"
            << total_files << std::endl;
  return 0;
}
