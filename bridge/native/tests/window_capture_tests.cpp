#include <mcxrinput/window_capture.hpp>

#include <cstdint>
#include <iostream>
#include <limits>

using namespace mcxrinput::native;

namespace {

int failures = 0;

void check(bool condition, const char* message) {
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

} // namespace

int main() {
	HWND window = nullptr;
	check(parseWindowHandle(L"0x123ABC", window), "ordinary hexadecimal HWND parses");
	check(reinterpret_cast<std::uintptr_t>(window) == 0x123ABCU,
			"parsed HWND preserves its numeric value");
	check(parseWindowHandle(L"0Xf", window), "uppercase prefix parses");
	check(!parseWindowHandle(L"123ABC", window), "missing prefix is rejected");
	check(!parseWindowHandle(L"0x", window), "empty hexadecimal value is rejected");
	check(!parseWindowHandle(L"0x0", window), "null HWND is rejected");
	check(!parseWindowHandle(L"0x12G", window), "non-hexadecimal digit is rejected");
	check(!parseWindowHandle(L"0xFFFFFFFFFFFFFFFFF", window),
			"value wider than uintptr_t is rejected");

	if (failures != 0) {
		std::cerr << failures << " window-capture test(s) failed.\n";
		return 1;
	}
	std::cout << "All window-capture tests passed.\n";
	return 0;
}
