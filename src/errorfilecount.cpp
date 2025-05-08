#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <tree_sitter/api.h>
#include <vector>

extern "C" TSLanguage *tree_sitter_c();
extern "C" TSLanguage *tree_sitter_cpp();

std::mutex cout_mutex;
std::mutex queue_mutex;
std::queue<std::filesystem::path> file_queue;
std::atomic<int> files_processed{0};
std::atomic<int> error_files{0};
size_t total_files{0};
void worker() {

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
    if (ts_node_has_error(root)) {
      std::lock_guard<std::mutex> lock(cout_mutex);
      std::cout << "[语法错误] " << file_path << "\n";
      error_files++;
    }

    // 清理资源
    ts_tree_delete(tree);
    ts_parser_delete(parser);

    // 更新计数器
    ++files_processed;
    {
      std::lock_guard<std::mutex> lock(cout_mutex);
      std::clog << "\r已处理文件: " << files_processed << "/" << total_files
                << " (" << (files_processed * 100) / total_files << "%)";
      std::clog << " 语法错误文件: " << error_files;
      std::clog.flush();
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
    threads.emplace_back(worker); // 由workthread内部决定使用的解析语言和解析器
  }

  // 等待所有线程完成
  for (auto &t : threads) {
    t.join();
  }

  // worker_thread(); // 由workthread内部决定使用的解析语言和解析器
  std::clog << "\n处理完成！已处理文件数: " << files_processed << "/"
            << total_files << std::endl;
  return 0;
}
