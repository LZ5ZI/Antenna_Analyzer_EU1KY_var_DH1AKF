

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
            Cp=-1/( 6.2832 *freq1* xp);
        if(Cp>C0) //Capacitance
            {
                Cp=-1/( 6.2832 *freq1* xp)-C0;
                Lp=0.0;
            }
        else//xp>0 Inductance
            {
                Cp=0.0;
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
            Cp=-1/( 6.2832 *freq1* xp);
        if(Cp>C0) //Capacitance
            {
                Cp=-1/( 6.2832 *freq1* xp)-C0;
                if(Cp<0)
                     Cp=0;
                Lp=0.0;
            }
        else//xp>0 Inductance
            {
                Cp=0.0;
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

    FONT_Print(FONT_FRANBIG, TextColor, BackGrColor, 5, 155, "Cx = %.2f pF", 1e12*Cp);
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
                        LCD_ResetLayer();
                        LCD_FillAll(BackGrColor);
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

void Quartz_proc1(void){    // LZ5ZI
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
