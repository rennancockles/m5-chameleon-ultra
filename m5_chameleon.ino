/**
 * @file m5_chameleon.ino
 * @author Rennan Cockles (r3ck.dev@gmail.com)
 * @brief M5 Chameleon Ultra
 * @version 0.1
 * @date 2024-11-07
 *
 *
 * @Hardwares: M5 Cardputer and StickC
 * @Platform Version: Arduino M5Stack Board Manager v2.0.7
 */


#define CARDPUTER

#ifdef CARDPUTER
  #include "M5Cardputer.h"
#else
  #include <M5Unified.h>
#endif
#include <FS.h>
#include <SD.h>
#include <LittleFS.h>
#include <chameleonUltra.h>
#include <set>
#include <SPI.h>
#include <regex>

#define FGCOLOR 0x96FF
#define BGCOLOR 0
#define ROT 1
#define SMOOTH_FONT 1
#define FP 1
#define FM 2
#define FG 3
#define LW 6
#define LH 8
#define BORDER_PAD 5
#define REDRAW_DELAY 200
#define SEL_BTN 37
#define UP_BTN 35
#define DW_BTN 39
#define HEIGHT M5.Display.height()
#define WIDTH  M5.Display.width()
#define MAX_MENU_SIZE (int)(HEIGHT/25)
#define MAX_ITEMS (int)(HEIGHT-20)/(LH*2)

#ifdef CARDPUTER
	#define SDCARD_CS   12
	#define SDCARD_SCK  40
	#define SDCARD_MISO 39
	#define SDCARD_MOSI 14
#else
  #define SDCARD_CS   14
  #define SDCARD_SCK  0
  #define SDCARD_MISO 36
  #define SDCARD_MOSI 26
#endif

struct Option {
  std::string label;
  std::function<void()> operation;
  bool selected = false;

  Option(const std::string& lbl, const std::function<void()>& op, bool sel = false)
    : label(lbl), operation(op), selected(sel) {}
};
struct FileList {
  String filename;
  bool folder;
  bool operation;
};
bool sdcardMounted;
SPIClass sdcardSPI;
std::vector<FileList> fileList;

typedef struct {
  String uid;
  String bcc;
  String sak;
  String atqa;
  String piccType;
} PrintableUID;

typedef struct {
  String tagType;
  String uid;
} ScanResult;

enum AppMode {
  BATTERY_INFO_MODE,
  FACTORY_RESET_MODE,

  LF_READ_MODE,
  LF_SCAN_MODE,
  LF_CLONE_MODE,
  LF_EMULATION_MODE,
  LF_SAVE_MODE,
  LF_LOAD_MODE,
  LF_CUSTOM_UID_MODE,

  HF_READ_MODE,
  HF_SCAN_MODE,
  HF_EMULATION_MODE,
  HF_SAVE_MODE,
  HF_LOAD_MODE,
  HF_CLONE_MODE,
  HF_WRITE_MODE,
  HF_CUSTOM_UID_MODE,

  FULL_SCAN_MODE,
};

ChameleonUltra chmUltra = ChameleonUltra(true);
ChameleonUltra::LfTag lfTagData;
ChameleonUltra::HfTag hfTagData;
AppMode currentMode;
PrintableUID printableHFUID;
String printableLFUID;
String dumpFilename = "";
String strDump = "";
bool _lf_read_uid = false;
bool _hf_read_uid = false;
bool _battery_set = false;
bool pageReadSuccess = false;
String strAllPages = "";
int totalPages = 0;
int dataPages = 0;
std::set<String> _scanned_set;
std::vector<ScanResult> _scanned_tags;
bool chameleonConnected = false;


bool checkNextPress(){
  #ifdef CARDPUTER
    M5Cardputer.update();
    return (M5Cardputer.Keyboard.isKeyPressed('/') || M5Cardputer.Keyboard.isKeyPressed('.'));
  #endif
  return (digitalRead(DW_BTN) == LOW);
}

bool checkPrevPress() {
  #ifdef CARDPUTER
    M5Cardputer.update();
    return (M5Cardputer.Keyboard.isKeyPressed(',') || M5Cardputer.Keyboard.isKeyPressed(';'));
  #endif
  return (digitalRead(UP_BTN) == LOW);
}

bool checkSelPress(){
  #ifdef CARDPUTER
    M5Cardputer.update();
    return (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER) || digitalRead(0)==LOW);
  #endif
  return (digitalRead(SEL_BTN) == LOW);
}

bool checkEscPress(){
  #ifdef CARDPUTER
    M5Cardputer.update();
    return (M5Cardputer.Keyboard.isKeyPressed('`') || M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE));
  #endif
  return (digitalRead(UP_BTN) == LOW);
}

bool checkAnyKeyPress() {
  #ifdef CARDPUTER
    M5Cardputer.update();
    return M5Cardputer.Keyboard.isPressed();
  #endif
  return (checkNextPress() || checkPrevPress() || checkSelPress());
}

#ifdef CARDPUTER
bool checkNextPagePress() {
  M5Cardputer.update();
  return M5Cardputer.Keyboard.isKeyPressed('/');
}

bool checkPrevPagePress() {
  M5Cardputer.update();
  return M5Cardputer.Keyboard.isKeyPressed(',');
}
#endif

void checkReboot() {
  int countDown;
  /* Long press power off */
  if (digitalRead(UP_BTN) == LOW) {
    uint32_t time_count = millis();
    while (digitalRead(UP_BTN) == LOW) {
      // Display poweroff bar only if holding button
      if (millis() - time_count > 500) {
        M5.Display.setCursor(60, 12);
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        countDown = (millis() - time_count) / 1000 + 1;
        M5.Display.printf(" PWR OFF IN %d/3\n", countDown);
        delay(10);
      }
    }

    // Clear text after releasing the button
    delay(30);
    M5.Display.fillRect(60, 12, M5.Display.width() - 60, M5.Display.fontHeight(1), TFT_BLACK);
  }
}

void resetTftDisplay(int size = FM) {
  M5.Display.setCursor(0,0);
  M5.Display.fillScreen(BGCOLOR);
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(FGCOLOR, BGCOLOR);
}

String keyboard(String mytext, int maxSize, String msg) {
  String _mytext = mytext;

  resetTftDisplay();
  bool caps=false;
  int x=0;
  int y=-1;
  int x2=0;
  int y2=0;
  char keys[4][12][2] = { //4 lines, with 12 characteres, low and high caps
    {
      { '1', '!' },//1
      { '2', '@' },//2
      { '3', '#' },//3
      { '4', '$' },//4
      { '5', '%' },//5
      { '6', '^' },//6
      { '7', '&' },//7
      { '8', '*' },//8
      { '9', '(' },//9
      { '0', ')' },//10
      { '-', '_' },//11
      { '=', '+' } //12
     },
    {
      { 'q', 'Q' },//1
      { 'w', 'W' },//2
      { 'e', 'E' },//3
      { 'r', 'R' },//4
      { 't', 'T' },//5
      { 'y', 'Y' },//6
      { 'u', 'U' },//7
      { 'i', 'I' },//8
      { 'o', 'O' },//9
      { 'p', 'P' },//10
      { '[', '{' },//11
      { ']', '}' } //12
    },
    {
      { 'a', 'A' },//1
      { 's', 'S' },//2
      { 'd', 'D' },//3
      { 'f', 'F' },//4
      { 'g', 'G' },//5
      { 'h', 'H' },//6
      { 'j', 'J' },//7
      { 'k', 'K' },//8
      { 'l', 'L' },//9
      { ';', ':' },//10
      { '"', '\'' },//11
      { '|', '\\' } //12
    },
    {
      { '\\', '|' },//1
      { 'z', 'Z' },//2
      { 'x', 'X' },//3
      { 'c', 'C' },//4
      { 'v', 'V' },//5
      { 'b', 'B' },//6
      { 'n', 'N' },//7
      { 'm', 'M' },//8
      { ',', '<' },//9
      { '.', '>' },//10
      { '?', '/' },//11
      { '/', '/' } //12
    }
  };
  int _x = WIDTH/12;
  int _y = (HEIGHT - 54)/4;
  int _xo = _x/2-3;
  int i=0;
  int j=-1;
  bool redraw=true;
  delay(200);
  int cX =0;
  int cY =0;
  M5.Display.fillScreen(BGCOLOR);
  while(1) {
    if(redraw) {
      M5.Display.setCursor(0,0);
      M5.Display.setTextColor(TFT_WHITE, BGCOLOR);
      M5.Display.setTextSize(FM);

      //Draw the rectangles
      if(y<0) {
        M5.Display.fillRect(0,1,WIDTH,22,BGCOLOR);
        M5.Display.drawRect(7,2,46,20,TFT_WHITE);       // Ok Rectangle
        M5.Display.drawRect(55,2,50,20,TFT_WHITE);      // CAP Rectangle
        M5.Display.drawRect(107,2,50,20,TFT_WHITE);     // DEL Rectangle
        M5.Display.drawRect(159,2,74,20,TFT_WHITE);     // SPACE Rectangle
        M5.Display.drawRect(3,32,WIDTH-3,20,FGCOLOR); // mystring Rectangle


        if(x==0 && y==-1) { M5.Display.setTextColor(BGCOLOR, TFT_WHITE); M5.Display.fillRect(7,2,50,20,TFT_WHITE); }
        else M5.Display.setTextColor(TFT_WHITE, BGCOLOR);
        M5.Display.drawString("OK", 18, 4);


        if(x==1 && y==-1) { M5.Display.setTextColor(BGCOLOR, TFT_WHITE); M5.Display.fillRect(55,2,50,20,TFT_WHITE); }
        else if(caps) { M5.Display.fillRect(55,2,50,20,TFT_DARKGREY); M5.Display.setTextColor(TFT_WHITE, TFT_DARKGREY); }
        else M5.Display.setTextColor(TFT_WHITE, BGCOLOR);
        M5.Display.drawString("CAP", 64, 4);


        if(x==2 && y==-1) { M5.Display.setTextColor(BGCOLOR, TFT_WHITE); M5.Display.fillRect(107,2,50,20,TFT_WHITE); }
        else M5.Display.setTextColor(TFT_WHITE, BGCOLOR);
        M5.Display.drawString("DEL", 115, 4);

        if(x>2 && y==-1) { M5.Display.setTextColor(BGCOLOR, TFT_WHITE); M5.Display.fillRect(159,2,74,20,TFT_WHITE); }
        else M5.Display.setTextColor(TFT_WHITE, BGCOLOR);
        M5.Display.drawString("SPACE", 168, 4);
      }

      M5.Display.setTextSize(FP);
      M5.Display.setTextColor(TFT_WHITE, 0x5AAB);
      M5.Display.drawString(msg.substring(0,38), 3, 24);

      M5.Display.setTextSize(FM);

      // reseta o quadrado do texto
      if (mytext.length() == 19 || mytext.length() == 20 || mytext.length() == 38 || mytext.length() == 39) M5.Display.fillRect(3,32,WIDTH-3,20,BGCOLOR); // mystring Rectangle
      // escreve o texto
      M5.Display.setTextColor(TFT_WHITE);
      if(mytext.length()>19) {
        M5.Display.setTextSize(FP);
        if(mytext.length()>38) {
          M5.Display.drawString(mytext.substring(0,38), 5, 34);
          M5.Display.drawString(mytext.substring(38,mytext.length()), 5, 42);
        }
        else {
          M5.Display.drawString(mytext, 5, 34);
        }
      } else {
        M5.Display.drawString(mytext, 5, 34);
      }
      //desenha o retangulo colorido
      M5.Display.drawRect(3,32,WIDTH-3,20,FGCOLOR); // mystring Rectangle


      M5.Display.setTextColor(TFT_WHITE, BGCOLOR);
      M5.Display.setTextSize(FM);


      for(i=0;i<4;i++) {
        for(j=0;j<12;j++) {
          //use last coordenate to paint only this letter
          if(x2==j && y2==i) { M5.Display.setTextColor(~BGCOLOR, BGCOLOR); M5.Display.fillRect(j*_x,i*_y+54,_x,_y,BGCOLOR);}
          /* If selected, change font color and draw Rectangle*/
          if(x==j && y==i) { M5.Display.setTextColor(BGCOLOR, ~BGCOLOR); M5.Display.fillRect(j*_x,i*_y+54,_x,_y,~BGCOLOR);}


          /* Print the letters */
          if(!caps) M5.Display.drawChar(keys[i][j][0], (j*_x+_xo), (i*_y+56));
          else M5.Display.drawChar(keys[i][j][1], (j*_x+_xo), (i*_y+56));

          /* Return colors to normal to print the other letters */
          if(x==j && y==i) { M5.Display.setTextColor(~BGCOLOR, BGCOLOR); }
        }
      }
      // save actual key coordenate
      x2=x;
      y2=y;
      redraw = false;
    }

    //cursor handler
    if(mytext.length()>19) {
      M5.Display.setTextSize(FP);
      if(mytext.length()>38) {
        cY=42;
        cX=5+(mytext.length()-38)*LW;
      }
      else {
        cY=34;
        cX=5+mytext.length()*LW;
      }
    } else {
      cY=34;
      cX=5+mytext.length()*LW*2;
    }

    /* When Select a key in keyboard */
#ifdef CARDPUTER
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isPressed()) {
      M5.Display.setCursor(cX,cY);
      auto status = M5Cardputer.Keyboard.keysState();

      bool Fn = status.fn;
      if(Fn && M5Cardputer.Keyboard.isKeyPressed('`')) {
        mytext = _mytext; // return the old name
        break;
      }

      for (auto i : status.word) {
        if(mytext.length()<maxSize) {
          mytext += i;
          if(mytext.length()!=20 && mytext.length()!=20) M5.Display.print(i);
          cX=M5.Display.getCursorX();
          cY=M5.Display.getCursorY();
          if(mytext.length()==20) redraw = true;
          if(mytext.length()==39) redraw = true;
        }
      }

      if (status.del && mytext.length() > 0) {
        // Handle backspace key
        mytext.remove(mytext.length() - 1);
        int fS=FM;
        if(mytext.length()>19) { M5.Display.setTextSize(FP); fS=FP; }
        else M5.Display.setTextSize(FM);
        M5.Display.setCursor((cX-fS*LW),cY);
        M5.Display.setTextColor(FGCOLOR,BGCOLOR);
        M5.Display.print(" ");
        M5.Display.setTextColor(TFT_WHITE, 0x5AAB);
        M5.Display.setCursor(cX-fS*LW,cY);
        cX=M5.Display.getCursorX();
        cY=M5.Display.getCursorY();
        if(mytext.length()==19) redraw = true;
        if(mytext.length()==38) redraw = true;
      }

      if (status.enter) {
        break;
      }
      delay(200);
    }

    if(checkSelPress()) break;

#else

    int z=0;

    if(checkSelPress())  {
      M5.Display.setCursor(cX,cY);
      if(caps) z=1;
      else z=0;
      if(x==0 && y==-1) break;
      else if(x==1 && y==-1) caps=!caps;
      else if(x==2 && y==-1 && mytext.length() > 0) {
        DEL:
        mytext.remove(mytext.length()-1);
        int fS=FM;
        if(mytext.length()>19) { M5.Display.setTextSize(FP); fS=FP; }
        else M5.Display.setTextSize(FM);
        M5.Display.setCursor((cX-fS*LW),cY);
        M5.Display.setTextColor(FGCOLOR,BGCOLOR);
        M5.Display.print(" ");
        M5.Display.setTextColor(TFT_WHITE, 0x5AAB);
        M5.Display.setCursor(cX-fS*LW,cY);
        cX=M5.Display.getCursorX();
        cY=M5.Display.getCursorY();
      }
      else if(x>2 && y==-1 && mytext.length()<maxSize) mytext += " ";
      else if(y>-1 && mytext.length()<maxSize) {
        ADD:
        mytext += keys[y][x][z];
        if(mytext.length()!=20 && mytext.length()!=20) M5.Display.print(keys[y][x][z]);
        cX=M5.Display.getCursorX();
        cY=M5.Display.getCursorY();
      }
      redraw = true;
      delay(200);
    }

    /* Down Btn to move in X axis (to the right) */
    if(checkNextPress()) {
      delay(200);
      if(checkNextPress()) { x--; delay(250); } // Long Press
      else x++; // Short Press
      if(y<0 && x>3) x=0;
      if(x>11) x=0;
      else if (x<0) x=11;
      redraw = true;
    }
    /* UP Btn to move in Y axis (Downwards) */
    if(checkPrevPress()) {
      delay(200);
      if(checkPrevPress()) { y--; delay(250);  }// Long press
      else y++; // short press
      if(y>3) { y=-1; }
      else if(y<-1) y=3;
      redraw = true;
    }

#endif

  }

  //Resets screen when finished writing
  M5.Display.fillRect(0,0,WIDTH,HEIGHT,BGCOLOR);
  resetTftDisplay();

  return mytext;
}


void printTitle(String title) {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextSize(FM);
  M5.Display.setTextDatum(top_center);

  M5.Display.drawString(title, M5.Display.width() / 2, BORDER_PAD);

  M5.Display.setTextDatum(top_left);
  M5.Display.setTextSize(FP);
  M5.Display.setCursor(BORDER_PAD, 25);
}

void printSubtitle(String subtitle) {
  M5.Display.setTextSize(FP);
  M5.Display.setTextDatum(top_center);

  M5.Display.drawString(subtitle, M5.Display.width() / 2, 25);

  M5.Display.setTextDatum(top_left);
  M5.Display.setCursor(BORDER_PAD, 35);
}

void padprintln(const String &s) {
  M5.Display.setCursor(BORDER_PAD, M5.Display.getCursorY());
  M5.Display.println(s);
}


void displayRedStripe(String text, uint16_t fgcolor = TFT_WHITE, uint16_t bgcolor = TFT_RED) {
  int size;
  if(text.length()*LW*FM<(M5.Display.width()-2*FM*LW)) size = FM;
  else size = FP;
  M5.Display.fillSmoothRoundRect(10,M5.Display.height()/2-13,M5.Display.width()-20,26,7,bgcolor);
  M5.Display.fillSmoothRoundRect(10,M5.Display.height()/2-13,M5.Display.width()-20,26,7,bgcolor);
  M5.Display.setTextColor(fgcolor,bgcolor);
  if(size==FM) {
    M5.Display.setTextSize(FM);
    M5.Display.setCursor(M5.Display.width()/2 - FM*3*text.length(), M5.Display.height()/2-8);
  }
  else {
    M5.Display.setTextSize(FP);
    M5.Display.setCursor(M5.Display.width()/2 - FP*3*text.length(), M5.Display.height()/2-8);
  }
  M5.Display.println(text);
  M5.Display.setTextColor(FGCOLOR, TFT_BLACK);
}

void displayError(String txt, bool waitKeyPress = false)   {
  displayRedStripe(txt);
  delay(200);
  while(waitKeyPress && !checkAnyKeyPress()) delay(100);
}

void displayWarning(String txt, bool waitKeyPress = false) {
  displayRedStripe(txt, TFT_BLACK, TFT_YELLOW);
  delay(200);
  while(waitKeyPress && !checkAnyKeyPress()) delay(100);
}

void displayInfo(String txt, bool waitKeyPress = false)    {
  displayRedStripe(txt, TFT_WHITE, TFT_BLUE);
  delay(200);
  while(waitKeyPress && !checkAnyKeyPress()) delay(100);
}

void displaySuccess(String txt, bool waitKeyPress = false) {
  displayRedStripe(txt, TFT_WHITE, TFT_DARKGREEN);
  delay(200);
  while(waitKeyPress && !checkAnyKeyPress()) delay(100);
}

int loopOptions(std::vector<Option>& options, bool submenu = false, String subText = ""){
  bool redraw = true;
  int index=0;
  int menuSize = options.size();
  if(options.size()>MAX_MENU_SIZE) menuSize = MAX_MENU_SIZE;

  while(1){
    if (redraw) {
      if(submenu) drawSubmenu(index, options, subText);
      else drawOptions(index, options, FGCOLOR, BGCOLOR);
      redraw=false;
      delay(REDRAW_DELAY);
    }

    if(checkPrevPress()) {
    #ifdef CARDPUTER
      if(index==0) index = options.size() - 1;
      else if(index>0) index--;
      redraw = true;
    #else
    long _tmp=millis();
    // while(checkPrevPress()) { if(millis()-_tmp>200) M5.Display.drawArc(WIDTH/2, HEIGHT/2, 25,15,0,360*(millis()-(_tmp+200))/500,FGCOLOR-0x2000,BGCOLOR); }
    if(millis()-_tmp>700) { // longpress detected to exit
      break;
    }
    else {
      if(index==0) index = options.size() - 1;
      else if(index>0) index--;
      redraw = true;
    }
    #endif
    }
    /* DW Btn to next item */
    if(checkNextPress()) {
      index++;
      if((index+1)>options.size()) index = 0;
      redraw = true;
    }

    /* Select and run function */
    if(checkSelPress()) {
      Serial.println("Selected: " + String(options[index].label.c_str()));
      options[index].operation();
      break;
    }
  }
  delay(200);
  return index;
}

void drawOptions(int index,std::vector<Option>& options, uint16_t fgcolor, uint16_t bgcolor) {
  int menuSize = options.size();
  if(options.size()>MAX_MENU_SIZE) {
    menuSize = MAX_MENU_SIZE;
  }

  if(index==0) M5.Display.fillRoundRect(WIDTH*0.10,HEIGHT/2-menuSize*(FM*8+4)/2 -5,WIDTH*0.8,(FM*8+4)*menuSize+10,5,bgcolor);

  M5.Display.setTextColor(fgcolor,bgcolor);
  M5.Display.setTextSize(FM);
  M5.Display.setCursor(WIDTH*0.10+5,HEIGHT/2-menuSize*(FM*8+4)/2);

  int i=0;
  int init = 0;
  int cont = 1;
  if(index==0) M5.Display.fillRoundRect(WIDTH*0.10,HEIGHT/2-menuSize*(FM*8+4)/2 -5,WIDTH*0.8,(FM*8+4)*menuSize+10,5,bgcolor);
  menuSize = options.size();
  if(index>=MAX_MENU_SIZE) init=index-MAX_MENU_SIZE+1;
  for(i=0;i<menuSize;i++) {
    if(i>=init) {
      if(options[i].selected) M5.Display.setTextColor(fgcolor-0x2000,bgcolor); // if selected, change Text color
      else M5.Display.setTextColor(fgcolor,bgcolor);

      String text="";
      if(i==index) text+=">";
      else text +=" ";
      text += String(options[i].label.c_str()) + "              ";
      M5.Display.setCursor(WIDTH*0.10+5,M5.Display.getCursorY()+4);
      M5.Display.println(text.substring(0,(WIDTH*0.8 - 10)/(LW*FM) - 1));
      cont++;
    }
    if(cont>MAX_MENU_SIZE) goto Exit;
  }
  Exit:
  if(options.size()>MAX_MENU_SIZE) menuSize = MAX_MENU_SIZE;
  M5.Display.drawRoundRect(WIDTH*0.10,HEIGHT/2-menuSize*(FM*8+4)/2 -5,WIDTH*0.8,(FM*8+4)*menuSize+10,5,fgcolor);
}

void drawSubmenu(int index,std::vector<Option>& options, String system) {
  int menuSize = options.size();
  if(index==0) resetTftDisplay(FP);
  M5.Display.setTextColor(FGCOLOR,BGCOLOR);
  M5.Display.fillRect(6,26,WIDTH-12,20,BGCOLOR);
  M5.Display.fillRoundRect(6,26,WIDTH-12,HEIGHT-32,5,BGCOLOR);
  M5.Display.setTextSize(FP);
  M5.Display.setCursor(12,30);
  M5.Display.setTextColor(FGCOLOR);
  M5.Display.println(system);

  if (index-1>=0) {
    M5.Display.setTextSize(FM);
    M5.Display.setTextColor(FGCOLOR-0x2000);
    M5.Display.drawCentreString(options[index-1].label.c_str(),WIDTH/2, 42+(HEIGHT-134)/2,SMOOTH_FONT);
  } else {
    M5.Display.setTextSize(FM);
    M5.Display.setTextColor(FGCOLOR-0x2000);
    M5.Display.drawCentreString(options[menuSize-1].label.c_str(),WIDTH/2, 42+(HEIGHT-134)/2,SMOOTH_FONT);
  }

  int selectedTextSize = options[index].label.length() <= WIDTH/(LW*FG)-1 ? FG : FM;
  M5.Display.setTextSize(selectedTextSize);
  M5.Display.setTextColor(FGCOLOR);
  M5.Display.drawCentreString(
    options[index].label.c_str(),
    WIDTH/2,
    67+(HEIGHT-134)/2+((selectedTextSize-1)%2)*LH/2,
    SMOOTH_FONT
  );

  if (index+1<menuSize) {
    M5.Display.setTextSize(FM);
    M5.Display.setTextColor(FGCOLOR-0x2000);
    M5.Display.drawCentreString(options[index+1].label.c_str(),WIDTH/2, 102+(HEIGHT-134)/2,SMOOTH_FONT);
  } else {
    M5.Display.setTextSize(FM);
    M5.Display.setTextColor(FGCOLOR-0x2000);
    M5.Display.drawCentreString(options[0].label.c_str(),WIDTH/2, 102+(HEIGHT-134)/2,SMOOTH_FONT);
  }

  M5.Display.drawFastHLine(
    WIDTH/2 - options[index].label.size()*selectedTextSize*LW/2,
    67+(HEIGHT-134)/2+((selectedTextSize-1)%2)*LH/2+selectedTextSize*LH,
    options[index].label.size()*selectedTextSize*LW,
    FGCOLOR
  );
  M5.Display.fillRect(WIDTH-5,0,5,HEIGHT,BGCOLOR);
  M5.Display.fillRect(WIDTH-5,index*HEIGHT/menuSize,5,HEIGHT/menuSize,FGCOLOR);
}

void progressHandler(int progress, size_t total, String message) {
  int barWidth = map(progress, 0, total, 0, 200);
  if(barWidth <3) {
    M5.Display.fillRect(6, 27, WIDTH-12, HEIGHT-33, BGCOLOR);
    M5.Display.drawRect(18, HEIGHT - 47, 204, 17, FGCOLOR);
    displayRedStripe(message, TFT_WHITE, FGCOLOR);
  }
  M5.Display.fillRect(20, HEIGHT - 45, barWidth, 13, FGCOLOR);
}


bool setupSdCard() {
  if(SDCARD_SCK==-1) {
    sdcardMounted = false;
    return false;
  }

  // avoid unnecessary remounting
  if(sdcardMounted) return true;

  sdcardSPI.begin(SDCARD_SCK, SDCARD_MISO, SDCARD_MOSI, SDCARD_CS); // start SPI communications
  if (!SD.begin(SDCARD_CS, sdcardSPI)) {
    #if !defined(CARDPUTER)
      sdcardSPI.end(); // Closes SPI connections and release pin header.
    #endif
    sdcardMounted = false;
    return false;
  }
  else {
    sdcardMounted = true;
    return true;
  }
}

void closeSdCard() {
  SD.end();
  #if !defined(CARDPUTER)
  sdcardSPI.end(); // Closes SPI connections and release pins.
  #endif
  sdcardMounted = false;
}

bool sortList(const FileList& a, const FileList& b) {
    // Order items alfabetically
    String fa=a.filename.c_str();
    fa.toUpperCase();
    String fb=b.filename.c_str();
    fb.toUpperCase();
    return fa < fb;
}

bool checkExt(String ext, String pattern) {
    ext.toUpperCase();
    pattern.toUpperCase();
    if (ext == pattern) return true;

    pattern = "^(" + pattern + ")$";

    char charArray[pattern.length() + 1];
    pattern.toCharArray(charArray, pattern.length() + 1);
    std::regex ext_regex(charArray);
    return std::regex_search(ext.c_str(), ext_regex);
}

bool checkLittleFsSize() {
  if((LittleFS.totalBytes() - LittleFS.usedBytes()) < 4096) {
    displayError("LittleFS is Full", true);
    return false;
  } else return true;
}

bool getFsStorage(FS *&fs) {
  if(setupSdCard()) fs=&SD;
  else if(checkLittleFsSize()) fs=&LittleFS;
  else return false;

  return true;
}

void listFiles(int index, std::vector<FileList> fileList) {
    if(index==0){
      M5.Display.fillScreen(BGCOLOR);
      M5.Display.fillScreen(BGCOLOR);
    }
    M5.Display.setCursor(10,10);
    M5.Display.setTextSize(FM);
    int i=0;
    int arraySize = fileList.size();
    int start=0;
    if(index>=MAX_ITEMS) {
        start=index-MAX_ITEMS+1;
        if(start<0) start=0;
    }
    int nchars = (WIDTH-20)/(6*M5.Display.getTextStyle().size_x);
    String txt=">";
    while(i<arraySize) {
        if(i>=start) {
            M5.Display.setCursor(10,M5.Display.getCursorY());
            if(fileList[i].folder==true) M5.Display.setTextColor(FGCOLOR-0x2000, BGCOLOR);
            else if(fileList[i].operation==true) M5.Display.setTextColor(TFT_RED, BGCOLOR);
            else { M5.Display.setTextColor(FGCOLOR,BGCOLOR); }

            if (index==i) txt=">";
            else txt=" ";
            txt+=fileList[i].filename + "                 ";
            M5.Display.println(txt.substring(0,nchars));
        }
        i++;
        if (i==(start+MAX_ITEMS) || i==arraySize) break;
    }
    M5.Display.drawRoundRect(5, 5, WIDTH - 10, HEIGHT - 10, 5, FGCOLOR);
    M5.Display.drawRoundRect(5, 5, WIDTH - 10, HEIGHT - 10, 5, FGCOLOR);

}

void readFs(FS fs, String folder, String allowed_ext) {
    int allFilesCount = 0;
    fileList.clear();
    FileList object;

    File root = fs.open(folder);
    if (!root || !root.isDirectory()) {
        //Serial.println("Não foi possível abrir o diretório");
        return; // Retornar imediatamente se não for possível abrir o diretório
    }

    //Add Folders to the list
    File file = root.openNextFile();
    while (file && ESP.getFreeHeap()>1024) {
        String fileName = file.name();
        if (file.isDirectory()) {
            object.filename = fileName.substring(fileName.lastIndexOf("/") + 1);
            object.folder = true;
            object.operation=false;
            fileList.push_back(object);
        }
        file = root.openNextFile();
    }
    file.close();
    root.close();
    // Sort folders
    std::sort(fileList.begin(), fileList.end(), sortList);
    int new_sort_start=fileList.size();

    //Add files to the list
    root = fs.open(folder);
    File file2 = root.openNextFile();
    while (file2) {
        String fileName = file2.name();
        if (!file2.isDirectory()) {
            String ext = fileName.substring(fileName.lastIndexOf(".") + 1);
            if (allowed_ext=="*" || checkExt(ext, allowed_ext)) {
              object.filename = fileName.substring(fileName.lastIndexOf("/") + 1);
              object.folder = false;
              object.operation=false;
              fileList.push_back(object);
            }
        }
        file2 = root.openNextFile();
    }
    file2.close();
    root.close();

    //
    Serial.println("Files listed with: " + String(fileList.size()) + " files/folders found");

    // Order file list
    std::sort(fileList.begin()+new_sort_start, fileList.end(), sortList);

    // Adds Operational btn at the botton
    object.filename = "> Back";
    object.folder=false;
    object.operation=true;

    fileList.push_back(object);
}

String loopSD(FS &fs, String allowed_ext) {
  String result = "";
  std::vector<Option> options;
  bool reload=false;
  bool redraw = true;
  int index = 0;
  int maxFiles = 0;
  String Folder = "/";
  String PreFolder = "/";
  M5.Display.fillScreen(BGCOLOR);
  M5.Display.drawRoundRect(5,5,WIDTH-10,HEIGHT-10,5,FGCOLOR);
  if(&fs==&SD) {
    closeSdCard();
    if(!setupSdCard()){
      displayError("Fail Mounting SD", true);
      return "";
    }
  }
  bool exit = false;

  readFs(fs, Folder, allowed_ext);

  maxFiles = fileList.size() - 1; //discount the >back operator
  while(1){
    if(exit) break; // stop this loop and retur to the previous loop

    if(redraw) {
      if(strcmp(PreFolder.c_str(),Folder.c_str()) != 0 || reload){
        M5.Display.fillScreen(BGCOLOR);
        M5.Display.drawRoundRect(5,5,WIDTH-10,HEIGHT-10,5,FGCOLOR);
        index=0;
        Serial.println("reload to read: " + Folder);
        readFs(fs, Folder, allowed_ext);
        PreFolder = Folder;
        maxFiles = fileList.size()-1;
        reload=false;
      }
      if(fileList.size()<2) readFs(fs, Folder,allowed_ext);

      listFiles(index, fileList);
      delay(REDRAW_DELAY);
      redraw = false;
    }

    #ifdef CARDPUTER
      if(checkEscPress()) break;  // quit

      const short PAGE_JUMP_SIZE = 5;
      if(checkNextPagePress()) {
        index += PAGE_JUMP_SIZE;
        if(index>maxFiles) index=maxFiles-1; // check bounds
        redraw = true;
        continue;
      }
      if(checkPrevPagePress()) {
        index -= PAGE_JUMP_SIZE;
        if(index<0) index = 0;  // check bounds
        redraw = true;
        continue;
      }
    #endif

    if(checkPrevPress()) {
      if(index==0) index = maxFiles;
      else if(index>0) index--;
      redraw = true;
    }
    /* DW Btn to next item */
    if(checkNextPress()) {
      if(index==maxFiles) index = 0;
      else index++;
      redraw = true;
    }

    /* Select to install */
    if(checkSelPress()) {
      delay(200);

      if(fileList[index].folder==true && fileList[index].operation==false) {
        Folder = Folder + (Folder=="/"? "":"/") +  fileList[index].filename; //Folder=="/"? "":"/" +
        //Debug viewer
        Serial.println(Folder);
        redraw=true;
      }
      else if (fileList[index].folder==false && fileList[index].operation==false) {
        //Save the file/folder info to Clear memory to allow other functions to work better
        String filepath=Folder + (Folder=="/"? "":"/") +  fileList[index].filename; //
        String filename=fileList[index].filename;
        //Debug viewer
        Serial.println(filepath + " --> " + filename);
        fileList.clear(); // Clear memory to allow other functions to work better

        result = filepath;
        break;
      }
      else {
        if(Folder == "/") break;
        Folder = Folder.substring(0,Folder.lastIndexOf('/'));
        if(Folder=="") Folder = "/";
        Serial.println("Going to folder: " + Folder);
        index = 0;
        redraw=true;
      }
      redraw = true;
    }
  }

  fileList.clear();
  return result;
}


/////////////////////////
// Chameleon Functions //
/////////////////////////

void displayBanner() {
  printTitle("CHAMELEON");

  switch (currentMode) {
    case BATTERY_INFO_MODE:
        printSubtitle("BATTERY INFO");
        break;
    case FACTORY_RESET_MODE:
        printSubtitle("FACTORY RESET");
        break;
    case FULL_SCAN_MODE:
        printSubtitle("FULL SCAN MODE");
        break;

    case LF_READ_MODE:
        printSubtitle("LF READ MODE");
        break;
    case LF_SCAN_MODE:
        printSubtitle("LF SCAN MODE");
        break;
    case LF_CLONE_MODE:
        printSubtitle("LF CLONE MODE");
        break;
    case LF_CUSTOM_UID_MODE:
        printSubtitle("LF CUSTOM UID MODE");
        break;
    case LF_EMULATION_MODE:
        printSubtitle("LF EMULATION MODE");
        break;
    case LF_SAVE_MODE:
        printSubtitle("LF SAVE MODE");
        break;
    case LF_LOAD_MODE:
        printSubtitle("LF LOAD MODE");
        break;

    case HF_READ_MODE:
        printSubtitle("HF READ MODE");
        break;
    case HF_SCAN_MODE:
        printSubtitle("HF SCAN MODE");
        break;
    case HF_CLONE_MODE:
        printSubtitle("HF CLONE MODE");
        break;
    case HF_WRITE_MODE:
        printSubtitle("HF WRITE MODE");
        break;
    case HF_CUSTOM_UID_MODE:
        printSubtitle("HF CUSTOM UID MODE");
        break;
    case HF_EMULATION_MODE:
        printSubtitle("HF EMULATION MODE");
        break;
    case HF_SAVE_MODE:
        printSubtitle("HF SAVE MODE");
        break;
    case HF_LOAD_MODE:
        printSubtitle("HF LOAD MODE");
        break;
  }

  M5.Display.setTextSize(FP);
  padprintln("");
  padprintln("Press [OK] to change mode.");
  padprintln("");
}

void dumpHFCardDetails() {
  padprintln("Device type: " + printableHFUID.piccType);
  padprintln("UID: " + printableHFUID.uid);
  padprintln("ATQA: " + printableHFUID.atqa);
  padprintln("SAK: " + printableHFUID.sak);
  if (!pageReadSuccess) padprintln("[!] Failed to read data blocks");
}

void dumpScanResults() {
  for (int i = _scanned_tags.size(); i > 0; i--) {
    if (_scanned_tags.size() > 5 && i <= _scanned_tags.size()-5) return;
    padprintln(String(i) + ": " + _scanned_tags[i-1].tagType + " | " + _scanned_tags[i-1].uid);
  }
}


bool connect() {
  displayInfo("Turn on Chameleon device", true);

  displayBanner();
  padprintln("");
  padprintln("Searching Chameleon Device...");

  if (!chmUltra.searchChameleonDevice()) {
    displayError("Chameleon not found");
    delay(1000);
    return false;
  }

  if (!chmUltra.connectToChamelon()) {
    displayError("Chameleon connect error");
    delay(1000);
    return false;
  }

  displaySuccess("Chameleon Connected");
  delay(1000);

  return true;
}

void setMode(AppMode mode) {
  currentMode = mode;
  _battery_set = false;

  displayBanner();

  if (_scanned_set.size() > 0) {
      saveScanResult();
      _scanned_set.clear();
      _scanned_tags.clear();
  }

  chmUltra.cmdChangeMode(chmUltra.HW_MODE_READER);

  switch (mode) {
    case LF_READ_MODE:
    case HF_READ_MODE:
      _lf_read_uid = false;
      _hf_read_uid = false;
      break;
    case LF_SCAN_MODE:
    case HF_SCAN_MODE:
    case FULL_SCAN_MODE:
      _scanned_set.clear();
      _scanned_tags.clear();
      break;
    case LF_LOAD_MODE:
    case HF_LOAD_MODE:
    case LF_CUSTOM_UID_MODE:
    case HF_CUSTOM_UID_MODE:
      _lf_read_uid = false;
      _hf_read_uid = false;
      break;
    case LF_CLONE_MODE:
      padprintln("New UID: " + printableLFUID);
      padprintln("");
      break;
    case HF_CLONE_MODE:
      padprintln("Device type: " + printableHFUID.piccType);
      padprintln("New UID: " + printableHFUID.uid);
      padprintln("");
      break;
    case LF_EMULATION_MODE:
      padprintln("UID: " + printableLFUID);
      padprintln("");
      break;
    case HF_EMULATION_MODE:
      padprintln("Device type: " + printableHFUID.piccType);
      padprintln("UID: " + printableHFUID.uid);
      padprintln("");
      break;

    case LF_SAVE_MODE:
    case HF_SAVE_MODE:
    case BATTERY_INFO_MODE:
    case FACTORY_RESET_MODE:
      break;
  }
  delay(300);
}

void selectMode() {
  std::vector<Option> options = {};

  if (_hf_read_uid) {
    options.push_back({"HF Clone UID",  [=]() { setMode(HF_CLONE_MODE); }});
    options.push_back({"HF Write data", [=]() { setMode(HF_WRITE_MODE); }});
    options.push_back({"HF Emulation",  [=]() { setMode(HF_EMULATION_MODE); }});
    options.push_back({"HF Save file",  [=]() { setMode(HF_SAVE_MODE); }});
  }
  options.push_back({"HF Read",        [=]() { setMode(HF_READ_MODE); }});
  options.push_back({"HF Scan",        [=]() { setMode(HF_SCAN_MODE); }});
  options.push_back({"HF Load file",   [=]() { setMode(HF_LOAD_MODE); }});
  options.push_back({"HF Custom UID",  [=]() { setMode(HF_CUSTOM_UID_MODE); }});

  if (_lf_read_uid) {
    options.push_back({"LF Clone UID",  [=]() { setMode(LF_CLONE_MODE); }});
    options.push_back({"LF Emulation",  [=]() { setMode(LF_EMULATION_MODE); }});
    options.push_back({"LF Save file",  [=]() { setMode(LF_SAVE_MODE); }});
  }
  options.push_back({"LF Read",        [=]() { setMode(LF_READ_MODE); }});
  options.push_back({"LF Scan",        [=]() { setMode(LF_SCAN_MODE); }});
  options.push_back({"LF Load file",   [=]() { setMode(LF_LOAD_MODE); }});
  options.push_back({"LF Custom UID",  [=]() { setMode(LF_CUSTOM_UID_MODE); }});

  options.push_back({"Full Scan",      [=]() { setMode(FULL_SCAN_MODE); }});
  options.push_back({"Factory Reset",  [=]() { setMode(FACTORY_RESET_MODE); }});

  delay(200);
  loopOptions(options);
}


void getBatteryInfo() {
    if (_battery_set) return;

  chmUltra.cmdBatteryInfo();

  displayBanner();
  padprintln("");
  padprintln("Battery " + String(chmUltra.cmdResponse.data[2]) + "%");

  _battery_set = true;

  delay(500);
}

void factoryReset() {
  bool proceed = false;

  std::vector<Option> options = {
    {"No",  [&]() { proceed=false; }},
    {"Yes", [&]() { proceed=true; }},
  };
  delay(200);
  loopOptions(options,true,"Proceed with Factory Reset?");

  displayBanner();

  if (!proceed) {
    displayInfo("Aborting factory reset.");
  }
  else if (chmUltra.cmdFactoryReset()) {
    displaySuccess("Factory reset success");
  }
  else {
    displayError("Factory reset error");
  }

  delay(1000);
}


void readLFTag() {
  if (!chmUltra.cmdLFRead()) return;

  formatLFUID();
  lfTagData = chmUltra.lfTagData;

  displayBanner();
  padprintln("UID: " + printableLFUID);

  _lf_read_uid = true;
  delay(500);
}

void scanLFTags() {
  if (!chmUltra.cmdLFRead()) return;

  formatLFUID();

  if (_scanned_set.find(printableLFUID) == _scanned_set.end()) {
    Serial.println("New LF tag found: " + printableLFUID);
    _scanned_set.insert(printableLFUID);
    _scanned_tags.push_back({"LF", printableLFUID});
  }

  displayBanner();
  dumpScanResults();

  delay(200);
}

void cloneLFTag() {
  if (!chmUltra.cmdLFRead()) return;

  if (chmUltra.cmdLFWrite(lfTagData.uidByte, lfTagData.size)) {
    displaySuccess("UID written successfully.");
  } else {
    displayError("Error writing UID to tag.");
  }

  delay(1000);
  setMode(BATTERY_INFO_MODE);
}

void customLFUid() {
  String custom_uid = keyboard("", 10, "UID (hex):");

  custom_uid.trim();
  custom_uid.replace(" ", "");
  custom_uid.toUpperCase();

  displayBanner();

  if (custom_uid.length() != 10) {
    displayError("Invalid UID");
    delay(1000);
    return setMode(BATTERY_INFO_MODE);
  }

  printableLFUID = "";
  for (size_t i = 0; i < custom_uid.length(); i += 2) {
    printableLFUID += custom_uid.substring(i, i + 2) + " ";
  }
  printableLFUID.trim();
  parseLFUID();

  std::vector<Option> options = {
    {"Clone UID",  [=]() { setMode(LF_CLONE_MODE); }},
    {"Emulate",    [=]() { setMode(LF_EMULATION_MODE); }},
  };
  delay(200);
  loopOptions(options);
}

void emulateLF() {
  uint8_t slot = selectSlot();

  displayBanner();

  if (
    chmUltra.cmdEnableSlot(slot, chmUltra.RFID_LF)
    && chmUltra.cmdChangeActiveSlot(slot)
    && chmUltra.cmdLFEconfig(lfTagData.uidByte, lfTagData.size)
    && chmUltra.cmdChangeMode(chmUltra.HW_MODE_EMULATOR)
  ) {
    displaySuccess("Emulation successful.");
  } else {
    displayError("Error emulating LF tag.");
  }

  delay(1000);
  setMode(BATTERY_INFO_MODE);
}

void loadFileLF() {
  displayBanner();

  if (readFileLF()) {
    displaySuccess("File loaded");
    delay(1000);
    _lf_read_uid = true;

    std::vector<Option> options = {
      {"Clone UID",  [=]() { setMode(LF_CLONE_MODE); }},
      {"Emulate",    [=]() { setMode(LF_EMULATION_MODE); }},
    };
    delay(200);
    loopOptions(options);
  }
  else {
    displayError("Error loading file");
    delay(1000);
    setMode(BATTERY_INFO_MODE);
  }
}

void saveFileLF() {
  String data = printableLFUID;
  data.replace(" ", "");
  String filename = keyboard(data, 30, "File name:");

  displayBanner();

  if (writeFileLF(filename)) {
    displaySuccess("File saved.");
  }
  else {
    displayError("Error writing file.");
  }
  delay(1000);
  setMode(BATTERY_INFO_MODE);
}

bool readFileLF() {
  String filepath;
  File file;
  FS *fs;

  if(!getFsStorage(fs)) return false;
  filepath = loopSD(*fs, "RFIDLF");
  file = fs->open(filepath, FILE_READ);

  if (!file) {
    return false;
  }

  String line;
  String strData;

  while (file.available()) {
    line = file.readStringUntil('\n');
    strData = line.substring(line.indexOf(":") + 1);
    strData.trim();
    if(line.startsWith("UID:")) printableLFUID = strData;
  }

  file.close();
  delay(100);
  parseLFUID();

  return true;
}

bool writeFileLF(String filename) {
  FS *fs;
  if(!getFsStorage(fs)) return false;

  if (!(*fs).exists("/Chameleon")) (*fs).mkdir("/Chameleon");
  if ((*fs).exists("/Chameleon/" + filename + ".rfidlf")) {
    int i = 1;
    filename += "_";
    while((*fs).exists("/Chameleon/" + filename + String(i) + ".rfidlf")) i++;
    filename += String(i);
  }
  File file = (*fs).open("/Chameleon/"+ filename + ".rfidlf", FILE_WRITE);

  if(!file) {
    return false;
  }

  file.println("Filetype: Chameleon RFID 125kHz File");
  file.println("Version 1");
  file.println("UID: " + printableLFUID);

  file.close();
  delay(100);
  return true;
}

void formatLFUID() {
  printableLFUID = "";
  for (byte i = 0; i < chmUltra.lfTagData.size; i++) {
    printableLFUID += chmUltra.lfTagData.uidByte[i] < 0x10 ? " 0" : " ";
    printableLFUID += String(chmUltra.lfTagData.uidByte[i], HEX);
  }
  printableLFUID.trim();
  printableLFUID.toUpperCase();
}

void parseLFUID() {
  String strUID = printableLFUID;
  strUID.trim();
  strUID.replace(" ", "");

  lfTagData.size = strUID.length() / 2;
  for (size_t i = 0; i < strUID.length(); i += 2) {
    lfTagData.uidByte[i / 2] = strtoul(strUID.substring(i, i + 2).c_str(), NULL, 16);
  }
}


void readHFTag() {
  if (!chmUltra.cmd14aScan()) return;

  displayInfo("Reading data blocks...");
  if (chmUltra.hfTagData.sak == 0x00) chmUltra.cmdMfuVersion();

  pageReadSuccess = readHFDataBlocks();

  formatHFData();
  hfTagData = chmUltra.hfTagData;

  displayBanner();
  dumpHFCardDetails();

  _hf_read_uid = true;
  delay(500);
}

void scanHFTags() {
  if (!chmUltra.cmd14aScan()) return;

  formatHFData();

  if (_scanned_set.find(printableHFUID.uid) == _scanned_set.end()) {
    Serial.println("New HF tag found: " + printableHFUID.uid);
    _scanned_set.insert(printableHFUID.uid);
    _scanned_tags.push_back({"HF", printableHFUID.uid});
  }

  displayBanner();
  dumpScanResults();

  delay(200);
}

void cloneHFTag() {
  if (!chmUltra.cmd14aScan()) return;

  if (chmUltra.hfTagData.sak != hfTagData.sak) {
    displayError("Tag types do not match.");
    delay(1000);
    return;
  }

  if (chmUltra.cmdMfSetUid(hfTagData.uidByte, hfTagData.size)) {
    displaySuccess("UID written successfully.");
  } else {
    displayError("Error writing UID to tag.");
  }

  delay(1000);
  setMode(BATTERY_INFO_MODE);
}

void writeHFData() {
  if (!chmUltra.cmd14aScan()) return;

  if (chmUltra.hfTagData.sak != hfTagData.sak) {
    displayError("Tag types do not match.");
    delay(1000);
    return;
  }

  if (writeHFDataBlocks()) {
    displaySuccess("Tag written successfully.");
  } else {
    displayError("Error writing data to tag.");
  }

  delay(1000);
  setMode(BATTERY_INFO_MODE);
}

void customHFUid() {
  String custom_uid = keyboard("", 14, "UID (hex):");

  custom_uid.trim();
  custom_uid.replace(" ", "");
  custom_uid.toUpperCase();

  displayBanner();

  if (custom_uid.length() != 8 && custom_uid.length() != 14) {
    displayError("Invalid UID");
    delay(1000);
    return setMode(BATTERY_INFO_MODE);
  }

  printableHFUID.uid = "";
  for (size_t i = 0; i < custom_uid.length(); i += 2) {
    printableHFUID.uid += custom_uid.substring(i, i + 2) + " ";
  }
  printableHFUID.uid.trim();

  printableHFUID.sak = custom_uid.length() == 8 ? "08" : "00";
  printableHFUID.atqa = custom_uid.length() == 8 ? "0004" : "0044";
  pageReadSuccess = true;
  parseHFData();
  printableHFUID.piccType = chmUltra.getTagTypeStr(hfTagData.sak);

  std::vector<Option> options = {
    {"Clone UID",  [=]() { setMode(HF_CLONE_MODE); }},
    {"Emulate",    [=]() { setMode(HF_EMULATION_MODE); }},
  };
  delay(200);
  loopOptions(options);
}

void emulateHF() {
  if (!isMifareClassic(hfTagData.sak)) {
    displayError("Not implemented for this tag type");
    delay(1000);
    return setMode(BATTERY_INFO_MODE);
  }

  String strDump = "";
  String strData = "";
  String line = "";
  int startIndex = 0;
  int finalIndex;

  while(true) {
    finalIndex = strAllPages.indexOf("\n", startIndex);
    if (finalIndex == -1) finalIndex = strAllPages.length();

    line = strAllPages.substring(startIndex, finalIndex);
    if (line.length() < 5) break;

    strData = line.substring(line.indexOf(":") + 1);
    strData.trim();
    strDump += strData;

    startIndex = finalIndex + 1;
  }
  strDump.trim();
  strDump.replace(" ", "");

  uint8_t slot = selectSlot();

  ChameleonUltra::TagType tagType = chmUltra.getTagType(hfTagData.sak);

  displayBanner();

  if (
    chmUltra.cmdEnableSlot(slot, chmUltra.RFID_HF)
    && chmUltra.cmdChangeActiveSlot(slot)
    && chmUltra.cmdChangeSlotType(slot, tagType)
    && chmUltra.cmdMfEload(strDump)
    && chmUltra.cmdMfEconfig(hfTagData.uidByte, hfTagData.size, hfTagData.atqaByte, hfTagData.sak)
    && chmUltra.cmdChangeMode(chmUltra.HW_MODE_EMULATOR)
  ) {
    displaySuccess("Emulation successful.");
  } else {
    displayError("Error emulating HF tag.");
  }

  delay(1000);
  setMode(BATTERY_INFO_MODE);
}

void loadFileHF() {
  displayBanner();

  if (readFileHF()) {
    displaySuccess("File loaded");
    delay(1000);
    _hf_read_uid = true;

    std::vector<Option> options = {
      {"Clone UID",  [=]() { setMode(HF_CLONE_MODE); }},
      {"Write Data", [=]() { setMode(HF_WRITE_MODE); }},
      {"Emulate",    [=]() { setMode(HF_EMULATION_MODE); }},
    };
    delay(200);
    loopOptions(options);
  }
  else {
    displayError("Error loading file");
    delay(1000);
    setMode(BATTERY_INFO_MODE);
  }
}

void saveFileHF() {
  String uid_str = printableHFUID.uid;
  uid_str.replace(" ", "");
  String filename = keyboard(uid_str, 30, "File name:");

  displayBanner();

  if (writeFileHF(filename)) {
    displaySuccess("File saved.");
  }
  else {
    displayError("Error writing file.");
  }
  delay(1000);
  setMode(BATTERY_INFO_MODE);
}

bool readFileHF() {
  String filepath;
  File file;
  FS *fs;

  if(!getFsStorage(fs)) return false;
  filepath = loopSD(*fs, "RFID|NFC");
  file = fs->open(filepath, FILE_READ);

  if (!file) {
      return false;
  }

  String line;
  String strData;
  strAllPages = "";
  pageReadSuccess = true;

  while (file.available()) {
      line = file.readStringUntil('\n');
      strData = line.substring(line.indexOf(":") + 1);
      strData.trim();
      if(line.startsWith("Device type:"))  printableHFUID.piccType = strData;
      if(line.startsWith("UID:"))          printableHFUID.uid = strData;
      if(line.startsWith("SAK:"))          printableHFUID.sak = strData;
      if(line.startsWith("ATQA:"))         printableHFUID.atqa = strData;
      if(line.startsWith("Pages total:"))  dataPages = strData.toInt();
      if(line.startsWith("Pages read:"))   pageReadSuccess = false;
      if(line.startsWith("Page "))         strAllPages += line + "\n";
  }

  file.close();
  delay(100);
  parseHFData();

  return true;
}

bool writeFileHF(String filename) {
  FS *fs;
  if(!getFsStorage(fs)) return false;

  if (!(*fs).exists("/Chameleon")) (*fs).mkdir("/Chameleon");
  if ((*fs).exists("/Chameleon/" + filename + ".rfid")) {
      int i = 1;
      filename += "_";
      while((*fs).exists("/Chameleon/" + filename + String(i) + ".rfid")) i++;
      filename += String(i);
  }
  File file = (*fs).open("/Chameleon/"+ filename + ".rfid", FILE_WRITE);

  if(!file) {
      return false;
  }

  file.println("Filetype: Chameleon RFID File");
  file.println("Version 1");
  file.println("Device type: " + printableHFUID.piccType);
  file.println("# UID, ATQA and SAK are common for all formats");
  file.println("UID: " + printableHFUID.uid);
  file.println("SAK: " + printableHFUID.sak);
  file.println("ATQA: " + printableHFUID.atqa);
  file.println("# Memory dump");
  file.println("Pages total: " + String(dataPages));
  if (!pageReadSuccess) file.println("Pages read: " + String(dataPages));
  file.print(strAllPages);

  file.close();
  delay(100);
  return true;
}

bool readHFDataBlocks() {
  dataPages = 0;
  totalPages = 0;
  bool readSuccess = false;
  strAllPages = "";

  switch (chmUltra.hfTagData.sak) {
    case 0x08:
    case 0x09:
    case 0x10:
    case 0x11:
    case 0x18:
    case 0x19:
      readSuccess = readMifareClassicDataBlocks({});
      break;

    case 0x00:
      readSuccess = readMifareUltralightDataBlocks();
      break;

    default:
      break;
  }

  return readSuccess;
}

bool readMifareClassicDataBlocks(uint8_t *key) {
  bool sectorReadSuccess;

  switch (chmUltra.hfTagData.sak) {
    case 0x09:
      totalPages = 20;  // 320 bytes / 16 bytes per page
      break;

    case 0x08:
      totalPages = 64;  // 1024 bytes / 16 bytes per page
      break;

    case 0x18:
      totalPages = 256;  // 4096 bytes / 16 bytes per page
      break;

    case 0x19:
      totalPages = 128;  // 2048 bytes / 16 bytes per page
      break;

    default: // Should not happen. Ignore.
      break;
  }

  String strPage;

  for (byte i = 0; i < totalPages; i++) {
    if (!chmUltra.cmdMfReadBlock(i, key)) return false;

    strPage = "";
    for (byte index = 0; index < chmUltra.cmdResponse.dataSize; index++) {
      strPage += chmUltra.cmdResponse.data[index] < 0x10 ? F(" 0") : F(" ");
      strPage += String(chmUltra.cmdResponse.data[index], HEX);
    }
    strPage.trim();
    strPage.toUpperCase();

    strAllPages += "Page " + String(dataPages) + ": " + strPage + "\n";
    dataPages++;
  }

  return true;
}

bool readMifareUltralightDataBlocks() {
  String strPage;

  ChameleonUltra::TagType tagType = chmUltra.getTagType(chmUltra.hfTagData.sak);

  switch (tagType) {
    case ChameleonUltra::NTAG_210:
    case ChameleonUltra::MF0UL11:
      totalPages = 20;
      break;
    case ChameleonUltra::NTAG_212:
    case ChameleonUltra::MF0UL21:
      totalPages = 41;
      break;
    case ChameleonUltra::NTAG_213:
      totalPages = 45;
      break;
    case ChameleonUltra::NTAG_215:
      totalPages = 135;
      break;
    case ChameleonUltra::NTAG_216:
      totalPages = 231;
      break;
    default:
      totalPages = 256;
      break;
  }

  for (byte i = 0; i < totalPages; i++) {
    if (!chmUltra.cmdMfuReadPage(i)) return false;
    if (chmUltra.cmdResponse.dataSize == 0) break;

    strPage = "";
    for (byte index = 0; index < chmUltra.cmdResponse.dataSize; index++) {
      strPage += chmUltra.cmdResponse.data[index] < 0x10 ? F(" 0") : F(" ");
      strPage += String(chmUltra.cmdResponse.data[index], HEX);
    }
    strPage.trim();
    strPage.toUpperCase();

    strAllPages += "Page " + String(dataPages) + ": " + strPage + "\n";
    dataPages++;
  }

  return true;
}

bool writeHFDataBlocks() {
  String pageLine = "";
  String strBytes = "";
  int lineBreakIndex;
  int pageIndex;
  bool blockWriteSuccess;
  int totalSize = strAllPages.length();

  while (strAllPages.length() > 0) {
    lineBreakIndex = strAllPages.indexOf("\n");
    pageLine = strAllPages.substring(0, lineBreakIndex);
    strAllPages = strAllPages.substring(lineBreakIndex + 1);

    pageIndex = pageLine.substring(5, pageLine.indexOf(":")).toInt();
    strBytes = pageLine.substring(pageLine.indexOf(":") + 1);
    strBytes.trim();
    strBytes.replace(" ", "");

    if (pageIndex == 0) continue;

    byte size = strBytes.length() / 2;
    byte buffer[size];
    for (size_t i = 0; i < strBytes.length(); i += 2) {
      buffer[i / 2] = strtoul(strBytes.substring(i, i + 2).c_str(), NULL, 16);
    }

    blockWriteSuccess = false;
    if (isMifareClassic(chmUltra.hfTagData.sak)) {
      if (pageIndex == 0 || (pageIndex + 1) % 4 == 0) continue;  // Data blocks for MIFARE Classic
      blockWriteSuccess = chmUltra.cmdMfWriteBlock(pageIndex, {}, buffer, size);
    }
    else if (chmUltra.hfTagData.sak == 0x00) {
      if (pageIndex < 4 || pageIndex >= dataPages-5) continue;  // Data blocks for NTAG21X
      blockWriteSuccess = chmUltra.cmdMfuWritePage(pageIndex, buffer, size);
    }

    if (!blockWriteSuccess) return false;

    progressHandler(totalSize-strAllPages.length(), totalSize, "Writing data blocks...");
  }

  return true;
}

void formatHFData() {
  byte bcc = 0;

  printableHFUID.piccType = chmUltra.getTagTypeStr(chmUltra.hfTagData.sak);

  printableHFUID.sak = chmUltra.hfTagData.sak < 0x10 ? "0" : "";
  printableHFUID.sak += String(chmUltra.hfTagData.sak, HEX);
  printableHFUID.sak.toUpperCase();

  // UID
  printableHFUID.uid = "";
  for (byte i = 0; i < chmUltra.hfTagData.size; i++) {
    printableHFUID.uid += chmUltra.hfTagData.uidByte[i] < 0x10 ? " 0" : " ";
    printableHFUID.uid += String(chmUltra.hfTagData.uidByte[i], HEX);
    bcc = bcc ^ chmUltra.hfTagData.uidByte[i];
  }
  printableHFUID.uid.trim();
  printableHFUID.uid.toUpperCase();

  // BCC
  printableHFUID.bcc = bcc < 0x10 ? "0" : "";
  printableHFUID.bcc += String(bcc, HEX);
  printableHFUID.bcc.toUpperCase();

  // ATQA
  printableHFUID.atqa = "";
  for (byte i = 0; i < 2; i++) {
    printableHFUID.atqa += chmUltra.hfTagData.atqaByte[i] < 0x10 ? " 0" : " ";
    printableHFUID.atqa += String(chmUltra.hfTagData.atqaByte[i], HEX);
  }
  printableHFUID.atqa.trim();
  printableHFUID.atqa.toUpperCase();
}

void parseHFData() {
  String strUID = printableHFUID.uid;
  strUID.trim();
  strUID.replace(" ", "");
  hfTagData.size = strUID.length() / 2;
  for (size_t i = 0; i < strUID.length(); i += 2) {
    hfTagData.uidByte[i / 2] = strtoul(strUID.substring(i, i + 2).c_str(), NULL, 16);
  }

  printableHFUID.sak.trim();
  hfTagData.sak = strtoul(printableHFUID.sak.c_str(), NULL, 16);

  String strATQA = printableHFUID.atqa;
  strATQA.trim();
  strATQA.replace(" ", "");
  for (size_t i = 0; i < strATQA.length(); i += 2) {
    hfTagData.atqaByte[i / 2] = strtoul(strATQA.substring(i, i + 2).c_str(), NULL, 16);
  }
}


uint8_t selectSlot() {
  uint8_t slot = 8;

  std::vector<Option> options = {
    {"1", [&]() { slot=1; }},
    {"2", [&]() { slot=2; }},
    {"3", [&]() { slot=3; }},
    {"4", [&]() { slot=4; }},
    {"5", [&]() { slot=5; }},
    {"6", [&]() { slot=6; }},
    {"7", [&]() { slot=7; }},
    {"8", [&]() { slot=8; }},
  };
  delay(200);
  loopOptions(options,true,"Set Emulation Slot");

  return slot;
}

bool isMifareClassic(byte sak) {
  return (
    sak == 0x08
    || sak == 0x09
    || sak == 0x10
    || sak == 0x11
    || sak == 0x18
    || sak == 0x19
  );
}

void saveScanResult() {
  FS *fs;
  if(!getFsStorage(fs)) return;

  String filename = "scan_result";

  if (!(*fs).exists("/Chameleon")) (*fs).mkdir("/Chameleon");
  if (!(*fs).exists("/Chameleon/Scans")) (*fs).mkdir("/Chameleon/Scans");
  if ((*fs).exists("/Chameleon/Scans/" + filename + ".rfidscan")) {
    int i = 1;
    filename += "_";
    while((*fs).exists("/Chameleon/Scans/" + filename + String(i) + ".rfidscan")) i++;
    filename += String(i);
  }
  File file = (*fs).open("/Chameleon/Scans/"+ filename + ".rfidscan", FILE_WRITE);

  if(!file) {
    return;
  }

  file.println("Filetype: Chameleon RFID Scan Result");
  for (ScanResult scanResult : _scanned_tags) {
    file.println(scanResult.tagType + " | " + scanResult.uid);
  }

  file.close();
  delay(100);
  return;
}


void fullScanTags() {
  scanLFTags();
  scanHFTags();
}



void setup() {
  Serial.begin(115200);

  auto cfg = M5.config();
#ifdef CARDPUTER
  M5Cardputer.begin(cfg, true);
#else
  M5.begin(cfg);
#endif

  M5.Display.setRotation(ROT);
  M5.Display.setTextColor(FGCOLOR, 0);

  if(!LittleFS.begin(true)) { LittleFS.format(), LittleFS.begin();}
  setupSdCard();

  displayBanner();

  delay(500);
}


void loop() {
  Serial.println("Loop");
  if (!chameleonConnected){
    if (!connect()) return;
    chameleonConnected = true;
    displayBanner();
    setMode(BATTERY_INFO_MODE);
  }

  if (checkEscPress()) {
    checkReboot();
  }

  if (checkSelPress()) {
    selectMode();
  }

  switch (currentMode) {
    case BATTERY_INFO_MODE:
      getBatteryInfo();
      break;

    case FACTORY_RESET_MODE:
      factoryReset();
      break;

    case FULL_SCAN_MODE:
      fullScanTags();
      break;

    case LF_READ_MODE:
      readLFTag();
      break;
    case LF_SCAN_MODE:
      scanLFTags();
      break;
    case LF_CLONE_MODE:
      cloneLFTag();
      break;
    case LF_CUSTOM_UID_MODE:
      customLFUid();
      break;
    case LF_EMULATION_MODE:
      emulateLF();
      break;
    case LF_SAVE_MODE:
      saveFileLF();
      break;
    case LF_LOAD_MODE:
      loadFileLF();
      break;

    case HF_READ_MODE:
      readHFTag();
      break;
    case HF_SCAN_MODE:
      scanHFTags();
      break;
    case HF_WRITE_MODE:
      writeHFData();
      break;
    case HF_CLONE_MODE:
      cloneHFTag();
      break;
    case HF_CUSTOM_UID_MODE:
      customHFUid();
      break;
    case HF_EMULATION_MODE:
      emulateHF();
      break;
    case HF_SAVE_MODE:
      saveFileHF();
      break;
    case HF_LOAD_MODE:
      loadFileHF();
      break;
  }

}
