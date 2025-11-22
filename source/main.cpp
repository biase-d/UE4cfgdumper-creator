#include <switch.h>
#include <cstdio>

#include "tui_manager.hpp" 

int main(int argc, char* argv[])
{
    consoleInit(NULL);
    printf("\x1b[?25l");
    consoleUpdate(NULL);
    
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    if (appletGetAppletType() == AppletType_Application) {
        enterManageMode();
    } else {
        runDefaultScan();
    }
    
    consoleExit(NULL);
    return 0;
}