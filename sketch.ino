#include <Adafruit_NeoPixel.h>
#include <Keypad.h>

// Config NeoPixel
#define PIN 2
#define NUMPIXELS 16
Adafruit_NeoPixel ring(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

// Keypad 4x4 
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
// rows -> D10..D7, cols -> D6..D3 (D2 вільний для NeoPixel)
byte rowPins[ROWS] = {10, 9, 8, 7};
byte colPins[COLS] = {6, 5, 4, 3};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Extra buttons
#define DYN_PIN 11   // перемикач динамічного режиму (натискання - toggle)
#define WAVE_PIN 12  // кнопка хвилі (натискання - toggle)

// States
bool powerOn = true;
uint32_t currentColor = 0xFFFFFF; // поточний статичний колір
uint32_t lastColor = 0xFFFFFF;    // зберігаємо перед вимкненням
int brightness = 200;             // загальна яскравість 0..255

// режими
bool dynamicMode = false;   // palette cycling (вмикається DYN_PIN)
bool waveMode = false;      // хвиля (вмикається WAVE_PIN)
bool waveUsesDynamic = false; // якщо хвиля стартувала після dynamicMode=true

// таймери / індекси
unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL = 120; // мс між кроками анімації
int dynamicIndex = 0; // індекс для циклу палітри
int wavePos = 0;      // позиція провідного пікселя хвилі

// debounce для кнопок 11/12
bool lastDynRaw = HIGH;
bool lastWaveRaw = HIGH;
unsigned long lastDynTime = 0;
unsigned long lastWaveTime = 0;
const unsigned long DEBOUNCE_MS = 50;

// Palette
// В hex у форматі 0xRRGGBB
const int PALETTE_SIZE = 12;
uint32_t palette[PALETTE_SIZE] = {
  0x0000FF, // 1 pure blue
  0x0040FF, // 2 blue -> greener
  0x0080FF, // 3 turquoise
  0x00FF00, // 4 green
  0x80FF00, // 5 green->yellow
  0xC0FF00, // 6 green close to yellow
  0xFFFF00, // 7 yellow
  0xFF8000, // 8 orange
  0xFF0000, // 9 red
  0x800080, // * violet
  0xC000C0, // 0 pink-violet
  0xFF00FF  // # saturated magenta
};

// Helpers

// Розбиває 24-bit color на r,g,b (0..255)
inline void colorToRGB(uint32_t color, uint8_t &r, uint8_t &g, uint8_t &b) {
  r = (color >> 16) & 0xFF;
  g = (color >> 8) & 0xFF;
  b = color & 0xFF;
}

// Встановлює всі пікселі одним кольором (враховує brightness як глобальний множник)
void setAllStatic(uint32_t color) {
  uint8_t cr, cg, cb;
  colorToRGB(color, cr, cg, cb);

  float globalScale = brightness / 255.0f;

  for (int i = 0; i < NUMPIXELS; ++i) {
    uint8_t rr = (uint8_t)(cr * globalScale + 0.5f);
    uint8_t gg = (uint8_t)(cg * globalScale + 0.5f);
    uint8_t bb = (uint8_t)(cb * globalScale + 0.5f);
    ring.setPixelColor(i, ring.Color(rr, gg, bb));
  }
  ring.show();
}

// Рендер хвилі
// лідер = wavePos (яскравий), наступники з меншим рівнем
void renderWave() {
  // multipliers для трьох рівнів хвилі і фонового рівня
  const float m0 = 1.00f;  // яскраво
  const float m1 = 0.54f;  // помірно
  const float m2 = 0.28f;  // слабо
  const float mBase = 0.02f; // фон, майже вимкнено

  // Якщо хвиля використовує динаміку -> кожному індексу даємо свій колір з палітри
  // Індекс палітри зміщується від dynamicIndex, щоб кольори теж рухались, коли dynamicMode=true
  int paletteOffset = dynamicIndex; // це створює рух кольорів при динамічному режимі

  for (int i = 0; i < NUMPIXELS; ++i) {
    // відносна позиція від початкового обраного діода в позитивному напрямку
    int rel = (i - wavePos);
    if (rel < 0) rel += NUMPIXELS;
    float mult = mBase;
    if (rel == 0) mult = m0;
    else if (rel == 1) mult = m1;
    else if (rel == 2) mult = m2;
    else mult = mBase;

    uint32_t baseColor;
    if (waveUsesDynamic) {
      // колір для цього пікселя беремо з палітри, зрушуючи індекс на i + paletteOffset
      int idx = (i + paletteOffset) % PALETTE_SIZE;
      baseColor = palette[idx];
    } else {
      // хвиля одного кольору - використовуємо currentColor
      baseColor = currentColor;
    }

    // масштабування кольору на mult та загальний brightness
    uint8_t cr, cg, cb;
    colorToRGB(baseColor, cr, cg, cb);

    float scale = mult * (brightness / 255.0f);
    uint8_t rr = (uint8_t)constrain((int)(cr * scale + 0.5f), 0, 255);
    uint8_t gg = (uint8_t)constrain((int)(cg * scale + 0.5f), 0, 255);
    uint8_t bb = (uint8_t)constrain((int)(cb * scale + 0.5f), 0, 255);

    ring.setPixelColor(i, ring.Color(rr, gg, bb));
  }
  ring.show();
}

// Просте оновлення стану динамічної індексації (щоб кольори рухались)
void advanceDynamics() {
  dynamicIndex = (dynamicIndex + 1) % PALETTE_SIZE;
}

// Setup
void setup() {
  ring.begin();
  ring.show();

  pinMode(DYN_PIN, INPUT_PULLUP);
  pinMode(WAVE_PIN, INPUT_PULLUP);

  // початковий колір білий
  currentColor = 0xFFFFFF;
  setAllStatic(currentColor);
}

// Main loop
void loop() {
  // обробка keypad
  char key = keypad.getKey();
  if (key != NO_KEY) {
    // будь-яка кнопка кольору повинна повернути нас до статичного режиму
    // (вимкнути dynamic і wave)
    if (key == 'A') {
      // вкл/викл
      powerOn = !powerOn;
      if (!powerOn) {
        lastColor = currentColor;
        ring.clear();
        ring.show();
      } else {
        currentColor = lastColor;
        dynamicMode = false;
        waveMode = false;
        setAllStatic(currentColor);
      }
    }
    else if (key == 'B' && powerOn) { // яскравість +
      brightness += 16; if (brightness > 255) brightness = 255;
      if (!waveMode) setAllStatic(currentColor);
    }
    else if (key == 'C' && powerOn) { // яскравість -
      brightness -= 16; if (brightness < 0) brightness = 0;
      if (!waveMode) setAllStatic(currentColor);
    }
    else if (key == 'D' && powerOn) { // білий
      currentColor = 0xFFFFFF;
      dynamicMode = false;
      waveMode = false;
      setAllStatic(currentColor);
    }
    else if (powerOn) {
      // кольори палітри (1..9, *,0,#)
      switch (key) {
        case '1': currentColor = palette[0]; break;
        case '2': currentColor = palette[1]; break;
        case '3': currentColor = palette[2]; break;
        case '4': currentColor = palette[3]; break;
        case '5': currentColor = palette[4]; break;
        case '6': currentColor = palette[5]; break;
        case '7': currentColor = palette[6]; break;
        case '8': currentColor = palette[7]; break;
        case '9': currentColor = palette[8]; break;
        case '*': currentColor = palette[9]; break;
        case '0': currentColor = palette[10]; break;
        case '#': currentColor = palette[11]; break;
        default: break;
      }
      // натиснення кольору має вимикати динамічні режими (повернути до статичного)
      dynamicMode = false;
      waveMode = false;
      setAllStatic(currentColor);
    }
  }

  // обробка кнопок DYN_PIN (11) та WAVE_PIN (12) з debounce
  bool dynRaw = digitalRead(DYN_PIN);
  if (dynRaw != lastDynRaw && (millis() - lastDynTime) > DEBOUNCE_MS) {
    lastDynTime = millis();
    lastDynRaw = dynRaw;
    if (dynRaw == LOW) { // натискання
      // Toggle dynamic mode
      dynamicMode = !dynamicMode;
      // Якщо включили динаміку, вимикаємо хвилю (щоб окремо стартувала)
      if (dynamicMode) {
        waveMode = false;
      }
    }
  }

  bool waveRaw = digitalRead(WAVE_PIN);
  if (waveRaw != lastWaveRaw && (millis() - lastWaveTime) > DEBOUNCE_MS) {
    lastWaveTime = millis();
    lastWaveRaw = waveRaw;
    if (waveRaw == LOW) { // натискання
      // Toggle wave mode
      waveMode = !waveMode;
      if (waveMode) {
        waveUsesDynamic = dynamicMode;
        // якщо хвиля стартувала окремо, зберігаємо currentColor (для відновлення)
        // і скидаємо позицію хвилі
        wavePos = 0;
        dynamicIndex = 0; // скидаємо індекс динаміки, щоб цикл починався з початку
      } else {
        // вимкнули хвилю - якщо була динаміка до цього, залишаємо її увімкненою
        // (тільки хвиля зупинилася)
        // Якщо dynamicMode було false, повертаємо статичний колір
        if (!dynamicMode) setAllStatic(currentColor);
      }
    }
  }

  // Якщо вимкнено харчування, нічого не анімуємо
  if (!powerOn) return;

  // анімація: динамічний режим (коли він увімкнений і не хвиля)
  unsigned long now = millis();
  if (now - lastUpdate >= UPDATE_INTERVAL) {
    lastUpdate = now;

    if (dynamicMode && !waveMode) {
      // просто циклічна зміна палітри як статичного показу
      currentColor = palette[dynamicIndex % PALETTE_SIZE];
      dynamicIndex = (dynamicIndex + 1) % PALETTE_SIZE;
      setAllStatic(currentColor);
    }

    if (waveMode) {
      // Якщо хвиля використовує динаміку, просуваємо dynamicIndex також,
      // щоб кольори переміщувалися по палітрі разом з хвилею.
      if (waveUsesDynamic) {
        // просунути палетний індекс, щоб кольори "рухались"
        dynamicIndex = (dynamicIndex + 1) % PALETTE_SIZE;
      }
      // Рендеримо хвилю по поточному wavePos
      renderWave();

      // рухаємо хвилю вперед
      wavePos = (wavePos + 1) % NUMPIXELS;
    }
  }
}
