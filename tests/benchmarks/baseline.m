int integer_math() {
    int value = 1
    int i = 0
    while (i < 300000) {
        int next = value * 3 + i
        value = next - next / 1000003 * 1000003
        i++
    }
    return value
}

double real_math() {
    double value = 1.0
    int i = 0
    while (i < 300000) {
        value = (value + double(i - i / 17 * 17)) * 0.5
        i++
    }
    return value
}

int loop_count() {
    int count = 0
    int i = 0
    while (i < 1000000) {
        if (i - i / 13 * 13 == 0) { count++ }
        i++
    }
    return count
}

int mix(int value) {
    int mixed = value * 3 + 7
    return mixed - mixed / 1009 * 1009
}

int function_calls() {
    int sum = 0
    int i = 0
    while (i < 300000) {
        sum += mix(i)
        i++
    }
    return sum
}

bool string_concat() {
    str value = ""
    int i = 0
    while (i < 3500) {
        str previous = value
        value = previous + "x"
        i++
    }
    return value == "x" * 3500
}

int list_access() {
    str[] items = ["a", "b", "c", "d"]
    int count = 0
    int i = 0
    while (i < 50000) {
        items.append("e")
        if (items[4] == "e" && items[1] == "b") { count++ }
        items.remove("a")
        items.pop()
        items.insert(0, "a")
        i++
    }
    return count + len(items)
}

int list_sort() {
    str[] items = ["d", "c", "b", "a"]
    int count = 0
    int i = 0
    while (i < 50000) {
        items[0] = "d"
        items[1] = "c"
        items[2] = "b"
        items[3] = "a"
        items.sort()
        if (items[0] == "a" && items[1] == "b" && items[2] == "c" && items[3] == "d") {
            count++
        }
        i++
    }
    return count
}

int indexed_sweep(str point) {
    str data = point * 4000
    int i = 0
    int count = 0
    while (i < 4000) {
        if (data[i] == point) { count++ }
        i++
    }
    return count
}

int ascii_indexed() { return indexed_sweep("a") }
int hangul_indexed() { return indexed_sweep("\u{D55C}") }

bool file_io() {
    str chunk = "x" * 4096
    int i = 0
    while (i < 64) {
        AText("data.txt", chunk)
        i++
    }
    return RText("data.txt") == "x" * 262144
}
