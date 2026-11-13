from pathlib import Path


def integer_math():
    value = 1
    for i in range(300_000):
        next_value = value * 3 + i
        value = next_value - next_value // 1_000_003 * 1_000_003
    return value


def real_math():
    value = 1.0
    for i in range(300_000):
        value = (value + float(i - i // 17 * 17)) * 0.5
    return value


def loop_count():
    count = 0
    for i in range(1_000_000):
        if i - i // 13 * 13 == 0:
            count += 1
    return count


def mix(value):
    mixed = value * 3 + 7
    return mixed - mixed // 1009 * 1009


def function_calls():
    total = 0
    for i in range(300_000):
        total += mix(i)
    return total


def string_concat():
    value = ""
    for _ in range(3500):
        previous = value
        value = previous + "x"
    return value == "x" * 3500


def list_access():
    items = ["a", "b", "c", "d"]
    count = 0
    for _ in range(50_000):
        items.append("e")
        if items[4] == "e" and items[1] == "b":
            count += 1
        items.remove("a")
        items.pop()
        items.insert(0, "a")
    return count + len(items)


def list_sort():
    items = ["d", "c", "b", "a"]
    count = 0
    for _ in range(50_000):
        items[0] = "d"
        items[1] = "c"
        items[2] = "b"
        items[3] = "a"
        items.sort()
        if items[0] == "a" and items[1] == "b" and items[2] == "c" and items[3] == "d":
            count += 1
    return count


def codepoint_sweep(point):
    data = point * 4000
    count = 0
    for _ in range(300):
        for character in data:
            if character == point:
                count += 1
    return count


def ascii_foreach():
    return codepoint_sweep("a")


def hangul_foreach():
    return codepoint_sweep("\uD55C")


def file_io():
    file = Path("data.txt")
    chunk = b"x" * 4096
    for _ in range(64):
        with file.open("ab") as output:
            output.write(chunk)
    return file.read_bytes().decode("utf-8") == "x" * 262144
