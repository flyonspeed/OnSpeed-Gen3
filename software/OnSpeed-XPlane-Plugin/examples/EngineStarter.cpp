// Downloaded from https://developer.x-plane.com/code-sample/enginestarter/


/*
	Engine Starter example
	Written by Sandy Barbour - 28/09/2005
	
	This examples shows how to start the engines.
	It also shows some neat widgetry
*/

#include "XPLMPlugin.h"
#include "XPLMUtilities.h"
#include "XPLMProcessing.h"
#include "XPLMDataAccess.h"
#include "XPLMMenus.h"
#include "XPWidgets.h"
#include "XPStandardWidgets.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#if IBM
#include <windows.h>
#endif

// Limit to 8 engines, any more and you are on your own :-)
#define MAX_NUMBER_ENGINES 8

// Enums for each engine
typedef struct _MESSAGE_STRUCT
{
	int MessageEnum;
} MESSAGE_STRUCT;

MESSAGE_STRUCT JoystickCommands[MAX_NUMBER_ENGINES] =
{
	{xplm_joy_start_0},
	{xplm_joy_start_1},
	{xplm_joy_start_2},
	{xplm_joy_start_3},
	{xplm_joy_start_4},
	{xplm_joy_start_5},
	{xplm_joy_start_6},
	{xplm_joy_start_7}
};

// These will hold the XPLMDataRef values	
static XPLMDataRef gIgnitersDataRef = NULL;
static XPLMDataRef gStarterDurationDataRef = NULL;
static XPLMDataRef gNumberOfEnginesDataRef = NULL;

// Descriptions for the Input Text widget
static char DataRefDesc[MAX_NUMBER_ENGINES][20] = {"1", "2", "3", "4", "5", "6", "7", "8"};

static float EngineStarterLoopCB(float elapsedMe, float elapsedSim, int counter, void * refcon);;

static int MenuItem1;
// Widgets
static XPWidgetID	EngineStarterWidget = NULL, EngineStarterWindow = NULL;
static XPWidgetID	Description[MAX_NUMBER_ENGINES] = {NULL};
static XPWidgetID	IgnitersState[MAX_NUMBER_ENGINES] = {NULL};
static XPWidgetID	StartButton[MAX_NUMBER_ENGINES] = {NULL};
static XPWidgetID	XPlaneStarterDurationEdit[MAX_NUMBER_ENGINES] = {NULL};
static XPWidgetID	StarterDurationText = NULL;
static XPWidgetID	StarterDurationEdit = NULL;
static XPWidgetID	ReleaseDelayText = NULL;
static XPWidgetID	ReleaseDelayEdit = NULL;

// Globals for engine starting.
static int Igniters[MAX_NUMBER_ENGINES] = {0};
static int ElapsedTime[MAX_NUMBER_ENGINES] = {0};
static int EngineStarting[MAX_NUMBER_ENGINES] = {0};

// Intitial window size
static int x = 300, y = 500, w = 390, h = 290;

// Callback prototypes
static void EngineStarterMenuHandler(void *, void *);

static void CreateEngineStarterWidget(int x1, int y1, int w, int h);

static int EngineStarterHandler(
						XPWidgetMessage			inMessage,
						XPWidgetID				inWidget,
						intptr_t				inParam1,
						intptr_t				inParam2);

static void ResizeWidgets(int NumberOfEngines);

// Standard Plugin callbacks
PLUGIN_API int XPluginStart(
						char *		outName,
						char *		outSig,
						char *		outDesc)
{
	XPLMMenuID	Id;
	int			Item;

	strcpy(outName, "EngineStarter");
	strcpy(outSig, "xpsdk.examples.EngineStarter");
	strcpy(outDesc, "A plug-in that handles data Input/Output.");

	// Create our menu
	Item = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "Engine Starter", NULL, 1);
	Id = XPLMCreateMenu("Engine Starter", XPLMFindPluginsMenu(), Item, EngineStarterMenuHandler, NULL);
	XPLMAppendMenuItem(Id, "Start Engines", (void *)"EngineStarter", 1);

	// Flag to tell us if the widget is being displayed.
	MenuItem1 = 0;


	// Get our dataref handles here
	gIgnitersDataRef = XPLMFindDataRef("sim/cockpit/engine/igniters_on");
	gStarterDurationDataRef = XPLMFindDataRef("sim/cockpit/engine/starter_duration");
	gNumberOfEnginesDataRef = XPLMFindDataRef("sim/aircraft/engine/acf_num_engines");


	// Register our FL callback with initial callback time period of 1 second.
	XPLMRegisterFlightLoopCallback(EngineStarterLoopCB, 1.0, NULL);

	return 1;
}

PLUGIN_API void	XPluginStop(void)
{
	/* Unregister the callback */
	XPLMUnregisterFlightLoopCallback(EngineStarterLoopCB, NULL);

	if (MenuItem1 == 1)
	{
		XPDestroyWidget(EngineStarterWidget, 1);
		MenuItem1 = 0;
	}
}

PLUGIN_API int XPluginEnable(void)
{
	return 1;
}

PLUGIN_API void XPluginDisable(void)
{
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFrom, int inMsg, void * inParam)
{
    if (inFrom == XPLM_PLUGIN_XPLANE)
    {
		switch(inMsg)
		{
			case XPLM_MSG_PLANE_LOADED:
				// Need to resize widgets if number of engines change.
				// This messge is received on XPlane start so that is covered.
				int NumberOfEngines = XPLMGetDatai(gNumberOfEnginesDataRef);
				ResizeWidgets(NumberOfEngines);
				break;
		}
	}
}


float EngineStarterLoopCB(float elapsedMe, float elapsedSim, int counter, void * refcon)
{
	int Item;
	int count;
	char Buffer[255];
	float StarterDurationArray[MAX_NUMBER_ENGINES];

	if (MenuItem1 == 0) // Don't process if widget not visible
		return 1;

	// Only deal with the actual engines that we have
	int NumberOfEngines = XPLMGetDatai(gNumberOfEnginesDataRef);

	// Get our Igniters states for each engine
	count = XPLMGetDatavi(gIgnitersDataRef, Igniters, 0, MAX_NUMBER_ENGINES);

	// Get Xplane starter duration for each engine
	count = XPLMGetDatavf(gStarterDurationDataRef, StarterDurationArray, 0, MAX_NUMBER_ENGINES);

	// Uncheck all Igniters check boxes, need this if user load an aircraft
	// with less engines than the current aircraft
	for (Item=0; Item<MAX_NUMBER_ENGINES; Item++)
		XPSetWidgetProperty(IgnitersState[Item], xpProperty_ButtonState, 0);

	// Process each actual engine
	for (Item=0; Item<NumberOfEngines; Item++)
	{
		if (Igniters[Item] == 1)
			XPSetWidgetProperty(IgnitersState[Item], xpProperty_ButtonState, 1);
		else
			XPSetWidgetProperty(IgnitersState[Item], xpProperty_ButtonState, 0);

		sprintf(Buffer, "%f", StarterDurationArray[Item]);
		XPSetWidgetDescriptor(XPlaneStarterDurationEdit[Item], Buffer);
	}

	// I have used extra braces for clarity.
	// Store elapsed start time for each engine
	for (Item=0; Item<NumberOfEngines; Item++)
	{
		if (EngineStarting[Item] == 1)
			ElapsedTime[Item]++;
	}

	// I have used extra braces for clarity.
	// If the time for that engine has elapsed then send release
	XPGetWidgetDescriptor(ReleaseDelayEdit, Buffer, sizeof(Buffer));
	int ReleaseDelay = atoi(Buffer);

	for (Item=0; Item<NumberOfEngines; Item++)
	{
		if (EngineStarting[Item] == 1)
		{
			if (ElapsedTime[Item] > ReleaseDelay)
			{
				XPLMCommandButtonRelease(JoystickCommands[Item].MessageEnum);
				XPSetWidgetProperty(StartButton[Item], xpProperty_Enabled, 1);
			}
		}
	}

	// This means call us ever 1 second.
	return (float) 1.0;
}

void EngineStarterMenuHandler(void * mRef, void * iRef)
{

	// If menu selected create our widget dialog
	if (!strcmp((char *) iRef, "EngineStarter"))
	{
		if (MenuItem1 == 0)
		{
			// Take care of resize based on actual number of engines
			int NumberOfEngines = XPLMGetDatai(gNumberOfEnginesDataRef);
			CreateEngineStarterWidget(x, y, w, h);
			ResizeWidgets(NumberOfEngines);
			MenuItem1 = 1;
		}
		else
			if(!XPIsWidgetVisible(EngineStarterWidget))
				XPShowWidget(EngineStarterWidget);
	}
}						

// This will create our widget dialog.
// I have made all child widgets relative to the input paramter.
// This makes it easy to position the dialog
// Any position or size changes need to be done in the "ResizeWidgets" function as well.
// This could be made more manageable if required
void CreateEngineStarterWidget(int x, int y, int w, int h)
{
	int Item;

	int x2 = x + w;
	int y2 = y - h;
	
	// Create the Main Widget window
	EngineStarterWidget = XPCreateWidget(x, y, x2, y2,
					1,	// Visible
					"Engine Starter Example by Sandy Barbour",	// desc
					1,		// root
					NULL,	// no container
					xpWidgetClass_MainWindow);

	// Add Close Box decorations to the Main Widget
	XPSetWidgetProperty(EngineStarterWidget, xpProperty_MainWindowHasCloseBoxes, 1);

	// Create the Sub Widget window
	EngineStarterWindow = XPCreateWidget(x+10, y-30, x2-10, y2+10,
					1,	// Visible
					"",	// desc
					0,		// root
					EngineStarterWidget,
					xpWidgetClass_SubWindow);

	// Set the style to sub window
	XPSetWidgetProperty(EngineStarterWindow, xpProperty_SubWindowType, xpSubWindowStyle_SubWindow);

	// For each engine
	for (Item=0; Item<MAX_NUMBER_ENGINES; Item++)
	{
		// Create a text widget
		Description[Item] = XPCreateWidget(x+20, y-(40 + (Item*30)), x+82, y-(62 + (Item*30)),
							1,	// Visible
							DataRefDesc[Item],// desc
							0,		// root
							EngineStarterWidget,
							xpWidgetClass_Caption);

		// Create a check box for the Igniters
		IgnitersState[Item] = XPCreateWidget(x+45, y-(40 + (Item*30)), x+67, y-(62 + (Item*30)),
							1, "", 0, EngineStarterWidget,
							xpWidgetClass_Button);

		// Set it to be check box
		XPSetWidgetProperty(IgnitersState[Item], xpProperty_ButtonType, xpRadioButton);
		XPSetWidgetProperty(IgnitersState[Item], xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox);
		XPSetWidgetProperty(IgnitersState[Item], xpProperty_ButtonState, 1);

		// Create a button widget for the Start Engine
		StartButton[Item] = XPCreateWidget(x+80, y-(40 + (Item*30)), x+130, y-(62 + (Item*30)),
							1, " Start", 0, EngineStarterWidget,
							xpWidgetClass_Button);

		// Set it to be normal push button
		XPSetWidgetProperty(StartButton[Item], xpProperty_ButtonType, xpPushButton);

		// Create an edit widget for the xplane starter duration
		XPlaneStarterDurationEdit[Item] = XPCreateWidget(x+140, y-(40 + (Item*30)), x+200, y-(62 + (Item*30)),
							1, "", 0, EngineStarterWidget,
							xpWidgetClass_TextField);

	}

	// Create a text widget for the starter duration
	StarterDurationText = XPCreateWidget(x+210, y-40, x+310, y-62,
							1,	// Visible
							"Starter Duration",// desc
							0,		// root
							EngineStarterWidget,
							xpWidgetClass_Caption);

	// Create an edit widget for the starter duration
	StarterDurationEdit = XPCreateWidget(x+315, y-40, x+355, y-62,
						1, "10", 0, EngineStarterWidget,
						xpWidgetClass_TextField);

	// Create a text widget for the release delay
	ReleaseDelayText = XPCreateWidget(x+210, y-70, x+310, y-92,
							1,	// Visible
							"Release Delay",// desc
							0,		// root
							EngineStarterWidget,
							xpWidgetClass_Caption);

	// Set it to be text entry
	XPSetWidgetProperty(StarterDurationEdit, xpProperty_TextFieldType, xpTextEntryField);

	// Create an edit widget for the release delay
	ReleaseDelayEdit = XPCreateWidget(x+315, y-70, x+355, y-92,
						1, "10", 0, EngineStarterWidget,
						xpWidgetClass_TextField);

	// Set it to be text entry
	XPSetWidgetProperty(ReleaseDelayEdit, xpProperty_TextFieldType, xpTextEntryField);

	// Register our widget handler
	XPAddWidgetCallback(EngineStarterWidget, EngineStarterHandler);
}

// Included this as it shows how to maintain a dynamic widget
// Any position or size changes need to be done in the "CreateEngineStarterWidget" function as well.
// This could be made more manageable if required
void ResizeWidgets(int NumberOfEngines)
{
	int Item;

	// Adjust main Widget height
	switch (NumberOfEngines)
	{
		case 1:
		case 2:
			h = 110;
			break;
		case 4:
			h = 170;
			break;
		case 8:
			h = 290;
			break;
	}
	int x2 = x + w;
	int y2 = y - h;

	// Only set or enable widgets that are actually needed
	for (Item=0; Item<MAX_NUMBER_ENGINES; Item++)
	{
		XPSetWidgetProperty(IgnitersState[Item], xpProperty_ButtonState, 0);
		XPSetWidgetDescriptor(Description[Item], "");
		XPHideWidget(Description[Item]);
		XPHideWidget(StartButton[Item]);
		XPHideWidget(IgnitersState[Item]);
		XPHideWidget(XPlaneStarterDurationEdit[Item]);
	}

	// Resize the main widget and window
	XPSetWidgetGeometry(EngineStarterWidget, x, y, x2, y2);
	XPSetWidgetGeometry(EngineStarterWindow, x+10, y-30, x2-10, y2+10);

	
	// For each engine widget do a resize
	// Then make sure each widget is shown.
	for (Item=0; Item<NumberOfEngines; Item++)
	{
		XPSetWidgetGeometry(Description[Item], x+20, y-(40 + (Item*30)), x+82, y-(62 + (Item*30)));
		XPSetWidgetGeometry(IgnitersState[Item], x+45, y-(40 + (Item*30)), x+67, y-(62 + (Item*30)));
		XPSetWidgetGeometry(StartButton[Item], x+80, y-(40 + (Item*30)), x+130, y-(62 + (Item*30)));
		XPSetWidgetGeometry(XPlaneStarterDurationEdit[Item], x+140, y-(40 + (Item*30)), x+200, y-(62 + (Item*30)));
		XPSetWidgetDescriptor(Description[Item], DataRefDesc[Item]);
		XPShowWidget(Description[Item]);
		XPShowWidget(StartButton[Item]);
		XPShowWidget(IgnitersState[Item]);
		XPShowWidget(XPlaneStarterDurationEdit[Item]);
	}

	// No take care of the rest
	XPSetWidgetGeometry(StarterDurationText, x+210, y-40, x+310, y-62);
	XPSetWidgetGeometry(StarterDurationEdit, x+315, y-40, x+355, y-62);
	XPSetWidgetGeometry(ReleaseDelayText, x+210, y-70, x+310, y-92);
	XPSetWidgetGeometry(ReleaseDelayEdit, x+315, y-70, x+355, y-92);
}

// This is the handler for our widget
// It can be used to process button presses etc.
// In this example we are only interested when the close box is pressed
int	EngineStarterHandler(
						XPWidgetMessage			inMessage,
						XPWidgetID				inWidget,
						intptr_t				inParam1,
						intptr_t				inParam2)
{
	char Buffer[255];
	int Item, State;
	float StarterDuration, StarterDurationArray[MAX_NUMBER_ENGINES];

	if (inMessage == xpMessage_CloseButtonPushed)
	{
		if (MenuItem1 == 1)
		{
			XPHideWidget(EngineStarterWidget);
		}
		return 1;
	}

	// Handle any button pushes
	if (inMessage == xpMsg_PushButtonPressed)
	{
		// This is redundant in V8.
		// It will work in V7 but my other method can be used
		// which reduces the code size.
		XPGetWidgetDescriptor(StarterDurationEdit, Buffer, sizeof(Buffer));
		StarterDuration = (float)atof(Buffer);

		// Set all starter durations in XPlane
		for (Item=0; Item<MAX_NUMBER_ENGINES; Item++)
			StarterDurationArray[Item] = StarterDuration;

		XPLMSetDatavf(gStarterDurationDataRef, StarterDurationArray, 0, MAX_NUMBER_ENGINES);

		for (Item=0; Item<MAX_NUMBER_ENGINES; Item++)
		{
			// Handle if the Start Button is pushed
			if (inParam1 == (intptr_t)StartButton[Item])
			{
				// Do the actual joystick button press
				XPLMCommandButtonPress(JoystickCommands[Item].MessageEnum);
				// Set flag for the engine selected
				EngineStarting[Item] = 1;
				// Reset the timer for that engine
				ElapsedTime[Item] = 0;
				// Disable the start button for that engine
				XPSetWidgetProperty(StartButton[Item], xpProperty_Enabled, 0);
			}
		}
		return 1;
	}

	// Handle any check box selections
	if (inMessage == xpMsg_ButtonStateChanged)
	{
		// Get the Igniters check box for each engine.
		for (Item=0; Item<MAX_NUMBER_ENGINES; Item++)
		{
			State = (int) XPGetWidgetProperty(IgnitersState[Item], xpProperty_ButtonState, 0);
			Igniters[Item] = State;
		}
		// This will switch the igniter on/off depeding on State.
		XPLMSetDatavi(gIgnitersDataRef, Igniters, 0, MAX_NUMBER_ENGINES);
		return 1;
	}

	return 0;
}

