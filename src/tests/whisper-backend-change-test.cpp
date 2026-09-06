#include <iostream>

#include "whisper-utils/whisper-backend-utils.h"

struct backend_change_test_case {
	const char *name;
	int current_backend_device;
	int new_backend_device;
	bool current_enable_flash_attn;
	bool new_enable_flash_attn;
	bool expected;
};

int main()
{
	const backend_change_test_case test_cases[] = {
		{"same backend and Flash Attention", -1, -1, false, false, false},
		{"different backend", -1, 0, false, false, true},
		{"changed Flash Attention", -1, -1, false, true, true},
		{"different backend and changed Flash Attention", -1, 0, false, true, true},
	};

	for (const auto &test_case : test_cases) {
		const bool actual = is_whisper_backend_changed(
			test_case.current_backend_device, test_case.new_backend_device,
			test_case.current_enable_flash_attn, test_case.new_enable_flash_attn);
		if (actual != test_case.expected) {
			std::cerr << "Failed: " << test_case.name << " (expected " << test_case.expected
				  << ", got " << actual << ")\n";
			return 1;
		}
	}

	return 0;
}
