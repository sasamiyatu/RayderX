#include "common.h"

bool read_binary_file(const char* filepath, std::vector<uint8_t>& data)
{
	FILE* f = fopen(filepath, "rb");
	if (!f)
	{
		printf("Failed to open file %s", filepath);
		return false;
	}

	fseek(f, 0, SEEK_END);
	long filesize = ftell(f);
	fseek(f, 0, SEEK_SET);
	data.resize(filesize);
	size_t bytes_read = fread(data.data(), 1, filesize, f);
	assert(bytes_read == filesize);

	fclose(f);

	return true;
}

std::string read_text_file(const char* filepath)
{
	FILE* f = fopen(filepath, "rb");
	if (!f)
	{
		printf("Failed to open file %s", filepath);
		return std::string();
	}

	fseek(f, 0, SEEK_END);
	long filesize = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t* data = (uint8_t*)malloc(filesize);
	assert(data);
	size_t bytes_read = fread(data, 1, filesize, f);
	assert(bytes_read == filesize);

	std::string str = std::string((const char*)data, (size_t)filesize);
	free(data);
	fclose(f);

	return str;
}
