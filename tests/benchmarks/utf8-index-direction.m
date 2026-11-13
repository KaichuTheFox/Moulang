import "std:time"

long measureIndex(str data, int index, str expected) {
    int matches = 0
    int i = 0
    long start = time.monotonicNanos()
    while (i < 8000) {
        if (data[index] == expected) { matches++ }
        i++
    }
    long elapsed = time.monotonicNanos() - start
    if (matches != 8000) { return -1 }
    return elapsed
}

main() {
    str ascii = "a" * 16384
    str point = "\u{D55C}"
    str shortText = point * 16
    str longText = point * 16384
    print(measureIndex(ascii, 0, "a"), measureIndex(shortText, 0, point),
        measureIndex(longText, 0, point), measureIndex(longText, -1, point),
        measureIndex(longText, 16383, point))
}
