/***************************************************
 * This is an example sketch for the Adafruit 2.2" SPI display.
 * This library works with the Adafruit 2.2" TFT Breakout w/SD card
 * ----> http://www.adafruit.com/products/1480
 * Check out the links above for our tutorials and wiring diagrams
 * These displays use SPI to communicate, 4 or 5 pins are required to
 * interface (RST is optional)
 * Adafruit invests time and resources providing this open source code,
 * please support Adafruit and open-source hardware by purchasing
 * products from Adafruit!
 * 
 * Written by Limor Fried/Ladyada for Adafruit Industries.
 * MIT license, all text above must be included in any redistribution
 ****************************************************/

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_ILI9340.h>
#include <Adafruit_GFX.h>
#include "TimerOne.h"

// These are the pins used for the UNO
// for Due/Mega/Leonardo use the hardware SPI pins (which are different)
#define _sclk 13
#define _miso 12
#define _mosi 11
#define _cs 10
#define _dc 9
#define _rst 8

#define NOTSPECIAL 1
#define GOTESCAPE 2
#define GOTBRACKET 3
#define INNUM 4
#define IMGSHOW 5

#define LEFT_EDGE0 0
#define RIGHT_EDGE320 319
#define TOP_EDGE0 0
#define BOTTOM_EDGE240 239

#define Serial_RX_BUFFER_SIZE 64

const char version[] = "v2.0";

uint8_t pwm = 255;
uint8_t ledPin = 5;  // PWM LED Backlight control to digital pin 5

// Using software SPI is really not suggested, its incredibly slow
//Adafruit_ILI9340 tft = Adafruit_ILI9340(_cs, _dc, _mosi, _sclk, _rst, _miso);
// Use hardware SPI
Adafruit_ILI9340 tft = Adafruit_ILI9340(_cs, _dc, _rst);

enum ShowError {
  SUCCESS = 0,
  WINDOW_POS_OUT_OF_BOUNDS,
  DUPLICATE_CHILD_INSERTION,
  DUPLICATE_NEIGHBOR_INSERTION,
  COMPONENT_UNINIT_RENDER,
};

class WindowPosition {
public:
  WindowPosition(uint16_t x, uint8_t y)
    : _x(x), _y(y) {}

  WindowPosition(uint16_t maybe_x, uint8_t maybe_y, ShowError& exit_status)
    : _x(maybe_x), _y(maybe_y) {
    if (maybe_x > RIGHT_EDGE320 || maybe_y > BOTTOM_EDGE240) {
      exit_status = WINDOW_POS_OUT_OF_BOUNDS;
    }

    exit_status = SUCCESS;
  }

  inline uint16_t x() {
    return this->_x;
  }

  inline uint8_t y() {
    return this->_y;
  }
private:
  uint16_t _x;
  uint8_t _y;
};

static WindowPosition DEFAULT_WINDOW_POSITION = WindowPosition(0, 0);

class Color {
public:
  Color(uint8_t r, uint8_t g, uint8_t b)
    : raw(tft.Color565(r, g, b)) {}

  const uint16_t raw;
};

#define RED Color(0xff, 0x00, 0x00)
#define GREEN Color(0x00, 0xff, 0x00)
#define BLUE Color(0x00, 0x00, 0xff)
#define WHITE Color(0xff, 0xff, 0xff)
#define BLACK Color(0x00, 0x00, 0x00)

enum WindowComponentFlags {
  FWIN_DEFAULT = 0b00000000,
  FWIN_INIT = 0b00000001,
  FWIN_SELECTED = 0b00000010,
};

class WindowComponent;

class ComponentCanvas {
public:
  ComponentCanvas(const WindowComponent* c)
    : component(c) {
      Serial.println("\t\t\tIn ComponentCanvas Constructor");
    }

  // virtual ~ComponentCanvas() {
  //   int16_t x = this->component->left_bound();
  //   int16_t y = this->component->top_bound();
  //   int16_t w = x + this->component->width();
  //   int16_t h = y + this->component->height();
  //   uint16_t color = this->component->page_background.raw;
  //   tft.drawRect(x, y, w, h, color);
  // }

  void drawFastHLine(int16_t x, int16_t y, int16_t w, Color color);
  void drawFastVLine(int16_t x, int16_t y, int16_t h, Color color);
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Color color);

private:
  WindowComponent* component;
};

class WindowComponent {
public:
  WindowComponent() = default;

  WindowComponent(Color page_background)
    : WindowComponent(DEFAULT_WINDOW_POSITION, page_background, WindowComponentFlags::FWIN_DEFAULT) {}

  virtual ~WindowComponent() {
    Serial.print("DESTRUCTOR!!!! (n = ");
    Serial.print((unsigned int) this->next);
    Serial.print(", c = ");
    Serial.print((unsigned int) this->child);
    Serial.print(")\n");
    Serial.flush();

    if (this->next != nullptr)
      delete this->next;
    if (this->child != nullptr)
      delete this->child;
  }

  void set_pos(WindowPosition window_position) {
    this->window_position = window_position;
    this->flags |= WindowComponentFlags::FWIN_INIT;
  }

  uint16_t left_bound() const {
    return this->window_position.x();
  }

  uint16_t right_bound() const {
    return this->left_bound() + this->width();
  }

  uint8_t top_bound() const {
    return this->window_position.y();
  }

  uint8_t bottom_bound() const {
    return this->top_bound() + this->height();
  }

  inline void select() {
    this->flags |= WindowComponentFlags::FWIN_SELECTED;
  }

  inline void deselect() {
    this->flags ^= WindowComponentFlags::FWIN_SELECTED;
  }

  ShowError add_child(WindowComponent child) {
    if (this->child != nullptr) {
      return ShowError::DUPLICATE_CHILD_INSERTION;
    }

    this->child = new WindowComponent(child);

    return ShowError::SUCCESS;
  }

  ShowError add_next(WindowComponent next) {
    if (this->next != nullptr) {
      return ShowError::DUPLICATE_NEIGHBOR_INSERTION;
    }

    this->next = new WindowComponent(next);

    return ShowError::SUCCESS;
  }

  void insert_next(WindowComponent to_insert) {
    if (this->next == nullptr) {
      this->add_next(to_insert);
    } else {
      WindowComponent* old_next = this->next;
      WindowComponent* new_next = new WindowComponent(to_insert);
      new_next->next = old_next;

      this->next = new_next;
    }
  }

  virtual uint16_t width() const {
    return 0;
  }

  virtual uint8_t height() const {
    return 0;
  }

  virtual void render(ComponentCanvas *canvas) {
  }

  friend class ComponentCanvas;
  friend class Application;
private:
  WindowPosition window_position;
  const Color page_background;
  WindowComponent* child;
  WindowComponent* next;
  ComponentCanvas canvas;
  uint8_t flags;

  WindowComponent(
    WindowPosition _window_position,
    Color _page_background,
    uint8_t _flags)
    : window_position(_window_position),
      page_background(_page_background),
      flags(_flags),
      canvas(ComponentCanvas(this)),
      next(nullptr),
      child(nullptr) {
    
    Serial.println("\t\tIn WindowComponent Constructor");
    // this->canvas = ComponentCanvas(this);
  }

  bool initialized() {
    return this->flags & WindowComponentFlags::FWIN_INIT;
  }

  ShowError render_row() {
    Serial.println("Rendering row...");
    for (WindowComponent* p = this; p != nullptr; p = p->next) {
      if (!p->initialized()) {
        return ShowError::COMPONENT_UNINIT_RENDER;
      }

      p->render(&p->canvas);  // maybe switch to after rendering children?
      p->render_child();
    }

    return ShowError::SUCCESS;
  }

  ShowError render_child() {
    if (this->child != nullptr) {
      return this->child->render_row();
    }

    return ShowError::SUCCESS;
  }
};

#define TASKBAR_HEIGHT 50

class Taskbar : public WindowComponent {
  uint16_t width() const override {
    return RIGHT_EDGE320;
  }

  uint8_t height() const override {
    return TASKBAR_HEIGHT;
  }

  void render(ComponentCanvas *canvas) override {
  }
};

class Application {
public:
  Application(char* _name, WindowComponent _root_component)
    : name(_name), root_component(_root_component) {
      this->root_component.set_pos(WindowPosition(0, 50));
  }

  ShowError render() {
    Serial.println("Render frame");
    return this->root_component.render_row();
  }

private:
  char* name;
  WindowComponent root_component;
};

#define MAX(a, b) ((a > b ? a : b))
#define MIN(a, b) ((a < b ? a : b))

template<typename T>
T fit(T val, T min, T max) {
  return MAX(MIN(val, max), min);
}

void ComponentCanvas::drawFastHLine(int16_t x, int16_t y, int16_t w, Color color) {
  uint16_t left_bound = this->component->left_bound();
  uint16_t right_bound = this->component->right_bound();

  auto fit_x = fit<uint16_t>(x + left_bound, left_bound, right_bound);

  uint8_t top_bound = this->component->top_bound();
  uint8_t bottom_bound = this->component->bottom_bound();

  auto fit_y = fit<uint8_t>(y + top_bound, top_bound, bottom_bound);

  auto fit_w = fit<uint8_t>(fit_x + w, left_bound, right_bound);

  tft.drawFastHLine(fit_x, fit_y, fit_w, color.raw);
}

void ComponentCanvas::drawFastVLine(int16_t x, int16_t y, int16_t h, Color color) {
  uint16_t left_bound = this->component->left_bound();
  uint16_t right_bound = this->component->right_bound();

  auto fit_x = fit<uint16_t>(x + left_bound, left_bound, right_bound);

  uint8_t top_bound = this->component->top_bound();
  uint8_t bottom_bound = this->component->bottom_bound();

  auto fit_y = fit<uint8_t>(y + top_bound, top_bound, bottom_bound);

  auto fit_h = fit<uint8_t>(fit_y + h, left_bound, right_bound);

  tft.drawFastVLine(fit_x, fit_y, fit_h, color.raw);
}

void ComponentCanvas::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Color color) {
  uint16_t left_bound = this->component->left_bound();
  uint16_t right_bound = this->component->right_bound();

  auto fit_x = fit<uint16_t>(x + left_bound, left_bound, right_bound);

  uint8_t top_bound = this->component->top_bound();
  uint8_t bottom_bound = this->component->bottom_bound();

  auto fit_y = fit<uint8_t>(y + top_bound, top_bound, bottom_bound);

  auto fit_w = fit<uint16_t>(fit_x + w, left_bound, right_bound);
  auto fit_h = fit<uint8_t>(fit_y + h, top_bound, bottom_bound);

  tft.fillRect(fit_x, fit_y, fit_w, fit_h, color.raw);
}

void write_str(int16_t x, int16_t y, char& str) {
  auto cursor_x = tft.cursor_x, cursor_y = tft.cursor_y;
}

void k() {
  // tft.println()
  // tft.fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
}

class RedPane : public WindowComponent {
public:
  RedPane() : WindowComponent(BLUE) {
    Serial.println("\tIn RP Constructor");
  };
  
  uint16_t width() const override {
    return 200;
  }

  uint8_t height() const override {
    return 100;
  }

  void render(ComponentCanvas *canvas) override {
    Serial.println("\toverride render()");
    canvas->fillRect(0, 0, 200, 100, RED);
  }
};

Application *app;

void setup() {
  Serial.begin(500000);
  Serial.println("Welcome to the ODROID-SHOW");

  tft.begin();
  // initialize the digital pin as an output for LED Backlibht
  initPins();

  tft.setRotation(1);
  tft.setTextSize(2);
  tft.setCursor(50, 50);
  tft.print("Mateo's Odroid Show");
  tft.setCursor(250, 200);
  tft.print(version);

  delay(1000);
  // tft.fillScreen(backgroundColor);
  // tft.setCursor(0, 0);

  Serial.println("Making RP...");

  RedPane rp = RedPane();

  Serial.println("Made RP");

  Serial.flush();

  app = new Application("main", rp);

  // tft.fillRect(0, 0, 20, 20, tft.Color565(0xff, 0, 0));

  // Timer1.initialize(20000);
  // Timer1.attachInterrupt(timerCallback);
}

void initPins() {
  pinMode(ledPin, OUTPUT);
  pinMode(3, OUTPUT);
  pinMode(4, OUTPUT);
  pinMode(6, OUTPUT);
  pinMode(7, INPUT);
  pinMode(A0, INPUT);
  pinMode(A1, INPUT);

  analogWrite(ledPin, pwm);
}

unsigned char btn0Presses = 0;
unsigned char btn0Releases = 0;
unsigned char btn1Presses = 0;
unsigned char btn1Releases = 0;
unsigned char btn2Presses = 0;
unsigned char btn2Releases = 0;

unsigned char btn0Pushed = 0;
unsigned char btn1Pushed = 0;
unsigned char btn2Pushed = 0;

void readBtn() {
  if (!digitalRead(A1) && (btn2Presses == 0)) {
    btn2Presses = 1;
    btn2Releases = 0;
    btn2Pushed = 1;
    digitalWrite(6, LOW);
  }

  if (digitalRead(A1) && (btn2Releases == 0)) {
    btn2Releases = 1;
    btn2Presses = 0;
    btn2Pushed = 0;
    digitalWrite(6, HIGH);
  }

  if (!digitalRead(7) && (btn0Presses == 0)) {
    btn0Presses = 1;
    btn0Releases = 0;
    btn0Pushed = 1;
    if (pwm > 225)
      pwm = 255;
    else
      pwm += 30;
    analogWrite(ledPin, pwm);
    digitalWrite(3, LOW);
  }

  if (digitalRead(7) && (btn0Releases == 0)) {
    btn0Releases = 1;
    btn0Presses = 0;
    btn0Pushed = 0;
    digitalWrite(3, HIGH);
  }

  if (!digitalRead(A0) && (btn1Presses == 0)) {
    btn1Presses = 1;
    btn1Releases = 0;
    btn1Pushed = 1;
    if (pwm < 30)
      pwm = 0;
    else
      pwm -= 30;
    analogWrite(ledPin, pwm);
    digitalWrite(4, LOW);
  }

  if (digitalRead(A0) && (btn1Releases == 0)) {
    btn1Releases = 1;
    btn1Presses = 0;
    btn1Pushed = 0;
    digitalWrite(4, HIGH);
  }
}

void loop(void) {
  Serial.println("Attempting to render...");
  app->render();
  delay(5000);
}