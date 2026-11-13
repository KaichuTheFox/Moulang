extern "C" void moulang_main();
#ifdef MOULANG_ARC_DEBUG
extern "C" void moulang_arc_finish();
#endif

// MinGW's CRT initializes its static support libraries before entering the language.
int main() {
    moulang_main();
#ifdef MOULANG_ARC_DEBUG
    moulang_arc_finish();
#endif
    return 0;
}
