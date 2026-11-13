int codepoint_sweep(str point) {
    str data = point * 4000
    int count = 0
    int pass = 0
    while (pass < 300) {
        foreach (character in data) {
            if (character == point) { count++ }
        }
        pass++
    }
    return count
}

int ascii_foreach() { return codepoint_sweep("a") }
int hangul_foreach() { return codepoint_sweep("\u{D55C}") }
