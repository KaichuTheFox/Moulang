main() {
    char byteValue = char(260 & 255)
    short small = short(32000)
    long count = 2147483647
    float fraction = float(2.5)
    double total = fraction + small

    print(byteValue)
    print(total)
    print(int(2.5))
    print(int(3.5))
    count++
    print(count)
}
