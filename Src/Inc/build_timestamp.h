#ifndef BUILD_TIMESTAMP 
#warning Mercurial failed. Repository not found. Firmware revision will not be generated. 
#define HGREV N/A 
#define BUILD_TIMESTAMP "2020-02-26 10:17 UT"
#define HGREVSTR(s) stringify_(s) 
#define stringify_(s) #s 
#endif 
