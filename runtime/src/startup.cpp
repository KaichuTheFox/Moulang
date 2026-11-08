extern "C" void moulang_main();

// MinGW's CRT initializes its static support libraries before entering the language.
int main() {
    moulang_main();
    return 0;
}
