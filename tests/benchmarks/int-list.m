int int_slice_append_read() {
    int data[] = []
    int index = 0
    while (index < 1000000) {
        data = append(data, index & 1023)
        index++
    }
    int sum = 0
    foreach (value in data) { sum += value }
    return sum
}

int int_list_append_read() {
    int[] data = []
    int index = 0
    while (index < 1000000) {
        data.append(index & 1023)
        index++
    }
    int sum = 0
    foreach (value in data) { sum += value }
    return sum
}

int[] writeInput() {
    int[] data = []
    int index = 0
    while (index < 262144) {
        data.append(0)
        index++
    }
    return data
}

int int_list_write() {
    var data = writeInput()
    int sum = 0
    int index = 0
    while (index < 128) {
        data[0] = index
        sum += data[0]
        index++
    }
    return sum
}

int int_list_view_write() {
    var data = writeInput()
    int sum = 0
    int index = 0
    while (index < 128) {
        var view = data[0:262144]
        data[0] = index
        int previous = index == 0 ? 0 : index - 1
        if (view[0] != previous) { return -1 }
        sum += data[0]
        index++
    }
    return sum
}
