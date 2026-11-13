main() {
    str chunk = "x" * 512
    str[] list = []
    int i = 0
    int matches = 0
    while (i < 1000000) {
        str text = chunk + "!"
        if (i < 50000) {
            list.append(text)
            if (list.pop() == text) { matches++ }
            list.append(text)
            list.remove(text)
        }
        i++
    }
    print(i, matches, len(list))
}
