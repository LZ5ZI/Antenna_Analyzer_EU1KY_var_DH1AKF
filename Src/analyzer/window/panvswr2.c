/*
 *   (c) Yury Kuchura
 *   kuchura@gmail.com
 *
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

#include <stdint.h>
#include <stdio.h>
#include <math.h>       /* atan2 */
#define PI 3.14159265
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include <complex.h>
#include "LCD.h"
#include "touch.h"
#include "font.h"
#include "config.h"
#include "ff.h"
#include "crash.h"
#include "dsp.h"
#include "gen.h"
#include "oslfile.h"
#include "stm32746g_discovery_lcd.h"
#include "screenshot.h"
#include "panvswr2.h"
#include "panfreq.h"
#include "smith.h"
#include "textbox.h"
#include "measurement.h"
#include "generator.h"
#include "FreqCounter.h"

#define PI 3.14159265

#define X0 51
#define Y0 18
#define WWIDTH  400
#define WHEIGHT 200
#define WY(offset) ((WHEIGHT + Y0) - (offset))
//#define WGRIDCOLOR LCD_RGB(80,80,80)
#define WGRIDCOLOR LCD_COLOR_DARKGRAY
#define RED1 LCD_RGB(245,0,0)
#define RED2 LCD_RGB(235,0,0)


#define WGRIDCOLORBR LCD_RGB(160,160,96)
#define SMITH_CIRCLE_BG LCD_BLACK
#define SMITH_LINE_FG LCD_GREEN

#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))

// Please read the article why smoothing looks beautiful but actually
// decreases precision, and averaging increases precision though looks ugly:
// http://www.microwaves101.com/encyclopedias/smoothing-is-cheating
// This analyzer draws both smoothed (bright) and averaged (dark) measurement
// results, you see them both.
#define SMOOTHWINDOW 3 //Must be odd!
#define SMOOTHOFS (SMOOTHWINDOW/2)
#define SMOOTHWINDOW_HI 7 //Must be odd!
#define SMOOTHOFS_HI (SMOOTHWINDOW_HI/2)
#define SM_INTENSITY 64
extern uint8_t rqDel;
//extern void ShowF(void);

typedef enum
{
    GRAPH_VSWR, GRAPH_VSWR_Z, GRAPH_VSWR_RX, GRAPH_RX, GRAPH_SMITH, GRAPH_S11
} GRAPHTYPE;

/*typedef struct
{
    uint32_t flo;
    uint32_t fhi;
} HAM_BANDS;
*/
static const HAM_BANDS hamBands[] =
{
    {1800ul,  2000ul},
    {3500ul,  3800ul},
    {7000ul,  7200ul},
    {10100ul, 10150ul},
    {14000ul, 14350ul},
    {18068ul, 18168ul},
    {21000ul, 21450ul},
    {24890ul, 24990ul},
    {28000ul, 29700ul},
    {50000ul, 52000ul},
    {144000ul, 146000ul},
    {222000ul, 225000ul},
    {430000ul, 440000ul},
};
uint8_t Q_Fs_find=0;
static const uint32_t hamBandsNum = sizeof(hamBands) / sizeof(*hamBands);
static const uint32_t cx0 = 240; //Smith chart center
static const uint32_t cy0 = 120; //Smith chart center
static const int32_t smithradius = 100;
static const char *modstr = "EU1KY AA v." AAVERSION " ";
char str[100]="";

static uint32_t modstrw = 0;
// ** WK ** :
const char* BSSTR[] = {"1 kHz","2 kHz","4 kHz","10 kHz","20 kHz","40 kHz","100 kHz","200 kHz", "400 kHz", "1000 kHz", "2 MHz", "4 MHz", "10 MHz", "20 MHz", "30 MHz", "40 MHz", "100 MHz"};
const char* BSSTR_HALF[] = {"0.5 kHz","1 kHz","2 kHz","5 kHz","10 kHz","20 kHz","50 kHz","100 kHz", "200 kHz", "500 kHz", "1 MHz", "2 MHz", "5 MHz", "10 MHz", "15 MHz", "20 MHz", "50 MHz"};
const uint32_t BSVALUES[] = {1,2,4,10,20,40,100,200, 400, 1000, 2000, 4000, 10000, 20000, 30000, 40000, 100000};


static uint32_t f1 = 14000000; //Scan range start frequency, in Hz
static BANDSPAN span = BS400;
static float fcur;// frequency at cursor position in kHz
static char buf[100];
static LCDPoint pt;
static float complex values[WWIDTH+1];
static int isMeasured = 0;
static uint32_t cursorPos = WWIDTH / 2;
static GRAPHTYPE grType = GRAPH_VSWR;
static uint32_t isSaved = 0;
static uint32_t cursorChangeCount = 0;
static uint32_t autofast = 0;
static int loglog=0;// scale for SWR
extern volatile uint32_t autosleep_timer;

//static void DrawRX();
static void DrawRX(int SelQu, int SelEqu);

static void DrawSmith();
static float complex SmoothRX(int idx, int useHighSmooth);
static TEXTBOX_t SWR_ctx;
void SWR_Exit(void);
static void SWR_2(void);
void SWR_Mute(void);
static void SWR_3(void);
void SWR_SetFrequency(void);
uint32_t QuFindPositivX(BANDSPAN bs);

void QuSaveFile(void);
void QuSetFequency(void);
void QuMeasure(void);
void QuCalibrate(void);
void C0Calibrate(void);
void CxMeasure(void);
void DrawX_Scale(float maxRXi, float minRXi);
static void ShowMeasFr(void);

int sFreq, sCalib;

#define M_BGCOLOR LCD_RGB(0,0,64)    //Menu item background color
#define M_FGCOLOR LCD_RGB(255,255,0) //Menu item foreground color

////////////////////////////////////////////////////////////////
static const TEXTBOX_t tb_menuMeasCx[] = {
    (TEXTBOX_t){.x0 = 309, .y0 = 231, .text =   "Calibrate Co", .font = FONT_FRANBIG,.width = 170, .height = 40, .center = 1,
                 .border = TEXTBOX_BORDER_BUTTON, .fgcolor = M_FGCOLOR, .bgcolor = M_BGCOLOR,   .cb = C0Calibrate ,             .cbparam = 1,.next = (void*)&tb_menuMeasCx[1] },

    (TEXTBOX_t){.x0 =   0, .y0 =   0, .text = "-500kHz",        .font = FONT_FRANBIG,.width =  110, .height = 40, .center = 1,
                 .border = TEXTBOX_BORDER_BUTTON, .fgcolor = M_FGCOLOR, .bgcolor = M_BGCOLOR,   .cb = MEASUREMENT_FDecr_500k , .cbparam = 1, .next = (void*)&tb_menuMeasCx[2] },

    (TEXTBOX_t){.x0 = 369, .y0 =   0, .text = "+500kHz",        .font = FONT_FRANBIG,.width =  110, .height = 40, .center = 1,
                 .border = TEXTBOX_BORDER_BUTTON, .fgcolor = M_FGCOLOR, .bgcolor = M_BGCOLOR,   .cb = MEASUREMENT_FIncr_500k , .cbparam = 1, .next = (void*)&tb_menuMeasCx[3] },

    (TEXTBOX_t){.x0 = 90, .y0 = 231, .text =    "Set Frequency", .font = FONT_FRANBIG,.width = 180, .height = 40, .center = 1,
                 .border = TEXTBOX_BORDER_BUTTON, .fgcolor = M_FGCOLOR, .bgcolor = M_BGCOLOR,   .cb = QuSetFequency , .cbparam = 1, .next = (void*)&tb_menuMeasCx[4] },

     (TEXTBOX_t){.x0 = 0, .y0 = 231, .text = "Exit"   ,        .font = FONT_FRANBIG, .width = 70, .height = 40, .center = 1,
                 .border = TEXTBOX_BORDER_BUTTON, .fgcolor = M_FGCOLOR, .bgcolor = LCD_RED,     .cb = (void(*)(void))SWR_Exit, .cbparam = 1,},
};


//////////////////////////////////////////////////////////////////
static const TEXTBOX_t tb_menuQuartz[] = {
/*    (TEXTBOX_t){.x0 = 290, .y0 = 237, .text =    "Set Frequency", .font = FONT_FRANBIG,.width = 180, .height = 34, .center = 1,
                 .border = TEXTBOX_BORDER_BUTTON, .fgcolor = M_FGCOLOR, .bgcolor = M_BGCOLOR,   .cb = QuSetFequency , .cbparam = 1, .next = (void*)&tb_menuQuartz[1] },
*/
    (TEXTBOX_t){.x0 = 200, .y0 = 180, .text =   "Calibrate OPEN", .font = FONT_FRANBIG,.width = 220, .height = 34, .center = 1,
                 .border = TEXTBOX_BORDER_BUTTON, .fgcolor = M_FGCOLOR, .bgcolor = M_BGCOLOR,   .cb = QuCalibrate , .cbparam = 1,     .next = (void*)&tb_menuQuartz[1] },

    (TEXTBOX_t){.x0 = 80, .y0 = 237, .text =  "Start", .font = FONT_FRANBIG,.width = 100, .height = 34, .center = 1,
                 .border = TEXTBOX_BORDER_BUTTON, .fgcolor = M_FGCOLOR, .bgcolor = M_BGCOLOR,   .cb = QuMeasure , .cbparam = 1, .next = (void*)&tb_menuQuartz[2] },

    (TEXTBOX_t){ .x0 = 1, .y0 = 237, .text = "Exit", .font = FONT_FRANBIG, .width = 70, .height = 34, .center = 1,
                 .border = TEXTBOX_BORDER_BUTTON, .fgcolor = M_FGCOLOR, .bgcolor = LCD_RED,     .cb = (void(*)(void))SWR_Exit, .cbparam = 1,},
};


static const TEXTBOX_t tb_menuQuartz2[] = {

    (TEXTBOX_t){.x0 = 80, .y0 = 237, .text =  "Start", .font = FONT_FRANBIG,.width = 100, .height = 34, .center = 1,
                 .border = TEXTBOX_BORDER_BUTTON, .fgcolor = M_FGCOLOR, .bgcolor = M_BGCOLOR, .cb = QuMeasure , .cbparam = 1, .next = (void*)&tb_menuQuartz2[1] },
    (TEXTBOX_t){.x0 = 290, .y0 = 237, .text =    "Set Frequency", .font = FONT_FRANBIG,.width = 180, .height = 34, .center = 1,
                 .border = TEXTBOX_BORDER_BUTTON, .fgcolor = M_FGCOLOR, .bgcolor = M_BGCOLOR,   .cb = QuSetFequency , .cbparam = 1, .next = (void*)&tb_menuQuartz2[2] },
    (TEXTBOX_t){.x0 = 359, .y0 = 0, .text =  "Save File", .font = FONT_FRANBIG,.width = 120, .height = 34, .center = 1,
                 .border = TEXTBOX_BORDER_BUTTON, .fgcolor = M_FGCOLOR, .bgcolor = M_BGCOLOR,   .cb = QuSaveFile , .cbparam = 1, .next = (void*)&tb_menuQuartz2[3] },
    (TEXTBOX_t){ .x0 = 0, .y0 = 237, .text = "Exit", .font = FONT_FRANBIG, .width = 70, .height = 34, .center = 1,
                 .border = TEXTBOX_BORDER_BUTTON, .fgcolor = M_FGCOLOR, .bgcolor = LCD_RED, .cb = (void(*)(void))SWR_Exit, .cbparam = 1,},
};


static const TEXTBOX_t tb_menuSWR[] = {
    (TEXTBOX_t){.x0 = 70, .y0 = 210, .text =    "Frequency", .font = FONT_FRANBIG,.width = 120, .height = 34, .center = 1,
                 .border = TEXTBOX_BORDER_BUTTON, .fgcolor = M_FGCOLOR, .bgcolor = M_BGCOLOR, .cb = SWR_SetFrequency , .cbparam = 1, .next = (void*)&tb_menuSWR[1] },
   (TEXTBOX_t){.x0 = 280, .y0 = 210, .text =  "SWR_2", .font = FONT_FRANBIG,.width = 100, .height = 34, .center = 1,
                 .border = TEXTBOX_BORDER_BUTTON, .fgcolor = M_FGCOLOR, .bgcolor = M_BGCOLOR, .cb = SWR_2 , .cbparam = 1, .next = (void*)&tb_menuSWR[2] },
    (TEXTBOX_t){.x0 = 380, .y0 = 210, .text =  "SWR_3", .font = FONT_FRANBIG,.width = 96, .height = 34, .center = 1,
                 .border = TEXTBOX_BORDER_BUTTON, .fgcolor = M_FGCOLOR, .bgcolor = M_BGCOLOR, .cb = SWR_3 , .cbparam = 1, .next = (void*)&tb_menuSWR[3] },
    (TEXTBOX_t){.x0 = 190, .y0 = 210, .text =  "Tone", .font = FONT_FRANBIG,.width = 90, .height = 34, .center = 1,
                 .border = TEXTBOX_BORDER_BUTTON, .fgcolor = M_FGCOLOR, .bgcolor = M_BGCOLOR, .cb = SWR_Mute , .cbparam = 1, .next = (void*)&tb_menuSWR[4] },
    (TEXTBOX_t){ .x0 = 0, .y0 = 210, .text = "Exit", .font = FONT_FRANBIG, .width = 70, .height = 34, .center = 1,
                 .border = TEXTBOX_BORDER_BUTTON, .fgcolor = M_FGCOLOR, .bgcolor = LCD_RED, .cb = (void(*)(void))SWR_Exit, .cbparam = 1,},
};

static uint32_t multi_fr[5]  = {1850,21200,27800,3670,7150};//Multi SWR frequencies in kHz
static uint32_t multi_bw[5]  = {200,1000,200,400,100};//Multi SWR bandwidth in kHz
static BANDSPAN multi_bwNo[5]  = {6,8,6,5,4};//Multi SWR bandwidth number
static int beep;

void Beep(int duration){
    if (BeepOn1==0) return;
    if(beep==0){
        beep=1;
        AUDIO1=1;
        UB_TIMER2_Init_FRQ(880);
        UB_TIMER2_Start();
        Sleep(100);
        AUDIO1=0;
       // UB_TIMER2_Stop();
    }
    if(duration==1) beep=0;
}

unsigned long GetUpper(int i){
if((i>=0)&&(i<=12))
    return 1000*hamBands[i].fhi;
return 0;
}
unsigned long GetLower(int i){
if((i>=0)&&(i<=12))
    return 1000*hamBands[i].flo;
return 0;
}

void DrawFootText(void){
    FONT_Write(FONT_FRANBIG, CurvColor, BackGrColor, 2, 244, "Exit");
    LCD_Rectangle(LCD_MakePoint(0,249),LCD_MakePoint(60,270),CurvColor);
    FONT_Write(FONT_FRAN, CurvColor, BackGrColor, 81, 251, "- Zoom +");
    LCD_Rectangle(LCD_MakePoint(70,249),LCD_MakePoint(104,270),CurvColor);
    LCD_Rectangle(LCD_MakePoint(70,249),LCD_MakePoint(138,270),CurvColor);
    FONT_Write(FONT_FRANBIG, CurvColor, BackGrColor, 420, 244, "Scan");
    LCD_Rectangle(LCD_MakePoint(410,249),LCD_MakePoint(478,270),CurvColor);
}


int GetBandNr(unsigned long freq){
int i, found=0;
    for(i=0;i<=12;i++){
        if(GetLower(i)>=freq){
            found=1;
            i--;
            break;
        }
    }
    if(found==1){
        if(GetUpper(i)>=freq)
            return i;
    }
    if((GetLower(12)<=freq)&&(GetUpper(12)>=freq)) return 12;
    return -1;// not in a Ham band
}

static void WK_InvertPixel(LCDPoint p){
LCDColor    c;
    c=LCD_ReadPixel(p);
    switch (c){
    case LCD_COLOR_YELLOW:
        {
            LCD_SetPixel(p,LCD_COLOR_RED);
            return;
        }
    case LCD_COLOR_WHITE:
        {
            LCD_SetPixel(p,RED1);
            return;
        }
    case LCD_COLOR_DARKGRAY:
        {
            LCD_SetPixel(p,RED2);
            return;
        }
    case LCD_COLOR_RED:
        {
            LCD_SetPixel(p,LCD_COLOR_YELLOW);
            return;
        }
    case RED1:
        {
            LCD_SetPixel(p,LCD_COLOR_WHITE);
            return;
        }
    case RED2:
        {
            LCD_SetPixel(p,LCD_COLOR_DARKGRAY);
            return;
        }
    default:LCD_InvertPixel(p);
    }
}

static int swroffset(float swr)
{
    int offs = (int)roundf(150. * log10f(swr));
    if (offs >= WHEIGHT)
        offs = WHEIGHT - 1;
    else if (offs < 0)
        offs = 0;
    return offs;
}

static int Z_offset(float Z)
{
    int offs = (int)roundf(50. * log10f(Z+1));
    if (offs >= WHEIGHT)
        offs = WHEIGHT - 1;
    else if (offs < 0)
        offs = 0;
    return offs;
}


static float S11Calc(float swr)
{
    float offs = 20 * log10f((swr-1)/(swr+1));
    return offs;
}

static int IsFinHamBands(uint32_t f_kHz)
{
    uint32_t i;
    for (i = 0; i < hamBandsNum; i++)
    {
        if ((f_kHz >= hamBands[i].flo) && (f_kHz <= hamBands[i].fhi))
            return 1;
    }
    return 0;
}

static void DrawCursor()
{
    int8_t i;
    LCDPoint p;
    if (!isMeasured)
        return;

    if (grType == GRAPH_SMITH)
    {
        float complex rx = values[cursorPos]; //SmoothRX(cursorPos, f1 > (CFG_GetParam(CFG_PARAM_BAND_FMAX) / 1000) ? 1 : 0);
        float complex g = OSL_GFromZ(rx, (float)CFG_GetParam(CFG_PARAM_R0));
        uint32_t x = (uint32_t)roundf(cx0 + crealf(g) * 100.);
        uint32_t y = (uint32_t)roundf(cy0 - cimagf(g) * 100.);
        p = LCD_MakePoint(x, y);
        for(i=-4;i<4;i++){
           p.x+=i;
           LCD_InvertPixel(p);
           p.x-=i;
        }
        for(i=-4;i<4;i++){
           p.y+=i;
           LCD_InvertPixel(p);
           p.y-=i;
        }
    }
    else
    {
        //Draw cursor line as inverted image
        p = LCD_MakePoint(X0 + cursorPos, Y0);
        if(ColourSelection==1){// Daylightcolours
            while (p.y < Y0 + WHEIGHT){
               if((p.y % 20)<10)
                    WK_InvertPixel(p);
               else LCD_InvertPixel(p);
               p.y++;

            }
        }
        else{
            while (p.y < Y0 + WHEIGHT){
                if((p.y % 20)<10)
                    LCD_InvertPixel(p);
                p.y++;

            }
        }
        if(FatLines){
            p.x--;
            while (p.y >= Y0)
            {
                LCD_InvertPixel(p);
                p.y--;
            }
            p.x+=2;
            while (p.y < Y0 + WHEIGHT)
            {
                LCD_InvertPixel(p);
                p.y++;
            }
            p.x--;
        }

        LCD_FillRect((LCDPoint){X0 + cursorPos-3,Y0+WHEIGHT+1},(LCDPoint){X0 + cursorPos+3,Y0+WHEIGHT+3},BackGrColor);
        LCD_FillRect((LCDPoint){X0 + cursorPos-2,Y0+WHEIGHT+1},(LCDPoint){X0 + cursorPos+2,Y0+WHEIGHT+3},TextColor);
    }
    Sleep(5);

}
static void DrawCursorText()
{
    float complex rx = values[cursorPos]; //SmoothRX(cursorPos, f1 > (CFG_GetParam(CFG_PARAM_BAND_FMAX) / 1000) ? 1 : 0);
    float ga = cabsf(OSL_GFromZ(rx, (float)CFG_GetParam(CFG_PARAM_R0))); //G magnitude

    uint32_t fstart;
    if (CFG_GetParam(CFG_PARAM_PAN_CENTER_F) == 0)
        fstart = f1;
    else
        fstart = f1 - 500*BSVALUES[span];

    fcur = ((float)(fstart/1000. + (float)cursorPos * BSVALUES[span] / WWIDTH));///1000.;
    if (fcur * 1000.f > (float)(CFG_GetParam(CFG_PARAM_BAND_FMAX) + 1))
        fcur = 0.f;

/*
    float Q = 0.f;
    if ((crealf(rx) > 0.1f) && (fabs(cimagf(rx)) > crealf(rx)))
        Q = fabs(cimagf(rx) / crealf(rx));
    if (Q > 2000.f)
        Q = 2000.f;
*/
    float Q = 0.0;
    if ((crealf(rx) != 0.0)&&(cimagf(rx)>0.0))
        Q = cimagf(rx) / fabs(crealf(rx));
    if (Q > 2000.0)
        Q = 2000.0;
    DrawFootText();

    LCD_FillRect(LCD_MakePoint(0, 234),LCD_MakePoint(478, 248),BackGrColor);
    FONT_Print(FONT_FRAN, TextColor, BackGrColor ,0 , 234, "F: %.3f   Z: %.1f%+.1fj  %.1f°",
               fcur,
               crealf(rx),
               cimagf(rx),atan2(cimagf(rx),crealf(rx))*180.0/PI);

    FONT_Print(FONT_FRAN, TextColor, BackGrColor ,240 , 234, "SWR: %.2f    MCL: %.2f dB",
               DSP_CalcVSWR(rx),
               (ga > 0.01f) ? (-10. * log10f(ga)) : 99.f); // Matched cable loss

    FONT_Print(FONT_FRAN, TextColor, BackGrColor ,420 , 234, "Q: %.1f", Q);

    LCD_HLine(LCD_MakePoint(0,249), 62, CurvColor);
    LCD_HLine(LCD_MakePoint(70,249), 70, CurvColor);
    LCD_HLine(LCD_MakePoint(410,249), 69, CurvColor);
}


static void DrawCursorText1()
{
    float complex rx = values[cursorPos]; //SmoothRX(cursorPos, f1 > (CFG_GetParam(CFG_PARAM_BAND_FMAX) / 1000) ? 1 : 0);
    float ga = cabsf(OSL_GFromZ(rx, (float)CFG_GetParam(CFG_PARAM_R0))); //G magnitude
    float vswr,x,r;

    uint32_t fstart;
    if (CFG_GetParam(CFG_PARAM_PAN_CENTER_F) == 0)
        fstart = f1;
    else
        fstart = f1 - 500*BSVALUES[span];

    fcur = ((float)(fstart/1000. + (float)cursorPos * BSVALUES[span] / WWIDTH));///1000.;
    if (fcur * 1000.f > (float)(CFG_GetParam(CFG_PARAM_BAND_FMAX) + 1))
        fcur = 0.f;

    float Q = 0.0;
    if ((crealf(rx) != 0.0) )
        Q = fabs(cimagf(rx)) / fabs(crealf(rx));
    if (Q > 2000.0)
        Q = 2000.0;
    vswr=DSP_CalcVSWR(rx);
    r=crealf(rx);
    x=cimagf(rx);
    //LCD_FillRect(LCD_MakePoint(0, Y0 + WHEIGHT + 16),LCD_MakePoint(479 , Y0 + WHEIGHT + 30),BackGrColor);
    LCD_FillRect(LCD_MakePoint(0, 234),LCD_MakePoint(478, 248),BackGrColor);

    sprintf(str,"F: %.3f  Z: %.1f%+.1fj",
               fcur,
               r,
               x);
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 0, 234, str);


    sprintf(str,"SWR: %.1f  MCL: %.2f dB",
               vswr,
               (ga > 0.01f) ? (-10. * log10f(ga)) : 99.f
               );
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 240, 234, str);

    sprintf(str,"Q:%.1f",Q);
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 420, 234, str);

 /*   charnum=sprintf(str,"F: %.3f  Z: %.1f%+.1fj  SWR: %.1f  MCL: %.2f dB  Q: %.1f",
               fcur,
               r,
               x,
               vswr,
               (ga > 0.01f) ? (-10. * log10f(ga)) : 99.f, // Matched cable loss
               Q);

               if(charnum>72)
                   while(1);
*/

   /*   FONT_Print(FONT_FRAN, TextColor, BackGrColor, 0, Y0 + WHEIGHT + 16, "F: %.3f  Z: %.1f%+.1fj  SWR: %.1f  MCL: %.2f dB  Q: %.1f",
               fcur,
               crealf(rx),
               cimagf(rx),
               DSP_CalcVSWR(rx),
               (ga > 0.01f) ? (-10. * log10f(ga)) : 99.f, // Matched cable loss
               Q
              );*/

  //  LCD_HLine(LCD_MakePoint(0,249), 62, CurvColor);
  //  LCD_HLine(LCD_MakePoint(70,249), 70, CurvColor);
  //  LCD_HLine(LCD_MakePoint(410,249), 69, CurvColor);
}
static void DrawCursorTextWithS11()
{
    float complex rx = values[cursorPos]; //SmoothRX(cursorPos, f1 > (CFG_GetParam(CFG_PARAM_BAND_FMAX) / 1000) ? 1 : 0);
   // float ga = cabsf(OSL_GFromZ(rx, (float)CFG_GetParam(CFG_PARAM_R0))); //G magnitude

    uint32_t fstart;
    if (CFG_GetParam(CFG_PARAM_PAN_CENTER_F) == 0)
        fstart = f1;
    else
        fstart = f1 - 500*BSVALUES[span];// / 2;

    fcur = ((float)(fstart/1000. + (float)cursorPos * BSVALUES[span] / WWIDTH));///1000.;
    if (fcur * 1000.f > (float)(CFG_GetParam(CFG_PARAM_BAND_FMAX) + 1))
        fcur = 0.f;
    LCD_FillRect(LCD_MakePoint(0, 234),LCD_MakePoint(478, 248),BackGrColor);
    DrawFootText();
    FONT_Print(FONT_FRAN, TextColor, BackGrColor ,0 , 234, "F: %.3f   Z: %.1f%+.1fj",
                fcur,
                crealf(rx),
                cimagf(rx)
                );

    FONT_Print(FONT_FRAN, TextColor, BackGrColor ,240 , 234, "SWR: %.1f",
                DSP_CalcVSWR(rx)
                );

    FONT_Print(FONT_FRAN, TextColor, BackGrColor ,380 , 234, "MCL: %.2f dB",
                S11Calc(DSP_CalcVSWR(rx))
                );
 /*
    FONT_Print(FONT_FRAN, TextColor, BackGrColor,0,234, "F: %.3f  Z: %.1f%+.1fj  SWR: %.1f  S11: %.2f dB",
               fcur,
               crealf(rx),
               cimagf(rx),
               DSP_CalcVSWR(rx),
               S11Calc(DSP_CalcVSWR(rx))
              );
 */

    LCD_HLine(LCD_MakePoint(0,249), 62, CurvColor);
    LCD_HLine(LCD_MakePoint(70,249), 70, CurvColor);
    LCD_HLine(LCD_MakePoint(410,249), 69, CurvColor);
}

static void DrawCursorTextWithS111()
{
    float complex rx = values[cursorPos]; //SmoothRX(cursorPos, f1 > (CFG_GetParam(CFG_PARAM_BAND_FMAX) / 1000) ? 1 : 0);
   // float ga = cabsf(OSL_GFromZ(rx, (float)CFG_GetParam(CFG_PARAM_R0))); //G magnitude
    uint32_t fstart;
    if (CFG_GetParam(CFG_PARAM_PAN_CENTER_F) == 0)
        fstart = f1;
    else
        fstart = f1 - 500*BSVALUES[span];// / 2;

    fcur = ((float)(fstart/1000. + (float)cursorPos * BSVALUES[span] / WWIDTH));///1000.;
    if (fcur * 1000.f > (float)(CFG_GetParam(CFG_PARAM_BAND_FMAX) + 1))
        fcur = 0.f;
    //LCD_FillRect(LCD_MakePoint(0, Y0 + WHEIGHT + 16),LCD_MakePoint(479 , Y0 + WHEIGHT + 30),BackGrColor);
    LCD_FillRect(LCD_MakePoint(0, 234),LCD_MakePoint(478 , 249),BackGrColor);

    sprintf(str,"F: %.3f  Z: %.1f%+.1fj",
               fcur,
               crealf(rx),
               cimagf(rx));
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 0, 234, str);


    sprintf(str,"SWR: %.1f  S11: %.2f dB",
               DSP_CalcVSWR(rx),
               S11Calc(DSP_CalcVSWR(rx)));
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 240, 234, str);

    /*
    sprintf(str,"F: %.3f Z: %.1f%+.1fj  SWR: %.1f  S11: %.2f dB",
               fcur,
               crealf(rx),
               cimagf(rx),
               DSP_CalcVSWR(rx),
               S11Calc(DSP_CalcVSWR(rx))
              );
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 0, 234, str);
    /*
    FONT_Print(FONT_FRAN, TextColor, BackGrColor, 0, Y0 + WHEIGHT + 16, "F: %.3f Z: %.1f%+.1fj  SWR: %.1f  S11: %.2f dB",
               fcur,
               crealf(rx),
               cimagf(rx),
               DSP_CalcVSWR(rx),
               S11Calc(DSP_CalcVSWR(rx))
              );
              */
   // LCD_HLine(LCD_MakePoint(0,249), 62, CurvColor);
   // LCD_HLine(LCD_MakePoint(70,249), 70, CurvColor);
   // LCD_HLine(LCD_MakePoint(410,249), 69, CurvColor);
}

static void DrawAutoText(void)
{
    static const char* atxt = " Auto (fast, 1/8 pts)  ";
    if (0 == autofast)
        FONT_Print(FONT_FRAN, TextColor, BackGrColor, 260, Y0 + WHEIGHT + 16 + 16,  atxt);
    else
        FONT_Print(FONT_FRAN, TextColor, LCD_MakeRGB(0, 128, 0), 260, Y0 + WHEIGHT + 16 + 16,  atxt);
}

static void DrawBottomText(void)
{
    static const char* txt = " Save snapshot ";
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 165,
               Y0 + WHEIGHT + 16 + 16, txt);
    //DrawFootText();
}

static void DrawSavingText(void)
{
    static const char* txt = "  Saving snapshot...  ";
    FONT_Write(FONT_FRAN, LCD_WHITE, LCD_BLUE, 165,
               Y0 + WHEIGHT + 16 + 16, txt);
    Sleep(20);
}

static void DrawSavedText(void)
{
    static const char* txt = "  Snapshot saved  ";
    FONT_Write(FONT_FRAN, LCD_WHITE, LCD_RGB(0, 60, 0), 165,
               Y0 + WHEIGHT + 16 + 16, txt);
    DrawFootText();
    DrawAutoText();
}

static void DecrCursor()
{
    if (!isMeasured)
        return;
    if (cursorPos == 0)
        return;
    DrawCursor();
    cursorPos--;
    DrawCursor();
    if ((grType == GRAPH_S11) && (CFG_GetParam(CFG_PARAM_S11_SHOW) == 1))
    {
        DrawCursorTextWithS11();
    }


    else
    {
        DrawCursorText();
    }
    if (cursorChangeCount++ < 10)
        Sleep(100); //Slow down at first steps
    Sleep(5);
}

static void IncrCursor()
{
    if (!isMeasured)
        return;
    if (cursorPos == WWIDTH)
        return;
    DrawCursor();
    cursorPos++;
    DrawCursor();
    if ((grType == GRAPH_S11) && (CFG_GetParam(CFG_PARAM_S11_SHOW) == 1))
    {
        DrawCursorTextWithS11();
    }

    else
    {
        DrawCursorText();
    }
    if (cursorChangeCount++ < 10)
        Sleep(100); //Slow down at first steps
    Sleep(5);
}

static void DrawGrid(GRAPHTYPE grType)  //
{
    int i;
    LCD_FillAll(BackGrColor);

    FONT_Write(FONT_FRAN, LCD_BLACK, LCD_PURPLE, X0+1, 0, modstr);
    FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 2, 110, "<");
    FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 460, 110, ">");
    uint32_t fstart;
    uint32_t pos = modstrw + 8+ X0;//WK
    if (grType == GRAPH_RX)// R/X
    {
        //  Print colored R/X
        FONT_Write(FONT_FRAN, CurvColor, BackGrColor, pos, 0, " R");
        pos += FONT_GetStrPixelWidth(FONT_FRAN, " R") + 1;
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, pos, 0, "/");
        pos += FONT_GetStrPixelWidth(FONT_FRAN, "/") + 1;
        FONT_Write(FONT_FRAN, LCD_BLACK, LCD_RED, pos, 0, "X");
        pos += FONT_GetStrPixelWidth(FONT_FRAN, "X") + 1;
    }

    if (grType == GRAPH_S11)
    {
        //  Print colored S11
        FONT_Write(FONT_FRAN, CurvColor, BackGrColor, pos, 0, " S11");
        pos += FONT_GetStrPixelWidth(FONT_FRAN, "S11") + 6;
    }

    if (0 == CFG_GetParam(CFG_PARAM_PAN_CENTER_F))  {
        fstart = f1;

        if (grType == GRAPH_VSWR)
            sprintf(buf, "VSWR graph: %.3f MHz + %s   (Z0 = %d)", (float)f1/1000000, BSSTR[span], CFG_GetParam(CFG_PARAM_R0));
        else if (grType == 3)
            sprintf(buf, " VSWR/|X| graph: %.3f MHz +%s (Z0 = %d)", (float)f1/1000000, BSSTR[span], CFG_GetParam(CFG_PARAM_R0));
        else
            sprintf(buf, " graph: %.3f MHz +%s", (float)f1/1000000, BSSTR[span]);
    }

    else     {
        fstart = f1 - 500*BSVALUES[span];

        if (grType == GRAPH_VSWR)
            sprintf(buf, " VSWR graph: %.3f MHz +/- %s (Z0 = %d)", (float)f1/1000000, BSSTR_HALF[span], CFG_GetParam(CFG_PARAM_R0));
        else if (grType == 3)
            sprintf(buf, " VSWR/|X| graph: %.3f MHz +/- %s (Z0 = %d)", (float)f1/1000000, BSSTR_HALF[span], CFG_GetParam(CFG_PARAM_R0));
        else
            sprintf(buf, " graph: %.3f MHz +/- %s", (float)f1/1000000, BSSTR_HALF[span]);
    }

    FONT_Write(FONT_FRAN, TextColor, BackGrColor, pos, 0, buf);//LCD_BLUE

    //Mark ham bands with colored background
    for (i = 0; i <= WWIDTH; i++)
    {
        uint32_t f = fstart/1000 + (i * BSVALUES[span]) / WWIDTH;
        if (IsFinHamBands(f))
        {
            LCD_VLine(LCD_MakePoint(X0 + i, Y0), WHEIGHT, Color3);// (0, 0, 64) darkblue << >> yellow
        }
    }

    //Draw F grid and labels
    int lmod = 5;
    int linediv = 10; //Draw vertical line every linediv pixels

    for (i = 0; i <= WWIDTH/linediv; i++)
    {
        int x = X0 + i * linediv;
        if ((i % lmod) == 0 || i == WWIDTH/linediv)
        {
            char fr[10];
            float flabel = ((float)(fstart/1000. + i * BSVALUES[span] / (WWIDTH/linediv)))/1000.f;
            if (flabel * 1000000.f > (float)(CFG_GetParam(CFG_PARAM_BAND_FMAX)+1))
                continue;
            if(flabel>999.99)
                sprintf(fr, "%.1f", ((float)(fstart/1000. + i * BSVALUES[span] / (WWIDTH/linediv)))/1000.f);
            else if(flabel>99.99)
                sprintf(fr, "%.2f", ((float)(fstart/1000. + i * BSVALUES[span] / (WWIDTH/linediv)))/1000.f);
            else
                sprintf(fr, "%.3f", ((float)(fstart/1000. + i * BSVALUES[span] / (WWIDTH/linediv)))/1000.f);// WK
            int w = FONT_GetStrPixelWidth(FONT_SDIGITS, fr);
           // FONT_Write(FONT_SDIGITS, LCD_WHITE, LCD_BLACK, x - w / 2, Y0 + WHEIGHT + 5, f);// WK
            FONT_Write(FONT_FRAN, TextColor, BackGrColor, x -8 - w / 2, Y0 + WHEIGHT +3, fr);
            LCD_VLine(LCD_MakePoint(x, Y0), WHEIGHT, WGRIDCOLOR);
            LCD_VLine(LCD_MakePoint(x+1, Y0), WHEIGHT, WGRIDCOLOR);// WK
        }
        else
        {
            LCD_VLine(LCD_MakePoint(x, Y0), WHEIGHT, WGRIDCOLOR);
        }
    }

    if ((grType == GRAPH_VSWR)||(grType == GRAPH_VSWR_Z)||(grType == GRAPH_VSWR_RX))
    {
        if(loglog==0){
            //Draw SWR grid and labels
            static const float swrs[]  = { 1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10., 13., 16., 20.};
            static const char labels[] = { 1,  0,   0,   0,    0,   1,  1,   0,  1,  1,  1,  0,  1,  0,  0,   1,   1,   1,   1 };
            static const int nswrs = sizeof(swrs) / sizeof(float);
            for (i = 0; i < nswrs; i++)
            {
                int yofs = swroffset(swrs[i]);
                if (labels[i])
                {
                    char s[10];
                    if((int)(10*swrs[i])%10==0){// WK
                       if(swrs[i]>9.0)
                        sprintf(s, "%d", (int)swrs[i]);
                       else
                        sprintf(s, " % d", (int)swrs[i]);
                    }
                    else
                        sprintf(s, "%.1f", swrs[i]);
                   // FONT_Write(FONT_SDIGITS, LCD_WHITE, LCD_BLACK, X0 - 15, WY(yofs) - 2, s);
                    FONT_Write(FONT_FRAN, CurvColor, BackGrColor, X0 - 21, WY(yofs) - 12, s);
                }
                LCD_HLine(LCD_MakePoint(X0, WY(yofs)), WWIDTH, WGRIDCOLOR);
            }
        }
        else{
            static const float swrsl[]  = { 1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10., 13., 16., 20.};
            static const char labelsl[] = { 1,  1,   1,   0,    0,   1,  1,   0,  1,  0,  1,  0,  0,  0,  0,   1,   0,   0,   1 };
            static const int nswrsl = sizeof(swrsl) / sizeof(float);
            for (i = 0; i < nswrsl; i++)
            {
                int yofs = swroffset(14*log10f(swrsl[i])+1);
                if (labelsl[i])
                {
                    char s[10];
                    if((int)(10*swrsl[i])%10==0){// WK
                       if(swrsl[i]>9.0)
                        sprintf(s, "%d", (int)swrsl[i]);
                       else
                        sprintf(s, " % d", (int)swrsl[i]);
                    }
                    else
                        sprintf(s, "%.1f", swrsl[i]);
                   // FONT_Write(FONT_SDIGITS, LCD_WHITE, LCD_BLACK, X0 - 15, WY(yofs) - 2, s);
                    FONT_Write(FONT_FRAN, CurvColor, BackGrColor, X0 - 21, WY(yofs) - 12, s);
                }
                LCD_HLine(LCD_MakePoint(X0, WY(yofs)), WWIDTH, WGRIDCOLOR);
            }
        }
        LCD_FillRect((LCDPoint){0 ,155},(LCDPoint){X0 -22,215},BackGrColor);

        LCD_Rectangle((LCDPoint){0 ,155},(LCDPoint){X0 -22,215},CurvColor);
        FONT_Write(FONT_FRAN, CurvColor, BackGrColor, 4, 160, "Log");
        if(loglog==1)
            FONT_Write(FONT_FRAN, CurvColor, BackGrColor, 4, 190, "Log");
    }
    LCD_FillRect((LCDPoint){X0 ,Y0+WHEIGHT+1},(LCDPoint){X0 + WWIDTH+2,Y0+WHEIGHT+3},BackGrColor);
}

static void ScanRXFast(void)
{
    uint64_t i;
    uint32_t fstart;
    if (CFG_GetParam(CFG_PARAM_PAN_CENTER_F) == 0)
        fstart = f1;
    else
        fstart = f1 - 500*BSVALUES[span];
   // fstart *= 1000; //Convert to Hz

    DSP_Measure(fstart, 1, 1, 1); //Fake initial run to let the circuit stabilize

    for(i = 0; i <= WWIDTH; i+=8)
    {
        uint32_t freq;
        freq = fstart + (i * BSVALUES[span] * 1000) / WWIDTH;
        if (freq == 0) //To overcome special case in DSP_Measure, where 0 is valid value
            freq = 1;
        DSP_Measure(freq, 1, 1, CFG_GetParam(CFG_PARAM_PAN_NSCANS));
        float complex rx = DSP_MeasuredZ();
        if (isnan(crealf(rx)) || isinf(crealf(rx)))
            rx = 0.0f + cimagf(rx) * I;
        if (isnan(cimagf(rx)) || isinf(cimagf(rx)))
            rx = crealf(rx) + 0.0fi;
        values[i] = rx;
        LCDPoint pt;
        if ((0 == (i % 32)) && TOUCH_Poll(&pt))
            break;
    }
    GEN_SetMeasurementFreq(0);
    isMeasured = 1;

    //Interpolate intermediate values
    for(i = 0; i <= WWIDTH; i++)
    {
        uint32_t fr = i % 8;
        if (0 == fr)
            continue;
        int fi0, fi1, fi2;
        if (i < 8)
        {
            fi0 = i - fr;
            fi1 = i + 8 - fr;
            fi2 = i + 16 - fr;
        }
        else
        {
            fi0 = i - 8 - fr;
            fi1 = i - fr;
            fi2 = i + (8 - fr);
        }
        float complex G0 = OSL_GFromZ(values[fi0], 50.f);
        float complex G1 = OSL_GFromZ(values[fi1], 50.f);
        float complex G2 = OSL_GFromZ(values[fi2], 50.f);
        float complex Gi = OSL_ParabolicInterpolation(G0, G1, G2, (float)fi0, (float)fi1, (float)fi2, (float)i);
        values[i] = OSL_ZFromG(Gi, 50.f);
    }
}

static uint32_t Fs, Fp,Fq1,Fq2;// in Hz
static float phi1,phi,Cx,Cp,Lp,Rs,Cs,Ls,Ls1,Ls2,Q,XL1,XL2;
//////////////

static void ScanRX_ZI(void)
{
    uint64_t i;
    uint32_t fstart;
    if (CFG_GetParam(CFG_PARAM_PAN_CENTER_F) == 0)
        fstart = f1;
    else
        fstart = f1 - 500*BSVALUES[span];
   // fstart *= 1000; //Convert to Hz

    DSP_Measure(fstart, 1, 1, 1); //Fake initial run to let the circuit stabilize
    Sleep(20);
    for(i = 0; i <= WWIDTH; i++)
    {
        uint32_t freq;
        freq = fstart + (i * BSVALUES[span] * 1000) / WWIDTH;
        if (freq == 0) //To overcome special case in DSP_Measure, where 0 is valid value
            freq = 1;
        DSP_Measure(freq, 1, 1, CFG_GetParam(CFG_PARAM_PAN_NSCANS));
        float complex rx = DSP_MeasuredZ();
        if (isnan(crealf(rx)) || isinf(crealf(rx)))
            rx = 0.0f + cimagf(rx) * I;
        if (isnan(cimagf(rx)) || isinf(cimagf(rx)))
            rx = crealf(rx) + 0.0fi;
        values[i] = rx;
        LCD_SetPixel(LCD_MakePoint(X0 + i, 135), LCD_BLUE);// progress line
    }
    GEN_SetMeasurementFreq(0);
    isMeasured = 1;
}
static void ScanRX(int selector)
{
char str[100];
float complex rx, rx0;
float newX, oldX, MaxX, absX;
uint32_t i, k, sel, imax;
uint32_t fstart, freq1, deltaF;

    f1=CFG_GetParam(CFG_PARAM_PAN_F1);
    if (CFG_GetParam(CFG_PARAM_PAN_CENTER_F) == 0)
        fstart = f1;
    else
        fstart = f1 - 500*BSVALUES[span]; // 2;
    //fstart *= 1000; //Convert to Hz

    freq1=fstart-100000;
    if(freq1<100000)freq1=100000;
    DSP_Measure(freq1, 1, 1, 3); //Fake initial run to let the circuit stabilize
    rx0 = DSP_MeasuredZ();
    Sleep(20);

    deltaF=(BSVALUES[span] * 1000) / WWIDTH;
    MaxX=0;
    sel=0;
    k=CFG_GetParam(CFG_PARAM_PAN_NSCANS);
    for(i = 0; i <= WWIDTH; i++)
    {
        if(i%40==0){
            FONT_Write(FONT_FRAN, LCD_RED, LCD_BLACK, 450, 0, "TS");
            Sleep(50);
        }
        Sleep(10);
        freq1 = fstart + i * deltaF;
        if (freq1 == 0) //To overcome special case in DSP_Measure, where 0 is valid value
            freq1 = 1;
        DSP_Measure(freq1, 1, 1, k);
        rx = DSP_MeasuredZ();

        if (isnan(crealf(rx)) || isinf(crealf(rx))){
            if(i>0) rx = crealf(values[i-1]) + cimagf(rx) * I;
            else rx = 0.0f + cimagf(rx) * I;
        }
        if (isnan(cimagf(rx)) || isinf(cimagf(rx))){
            if(i>0) rx = crealf(rx) + cimagf(values[i-1]) * I;
            else rx = crealf(rx) + 99999.0f * I;
            if(sel==1){
                Fp=freq1;// first pole above Fs
                sel=3;
            }
        }
        else{
            newX=cimagf(rx);
            absX=fabsf(newX);
            if((sel==0)&&(newX>=0)&&(oldX<0)){// serial frequency of a quartz
                Fs=freq1;
                //1Rs=crealf(rx);
                sel=1;
            }
            else if ((sel==1)||(sel==2)){
                if(absX>MaxX){
                    MaxX=absX;
                    imax=i;
                    sel=2;// increasing X
                }
                else if(sel==2){
                    if((newX<MaxX)&&(i==imax+1)){// first decrease of X
                        Fp=freq1;
                        sel=3;
                    }
                }
            }
        }
        FONT_Write(FONT_FRAN, LCD_RED, LCD_BLACK, 450, 0, "dP");
        Sleep(10);
        values[i] = rx;
        oldX=newX;
        LCD_SetPixel(LCD_MakePoint(X0 + i, 135), LCD_BLUE);// progress line
        LCD_SetPixel(LCD_MakePoint(X0 + i, 136), LCD_BLUE);
    }
    FONT_Write(FONT_FRAN, LCD_RED, LCD_BLACK, 420, 0, "     ");
    GEN_SetMeasurementFreq(0);
    isMeasured = 1;
}


static void ScanRX_QuFast(void)
{
char str[100];
float complex rx, rx0;
float impedance, newX, oldX, MaxX, absX;
uint32_t i, k, sel, imax;
uint32_t fstart, freq1, deltaF;
/////////////////
    fstart = f1;
    freq1=fstart-100000;
    if(freq1<100000)freq1=100000;


    deltaF=(BSVALUES[span] * 1000) / WWIDTH;
    MaxX=0;
    sel=0;
    k=CFG_GetParam(CFG_PARAM_PAN_NSCANS);

    for(i = 0; i <= WWIDTH; i+=8)
    {
        uint32_t freq;
        fstart = f1;

        freq = fstart + (i * BSVALUES[span] * 1000) / WWIDTH;
        if (freq == 0) //To overcome special case in DSP_Measure, where 0 is valid value
            freq = 1;
        DSP_Measure(freq, 1, 1, CFG_GetParam(CFG_PARAM_PAN_NSCANS));
        float complex rx = DSP_MeasuredZ();

        if (isnan(crealf(rx)) || isinf(crealf(rx)))
            rx = 0.0f + cimagf(rx) * I;
        if (isnan(cimagf(rx)) || isinf(cimagf(rx)))
            rx = crealf(rx) + 0.0fi;

        values[i] = rx;
    }
    GEN_SetMeasurementFreq(0);
    isMeasured = 1;

    //Interpolate intermediate values
    for(i = 0; i <= WWIDTH; i++)
    {
        uint32_t fr = i % 8;
        if (0 == fr)
            continue;
        int fi0, fi1, fi2;
        if (i < 8)
        {
            fi0 = i - fr;
            fi1 = i + 8 - fr;
            fi2 = i + 16 - fr;
        }
        else
        {
            fi0 = i - 8 - fr;
            fi1 = i - fr;
            fi2 = i + (8 - fr);
        }
        float complex G0 = OSL_GFromZ(values[fi0], 50.f);
        float complex G1 = OSL_GFromZ(values[fi1], 50.f);
        float complex G2 = OSL_GFromZ(values[fi2], 50.f);
        float complex Gi = OSL_ParabolicInterpolation(G0, G1, G2, (float)fi0, (float)fi1, (float)fi2, (float)i);
        values[i] = OSL_ZFromG(Gi, 50.f);
    }
//
    for(i = 0; i <= WWIDTH; i++)
    {
        rx = values[i];
        freq1 = fstart + (i * BSVALUES[span] * 1000) / WWIDTH;

        if (isnan(crealf(rx)) || isinf(crealf(rx)))
            {
            if(i>0) rx = crealf(values[i-1]) + cimagf(rx) * I;
            else rx = 0.0f + cimagf(rx) * I;
            }
        if (isnan(cimagf(rx)) || isinf(cimagf(rx)))
            {
            if(i>0) rx = crealf(rx) + cimagf(values[i-1]) * I;
            else rx = crealf(rx) + 99999.0f * I;
            }
            impedance = cimagf(rx)*cimagf(rx)+crealf(rx)*crealf(rx);
            impedance=sqrtf(impedance);
            newX=cimagf(rx);
            //absX=fabsf(newX);
            if((sel==1)&&(newX<=0)&&(oldX>0)&&(impedance>500))
            {// parallel frequency of a quartz
                Fp=freq1;
                //sel=0;
            }

            if((sel==0)&&(newX>=0)&&(oldX<0))
            {// serial frequency of a quartz
                Fs=freq1;
                //2Rs=crealf(rx);
                sel=1;
                //i+=5;
            }

        oldX=newX;
        LCD_SetPixel(LCD_MakePoint(X0 + i, 135), LCD_BLUE);// progress line
    }
    GEN_SetMeasurementFreq(0);
    isMeasured = 1;
}

//Calculates average R and X of SMOOTHWINDOW measurements around frequency
//In the beginning and the end of measurement data missing measurements are replaced
//with first and last measurement respectively.
static float complex SmoothRX(int idx, int useHighSmooth)
{
    int i;
    float complex sample;
    float resr = 0.0f;
    float resx = 0.0f;
    int smoothofs;
    int smoothwindow;
    if (useHighSmooth)
    {
        smoothofs = SMOOTHOFS_HI;
        smoothwindow = SMOOTHWINDOW_HI;
    }
    else
    {
        smoothofs = SMOOTHOFS;
        smoothwindow = SMOOTHWINDOW;
    }
    for (i = -smoothofs; i <= smoothofs; i++)
    {
        if ((idx + i) < 0)
            sample = values[0];
        else if ((idx + i) >= (WWIDTH - 1))
            sample = values[WWIDTH - 1];
        else
            sample  = values[idx + i];
        resr += crealf(sample);
        resx += cimagf(sample);
    }
    resr /= smoothwindow;
    resx /= smoothwindow;
    return resr + resx * I;
}
static uint32_t MinSWR;
static uint32_t MinIndex;

static void DrawVSWR1(void)
{
    if (!isMeasured)
        return;
    MinSWR=0;
    MinIndex=9999;
    float MaxZ, MinZ, factorA, factorB;
    int lastoffset = 0;
    int lastoffset_sm = 0;
    int i, x;
    float swr_float, swr_float_sm;
    int offset_log, offset_log_sm;
    int offset;
    int offset_sm;
    for(i = 0; i <= WWIDTH; i++)
    {
        swr_float=DSP_CalcVSWR(values[i]);
        swr_float_sm = DSP_CalcVSWR(SmoothRX(i,  f1 > (CFG_GetParam(CFG_PARAM_BAND_FMAX) ) ? 1 : 0));
        offset_log=14*log10f(swr_float)+1;
        if(loglog==1){
            offset=swroffset(offset_log);
            offset_sm=swroffset(14*log10f(swr_float_sm)+1);
        }
        else{
            offset=swroffset(swr_float);
            offset_sm=swroffset(swr_float_sm);
        }
        int x = X0 + i;
        if(WY(offset_sm)>MinSWR) {//offset
            MinSWR=WY(offset_sm);
            MinIndex=i;
        }
        if(i == 0)
        {
            LCD_SetPixel(LCD_MakePoint(x, WY(offset_sm)), CurvColor);
        }
        else
        {
            LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset_sm)), LCD_MakePoint(x, WY(offset_sm)), CurvColor);
            if(FatLines){
                LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset_sm)-1), LCD_MakePoint(x, WY(offset_sm)-1), CurvColor);
                LCD_Line(LCD_MakePoint(x - 2, WY(lastoffset_sm)-1), LCD_MakePoint(x-1, WY(offset_sm)-1), CurvColor);
                LCD_Line(LCD_MakePoint(x , WY(lastoffset_sm)-1), LCD_MakePoint(x+1, WY(offset_sm)+1), CurvColor);
            }
        }
        lastoffset = offset;
        lastoffset_sm = offset_sm;
    }
    FONT_Write(FONT_FRANBIG, CurvColor, BackGrColor, X0 -46, Y0+ 0, "S");
    FONT_Write(FONT_FRANBIG, CurvColor, BackGrColor, X0 -50, Y0+30, "W");
    FONT_Write(FONT_FRANBIG, CurvColor, BackGrColor, X0 -46, Y0+60, "R");
    cursorPos=MinIndex;
    DrawCursor();
    if(grType==GRAPH_VSWR_Z){
        float impedance;
        int yofs, yofs_sm;
        lastoffset = 0;
        lastoffset_sm = 0;
        MaxZ=0;
        MinZ=999999.;
        for(i = 0; i <= WWIDTH; i++)
        {
            impedance = cimagf(values[i])*cimagf(values[i])+crealf(values[i])*crealf(values[i]);
            impedance=sqrtf(impedance);
            if (impedance < MinZ)
                MinZ=impedance ;
            if (impedance > MaxZ)
                MaxZ=impedance ;
        }
        if(MinZ<2) MinZ=2;
        if(MaxZ/MinZ<2) {
            MaxZ=1.5f*MinZ;
            MinZ=0.5f*MinZ;
        }
        factorA=200./(MaxZ-MinZ);
        factorB=-200.*MinZ/(MaxZ-MinZ);
        for(i = 0; i <= WWIDTH; i++)
        {
            impedance = cimagf(values[i])*cimagf(values[i])+crealf(values[i])*crealf(values[i]);
            impedance=sqrtf(impedance);
            yofs=factorA*impedance+factorB;
            x = X0 + i;
            if(i == 0)
            {
                LCD_SetPixel(LCD_MakePoint(x, WY(yofs)), LCD_RED);
                LCD_SetPixel(LCD_MakePoint(x, WY(yofs)+1), LCD_RED);
            }
            else
            {
                LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset)), LCD_MakePoint(x, WY(yofs)), LCD_RED);
                if(FatLines){
                    LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset)+1), LCD_MakePoint(x, WY(yofs)+1), LCD_RED);
                    LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset)+2), LCD_MakePoint(x, WY(yofs)+2), LCD_RED);
                }

            }
            lastoffset = yofs;
            lastoffset_sm = yofs_sm;
        }
    DrawX_Scale(MaxZ,  MinZ);
    FONT_Write(FONT_FRANBIG, LCD_RED, BackGrColor, X0 +405, Y0+40, "|Z|");
    }
    else if (grType==GRAPH_VSWR_RX){
        DrawRX(0,1);
    }
}
static void DrawVSWR(void)
{  float last_x,min_swr=9999;
    if (!isMeasured)
        return;
    MinSWR=0;
    MinIndex=9999;
    float MaxZ, MinZ, factorA, factorB;
    int lastoffset = 0;
    int lastoffset_sm = 0;
    int lastoffset_q = 0;

    int i, x;
    float swr_float, swr_float_sm;
    int offset_log, offset_log_sm;
    int offset;
    int offset_sm;
    int offset_q;
    for(i = 0; i <= WWIDTH; i++)
    {
        swr_float=DSP_CalcVSWR(values[i]);
        swr_float_sm = DSP_CalcVSWR(SmoothRX(i,  f1 > (CFG_GetParam(CFG_PARAM_BAND_FMAX) ) ? 1 : 0));
        offset_log=14*log10f(swr_float)+1;
        if(loglog==1){
            offset=swroffset(offset_log);
            offset_sm=swroffset(14*log10f(swr_float_sm)+1);
        }
        else{
            offset=swroffset(swr_float);
            offset_sm=swroffset(swr_float_sm);
        }
        int x = X0 + i;
        /*
        if(WY(offset_sm)>MinSWR) {//offset
            MinSWR=WY(offset_sm);
            MinIndex=i;
        }
        */
        //////////////////    LZ5ZI
        if(grType==GRAPH_VSWR_RX && ((last_x<=0.0 && cimagf(values[i])>=0.0)||cimagf(values[i])==0.0))
            MinIndex=i;
        else
            if(DSP_CalcVSWR(values[i])<min_swr)
            {//offset
                min_swr=DSP_CalcVSWR(values[i]);
                MinIndex=i;
            }
        last_x=cimagf(values[i]);
        //////////////////    LZ5ZI
        if(i == 0)
        {
            LCD_SetPixel(LCD_MakePoint(x, WY(offset_sm)), CurvColor);
        }
        else
        {
            LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset_sm)), LCD_MakePoint(x, WY(offset_sm)), CurvColor);
            if(FatLines){
                LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset_sm)-1), LCD_MakePoint(x, WY(offset_sm)-1), CurvColor);
                LCD_Line(LCD_MakePoint(x - 2, WY(lastoffset_sm)-1), LCD_MakePoint(x-1, WY(offset_sm)-1), CurvColor);
                LCD_Line(LCD_MakePoint(x , WY(lastoffset_sm)-1), LCD_MakePoint(x+1, WY(offset_sm)+1), CurvColor);
            }
        }
        lastoffset = offset;
        lastoffset_sm = offset_sm;
    }
    FONT_Write(FONT_FRANBIG, CurvColor, BackGrColor, X0 -46, Y0+ 0, "S");
    FONT_Write(FONT_FRANBIG, CurvColor, BackGrColor, X0 -50, Y0+30, "W");
    FONT_Write(FONT_FRANBIG, CurvColor, BackGrColor, X0 -46, Y0+60, "R");
    cursorPos=MinIndex;
    DrawCursor();
    if(grType==GRAPH_VSWR_Z){
        float R,X,Q,impedance;
        int yofs,yofs_q, yofs_sm;
        lastoffset = 0;
        lastoffset_sm = 0;
        lastoffset_q = 0;
        MaxZ=0;
        MinZ=999999.;
        for(i = 0; i <= WWIDTH; i++)
        {

            impedance = cimagf(values[i])*cimagf(values[i])+crealf(values[i])*crealf(values[i]);
            impedance=sqrtf(impedance);
            if (impedance < MinZ)
                MinZ=impedance ;
            if (impedance > MaxZ)
                MaxZ=impedance ;
        }
        if(MinZ<2) MinZ=2;
        if(MaxZ/MinZ<2) {
            MaxZ=1.5f*MinZ;
            MinZ=0.5f*MinZ;
        }
        factorA=200./(MaxZ-MinZ);
        factorB=-200.*MinZ/(MaxZ-MinZ);
        for(i = 0; i <= WWIDTH; i++)
        {
            R = crealf(values[i]);
            X = cimagf(values[i]);
            if(R!=0.0)
                Q=X/R;
            if(Q<0.0)
                Q=-Q;
            impedance = cimagf(values[i])*cimagf(values[i])+crealf(values[i])*crealf(values[i]);
            impedance=sqrtf(impedance);
            yofs=factorA*impedance+factorB;
            yofs_q=5*factorA*Q+factorB;

            x = X0 + i;
            if(i == 0)
            {
                LCD_SetPixel(LCD_MakePoint(x, WY(yofs)), LCD_RED);
                LCD_SetPixel(LCD_MakePoint(x, WY(yofs)+1), LCD_RED);

                LCD_SetPixel(LCD_MakePoint(x, WY(yofs_q)), LCD_COLOR_WHITE);    //Q graph
                LCD_SetPixel(LCD_MakePoint(x, WY(yofs_q)+1), LCD_COLOR_WHITE);
            }
            else
            {
                LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset)), LCD_MakePoint(x, WY(yofs)), LCD_RED);
                LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset_q)), LCD_MakePoint(x, WY(yofs_q)), LCD_COLOR_WHITE);
                if(FatLines){
                    LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset)+1), LCD_MakePoint(x, WY(yofs)+1), LCD_RED);
                    LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset)+2), LCD_MakePoint(x, WY(yofs)+2), LCD_RED);
                    LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset_q)+1), LCD_MakePoint(x, WY(yofs_q)+1), LCD_COLOR_WHITE);
                    LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset_q)+2), LCD_MakePoint(x, WY(yofs_q)+2), LCD_COLOR_WHITE);
                }

            }
            lastoffset = yofs;
            lastoffset_q = yofs_q;
            lastoffset_sm = yofs_sm;
        }
    DrawX_Scale(MaxZ,  MinZ);
    FONT_Write(FONT_FRANBIG, LCD_RED, BackGrColor, X0 +405, Y0+10, "|Z|");
    FONT_Write(FONT_FRANBIG, LCD_COLOR_WHITE, BackGrColor, X0 +405, Y0+60, "Q");
    }
    else if (grType==GRAPH_VSWR_RX){
        DrawRX(0,1);
    }
}


static void LoadBkups()
{
    //Load saved frequency and span values from config file
    uint32_t fbkup = CFG_GetParam(CFG_PARAM_PAN_F1);
    if (fbkup != 0 && fbkup >= BAND_FMIN && fbkup <= CFG_GetParam(CFG_PARAM_BAND_FMAX) && (fbkup % 100) == 0)
    {
        f1 = fbkup;
    }
    else
    {
        f1 = 14000000;
        CFG_SetParam(CFG_PARAM_PAN_F1, f1);
        CFG_SetParam(CFG_PARAM_PAN_SPAN, BS400);
        CFG_Flush();
    }

    int spbkup = CFG_GetParam(CFG_PARAM_PAN_SPAN);
    if (spbkup <= BS100M)
    {
        span = (BANDSPAN)spbkup;
    }
    else
    {
        span = BS400;
        CFG_SetParam(CFG_PARAM_PAN_SPAN, span);
        CFG_Flush();
    }
}

static void DrawHelp(void)
{
    FONT_Write(FONT_FRAN, LCD_PURPLE, LCD_BLACK, 160,  20, "(Tap here to set F and Span)");
    FONT_Write(FONT_FRAN, LCD_PURPLE, LCD_BLACK, 160, 110, "(Tap here change graph type)");
}

/*
   This function is based on:
   "Nice Numbers for Graph Labels" article by Paul Heckbert
   from "Graphics Gems", Academic Press, 1990
   nicenum: find a "nice" number approximately equal to x.
   Round the number if round=1, take ceiling if round=0
 */
static float nicenum(float x, int round)
{
    int expv;   /* exponent of x */
    float f;    /* fractional part of x */
    float nf;   /* nice, rounded fraction */

    expv = floorf(log10f(x));
    f = x / powf(10., expv);    /* between 1 and 10 */
    if (round)
    {
        if (f < 1.5)
            nf = 1.;
        else if (f < 3.)
            nf = 2.;
        else if (f < 7.)
            nf = 5.;
        else
            nf = 10.;
    }
    else
    {
        if (f <= 1.)
            nf = 1.;
        else if (f <= 2.)
            nf = 2.;
        else if (f <= 5.)
            nf = 5.;
        else
            nf = 10.;
    }
    return nf * powf(10., expv);
}

static void DrawS11()
{
    int i;
    int j;
    if (!isMeasured)
        return;
    //Find min value among scanned S11 to set up scale
    float minS11 = 0.f;
    for (i = 0; i <= WWIDTH; i++)
    {
        if (S11Calc(DSP_CalcVSWR(values[i])) < minS11)
            minS11 = S11Calc(DSP_CalcVSWR(values[i]));
    }

    if (minS11 < -60.f)
        minS11 = -60.f;

    int nticks = 14; //Max number of intermediate ticks of labels
    float range = nicenum(-minS11, 0);
    float d = nicenum(range / (nticks - 1), 1);
    float graphmin = floorf(minS11 / d) * d;
    float graphmax = 0.f;
    float grange = graphmax - graphmin;
    float nfrac = MAX(-floorf(log10f(d)), 0);  // # of fractional digits to show
    char str[100];
    if (nfrac > 4) nfrac = 4;
    sprintf(str, "%%.%df", (int)nfrac);             // simplest axis labels

    //Draw horizontal lines and labels
    int yofs = 0;
    int yofs_sm = 0;
    float labelValue;

#define S11OFFS(s11) ((int)roundf(((s11 - graphmin) * WHEIGHT) / grange) + 1)

    for (labelValue = graphmin; labelValue < graphmax + (.5 * d); labelValue += d)
    {
        sprintf(buf, str, labelValue); //Get label string in buf
        yofs = S11OFFS(labelValue);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor,  X0 - 30, WY(yofs) - 12, buf);// FONT_SDIGITS WK
        if (roundf(labelValue) == 0)
            LCD_HLine(LCD_MakePoint(X0, WY(S11OFFS(0.f))), WWIDTH, WGRIDCOLOR);
        else
            LCD_HLine(LCD_MakePoint(X0, WY(yofs)), WWIDTH, WGRIDCOLOR);

    }
    FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, X0 +410, Y0,    "S ");
    FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, X0 +410, Y0+26, "1 ");
    FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, X0 +410, Y0+52, "1 ");
    uint16_t lasty = 0;
    int MaxJ, maxY=0;
    for(j = 0; j <= WWIDTH; j++)
    {
        int offset = roundf((WHEIGHT / (-graphmin)) * S11Calc(DSP_CalcVSWR(values[j])));

        uint16_t y = WY(offset + WHEIGHT);
        if (y > (WHEIGHT + Y0))
            y = WHEIGHT + Y0;
        int x = X0 + j;
        if(maxY<y){
            maxY=y;
            MaxJ=j;
        }
        if(j == 0)
        {
            LCD_SetPixel(LCD_MakePoint(x, y), CurvColor);
        }
        else
        {
            if(FatLines){
                LCD_Line(LCD_MakePoint(x - 1, lasty), LCD_MakePoint(x, y), CurvColor);// LCD_GREEN WK
                LCD_Line(LCD_MakePoint(x - 1, lasty+1), LCD_MakePoint(x, y+1), CurvColor);
            }
            LCD_Line(LCD_MakePoint(x , lasty), LCD_MakePoint(x+1, y), CurvColor);
        }
        lasty = y;
    }
    cursorPos=MaxJ;
    DrawCursor();
}


void DrawX_Scale(float MaxZ, float MinZ){
float labelValue, d, factorA, factorB;
int yofs;
char str[100];
int nticks = 7; //Max number of intermediate ticks of labels 8
float range_i = nicenum(MaxZ - MinZ, 0);
    d = nicenum(range_i / (nticks - 1), 1);
    float graphmin_i = floorf(MinZ / d) * d;
    float graphmax_i = MaxZ*0.95;//ceilf(MaxZ / d) * d;
    float grange_i = graphmax_i - graphmin_i;
    float nfrac_i = MAX(-floorf(log10f(d)), 0);  // # of fractional digits to show

    if (nfrac_i > 3) nfrac_i = 3;
    sprintf(str, "%%.%df", (int)nfrac_i);             // simplest axis labels

    //Draw  labels
    yofs = 0;

    factorA=200./(MaxZ-MinZ);
    factorB=-200.*MinZ/(MaxZ-MinZ);
    for (labelValue = graphmin_i; labelValue < graphmax_i + (.5 * d); labelValue += d)
    {
        if (graphmax_i >=10000 )
            sprintf(buf, "%.0f k", labelValue/1000);
        else
            sprintf(buf, str, labelValue); //Get label string in buf
        yofs=factorA*labelValue+factorB;

        FONT_Write(FONT_FRAN, LCD_RED, BackGrColor, 440, WY(yofs) - 12, buf);
    }

}

static void DrawRX(int SelQu, int SelEqu)// SelQu=1, if quartz measurement  SelEqu=1, if equal scales
{
#define LimitR 1999.f
    float LimitX;
    int i, imax;
    int x, RXX0;
    if (!isMeasured)
        return;
    //Find min and max values among scanned R and X to set up scale

    if(SelQu==0) {
        LimitX=LimitR;
        RXX0=X0;
    }
    else {
        LimitX= 99999.f;
        RXX0=21;
    }
    float minRXr = 1000000.f, minRXi = 1000000.f;
    float maxRXr = -1000000.f, maxRXi = -1000000.f;
    for (i = 0; i <= WWIDTH; i++)
    {
        if (crealf(values[i]) < minRXr)
            minRXr = crealf(values[i]);
        if (cimagf(values[i]) < minRXi)
            minRXi = cimagf(values[i]);
        if (crealf(values[i]) > maxRXr)
            maxRXr = crealf(values[i]);
        if (cimagf(values[i]) > maxRXi){
            maxRXi = cimagf(values[i]);
            if(cimagf(values[i+1])<=maxRXi)
                imax=i;
        }
    }


    if (minRXr < -LimitR)
        minRXr = -LimitR;
    if (maxRXr > LimitR)
        maxRXr = LimitR;

    if (minRXi < -LimitX)// 1999.f or 49999.f
        minRXi = -LimitX;
    if (maxRXi > LimitX)
        maxRXi = LimitX;

    if(SelEqu==1){
        if(maxRXr<maxRXi)
            maxRXr=maxRXi;
        else   maxRXi=maxRXr;
        if(minRXr>minRXi)
            minRXr=minRXi;
        else   minRXi=minRXr;
    }
    if(maxRXr-minRXr<40){
        maxRXr+=20;
        minRXr-=10;
    }
    if(maxRXi-minRXi<40){
        maxRXi+=20;
        minRXi-=10;
    }

    int nticks = 8; //Max number of intermediate ticks of labels
    float range_r = nicenum(maxRXr - minRXr, 0);

    float d = nicenum(range_r / (nticks - 1), 1);
    float graphmin_r = floorf(minRXr / d) * d;
    float graphmax_r = ceilf(maxRXr / d) * d;
    float grange_r = graphmax_r - graphmin_r;
    float nfrac_r = MAX(-floorf(log10f(d)), 0);  // # of fractional digits to show
    char str[100];
    if (nfrac_r > 4) nfrac_r = 4;
    sprintf(str, "%%.%df", (int)nfrac_r);             // simplest axis labels

    //Draw horizontal lines and labels
    int yofs = 0;
    int yofs_sm = 0;
    float labelValue;

#define RXOFFS(rx) ((int)roundf(((rx - graphmin_r) * WHEIGHT) / grange_r) + 1)
    int32_t RCurvColor;
    if(SelEqu==0)  RCurvColor = CurvColor;
    else RCurvColor = TextColor;
    for (labelValue = graphmin_r; labelValue < graphmax_r + (.5 * d); labelValue += d)
    {
        yofs = RXOFFS(labelValue);
        sprintf(buf, str, labelValue); //Get label string in buf
        if(SelEqu==0)// print only if we don't have equal scales
            FONT_Write(FONT_FRAN,RCurvColor, BackGrColor, 2, WY(yofs) - 12, buf);// WK
        if (roundf(labelValue) == 0)
            LCD_HLine(LCD_MakePoint(RXX0, WY(RXOFFS(0.f))), WWIDTH, WGRIDCOLORBR);
        else
            LCD_HLine(LCD_MakePoint(RXX0, WY(yofs)), WWIDTH, WGRIDCOLOR);
    }
    if(SelQu==0){
        FONT_Write(FONT_FRANBIG, RCurvColor, BackGrColor, RXX0 +412, Y0, "R");
        FONT_Write(FONT_FRANBIG, TextColor, LCD_RED, RXX0 +412, Y0+46, "X");
    }
    //Now draw R graph
    int lastoffset = 0;
    int lastoffset_sm = 0;

    for(i = 0; i <= WWIDTH; i++)
    {
        float r = crealf(values[i]);
        if (r < -LimitR)
            r = -LimitR;
        else if (r > LimitR)
            r = LimitR;
        yofs = RXOFFS(r);
        r = crealf(SmoothRX(i,  f1 > (CFG_GetParam(CFG_PARAM_BAND_FMAX) ) ? 1 : 0));
        if (r < -LimitR)
            r = -LimitR;
        else if (r > LimitR)
            r = LimitR;
        yofs_sm = RXOFFS(r);
        x = RXX0 + i;
        if(i == 0)
        {
            LCD_SetPixel(LCD_MakePoint(x, WY(yofs)), RCurvColor);
            LCD_SetPixel(LCD_MakePoint(x, WY(yofs_sm)), RCurvColor);
        }
        else
        {
            LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset)), LCD_MakePoint(x, WY(yofs)), RCurvColor);
            LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset_sm)), LCD_MakePoint(x, WY(yofs_sm)), RCurvColor);
            if(FatLines){
                LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset)+1), LCD_MakePoint(x, WY(yofs)+1), RCurvColor);// LCD_GREEN WK
                LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset_sm)+1), LCD_MakePoint(x, WY(yofs_sm)+1), RCurvColor);
            }
            LCD_Line(LCD_MakePoint(x , WY(lastoffset_sm)), LCD_MakePoint(x+1, WY(yofs_sm)), RCurvColor);
        }
        lastoffset = yofs;
        lastoffset_sm = yofs_sm;
    }
    float range_i = nicenum(maxRXi - minRXi, 0);
    d = nicenum(range_i / (nticks - 1), 1);
    float graphmin_i = floorf(minRXi / d) * d;
    float graphmax_i = ceilf(maxRXi / d) * d;
    float grange_i = graphmax_i - graphmin_i;
    float nfrac_i = MAX(-floorf(log10f(d)), 0);  // # of fractional digits to show

    if (nfrac_i > 4) nfrac_i = 4;
    sprintf(str, "%%.%df", (int)nfrac_i);             // simplest axis labels

    //Draw  labels
    yofs = 0;
    yofs_sm = 0;
   // draw right scale:
    for (labelValue = graphmin_i; labelValue < graphmax_i + (.5 * d); labelValue += d)
    {
        if (graphmax_i >=10000 )
            sprintf(buf, "%.0f k", labelValue/1000);
        else
            sprintf(buf, str, labelValue); //Get label string in buf

        yofs = ((int)roundf(((labelValue - graphmin_i) * WHEIGHT) / grange_i) + 1);

        FONT_Write(FONT_FRAN, LCD_RED, BackGrColor, 440, WY(yofs) - 12, buf);// WK
    }

    //Now draw X graph
    lastoffset = 0;
    lastoffset_sm = 0;
    for(i = 0; i <= WWIDTH; i++)
    {
        float ix = cimagf(values[i]);
        if (ix < -LimitX)
            ix = -LimitX;
        else if (ix > LimitX)
            ix = LimitX;

        yofs = ((int)roundf(((ix - graphmin_i) * WHEIGHT) / grange_i) + 1);

        ix = cimagf(SmoothRX(i,  f1 > (CFG_GetParam(CFG_PARAM_BAND_FMAX) ) ? 1 : 0));
        if (ix < -LimitX)
            ix = -LimitX;
        else if (ix > LimitX)
            ix = LimitX;
        yofs_sm = ((int)roundf(((ix - graphmin_i) * WHEIGHT) / grange_i) + 1);
        x = RXX0 + i;
        if(i == 0)
        {
            LCD_SetPixel(LCD_MakePoint(x, WY(yofs)), LCD_RED);//LCD_RGB(SM_INTENSITY, 0, 0
            LCD_SetPixel(LCD_MakePoint(x, WY(yofs_sm)), LCD_RED);
        }
        else
        {
            LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset)), LCD_MakePoint(x, WY(yofs)), LCD_RED);
            LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset_sm)), LCD_MakePoint(x, WY(yofs_sm)), LCD_RED);
            if(FatLines){
                LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset)+1), LCD_MakePoint(x, WY(yofs)+1), LCD_RED);// WK
                LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset_sm)+1), LCD_MakePoint(x, WY(yofs_sm)+1), LCD_RED);
            }
            LCD_Line(LCD_MakePoint(x , WY(lastoffset_sm)), LCD_MakePoint(x+1, WY(yofs_sm)), LCD_RED);

        }
        lastoffset = yofs;
        lastoffset_sm = yofs_sm;
    }
}

static void DrawSmith(void)
{
    int i;

    LCD_FillAll(BackGrColor);
    FONT_Write(FONT_FRAN, LCD_BLACK, LCD_PURPLE, 1, 0, modstr);
    if (0 == CFG_GetParam(CFG_PARAM_PAN_CENTER_F))
        sprintf(buf, "Smith chart: %.3f MHz + %s, red pt. is end. Z0 = %d.", (float)f1/1000000, BSSTR[span], CFG_GetParam(CFG_PARAM_R0));
    else
        sprintf(buf, "Smith chart: %.3f MHz +/- %s, red pt. is end. Z0 = %d.", (float)f1/1000000, BSSTR_HALF[span], CFG_GetParam(CFG_PARAM_R0));
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, modstrw + 10, 0, buf);

    SMITH_DrawGrid(cx0, cy0, smithradius, WGRIDCOLOR, BackGrColor, SMITH_R50 | SMITH_R25 | SMITH_R10 | SMITH_R100 | SMITH_R200 | SMITH_R500 |
                                 SMITH_J50 | SMITH_J100 | SMITH_J200 | SMITH_J25 | SMITH_J10 | SMITH_J500 | SMITH_SWR2 | SMITH_Y50);

    float r0f = (float)CFG_GetParam(CFG_PARAM_R0);


    SMITH_DrawLabels(TextColor, BackGrColor, SMITH_R10 | SMITH_R25 | SMITH_R50 | SMITH_R100 | SMITH_R200 | SMITH_R500 |
                                      SMITH_J10 | SMITH_J25 | SMITH_J50 | SMITH_J100 | SMITH_J200 | SMITH_J500);

    //Draw measured data
    if (isMeasured)
    {
        uint32_t lastx = 0;
        uint32_t lasty = 0;
        for(i = 0; i <= WWIDTH; i++)
        {
            float complex g = OSL_GFromZ(values[i], r0f);
            lastx = (uint32_t)roundf(cx0 + crealf(g) * smithradius);
            lasty = (uint32_t)roundf(cy0 - cimagf(g) * smithradius);
            SMITH_DrawG(i, g, CurvColor);
        }
        //Mark the end of sweep range with red cross
        SMITH_DrawGEndMark(LCD_RED);
    }
}

static void RedrawWindow()
{
    isSaved = 0;
    LCD_FillRect(LCD_MakePoint(0, 234),LCD_MakePoint(478, 248),BackGrColor);
    if ((grType == GRAPH_VSWR)||(grType == GRAPH_VSWR_Z)||(grType == GRAPH_VSWR_RX))
    {
        DrawGrid(GRAPH_VSWR);
        DrawVSWR();
        DrawCursor();
    }
    else if (grType == GRAPH_RX)
    {
        DrawGrid(GRAPH_RX);
        DrawRX(0,0);// 1
    }
    else if (grType == GRAPH_S11)
    {
        DrawGrid(GRAPH_S11);
        DrawS11();
        DrawCursor();
    }
    else
        DrawSmith();
    DrawCursor();
    if ((grType != GRAPH_S11))
    {
        DrawCursorText();
        DrawBottomText();
        DrawAutoText();
    }
    else if ((CFG_GetParam(CFG_PARAM_S11_SHOW) == 1) && (grType == GRAPH_S11))
    {
        DrawCursorTextWithS11();
        DrawBottomText();
        DrawAutoText();
    }
}

static void save_snapshot(void)
{
    static const TCHAR *sndir = "/aa/snapshot";
    char path[64];
    char wbuf[256];
    char* fname = 0;
    uint32_t i = 0;
    FRESULT fr = FR_OK;

    if (!isMeasured || isSaved)
        return;

    //DrawSavingText();
    Date_Time_Stamp();

    fname = SCREENSHOT_SelectFileName();

    if(strlen(fname)==0) return;

    SCREENSHOT_DeleteOldest();
    if (CFG_GetParam(CFG_PARAM_SCREENSHOT_FORMAT))
        SCREENSHOT_SavePNG(fname);
    else
        SCREENSHOT_Save(fname);

    //Now write measured data to S1P file
    sprintf(path, "%s/%s.s1p", sndir, fname);
    FIL fo = { 0 };
    UINT bw;
    fr = f_open(&fo, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (FR_OK != fr)
        CRASHF("Failed to open file %s", path);
    if (CFG_S1P_TYPE_S_RI == CFG_GetParam(CFG_PARAM_S1P_TYPE))
    {
        sprintf(wbuf, "! Touchstone file by EU1KY antenna analyzer\r\n"
                "# MHz S RI R 50\r\n"
                "! Format: Frequency S-real S-imaginary (normalized to 50 Ohm)\r\n");
    }
    else // CFG_S1P_TYPE_S_MA
    {
        sprintf(wbuf, "! Touchstone file by EU1KY antenna analyzer\r\n"
                "# MHz S MA R 50\r\n"
                "! Format: Frequency S-magnitude S-angle (normalized to 50 Ohm, angle in degrees)\r\n");
    }
    fr = f_write(&fo, wbuf, strlen(wbuf), &bw);
    if (FR_OK != fr) goto CRASH_WR;

    uint32_t fstart;
    if (CFG_GetParam(CFG_PARAM_PAN_CENTER_F) == 0)
        fstart = f1;
    else
        fstart = f1 - 500*BSVALUES[span];

    for (i = 0; i < WWIDTH; i++)
    {
        float complex g = OSL_GFromZ(values[i], 50.f);
        float fmhz = ((float)fstart/1000.f + (float)i * BSVALUES[span] / WWIDTH) / 1000.0f;
        if (CFG_S1P_TYPE_S_RI == CFG_GetParam(CFG_PARAM_S1P_TYPE))
        {
            sprintf(wbuf, "%.6f %.6f %.6f\r\n", fmhz, crealf(g), cimagf(g));
        }
        else // CFG_S1P_TYPE_S_MA
        {
            g = OSL_GtoMA(g); //Convert G to magnitude and angle in degrees
            sprintf(wbuf, "%.6f %.6f %.6f\r\n", fmhz, crealf(g), cimagf(g));
        }
        fr = f_write(&fo, wbuf, strlen(wbuf), &bw);
        if (FR_OK != fr) goto CRASH_WR;
    }
    f_close(&fo);

    isSaved = 1;
//    BSP_LCD_SelectLayer(0);
//    DrawSavedText();
//    BSP_LCD_SelectLayer(1);
    DrawSavedText();
    return;
CRASH_WR:
    CRASHF("Failed to write to file %s", path);
}
 #define XX0 190
 #define YY0 42

int TouchTest(){

     if (TOUCH_Poll(&pt)){
        if((pt.y <80)&&(pt.x >380)){
            // Upper right corner --> EXIT
            Beep(1);
            while(TOUCH_IsPressed());
            Sleep(100);
            return 99;
        }
        if(pt.x<(XX0-8)){// select the pressed field:
            Beep(1);
            if(pt.y<YY0+48) return 0;
            if(pt.y<YY0+96) return 1;
            if(pt.y<YY0+144) return 2;
            if(pt.y<YY0+192) return 3;
            return 4;
        }
     }
     return -1;
 }

//Scan R-50 / X in +/- 200 kHz range around measurement frequency with 10 kHz step, to draw a small graph besides the measurement
static int8_t lastR;// WK
static int8_t lastX;
static int rMax;
static int xMax;
static bool reverse1;
static float complex z200[21] = { 0 };

int Scan200(uint8_t line, int index1){

int touch;
 int32_t r;
 int32_t x;
 int8_t idx;
 int fq;// frequency in Hz
 if(multi_fr[line]==0) return -1;// nothing to do
    if(index1==0){
        rMax=0;
        xMax=0;
        for(idx=0;idx<21;idx++){
            fq = (int)multi_fr[line]*1000 + (idx - 10) * multi_bw[line]*50;
            touch=TouchTest();
            if(touch!=-1) return touch;
            if (fq > 0){
                GEN_SetMeasurementFreq(fq);
                Sleep(2);
                DSP_Measure(fq, 1, 1, CFG_GetParam(CFG_PARAM_MEAS_NSCANS));
                z200[idx] = DSP_MeasuredZ();
                r = (int32_t)crealf(z200[idx]);
                if(r<0) r=-r;
                if(rMax<r)rMax=r;
                x = (int32_t)cimagf(z200[idx]);
                if(x<0) x=-x;
                if(x>1000)x=1000;
                if(xMax<x) xMax=x;
            }
        }
        if(rMax<100)rMax=100;
        if(xMax<100)xMax=100;
        r=(int32_t)((crealf(z200[0])-50.0)*20.0/rMax);
        //if(r<0) r=-r;
        if(r>40) r=40;
        lastR=r;
        x=(int32_t)((cimagf(z200[0]))*16.0/xMax);
        if(x>16)x=16;
        if(x<-16) x=-16;
        lastX=x;
        r=(int32_t)(crealf(z200[10]));
        if(r>999) r=999;
        x=(int32_t)(cimagf(z200[10]));
        if(x>999) x=999;
        if(x<-999) x=-999;
        LCD_FillRect((LCDPoint){XX0+137, YY0 + line*48}, (LCDPoint){XX0+210, YY0 + 30 + line*48}, BackGrColor);
        FONT_Print(FONT_FRAN, TextColor, BackGrColor, XX0+138, 38 + 48*line, " %u Ohm", r);// r
        FONT_Print(FONT_FRAN, Color1, BackGrColor, XX0+138, 58 + 48*line, "%d *j Ohm", x);// x
        LCD_FillRect((LCDPoint){XX0-5, YY0-10  + line*48}, (LCDPoint){XX0+135, YY0 + 30 + line*48}, BackGrColor);
//        BSP_LCD_SelectLayer(BSP_LCD_GetActiveLayer());
        LCD_Line(LCD_MakePoint(XX0-5, YY0+10+48*line), LCD_MakePoint(XX0+135, YY0+10+48*line), Color2);
        LCD_Rectangle(LCD_MakePoint(XX0-5, YY0-10+48*line), LCD_MakePoint(XX0+135, YY0+30+48*line), Color2);
        LCD_Line(LCD_MakePoint(XX0+70, YY0+10+48*line), LCD_MakePoint(XX0+70, YY0+30+48*line), Color2);

    }
    else{
        touch=TouchTest();
        if(touch!=-1) return touch;
        r=(int)((crealf(z200[index1])-50.0)*20.0/rMax);// -50.0
        //if(r<0) r=-r;
        if(r>40) r=40;
        x=(int)((cimagf(z200[index1]))*16.0/xMax);
        if(x>16)x=16;
        if(x<-16) x=-16;
        if(index1!=0){
            if(reverse1){
                LCD_Line(LCD_MakePoint(XX0+index1*6, YY0+10+48*line-lastR), LCD_MakePoint(XX0+index1*6+5, YY0+10+48*line-r), TextColor);
                LCD_Line(LCD_MakePoint(XX0+index1*6, YY0+10+48*line-lastR-1), LCD_MakePoint(XX0+index1*6+5, YY0+10+48*line-r-1), TextColor);
                LCD_Line(LCD_MakePoint(XX0+index1*6, YY0+10+48*line-lastR+1), LCD_MakePoint(XX0+index1*6+5, YY0+10+48*line-r+1), TextColor);
            }
            LCD_Line(LCD_MakePoint(XX0+index1*6, YY0+10+48*line-lastX), LCD_MakePoint(XX0+index1*6+5, YY0+10+48*line-x), Color1);
            LCD_Line(LCD_MakePoint(XX0+index1*6, YY0+10+48*line-lastX-1), LCD_MakePoint(XX0+index1*6+5, YY0+10+48*line-x-1), Color1);
            LCD_Line(LCD_MakePoint(XX0+index1*6, YY0+10+48*line-lastX+1), LCD_MakePoint(XX0+index1*6+5, YY0+10+48*line-x+1), Color1);
            if(!reverse1){
                LCD_Line(LCD_MakePoint(XX0+index1*6, YY0+10+48*line-lastR), LCD_MakePoint(XX0+index1*6+5, YY0+10+48*line-r), TextColor);
                LCD_Line(LCD_MakePoint(XX0+index1*6, YY0+10+48*line-lastR-1), LCD_MakePoint(XX0+index1*6+5, YY0+10+48*line-r-1), TextColor);
                LCD_Line(LCD_MakePoint(XX0+index1*6, YY0+10+48*line-lastR+1), LCD_MakePoint(XX0+index1*6+5, YY0+10+48*line-r+1), TextColor);
            }
            lastR=r;
            lastX=x;
        }
    }
    return -1;
}


char str[100];
int i;

uint32_t freqx;// kHz

int ShowFreq(int indx){
uint32_t dp;
uint32_t mhz;
uint32_t bw1;

   if(indx>4) return -1;
    freqx=multi_fr[indx];
    bw1=multi_bw[indx];
    dp = (freqx % 1000) ;
    mhz = freqx / 1000;
    LCD_FillRect((LCDPoint){0, YY0-6 + indx*48}, (LCDPoint){XX0-6, FONT_GetHeight(FONT_FRANBIG)+ YY0-6 + indx*48}, BackGrColor);
    LCD_Rectangle((LCDPoint){2, YY0-10+48*indx}, (LCDPoint){XX0-8, YY0+30+48*indx}, LCD_BLACK);
    if(freqx==0) {
        LCD_FillRect((LCDPoint){4, YY0-10+48*indx}, (LCDPoint){XX0+229, YY0+32+48*indx}, BackGrColor);
        LCD_Rectangle((LCDPoint){2, YY0-10+48*indx}, (LCDPoint){XX0-8, YY0+30+48*indx}, TextColor);
        return -1;
    }
    LCD_FillRect((LCDPoint){XX0+230, YY0+5 + 48*indx}, (LCDPoint){XX0+288, YY0 + 20 + 48*indx}, BackGrColor);// clear bandwidth
    FONT_Print(FONT_FRAN, TextColor, BackGrColor, XX0+234, YY0+6 + 48*indx, "+-%u k", bw1/2);// bandwidth
    FONT_Print(FONT_FRANBIG, TextColor, BackGrColor, 4, YY0-6 + 48*indx, "%u.%03u", mhz, dp);// frequency
    return indx;
}

void ShowResult(int indx){

float VSWR;
float complex z0;

    if(ShowFreq(indx)==-1) return;// nothing to do
    GEN_SetMeasurementFreq(multi_fr[indx]*1000);
    Sleep(10);
    DSP_Measure(freqx*1000, 1, 1, CFG_GetParam(CFG_PARAM_MEAS_NSCANS));
    z0 = DSP_MeasuredZ();
    VSWR = DSP_CalcVSWR(z0);
    if(VSWR>99.0)
        sprintf(str, "%.0f", VSWR);
    else
        sprintf(str, "%.1f", VSWR);
    FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 118, YY0-6 + indx*48, str);

}
uint32_t  GetFrequency(uint32_t f0){// fo in kHz
uint32_t fkhz=f0;
    if (PanFreqWindow(&fkhz, &span));
    return fkhz;
}

static bool  rqExitSWR;
static uint8_t SWRLimit;

void SWR_Exit(void){
    rqExitSWR=true;
}

static int Tone;

void SWR_Mute(void){
    if(Tone==0){
        Tone=1;//tone
        FONT_Write(FONT_FRANBIG, M_FGCOLOR, M_BGCOLOR, 198, 212, " Tone ");
        SWRLimit=1;
        UB_TIMER2_Start();
    }
    else {
        Tone=0;//no tone
        FONT_Write(FONT_FRANBIG, M_FGCOLOR, M_BGCOLOR, 198, 212, " Mute ");
    }
}

static void SWR_2(void){
    if(SWRLimit==3) LCD_Rectangle((LCDPoint){380, 210}, (LCDPoint){476, 244}, 0xffffff00);//yellow
    if(SWRLimit==2) {
        SWRLimit=1;
        LCD_Rectangle((LCDPoint){280, 210}, (LCDPoint){380, 244}, 0xffffff00);
    }
    else {
        SWRLimit=2;
        LCD_Rectangle((LCDPoint){280, 210}, (LCDPoint){380, 244}, 0xffff0000);
    }
    while(TOUCH_IsPressed());
    Sleep(50);
}

static void SWR_3(void){
    if(SWRLimit==2) LCD_Rectangle((LCDPoint){280, 210}, (LCDPoint){380, 244}, 0xffffff00);
    if(SWRLimit==3){
        SWRLimit=1;
        LCD_Rectangle((LCDPoint){380, 210}, (LCDPoint){476, 244}, 0xffffff00);
    }
    else {
        SWRLimit=3;
        LCD_Rectangle((LCDPoint){380, 210}, (LCDPoint){476, 244}, 0xffff0000);// red
    }
    while(TOUCH_IsPressed());
    Sleep(50);
}

static void ShowMeasFr(void)
{
    char str[100];
    uint8_t i,j;
    float freq=(float)(CFG_GetParam(CFG_PARAM_MEAS_F) / 1000000.0);
    sprintf(str, "F: %.3f MHz  ", freq);
    LCD_FillRect(LCD_MakePoint(150, 35), LCD_MakePoint(320,65), BackGrColor);
    //
    FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 150, 35, str);// WK
}


static uint32_t fxs = 3600000ul; //Scan range start frequency, in Hz
static uint32_t fxkHzs;//Scan range start frequency, in kHz
static BANDSPAN *pBss;

void SWR_SetFrequency(void)
{
//    while(TOUCH_IsPressed()); WK
    fxs=CFG_GetParam(CFG_PARAM_MEAS_F);
    fxkHzs=fxs/1000;
    //span=BS400;

    if (PanFreqWindow(&fxkHzs, (BANDSPAN*)&span))
        {
            //Span or frequency has been changed
            CFG_SetParam(CFG_PARAM_MEAS_F, fxkHzs*1000);
            f1=fxkHzs*1000;
           // span=(BANDSPAN)pBss;
        }
    CFG_Flush();
  //  redrawWindow = 1;
    Sleep(200);
    ShowMeasFr();
    sFreq=1;
}

void setup_GPIO(void) // GPIO I Pin 2 for buzzer
{
GPIO_InitTypeDef gpioInitStructure;

  __HAL_RCC_GPIOI_CLK_ENABLE();
  gpioInitStructure.Pin = GPIO_PIN_2;
  gpioInitStructure.Mode = GPIO_MODE_OUTPUT_PP;
  gpioInitStructure.Pull = GPIO_NOPULL;
  gpioInitStructure.Speed = GPIO_SPEED_MEDIUM;
  HAL_GPIO_Init(GPIOI, &gpioInitStructure);
  HAL_GPIO_WritePin(GPIOI, GPIO_PIN_2, 0);
  //AUDIO1=0;
}

static uint32_t freqChg;

uint8_t AUDIO1=1;

void Tune_SWR_Proc(void){// -----------------------------------------------------------------------

GPIO_PinState OUTGpio;
char str[20];
float vswrf, vswrf_old, vswLogf, SwrDiff;
uint32_t width, vswLog=0, Timer;
uint32_t color1, vswr10, vsw_old, k=0;
TEXTBOX_CTX_t SWR1_ctx;
    Tone=1;//tone on
    SWRLimit=1;
    setup_GPIO();// GPIO I Pin 2 for buzzer
    freqChg=0;
    rqExitSWR=false;
    SetColours();
    LCD_FillAll(BackGrColor);
    FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 120, 10, "Tune SWR ");
    Sleep(1000);
    while(TOUCH_IsPressed());
    fxs=CFG_GetParam(CFG_PARAM_MEAS_F);
    fxkHzs=fxs/1000;
    ShowMeasFr();
    TEXTBOX_InitContext(&SWR1_ctx);
    AUDIO1=1;
    UB_TIMER2_Init_FRQ((uint32_t)(400)); //100...1000 Hz
    UB_TIMER2_Start();

//HW calibration menu
    TEXTBOX_Append(&SWR1_ctx, (TEXTBOX_t*)tb_menuSWR);
    TEXTBOX_DrawContext(&SWR1_ctx);
for(;;)
    {
        Sleep(0); //for autosleep to work
        if (TEXTBOX_HitTest(&SWR1_ctx))
        {
            if (rqExitSWR)
            {
                Tone=0;
                //UB_TIMER2_Init_FRQ(1000);
                rqExitSWR=false;
                GEN_SetMeasurementFreq(0);
                return;
            }
            if(freqChg==1){
               ShowMeasFr();
               freqChg=0;
            }
            Sleep(50);
        }
        k++;
        if(k>=5){
            k=0;
            DSP_Measure(fxkHzs*1000, 1, 1, CFG_GetParam(CFG_PARAM_MEAS_NSCANS));
            vswrf = DSP_CalcVSWR(DSP_MeasuredZ());
            SwrDiff=vswrf_old-vswrf;
            if(SwrDiff<0)SwrDiff=-SwrDiff;
            if((SwrDiff>0.01*vswrf)){// Difference more than 3 %
                vswrf_old=vswrf;
                vswr10=10.0*vswrf;
                if(SWRLimit==2){
                    if(vswr10>20) Tone=1;
                    else Tone=0;
                }
                if(SWRLimit==3){
                    if(vswr10>30) Tone=1;
                    else Tone=0;
                }
                vswLogf= 200.0*log10f(10.0*log10f(vswrf)+5.0);

                if(Tone==1){
                    UB_TIMER2_Init_FRQ((uint32_t)(6.0*vswLogf-250.0)); //100...1000 Hz
                    UB_TIMER2_Start();
                }
                else UB_TIMER2_Stop();
                sprintf(str, "SWR: %.2f  ", vswrf);
                LCD_FillRect(LCD_MakePoint(200, 60), LCD_MakePoint(470,115), BackGrColor);
                FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 200, 60, str);
                width=(uint32_t)(3*vswLogf-400.0);
                if(width>479) width=479;
                if(vswrf<2.0) color1=0xff00ff00;
                else if(vswrf<3) color1=0xffffff00;
                else color1=0xffff0000;
                LCD_FillRect(LCD_MakePoint(0, 116), LCD_MakePoint(width,160), TextColor);
                LCD_FillRect(LCD_MakePoint(0, 161), LCD_MakePoint(width,205), color1);
                LCD_FillRect(LCD_MakePoint(width+1, 116), LCD_MakePoint(479,205), BackGrColor);
            }
        }
        Sleep(1);//5
    }
}

void MultiSWR_Proc(void){// WK ******************************************************************************
int redrawRequired = 0;
int touch;
int fx;// in kHz
uint32_t activeLayer;
int i,j;

    while(TOUCH_IsPressed());
    SetColours();
    reverse1=true;
    multi_fr[0]=CFG_GetParam(CFG_PARAM_MULTI_F1);//  in kHz
    multi_fr[1]=CFG_GetParam(CFG_PARAM_MULTI_F2);
    multi_fr[2]=CFG_GetParam(CFG_PARAM_MULTI_F3);//  in kHz
    multi_fr[3]=CFG_GetParam(CFG_PARAM_MULTI_F4);//  in kHz
    multi_fr[4]=CFG_GetParam(CFG_PARAM_MULTI_F5);//  in kHz
    multi_bwNo[0]=CFG_GetParam(CFG_PARAM_MULTI_BW1);
    multi_bwNo[1]=CFG_GetParam(CFG_PARAM_MULTI_BW2);
    multi_bwNo[2]=CFG_GetParam(CFG_PARAM_MULTI_BW3);
    multi_bwNo[3]=CFG_GetParam(CFG_PARAM_MULTI_BW4);
    multi_bwNo[4]=CFG_GetParam(CFG_PARAM_MULTI_BW5);
    if(multi_bwNo[0]>=5)
        multi_bw[0]=BSVALUES[multi_bwNo[0]];//  in kHz
    else  multi_bw[0] = 0;
    if(multi_bwNo[1]>=5)
        multi_bw[1]=BSVALUES[multi_bwNo[1]];//  in kHz
    else  multi_bw[1] = 0;
    if(multi_bwNo[2]>=5)
        multi_bw[2]=BSVALUES[multi_bwNo[2]];//  in kHz
    else  multi_bw[2] = 0;
    if(multi_bwNo[3]>=5)
        multi_bw[3]=BSVALUES[multi_bwNo[3]];//  in kHz
    else  multi_bw[3] = 0;
    if(multi_bwNo[4]>=5)
        multi_bw[4]=BSVALUES[multi_bwNo[4]];//  in kHz
    else  multi_bw[4] = 0;
    LCD_FillAll(BackGrColor);
    LCD_FillRect((LCDPoint){380,1}, (LCDPoint){476,35}, LCD_MakeRGB(255, 0, 0));
    LCD_Rectangle(LCD_MakePoint(420, 1), LCD_MakePoint(476, 35), BackGrColor);
    FONT_Write(FONT_FRANBIG, TextColor, LCD_MakeRGB(255, 0, 0), 400, 2, "Exit");
    FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 5, 0, "MHz           SWR      R /");
    FONT_Write(FONT_FRANBIG, Color1, BackGrColor, 254, 0, "X");
//    LCD_ShowActiveLayerOnly();
    for(j=0;j<=4;j++){
        ShowFreq(j);// show stored frequencies and bandwidths
    }
    Sleep(500);

    for(;;){
        for(j=0;j<5;j++){
            if(j==0) reverse1=!reverse1;
            ShowResult(j);
            for(i=0;i<21;i++){
                touch=TouchTest();//if all fr[i]==0
                if(touch==-1) touch=Scan200(j,i);   // no touch
                if (touch==99){                     // Exit
                   LCD_FillAll(BackGrColor);
                   Sleep(100);
                   CFG_Flush();
                   return;
                }
                if(touch>=0){// new manual frequency input (touch = line 1..5
                    fx=GetFrequency(multi_fr[touch]);// manual frequency input
                    multi_fr[touch]=fx;
                    multi_bw[touch]=BSVALUES[span];
                    switch (touch){
                        case 0:
                            CFG_SetParam(CFG_PARAM_MULTI_F1, fx);
                            CFG_SetParam(CFG_PARAM_MULTI_BW1, span);
                            break;
                        case 1:
                            CFG_SetParam(CFG_PARAM_MULTI_F2, fx);
                            CFG_SetParam(CFG_PARAM_MULTI_BW2, span);
                            break;
                        case 2:
                            CFG_SetParam(CFG_PARAM_MULTI_F3, fx);
                            CFG_SetParam(CFG_PARAM_MULTI_BW3, span);
                            break;

                        case 3:
                            CFG_SetParam(CFG_PARAM_MULTI_F4, fx);
                            CFG_SetParam(CFG_PARAM_MULTI_BW4, span);
                            break;
                        case 4:
                            CFG_SetParam(CFG_PARAM_MULTI_F5, fx);
                            CFG_SetParam(CFG_PARAM_MULTI_BW5, span);
                            break;
                    }
                    ShowFreq(touch);// show new frequency and bandwidth
                    if(j<0) j=0;
                    break;
                }

            }
        }
    }
}

void zoomMinus(void){

}

void PANVSWR2_Proc(void)// **************************************************************************+*********
{
int redrawRequired = 0;
uint32_t activeLayer;
uint32_t FreqkHz;
int bip;
    f1=CFG_GetParam(CFG_PARAM_PAN_F1);
    SetColours();
    LCD_FillAll(BackGrColor);
    FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 120, 100, "Panoramic scan mode");
    Sleep(1000);
    while(TOUCH_IsPressed());

    LoadBkups();
    FreqkHz=f1/1000;
    grType = GRAPH_VSWR;
    if (!isMeasured)
    {
        isSaved = 0;
    }
    if (0 == modstrw)
    {
        modstrw = FONT_GetStrPixelWidth(FONT_FRAN, modstr);
    }

        if (!isMeasured)
        {
            DrawGrid(1);
            DrawHelp();
        }
        else
            RedrawWindow();
    DrawFootText();
    DrawAutoText();

    for(;;)
    {
        Sleep(5);
        activeLayer = BSP_LCD_GetActiveLayer();
        BSP_LCD_SelectLayer(activeLayer);
        if (TOUCH_Poll(&pt))
        {
            if(((grType == GRAPH_VSWR)||(grType == GRAPH_VSWR_Z))&&(pt.x<30)&&(pt.y>155)&&(pt.y<215)){
                bip=0;
                if(loglog==0) loglog=1;
                else loglog=0;
                redrawRequired = 1;
            }
            ///////////////
            if ((pt.y < 80)&&(pt.x>360)&&autofast)// Top Right +100 Hz
                f1+=100;
            if ((pt.y < 80)&&(pt.x<60)&&autofast)// Top Left -100 Hz
                f1-=100;
            ///////////////
            if ((pt.y < 80)&&(pt.x>160)&&(pt.x<320))// Top
            {

                if (PanFreqWindow(&FreqkHz, &span))
                {
                    //Span or frequency has been changed
                    bip=0;
                    f1=1000*FreqkHz;
                    isMeasured = 0;
                    RedrawWindow();
                    redrawRequired = 1;
                }
            }
            else if (pt.y > 90 && pt.y <= 170)
            {
                if (pt.x < 50)  // left <
                {
                    Beep(0);
                    DecrCursor();
                    continue;
                }
                else if (pt.x > 100 && pt.x < 380)   // center
                {
                    bip=1;
                    if (grType == GRAPH_VSWR)
                        grType = GRAPH_VSWR_Z;
                    else if (grType == GRAPH_VSWR_Z)
                        grType = GRAPH_VSWR_RX;
                    else if (grType == GRAPH_VSWR_RX)
                        grType = GRAPH_RX;
                    else if ((grType == GRAPH_RX) && (CFG_GetParam(CFG_PARAM_S11_SHOW) == 1))
                        grType = GRAPH_S11;
                    else if ((grType == GRAPH_RX) && (CFG_GetParam(CFG_PARAM_S11_SHOW) == 0))
                        grType = GRAPH_SMITH;
                    else if (grType == GRAPH_S11)
                        grType = GRAPH_SMITH;
                    else
                        grType = GRAPH_VSWR;
                    redrawRequired = 1;
                }
                else if (pt.x > 430)    //right >
                {
                    Beep(0);
                    IncrCursor();
                    continue;
                }
            }
            else if (pt.y > 200)
            {
                if (pt.x < 60) // Lower left corner EXIT
                {
                    Beep(0);
                    while(TOUCH_IsPressed());
                    autofast = 0;
                    Sleep(100);
                    return;// Exit
                }
                if (pt.x > 410)
                { //Lower right corner: perform scan or turn off auto
                    if (0 == autofast)
                    {
                        Beep(1);
                        FONT_Write(FONT_FRANBIG, LCD_RED, LCD_BLACK, 180, 100, "  Scanning...  ");
                        ScanRX_ZI();
                        bip=2;// silence
                        redrawRequired = 1;
                    }
                    else
                    {
                        bip=1;
                        autofast = 0;
                        redrawRequired = 1;
                        //RedrawWindow();
                    }

                }
                else if(pt.x > 70 && pt.x <= 104 ){
                    //zoomMinus();
                    bip=1;
                    if(fcur!=0)
                        f1=fcur*1000.;
                    if(span>0)
                        span--;
                    if(0 == autofast)
                        ScanRX(0);
                    redrawRequired = 1;
                }
                 else if(pt.x > 104 && pt.x < 140 ){
                    //zoomPlus();
                    bip=1;
                    if(fcur!=0)
                        f1=fcur*1000.;
                    if(span<BS100M)
                        span++;
                    if(0 == autofast)
                        ScanRX(0);
                    redrawRequired = 1;
                }
                else if (pt.x > 150 && pt.x < 240 && isMeasured && !isSaved)
                {
                    bip=1;
                    save_snapshot();
                }
                else if (pt.x >= 260 && pt.x <= 370)
                {
                    autofast = !autofast;
                    redrawRequired = 1;
                }
            }
            if(bip<2)
                Beep(bip);
            if(redrawRequired)
            {
                BSP_LCD_SelectLayer(!activeLayer);
                RedrawWindow();
                BSP_LCD_SelectLayer(activeLayer);
                RedrawWindow();
            }
            while(TOUCH_IsPressed())
            {
                Sleep(250);
            }

        }
        else
        {
            cursorChangeCount = 0;
            beep=0;
        }

        if (autofast && (cursorChangeCount == 0))
        {
			activeLayer = BSP_LCD_GetActiveLayer();
            BSP_LCD_SelectLayer(!activeLayer);
            ScanRXFast();
            RedrawWindow();
            autosleep_timer = 30000; //CFG_GetParam(CFG_PARAM_LOWPWR_TIME);
        }
        LCD_ShowActiveLayerOnly();
    }
}


TEXTBOX_CTX_t Quartz_ctx;
float C0;

FIL QuDataFile;           /* File object */
FRESULT res;                                          /* FatFs function common result code */
uint32_t byteswritten, bytesread;                     /* File write/read counts */
char fileeebuffer [200]="hfxyciluojioi;tgsvbiuubn  y ggoig i iu uiu iu iu  ygyug ";
char filebuffer [2200];
static int qu_num;


void QuAddToFile(void)
{
//static int qu_num;

    sprintf(str, "%.2d  ", qu_num+1);
    strcat (filebuffer,str);
/*
    sprintf(str, "Fs:%6.3f  ", Fs/1000.0);
    strcat (filebuffer,str);

    sprintf(str, "Fp:%6.3f  ", Fp/1000.0);
    strcat (filebuffer,str);
    */
    sprintf(str, "%6.3f  ", Fs/1000.0);
    strcat (filebuffer,str);

    sprintf(str, "%6.3f  ", Fp/1000.0);
    strcat (filebuffer,str);

    sprintf(str, "%6.0f    ", Q);
    strcat (filebuffer,str);

    sprintf(str, "%.1f mH    ", 1e3*Ls1);
    strcat (filebuffer,str);

    sprintf(str, "%.4f pF    ", 1e12*Cs);
    strcat (filebuffer,str);

    sprintf(str, "%.1f Ohm   ", Rs);
    strcat (filebuffer,str);

    sprintf(str, "%.2f pF\r", 1e12*Cp);
    strcat (filebuffer,str);

    qu_num++;
    FONT_Print(FONT_FRANBIG, TextColor, BackGrColor, 0, 0, "Qu %d  ", qu_num);
    FONT_Print(FONT_FRAN, TextColor, BackGrColor, 0, 30, "lenght: %d   ",strlen(filebuffer));
}

void QuSaveFile(void)
{
    res =f_unlink("QuData.TXT");
    if(f_open(&QuDataFile, "QuData.TXT", FA_OPEN_ALWAYS | FA_WRITE) != FR_OK)
    {
        FONT_Print(FONT_FRANBIG, LCD_COLOR_RED, BackGrColor, 150, 35, "ERROR");
        Sleep(1000);
        while(TOUCH_IsPressed());
        FONT_Print(FONT_FRANBIG, BackGrColor, BackGrColor, 150, 35, "ERROR");
    }
    else
    {
        strcat (filebuffer,"----------------------------------end---------------------------------------\n");
        res = f_write(&QuDataFile, filebuffer, sizeof filebuffer/*strlen(filebuffer)+1*/, (void *)&byteswritten);
        f_close(&QuDataFile);
        FONT_Print(FONT_FRANBIG, LCD_COLOR_GREEN, BackGrColor, 315, 0, "OK");
        Sleep(1000);
        while(TOUCH_IsPressed());
        FONT_Print(FONT_FRANBIG, BackGrColor, BackGrColor, 315, 0, "OK"); // CLEAR
    }
    while(TOUCH_IsPressed());
}
float Get_C(void)
{
    float complex rx0;
    uint32_t fstart, freq1;
    float r= fabsf(crealf(rx0));//calculate Cp (quartz)
    float im= cimagf(rx0);
    float c,xp=1.0f;

    if(Fs!=0)
        freq1= Fs-100000;
    else
        freq1=CFG_GetParam(CFG_PARAM_MEAS_F);

    //freq1=7900000;
    DSP_Measure(freq1, 1, 1, 3); //Fake initial run to let the circuit stabilize
    rx0 = DSP_MeasuredZ();
    Sleep(20);

    DSP_Measure(freq1, 1, 1, 3);

    rx0 = DSP_MeasuredZ();
    r= fabsf(crealf(rx0));//calculate Cp (quartz)
    im= cimagf(rx0);

    if(im*im>0.0025)
        xp=im+r*(r/im);// else xp=im=-10000.0f;// ??

    c=-1/( 6.2832 *freq1* xp);
    return c;
}


void QuCalibrate(void){
    if(sFreq==0){
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 10, 100, "First set estimated Frequency ");
        Sleep(20);
        return;
    }
    else    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 10, 100, "                                   ");
    if(span>BS1000) span=BS1000;// maximum: 1 MHz
    //ScanRX_QuFast(1);;//only compute C0
    C0=Get_C();
    //C0=Cp;
    sprintf(str, "C0 = %.2f pF", 1e12*C0);
    FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 200, 100, str);
    sCalib=1;
    LCD_FillRect(LCD_MakePoint(10,180),LCD_MakePoint(440,215),BackGrColor);
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 10, 180, "Now insert the Quartz");
    Sleep(20);
    TEXTBOX_InitContext(&Quartz_ctx);
    TEXTBOX_Append(&Quartz_ctx, (TEXTBOX_t*)tb_menuQuartz2);
    TEXTBOX_DrawContext(&Quartz_ctx);
}

float QuMeasLs(uint32_t frequency)
{
float LsL, LsH,Ls1;
float complex rx, rx0;
uint32_t fstart, freq1, deltaF;

    if(frequency==0)
    {

        freq1=Fs-200;
        DSP_Measure(freq1, 1, 1, 5); //Fake initial run to let the circuit stabilize
        rx0 = DSP_MeasuredZ();
        Sleep(20);

        DSP_Measure(freq1, 1, 1, 5);
        rx0 = DSP_MeasuredZ();
        LsL= cimagf(rx0);

        freq1=Fs+200;
        DSP_Measure(freq1, 1, 1, 5); //Fake initial run to let the circuit stabilize
        rx0 = DSP_MeasuredZ();
        Sleep(20);

        DSP_Measure(freq1, 1, 1, 5);
        rx0 = DSP_MeasuredZ();
        LsH = cimagf(rx0);
        return Ls1=(LsH-LsL)/(4.0*PI*400.0);
    }
    else    // frequency!=0
     {
        freq1=frequency-100000;
        DSP_Measure(freq1, 1, 1, 5); //Fake initial run to let the circuit stabilize
        rx0 = DSP_MeasuredZ();
        Sleep(20);

        DSP_Measure(freq1, 1, 1, 5);
        rx0 = DSP_MeasuredZ();
        LsL= cimagf(rx0);

        freq1=frequency+100000;
        DSP_Measure(freq1, 1, 1, 5); //Fake initial run to let the circuit stabilize
        rx0 = DSP_MeasuredZ();
        Sleep(20);

        DSP_Measure(freq1, 1, 1, 5);
        rx0 = DSP_MeasuredZ();
        LsH = cimagf(rx0);
        return Ls1=(LsH-LsL)/(4.0*PI*200000.0);
    }

}
static void ScanFsFp(void){
char str[100];
float complex rx;
float impedance, newX, oldX, MaxX, absX;
uint32_t i, k, sel, imax;
uint32_t fstart, freq1, dF;
int Fq1found=0,Fq2Found=0;
    fstart=Fs-1000;
    dF=100;     //coarse scanning for Fs
    freq1 = fstart;
    DSP_Measure(freq1, 1, 1, 7);

    for(i = 0; i <= 1000/*WWIDTH*/; i++)
    {
        Sleep(5);
        freq1 = fstart + (i * dF);
        DSP_Measure(freq1, 1, 1, 3);
        rx = DSP_MeasuredZ();

        //phi=DSP_MeasuredPhaseDeg();
        phi1=atan2(cimagf(rx),crealf(rx))*180.0/PI;
/*
        if(phi1>-75.0&&phi1<=-60.0 && !Fq1found) //cimagf(0.0-5.0*I))
        {
            fstart=freq1-i*10;
            dF=10;
            //Sleep(1000);
        }*/
        /*
        if(phi1>-45.0 && !Fq1found) //cimagf(0.0-5.0*I))
        {
            fstart=freq1-i;
            dF=1;
            //Sleep(1000);
        }*/
        if(phi1>=-45.0&&!Fq1found)
        {
            Fq1=freq1;
            XL1=cimagf(rx);
            Fq1found=1;

            fstart=freq1-i*10;
            dF=10;

        sprintf(str, "phi = %.1f°  F1=%.3f",phi1,Fq1/1000.0);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 180, 140, str);
        sprintf(str, "R= %.1f X= %.1f",crealf(rx),cimagf(rx));
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 180, 160, str);

        }

        if(cimagf(rx)>=0.0) //cimagf(0.0))
        {
            Fs=freq1;
            Rs=crealf(rx);
        /*  sprintf(str, "Rs = %.1f Ohm", Rs);
            FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 20, 180, str);
            Sleep(2000);
            FONT_Write(FONT_FRANBIG, BackGrColor, BackGrColor, 20, 180, str);
        */
        }
        if(((cimagf(rx)>=-100.0)&&(cimagf(rx)<-10.0)&& Fq1found)) //cimagf(0.0-5.0*I))
        {
            fstart=freq1-i*10;
            dF=10;
        }
        if(cimagf(rx)>=-10.0) //cimagf(0.0-5.0*I))
        {
            fstart=freq1-i;
            dF=1;
        }
        LCD_SetPixel(LCD_MakePoint(X0 + i/2, 135), LCD_BLUE);// progress line
        //LCD_SetPixel(LCD_MakePoint(X0 + i/2, 136), LCD_BLUE);
        //sprintf(str, "X = %.3f      ", cimagf(rx));
        //FONT_Write(FONT_FRAN, TextColor, BackGrColor, 0, 140, str);
        sprintf(str, "Fs = %.3f     ", freq1/1000.0);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 0, 160, str);
        sprintf(str, "dF = %d  R= %.1f X= %.1f    ", dF,crealf(rx),cimagf(rx));
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 0, 180, str);
        sprintf(str, "i = %d    ", i);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 0, 200, str);
        sprintf(str, "phi = %.1f°    ",phi1);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 60, 200, str);
        if(cimagf(rx)>=0.0) //cimagf(0.0))
            break;
    }
    Sleep(10);
    dF=10;      //fine scanning for Fs
    fstart=Fs-100;
    Fq1found=0;
    Fq2Found=0;
     for(i = 0; i <= 1000; i++)
    {
        Sleep(5);
        freq1 = fstart + (i * dF);
        DSP_Measure(freq1, 1, 1, 3);
        rx = DSP_MeasuredZ();

        //phi=DSP_MeasuredPhaseDeg();
        phi1=atan2(cimagf(rx),crealf(rx))*180.0/PI;
/*
        if(phi1>=30.0) //cimagf(0.0-5.0*I))
        {
            fstart=freq1-i;
            dF=1;
        }
*/
        if(phi1>=20.0) //cimagf(0.0-5.0*I))
        {
            fstart=freq1-i*10;
            dF=10;
        }

        if(phi1>=30.0) //cimagf(0.0-5.0*I))
        {
            fstart=freq1-i;
            dF=1;
        }


        if(phi1>=45.0)
        {
            Fq2=freq1;
            XL2=cimagf(rx);
            sprintf(str, "phi = %.1f°  F2=%.3f",phi1,Fq2/1000.0);
            FONT_Write(FONT_FRAN, TextColor, BackGrColor, 180, 180, str);
            sprintf(str, "R= %.1f X= %.1f",crealf(rx),cimagf(rx));
            FONT_Write(FONT_FRAN, TextColor, BackGrColor, 180, 200, str);
        }

        if(cimagf(rx)>=0.0 && !Fq2Found ) //cimagf(0.0))
        {
            Fs=freq1;
            Fq2Found=1;
        }
        sprintf(str, "X = %.3f      ", cimagf(rx));
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 0, 140, str);
        sprintf(str, "Fs = %.3f     ", freq1/1000.0);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 0, 160, str);
        sprintf(str, "dF = %d  R= %.1f X= %.1f    ", dF,crealf(rx),cimagf(rx));
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 0, 180, str);
        sprintf(str, "i = %d   ", i);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 0, 200, str);
        sprintf(str, "phi = %.1f°  ",phi1);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 60, 200, str);

        if(phi1>=45.0)
            break;
    }

    Sleep(5);

    fstart=Fp-1000;
    //fstart=Fs+1000;
    dF=100;          // coarse scanning for Fp
    freq1 = fstart;
    DSP_Measure(freq1, 1, 1, 1);
    int hiimpednce=0;
    for(i = 0; i <= WWIDTH; i++)
    {
        Sleep(5);
        freq1 = fstart +  (i * dF);//* deltaF;
        DSP_Measure(freq1, 1, 1, 3);
        rx = DSP_MeasuredZ();

        //impedance = cimagf(rx)*cimagf(rx)+crealf(rx)*crealf(rx);
        //impedance=sqrtf(impedance);

        if(cimagf(rx)<=0.0&&crealf(rx)>2000.0) //cimagf(0.0))
        {
            Fp=freq1;
        }
        LCD_SetPixel(LCD_MakePoint(X0 + i/2+200, 135), LCD_BLUE);// progress line
        //LCD_SetPixel(LCD_MakePoint(X0 + i/2+200, 136), LCD_BLUE);
        sprintf(str, "|Z| = %.3f      ", impedance);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 300, 40, str);
        sprintf(str, "Fp = %.3f     ", freq1/1000.0);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 300, 60, str);
        sprintf(str, "dF = %d     ", dF);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 300, 80, str);
        sprintf(str, "i = %d    ", i);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 300, 100, str);
        if(crealf(rx)>100000.0/*impedance>500.0*/&&hiimpednce==0) //cimagf(0.0-5.0*I))
        {
            fstart=freq1-i*10;
            dF=10;
            hiimpednce=1;
        }

        if(cimagf(rx)<=0.0/*&&impedance>5000.0*/) //cimagf(0.0))
            break;
    }
    Sleep(100);

    fstart=Fp-100;
    hiimpednce=0;
    dF=1;      // fine scanning for Fp
    freq1 = fstart;
    DSP_Measure(freq1, 1, 1, 1);
    for(i = 0; i <= WWIDTH; i++)
    {
        Sleep(5);
        freq1 = fstart +  (i * dF);//* deltaF;
        DSP_Measure(freq1, 1, 1, 3);
        rx = DSP_MeasuredZ();

        //impedance = cimagf(rx)*cimagf(rx)+crealf(rx)*crealf(rx);
        //impedance=sqrtf(impedance);

        if(cimagf(rx)<=0.0&&crealf(rx)>2000.0) //cimagf(0.0))
        {
            Fp=freq1;
        }
        LCD_SetPixel(LCD_MakePoint(X0 + i/2+200, 135), LCD_BLUE);// progress line
        LCD_SetPixel(LCD_MakePoint(X0 + i/2+200, 136), LCD_BLUE);
        sprintf(str, "|Z| = %.3f      ", impedance);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 300, 40, str);
        sprintf(str, "Fp = %.3f     ", freq1/1000.0);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 300, 60, str);
        sprintf(str, "dF = %d     ", dF);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 300, 80, str);
        sprintf(str, "i = %d    ", i);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 300, 100, str);

        if(crealf(rx)>100000.0/*impedance>500.0*/&&hiimpednce==0) //cimagf(0.0-5.0*I))
        {
            fstart=freq1-i;
            dF=1;
            hiimpednce=1;
        }

        if(cimagf(rx)<=0.0/*&&impedance>100000.0*/) //cimagf(0.0))
            break;
    }
    //GEN_SetMeasurementFreq(0);
    isMeasured = 1;
    Sleep(1);
}
////////////
uint32_t QuFindPositivX(BANDSPAN bs)
{
uint64_t j,k;
float R, X;
float complex  rx0;
uint32_t FSres,fstart1;
BANDSPAN span_old;

    span_old=span;
    span=bs;
    //f1=1000000;
    //fstart1 = BSVALUES[bs] * 1000;
    for(j=0;j<100;j++) // 100kHz +1MHz step
    {
        fstart1=100000+j*BSVALUES[bs] * 1000;
        f1 = fstart1;
        FONT_Print(FONT_FRAN, TextColor, BackGrColor, 0, 80, "j=%d  ", j);
        FONT_Print(FONT_FRAN, TextColor, BackGrColor, 0, 100, "f1=%d  ", fstart1/1000);
        while(TOUCH_IsPressed());

        ScanRXFast();
        //Sleep(200);
        for(k = 0; k <= WWIDTH; k++)
            {
                FSres = fstart1 + (k * BSVALUES[span] * 1000) / WWIDTH;
                rx0=values[k];

                if(/*crealf(rx0)>10.0&&*/cimagf(rx0)>100.0)
                {
                    span=span_old;
                    FONT_Print(FONT_FRAN, TextColor, BackGrColor, 0, 200, "Fr %d  ", FSres/1000);
                    Sleep(20);
                    FONT_Print(FONT_FRAN, BackGrColor, BackGrColor, 0, 200, "Fr %d  ", FSres/1000);
                    Q_Fs_find=1;
                    break;//return(FSres);
                }
            }
            if(Q_Fs_find)  // find fine
                {
                    fstart1=FSres-30000;
                    f1 = fstart1;
                    span=BS40;
                    ScanRXFast();
                    for(k = 0; k <= WWIDTH; k++)
                        {
                            FSres = fstart1 + (k * BSVALUES[span] * 1000) / WWIDTH;
                            rx0=values[k];

                            if(/*crealf(rx0)>10.0&&*/cimagf(rx0)>10.0)
                            {
                                span=span_old;
                                FONT_Print(FONT_FRAN, TextColor, BackGrColor, 0, 200, "Fr %d  ", FSres/1000);
                                Sleep(2000);
                                FONT_Print(FONT_FRAN, BackGrColor, BackGrColor, 0, 200, "Fr %d  ", FSres/1000);
                                Q_Fs_find=1;
                                return(FSres);
                            }
                        }
                }



        FONT_Print(FONT_FRAN, TextColor, BackGrColor, 0, 140, "F=%d  ", FSres/1000);
    }
    span=span_old;
}

void QuMeasure(void){
//int i;
char str[100];
    Fs=Fp=Fq1=Fq2=0;

    if(sFreq==0) {
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 10, 100, "First set estimated Frequency ");
        Sleep(20);
        return;
    }
    else FONT_Write(FONT_FRAN, BackGrColor, BackGrColor, 10, 100, "First set estimated Frequency ");

    if(sCalib==0) {
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 10, 100, "First do Calibration without quartz");
        Sleep(20);
        return;
    }
    else    FONT_Write(FONT_FRAN, BackGrColor, BackGrColor, 10, 100, "First do Calibration without quartz");
    LCD_FillRect(LCD_MakePoint(0,65),LCD_MakePoint(479,215),BackGrColor);
    if(!Q_Fs_find)
        f1=(QuFindPositivX(BS400)/1000)*1000-8000;//1000)*1000;
        //f1=(QuFindBigR(BS2M)/1000)*1000-5000;//1000)*1000;
    //span=BS40;
    ScanRX_QuFast();
    Cp=Get_C()-C0;
    //Cp-=C0;
    grType = GRAPH_VSWR_Z;
    RedrawWindow();

    sprintf(str, "Fs = %.3f  ", Fs/1000.0);
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 40, 60, str);
    sprintf(str, "Fp = %.3f  ", Fp/1000.0);
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 300, 60, str);

    //f1=((Fs/1000)-1)*1000;
    ScanFsFp();
    //Cp=Get_C()-C0;
    //Cp-=C0;
    LCD_FillAll(BackGrColor);
    FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 150, 0, "Quartz Data ");
    sprintf(str, "Fs = %.3f  ", Fs/1000.0);
    FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 20, 60, str);
    sprintf(str, "Fp = %.3f  ", Fp/1000.0);
    FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 240, 60, str);
    if(Fs!=0){
        sprintf(str, "Cp = %.2f pF ", 1e12*Cp);
        FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 240, 100, str);

        if(Fp!=0){
            Ls1=QuMeasLs(0);
            Ls2=(XL2-XL1)/(4*PI*(Fq2-Fq1));  //dX/dF at +-45°
            //Cs=2.0f*Cp*(Fp-Fs)/Fs;
            Cs=1.0/(Ls1*39.478f*Fs*Fs);
            sprintf(str, "Cs = %.4f pF ", 1e12*Cs);
            FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 20, 100, str);
            Ls=1/(Cs*39.478f*Fs*Fs);
            sprintf(str, "Ls1 = %.1f mH    ", 1e3*Ls1);
            FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 20, 140, str);
            sprintf(str, "Ls2 = %.1f mH    ", 1e3*Ls2);
            FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 240, 140, str);
            sprintf(str, "Rs = %.1f Ohm", Rs);
            FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 20, 180, str);
            //Q=(1/Rs)*sqrtf(Ls1/Cs);
            Q=2*PI*Fs*Ls1/Rs;
            sprintf(str, "Q = %.0f   ", Q);
            FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 240, 180, str);
            //sprintf(str, "Ls1 = %.1f mH ", 1e3*Ls1);
            //FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 240, 140, str);
        }
    }
    Sleep(10);
    TEXTBOX_InitContext(&Quartz_ctx);
    TEXTBOX_Append(&Quartz_ctx, (TEXTBOX_t*)tb_menuQuartz2);
    TEXTBOX_DrawContext(&Quartz_ctx);
    //while(!TOUCH_IsPressed());
   // rqExitSWR=true;

    QuAddToFile();

}
void QuSetFequency(void)
{
    //fxs=CFG_GetParam(CFG_PARAM_PAN_F1);
    fxkHzs=fxs/1000;

    if(SetFreqKBD(CFG_PARAM_MEAS_F,&fxkHzs, &span))
    {
    fxs=CFG_GetParam(CFG_PARAM_MEAS_F);
    }
    f1=fxs;
    //FG_Flush();
    ShowMeasFr();
    sFreq=1;
    Q_Fs_find=1;
}


void Quartz_proc(void){
int screen=0;
char str[200];
uint32_t width, vswLog=0;
//uint32_t k;
//float Cs,Ls,Q;

    qu_num=0;
    strcpy (filebuffer,"\n--------------------------------begin---------------------------------------\n");
   /* strcat (filebuffer,"1---------------------------------------------------------------------------\n");
    strcat (filebuffer,"2---------------------------------------------------------------------------\n");
    strcat (filebuffer,"3---------------------------------------------------------------------------\n");
    strcat (filebuffer,"4---------------------------------------------------------------------------\n");
    strcat (filebuffer,"5---------------------------------------------------------------------------\n");
    strcat (filebuffer,"6---------------------------------------------------------------------------\n");
    strcat (filebuffer,"7---------------------------------------------------------------------------\n");
    strcat (filebuffer,"8---------------------------------------------------------------------------\n");
    strcat (filebuffer,"9---------------------------------------------------------------------------\n");
    strcat (filebuffer,"10--------------------------------------------------------------------------\n");*/
    sprintf(str, "Nr    Fs        Fp        Q         Ls          Cs          Rs          Cp \n");
    strcat (filebuffer,str);
    f1=200000;
    Q_Fs_find=0;
    span=BS40;
    sFreq=1;
    sCalib=0;
    freqChg=0;
    rqExitSWR=false;
    SetColours();
    LCD_FillAll(BackGrColor);
    FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 150, 0, "Quartz Data ");
    //Sleep(1000);
    while(TOUCH_IsPressed());
    fxs=CFG_GetParam(CFG_PARAM_MEAS_F);
    fxkHzs=fxs/1000;
    ShowMeasFr();
    TEXTBOX_InitContext(&Quartz_ctx);

//HW calibration menu
    TEXTBOX_Append(&Quartz_ctx, (TEXTBOX_t*)tb_menuQuartz);
    TEXTBOX_DrawContext(&Quartz_ctx);
for(;;)
    {
        if (TOUCH_Poll(&pt))
        {
            if((pt.x>160)&&(pt.x<320)&&(pt.y<50)&&isMeasured==1)
                {
                    if(screen==0)
                    {
                        screen=1;
                        //f1=CFG_GetParam(CFG_PARAM_MEAS_F);
                        grType = GRAPH_VSWR_Z;
                        RedrawWindow();

                        sprintf(str, "Fs = %.3f", Fs/1000.0);
                        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 20, 180, str);
                        sprintf(str, "Fp = %.3f     ", Fp/1000.0);
                        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 350, 0, str);
                        sprintf(str, "F1=%.3f F2=%.3f",Fq1/1000.0,Fq2/1000.0);
                        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 120, 200, str);
/*
                        sprintf(str, "phi = %.1f°  F1=%.3f",phi1,Fq1/1000.0);
                        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 180, 140, str);

                        sprintf(str, "phi = %.1f°  F2=%.3f",phi1,Fq2/1000.0);
                        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 180, 180, str);
*/
                    }
                    else
                    {
                        screen=0;
                        LCD_FillAll(BackGrColor);// LCD_BLACK WK
                        FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 150, 0, "Quartz Data ");
                        sprintf(str, "Fs = %.3f  ", Fs/1000.0);
                        FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 20, 60, str);
                        sprintf(str, "Fp = %.3f  ", Fp/1000.0);
                        FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 240, 60, str);
                        if(Fs!=0)
                            {
                            sprintf(str, "Cp = %.2f pF ", 1e12*Cp);
                            FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 240, 100, str);

                            if(Fp!=0)
                                {
                                sprintf(str, "Cs = %.4f pF ", 1e12*Cs);
                                FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 20, 100, str);
                                sprintf(str, "Ls1 = %.1f mH    ", 1e3*Ls1);
                                FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 20, 140, str);
                                sprintf(str, "Ls2 = %.1f mH    ", 1e3*Ls2);
                                FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 240, 140, str);
                                sprintf(str, "Rs = %.1f Ohm", Rs);
                                FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 20, 180, str);
                                sprintf(str, "Q = %.0f   ", Q);
                                FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 240, 180, str);
                                }
                            }
                        FONT_Print(FONT_FRANBIG, TextColor, BackGrColor, 0, 0, "Qu %d  ", qu_num);
                        FONT_Print(FONT_FRAN, TextColor, BackGrColor, 0, 30, "lenght: %d   ",strlen(filebuffer));
                        TEXTBOX_DrawContext(&Quartz_ctx);
                    }
                while(TOUCH_IsPressed());
                }
        }
        //Sleep(0); //for autosleep to work
        if (TEXTBOX_HitTest(&Quartz_ctx))
        {
            if (rqExitSWR)
            {
                rqExitSWR=false;
                return;
            }
            if(freqChg==1)
            {
                ShowMeasFr();
                freqChg=0;
            }
            // Sleep(50);
        }
        Sleep(20);
    }
}

TEXTBOX_CTX_t MeasCx_ctx;
void C0Calibrate(void){
    sCalib=0;
    //CxGet();//only compute C0
    ///////////////
char str[100];
float complex rx0;
uint32_t freq1;

    freq1=CFG_GetParam(CFG_PARAM_MEAS_F);;
    span=BS2;
    DSP_Measure(freq1, 1, 1, 3); //Fake initial run to let the circuit stabilize
    rx0 = DSP_MeasuredZ();
    Sleep(20);

    DSP_Measure(freq1, 1, 1, 5);

    rx0 = DSP_MeasuredZ();

float r= fabsf(crealf(rx0));//calculate Cx
float im= cimagf(rx0);
 /*   if(im=0){
        Cp=0;
        Lp=0;
        return;
    }*/

float xp=1.0f;
    if(im*im>0.0025)
        xp=im+r*(r/im);// else xp=im=-10000.0f;// ??

    if(sCalib==0)
        {
            C0=-1/( 6.2832 *freq1* xp)-(1*1e-14);
            sCalib=1;
        }
    else
        {
            Cx=-1/( 6.2832 *freq1* xp);
        if(Cx>C0) //Capacitance
            {
                Cx=-1/( 6.2832 *freq1* xp)-C0;
                Lp=0.0;
            }
        else//xp>0 Inductance
            {
                Cx=0.0;
                Lp=xp/( 6.2832 *freq1)+C0;
            }
        }
    sprintf(str, "Craw = %.2f pF     ", 1e12*-1/( 6.2832 *freq1* xp));
    //FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 200, 80, str);

    Sleep(20);

    //FONT_Write(FONT_FRAN, LCD_RED, LCD_BLACK, 400, 0, "     ");
    GEN_SetMeasurementFreq(0);
    isMeasured = 1;
    //CxGet();//
    //////////////

    //C0=Cp;
    sprintf(str, "Co = %.2f pF", 1e12*C0);
    //FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 200, 120, str);
    //sCalib=1;
    //LCD_FillRect(LCD_MakePoint(10,180),LCD_MakePoint(440,215),BackGrColor);
    //Sleep(2000);
    //TEXTBOX_InitContext(&MeasCx_ctx);
    //TEXTBOX_Append(&MeasCx_ctx, (TEXTBOX_t*)tb_menuMeasCx);
    //TEXTBOX_DrawContext(&MeasCx_ctx);
    while(TOUCH_IsPressed());
}

void CxMeasure(void){
int i;
char str[100];       //////  Е тука таа дължина  на масива беше 10 и ебаваше майката на стека STACK
float complex rx0;
float impedance,R,X,phi;

uint32_t freq1;

    freq1=CFG_GetParam(CFG_PARAM_MEAS_F);
    //span=BS2;

    DSP_Measure(freq1, 1, 1, 3); //Fake initial run to let the circuit stabilize
    rx0 = DSP_MeasuredZ();
    Sleep(20);

    DSP_Measure(freq1, 1, 1, 5);

    rx0 = DSP_MeasuredZ();

float r= fabsf(crealf(rx0));//calculate Cx
float im= cimagf(rx0);
 /*   if(im=0){
        Cp=0;
        Lp=0;
        return;
    }*/

float xp=1.0f;
    if(im*im>0.0025)
        xp=im+r*(r/im);// else xp=im=-10000.0f;// ??

    if(sCalib==0)
        {
            C0=-1/( 6.2832 *freq1* xp);
            sCalib=1;
        }
    else
        {
            Cx=-1/( 6.2832 *freq1* xp);
        if(Cx>C0) //Capacitance
            {
                Cx=-1/( 6.2832 *freq1* xp)-C0;
                if(Cx<0)
                     Cx=0;
                Lp=0.0;
            }
        else//xp>0 Inductance
            {
                Cx=0.0;
                Lp=xp/( 6.2832 *freq1)+C0;
                if(Lp<0|Lp>1)   //  0--1000 mH
                    Lp=0;
            }
        }
    impedance = cimagf(rx0)*cimagf(rx0)+crealf(rx0)*crealf(rx0);
    impedance=sqrtf(impedance);
    //phi=DSP_MeasuredPhaseDeg();
    phi=atan2(cimagf(rx0),crealf(rx0))*180.0/PI;

    Q=0.0;
    R = crealf(rx0);
    X = cimagf(rx0);
    if(R!=0&&X>0.0)
        Q=X/R;

    FONT_Print(FONT_FRANBIG, TextColor, BackGrColor, 5, 155, "Cx = %.2f pF", 1e12*Cx);
    FONT_Print(FONT_FRANBIG, TextColor, BackGrColor, 5, 195, "Lx = %.2f uH  Q = %.1f ", 1e6*Lp,Q);

    FONT_Print(FONT_FRANBIG, TextColor, BackGrColor, 5, 115, "|Z| = %.1f ohm Phi = %.1f°",impedance,phi);
    FONT_Print(FONT_FRANBIG, TextColor, BackGrColor, 5, 75, "R = %.1f ohm X = %.1f ohm",R,X);
}


void CxMeas_Proc(void){
uint32_t width, vswLog=0;
uint32_t k;

    //if(span==0)
    //span=BS2;// +- 500 kHz
    sFreq=0;
    sCalib=0;
    freqChg=0;
    rqExitSWR=false;
    //SetColours();
    LCD_FillAll(BackGrColor);
    FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 145, 0, "Measure Cx Lx ");
   // Sleep(2000);
    while(TOUCH_IsPressed());
    fxs=CFG_GetParam(CFG_PARAM_MEAS_F);
    fxkHzs=fxs/1000;
    //C0Calibrate();
    ShowMeasFr();
   //Sleep(2000);
    TEXTBOX_InitContext(&MeasCx_ctx);
    //Sleep(50);
//HW calibration menu
    TEXTBOX_Append(&MeasCx_ctx, (TEXTBOX_t*)tb_menuMeasCx);
    //Sleep(50);
    TEXTBOX_DrawContext(&MeasCx_ctx);
    //Sleep(5000);
    LCD_SetLayer_0();

    LCD_FillAll(BackGrColor);
    //Sleep(1000);
    FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 145, 0, "Measure Cx Lx ");
    ShowMeasFr();
    TEXTBOX_DrawContext(&MeasCx_ctx);
    //Sleep(2000);
    LCD_SetLayer_1();
    FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 145, 0, "Measure Cx Lx ");
    ShowMeasFr();
    TEXTBOX_DrawContext(&MeasCx_ctx);
for(;;)
    {
        if(BSP_LCD_GetActiveLayer())
            LCD_SetLayer_0();
        else
            LCD_SetLayer_1();
        LCD_FillRect(LCD_MakePoint(0,60),LCD_MakePoint(479,230),BackGrColor);

        Sleep(0); //for autosleep to work
        if (TEXTBOX_HitTest(&MeasCx_ctx))
            {
                if (rqExitSWR)
                    {
                        rqExitSWR=false;
                        //LCD_FillAll(BackGrColor);
                        while (TOUCH_IsPressed());
                        //Sleep(100);
                        LCD_SetLayer_0();
                        LCD_FillAll(BackGrColor);
                        LCD_SetLayer_1();
                        LCD_FillAll(BackGrColor);
                        LCD_ResetLayer();
                        return;
                    }
                fxs=CFG_GetParam(CFG_PARAM_MEAS_F);
                fxkHzs=fxs/1000;
            }
        if(sCalib==1)
        {
            CxMeasure();
        }
        ShowMeasFr();
        Sleep(20);

    }
}

