#include "FileCollector.h"

void FileCollector::CollectFile(uint32_t fileId, size_t fileSize) {
  std::lock_guard<std::mutex> lock(globalMutex);
  if (files.find(fileId) != files.end()) {
    throw std::runtime_error("File already exists or in progress");
  }
  files[fileId] = std::make_shared<FileData>(fileSize);
}

void FileCollector::OnNewChunk(uint32_t fileId, size_t pos,
                               std::vector<uint8_t> &&chunk) {

  std::shared_ptr<FileData> fileData;
  {
    std::lock_guard<std::mutex> lock(globalMutex);
    auto it = files.find(fileId);
    if (it == files.end()) {

      return;
    }
    fileData = it->second;
  }

  std::unique_lock<std::mutex> lock(fileData->mtx);
  if (fileData->done) {

    return;
  }

  if (pos >= fileData->fileSize) {
    return;
  }
  if (pos + chunk.size() > fileData->fileSize) {

    chunk.resize(fileData->fileSize - pos);
  }
  if (chunk.empty()) {
    return;
  }

  auto currentIt = fileData->segments.emplace(pos, std::move(chunk)).first;
  size_t currStart = currentIt->first;
  size_t currEnd = currStart + currentIt->second.size();

  if (currentIt != fileData->segments.begin()) {
    auto prevIt = std::prev(currentIt);
    size_t prevStart = prevIt->first;
    size_t prevEnd = prevStart + prevIt->second.size();
    if (prevEnd >= currStart) {

      size_t mergedStart = prevStart;
      size_t mergedEnd = std::max(currEnd, prevEnd);
      std::vector<uint8_t> mergedData;
      mergedData.resize(mergedEnd - mergedStart);

      std::copy(prevIt->second.begin(), prevIt->second.end(),
                mergedData.begin());

      size_t overlapPos = prevEnd;
      if (overlapPos < currStart)
        overlapPos = currStart;
      size_t offsetInCurr =
          (overlapPos > currStart ? overlapPos - currStart : 0);
      if (offsetInCurr < currentIt->second.size()) {
        std::copy(currentIt->second.begin() + offsetInCurr,
                  currentIt->second.end(),
                  mergedData.begin() + (overlapPos - mergedStart));
      }

      fileData->segments.erase(currentIt);
      prevIt->second = std::move(mergedData);
      currentIt = prevIt;
      currStart = currentIt->first;
      currEnd = currStart + currentIt->second.size();
    }
  }

  while (true) {
    auto nextIt = std::next(currentIt);
    if (nextIt == fileData->segments.end()) {
      break;
    }
    size_t nextStart = nextIt->first;
    size_t nextEnd = nextStart + nextIt->second.size();
    if (nextStart <= currEnd) {

      size_t mergedStart = currStart;
      size_t mergedEnd = std::max(currEnd, nextEnd);
      std::vector<uint8_t> mergedData;
      mergedData.resize(mergedEnd - mergedStart);

      std::copy(currentIt->second.begin(), currentIt->second.end(),
                mergedData.begin());

      size_t overlapPos = currEnd;
      if (overlapPos < nextStart)
        overlapPos = nextStart;
      size_t offsetInNext =
          (overlapPos > nextStart ? overlapPos - nextStart : 0);
      if (offsetInNext < nextIt->second.size()) {
        std::copy(nextIt->second.begin() + offsetInNext, nextIt->second.end(),
                  mergedData.begin() + (overlapPos - mergedStart));
      }

      currentIt->second = std::move(mergedData);
      currEnd = mergedEnd;

      fileData->segments.erase(nextIt);
    } else {

      break;
    }
  }

  if (fileData->segments.size() == 1) {
    auto onlySegIt = fileData->segments.begin();
    if (onlySegIt->first == 0 &&
        onlySegIt->second.size() == fileData->fileSize) {

      fileData->done = true;

      std::vector<uint8_t> completeFile = std::move(onlySegIt->second);
      fileData->segments.clear();

      lock.unlock();
      fileData->promise.set_value(std::move(completeFile));

      std::lock_guard<std::mutex> gLock(globalMutex);
      files.erase(fileId);
    }
  }
}

std::future<std::vector<uint8_t>> FileCollector::GetFile(uint32_t fileId) {
  std::shared_ptr<FileData> fileData;
  {
    std::lock_guard<std::mutex> lock(globalMutex);
    auto it = files.find(fileId);
    if (it == files.end()) {
      throw std::runtime_error("File ID not found");
    }
    fileData = it->second;
  }

  std::lock_guard<std::mutex> lockFile(fileData->mtx);

  return fileData->promise.get_future();
}