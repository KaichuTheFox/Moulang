main() {
    str[] songs = ["Blue", "Amber"]
    songs.append("Copper")
    songs.sort() {
        print(songs, len(songs))
    }

    str[][] table = [songs, ["Encore"]]
    table[-1].append("Finale")
    table[0][0] = "Gold"
    print(table, type(table))
}
