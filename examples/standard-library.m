import "std:math"
import "std:text" as strings
import "std:time"
import "std:random"

main() {
    print(math.sqrt(81.0))
    var words = strings.split("one,two,three", ",")
    print(strings.join(words, " + "))
    print(time.formatUtcDate(0))
    random.seed(42)
    print(random.nextInt(1, 6))
}
