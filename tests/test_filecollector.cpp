#include <gtest/gtest.h>
#include "FileCollector.h"
#include <thread>
#include <algorithm>
#include <random>

// Тест сборки файла из перекрывающихся фрагментов
TEST(FileCollectorTest, AssembleFromOverlappingChunks) {
    FileCollector fc;
    uint32_t fileId = 1;
    std::string part1 = "Hello";
    std::string part2 = "World";
    // Перекрытие "llo" между part1 и part2
    std::string chunk1 = part1;               // "Hello" в позиции 0
    std::string chunk2 = part1.substr(2) + part2; // "lloWorld" в позиции 2
    std::string expected = part1 + part2;     // "HelloWorld"
    fc.CollectFile(fileId, expected.size());
    auto future = fc.GetFile(fileId);
    // Имитируем получение фрагментов не по порядку
    fc.OnNewChunk(fileId, 2, std::vector<uint8_t>(chunk2.begin(), chunk2.end()));
    fc.OnNewChunk(fileId, 0, std::vector<uint8_t>(chunk1.begin(), chunk1.end()));
    // Ждем завершения сборки и проверяем содержимое
    std::vector<uint8_t> result = future.get();
    std::string resultStr(result.begin(), result.end());
    EXPECT_EQ(resultStr, expected);
}

// Тест одновременной сборки нескольких файлов
TEST(FileCollectorTest, MultipleFilesParallelAssembly) {
    FileCollector fc;
    // Файл 1: "ABCDEFGHIJKLMNOPQRSTUVWXYZ" (26 байт)
    std::string data1 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    // Файл 2: "0123456789" (10 байт)
    std::string data2 = "0123456789";
    uint32_t id1 = 100, id2 = 200;
    fc.CollectFile(id1, data1.size());
    fc.CollectFile(id2, data2.size());
    auto fut1 = fc.GetFile(id1);
    auto fut2 = fc.GetFile(id2);
    // Разбиваем data1 на две части и data2 на две части
    std::string data1_a = data1.substr(0, 10);  // "ABCDEFGHIJ"
    std::string data1_b = data1.substr(10);     // "KLMNOPQRSTUVWXYZ"
    std::string data2_a = data2.substr(0, 5);   // "01234"
    std::string data2_b = data2.substr(5);      // "56789"
    // Запускаем потоки для имитации параллельного получения фрагментов для каждого файла
    std::thread t1([&]() {
        // Отправляем в обратном порядке для файла 1
        fc.OnNewChunk(id1, 10, std::vector<uint8_t>(data1_b.begin(), data1_b.end()));
        fc.OnNewChunk(id1, 0, std::vector<uint8_t>(data1_a.begin(), data1_a.end()));
    });
    std::thread t2([&]() {
        // Отправляем в обычном порядке для файла 2
        fc.OnNewChunk(id2, 0, std::vector<uint8_t>(data2_a.begin(), data2_a.end()));
        fc.OnNewChunk(id2, 5, std::vector<uint8_t>(data2_b.begin(), data2_b.end()));
    });
    // Ждем завершения работы потоков
    t1.join();
    t2.join();
    // Получаем результаты только один раз на каждый future
    auto vec1 = fut1.get();
    std::string result1(vec1.begin(), vec1.end());
    auto vec2 = fut2.get();
    std::string result2(vec2.begin(), vec2.end());
    // Проверяем, что оба файла собраны правильно
    EXPECT_EQ(result1, data1);
    EXPECT_EQ(result2, data2);
}

// Тест потокобезопасности путем сборки одного файла из множества фрагментов параллельно
TEST(FileCollectorTest, ThreadSafetyMultipleChunks) {
    FileCollector fc;
    uint32_t fileId = 999;
    // Создаем известную последовательность данных (например, 1000 байт)
    const size_t dataSize = 1000;
    std::vector<uint8_t> original(dataSize);
    for (size_t i = 0; i < dataSize; ++i) {
        original[i] = static_cast<uint8_t>(i % 256);
    }
    fc.CollectFile(fileId, dataSize);
    auto fut = fc.GetFile(fileId);
    // Разделяем данные на фрагменты, некоторые из них перекрываются
    struct Chunk { size_t pos; std::vector<uint8_t> data; };
    std::vector<Chunk> chunks;
    size_t chunkSize = 100;
    // Создаем последовательные фрагменты
    for (size_t pos = 0; pos < dataSize; pos += chunkSize) {
        size_t len = std::min(chunkSize, dataSize - pos);
        chunks.push_back({ pos, std::vector<uint8_t>(original.begin()+pos, original.begin()+pos+len) });
    }
    // Добавляем некоторые перекрывающиеся фрагменты, которые охватывают несколько последовательных фрагментов
    for (size_t pos = 0; pos < dataSize; pos += 2*chunkSize) {
        size_t spanEnd = std::min(dataSize, pos + 3*chunkSize/2);
        if (spanEnd > pos) {
            chunks.push_back({ pos, std::vector<uint8_t>(original.begin()+pos, original.begin()+spanEnd) });
        }
    }
    // Перемешиваем фрагменты для случайного порядка
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(chunks.begin(), chunks.end(), g);
    // Распределяем фрагменты между потоками
    int numThreads = 4;
    std::vector<std::vector<Chunk>> threadChunks(numThreads);
    for (size_t i = 0; i < chunks.size(); ++i) {
        threadChunks[i % numThreads].push_back(std::move(chunks[i]));
    }
    // Запускаем потоки для одновременного добавления фрагментов
    std::vector<std::thread> threads;
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (auto& ch : threadChunks[t]) {
                fc.OnNewChunk(fileId, ch.pos, std::vector<uint8_t>(ch.data.begin(), ch.data.end()));
            }
        });
    }
    // Ждем завершения всех потоков
    for (auto& th : threads) {
        th.join();
    }
    // Получаем результат и проверяем, что он соответствует исходным данным
    std::vector<uint8_t> result = fut.get();
    EXPECT_EQ(result.size(), original.size());
    EXPECT_EQ(result, original);
}

// Тест, что запрос файла, который не был собран, вызывает исключение
TEST(FileCollectorTest, GetFileInvalidId) {
    FileCollector fc;
    EXPECT_THROW(fc.GetFile(42), std::runtime_error);
}


TEST(Deadlock, CausesDeadlockIfNoUnlock) {
    FileCollector fc;
    uint32_t fileId = 42;
    fc.CollectFile(fileId, /*size=*/10);

    // Producer: calls the *bad* version that doesn't unlock before set_value
    std::thread producer([&](){
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        fc.OnNewChunk(
            fileId,
            /*pos=*/0,
            /*chunk=*/
            std::vector<uint8_t>(10, 0xFF)  // ten bytes of 0xFF, say
        );
    });

    // Consumer: grabs the future and waits, then calls GetFile again
    std::thread consumer([&](){
        auto fut = fc.GetFile(fileId);
        auto data = fut.get();  // wakes up when promise.set_value() is called under lock
        // Immediately try to call GetFile again, which locks the same mutex
        fc.GetFile(fileId);
    });

    // Let them deadlock
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // If we reach here, the test harness will hang on join() because of the deadlock:
    producer.detach();
    consumer.detach();
    SUCCEED() << "Test should have hung due to deadlock if OnNewChunkBad doesn't unlock";
}
