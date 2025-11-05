#include <Adafruit_SH110X.h>
#include <Adafruit_GFX.h>
//#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Arduino_JSON.h>

#include "sysfont7pt7b.h" // menu font
#include "FindersKeepers8pt7b.h" // files font

#define OLED_RESET -1
#define WINDOW_TYPE_REGULAR 0
#define WINDOW_TYPE_ROUNDED 1
#define WINDOW_TYPE_ALARM 2

#define CURSOR_TYPE_POINTER 0
#define CURSOR_TYPE_CROSSHAIR 1

// Generic funtion
typedef void (*Action) ();

struct window {
  int id;
  String title;
  int x;
  int y;
  int width;
  int height;
  byte windowType;
};

struct app {
  int id; // should match window
  String title;
  int x; //relevant for desktop app only
  int y; //relevant for desktop app only
  bool isOpen;
  bool isSelected;
  bool isDesktopApp;
  int cursorType; // cursor type while in the window
  window appWindow;
};

struct menu {
  String title;
  String items[5];
  bool enabledItems[5];
  Action callbacks[5];
  int itemsCount;
  int x0;
  int menuWidth; // width in main menu
  int dropdownWidth;
};

// I2C
Adafruit_SH1107 display = Adafruit_SH1107(128, 128, &Wire, OLED_RESET, 1000000, 100000);
GFXcanvas1 displayBuffer(128, 90);

// WiFi
char ssid[] = ""; // your network SSID (name)
char password[] = ""; // your network key
WiFiClientSecure client;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
// Create a WebSocket object
AsyncWebSocket ws("/ws");
// Cache client
AsyncWebSocketClient * globalClient = NULL;

// Json Variable to send info back
JSONVar deviceInfo; //TODO
JSONVar jsonReceived;

// clock
#define A_MINUTE 60000
const long utcOffsetInSeconds = 3600;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);
unsigned long lastClockUpdate = 0;
bool forceClockUpdate = true;
window clockWindow = {701, "--:--", 25, 30, 80, 0, WINDOW_TYPE_ALARM};

// Mouse
int cursorType = CURSOR_TYPE_POINTER;
int prevMouseX = -1;
int prevMouseY = -1;
int mouseX = 32;
int mouseY = 32;
bool mouseDown = false;
bool prevMouseDown = false;
unsigned long timeLastClick = 0;
#define DOUBLE_CLICK_SPEED  500 // less than 500 ms
#define MOUSE_WIDTH 10
#define MOUSE_HEIGHT 16

// Menu
void OpenClock();
void Quit();
int selectedMenu = -1; //0 = apple menu, 1 = File, -1 = None
#define MENU_ITEMS 2
menu menus[MENU_ITEMS] = {
  {"apple", 
  {"About...", "-", "Clock", "-"},
  {false, false, true, false},
  {null, null, OpenClock, null},
  3, 9, 9, -1 },
  
  {"File",
  {"Quit"},
  {false},
  {Quit},
  1, 33, 21, -1}
};

int selectedDropdownItem[] = {-1, -1}; // main menu index, item index
#define MENU_HIGHLIGHT_PADDING 10
#define MENU_DETECT_PADDING 8
#define MENU_SPACING 15
#define MENU_FIRST_X 19
#define SUBMENU_ITEM_HEIGHT 15
#define SUBMENU_ITEM_H_OFFSET 13

// Window
bool hasWindow = true;
bool pressingOnCloseButton = false;
window currentWindow = clockWindow;
const byte cornerPixelsOffset[] = { // x, -y form. Offset is from full height of window (so from bottom to top)
  0, 5,
  0, 4,
  0, 3,  1, 3,
  0, 2,  1, 2,  2, 2,
  0, 1,  1, 1,  2, 1,  3, 1,  4, 1
};
#define TITLE_BAR_HEIGHT 19

// app
bool hasApp = true; // desktop app show for 
app currentApp = {707, // current desktop app // use default app for showing IP 
                  "Test App",
                  80, 28,
                  false, false, 
                  true, // is desktop app
                  CURSOR_TYPE_POINTER,
                  {707, "--:--", 25, 30, 80, 0, WINDOW_TYPE_ALARM}
                 };
#define APP_DIMENSION 32
bool hasMenuApp = false; //TODO
app currentMenuApp = {124,
                      "Test Menu App",
                      0,0,
                      false, false,
                      false,
                      CURSOR_TYPE_POINTER,
                      {124, "Control Panel", 1, 21, 125, 48, WINDOW_TYPE_REGULAR}
                     };

byte lastAppBuffer[70 * 128 / 8]; //1120
bool lastAppBufferSet = false;
bool receivingBuffer = false;
int receivingBufferAppId = -1;
int lastAppBufferIndex = 0; // track where we should start inserting in AppBuffer.
bool appBufferShouldDraw = false;

// bg tile
byte bgTile[8] = {0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa};
bool bgTileRequested = false;

bool hasMessage = false; //TODO test
String lastMessage ="";

bool requestScreenRefresh = false;

/*-- Rounded window corners --*/
// 'topLeft', 6x19px
const unsigned char rw_topLeft [] PROGMEM = {
   0x04, 0x1c, 0x3c, 0x7c, 0x7c, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 
  0xfc, 0xfc, 0xfc
};
// 'topRight', 6x19px
const unsigned char rw_topRight [] PROGMEM = {
  0x80, 0xe0, 0xf0, 0xf8, 0xf8, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 
  0xfc, 0xfc, 0xfc
};
// 'bottomLeft', 6x6px
const unsigned char rw_bottomLeft [] PROGMEM = {
  0xc0, 0x40, 0x60, 0x30, 0x1c, 0x04
};
// 'bottomRight', 6x6px
const unsigned char rw_bottomRight [] PROGMEM = {
  0x0c, 0x08, 0x18, 0x30, 0xe0, 0x80
};

/*-- Other sprites --*/
// 'happyMac', 25x32px
const unsigned char happyMac [] PROGMEM = {
  0xbf, 0xff, 0xfe, 0x80, 0x40, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00, 0x80, 0x8f, 0xff, 0xf8, 0x80, 
  0x90, 0x00, 0x04, 0x80, 0x90, 0x00, 0x04, 0x80, 0x90, 0x00, 0x04, 0x80, 0x90, 0x88, 0x84, 0x80, 
  0x90, 0x88, 0x84, 0x80, 0x90, 0x08, 0x04, 0x80, 0x90, 0x08, 0x04, 0x80, 0x90, 0x18, 0x04, 0x80, 
  0x90, 0x00, 0x04, 0x80, 0x90, 0x42, 0x04, 0x80, 0x90, 0x3c, 0x04, 0x80, 0x90, 0x00, 0x04, 0x80, 
  0x90, 0x00, 0x04, 0x80, 0x8f, 0xff, 0xf8, 0x80, 0x80, 0x00, 0x00, 0x80, 0x80, 0x00, 0x00, 0x80, 
  0x80, 0x00, 0x00, 0x80, 0x80, 0x00, 0x00, 0x80, 0x98, 0x01, 0xf8, 0x80, 0x80, 0x00, 0x00, 0x80, 
  0x80, 0x00, 0x00, 0x80, 0x80, 0x00, 0x00, 0x80, 0x80, 0x00, 0x00, 0x80, 0x7f, 0xff, 0xff, 0x00, 
  0xc0, 0x00, 0x01, 0x80, 0x40, 0x00, 0x01, 0x00, 0xc0, 0x00, 0x01, 0x80, 0x7f, 0xff, 0xff, 0x00
};

// 'app', 32x32px
const unsigned char app_icon [] PROGMEM = {
  0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x80, 0x00, 0x00, 0x04, 0x40, 0x00, 0x00, 0x08, 0x20, 0x00, 
  0x00, 0x10, 0x10, 0x00, 0x00, 0x20, 0x08, 0x00, 0x00, 0x40, 0x04, 0x00, 0x00, 0x80, 0x02, 0x00, 
  0x01, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x80, 0x04, 0x00, 0x00, 0x40, 0x08, 0x00, 0x00, 0x20, 
  0x10, 0x00, 0x00, 0x10, 0x20, 0x00, 0x00, 0x08, 0x40, 0x00, 0x3f, 0x04, 0x80, 0x00, 0x40, 0x82, 
  0x40, 0x00, 0x80, 0x41, 0x20, 0x01, 0x30, 0x22, 0x10, 0x01, 0xc8, 0x14, 0x08, 0x0e, 0x7f, 0x8f, 
  0x04, 0x02, 0x30, 0x07, 0x02, 0x01, 0x00, 0x07, 0x01, 0x00, 0x80, 0x07, 0x00, 0x80, 0x60, 0x07, 
  0x00, 0x40, 0x1f, 0xe7, 0x00, 0x20, 0x02, 0x1f, 0x00, 0x10, 0x04, 0x07, 0x00, 0x08, 0x08, 0x00, 
  0x00, 0x04, 0x10, 0x00, 0x00, 0x02, 0x20, 0x00, 0x00, 0x01, 0x40, 0x00, 0x00, 0x00, 0x80, 0x00
};

// 'app_icon_bg', 32x32px
const unsigned char app_icon_bg [] PROGMEM = {
  0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x07, 0xc0, 0x00, 0x00, 0x0f, 0xe0, 0x00, 
  0x00, 0x1f, 0xf0, 0x00, 0x00, 0x3f, 0xf8, 0x00, 0x00, 0x7f, 0xfc, 0x00, 0x00, 0xff, 0xfe, 0x00, 
  0x01, 0xff, 0xff, 0x00, 0x03, 0xff, 0xff, 0x80, 0x07, 0xff, 0xff, 0xc0, 0x0f, 0xff, 0xff, 0xe0, 
  0x1f, 0xff, 0xff, 0xf0, 0x3f, 0xff, 0xff, 0xf8, 0x7f, 0xff, 0xff, 0xfc, 0xff, 0xff, 0xff, 0xfe, 
  0x7f, 0xff, 0xff, 0xff, 0x3f, 0xff, 0xff, 0xfe, 0x1f, 0xff, 0xff, 0xfc, 0x0f, 0xff, 0xff, 0xff, 
  0x07, 0xff, 0xff, 0xff, 0x03, 0xff, 0xff, 0xff, 0x01, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 
  0x00, 0x7f, 0xff, 0xff, 0x00, 0x3f, 0xfe, 0x1f, 0x00, 0x1f, 0xfc, 0x07, 0x00, 0x0f, 0xf8, 0x00, 
  0x00, 0x07, 0xf0, 0x00, 0x00, 0x03, 0xe0, 0x00, 0x00, 0x01, 0xc0, 0x00, 0x00, 0x00, 0x80, 0x00
};

// 'closeIcon', 9x9px
const unsigned char closeIcon [] PROGMEM = {
  0x08, 0x00, 0x49, 0x00, 0x2a, 0x00, 0x00, 0x00, 0xe3, 0x80, 0x00, 0x00, 0x2a, 0x00, 0x49, 0x00, 
  0x08, 0x00
};

// 'apple', 9x11px
const unsigned char apple [] PROGMEM = {
  0x06, 0x00, 0x0c, 0x00, 0x08, 0x00, 0x77, 0x00, 0xff, 0x80, 0xfe, 0x00, 0xfe, 0x00, 0xff, 0x80, 
  0xff, 0x80, 0x7f, 0x00, 0x36, 0x00
};

/*
// 'bg_tile', 8x8px
const unsigned char bg_tile [] PROGMEM = {
  //0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0xFF
  //0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa 
  //0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55
  0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa
  //0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  //0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 
  //0xaa, 0x55, 0xab, 0x57, 0xaf, 0x5f, 0xbf, 0x7f
};*/

// 'mouse', 10x16px
const unsigned char mouse [] PROGMEM = {
  0x00, 0x00, 0x40, 0x00, 0x60, 0x00, 0x70, 0x00, 0x78, 0x00, 0x7c, 0x00, 0x7e, 0x00, 0x7f, 0x00, 
  0x7f, 0x80, 0x7c, 0x00, 0x6c, 0x00, 0x46, 0x00, 0x06, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00
};

// 'mouseWhite', 10x16px
const unsigned char mouseWhite [] PROGMEM = {
  0xc0, 0x00, 0xe0, 0x00, 0xf0, 0x00, 0xf8, 0x00, 0xfc, 0x00, 0xfe, 0x00, 0xff, 0x00, 0xff, 0x80, 
  0xff, 0xc0, 0xff, 0xc0, 0xfe, 0x00, 0xef, 0x00, 0xcf, 0x00, 0x07, 0x80, 0x07, 0x80, 0x03, 0x80
};

void setup()   {                
  Serial.begin(9600);  

  delay(250);
  Serial.println("Trying to set up display");
  if ( ! display.begin(0x3C, true) ) {
     Serial.println("Unable to initialize OLED");
     while (1) yield();
  }
  Serial.println("Finished setting up display");
  //display.setContrast(255);
  
  ClearScreen();
  DrawStartup();

  initWiFi();
  initWebSocket();
  currentApp.title = WiFi.localIP().toString();

  // start server
  server.begin();

  // time
  timeClient.begin();

  // setup dropdown
  UpdateDropdownWidth(0);
  UpdateDropdownWidth(1);

  // draw screen
  //display.setContrast(100);
  display.setContrast(100);

  
  ClearScreen();
  DrawMenu(); // needs to run once to avoid weird cursor position bug

  DrawEntireScreen();
}

void loop() 
{
  if (receivingBuffer)
  {
    return; // just wait
  }
  
  if (requestScreenRefresh)
  {
    requestScreenRefresh = false;
    DrawEntireScreen();
  }

  CheckClickAndHoverAppWindow(false);
  if (prevMouseX != mouseX || prevMouseY != mouseY)
  {
    DrawMouse(mouseX, mouseY);
  }
  
  if (prevMouseDown != mouseDown)
  {
    prevMouseDown = mouseDown;
    CheckClickAndHoverAppWindow(true);
    
    Serial.print("Mousedown:");
    Serial.println(mouseDown);
    if (!mouseDown) // went from pressed to released == click
    {
      OnClick();
    }
  }

  //TODO testing
  /*if (hasMessage)
  {
    hasMessage = false;
    Serial.println("has message:");
    Serial.println(lastMessage);
  }*/

  CheckForBgTileRequest();
  CheckForAppBufferUpdate();
  CheckUpdateClock();
  CheckMainMenuSelected();
  CheckDropdownItemSelected();
  CheckCloseWindow();
  
  ws.cleanupClients();
}

void OnClick()
{
  if (selectedDropdownItem[0] >= 0) // Clicking on dropdown menu item
  {
    Serial.print("Click on menu-");
    Serial.print(selectedDropdownItem[0]);
    Serial.print(" > item-");
    Serial.println(selectedDropdownItem[1]);

    if (menus[selectedDropdownItem[0]].enabledItems[selectedDropdownItem[1]]) // if enabled
    {
      Action selectedAction = menus[selectedDropdownItem[0]].callbacks[selectedDropdownItem[1]];
      if (selectedAction != null)
      {
        CheckMainMenuSelected(); // close menu before going furtheer
        selectedAction();
        return; // don't check further clicks
      }
    }
  }

  if (millis() - timeLastClick < 500)
  {
    OnDoubleClick();
    CheckClickApp(true);
  }
  else
  {
    CheckClickApp(false);
  }

  timeLastClick = millis();
}

void OnDoubleClick()
{
  Serial.println("double click");
}

/* --   MENU CALLBACKS   -- */
void OpenClock()
{
  CloseCurrentWindow();
  currentWindow = clockWindow;
  hasWindow = true;
  DrawEntireScreen();
  forceClockUpdate = true;
  //sendWsMessage("CLOCK OPENED");
}

void Quit()
{
  CloseCurrentWindow();
}

void OpenCustomMenu()
{
  if (!hasMenuApp)
  {
    return;
  }
  CloseCurrentWindow();
  currentWindow = currentMenuApp.appWindow;
  hasWindow = true;
  RequestAppBufferUpdate(currentMenuApp);
  DrawEntireScreen();

  // enable the quit option
  menus[1].enabledItems[0] = true;
}

/* -- END MENU CALLBACKS -- */

void ClearScreen()
{
  // clear display and buffer
  display.clearDisplay();
  displayBuffer.fillScreen(0);
}

void DrawEntireScreen()
{
  ClearScreen();
  DrawBg(false);
  DrawApps();
  if (hasWindow)
  {
    DrawWindow(currentWindow);
  }
  DrawMenu();
  DrawCorners();
  display.drawBitmap(0,0,displayBuffer.getBuffer(),displayBuffer.width(), displayBuffer.height(), SH110X_WHITE, SH110X_BLACK);
  DrawMouse(mouseX, mouseY);
}

void RefreshMenuOnly()
{
  DrawMenu();
  DrawCorners();
  display.drawBitmap(0,0,displayBuffer.getBuffer(),displayBuffer.width(), displayBuffer.height(), SH110X_WHITE, SH110X_BLACK);
  DrawMouse(mouseX, mouseY);
}

void DrawSelectedPortionScreen(int x0, int y0, int width, int height)
{
  int x = 0;
  int y = 0;
  
  for (int r = 0; r < width; r++)
  {
    for (int c = 0; c < height; c++)
    {
      x = x0 + r;
      y = y0 + c;
      display.drawPixel(x, y, displayBuffer.getPixel(x, y) ? SH110X_WHITE : SH110X_BLACK);
    }
  }
}

void DrawSelectedOutlineScreen(int x0, int y0, int width, int height) // similar to portion, but unfilled
{
  for (int x = x0; x < x0 + width; x++)
  {
    display.drawPixel(x, y0, displayBuffer.getPixel(x, y0) ? SH110X_WHITE : SH110X_BLACK);
    display.drawPixel(x, y0 + height - 1, displayBuffer.getPixel(x, y0 + height - 1) ? SH110X_WHITE : SH110X_BLACK);
  }
  for (int y = y0; y < y0 + height; y++)
  {
    display.drawPixel(x0, y, displayBuffer.getPixel(x0, y) ? SH110X_WHITE : SH110X_BLACK);
    display.drawPixel(x0 + width - 1, y, displayBuffer.getPixel(x0 + width - 1, y) ? SH110X_WHITE : SH110X_BLACK);
  }
}

void CheckForBgTileRequest()
{
  //TODO
  if(bgTileRequested)
  {
    bgTileRequested = false;
    String json = "{\"bgTileResponse\":{";
    for (int i = 0; i < 8; i++)
    {
      json += "\"" + String(i) + "\":" + String(bgTile[i]);
      if (i != 7)
      {
        json += ",";
      }
    }
    json += "}}";

    Serial.print("json tile response:");
    Serial.println(json);
    
    sendWsMessage(json);
  }
}

void CheckForAppBufferUpdate()
{
  if (lastAppBufferSet)
  {
    lastAppBufferSet = false;
    if (hasWindow && receivingBufferAppId == currentWindow.id)
    {   
      appBufferShouldDraw = true;
      Serial.print("App buffer should draw: TRUE");
      DrawEntireScreen();
    }
  }
}

void RequestAppBufferUpdate(app a)
{
  String message = "{\"request\":" + String(a.id) + "}";
  sendWsMessage(message);
}

void DrawAppBuffer(window w, int color)
{
  for (int y = 0; y < w.height; y++)
  {
    for (int x = 0; x < w.width; x++)
    {
      int byteIndex = y * (w.width/8) + x/8;
      int bitPosition = 7 - (x % 8);
      byte byteValue = lastAppBuffer[byteIndex];
   
      // Extract the pixel value at the specified bit position
      int pixelValue = (byteValue >> bitPosition) & 0x01;

      if (pixelValue == 1)
      {
        displayBuffer.drawPixel(w.x + x, w.y + y + TITLE_BAR_HEIGHT, color);
      }
    }
  }
}

void UpdateDropdownWidth(byte menu)
{
  int16_t  x1, y1;
  uint16_t h;
  displayBuffer.setFont(&sysfont7pt7b);
  
  // get max width needed for these items
  int maxWidth = -1;
  for (int i = 0; i < menus[menu].itemsCount; i++)
  {
    uint16_t w;
    displayBuffer.getTextBounds(menus[menu].items[i], 0, 0, &x1, &y1, &w, &h);
    if (w > maxWidth)
    {
      maxWidth = w;
    }
  }
  
  // set width
  menus[menu].dropdownWidth = maxWidth + MENU_FIRST_X + 1;
}

void CheckUpdateClock()
{
  if (forceClockUpdate || millis() - lastClockUpdate >= A_MINUTE)
  {
    forceClockUpdate = false;
    
    if (hasWindow && currentWindow.windowType == WINDOW_TYPE_ALARM)
    {
      //while(!timeClient.update()) {
      timeClient.forceUpdate();
      //}
      int minutes = timeClient.getMinutes();
      String minutesStr = minutes < 10 ? ("0" + String(minutes)) : String(minutes);
      String hoursStr = String(timeClient.getHours());
      String formattedStr = hoursStr + ":" + minutesStr;
      currentWindow.title = formattedStr;
  
      Serial.println("Update Clock");
      Serial.println(currentWindow.title);
      DrawEntireScreen();
    }

    // adjust update to fix minutes being too late
    unsigned long fix = timeClient.getSeconds() * 1000;
    lastClockUpdate = millis() - fix;
    Serial.print("fix:");
    Serial.print(fix);
    Serial.print(" millis:");
    Serial.print(millis());
    Serial.print(" lastclockupdate:");
    Serial.println(lastClockUpdate);
  }
}

void CheckCloseWindow()
{
  if (selectedMenu == -1 && hasWindow) // if menu closed and showing window
  {
    // coordinates closing box
    int x0 = currentWindow.x + 9;
    int y0 = currentWindow.y + 4;
    int x1 = x0 + 10;
    int y1 = y0 + 10;

    bool previousPressingOnCloseButton = pressingOnCloseButton;
    bool hoveringOverCloseButton = false;
    
    if (mouseX >= x0 && mouseX <= x1 && mouseY >= y0 && mouseY <= y1)
    {
      hoveringOverCloseButton = true;
      pressingOnCloseButton = mouseDown;
    }
    else
    {
      pressingOnCloseButton = false;
    }

    if (!previousPressingOnCloseButton && pressingOnCloseButton) // started pressing
    {
      displayBuffer.drawBitmap(x0 + 1, y0 + 1, closeIcon, 9, 9, currentWindow.windowType == WINDOW_TYPE_ROUNDED ? SH110X_WHITE : SH110X_BLACK);
      DrawSelectedPortionScreen(x0, y0, 10, 10);
      DrawMouse(mouseX, mouseY); // make sure mouse draws over it and call display.display();
    }
    else if (previousPressingOnCloseButton && !pressingOnCloseButton) // stopped pressing
    {
      displayBuffer.fillRect(x0 + 1, y0 + 1, 9, 9, currentWindow.windowType == WINDOW_TYPE_ROUNDED ? SH110X_BLACK : SH110X_WHITE);
      DrawSelectedPortionScreen(x0, y0, 10, 10);
      DrawMouse(mouseX, mouseY); // make sure mouse draws over it and call display.display();
      //display.display();

      if (!mouseDown && hoveringOverCloseButton)
      {
        //TODO close window
        Serial.println("Should close window now");
        //hasWindow = false;

        CloseCurrentWindow();
        /*if (hasApp && currentApp.id == currentWindow.id)
        {
          currentApp.isSelected = true;
          DrawWindowAnimation(currentApp, false); //TODO test
          currentApp.isOpen = false;
        }
        DrawEntireScreen();
        */
      }
    }
  }
}

void CloseCurrentWindow()
{
  if (!hasWindow)
  {
    return;
  }

  // mark closed
  hasWindow = false;
  appBufferShouldDraw = false;
  Serial.print("App buffer should draw: FALSE");

  if (hasApp && currentApp.id == currentWindow.id) // close with animation
  {
    currentApp.isSelected = true;
    DrawWindowAnimation(currentApp, false);
    currentApp.isOpen = false;
  }

  // disable quit menu option
  menus[1].enabledItems[0] = false;

  // redraw
  DrawEntireScreen();
}

void CheckDropdownItemSelected()
{
  int selectedDropdownMenuIndex = selectedDropdownItem[0];
  int selectedDropdownItemIndex = selectedDropdownItem[1];

  if (selectedMenu == -1 || mouseY < 20)
  {
    selectedDropdownMenuIndex = -1;
    selectedDropdownItemIndex = -1;
  }
  else
  {
    menu m = menus[selectedMenu];
    int y0 = 20;
    int height = m.itemsCount * SUBMENU_ITEM_HEIGHT;

    // if in bounds of currentMenu
    if(mouseX > m.x0 && mouseX < m.x0 + m.dropdownWidth && mouseY > y0 && mouseY < y0 + height)
    {
      selectedDropdownMenuIndex = selectedMenu;
      selectedDropdownItemIndex = constrain((mouseY - y0) / SUBMENU_ITEM_HEIGHT, 0, m.itemsCount);
      if (!m.enabledItems[selectedDropdownItemIndex]) // if disabled don't have it selected
      {
        selectedDropdownItemIndex = -1;
      }
    }
    else
    {
      selectedDropdownMenuIndex = -1;
      selectedDropdownItemIndex = -1;
    }
  }

  if(selectedDropdownMenuIndex != selectedDropdownItem[0] || selectedDropdownItemIndex != selectedDropdownItem[1])
  {
    bool menuChanged = selectedDropdownMenuIndex != selectedDropdownItem[0];
    selectedDropdownItem[0] = selectedDropdownMenuIndex;
    selectedDropdownItem[1] = selectedDropdownItemIndex;

    // refresh only because something changed
    if (menuChanged) // major change
    {
      DrawEntireScreen();
    }
    else
    {
      RefreshMenuOnly();
    }
  }
}

void CheckMainMenuSelected()
{
  int newSelectedMenu = selectedMenu;
  if (mouseDown)
  {
    if (mouseY < 20)
    {
      newSelectedMenu = -1;
      int xPos = MENU_FIRST_X;
      for(int i = 0; i < MENU_ITEMS; i++)
      {
        if (mouseX >= (xPos - MENU_DETECT_PADDING) && mouseX <= (xPos + menus[i].menuWidth + MENU_DETECT_PADDING))
        {
          newSelectedMenu = i;
          break; 
        }
        xPos += menus[i].menuWidth + MENU_SPACING;
      }
    }
  }
  else
  {
    newSelectedMenu = -1;
  }

  if(selectedMenu != newSelectedMenu)
  {
    selectedMenu = newSelectedMenu;    
    // refresh only because something changed
    DrawEntireScreen();
  }
}

void CheckClickAndHoverAppWindow(bool mouseDownChanged)
{
  // check clicking in app window
  if (hasWindow && currentWindow.windowType != WINDOW_TYPE_ALARM && selectedMenu == -1) // only check if theres a window (non alarm) and not in menu
  {
    int previousCursorType = cursorType;
    if (mouseX >= currentWindow.x && mouseX < currentWindow.x + currentWindow.width && mouseY >= currentWindow.y + TITLE_BAR_HEIGHT && mouseY < currentWindow.y + currentWindow.height + TITLE_BAR_HEIGHT)
    {
        // is hovering over app window 
        if (currentMenuApp.id == currentWindow.id)
        {
          cursorType = currentMenuApp.cursorType;
        }
        else
        {
          cursorType = currentApp.cursorType;
        }

        if (mouseDownChanged)
        {
          // send cursor relative to app
          int relativeX = mouseX - currentWindow.x;
          int relativeY = mouseY - (currentWindow.y + TITLE_BAR_HEIGHT);
          String message = "{\"app\":" + String(currentWindow.id) + ", \"x\":" + String(relativeX) + ", \"y\":" + String(relativeY) + ", \"mouseDown\":" + String(mouseDown) + "}";
          sendWsMessage(message);
        }
    }
    else // not hovering
    {
      cursorType = CURSOR_TYPE_POINTER; //default
    }

    if (previousCursorType != cursorType) // if cursorType changed
    {
      // draw over previous area (bit larger to encompass both mouses so we leave no residue)
      DrawSelectedPortionScreen(prevMouseX - 5, prevMouseY - 5,MOUSE_WIDTH + 5, MOUSE_HEIGHT + 5);
    }
  }
}

void CheckClickApp(bool doubleClick) // check clicking on app icon to open it or select it
{
  if (selectedMenu >= 0 || !hasApp) // return if in menu or no app
  {
    return;
  }

  bool isSelected = currentApp.isSelected;
  bool reDraw = false;

  // calculate if hitting app, then check if window is in front of it
  if (mouseX >= currentApp.x && mouseX < currentApp.x + APP_DIMENSION && mouseY >= currentApp.y && mouseY < currentApp.y + APP_DIMENSION)
  {
    currentApp.isSelected = true;
    if (hasWindow && mouseX >= currentWindow.x && mouseX < currentWindow.x + currentWindow.width && mouseY >= currentWindow.y && mouseY < currentWindow.y + currentWindow.height + TITLE_BAR_HEIGHT)
    {
      currentApp.isSelected = false;
    }
  }
  else
  {
    currentApp.isSelected = false;
  }

  if (isSelected != currentApp.isSelected)
  {
    reDraw = true;
  }

  if (doubleClick && !currentApp.isOpen && currentApp.isSelected)
  {
    currentApp.isOpen = true;
    DrawWindowAnimation(currentApp, true);

    hasWindow = true;
    currentWindow = currentApp.appWindow;
    reDraw = true;

    if (currentWindow.windowType == WINDOW_TYPE_ALARM)
    {
      forceClockUpdate = true;
    }
    else
    {
      RequestAppBufferUpdate(currentApp);
      // enable the quit option
      menus[1].enabledItems[0] = true;
    }
  }
  
  if (reDraw)
  {
    DrawEntireScreen();
  }
}

void DrawStartup()
{
  DrawBg(true); // default bg
  displayBuffer.fillRect(51,29,25,32, SH110X_WHITE);
  displayBuffer.drawBitmap(51, 29, happyMac, 25, 32, SH110X_BLACK);
  DrawCorners();
  display.drawBitmap(0,0,displayBuffer.getBuffer(),displayBuffer.width(), displayBuffer.height(), SH110X_WHITE, SH110X_BLACK);
  display.display();
}

void DrawApps()
{
  if (!hasApp)
  {
    return;
  }
  
  // draw icon
  displayBuffer.drawBitmap(currentApp.x, currentApp.y, app_icon_bg, APP_DIMENSION, APP_DIMENSION, currentApp.isSelected ? SH110X_BLACK : SH110X_WHITE);
  if (!currentApp.isOpen)
  {
    displayBuffer.drawBitmap(currentApp.x, currentApp.y, app_icon, APP_DIMENSION, APP_DIMENSION, currentApp.isSelected ? SH110X_WHITE : SH110X_BLACK);
  }
  else // draw dotted pattern over bg
  {
    bool toggle = true;
    for (int y = 0; y < APP_DIMENSION; y++)
    {
      for (int x = 0; x < APP_DIMENSION; x+=2)
      {
        int byteIndex = y * (APP_DIMENSION / 8) + x / 8;
        int bitPosition = 7 - (x % 8);
        
        // Read the byte value from PROGMEM
        int byteValue = pgm_read_byte_near(app_icon_bg + byteIndex);
        
        // Extract the pixel value at the specified bit position
        int pixelValue = (byteValue >> bitPosition) & 0x01;

        if (pixelValue == 1 && toggle)
        {
          displayBuffer.drawPixel(currentApp.x + x, currentApp.y + y, currentApp.isSelected ? SH110X_WHITE : SH110X_BLACK);
        }
        toggle = !toggle;
      }
      toggle = !toggle;
    }
  }

  // get text width
  displayBuffer.setFont(&FindersKeepers8pt7b);
  int16_t  x1, y1;
  uint16_t widthTitle, hh;
  displayBuffer.getTextBounds(currentApp.title, 0, 0, &x1, &y1, &widthTitle, &hh);
  int textX = currentApp.x + 16 - (widthTitle / 2);

  // text bg
  displayBuffer.fillRect(textX - 4, currentApp.y + APP_DIMENSION, widthTitle + 8, 12, currentApp.isSelected ? SH110X_BLACK : SH110X_WHITE);

  // text
  displayBuffer.setCursor(textX, currentApp.y + 41); 
  displayBuffer.setTextColor(currentApp.isSelected ? SH110X_WHITE : SH110X_BLACK);
  displayBuffer.println(currentApp.title);
}

void DrawWindow(window w)
{
  //int titleBarHeight = 19;
  displayBuffer.setFont(&sysfont7pt7b);
  int16_t  x1, y1;
  uint16_t widthTitle, hh;
  displayBuffer.getTextBounds(w.title, 0, 0, &x1, &y1, &widthTitle, &hh);
  int textX = (w.width - widthTitle)/2;
  if (w.windowType == WINDOW_TYPE_ALARM)
  {
    textX += 9;
  }
  
  if (w.windowType == WINDOW_TYPE_ROUNDED)
  {
    displayBuffer.drawBitmap(w.x, w.y, rw_topLeft, 6, TITLE_BAR_HEIGHT, SH110X_BLACK);
    displayBuffer.drawBitmap(w.x + w.width - 6, w.y, rw_topRight, 6, TITLE_BAR_HEIGHT, SH110X_BLACK);
    displayBuffer.fillRect(w.x + 6, w.y, w.width - 12, TITLE_BAR_HEIGHT, SH110X_BLACK);

    // record pixels bottom corners
    bool pixelsLeft[12];
    bool pixelsRight[12];
    for (int i = 0; i < 12; i++)
    {
      int index = (i * 2);
      pixelsLeft[i] = displayBuffer.getPixel(w.x + cornerPixelsOffset[index], w.y + w.height + TITLE_BAR_HEIGHT - cornerPixelsOffset[index + 1]) == SH110X_BLACK;
      pixelsRight[i] = displayBuffer.getPixel(w.x + w.width - 1 - cornerPixelsOffset[index], w.y + w.height + TITLE_BAR_HEIGHT - cornerPixelsOffset[index + 1]) == SH110X_BLACK;
    }
    
    // clear body 
    displayBuffer.fillRect(w.x, w.y + 19, w.width, w.height, SH110X_WHITE);

    // draw inside
    if (appBufferShouldDraw)
    {
      DrawAppBuffer(w, SH110X_BLACK);
    }

    // draw window outline
    displayBuffer.drawFastVLine(w.x, w.y + TITLE_BAR_HEIGHT, w.height - 6, SH110X_BLACK);
    displayBuffer.drawFastVLine(w.x + w.width - 1, w.y + TITLE_BAR_HEIGHT, w.height - 6, SH110X_BLACK);
    displayBuffer.drawFastHLine(w.x + 6, w.y + w.height + TITLE_BAR_HEIGHT  - 1, w.width - 12, SH110X_BLACK);
    displayBuffer.drawBitmap(w.x, w.y + w.height + TITLE_BAR_HEIGHT - 6, rw_bottomLeft, 6, 6, SH110X_BLACK);
    displayBuffer.drawBitmap(w.x + w.width - 6, w.y + w.height + TITLE_BAR_HEIGHT - 6, rw_bottomRight, 6, 6, SH110X_BLACK);    

    // replace corner pixels
    for (int i = 0; i < 12; i++)
    {
      int index = (i * 2);
      displayBuffer.drawPixel(w.x + cornerPixelsOffset[index], w.y + w.height + TITLE_BAR_HEIGHT - cornerPixelsOffset[index + 1], pixelsLeft[i] ? SH110X_BLACK : SH110X_WHITE);
      displayBuffer.drawPixel(w.x + w.width - 1 - cornerPixelsOffset[index], w.y + w.height + TITLE_BAR_HEIGHT - cornerPixelsOffset[index + 1], pixelsRight[i] ? SH110X_BLACK : SH110X_WHITE);
    }

    // close button
    displayBuffer.drawRect(w.x + 9, w.y + 4, 11, 11, SH110X_WHITE);
  }
  else if (w.windowType == WINDOW_TYPE_REGULAR || w.windowType == WINDOW_TYPE_ALARM)
  {
    bool isRegular = w.windowType == WINDOW_TYPE_REGULAR; // shortcut
    
    // clear
    displayBuffer.fillRect(w.x, w.y, w.width, w.height + TITLE_BAR_HEIGHT - 1, SH110X_WHITE);

    // draw inside
    if (appBufferShouldDraw && isRegular)
    {
      DrawAppBuffer(w, SH110X_BLACK);
    }
    
    // top bar
    displayBuffer.drawRect(w.x, w.y, w.width, TITLE_BAR_HEIGHT, SH110X_BLACK);
    
    // lines
    if (isRegular)
    {
      for(int i = 4; i < 15; i +=2)
      {
        displayBuffer.drawFastHLine(w.x + 2, w.y + i, w.width - 4, SH110X_BLACK);
      }
    }
    
    // close button
    displayBuffer.fillRect(w.x + 8, w.y + 3, 13, 13, SH110X_WHITE);
    displayBuffer.drawRect(w.x + 9, w.y + 4, 11, 11, SH110X_BLACK);

    // title bg
    displayBuffer.fillRect(w.x + textX - 7, w.y + 3, widthTitle + 14, 15, SH110X_WHITE);

    // draw window outline
    if (isRegular)
    {
      displayBuffer.drawRect(w.x, w.y + TITLE_BAR_HEIGHT - 1, w.width, w.height, SH110X_BLACK);
    }

    // draw shadow
    if (isRegular)
    {
      displayBuffer.drawFastVLine(w.x + w.width, w.y + 1, w.height + TITLE_BAR_HEIGHT - 1, SH110X_BLACK);
      displayBuffer.drawFastHLine(w.x + 1, w.y + w.height + TITLE_BAR_HEIGHT - 1, w.width, SH110X_BLACK);
    }
    else
    {
      displayBuffer.drawFastVLine(w.x + w.width, w.y + 2, w.height + TITLE_BAR_HEIGHT - 1, SH110X_BLACK);
      displayBuffer.drawFastVLine(w.x + w.width + 1, w.y + 2, w.height + TITLE_BAR_HEIGHT - 1, SH110X_BLACK);
      displayBuffer.drawFastHLine(w.x + 2, w.y + w.height + TITLE_BAR_HEIGHT, w.width, SH110X_BLACK);
      displayBuffer.drawFastHLine(w.x + 2, w.y + w.height + TITLE_BAR_HEIGHT + 1, w.width, SH110X_BLACK);
    }

  }

  // title
  displayBuffer.setCursor(w.x + textX, w.y + 13); 
  displayBuffer.setTextColor(w.windowType == WINDOW_TYPE_ROUNDED ? SH110X_WHITE : SH110X_BLACK);
  displayBuffer.println(w.title);
}

void DrawWindowAnimationPart1(app a, bool opening)
{
  window w = a.appWindow;
  // stationary zoom in/out window
  w.height = w.height + TITLE_BAR_HEIGHT;

  int nrOfRects = 4; // number of rects showing at the same time
  window lws[] = {w, w, w, w}; // last windows
  
  float scale;
  
  // Steps it takes before smallest side will be shrinked to 2 pixels. 
  int steps = (log(2.0f/min(w.width, w.height)) / log(0.75f)) - nrOfRects;

  for (int ii = 0; ii < steps; ii++)
  {
    int i = opening ? (steps - 1) - ii : ii; // reverse if opening
    
    // draw over previous area
    for(int j = 0; j <nrOfRects; j++)
    {
      DrawSelectedOutlineScreen(lws[j].x, lws[j].y, lws[j].width, lws[j].height);
    }
       
    for(int j = 0; j < nrOfRects; j++)
    { 
      scale = pow(0.75, i+j);
      lws[j].width = w.width * scale;
      lws[j].height = w.height * scale;
      lws[j].x = w.x + (w.width - lws[j].width)/2;
      lws[j].y = w.y + (w.height - lws[j].height)/2;
      
      DrawDottedRect(lws[j].x, lws[j].y, lws[j].width, lws[j].height); // draw directly to screen
    }
    
    DrawMouse(mouseX, mouseY); // draw mouse and refresh display
  }

  // remove remaining rects
  for (int jj = 0; jj < nrOfRects; jj++)
  {
    int j = opening ? (nrOfRects - 1) - jj : jj; // reverse if opening
    DrawSelectedOutlineScreen(lws[j].x, lws[j].y, lws[j].width, lws[j].height);
    DrawMouse(mouseX, mouseY); // draw mouse and refresh display
  }
}

void DrawWindowAnimationPart2(app a, bool opening)
{
  window w = a.appWindow;
  w.height = w.height + TITLE_BAR_HEIGHT;

  int nrOfRects = 4; // number of rects showing at the same time
  window lws[] = {w, w, w, w}; // last windows

  int targetX = a.x + APP_DIMENSION/2; 
  int targetY = a.y + APP_DIMENSION/2;
  int startDimension = 2;
  int targetDimension = APP_DIMENSION;

  // origin (middle of window)
  int centerX = w.x + w.width/2;
  int centerY = w.y + w.height/2;

  // Steps it takes before startDimension will be scaled to targetDimension;
  int steps = (log(targetDimension/startDimension) / log(1.5f)) - nrOfRects;  
  float xStep = (targetX - centerX) / (float)(steps + nrOfRects);
  float yStep = (targetY - centerY) / (float)(steps + nrOfRects);
  float scale;

  if (opening)
  {
    // place exact
    DrawDottedRect(targetX - targetDimension/2 , targetY - targetDimension/2, targetDimension, targetDimension); // draw directly to screen
    DrawMouse(mouseX, mouseY); // draw mouse and refresh display 
  }
  
  for (int ii = 0; ii <= steps; ii++)
  {
    int i = opening ? steps - ii : ii; // reverse if opening
    
    // draw over previous area
    for(int j = 0; j <nrOfRects; j++)
    {
      DrawSelectedOutlineScreen(lws[j].x, lws[j].y, lws[j].width, lws[j].height);
    }

    for(int j = 0; j < nrOfRects; j++)
    { 
      scale = pow(1.5f, i+j);
      lws[j].width = startDimension * scale;
      lws[j].height = lws[j].width;
      lws[j].x = centerX + xStep * (i + j) - lws[j].width/2;
      lws[j].y = centerY + yStep * (i + j) - lws[j].height/2;
      
      DrawDottedRect(lws[j].x, lws[j].y, lws[j].width, lws[j].height); // draw directly to screen
    } 
    if (opening && ii == 0)
    {
      // remove exact
      DrawSelectedOutlineScreen(targetX - targetDimension/2 , targetY - targetDimension/2, targetDimension, targetDimension);
      //DrawSelectedOutlineScreen(targetX, targetY, targetDimension, targetDimension);
    }
    DrawMouse(mouseX, mouseY); // draw mouse and refresh display
  }

  if (!opening)
  {
    // place exact
    DrawDottedRect(targetX - targetDimension/2 , targetY - targetDimension/2, targetDimension, targetDimension); // draw directly to screen
    //DrawDottedRect(targetX, targetY, targetDimension, targetDimension); // draw directly to screen
    DrawMouse(mouseX, mouseY); // draw mouse and refresh display 
  }

  // remove remaining rects
  for (int j = 0; j < nrOfRects; j++) //TODO was 1
  {
    DrawSelectedOutlineScreen(lws[j].x, lws[j].y, lws[j].width, lws[j].height);
    DrawMouse(mouseX, mouseY); // draw mouse and refresh display
  }

  if (!opening)
  {
    // remove exact
    DrawSelectedOutlineScreen(targetX - targetDimension/2 , targetY - targetDimension/2, targetDimension, targetDimension);
    //DrawSelectedOutlineScreen(targetX, targetY, targetDimension, targetDimension);
    DrawMouse(mouseX, mouseY); // draw mouse and refresh display
  }
}

void DrawWindowAnimation(app app, bool opening)
{
  DrawEntireScreen(); // draw once so the screen is empty
  if (opening)
  {
    DrawWindowAnimationPart2(app, opening); 
    DrawWindowAnimationPart1(app, opening);
  }
  else
  {
    DrawWindowAnimationPart1(app, opening);
    DrawWindowAnimationPart2(app, opening); 
  }
}

void DrawMenu()
{
  // draw white bar
  displayBuffer.fillRect(0, 0, 128, 19, SH110X_WHITE);

  //draw higlight box
  if (selectedMenu >= 0)
  {
    int xPos = MENU_FIRST_X;
    int width = menus[selectedMenu].menuWidth;
    
    for(int i = 0; i < selectedMenu; i++)
    {
      xPos += menus[i].menuWidth + MENU_SPACING;
    }
    
    displayBuffer.fillRect(xPos - MENU_HIGHLIGHT_PADDING, 1, width + (MENU_HIGHLIGHT_PADDING * 2), 18, SH110X_BLACK);
  }

  // draw menu items (a bit hardcoded now)

  // apple menu
  displayBuffer.drawBitmap(19, 3, apple, 9, 11, selectedMenu == 0 ? SH110X_WHITE : SH110X_BLACK);

  // file menu
  displayBuffer.setCursor(42, 13);//7);
  displayBuffer.setFont(&sysfont7pt7b);
  displayBuffer.setTextColor(selectedMenu == 1 ? SH110X_WHITE : SH110X_BLACK);
  displayBuffer.println("File");

  // draw dropdown
  if (selectedMenu != -1)
  {
    DrawDropDown(selectedMenu);
  }
}

void DrawDropDown(byte menu)
{
  int itemsCount = menus[menu].itemsCount;
  int width = menus[menu].dropdownWidth;
  int height = itemsCount * SUBMENU_ITEM_HEIGHT;
  int x0 = menus[menu].x0;
  int y0 = 20;
  
  // draw container
  displayBuffer.fillRect(x0, y0, width, height + 1, SH110X_WHITE);
  displayBuffer.drawFastVLine(x0, y0, height + 1, SH110X_BLACK);
  displayBuffer.drawFastVLine(x0 + width, y0, height + 1, SH110X_BLACK);
  displayBuffer.drawFastHLine(x0, y0 + height + 1, width + 1, SH110X_BLACK);
  // shadow
  displayBuffer.drawFastVLine(x0 + width + 1, y0 + 3, height, SH110X_BLACK);
  displayBuffer.drawFastHLine(x0 + 3, y0 + height + 2, width - 1, SH110X_BLACK);
  displayBuffer.drawPixel(x0 + width + 1, y0, SH110X_BLACK);

  displayBuffer.setFont(&sysfont7pt7b);
  // draw items
  for (int i = 0; i < itemsCount; i++)
  {
    if(menus[menu].items[i] != "-")
    {
      bool isItemHighlighted = menus[menu].enabledItems[i] && menu == selectedDropdownItem[0] && i == selectedDropdownItem[1];
      if (isItemHighlighted)
      {
        // draw highight
        int extra = i == itemsCount - 1 ? 1 : 0; // add extra line for last item
        displayBuffer.fillRect(x0, y0 + (i * SUBMENU_ITEM_HEIGHT), width, SUBMENU_ITEM_HEIGHT + extra, SH110X_BLACK);
      }
      displayBuffer.setTextColor(isItemHighlighted ? SH110X_WHITE : SH110X_BLACK);
      displayBuffer.setCursor(x0 + SUBMENU_ITEM_H_OFFSET, y0 + 11 + (i * SUBMENU_ITEM_HEIGHT));
      displayBuffer.print(menus[menu].items[i]); 

      if (!menus[menu].enabledItems[i]) // if item disabled draw it greyed out
      {
        DrawDottedFilledRect(x0 + SUBMENU_ITEM_H_OFFSET, y0 + (i * SUBMENU_ITEM_HEIGHT), width - SUBMENU_ITEM_H_OFFSET - 2, SUBMENU_ITEM_HEIGHT, SH110X_WHITE);
      }
    }
    else // draw dotted line
    {
      DrawDottedHLine(x0 + 1, y0 + 7 + (i * SUBMENU_ITEM_HEIGHT), width, SH110X_BLACK, false);
    }
  }
}

void DrawDottedHLine(byte x0, byte y0, byte length, byte color, bool offset)
{
  for (byte i = (byte)offset; i < length; i += 2)
  {
    displayBuffer.drawPixel(x0 + i, y0, color);
  }
}

void DrawDottedVLine(byte x0, byte y0, byte length, byte color, bool offset)
{
  for (byte i = (byte)offset; i < length; i += 2)
  {
    displayBuffer.drawPixel(x0, y0 + i, color);
  }
}

void DrawDottedFilledRect(byte x0, byte y0, byte width, byte height, byte color)
{
  bool offset = false;
  for (byte i = 0; i < height; i ++)
  {
    DrawDottedHLine(x0, y0 + i, width, color, offset);
    offset = !offset;
  }
}

void DrawDottedRect(byte x0, byte y0, byte width, byte height) // draws dotted unfilled rect using xor directly to screen 
{
  // original offset = 0
  // offset 2nd vertical line = width + 1 % 2
  // offset 2nd horizontal line = height + 1 % 2
  int originalOffset = (x0 + y0 + 1)%2;
  int offsetV = (width + originalOffset) % 2;
  int offsetH = (height + 1 + originalOffset) % 2;
  
  // horizontal
  for (byte x = x0; x < x0 + width; x += 2)
  {
    //top
    if (x + originalOffset< x0 + width)
    {
      /*displayBuffer*/display.drawPixel(x + originalOffset, y0, displayBuffer.getPixel(x + originalOffset, y0) ? SH110X_BLACK : SH110X_WHITE);
    }
    //bottom
    if (x + offsetH < x0 + width)
    {
      /*displayBuffer*/display.drawPixel(x + offsetH, y0 + height - 1, displayBuffer.getPixel(x + offsetH, y0 + height - 1) ? SH110X_BLACK : SH110X_WHITE);
    }
  }

  originalOffset = (originalOffset + 1)% 2;
  
  // vertical
  for (byte y = y0 + 1; y < y0 + height - 1; y += 2)
  {
    //left
    if (y + originalOffset < y0 + height - 1)
    {
      /*displayBuffer*/display.drawPixel(x0, y + originalOffset, displayBuffer.getPixel(x0, y + originalOffset) ? SH110X_BLACK : SH110X_WHITE);
    }
    //right
    if (y + offsetV < y0 + height - 1)
    {
      /*displayBuffer*/display.drawPixel(x0 + width - 1, y + offsetV, displayBuffer.getPixel(x0 + width - 1, y + offsetV) ? SH110X_BLACK : SH110X_WHITE);
    }
  }
}

void DrawCorners()
{
  // left-top
  displayBuffer.drawFastVLine(0, 0, 5, SH110X_BLACK);
  displayBuffer.drawFastHLine(1, 0, 4, SH110X_BLACK);
  displayBuffer.drawFastVLine(1, 1, 2, SH110X_BLACK);
  displayBuffer.drawPixel(2, 1, SH110X_BLACK);

  // left-bottom
  displayBuffer.drawFastVLine(0, 85, 5, SH110X_BLACK);
  displayBuffer.drawFastHLine(1, 89, 4, SH110X_BLACK);
  displayBuffer.drawFastVLine(1, 87, 2, SH110X_BLACK);
  displayBuffer.drawPixel(2, 88, SH110X_BLACK);

  // right-top
  displayBuffer.drawFastVLine(127, 0, 5, SH110X_BLACK);
  displayBuffer.drawFastHLine(123, 0, 4, SH110X_BLACK);
  displayBuffer.drawFastVLine(126, 1, 2, SH110X_BLACK);
  displayBuffer.drawPixel(125, 1, SH110X_BLACK);

  // right-bottom
  displayBuffer.drawFastVLine(127, 85, 5, SH110X_BLACK);
  displayBuffer.drawFastHLine(123, 89, 4, SH110X_BLACK);
  displayBuffer.drawFastVLine(126, 87, 2, SH110X_BLACK);
  displayBuffer.drawPixel(125, 88, SH110X_BLACK);
}

void DrawBg(bool fullScreen)
{  
  for (int w = 0; w < 16; w++)
  {
    for(int h = 0; h < (fullScreen?12:9); h++)
    {
      for (int y = 0; y < 8; y++)
      {
        for (int x = 0; x < 8; x++)
        {
          int byteIndex = y;
          int bitPosition = 7 - (x % 8);
          byte byteValue = bgTile[byteIndex];
       
          // Extract the pixel value at the specified bit position
          int pixelValue = (byteValue >> bitPosition) & 0x01;
    
          if (pixelValue == 1)
          {
            displayBuffer.drawPixel((w * 8) + x, (h * 8) + (fullScreen?0:20) + y, SH110X_WHITE);
          }
        }
      }
    }
  }
  /*
  for(int x = 0; x < 16; x++)
  {
    for(int y = 0; y < 9; y++)
    {
      displayBuffer.drawBitmap(x * 8, (y * 8) + 20, bg_tile, 8, 8, SH110X_WHITE);
    }
  }*/
}

void DrawCrossHair(int x, int y)
{
  // draw over previous area
  DrawSelectedPortionScreen(prevMouseX - 5, prevMouseY - 5,11, 12); 

  for (int i = 1; i < 6; i++)
  {
    // center to top
    display.drawPixel(x, y + i, displayBuffer.getPixel(x, y + i) ? SH110X_BLACK : SH110X_WHITE);
    // center to down
    display.drawPixel(x, y - i, displayBuffer.getPixel(x, y - i) ? SH110X_BLACK : SH110X_WHITE);
    // center to left
    display.drawPixel(x - i, y, displayBuffer.getPixel(x - i, y) ? SH110X_BLACK : SH110X_WHITE);
    // center to right
    display.drawPixel(x + i, y, displayBuffer.getPixel(x + i, y) ? SH110X_BLACK : SH110X_WHITE);
  }

  // last two points
  display.drawPixel(x, y, displayBuffer.getPixel(x, y) ? SH110X_BLACK : SH110X_WHITE);
  display.drawPixel(x, y + 6, displayBuffer.getPixel(x, y + 6) ? SH110X_BLACK : SH110X_WHITE );
  display.display();

  prevMouseX = x;
  prevMouseY = y;
}

void DrawPointer(int x, int y)
{
  // draw over previous area mouse
  DrawSelectedPortionScreen(prevMouseX, prevMouseY, MOUSE_WIDTH, MOUSE_HEIGHT);
  
  display.drawBitmap(x, y, mouseWhite, MOUSE_WIDTH, MOUSE_HEIGHT, SH110X_WHITE);
  display.drawBitmap(x, y, mouse, MOUSE_WIDTH, MOUSE_HEIGHT, SH110X_BLACK);
  display.display();

  prevMouseX = x;
  prevMouseY = y;
}

void DrawMouse(int x, int y)
{
  if (cursorType == CURSOR_TYPE_POINTER)
  {
    DrawPointer(x, y);
  }
  else if (cursorType == CURSOR_TYPE_CROSSHAIR)
  {
    DrawCrossHair(x, y);
  }
}

void HandleJsonMessage(JSONVar obj)
{
  if (obj.hasOwnProperty("mouseX"))
  {
    mouseX = constrain((int)obj["mouseX"], 0, 127);
  }
  if (obj.hasOwnProperty("mouseY"))
  {
    mouseY = constrain((int)obj["mouseY"], 0, 127);
  }
  if (obj.hasOwnProperty("mouseDown"))
  {
    mouseDown = (bool)(int)obj["mouseDown"];
  }
  if(obj.hasOwnProperty("start"))
  {
    receivingBufferAppId = (int)obj["start"];
    receivingBuffer = true;
    lastAppBufferIndex = 0;
  }
  if(obj.hasOwnProperty("app")) // app register
  {
    int appId = (int)obj["app"];
    String title = (String)obj["title"];
    bool isDesktopApp = (bool)obj["isDesktopApp"];
    int cursorType = (int)obj["cursorType"];
    int windowX = (int)obj["windowX"];
    int windowY = (int)obj["windowY"];
    int windowWidth = (int)obj["windowWidth"];
    int windowHeight = (int)obj["windowHeight"];
    byte windowType = (byte)obj["windowType"];

    if (isDesktopApp)
    {
      hasApp = true;
      currentApp = {appId, title, 80, 28, false, false, true, cursorType,
                    {appId, title, windowX, windowY, windowWidth, windowHeight, windowType}
                    };
    }
    else
    {
      hasMenuApp = true;
      currentMenuApp = {appId, title, 0, 0, false, false, false, cursorType,
                        {appId, title, windowX, windowY, windowWidth, windowHeight, windowType}
                        };
      // set menu item
      menus[0].items[3] = title;
      menus[0].enabledItems[3] = true;
      menus[0].callbacks[3] = OpenCustomMenu;
      menus[0].itemsCount = 4;
      UpdateDropdownWidth(0);
    }

    requestScreenRefresh = true;
  }
  if(obj.hasOwnProperty("bg"))
  {
    for(int i = 0; i < 8; i++)
    {
      bgTile[i] = (int)obj["bg"][String(i)];
    }
    requestScreenRefresh = true;
  }
  if(obj.hasOwnProperty("bgTileRequest"))
  {
    bgTileRequested = true;
  }
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    String message = (char*)data;    
    lastMessage = message; //TODO temp
    hasMessage = true;
    
    if (message == "END")
    {
      receivingBuffer = false;
      lastAppBufferSet = true;
      lastAppBufferIndex = 0;
    }
    else
    {
      if (receivingBuffer)
      {
        // assumes message is only HEX like "00FF00FF..."
        
        // Convert hex string to byte array
        for (int i = 0; i < message.length() / 2; i++) {
          // Extract two characters from the hex string
          String hexPair = message.substring(i * 2, i * 2 + 2);
          // Convert the hex pair to a decimal number
          lastAppBuffer[lastAppBufferIndex] = strtol(hexPair.c_str(), NULL, 16);
          lastAppBufferIndex++;
        }
      }
      else // don't handle json while receiving
      {
        if (message[0] == '{')
        {
          jsonReceived = JSON.parse(message);
          HandleJsonMessage(jsonReceived);
        }
      }
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      globalClient = client;
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      globalClient = NULL;
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void sendWsMessage(String message)
{
  if(globalClient != NULL && globalClient->status() == WS_CONNECTED){
      if (receivingBuffer)
      {
        return;
      }
      globalClient->text(message);
   }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

IPAddress local_IP(192,168,0,123);
IPAddress gateway(192,168,0,1);
IPAddress subnet(255,255,255,0);

// Initialize WiFi
void initWiFi() {
  //WiFi.mode(WIFI_STA);
  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  
  Serial.println(WiFi.localIP());
}
