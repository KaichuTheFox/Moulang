#include <cstdint>
#include <cstring>

extern "C" void* moulang_list_new(int capacity, int depth);
extern "C" void moulang_list_release(void* handle);
extern "C" int moulang_list_len(void* handle);
extern "C" void moulang_arc_finish();
extern "C" void moulang_main() {}

struct Text {
    const char* data;
    int length;
    int ascii;
    void* owner;
};

struct ListItem {
    const void* data;
    int length;
    int ascii;
    void* owner;
};

extern "C" void moulang_text_repeat(const char*, int, int, void*, int, Text*);
extern "C" void moulang_text_index(const char*, int, int, void*, int, Text*);
extern "C" void moulang_text_release(void*);
extern "C" std::uint64_t moulang_list_begin(void*);
extern "C" void moulang_list_end(void*);
extern "C" void moulang_list_append(void*, const void*, int, int, void*);
extern "C" void moulang_list_next(void*, std::uint64_t, int*, ListItem*);

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (std::strcmp(argv[1], "stale-text") == 0) {
        Text original{};
        moulang_text_repeat("x", 1, true, nullptr, 2, &original);
        moulang_text_release(original.owner);
        Text result{};
        moulang_text_index(original.data, original.length, original.ascii, original.owner, 0, &result);
        moulang_text_release(result.owner);
        return 0;
    }
    auto* handle = moulang_list_new(1, 1);
    if (std::strcmp(argv[1], "leak") == 0) {
        moulang_arc_finish();
        return 0;
    }
    if (std::strcmp(argv[1], "stale") == 0) {
        moulang_list_release(handle);
        auto* replacement = moulang_list_new(0, 1);
        const int length = moulang_list_len(handle);
        moulang_list_release(replacement);
        return length;
    }
    if (std::strcmp(argv[1], "stale-iterator") == 0) {
        const auto version = moulang_list_begin(handle);
        moulang_list_end(handle);
        moulang_list_append(handle, "x", 1, true, nullptr);
        moulang_list_begin(handle);
        int cursor = 0;
        ListItem item{};
        moulang_list_next(handle, version, &cursor, &item);
        moulang_list_end(handle);
        moulang_list_release(handle);
        return 0;
    }
    return 2;
}
