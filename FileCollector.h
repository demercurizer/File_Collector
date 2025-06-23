#pragma once
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

class FileCollector {
public:

  void CollectFile(uint32_t fileId, size_t fileSize);

  void OnNewChunk(uint32_t fileId, size_t pos, std::vector<uint8_t> &&chunk);

  std::future<std::vector<uint8_t>> GetFile(uint32_t fileId);

private:
  struct FileData {
    size_t fileSize;
    std::map<size_t, std::vector<uint8_t>> segments;
    bool done;
    std::promise<std::vector<uint8_t>> promise;
    std::mutex mtx;
    FileData(size_t size) : fileSize(size), done(false) {}
  };

  std::mutex globalMutex;
  std::map<uint32_t, std::shared_ptr<FileData>> files;
};