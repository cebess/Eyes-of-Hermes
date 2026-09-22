#pragma once

#include <Arduino.h>

namespace EyeDisplay {

bool begin(int sdaPin, int sclPin, uint8_t screenAddress = 0x3C);
const char *getRandomImageFolder(const char *folderPath);
void drawFrames(const char *folderPath);
bool isDrawingFrames();

} // namespace EyeDisplay