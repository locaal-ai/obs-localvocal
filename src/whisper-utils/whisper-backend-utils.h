#ifndef WHISPER_BACKEND_UTILS_H
#define WHISPER_BACKEND_UTILS_H

inline bool is_whisper_backend_changed(int current_backend_device, int new_backend_device,
				       bool current_enable_flash_attn, bool new_enable_flash_attn)
{
	return current_backend_device != new_backend_device ||
	       current_enable_flash_attn != new_enable_flash_attn;
}

#endif // WHISPER_BACKEND_UTILS_H
