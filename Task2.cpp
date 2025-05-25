#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>

std::mutex coutMutex; // Mutex to protect console output from multiple threads

// Compress a chunk of data using Run-Length Encoding (RLE)
std::vector<char> compressRLEChunk(const std::vector<char>& data, size_t start, size_t end) {
    std::vector<char> compressed;
    size_t i = start;
    while (i < end) {
        char c = data[i];
        size_t count = 1;
        while (i + count < end && data[i + count] == c && count < 255)
            count++;
        compressed.push_back(c);
        compressed.push_back(static_cast<char>(count));
        i += count;
    }
    return compressed;
}

// Decompress a chunk of RLE-compressed data
std::vector<char> decompressRLEChunk(const std::vector<char>& data, size_t start, size_t end) {
    std::vector<char> decompressed;
    if ((end - start) % 2 != 0) {
        std::lock_guard<std::mutex> lock(coutMutex);
        std::cerr << "Invalid compressed chunk size\n";
        return decompressed;
    }
    for (size_t i = start; i < end; i += 2) {
        char c = data[i];
        unsigned char count = static_cast<unsigned char>(data[i + 1]);
        decompressed.insert(decompressed.end(), count, c);
    }
    return decompressed;
}

// Write binary data to file
bool writeFile(const std::string& filename, const std::vector<char>& data) {
    std::ofstream out(filename, std::ios::binary);
    if (!out) return false;
    out.write(data.data(), data.size());
    return true;
}

// Read entire binary file into a vector<char>
std::vector<char> readFile(const std::string& filename) {
    std::ifstream in(filename, std::ios::binary);
    if (!in) {
        std::cerr << "Failed to open " << filename << "\n";
        return {};
    }
    in.seekg(0, std::ios::end);
    size_t size = (size_t)in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<char> data(size);
    in.read(data.data(), size);
    return data;
}

// Interactive file creation: user enters lines, saved to disk
void createFile() {
    std::string filename;
    std::cout << "Enter filename to create: ";
    std::getline(std::cin, filename);

    std::ofstream out(filename);
    if (!out) {
        std::cerr << "Cannot create file.\n";
        return;
    }

    std::cout << "Enter text lines (empty line to finish):\n";
    while (true) {
        std::string line;
        std::getline(std::cin, line);
        if (line.empty()) break;
        out << line << "\n";
    }

    std::cout << "File \"" << filename << "\" created.\n";
}

// Multithreaded RLE compression
void compressFile() {
    std::string inFile, outFile;
    std::cout << "Enter file to compress: ";
    std::getline(std::cin, inFile);

    std::vector<char> data = readFile(inFile);
    if (data.empty()) {
        std::cerr << "Failed to read input file or file is empty.\n";
        return;
    }

    // Determine how many threads to use
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 2;

    // Split data into chunks
    std::vector<std::vector<char>> compressedChunks(numThreads);
    std::vector<std::thread> threads;
    size_t chunkSize = data.size() / numThreads;

    // Launch threads for each chunk
    for (unsigned int i = 0; i < numThreads; i++) {
        size_t start = i * chunkSize;
        size_t end = (i == numThreads -1) ? data.size() : start + chunkSize;

        threads.emplace_back([&data, &compressedChunks, i, start, end]() {
            compressedChunks[i] = compressRLEChunk(data, start, end);
        });
    }

    // Wait for all threads to finish
    for (auto& t : threads) t.join();

    // Merge all compressed chunks
    std::vector<char> compressed;
    for (const auto& chunk : compressedChunks)
        compressed.insert(compressed.end(), chunk.begin(), chunk.end());

    std::cout << "Original size: " << data.size() << ", Compressed size: " << compressed.size() << "\n";

    std::cout << "Enter output file name for compressed data: ";
    std::getline(std::cin, outFile);

    // Store header + data
    std::vector<char> outputData;
    if (compressed.size() >= data.size()) {
        std::cout << "Compression not effective. Saving uncompressed data.\n";
        outputData.push_back('U');
        outputData.insert(outputData.end(), data.begin(), data.end());
    } else {
        outputData.push_back('C');
        outputData.insert(outputData.end(), compressed.begin(), compressed.end());
    }

    if (!writeFile(outFile, outputData)) {
        std::cerr << "Failed to write compressed file.\n";
        return;
    }

    std::cout << "Compression successful.\n";
}

// Multithreaded RLE decompression
void decompressFile() {
    std::string inFile, outFile;
    std::cout << "Enter file to decompress: ";
    std::getline(std::cin, inFile);

    std::vector<char> data = readFile(inFile);
    if (data.size() < 1) {
        std::cerr << "Input file is empty or failed to read.\n";
        return;
    }

    char header = data[0];
    std::vector<char> decompressed;

    if (header == 'C') {
        const char* rawData = data.data() + 1;
        size_t rawSize = data.size() - 1;

        unsigned int numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 2;

        size_t pairCount = rawSize / 2;
        size_t pairsPerThread = pairCount / numThreads;

        std::vector<std::vector<char>> decompressedChunks(numThreads);
        std::vector<std::thread> threads;

        // Decompress each chunk in its own thread
        for (unsigned int i = 0; i < numThreads; i++) {
            size_t startPair = i * pairsPerThread;
            size_t endPair = (i == numThreads -1) ? pairCount : startPair + pairsPerThread;

            size_t startByte = startPair * 2;
            size_t endByte = endPair * 2;

            threads.emplace_back([rawData, startByte, endByte, &decompressedChunks, i]() {
                decompressedChunks[i] = decompressRLEChunk(
                    std::vector<char>(rawData + startByte, rawData + endByte), 0, endByte - startByte);
            });
        }

        // Wait for all threads to finish
        for (auto& t : threads) t.join();

        // Combine all decompressed chunks
        for (const auto& chunk : decompressedChunks)
            decompressed.insert(decompressed.end(), chunk.begin(), chunk.end());

        std::cout << "Data was compressed using RLE.\n";

    } else if (header == 'U') {
        // File was stored uncompressed
        decompressed = std::vector<char>(data.begin() + 1, data.end());
        std::cout << "Data was stored uncompressed.\n";
    } else {
        std::cerr << "Unknown file format header.\n";
        return;
    }

    std::cout << "Compressed size: " << data.size() - 1 << ", Decompressed size: " << decompressed.size() << "\n";

    std::cout << "Enter output filename for decompressed data: ";
    std::getline(std::cin, outFile);

    if (!writeFile(outFile, decompressed)) {
        std::cerr << "Failed to write decompressed file.\n";
        return;
    }

    std::cout << "Decompression successful.\n";
}

// Main program loop
int main() {
    std::cout << "Multithreaded File Compressor/Decompressor using RLE\n";

    while (true) {
        std::cout << "\nMenu:\n";
        std::cout << "1. Create and write a file\n";
        std::cout << "2. Compress a file\n";
        std::cout << "3. Decompress a file\n";
        std::cout << "4. Exit\n";
        std::cout << "Enter choice: ";

        std::string choiceStr;
        std::getline(std::cin, choiceStr);

        if (choiceStr == "1") {
            createFile();
        } else if (choiceStr == "2") {
            compressFile();
        } else if (choiceStr == "3") {
            decompressFile();
        } else if (choiceStr == "4") {
            std::cout << "Goodbye!\n";
            break;
        } else {
            std::cout << "Invalid choice. Try again.\n";
        }
    }

    return 0;
}
