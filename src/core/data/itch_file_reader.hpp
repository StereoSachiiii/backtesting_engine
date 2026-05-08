#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>

class ITCHReader {
private:
    FILE* file_ = nullptr;
    static constexpr size_t buffer_size = 8192;
	bool raw_mode_ = false;

public:
	void set_raw_mode(bool raw) { raw_mode_ = raw; }

	~ITCHReader() {
		close();
	}

	void close() {
		if (!file_) return;
		fclose(file_);
		file_ = nullptr;
	}

	bool open(const char* filename) {
		file_ = fopen(filename, "rb");
		return file_ != nullptr;
	}

	bool read_next(uint8_t* buffer, size_t& length) {
		if (!file_) return false;

		if (raw_mode_) {
			// Skip any null padding 
			uint8_t type = 0;
			while (true) {
				if (fread(&type, 1, 1, file_) != 1) return false;
				if (type != 0) break;
			}
			buffer[0] = type;
			size_t msg_len = get_raw_msg_len(type);
			if (msg_len == 0) return false;

			if (fread(buffer + 1, 1, msg_len - 1, file_) != (msg_len - 1)) return false;
			length = msg_len;
			return true;
		} else {
			// SoupBinTCP mode (2-byte length prefix)
			uint8_t len_bytes[2];
			if (fread(len_bytes, 1, 2, file_) != 2) return false;
			uint16_t msg_len = (static_cast<uint16_t>(len_bytes[0]) << 8) | static_cast<uint16_t>(len_bytes[1]);
			if (msg_len == 0 || msg_len > buffer_size) return false;

			if (fread(buffer, 1, msg_len, file_) != msg_len) return false;
			length = msg_len;
			return true;
		}
	}

	size_t get_size() {
		if (!file_) return 0;
		long long current = 
#ifdef _MSC_VER
			_ftelli64(file_);
#else
			ftello(file_);
#endif
		fseek(file_, 0, SEEK_END);
		long long size = 
#ifdef _MSC_VER
			_ftelli64(file_);
#else
			ftello(file_);
#endif
		
#ifdef _MSC_VER
			_fseeki64(file_, current, SEEK_SET);
#else
			fseeko(file_, current, SEEK_SET);
#endif
		return (size_t)size;
	}

	size_t get_offset() {
		if (!file_) return 0;
#ifdef _MSC_VER
		return (size_t)_ftelli64(file_);
#else
		return (size_t)ftello(file_);
#endif
	}

private:
	static size_t get_raw_msg_len(uint8_t type) {
		switch (type) {
			case 'S': return 12; case 'R': return 39; case 'H': return 25;
			case 'Y': return 20; case 'L': return 26; case 'V': return 35;
			case 'W': return 12; case 'K': return 28; case 'J': return 35;
			case 'h': return 21; case 'A': return 36; case 'F': return 40;
			case 'E': return 31; case 'C': return 36; case 'X': return 23;
			case 'D': return 19; case 'U': return 35; case 'P': return 44;
			case 'Q': return 40; case 'B': return 19; case 'I': return 50;
			case 'N': return 20; default: return 0;
		}
	}
};
