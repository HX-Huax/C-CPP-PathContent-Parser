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
#include <sstream>
#include <string>
#include <thread>
#include <tree_sitter/api.h>
#include <unordered_map>
#include <vector>

extern "C" TSLanguage *tree_sitter_c();
extern "C" TSLanguage *tree_sitter_cpp();

constexpr int MAX_DEPTH = 1200;
int PATH_CONTEXT_LENGTH = 200;

std::mutex cout_mutex;
std::mutex queue_mutex;
std::queue<std::filesystem::path> file_queue;

std::unordered_map<std::string, unsigned int> token_vocab;
std::unordered_map<std::string, unsigned int> type_vocab;
struct VectorHash {
  template <typename T> size_t operator()(const std::vector<T> &vec) const {
    return std::hash<std::string>()(std::string(vec.begin(), vec.end()));
  }
};
std::unordered_map<std::vector<unsigned int>, unsigned int, VectorHash>
    path_vocab;

std::atomic<int> files_processed{0};
size_t total_files = 0;

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

void cleanNodeType(std::string &type) {
  type.erase(std::remove_if(type.begin(), type.end(),
                            [](unsigned char c) { return std::isspace(c); }),
             type.end());
  std::replace(type.begin(), type.end(), '_', '|');
}

std::string node_type_to_string(TSNode node) {
  if (ts_node_is_null(node))
    return "null";
  std::string tmp = ts_node_type(node);
  cleanNodeType(tmp);
  return tmp;
}

TSNode find_lca(TSNode source, TSNode target) {
  std::vector<TSNode> path1, path2;
  TSNode current = source;
  if (ts_node_eq(source, target))
    return source;
  while (!ts_node_is_null(current)) {
    path1.emplace_back(current);
    current = ts_node_parent(current);
  }
  current = target;
  while (!ts_node_is_null(current)) {
    path2.emplace_back(current);
    current = ts_node_parent(current);
  }
  TSNode lca = TSNode{};
  int i = path1.size() - 1, j = path2.size() - 1;
  while (i >= 0 && j >= 0 && ts_node_eq(path1[i], path2[j])) {
    lca = path1[i];
    i--;
    j--;
  }
  return lca;
}

std::string lca_path_traverse(TSNode root,
                              const std::filesystem::path &file_path,
                              const std::string &source, std::mt19937 &gen,
                              int path_width = 200) {
  std::vector<TSNode> leaves;
  traverse_ast(root, [&](TSNode node) {
    if (isRealNode(node) && !ts_node_eq(node, root))
      leaves.emplace_back(node);
  });

  int leaves_count = leaves.size();
  std::string result;

  if (leaves_count >= 2) {
    result += (file_path.filename().string() + " ");
    std::uniform_int_distribution<int> isGen(0, 1);
    for (int i = 0; i < leaves_count; i++) {
      for (int j = i + 1; j < min(leaves_count, i + path_width); j++) {
        if (isGen(gen) == 0)
          continue;

        TSNode node1 = leaves[i];
        TSNode node2 = leaves[j];

        TSNode lca = find_lca(node1, node2);
        std::vector<TSNode> path;
        TSNode current = node1;
        while (!ts_node_eq(current, lca)) {
          if (ts_node_is_named(current) && !ts_node_is_error(current))
            path.push_back(current);
          current = ts_node_parent(current);
        }

        std::vector<TSNode> rpath;
        current = node2;
        while (!ts_node_eq(current, lca)) {
          if (ts_node_is_named(current) && !ts_node_is_error(current))
            rpath.push_back(current);
          current = ts_node_parent(current);
        }

        path.insert(path.end(), rpath.rbegin(), rpath.rend());

        std::string token1, token2, path_str;
        unsigned int token1_hash = 0, token2_hash = 0;
        std::vector<unsigned int> path_int;
        for (auto it = path.begin(); it != path.end(); ++it) {
          TSNode node = *it;
          if (it == path.begin()) {
            token1 = source.substr(ts_node_start_byte(node),
                                   ts_node_end_byte(node) -
                                       ts_node_start_byte(node));
            cleanNodeType(token1);
            auto it1 = token_vocab.find(token1);
            token1_hash = (it1 != token_vocab.end()) ? it1->second : 0;
          }

          std::string tmp = node_type_to_string(node);
          auto type_it = type_vocab.find(tmp);
          unsigned int type_id =
              (type_it != type_vocab.end()) ? type_it->second : 0;
          path_int.push_back(type_id);

          if (it == path.end() - 1) {
            token2 = source.substr(ts_node_start_byte(node),
                                   ts_node_end_byte(node) -
                                       ts_node_start_byte(node));
            cleanNodeType(token2);
            auto it2 = token_vocab.find(token2);
            token2_hash = (it2 != token_vocab.end()) ? it2->second : 0;

            auto path_it = path_vocab.find(path_int);
            path_str = (path_it != path_vocab.end())
                           ? std::to_string(path_it->second)
                           : "0";
          }
        }

        result += std::to_string(token1_hash) + "," + path_str + "," +
                  std::to_string(token2_hash) + " ";
      }
    }
  }
  return result;
}

void load_token_vocab(const std::filesystem::path &file_path) {
  std::ifstream infile(file_path);
  std::string token;
  unsigned int id;
  while (infile >> token >> id)
    token_vocab[token] = id;
}

void load_type_vocab(const std::filesystem::path &file_path) {
  std::ifstream infile(file_path);
  std::string type;
  unsigned int id;
  while (infile >> type >> id)
    type_vocab[type] = id;
}

void load_path_vocab(const std::filesystem::path &file_path) {
  std::ifstream infile(file_path);
  std::string line;
  while (std::getline(infile, line)) {
    std::stringstream ss(line);
    std::vector<unsigned int> path_vec;
    std::string part;
    // Parse all parts except the last one (ID)
    while (std::getline(ss, part, ',')) {
      part.erase(part.find_last_not_of(" \t") + 1); // Trim trailing spaces
      if (ss.peek() == EOF) {                       // Last part is the ID
        path_vocab[path_vec] = std::stoi(part);
        break;
      }
      path_vec.push_back(std::stoi(part));
    }

    // while (std::getline(ss, part, ',')) {
    //   if (part.find(' ') != std::string::npos) {
    //     std::stringstream parts(part);
    //     std::string last, id_str;
    //     std::getline(parts, last, ' ');
    //     std::getline(parts, id_str);
    //     if (!last.empty())
    //       path_vec.push_back(std::stoi(last));
    //     path_vocab[path_vec] = std::stoi(id_str);
    //   } else {
    //     path_vec.push_back(std::stoi(part));
    //   }
    // }
  }
}

void worker_thread() {
  while (true) {
    std::filesystem::path file_path;
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      if (file_queue.empty())
        return;
      file_path = file_queue.front();
      file_queue.pop();
    }

    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
      std::lock_guard<std::mutex> lock(cout_mutex);
      std::cerr << "无法打开文件: " << file_path << "\n";
      continue;
    }

    std::string source((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    TSParser *parser = ts_parser_new();
    TSLanguage *lang;
    thread_local std::mt19937 gen(std::random_device{}());

    if (file_path.extension() == ".c") {
      lang = tree_sitter_c();
    } else {
      lang = tree_sitter_cpp();
    }

    ts_parser_set_language(parser, lang);
    TSTree *tree =
        ts_parser_parse_string(parser, nullptr, source.c_str(), source.size());
    TSNode root = ts_tree_root_node(tree);

    std::string tmp = lca_path_traverse(root, file_path, source, gen, 200);
    {
      std::lock_guard<std::mutex> lock(cout_mutex);
      std::cout << tmp << "\n";
    }

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    int processed = files_processed.fetch_add(1, std::memory_order_relaxed) + 1;
    {
      std::lock_guard<std::mutex> lock(cout_mutex);
      std::clog << "\r已处理文件: " << processed << "/" << total_files << " ("
                << (processed * 100) / total_files << "%)";
      std::clog.flush();
    }
  }
}

int main(int argc, char **argv) {
  std::ios::sync_with_stdio(false);
  std::cin.tie(nullptr);
  std::cout.tie(nullptr);

  if (argc < 2 || argc > 3) {
    std::cerr << "用法: " << argv[0] << " <目标目录> [上下文长度]\n";
    return 1;
  }
  if (argc == 3)
    PATH_CONTEXT_LENGTH = std::stoi(argv[2]);

  const std::filesystem::path root_path(argv[1]);
  const std::filesystem::path vocab_dir = root_path / "out";

  load_token_vocab(vocab_dir / "token_vocab.txt");
  load_type_vocab(vocab_dir / "type_vocab.txt");
  load_path_vocab(vocab_dir / "path_vocab.txt");

  if (path_vocab.empty()) {
    std::cerr << "路径词汇表为空，请检查路径词汇表文件\n";
    return 1;
  }
  auto it = path_vocab.begin();
  std::cerr << "Path:";
  for (const auto &val : it->first) {
    std::cerr << val << ",";
  }
  std::cerr << "ID: " << it->second << std::endl;

  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(root_path)) {
    if (entry.is_regular_file()) {
      const auto &path = entry.path();
      std::string ext = path.extension().string();
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

  const unsigned num_threads = std::thread::hardware_concurrency();
  std::vector<std::thread> threads;
  std::clog << "启动" << num_threads << "个线程处理" << total_files
            << "个文件...\n";

  for (unsigned i = 0; i < num_threads; ++i)
    threads.emplace_back(worker_thread);

  for (auto &t : threads)
    t.join();

  std::clog << "\n处理完成！已处理文件数: " << files_processed << "/"
            << total_files << "\n";
  return 0;
}
