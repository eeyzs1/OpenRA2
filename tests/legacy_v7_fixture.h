#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>

// Generate historical v7/v29, v8/v30, v9/v31 and v10/v31 layouts by omitting their absent
// native mask/identity/frame fields from a generated-map snapshot. The format
// sizes are frozen values independent of sizeof(Cell) or current writer code.
std::vector<uint8_t> makeLegacyV6Fixture(const std::vector<uint8_t>& current,FILE* currentFog,size_t cells);
std::vector<uint8_t> makeLegacyV7Fixture(const std::vector<uint8_t>& current,FILE* currentFog,size_t cells);
std::vector<uint8_t> makeLegacyV8Fixture(const std::vector<uint8_t>& current,FILE* currentFog,size_t cells);
std::vector<uint8_t> makeLegacyV9Fixture(const std::vector<uint8_t>& current,FILE* currentFog,size_t cells);
std::vector<uint8_t> makeLegacyV10Fixture(const std::vector<uint8_t>& current,FILE* currentFog,size_t cells);
std::vector<uint8_t> makeLegacyWorld32Fixture(const std::vector<uint8_t>& current,FILE* currentFog,size_t cells);
std::vector<uint8_t> makeLegacyWorld33Fixture(const std::vector<uint8_t>& current,FILE* currentFog,size_t cells);
std::vector<uint8_t> makeLegacyWorld34Fixture(const std::vector<uint8_t>& current,FILE* currentFog,size_t cells);
std::vector<uint8_t> makeLegacyWorld35Fixture(const std::vector<uint8_t>& current,FILE* currentFog,size_t cells);
std::vector<uint8_t> makeLegacyWorld36Fixture(const std::vector<uint8_t>& current,FILE* currentFog,size_t cells);
std::vector<uint8_t> makeLegacyWorld37Fixture(const std::vector<uint8_t>& current,FILE* currentFog,size_t cells);
std::vector<uint8_t> makeLegacyWorld38Fixture(const std::vector<uint8_t>& current,FILE* currentFog,size_t cells);
std::vector<uint8_t> makeLegacyWorld39Fixture(const std::vector<uint8_t>& current,FILE* currentFog,size_t cells);
std::vector<uint8_t> makeLegacyWorld40Fixture(const std::vector<uint8_t>& current,FILE* currentFog,size_t cells);
std::vector<uint8_t> makeLegacyWorld41Fixture(const std::vector<uint8_t>& current,FILE* currentFog,size_t cells);
std::vector<uint8_t> makeLegacyWorld42Fixture(const std::vector<uint8_t>& current,FILE* currentFog,size_t cells);
std::vector<uint8_t> makeLegacyWorld43Fixture(const std::vector<uint8_t>& current,FILE* currentFog,size_t cells);
std::vector<uint8_t> makeLegacyWorld44Fixture(const std::vector<uint8_t>& current,FILE* currentFog,size_t cells);

std::vector<uint8_t> makeLegacyWorld45Fixture(const std::vector<uint8_t>& current,FILE* currentFog,size_t cells);
