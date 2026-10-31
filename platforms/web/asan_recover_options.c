// Diagnostic Release ASan profile only: report every error and keep running so
// a single browser run can reach late states such as a game's save screen.
const char *__asan_default_options(void) {
    return "halt_on_error=0:detect_leaks=0:print_summary=1:malloc_context_size=30";
}
