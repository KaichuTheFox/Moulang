#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>

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

extern "C" void moulang_output(const char* text, int length) {
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    if (!GetConsoleMode(output, &mode)) {
        writeBytes(output, text, length);
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
}

extern "C" void moulang_newline() {
    const auto output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    if (GetConsoleMode(output, &mode)) writeWide(output, L"\r\n", 2);
    else writeBytes(output, "\n", 1);
}

extern "C" [[noreturn]] void moulang_entry() {
    moulang_main();
    ExitProcess(0);
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

extern "C" int moulang_string_equal(const char* left, int leftSize, const char* right, int rightSize) {
    if (leftSize != rightSize) return 0;
    for (int i = 0; i < leftSize; ++i) {
        if (left[i] != right[i]) return 0;
    }
    return 1;
}

namespace {
struct Text {
    const char* data;
    int length;
};

struct ListItem {
    const void* data;
    int length;
};

struct StringList {
    ListItem* items;
    int size;
    int capacity;
    int depth;
};

// Values can escape functions, so dynamic text stays valid until process exit.
constexpr std::size_t retainedLimit = 256u * 1024u * 1024u;
std::size_t retainedBytes = 0;

void* retain(std::size_t size) {
    if (size > retainedLimit - retainedBytes) {
        runtimeError("Moulang runtime: dynamic text and collection memory limit exceeded.\r\n");
    }
    auto* storage = HeapAlloc(GetProcessHeap(), 0, size);
    if (!storage) runtimeError("Moulang runtime: cannot allocate text or collection storage.\r\n");
    retainedBytes += size;
    return storage;
}

void reserveItems(StringList* list, int capacity) {
    if (capacity <= list->capacity) return;
    const auto size = sizeof(ListItem) * static_cast<std::size_t>(capacity);
    const auto growth = sizeof(ListItem) * static_cast<std::size_t>(capacity - list->capacity);
    if (growth > retainedLimit - retainedBytes) {
        runtimeError("Moulang runtime: dynamic text and collection memory limit exceeded.\r\n");
    }
    const auto heap = GetProcessHeap();
    auto* data = list->items ? HeapReAlloc(heap, 0, list->items, size) : HeapAlloc(heap, 0, size);
    if (!data) runtimeError("Moulang runtime: cannot grow string list.\r\n");
    retainedBytes += growth;
    list->items = static_cast<ListItem*>(data);
    list->capacity = capacity;
}

int listIndex(const StringList* list, int index, bool allowEnd = false) {
    if (index < 0) index += list->size;
    if (index < 0 || index > list->size || (!allowEnd && index == list->size)) {
        runtimeError("Moulang runtime: string list index out of bounds.\r\n");
    }
    return index;
}

void insertItem(StringList* list, int index, ListItem item) {
    if (list->size == list->capacity) {
        const int extra = list->capacity / 2 + 1;
        reserveItems(list, list->capacity + extra);
    }
    auto* slot = list->items + index;
    std::memmove(slot + 1, slot, sizeof(ListItem) * (list->size - index));
    ::new (slot) ListItem(item);
    ++list->size;
}

int compareItems(ListItem left, ListItem right) {
    const int length = (std::min)(left.length, right.length);
    const int bytes = length ? std::memcmp(left.data, right.data, length) : 0;
    return bytes ? bytes : left.length - right.length;
}

void printListText(ListItem item) {
    const auto* text = static_cast<const char*>(item.data);
    moulang_output("\"", 1);
    int start = 0;
    for (int i = 0; i < item.length; ++i) {
        const auto byte = static_cast<unsigned char>(text[i]);
        if (byte >= 0x20 && byte != 0x7f && byte != '"' && byte != '\\') continue;
        moulang_output(text + start, i - start);
        switch (byte) {
        case '\0': moulang_output("\\0", 2); break;
        case '\r': moulang_output("\\r", 2); break;
        case '\n': moulang_output("\\n", 2); break;
        case '\t': moulang_output("\\t", 2); break;
        case '"': moulang_output("\\\"", 2); break;
        case '\\': moulang_output("\\\\", 2); break;
        default: {
            constexpr char hex[] = "0123456789ABCDEF";
            const char escape[] = {'\\', 'x', hex[byte >> 4], hex[byte & 15]};
            moulang_output(escape, 4);
        }
        }
        start = i + 1;
    }
    moulang_output(text + start, item.length - start);
    moulang_output("\"", 1);
}

char* textStorage(std::int64_t length) {
    if (length > (std::numeric_limits<int>::max)()) {
        runtimeError("Moulang runtime: string byte length overflow.\r\n");
    }
    return static_cast<char*>(retain(static_cast<std::size_t>(length)));
}

bool continuation(char byte) {
    return (static_cast<unsigned char>(byte) & 0xc0) == 0x80;
}

int nextPoint(const char* data, int length, int offset) {
    do { ++offset; } while (offset < length && continuation(data[offset]));
    return offset;
}

int previousPoint(const char* data, int offset) {
    do { --offset; } while (offset > 0 && continuation(data[offset]));
    return offset;
}

int pointCount(const char* data, int length) {
    int count = 0;
    for (int i = 0; i < length; ++i) if (!continuation(data[i])) ++count;
    return count;
}

int pointOffset(const char* data, int length, int point) {
    int offset = 0;
    for (int i = 0; i < point; ++i) offset = nextPoint(data, length, offset);
    return offset;
}

void formatPass(const char* pattern, int length, const Text* values, int count, bool emit) {
    int next = 0;
    int start = 0;
    for (int i = 0; i < length; ++i) {
        const char brace = pattern[i];
        if (brace != '{' && brace != '}') continue;
        const char following = i + 1 < length ? pattern[i + 1] : '\0';
        if (emit) moulang_output(pattern + start, i - start);
        if (following == brace) {
            if (emit) moulang_output(pattern + i, 1);
        } else if (brace == '{' && following == '}') {
            if (next == count) runtimeError("Moulang runtime: format has more placeholders than arguments.\r\n");
            if (emit) moulang_output(values[next].data, values[next].length);
            ++next;
        } else {
            runtimeError("Moulang runtime: invalid format brace; use {}, {{ or }}.\r\n");
        }
        ++i;
        start = i + 1;
    }
    if (next != count) runtimeError("Moulang runtime: format has fewer placeholders than arguments.\r\n");
    if (emit) moulang_output(pattern + start, length - start);
}

constexpr int inputLimit = 1024 * 1024;

template<class Unit, class Reader>
int readLine(Unit* line, Reader read) {
    static Unit buffer[4096];
    static DWORD cursor = 0;
    static DWORD available = 0;
    int length = 0;
    for (;;) {
        if (cursor == available) {
            cursor = 0;
            available = read(buffer);
            if (!available) {
                if (!length) runtimeError("Moulang runtime: end of standard input.\r\n");
                break;
            }
        }
        const Unit unit = buffer[cursor++];
        if (unit == '\n') break;
        if (length > inputLimit) runtimeError("Moulang runtime: input line exceeds 1 MiB.\r\n");
        line[length++] = unit;
    }
    if (length && line[length - 1] == '\r') --length;
    if (length > inputLimit) runtimeError("Moulang runtime: input line exceeds 1 MiB.\r\n");
    return length;
}

int consoleLine(HANDLE handle, char* line) {
    static wchar_t wide[inputLimit + 1];
    const int units = readLine(wide, [handle](wchar_t* buffer) {
        DWORD read = 0;
        if (!ReadConsoleW(handle, buffer, 4096, &read, nullptr)) {
            runtimeError("Moulang runtime: cannot read standard input.\r\n");
        }
        return read;
    });
    if (!units) return 0;
    if (units == 1 && wide[0] == 0x1a) runtimeError("Moulang runtime: end of standard input.\r\n");
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, units, nullptr, 0, nullptr, nullptr);
    if (!bytes) runtimeError("Moulang runtime: invalid Unicode on standard input.\r\n");
    if (bytes > inputLimit) runtimeError("Moulang runtime: input line exceeds 1 MiB.\r\n");
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, units, line, bytes, nullptr, nullptr)) {
        runtimeError("Moulang runtime: invalid Unicode on standard input.\r\n");
    }
    return bytes;
}
}

extern "C" StringList* moulang_list_new(int capacity, int depth) {
    auto* list = ::new (retain(sizeof(StringList))) StringList{nullptr, 0, 0, depth};
    reserveItems(list, capacity);
    return list;
}

extern "C" int moulang_list_len(const StringList* list) { return list->size; }

extern "C" void moulang_list_get(const StringList* list, int index, ListItem* result) {
    *result = list->items[listIndex(list, index)];
}

extern "C" void moulang_list_set(StringList* list, int index, const void* data, int length) {
    list->items[listIndex(list, index)] = {data, length};
}

extern "C" void moulang_list_append(StringList* list, const void* data, int length) {
    insertItem(list, list->size, {data, length});
}

extern "C" void moulang_list_insert(StringList* list, int index, const void* data, int length) {
    insertItem(list, listIndex(list, index, true), {data, length});
}

extern "C" void moulang_list_remove(StringList* list, const void* data, int length) {
    for (int i = 0; i < list->size; ++i) {
        auto* slot = list->items + i;
        if (compareItems(*slot, {data, length}) == 0) {
            --list->size;
            std::memmove(slot, slot + 1, sizeof(ListItem) * (list->size - i));
            return;
        }
    }
    runtimeError("Moulang runtime: string list does not contain the value to remove.\r\n");
}

extern "C" void moulang_list_sort(StringList* list) {
    if (list->size < 2) return;
    std::sort(list->items, list->items + list->size,
        [](ListItem left, ListItem right) { return compareItems(left, right) < 0; });
}

extern "C" void moulang_list_pop(StringList* list, ListItem* result) {
    if (!list->size) runtimeError("Moulang runtime: cannot pop an empty string list.\r\n");
    *result = list->items[--list->size];
}

extern "C" void moulang_list_print(const StringList* list) {
    moulang_output("[", 1);
    for (int i = 0; i < list->size; ++i) {
        if (i) moulang_output(", ", 2);
        const auto item = list->items[i];
        if (list->depth == 1) printListText(item);
        else moulang_list_print(static_cast<const StringList*>(item.data));
    }
    moulang_output("]", 1);
}

extern "C" void moulang_format_output(const char* pattern, int length, const Text* values, int count) {
    formatPass(pattern, length, values, count, false);
    formatPass(pattern, length, values, count, true);
}

extern "C" void moulang_read_line(int save, Text* result) {
    static char line[inputLimit + 1];
    const HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    int length;
    if (GetConsoleMode(handle, &mode)) {
        length = consoleLine(handle, line);
    } else {
        length = readLine(line, [handle](char* buffer) {
            DWORD read = 0;
            if (!ReadFile(handle, buffer, 4096, &read, nullptr)) {
                const DWORD error = GetLastError();
                if (error == ERROR_BROKEN_PIPE || error == ERROR_HANDLE_EOF) return DWORD{0};
                runtimeError("Moulang runtime: cannot read standard input.\r\n");
            }
            return read;
        });
        if (length && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, line, length, nullptr, 0)) {
            runtimeError("Moulang runtime: standard input is not valid UTF-8.\r\n");
        }
    }
    if (save && length) {
        auto* data = textStorage(length);
        std::memcpy(data, line, length);
        *result = {data, length};
    } else {
        *result = {length ? line : "", length};
    }
}

extern "C" void moulang_text_concat(const char* left, int leftLength, const char* right, int rightLength, Text* result) {
    if (!leftLength) { *result = {right, rightLength}; return; }
    if (!rightLength) { *result = {left, leftLength}; return; }
    const auto length = static_cast<std::int64_t>(leftLength) + rightLength;
    auto* data = textStorage(length);
    std::memcpy(data, left, leftLength);
    std::memcpy(data + leftLength, right, rightLength);
    *result = {data, static_cast<int>(length)};
}

extern "C" void moulang_text_repeat(const char* source, int size, int count, Text* result) {
    if (count < 0) runtimeError("Moulang runtime: string repetition count must be nonnegative.\r\n");
    if (!size || !count) { *result = {"", 0}; return; }
    if (count == 1) { *result = {source, size}; return; }
    const auto length = static_cast<std::int64_t>(size) * count;
    auto* data = textStorage(length);
    std::memcpy(data, source, size);
    for (std::int64_t written = size; written < length;) {
        const auto chunk = (std::min)(written, length - written);
        std::memcpy(data + written, data, static_cast<std::size_t>(chunk));
        written += chunk;
    }
    *result = {data, static_cast<int>(length)};
}

extern "C" void moulang_text_index(const char* data, int length, int index, Text* result) {
    const int count = pointCount(data, length);
    if (index < 0) index += count;
    if (index < 0 || index >= count) runtimeError("Moulang runtime: string index out of bounds.\r\n");
    const int start = pointOffset(data, length, index);
    *result = {data + start, nextPoint(data, length, start) - start};
}

extern "C" void moulang_text_slice(const char* data, int length, int start, int end, int step,
    int supplied, Text* result) {
    if (!step) runtimeError("Moulang runtime: string slice step must not be zero.\r\n");
    const int count = pointCount(data, length);
    const int low = step > 0 ? 0 : -1;
    const int high = step > 0 ? count : count - 1;
    const auto bound = [=](int value) { return std::clamp(value < 0 ? value + count : value, low, high); };
    start = supplied & 1 ? bound(start) : (step > 0 ? low : high);
    end = supplied & 2 ? bound(end) : (step > 0 ? high : low);
    if (step > 0 ? start >= end : start <= end) { *result = {"", 0}; return; }
    const int first = pointOffset(data, length, start);
    if (step == 1) {
        *result = {data + first, pointOffset(data, length, end) - first};
        return;
    }
    const auto gather = [&](char* output) {
        int offset = first;
        int size = 0;
        for (int point = start; step > 0 ? point < end : point > end;) {
            const int after = nextPoint(data, length, offset);
            if ((static_cast<std::int64_t>(point) - start) % step == 0) {
                if (output) std::memcpy(output + size, data + offset, after - offset);
                size += after - offset;
            }
            if (step > 0) { ++point; offset = after; }
            else if (--point > end) offset = previousPoint(data, offset);
        }
        return size;
    };
    const int size = gather(nullptr);
    auto* output = textStorage(size);
    gather(output);
    *result = {output, size};
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

extern "C" void moulang_append_text(const char* path, int pathLength, const char* text, int length) {
    wchar_t nativePath[MAX_PATH];
    textPath(path, pathLength, nativePath);
    const auto file = CreateFileW(nativePath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) runtimeError("Moulang runtime: cannot open text file for append.\r\n");
    const bool written = GetFileType(file) == FILE_TYPE_DISK && transferText(file, nullptr, text, length);
    if (!CloseHandle(file) || !written) runtimeError("Moulang runtime: cannot append to text file.\r\n");
}
