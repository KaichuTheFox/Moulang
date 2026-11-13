struct Note {
    str text
    str[] tags
}

class Holder {
    public Note note

    public Holder(Note initial) {
        note = initial
    }

    public str text() {
        return note.text
    }
}

str view() {
    str value = "A" + "hello" + "Z"
    return value[1:-1]
}

str[] child() {
    str[][] rows = [["a" + "b"], ["c"]]
    return rows[0]
}

str replace(str[] items) {
    items[0] = "new" + "!"
    return items[0]
}

str? optional(bool present) {
    if (present) { return "yes" + "!" }
    return null
}

str pick(bool first) {
    return first ? view() : "no" + "!"
}

main() {
    str text = view()
    text = text
    str[] row = child()
    var alias = row
    print(row[0], replace(alias))
    print(text, row[0], optional(false) ?? pick(false), optional(true) ?? "bad")

    Note original = Note("left" + "!", row)
    Note copied = original
    original.text = "right" + "!"
    Holder holder = new Holder(original)
    holder.note = copied
    Holder? optionalHolder = holder
    print(original.text, optionalHolder?.text() ?? "bad", copied.tags[0])
    Note? optionalNote = optionalHolder?.note
    Note recovered = optionalNote ?? original
    optionalHolder = null
    Note fallback = optionalHolder?.note ?? original
    print(recovered.text, fallback.text)
    print("item {}") { format(alias.pop()) }
    print(len(row))

    WText("arc.txt", text)
    str loaded = RText("arc.txt")
    text = loaded[1:4]
    loaded = "replaced"
    print(text)

    int i = 0
    int count = 0
    while (i < 100) {
        str local = pick(i < 50)
        i++
        if (i < 50) { continue }
        if (i > 70) { break }
        if (local == "hello" || (local + "!") == "no!!") { count++ }
    }
    print(count)
}
