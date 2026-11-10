main() {
    int numbers[] = [10, 20, 30]
    numbers = append(numbers, 40)
    print(numbers[3])

    str? nickname = null
    print(nickname ?? "Unknown")
    nickname = "Moulang"
    if (nickname != null) {
        print(nickname)
    }
}
