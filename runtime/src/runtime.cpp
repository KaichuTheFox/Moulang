#include <Windows.h>
#include <cstdint>
#include <limits>

extern "C" void moulang_main();

namespace {
const char* errorSite = nullptr;
DWORD errorSiteLength = 0;

void reportErrorText(const char* text, DWORD length) {
    const auto output = GetStdHandle(STD_ERROR_HANDLE);
    DWORD written;
    DWORD mode;
    if (GetConsoleMode(output, &mode)) {
        const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, static_cast<int>(length), nullptr, 0);
        if (size) {
            const auto heap = GetProcessHeap();
            auto* wide = static_cast<wchar_t*>(HeapAlloc(heap, 0, sizeof(wchar_t) * size));
            if (wide) {
                const bool converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                    text, static_cast<int>(length), wide, size) != 0;
                const bool reported = converted && WriteConsoleW(output, wide, size, &written, nullptr);
                HeapFree(heap, 0, wide);
                if (reported) return;
            }
        }
    }
    WriteFile(output, text, length, &written, nullptr);
}
}

extern "C" void moulang_set_error_site(const char* text, int length) {
    errorSite = text;
    errorSiteLength = static_cast<DWORD>(length);
}

extern "C" [[noreturn]] void moulang_runtime_error(const char* message, int length) {
    if (errorSiteLength) reportErrorText(errorSite, errorSiteLength);
    reportErrorText(message, static_cast<DWORD>(length));
    ExitProcess(1);
}

namespace {
template <size_t size>
[[noreturn]] void runtimeError(const char (&message)[size]) {
    moulang_runtime_error(message, static_cast<int>(size - 1));
}

[[noreturn]] void outputError() {
    runtimeError("Moulang runtime: cannot write standard output.\r\n");
}

void writeBytes(HANDLE output, const char* data, DWORD length) {
    while (length > 0) {
        DWORD written;
        if (!WriteFile(output, data, length, &written, nullptr) || written == 0) {
            outputError();
        }
        data += written;
        length -= written;
    }
}

void writeWide(HANDLE output, const wchar_t* data, DWORD length) {
    while (length > 0) {
        DWORD written;
        // Keep console writes small while preserving UTF-16 surrogate pairs.
        DWORD chunk = length > 4096 ? 4096 : length;
        if (chunk < length && data[chunk - 1] >= 0xD800 && data[chunk - 1] <= 0xDBFF) {
            --chunk;
        }
        if (!WriteConsoleW(output, data, chunk, &written, nullptr) || written == 0) {
            outputError();
        }
        data += written;
        length -= written;
    }
}
}

extern "C" void moulang_print(const char* text, int length) {
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    if (!GetConsoleMode(output, &mode)) {
        writeBytes(output, text, length);
        writeBytes(output, "\n", 1);
        return;
    }
    if (length > 0) {
        const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, length, nullptr, 0);
        if (!size) {
            outputError();
        }
        const HANDLE heap = GetProcessHeap();
        auto* wide = static_cast<wchar_t*>(HeapAlloc(heap, 0, sizeof(wchar_t) * size));
        if (!wide || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, length, wide, size)) {
            outputError();
        }
        writeWide(output, wide, size);
        HeapFree(heap, 0, wide);
    }
    writeWide(output, L"\r\n", 2);
}

extern "C" [[noreturn]] void moulang_entry() {
    moulang_main();
    ExitProcess(0);
}

extern "C" void moulang_print_int(int value) {
    char buffer[11];
    char* const end = buffer + sizeof(buffer);
    char* digit = end;
    const bool negative = value < 0;
    unsigned magnitude = static_cast<unsigned>(value);
    if (negative) magnitude = 0u - magnitude;
    do {
        *--digit = static_cast<char>('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude != 0);
    if (negative) *--digit = '-';
    moulang_print(digit, static_cast<int>(end - digit));
}

extern "C" void moulang_print_bool(int value) {
    if (value) {
        moulang_print("true", 4);
    } else {
        moulang_print("false", 5);
    }
}

extern "C" int moulang_divide(int left, int right) {
    if (right == 0) {
        runtimeError("Moulang runtime: division by zero.\r\n");
    }
    if (left == (-2147483647 - 1) && right == -1) return left;
    return left / right;
}

extern "C" int moulang_shift_count(int count, int width) {
    if (count < 0 || count >= width) runtimeError("Moulang runtime: invalid bit shift count.\r\n");
    return count;
}

extern "C" int moulang_shift_count64(std::int64_t count, int width) {
    if (count < 0 || count >= width) runtimeError("Moulang runtime: invalid bit shift count.\r\n");
    return static_cast<int>(count);
}

extern "C" int moulang_checked_int(std::int64_t value) {
    if (value < (std::numeric_limits<int>::min)() || value > (std::numeric_limits<int>::max)())
        runtimeError("Moulang runtime: long value is outside the int range.\r\n");
    return static_cast<int>(value);
}

extern "C" std::int64_t moulang_divide_long(std::int64_t left, std::int64_t right) {
    if (right == 0) runtimeError("Moulang runtime: division by zero.\r\n");
    if (left == (std::numeric_limits<std::int64_t>::min)() && right == -1) return left;
    return left / right;
}

extern "C" void moulang_print_long(std::int64_t value) {
    char buffer[20];
    char* digit = buffer + sizeof(buffer);
    auto magnitude = static_cast<std::uint64_t>(value);
    if (value < 0) magnitude = 0 - magnitude;
    do {
        *--digit = static_cast<char>('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude);
    if (value < 0) *--digit = '-';
    moulang_print(digit, static_cast<int>(buffer + sizeof(buffer) - digit));
}

extern "C" int moulang_string_equal(const char* left, int leftSize, const char* right, int rightSize) {
    if (leftSize != rightSize) return 0;
    for (int i = 0; i < leftSize; ++i) {
        if (left[i] != right[i]) return 0;
    }
    return 1;
}

namespace {
struct SliceBuffer {
    int capacity;
    int used;
    int* elements;
};
}

extern "C" SliceBuffer* moulang_slice_append(SliceBuffer* buffer, int length, int value) {
    constexpr int maximum = 2147483647;
    if (length == maximum) runtimeError("Moulang runtime: slice length limit exceeded.\r\n");
    if (!buffer || buffer->used != length || length == buffer->capacity) {
        const int capacity = length < 4 ? 4 : (length > maximum / 2 ? maximum : length * 2);
        const auto heap = GetProcessHeap();
        auto* next = static_cast<SliceBuffer*>(HeapAlloc(heap, 0, sizeof(SliceBuffer)));
        auto* elements = static_cast<int*>(HeapAlloc(heap, 0, sizeof(int) * static_cast<size_t>(capacity)));
        if (!next || !elements) runtimeError("Moulang runtime: cannot allocate a slice.\r\n");
        for (int i = 0; i < length; ++i) elements[i] = buffer->elements[i];
        next->capacity = capacity;
        next->elements = elements;
        buffer = next;
    }
    buffer->elements[length] = value;
    buffer->used = length + 1;
    return buffer;
}

extern "C" int moulang_slice_get(const SliceBuffer* buffer, int length, int index) {
    if (index < 0 || index >= length) runtimeError("Moulang runtime: slice index out of bounds.\r\n");
    return buffer->elements[index];
}

extern "C" const int* moulang_slice_data(const SliceBuffer* buffer) {
    return buffer ? buffer->elements : nullptr;
}

extern "C" const int* moulang_view_start(const int* data, int length, int begin, int end) {
    if (begin < 0 || end < begin || end > length) {
        runtimeError("Moulang runtime: slice range out of bounds.\r\n");
    }
    return begin ? data + begin : data;
}

extern "C" int moulang_view_get(const int* data, int length, int index) {
    if (index < 0 || index >= length) runtimeError("Moulang runtime: slice index out of bounds.\r\n");
    return data[index];
}

extern "C" int moulang_checked_array_index(int index, int length) {
    if (index < 0 || index >= length) runtimeError("Moulang runtime: fixed array index out of bounds.\r\n");
    return index;
}

extern "C" void* moulang_allocate_object(size_t size) {
    auto* object = HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
    if (!object) runtimeError("Moulang runtime: cannot allocate an object.\r\n");
    return object;
}

namespace {
void textPath(const char* source, int length, wchar_t (&path)[MAX_PATH]) {
    for (int i = 0; i < length; ++i) {
        if (source[i] == '\0') runtimeError("Moulang runtime: invalid text file path.\r\n");
    }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, length, path, MAX_PATH - 1);
    if (!size) runtimeError("Moulang runtime: invalid text file path.\r\n");
    path[size] = L'\0';
}

bool transferText(HANDLE file, char* input, const char* output, int length) {
    int offset = 0;
    while (offset < length) {
        DWORD transferred = 0;
        const auto remaining = static_cast<DWORD>(length - offset);
        const auto chunk = remaining > 65536 ? 65536 : remaining;
        const BOOL success = input
            ? ReadFile(file, input + offset, chunk, &transferred, nullptr)
            : WriteFile(file, output + offset, chunk, &transferred, nullptr);
        if (!success || transferred == 0) return false;
        offset += static_cast<int>(transferred);
    }
    return true;
}
}

extern "C" void moulang_read_text(const char* path, int pathLength, char** text, int* length) {
    wchar_t nativePath[MAX_PATH];
    textPath(path, pathLength, nativePath);
    const auto file = CreateFileW(nativePath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) runtimeError("Moulang runtime: cannot open text file.\r\n");
    LARGE_INTEGER size;
    if (GetFileType(file) != FILE_TYPE_DISK || !GetFileSizeEx(file, &size) ||
        size.QuadPart < 0 || size.QuadPart > 2147483647) {
        CloseHandle(file);
        runtimeError("Moulang runtime: invalid text file size.\r\n");
    }
    const auto count = static_cast<int>(size.QuadPart);
    auto* bytes = static_cast<char*>(HeapAlloc(GetProcessHeap(), 0, count ? count : 1));
    const bool read = bytes && transferText(file, bytes, nullptr, count);
    CloseHandle(file);
    if (!read) runtimeError("Moulang runtime: cannot read text file.\r\n");
    if (count && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, count, nullptr, 0)) {
        runtimeError("Moulang runtime: text file must be valid UTF-8.\r\n");
    }
    *text = bytes;
    *length = count;
}

extern "C" void moulang_write_text(const char* path, int pathLength, const char* text, int length) {
    wchar_t nativePath[MAX_PATH];
    textPath(path, pathLength, nativePath);
    wchar_t absolute[MAX_PATH];
    wchar_t* leaf = nullptr;
    const DWORD size = GetFullPathNameW(nativePath, MAX_PATH, absolute, &leaf);
    if (!size || size >= MAX_PATH || !leaf || !*leaf) {
        runtimeError("Moulang runtime: invalid text file path.\r\n");
    }

    wchar_t temporary[MAX_PATH];
    const wchar_t saved = *leaf;
    *leaf = L'\0';
    const auto created = GetTempFileNameW(absolute, L"MEL", 0, temporary);
    *leaf = saved;
    if (!created) runtimeError("Moulang runtime: cannot create temporary text file.\r\n");
    const auto file = CreateFileW(temporary, GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    bool complete = false;
    if (file != INVALID_HANDLE_VALUE) {
        complete = transferText(file, nullptr, text, length) && FlushFileBuffers(file);
        complete = CloseHandle(file) && complete;
    }
    if (!complete || !MoveFileExW(temporary, absolute, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(temporary);
        runtimeError("Moulang runtime: cannot write text file.\r\n");
    }
}
