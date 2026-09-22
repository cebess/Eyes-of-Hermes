#include "EyeDisplay.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LittleFS.h>
#include <Wire.h>
#include <string.h>

namespace {

constexpr uint8_t SCREEN_WIDTH = 128;
constexpr uint8_t SCREEN_HEIGHT = 64;
constexpr int8_t OLED_RESET = -1;
constexpr int MAX_FRAMES = 16;
constexpr int MAX_FOLDERS = 16;
constexpr uint16_t FRAME_DELAY_MS = 50;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
char frameNames[MAX_FRAMES][64];
int frameCount = 0;
volatile bool drawingInProgress = false;
char drawFramesTaskFolder[64];

uint16_t readLE16(File &file) {
  uint16_t lsb = file.read();
  uint16_t msb = file.read();
  return (msb << 8) | lsb;
}

uint32_t readLE32(File &file) {
  uint32_t b0 = file.read();
  uint32_t b1 = file.read();
  uint32_t b2 = file.read();
  uint32_t b3 = file.read();
  return (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
}

void drawBmp(const char *path, bool flip = false) {
  File bmpFile = LittleFS.open(path, "r");
  if (!bmpFile) {
    Serial.println(F("Failed to open BMP file"));
    return;
  }

  if (readLE16(bmpFile) != 0x4D42) {
    Serial.print(F("Not a BMP file "));
    Serial.println(path);
    bmpFile.close();
    return;
  }

  bmpFile.seek(10);
  uint32_t dataOffset = readLE32(bmpFile);
  bmpFile.seek(14);
  uint32_t headerSize = readLE32(bmpFile);
  bmpFile.seek(18);
  int32_t bmpWidth = static_cast<int32_t>(readLE32(bmpFile));
  int32_t bmpHeight = static_cast<int32_t>(readLE32(bmpFile));
  bmpFile.seek(28);
  uint16_t bpp = readLE16(bmpFile);
  uint32_t compression = readLE32(bmpFile);

  if (compression != 0 || (bpp != 1 && bpp != 8 && bpp != 24)) {
    Serial.println(F("Unsupported BMP format (need 1-bit, 8-bit, or 24-bit uncompressed)"));
    bmpFile.close();
    return;
  }

  uint8_t paletteLuminance[256];
  if (bpp == 1 || bpp == 8) {
    bmpFile.seek(46);
    uint32_t clrUsed = readLE32(bmpFile);
    uint32_t colorCount = clrUsed != 0 ? clrUsed : (1u << bpp);
    bmpFile.seek(14 + headerSize);
    for (uint32_t index = 0; index < colorCount && index < 256; index++) {
      uint8_t blue = bmpFile.read();
      uint8_t green = bmpFile.read();
      uint8_t red = bmpFile.read();
      bmpFile.read();
      paletteLuminance[index] = static_cast<uint8_t>((red * 299 + green * 587 + blue * 114) / 1000);
    }
  }

  bool bottomUp = bmpHeight > 0;
  uint32_t height = bottomUp ? static_cast<uint32_t>(bmpHeight) : static_cast<uint32_t>(-bmpHeight);
  uint32_t width = static_cast<uint32_t>(bmpWidth);
  uint32_t rowSize = ((width * bpp + 31) / 32) * 4;
  uint8_t *rowBuffer = static_cast<uint8_t *>(malloc(rowSize));
  if (!rowBuffer) {
    Serial.println(F("Not enough memory to read BMP row"));
    bmpFile.close();
    return;
  }

  display.clearDisplay();
  for (uint32_t row = 0; row < height && row < SCREEN_HEIGHT; row++) {
    uint32_t sourceRow = bottomUp ? (height - 1 - row) : row;
    bmpFile.seek(dataOffset + sourceRow * rowSize);
    bmpFile.read(rowBuffer, rowSize);

    for (uint32_t col = 0; col < width && col < SCREEN_WIDTH; col++) {
      uint16_t luminance;
      if (bpp == 1) {
        uint8_t index = (rowBuffer[col / 8] >> (7 - (col % 8))) & 0x01;
        luminance = paletteLuminance[index];
      } else if (bpp == 8) {
        luminance = paletteLuminance[rowBuffer[col]];
      } else {
        uint8_t blue = rowBuffer[col * 3];
        uint8_t green = rowBuffer[col * 3 + 1];
        uint8_t red = rowBuffer[col * 3 + 2];
        luminance = (red * 299 + green * 587 + blue * 114) / 1000;
      }
      uint32_t displayRow = flip ? height - 1 - row : row;
      display.drawPixel(col, displayRow, luminance > 128 ? SSD1306_WHITE : SSD1306_BLACK);
    }
  }

  free(rowBuffer);
  bmpFile.close();
  display.display();
}

void loadFrameList(const char *folderPath) {
  frameCount = 0;
  File directory = LittleFS.open(folderPath);
  if (!directory || !directory.isDirectory()) {
    Serial.println(F("Failed to open image folder"));
    return;
  }

  File file = directory.openNextFile();
  while (file && frameCount < MAX_FRAMES) {
    if (!file.isDirectory()) {
      const char *name = file.name();
      if (strchr(name, '/')) {
        strncpy(frameNames[frameCount], name, sizeof(frameNames[frameCount]) - 1);
      } else {
        snprintf(frameNames[frameCount], sizeof(frameNames[frameCount]), "%s/%s", folderPath, name);
      }
      frameNames[frameCount][sizeof(frameNames[frameCount]) - 1] = '\0';
      frameCount++;
    }
    file = directory.openNextFile();
  }

  for (int index = 1; index < frameCount; index++) {
    char key[64];
    strcpy(key, frameNames[index]);
    int previousIndex = index - 1;
    while (previousIndex >= 0 && strcmp(frameNames[previousIndex], key) > 0) {
      strcpy(frameNames[previousIndex + 1], frameNames[previousIndex]);
      previousIndex--;
    }
    strcpy(frameNames[previousIndex + 1], key);
  }
}

int loadSubfolderList(const char *parentPath, char list[][64], int maxCount) {
  int count = 0;
  File parent = LittleFS.open(parentPath);
  if (!parent || !parent.isDirectory()) {
    Serial.println(F("Failed to open folder"));
    return 0;
  }

  bool parentIsRoot = strcmp(parentPath, "/") == 0;
  File entry = parent.openNextFile();
  while (entry && count < maxCount) {
    if (entry.isDirectory()) {
      const char *name = entry.name();
      if (name[0] == '/') {
        strncpy(list[count], name, sizeof(list[count]) - 1);
      } else if (parentIsRoot) {
        snprintf(list[count], sizeof(list[count]), "/%s", name);
      } else {
        snprintf(list[count], sizeof(list[count]), "%s/%s", parentPath, name);
      }
      list[count][sizeof(list[count]) - 1] = '\0';
      count++;
    }
    entry = parent.openNextFile();
  }

  return count;
}

void runFrames(const char *folderPath) {
  loadFrameList(folderPath);
  for (int index = 0; index < frameCount; index++) {
    drawBmp(frameNames[index], true);
    delay(FRAME_DELAY_MS);
  }
}

void drawFramesTask(void *parameters) {
  runFrames(static_cast<const char *>(parameters));
  drawingInProgress = false;
  vTaskDelete(NULL);
}

} // namespace

namespace EyeDisplay {

bool begin(int sdaPin, int sclPin, uint8_t screenAddress) {
  Wire.begin(sdaPin, sclPin);
  if (!display.begin(SSD1306_SWITCHCAPVCC, screenAddress)) {
    Serial.println(F("SSD1306 allocation failed"));
    return false;
  }

  if (!LittleFS.begin(true)) {
    Serial.println(F("LittleFS mount failed"));
    return false;
  }

  return true;
}

const char *getRandomImageFolder(const char *folderPath) {
  static char selectedFolder[64];
  char subfolders[MAX_FOLDERS][64];
  char cleanPath[64];
  const char *path = folderPath;

  if (strncmp(path, "data/", 5) == 0 || strncmp(path, "data\\", 5) == 0) {
    path += 4;
  } else if (strncmp(path, "/data/", 6) == 0 || strncmp(path, "\\data\\", 6) == 0) {
    path += 5;
  }

  if (path[0] != '/' && path[0] != '\\') {
    snprintf(cleanPath, sizeof(cleanPath), "/%s", path);
  } else {
    strncpy(cleanPath, path, sizeof(cleanPath) - 1);
    cleanPath[sizeof(cleanPath) - 1] = '\0';
  }

  for (size_t index = 0; index < strlen(cleanPath); index++) {
    if (cleanPath[index] == '\\') {
      cleanPath[index] = '/';
    }
  }

  int count = loadSubfolderList(cleanPath, subfolders, MAX_FOLDERS);
  if (count == 0) {
    return nullptr;
  }

  strncpy(selectedFolder, subfolders[random(0, count)], sizeof(selectedFolder) - 1);
  selectedFolder[sizeof(selectedFolder) - 1] = '\0';
  return selectedFolder;
}

bool isDrawingFrames() {
  return drawingInProgress;
}

void drawFrames(const char *folderPath) {
  if (drawingInProgress) {
    Serial.println(F("Skipping drawFrames: previous animation still running"));
    return;
  }

  drawingInProgress = true;
  strncpy(drawFramesTaskFolder, folderPath, sizeof(drawFramesTaskFolder) - 1);
  drawFramesTaskFolder[sizeof(drawFramesTaskFolder) - 1] = '\0';
  xTaskCreatePinnedToCore(drawFramesTask, "DrawFrames", 8192, drawFramesTaskFolder, 1, NULL, 1);
  Serial.print(F("Started drawFrames task: "));
  Serial.println(drawFramesTaskFolder);
}

} // namespace EyeDisplay