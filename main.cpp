#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

#include "CRC32.hpp"
#include "IO.hpp"

/// @brief Переписывает последние 4 байта значением value
void replaceLastFourBytes(std::vector<char>& data, uint32_t value) {
    std::copy_n(reinterpret_cast<const char*>(&value), 4, data.end() - 4);
}

void CalcCrc(uint32_t start, uint32_t end, uint32_t prev_crc,
             uint32_t originalCrc32, uint32_t& res, std::atomic_bool& stop_fl) {
    std::vector<uint32_t> buff(2, 0);
    for (uint32_t i = start; i < end; ++i) {
        if (stop_fl) {
            break;
        }
        buff[0] = i;
        auto currentCrc32 =
            crc32(reinterpret_cast<char*>(&buff[0]), sizeof(i), prev_crc);
        if (currentCrc32 == originalCrc32) {
            std::cout << "Success (originalCrc32 = " << originalCrc32
                      << " currentCrc32 = " << currentCrc32 << " i = " << i
                      << ")" << std::endl;
            res = i;
            stop_fl = true;
            break;
        }
    }
}

/**
 * @brief Формирует новый вектор с тем же CRC32, добавляя в конец оригинального
 * строку injection и дополнительные 4 байта
 * @details При формировании нового вектора последние 4 байта не несут полезной
 * нагрузки и подбираются таким образом, чтобы CRC32 нового и оригинального
 * вектора совпадали
 * @param original оригинальный вектор
 * @param injection произвольная строка, которая будет добавлена после данных
 * оригинального вектора
 * @return новый вектор
 */
std::vector<char> hack(const std::vector<char>& original,
                       const std::string& injection) {
    const uint32_t originalCrc32 = crc32(original.data(), original.size());

    std::vector<char> result(original.size() + injection.size() + 4, 0);
    auto it = std::copy(original.begin(), original.end(), result.begin());
    std::copy(injection.begin(), injection.end(), it);

    const uint32_t originalPlusInjectionCrc32 =
        crc32(result.data(), original.size() + injection.size());

    const size_t maxVal = std::numeric_limits<uint32_t>::max();

    const size_t numberThr{std::thread::hardware_concurrency()};
    std::cout << "numberThr = " << numberThr << std::endl;
    std::vector<std::thread> threads;
    std::vector<uint32_t> results(numberThr, 0);
    size_t batch_size = maxVal / numberThr;
    std::cout << "batch_size = " << batch_size << std::endl;
    std::cout << "maxVal = " << maxVal << std::endl;
    std::atomic_bool stop{false};
    for (size_t i = 0; i < numberThr; i++) {
        const size_t start = i * batch_size;
        const size_t end = start + batch_size;
        std::cout << "Cretate thread i = " << i << " start = " << start
                  << " end = " << end << std::endl;

        threads.emplace_back(std::thread{
            CalcCrc, start, std::min(maxVal, end), originalPlusInjectionCrc32,
            originalCrc32, std::ref(results[i]), std::ref(stop)});
    }
    for (auto& t : threads) {
        t.join();
    }
    for (auto& r : results) {
        if (r != 0) {
            std::cout << "Result = " << r << std::endl;
            replaceLastFourBytes(result, r);
            return result;
        }
    }
    throw std::logic_error("Can't hack");
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Call with two args: " << argv[0]
                  << " <input file> <output file>\n";
        return 1;
    }

    try {
        const std::vector<char> data = readFromFile(argv[1]);
        const std::vector<char> badData = hack(data, "He-he-he");
        writeToFile(argv[2], badData);
    } catch (std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 2;
    }
    return 0;
}
