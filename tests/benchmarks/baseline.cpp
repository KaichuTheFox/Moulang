#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

int integer_math() {
    int value = 1;
    for (int i = 0; i < 300000; ++i) {
        int next = value * 3 + i;
        value = next - next / 1000003 * 1000003;
    }
    return value;
}

double real_math() {
    double value = 1.0;
    for (int i = 0; i < 300000; ++i) value = (value + double(i - i / 17 * 17)) * 0.5;
    return value;
}

int loop_count() {
    int count = 0;
    for (int i = 0; i < 1000000; ++i) if (i - i / 13 * 13 == 0) ++count;
    return count;
}

int mix(int value) {
    int mixed = value * 3 + 7;
    return mixed - mixed / 1009 * 1009;
}

int function_calls() {
    int sum = 0;
    for (int i = 0; i < 300000; ++i) sum += mix(i);
    return sum;
}

bool string_concat() {
    std::string value;
    for (int i = 0; i < 3500; ++i) {
        const auto& previous = value;
        value = previous + "x";
    }
    return value == std::string(3500, 'x');
}

int list_access() {
    std::vector<std::string> items{"a", "b", "c", "d"};
    int count = 0;
    for (int i = 0; i < 50000; ++i) {
        items.push_back("e");
        if (items.at(4) == "e" && items.at(1) == "b") ++count;
        items.erase(std::find(items.begin(), items.end(), "a"));
        items.pop_back();
        items.insert(items.begin(), "a");
    }
    return count + int(items.size());
}

int list_sort() {
    std::vector<std::string> items{"d", "c", "b", "a"};
    int count = 0;
    for (int i = 0; i < 50000; ++i) {
        items[0] = "d";
        items[1] = "c";
        items[2] = "b";
        items[3] = "a";
        std::sort(items.begin(), items.end());
        if (items[0] == "a" && items[1] == "b" && items[2] == "c" && items[3] == "d") ++count;
    }
    return count;
}

int utf8_sweep(std::string_view point) {
    std::string data;
    for (int i = 0; i < 4000; ++i) data += point;
    int count = 0;
    for (int repeat = 0; repeat < 300; ++repeat) {
        for (std::size_t start = 0; start < data.size();) {
            std::size_t end = start + 1;
            while (end < data.size() && (static_cast<unsigned char>(data[end]) & 0xc0) == 0x80) ++end;
            if (std::string_view(data).substr(start, end - start) == point) ++count;
            start = end;
        }
    }
    return count;
}

int ascii_foreach() { return utf8_sweep("a"); }
int hangul_foreach() { return utf8_sweep("\xED\x95\x9C"); }

bool file_io() {
    const std::string chunk(4096, 'x');
    for (int i = 0; i < 64; ++i) {
        std::ofstream out("data.txt", std::ios::binary | std::ios::app);
        out.write(chunk.data(), chunk.size());
        out.close();
        if (!out) return false;
    }
    std::ifstream in("data.txt", std::ios::binary);
    const std::string result{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, result.data(), int(result.size()), nullptr, 0)
        && result == std::string(262144, 'x');
}
