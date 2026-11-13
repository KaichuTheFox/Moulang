int indexed_checks(str point, int length, int iterations) {
    str data = point * length
    int matches = 0
    int i = 0
    while (i < iterations) {
        int index = i & (length - 1)
        if (data[index] == point && data[-index - 1] == point) { matches++ }
        i++
    }
    return matches
}

int ascii_index_short() { return indexed_checks("a", 16, 50000) }
int ascii_index_long() { return indexed_checks("a", 16384, 8000) }
int hangul_index_short() { return indexed_checks("\u{D55C}", 16, 50000) }
int hangul_index_long() { return indexed_checks("\u{D55C}", 16384, 8000) }
