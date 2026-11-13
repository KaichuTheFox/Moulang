int runtime_integer_casts() {
    int[] values = []
    int index = 0
    while (index < 64) {
        values.append(index)
        index++
    }
    int sum = 0
    index = 0
    while (index < 12000000) {
        int byteValue = values[index & 63]
        int shortValue = values[(index + 16) & 63] - 32
        sum += int(char(byteValue)) + int(short(shortValue))
        index++
    }
    return sum
}

int bounded_integer_casts() {
    int sum = 0
    int index = 0
    while (index < 12000000) {
        int byteValue = index & 63
        int shortValue = ((index + 16) & 63) - 32
        sum += int(char(byteValue)) + int(short(shortValue))
        index++
    }
    return sum
}
