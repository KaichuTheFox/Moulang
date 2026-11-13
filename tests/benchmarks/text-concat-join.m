import "std:time"
import "std:text"

long measureConcat(bool collect) {
    str value = ""
    str[] pieces = []
    int i = 0
    long start = time.monotonicNanos()
    if (collect) {
        while (i < 16000) {
            pieces.append("x")
            i++
        }
        value = text.join(pieces, "")
    } else {
        while (i < 16000) {
            value += "x"
            i++
        }
    }
    long elapsed = time.monotonicNanos() - start
    if (value != "x" * 16000) { return -1 }
    return elapsed
}

main() {
    print(measureConcat(false), measureConcat(true))
}
