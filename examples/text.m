main() {
    str name = "Moulang"
    str greeting = "Hello, " + name
    print(greeting, type(greeting))
    print(name[:3], name[-1], "!" * 3)
    print("{{{}}}") { format(name) }
}
