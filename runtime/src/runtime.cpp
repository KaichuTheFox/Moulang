#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#ifdef MOULANG_ARC_DEBUG
#include <charconv>
#include <unordered_map>
#include <vector>
#endif

extern "C" void moulang_main();
extern "C" int moulang_render_long(std::int64_t value, char* buffer);
extern "C" void moulang_std_write_utc_date(std::int64_t value, char* output);

namespace {
const char* errorSite = nullptr;
DWORD errorSiteLength = 0;

#ifdef MOULANG_ARC_DEBUG
bool arcReporting() {
    wchar_t enabled[2]{};
    return GetEnvironmentVariableW(L"MOULANG_ARC_REPORT", enabled, 2) == 1 && enabled[0] == L'1';
}
#endif

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
#ifdef MOULANG_ARC_DEBUG
    if (arcReporting()) {
        constexpr char note[] = "Moulang ARC debug: aborted before normal leak report.\r\n";
        reportErrorText(note, sizeof(note) - 1);
    }
#endif
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
enum class ArcKind { text, list, intList, intBuffer };

struct ArcHeader {
    std::size_t references;
    std::size_t bytes;
    std::size_t charge;
};

struct Text {
    const char* data;
    int length;
    int ascii;
    void* owner = nullptr;
};

struct ListItem {
    const void* data;
    int length;
    int ascii;
    void* owner = nullptr;
};

struct ListIteration {
    std::uint64_t version = 0;
    unsigned iterators = 0;
};

struct StringList {
    ArcHeader arc;
    ListItem* items;
    int size;
    int capacity;
    int depth;
    ListIteration iteration{};
};

struct IntList {
    ArcHeader arc;
    void* buffer = nullptr;
    int size = 0;
    int capacity = 0;
    ListIteration iteration{};
};

struct IntListView {
    const int* data;
    int size;
    void* owner;
};

constexpr std::size_t liveLimit = 256u * 1024u * 1024u;
std::size_t chargedBytes = 0;

#ifdef MOULANG_ARC_DEBUG
struct DebugObject {
    void* storage;
    ArcKind kind;
};

std::unordered_map<std::uintptr_t, DebugObject> liveObjects;
std::uintptr_t nextId = 0;
std::size_t trackedBytes = 0;
void* quarantine[64]{};
std::size_t quarantineSlot = 0;

struct Root {
    void* object;
    void (*releaseFields)(void*);
};
std::vector<Root> classRoots;
#endif

void checkBudget(std::size_t size) {
    if (size > liveLimit - chargedBytes) {
        runtimeError("Moulang runtime: dynamic text and collection memory limit exceeded.\r\n");
    }
}

void* allocateArc(std::size_t size, std::size_t charge) {
    checkBudget(charge);
    auto* storage = HeapAlloc(GetProcessHeap(), 0, size);
    if (!storage) runtimeError("Moulang runtime: cannot allocate text or collection storage.\r\n");
    chargedBytes += charge;
    return storage;
}

void* arcHandle(void* storage, ArcKind kind) {
#ifdef MOULANG_ARC_DEBUG
    if (nextId == (std::numeric_limits<std::uintptr_t>::max)()) {
        runtimeError("Moulang ARC debug: allocation identifier overflow.\r\n");
    }
    const auto id = ++nextId;
    liveObjects.emplace(id, DebugObject{storage, kind});
    trackedBytes += static_cast<ArcHeader*>(storage)->bytes;
    return reinterpret_cast<void*>(id);
#else
    (void)kind;
    return storage;
#endif
}

void* resolveArc(void* handle, ArcKind kind) {
#ifdef MOULANG_ARC_DEBUG
    const auto found = liveObjects.find(reinterpret_cast<std::uintptr_t>(handle));
    if (found == liveObjects.end() || found->second.kind != kind) {
        runtimeError("Moulang ARC debug: stale or invalid ownership handle.\r\n");
    }
    return found->second.storage;
#else
    (void)kind;
    return handle;
#endif
}

#ifdef MOULANG_ARC_DEBUG
void validateText(const char* data, int length, void* owner) {
    if (!owner) return;
    const auto* header = static_cast<const ArcHeader*>(resolveArc(owner, ArcKind::text));
    const auto begin = reinterpret_cast<std::uintptr_t>(header + 1);
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    const auto capacity = header->bytes - sizeof(ArcHeader);
    if (length < 0 || address < begin || address - begin > capacity ||
        static_cast<std::size_t>(length) > capacity - (address - begin)) {
        runtimeError("Moulang ARC debug: text lies outside its backing buffer.\r\n");
    }
}
#endif

void disposeStorage(void* storage, std::size_t size) {
    const auto heap = GetProcessHeap();
#ifdef MOULANG_ARC_DEBUG
    std::memset(storage, 0xdd, size);
    if (size <= 16384) {
        auto& previous = quarantine[quarantineSlot];
        if (previous) HeapFree(heap, 0, previous);
        previous = storage;
        quarantineSlot = (quarantineSlot + 1) % std::size(quarantine);
        return;
    }
#else
    (void)size;
#endif
    HeapFree(heap, 0, storage);
}

void disposeArc(void* handle, ArcHeader* arc) {
    const auto bytes = arc->bytes;
    chargedBytes -= arc->charge;
#ifdef MOULANG_ARC_DEBUG
    liveObjects.erase(reinterpret_cast<std::uintptr_t>(handle));
    trackedBytes -= bytes;
#else
    (void)handle;
#endif
    disposeStorage(arc, bytes);
}

void retainArc(void* handle, ArcKind kind) {
    if (!handle) return;
    auto* arc = static_cast<ArcHeader*>(resolveArc(handle, kind));
    if (arc->references == (std::numeric_limits<std::size_t>::max)()) {
        runtimeError("Moulang runtime: reference count overflow.\r\n");
    }
    ++arc->references;
}

void releaseBuffer(void* handle, ArcKind kind) {
    if (!handle) return;
    auto* arc = static_cast<ArcHeader*>(resolveArc(handle, kind));
    if (!--arc->references) disposeArc(handle, arc);
}

void releaseText(void* handle) { releaseBuffer(handle, ArcKind::text); }

void releaseList(void* handle);

void retainItem(const StringList* list, ListItem item) {
    if (list->depth == 1) retainArc(item.owner, ArcKind::text);
    else retainArc(const_cast<void*>(item.data), ArcKind::list);
}

void releaseItem(const StringList* list, ListItem item) {
    if (list->depth == 1) releaseText(item.owner);
    else releaseList(const_cast<void*>(item.data));
}

void releaseList(void* handle) {
    if (!handle) return;
    auto* list = static_cast<StringList*>(resolveArc(handle, ArcKind::list));
    if (--list->arc.references) return;
    for (int i = 0; i < list->size; ++i) releaseItem(list, list->items[i]);
    const auto capacityBytes = sizeof(ListItem) * static_cast<std::size_t>(list->capacity);
    if (list->items) disposeStorage(list->items, capacityBytes);
    chargedBytes -= capacityBytes;
#ifdef MOULANG_ARC_DEBUG
    trackedBytes -= capacityBytes;
#endif
    disposeArc(handle, &list->arc);
}

void reserveItems(StringList* list, int capacity) {
    if (capacity <= list->capacity) return;
    const auto size = sizeof(ListItem) * static_cast<std::size_t>(capacity);
    const auto growth = sizeof(ListItem) * static_cast<std::size_t>(capacity - list->capacity);
    checkBudget(growth);
    const auto heap = GetProcessHeap();
    auto* data = list->items ? HeapReAlloc(heap, 0, list->items, size) : HeapAlloc(heap, 0, size);
    if (!data) runtimeError("Moulang runtime: cannot grow string list.\r\n");
    chargedBytes += growth;
#ifdef MOULANG_ARC_DEBUG
    trackedBytes += growth;
#endif
    list->items = static_cast<ListItem*>(data);
    list->capacity = capacity;
}

template <size_t size>
int listIndex(int length, int index, bool allowEnd, const char (&message)[size]) {
    if (index < 0) index += length;
    if (index < 0 || index > length || (!allowEnd && index == length)) {
        runtimeError(message);
    }
    return index;
}

int listIndex(const StringList* list, int index, bool allowEnd = false) {
    return listIndex(list->size, index, allowEnd, "Moulang runtime: string list index out of bounds.\r\n");
}

int listIndex(const IntList* list, int index, bool allowEnd = false) {
    return listIndex(list->size, index, allowEnd, "Moulang runtime: int list index out of bounds.\r\n");
}

void checkStructuralChange(const ListIteration& iteration) {
    if (iteration.iterators) {
        runtimeError("Moulang runtime: cannot structurally change a list during foreach.\r\n");
    }
    if (iteration.version == (std::numeric_limits<std::uint64_t>::max)()) {
        runtimeError("Moulang runtime: list version overflow.\r\n");
    }
}

std::uint64_t beginIteration(ListIteration& iteration) {
    if (iteration.iterators == (std::numeric_limits<unsigned>::max)()) {
        runtimeError("Moulang runtime: too many active list iterators.\r\n");
    }
    ++iteration.iterators;
    return iteration.version;
}

void endIteration(ListIteration& iteration) {
    if (!iteration.iterators) runtimeError("Moulang runtime: no active list iterator to end.\r\n");
    --iteration.iterators;
}

void checkIterationVersion(const ListIteration& iteration, std::uint64_t version) {
    if (iteration.version != version) {
        runtimeError("Moulang runtime: list structure changed during foreach.\r\n");
    }
}

int growListCapacity(int capacity) { return capacity + capacity / 2 + 1; }

void insertItem(StringList* list, int index, ListItem item) {
    checkStructuralChange(list->iteration);
    if (list->size == list->capacity) {
        reserveItems(list, growListCapacity(list->capacity));
    }
    retainItem(list, item);
    auto* slot = list->items + index;
    std::memmove(slot + 1, slot, sizeof(ListItem) * (list->size - index));
    ::new (slot) ListItem(item);
    ++list->size;
    ++list->iteration.version;
}

IntList* intList(void* handle) {
    return static_cast<IntList*>(resolveArc(handle, ArcKind::intList));
}

ArcHeader* intBuffer(const IntList* list) {
    return list->buffer ? static_cast<ArcHeader*>(resolveArc(list->buffer, ArcKind::intBuffer)) : nullptr;
}

int* intBufferData(ArcHeader* buffer) { return buffer ? reinterpret_cast<int*>(buffer + 1) : nullptr; }

int* writableInts(IntList* list, int minimumCapacity) {
    auto* previous = intBuffer(list);
    if (minimumCapacity <= list->capacity && (!previous || previous->references == 1)) {
        return intBufferData(previous);
    }
    // Views pin the old buffer, while aliases continue to share the list header.
    const int capacity = (std::max)(minimumCapacity, list->capacity);
    const auto bytes = sizeof(ArcHeader) + sizeof(int) * static_cast<std::size_t>(capacity);
    auto* next = ::new (allocateArc(bytes, bytes)) ArcHeader{1, bytes, bytes};
    const auto handle = arcHandle(next, ArcKind::intBuffer);
    auto* data = intBufferData(next);
    if (list->size) std::memcpy(data, intBufferData(previous), sizeof(int) * static_cast<std::size_t>(list->size));
    releaseBuffer(list->buffer, ArcKind::intBuffer);
    list->buffer = handle;
    list->capacity = capacity;
    return data;
}

void insertInt(IntList* list, int index, int value) {
    checkStructuralChange(list->iteration);
    const int capacity = list->size == list->capacity ? growListCapacity(list->capacity) : list->capacity;
    auto* slot = writableInts(list, capacity) + index;
    std::memmove(slot + 1, slot, sizeof(int) * (list->size - index));
    *slot = value;
    ++list->size;
    ++list->iteration.version;
}

int compareItems(ListItem left, ListItem right) {
#ifdef MOULANG_ARC_DEBUG
    validateText(static_cast<const char*>(left.data), left.length, left.owner);
    validateText(static_cast<const char*>(right.data), right.length, right.owner);
#endif
    const int length = (std::min)(left.length, right.length);
    const int bytes = length ? std::memcmp(left.data, right.data, length) : 0;
    return bytes ? bytes : left.length - right.length;
}

void printListText(ListItem item) {
    const auto* text = static_cast<const char*>(item.data);
#ifdef MOULANG_ARC_DEBUG
    validateText(text, item.length, item.owner);
#endif
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

bool isAscii(const char* data, int length) {
    return !length || std::all_of(data, data + length, [](unsigned char byte) { return byte < 0x80; });
}

char* textStorage(std::int64_t length, Text* result, bool budgeted = true) {
    if (length > (std::numeric_limits<int>::max)()) {
        runtimeError("Moulang runtime: string byte length overflow.\r\n");
    }
    if (!length) {
        *result = {"", 0, true};
        return nullptr;
    }
    const auto bytes = sizeof(ArcHeader) + static_cast<std::size_t>(length);
    const auto charge = budgeted ? bytes : 0;
    auto* header = ::new (allocateArc(bytes, charge)) ArcHeader{1, bytes, charge};
    auto* data = reinterpret_cast<char*>(header + 1);
    *result = {data, static_cast<int>(length), false, arcHandle(header, ArcKind::text)};
    return data;
}

Text shareText(const char* data, int length, int ascii, void* owner) {
    retainArc(owner, ArcKind::text);
    return {data, length, ascii, owner};
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

extern "C" void moulang_text_retain(void* handle) { retainArc(handle, ArcKind::text); }
extern "C" void moulang_text_release(void* handle) { releaseText(handle); }
extern "C" void moulang_list_retain(void* handle) { retainArc(handle, ArcKind::list); }
extern "C" void moulang_list_release(void* handle) { releaseList(handle); }

#ifdef MOULANG_ARC_DEBUG
extern "C" void moulang_text_validate(const char* data, int length, void* owner) {
    validateText(data, length, owner);
}

extern "C" void moulang_arc_register_root(void* object, void (*releaseFields)(void*)) {
    if (arcReporting()) classRoots.push_back({object, releaseFields});
}

extern "C" void moulang_arc_finish() {
    if (!arcReporting()) return;
    const auto objectsBefore = liveObjects.size();
    const auto bytesBefore = trackedBytes;
    for (const auto& root : classRoots) root.releaseFields(root.object);
    char line[256];
    auto* next = line;
    const auto append = [&](const char* label, std::size_t value) {
        const auto length = std::strlen(label);
        std::memcpy(next, label, length);
        next = std::to_chars(next + length, line + sizeof(line), value).ptr;
    };
    append("Moulang ARC debug: live_objects=", liveObjects.size());
    append(" live_bytes=", trackedBytes);
    append(" class_roots=", classRoots.size());
    append(" rooted_objects=", objectsBefore - liveObjects.size());
    append(" rooted_bytes=", bytesBefore - trackedBytes);
    *next++ = '\r';
    *next++ = '\n';
    reportErrorText(line, static_cast<DWORD>(next - line));
}
#endif

extern "C" void* moulang_list_new(int capacity, int depth) {
    constexpr auto size = sizeof(StringList);
    auto* list = ::new (allocateArc(size, size)) StringList{{1, size, size}, nullptr, 0, 0, depth};
    const auto handle = arcHandle(list, ArcKind::list);
    reserveItems(list, capacity);
    return handle;
}

extern "C" int moulang_list_len(void* handle) {
    return static_cast<const StringList*>(resolveArc(handle, ArcKind::list))->size;
}

extern "C" std::uint64_t moulang_list_begin(void* handle) {
    auto* list = static_cast<StringList*>(resolveArc(handle, ArcKind::list));
    return beginIteration(list->iteration);
}

extern "C" void moulang_list_end(void* handle) {
    auto* list = static_cast<StringList*>(resolveArc(handle, ArcKind::list));
    endIteration(list->iteration);
}

extern "C" void moulang_list_get(void* handle, int index, ListItem* result) {
    const auto* list = static_cast<const StringList*>(resolveArc(handle, ArcKind::list));
    *result = list->items[listIndex(list, index)];
    retainItem(list, *result);
}

extern "C" void moulang_list_next(void* handle, std::uint64_t version, int* cursor, ListItem* result) {
    const auto* list = static_cast<const StringList*>(resolveArc(handle, ArcKind::list));
    checkIterationVersion(list->iteration, version);
    *result = list->items[listIndex(list, *cursor)];
    retainItem(list, *result);
    ++*cursor;
}

extern "C" void moulang_list_set(void* handle, int index, const void* data, int length, int ascii, void* owner) {
    auto* list = static_cast<StringList*>(resolveArc(handle, ArcKind::list));
    auto& slot = list->items[listIndex(list, index)];
    const ListItem item{data, length, ascii, owner};
    retainItem(list, item);
    releaseItem(list, slot);
    slot = item;
}

extern "C" void moulang_list_append(void* handle, const void* data, int length, int ascii, void* owner) {
    auto* list = static_cast<StringList*>(resolveArc(handle, ArcKind::list));
    insertItem(list, list->size, {data, length, ascii, owner});
}

extern "C" void moulang_list_insert(void* handle, int index, const void* data, int length, int ascii, void* owner) {
    auto* list = static_cast<StringList*>(resolveArc(handle, ArcKind::list));
    insertItem(list, listIndex(list, index, true), {data, length, ascii, owner});
}

extern "C" void moulang_list_remove(void* handle, const void* data, int length, int ascii, void* owner) {
    auto* list = static_cast<StringList*>(resolveArc(handle, ArcKind::list));
    for (int i = 0; i < list->size; ++i) {
        auto* slot = list->items + i;
        if (compareItems(*slot, {data, length, ascii, owner}) == 0) {
            checkStructuralChange(list->iteration);
            releaseItem(list, *slot);
            --list->size;
            std::memmove(slot, slot + 1, sizeof(ListItem) * (list->size - i));
            ++list->iteration.version;
            return;
        }
    }
    runtimeError("Moulang runtime: string list does not contain the value to remove.\r\n");
}

extern "C" void moulang_list_sort(void* handle) {
    auto* list = static_cast<StringList*>(resolveArc(handle, ArcKind::list));
    if (list->size < 2) return;
    const auto less = [](ListItem left, ListItem right) { return compareItems(left, right) < 0; };
    if (list->iteration.iterators && std::is_sorted(list->items, list->items + list->size, less)) return;
    checkStructuralChange(list->iteration);
    std::sort(list->items, list->items + list->size, less);
    ++list->iteration.version;
}

extern "C" void moulang_list_pop(void* handle, ListItem* result) {
    auto* list = static_cast<StringList*>(resolveArc(handle, ArcKind::list));
    if (!list->size) runtimeError("Moulang runtime: cannot pop an empty string list.\r\n");
    checkStructuralChange(list->iteration);
    *result = list->items[--list->size];
    ++list->iteration.version;
}

extern "C" void moulang_list_print(void* handle) {
    const auto* list = static_cast<const StringList*>(resolveArc(handle, ArcKind::list));
    moulang_output("[", 1);
    for (int i = 0; i < list->size; ++i) {
        if (i) moulang_output(", ", 2);
        const auto item = list->items[i];
        if (list->depth == 1) printListText(item);
        else moulang_list_print(const_cast<void*>(item.data));
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
        auto* data = textStorage(length, result);
        std::memcpy(data, line, length);
        result->ascii = isAscii(data, length);
    } else {
        *result = {length ? line : "", length, isAscii(line, length)};
    }
}

extern "C" void moulang_text_concat(const char* left, int leftLength, int leftAscii, void* leftOwner,
    const char* right, int rightLength, int rightAscii, void* rightOwner, Text* result) {
#ifdef MOULANG_ARC_DEBUG
    validateText(left, leftLength, leftOwner);
    validateText(right, rightLength, rightOwner);
#endif
    if (!leftLength) { *result = shareText(right, rightLength, rightAscii, rightOwner); return; }
    if (!rightLength) { *result = shareText(left, leftLength, leftAscii, leftOwner); return; }
    const auto length = static_cast<std::int64_t>(leftLength) + rightLength;
    auto* data = textStorage(length, result);
    std::memcpy(data, left, leftLength);
    std::memcpy(data + leftLength, right, rightLength);
    result->ascii = leftAscii && rightAscii;
}

extern "C" void moulang_text_repeat(const char* source, int size, int ascii, void* owner, int count, Text* result) {
#ifdef MOULANG_ARC_DEBUG
    validateText(source, size, owner);
#endif
    if (count < 0) runtimeError("Moulang runtime: string repetition count must be nonnegative.\r\n");
    if (!size || !count) { *result = {"", 0, true}; return; }
    if (count == 1) { *result = shareText(source, size, ascii, owner); return; }
    const auto length = static_cast<std::int64_t>(size) * count;
    auto* data = textStorage(length, result);
    std::memcpy(data, source, size);
    for (std::int64_t written = size; written < length;) {
        const auto chunk = (std::min)(written, length - written);
        std::memcpy(data + written, data, static_cast<std::size_t>(chunk));
        written += chunk;
    }
    result->ascii = ascii;
}

extern "C" void moulang_text_index(const char* data, int length, int ascii, void* owner, int index, Text* result) {
#ifdef MOULANG_ARC_DEBUG
    validateText(data, length, owner);
#endif
    if (ascii) {
        if (index < 0) index += length;
        if (index >= 0 && index < length) {
            *result = shareText(data + index, 1, true, owner);
            return;
        }
    } else if (index >= -length && index < length) {
        // The byte length bounds the number of code points, including for a shared slice.
        if (index < 0) {
            int start = length;
            do {
                const int end = start;
                start = previousPoint(data, start);
                if (++index == 0) {
                    *result = shareText(data + start, end - start, end - start == 1, owner);
                    return;
                }
            } while (start > 0);
        } else {
            int start = 0;
            while (index > 0 && start < length) {
                start = nextPoint(data, length, start);
                --index;
            }
            if (start < length) {
                const int size = nextPoint(data, length, start) - start;
                *result = shareText(data + start, size, size == 1, owner);
                return;
            }
        }
    }
    runtimeError("Moulang runtime: string index out of bounds.\r\n");
}

extern "C" void moulang_text_next(const char* data, int length, void* owner, int* cursor, Text* result) {
#ifdef MOULANG_ARC_DEBUG
    validateText(data, length, owner);
    if (*cursor < 0 || *cursor >= length) {
        runtimeError("Moulang ARC debug: invalid string iteration cursor.\r\n");
    }
#endif
    const auto start = *cursor;
    const auto end = nextPoint(data, length, start);
    *result = shareText(data + start, end - start, end - start == 1, owner);
    *cursor = end;
}

extern "C" void moulang_text_slice(const char* data, int length, int ascii, void* owner, int start, int end, int step,
    int supplied, Text* result) {
#ifdef MOULANG_ARC_DEBUG
    validateText(data, length, owner);
#endif
    if (!step) runtimeError("Moulang runtime: string slice step must not be zero.\r\n");
    const int count = pointCount(data, length);
    const int low = step > 0 ? 0 : -1;
    const int high = step > 0 ? count : count - 1;
    const auto bound = [=](int value) { return std::clamp(value < 0 ? value + count : value, low, high); };
    start = supplied & 1 ? bound(start) : (step > 0 ? low : high);
    end = supplied & 2 ? bound(end) : (step > 0 ? high : low);
    if (step > 0 ? start >= end : start <= end) { *result = {"", 0, true}; return; }
    const int first = pointOffset(data, length, start);
    if (step == 1) {
        const auto size = pointOffset(data, length, end) - first;
        *result = shareText(data + first, size, ascii || isAscii(data + first, size), owner);
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
    auto* output = textStorage(size, result);
    gather(output);
    result->ascii = ascii || isAscii(output, size);
}

namespace {
std::string_view textBytes(const Text& text) {
#ifdef MOULANG_ARC_DEBUG
    validateText(text.data, text.length, text.owner);
#endif
    return {text.data ? text.data : "", static_cast<std::size_t>(text.length)};
}

void copyBytes(char*& output, std::string_view bytes) {
    if (bytes.empty()) return;
    std::memcpy(output, bytes.data(), bytes.size());
    output += bytes.size();
}

void mapAscii(const Text& value, char first, char last, int shift, Text* result) {
    const auto bytes = textBytes(value);
    const auto changes = [=](char byte) { return byte >= first && byte <= last; };
    if (std::none_of(bytes.begin(), bytes.end(), changes)) {
        *result = shareText(bytes.data(), value.length, value.ascii, value.owner);
        return;
    }
    auto* output = textStorage(value.length, result);
    std::transform(bytes.begin(), bytes.end(), output,
        [=](char byte) { return changes(byte) ? static_cast<char>(byte + shift) : byte; });
    result->ascii = value.ascii;
}
}

extern "C" int moulang_std_text_len(const Text* value) {
    const auto bytes = textBytes(*value);
    return value->ascii ? value->length : pointCount(bytes.data(), value->length);
}

extern "C" int moulang_std_text_find(const Text* value, const Text* needle) {
    const auto bytes = textBytes(*value);
    const auto offset = bytes.find(textBytes(*needle));
    if (offset == std::string_view::npos) return -1;
    const int count = static_cast<int>(offset);
    return value->ascii ? count : pointCount(bytes.data(), count);
}

extern "C" void moulang_std_text_substring(const Text* value, int start, int end, Text* result) {
    moulang_text_slice(value->data, value->length, value->ascii, value->owner, start, end, 1, 3, result);
}

extern "C" void moulang_std_text_replace(const Text* value, const Text* needle, const Text* replacement, Text* result) {
    const auto bytes = textBytes(*value);
    const auto match = textBytes(*needle);
    const auto substitute = textBytes(*replacement);
    if (match.empty()) runtimeError("Moulang runtime: text.replace requires a nonempty needle.\r\n");
    std::int64_t count = 0;
    for (auto found = bytes.find(match); found != std::string_view::npos;
        found = bytes.find(match, found + match.size())) ++count;
    if (!count) {
        *result = shareText(bytes.data(), value->length, value->ascii, value->owner);
        return;
    }
    const auto length = value->length + count * (replacement->length - needle->length);
    auto* output = textStorage(length, result);
    if (!output) return;
    std::size_t start = 0;
    for (auto found = bytes.find(match); found != std::string_view::npos; found = bytes.find(match, start)) {
        copyBytes(output, bytes.substr(start, found - start));
        copyBytes(output, substitute);
        start = found + match.size();
    }
    copyBytes(output, bytes.substr(start));
    result->ascii = isAscii(result->data, result->length);
}

extern "C" void* moulang_std_text_split(const Text* value, const Text* separator) {
    const auto bytes = textBytes(*value);
    const auto match = textBytes(*separator);
    if (match.empty()) runtimeError("Moulang runtime: text.split requires a nonempty separator.\r\n");
    std::int64_t count = 1;
    for (auto found = bytes.find(match); found != std::string_view::npos;
        found = bytes.find(match, found + match.size())) ++count;
    if (count > (std::numeric_limits<int>::max)()) {
        runtimeError("Moulang runtime: text.split result is too large.\r\n");
    }
    const auto handle = moulang_list_new(static_cast<int>(count), 1);
    auto* list = static_cast<StringList*>(resolveArc(handle, ArcKind::list));
    std::size_t start = 0;
    for (;;) {
        const auto found = bytes.find(match, start);
        const auto part = bytes.substr(start, found == std::string_view::npos ? found : found - start);
        const auto size = static_cast<int>(part.size());
        insertItem(list, list->size, size
            ? ListItem{part.data(), size, value->ascii || isAscii(part.data(), size), value->owner}
            : ListItem{"", 0, true});
        if (found == std::string_view::npos) return handle;
        start = found + match.size();
    }
}

extern "C" void moulang_std_text_join(void* handle, const Text* separator, Text* result) {
    const auto* list = static_cast<const StringList*>(resolveArc(handle, ArcKind::list));
    const auto divider = textBytes(*separator);
    std::int64_t length = list->size ? static_cast<std::int64_t>(list->size - 1) * separator->length : 0;
    for (int i = 0; i < list->size; ++i) length += list->items[i].length;
    auto* output = textStorage(length, result);
    if (!output) return;
    for (int i = 0; i < list->size; ++i) {
        if (i) copyBytes(output, divider);
        const auto& item = list->items[i];
        const Text text{static_cast<const char*>(item.data), item.length, item.ascii, item.owner};
        copyBytes(output, textBytes(text));
    }
    result->ascii = isAscii(result->data, result->length);
}

extern "C" void moulang_std_text_lower_ascii(const Text* value, Text* result) {
    mapAscii(*value, 'A', 'Z', 'a' - 'A', result);
}

extern "C" void moulang_std_text_upper_ascii(const Text* value, Text* result) {
    mapAscii(*value, 'a', 'z', 'A' - 'a', result);
}

extern "C" void moulang_std_format_utc_date(std::int64_t value, Text* result) {
    auto* output = textStorage(10, result);
    moulang_std_write_utc_date(value, output);
    result->ascii = true;
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

extern "C" void* moulang_int_list_new(int capacity) {
    constexpr auto bytes = sizeof(IntList);
    auto* list = ::new (allocateArc(bytes, bytes)) IntList{{1, bytes, bytes}};
    const auto handle = arcHandle(list, ArcKind::intList);
    writableInts(list, capacity);
    return handle;
}

extern "C" void moulang_int_list_retain(void* handle) { retainArc(handle, ArcKind::intList); }

extern "C" void moulang_int_list_release(void* handle) {
    if (!handle) return;
    auto* list = intList(handle);
    if (--list->arc.references) return;
    releaseBuffer(list->buffer, ArcKind::intBuffer);
    disposeArc(handle, &list->arc);
}

extern "C" void moulang_int_buffer_retain(void* handle) { retainArc(handle, ArcKind::intBuffer); }
extern "C" void moulang_int_buffer_release(void* handle) { releaseBuffer(handle, ArcKind::intBuffer); }

extern "C" int moulang_int_list_len(void* handle) { return intList(handle)->size; }

extern "C" std::uint64_t moulang_int_list_begin(void* handle) { return beginIteration(intList(handle)->iteration); }
extern "C" void moulang_int_list_end(void* handle) { endIteration(intList(handle)->iteration); }

extern "C" int moulang_int_list_get(void* handle, int index) {
    const auto* list = intList(handle);
    const int offset = listIndex(list, index);
    return intBufferData(intBuffer(list))[offset];
}

extern "C" int moulang_int_list_next(void* handle, std::uint64_t version, int* cursor) {
    const auto* list = intList(handle);
    checkIterationVersion(list->iteration, version);
    const int offset = listIndex(list, *cursor);
    ++*cursor;
    return intBufferData(intBuffer(list))[offset];
}

extern "C" void moulang_int_list_set(void* handle, int index, int value) {
    auto* list = intList(handle);
    const int offset = listIndex(list, index);
    writableInts(list, list->capacity)[offset] = value;
}

extern "C" void moulang_int_list_append(void* handle, int value) {
    auto* list = intList(handle);
    insertInt(list, list->size, value);
}

extern "C" void moulang_int_list_insert(void* handle, int index, int value) {
    auto* list = intList(handle);
    insertInt(list, listIndex(list, index, true), value);
}

extern "C" void moulang_int_list_remove(void* handle, int value) {
    auto* list = intList(handle);
    const auto* elements = intBufferData(intBuffer(list));
    int index = 0;
    while (index < list->size && elements[index] != value) ++index;
    if (index == list->size) runtimeError("Moulang runtime: int list does not contain the value to remove.\r\n");
    checkStructuralChange(list->iteration);
    if (index + 1 < list->size) {
        auto* slot = writableInts(list, list->capacity) + index;
        std::memmove(slot, slot + 1, sizeof(int) * (list->size - index - 1));
    }
    --list->size;
    ++list->iteration.version;
}

extern "C" void moulang_int_list_sort(void* handle) {
    auto* list = intList(handle);
    if (list->size < 2) return;
    const auto* elements = intBufferData(intBuffer(list));
    if (std::is_sorted(elements, elements + list->size)) return;
    checkStructuralChange(list->iteration);
    auto* data = writableInts(list, list->capacity);
    std::sort(data, data + list->size);
    ++list->iteration.version;
}

extern "C" int moulang_int_list_pop(void* handle) {
    auto* list = intList(handle);
    if (!list->size) runtimeError("Moulang runtime: cannot pop an empty int list.\r\n");
    checkStructuralChange(list->iteration);
    ++list->iteration.version;
    return intBufferData(intBuffer(list))[--list->size];
}

extern "C" void moulang_int_view_slice(const int* data, int length, void* owner, int begin, int end,
    IntListView* result) {
    const auto* start = moulang_view_start(data, length, begin, end);
    if (begin == end) {
        *result = {};
        return;
    }
    retainArc(owner, ArcKind::intBuffer);
    *result = {start, end - begin, owner};
}

extern "C" void moulang_int_list_view(void* handle, int begin, int end, IntListView* result) {
    const auto* list = intList(handle);
    moulang_int_view_slice(intBufferData(intBuffer(list)), list->size, list->buffer, begin, end, result);
}

extern "C" void moulang_int_list_print(void* handle) {
    const auto* list = intList(handle);
    const auto* data = intBufferData(intBuffer(list));
    moulang_output("[", 1);
    for (int i = 0; i < list->size; ++i) {
        if (i) moulang_output(", ", 2);
        char text[24];
        const int length = moulang_render_long(data[i], text);
        moulang_output(text, length);
    }
    moulang_output("]", 1);
}

extern "C" int moulang_checked_array_index(int index, int length) {
    if (index < 0 || index >= length) runtimeError("Moulang runtime: fixed array index out of bounds.\r\n");
    return index;
}

extern "C" void* moulang_allocate_object(size_t size) {
    // Constructor assignments may release a field that has not yet been initialized.
    auto* object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
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

extern "C" void moulang_read_text(const char* path, int pathLength, Text* result) {
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
    auto* bytes = textStorage(count, result, false);
    const bool read = !count || transferText(file, bytes, nullptr, count);
    CloseHandle(file);
    if (!read) runtimeError("Moulang runtime: cannot read text file.\r\n");
    if (count && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, count, nullptr, 0)) {
        runtimeError("Moulang runtime: text file must be valid UTF-8.\r\n");
    }
    result->ascii = isAscii(bytes, count);
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
