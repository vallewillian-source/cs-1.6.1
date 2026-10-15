#ifndef CSMOD_HOST_SERVER_H
#define CSMOD_HOST_SERVER_H
class CCSBot;
void CSModHost_Init();
void CSModHost_Frame();
void CSModHost_Round();
void CSModHost_Bot(CCSBot *bot);
void CSModHost_MapEnd();
void CSModHost_Shutdown();
#endif
