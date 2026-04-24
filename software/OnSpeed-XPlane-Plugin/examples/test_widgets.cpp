/*
	TestWidgets Example

	Written by Sandy Barbour - 21/02/2003
	Modified by Sandy Barbour - 08/12/2009 // How time flies when you are having fun. :-)
	Combined example plugin with the Custom Widget creation code.

	This example shows how to create 2 custom widgets.
	These are then used by the example plugin.
*/

#if IBM
#include <windows.h>
#endif
#if LIN
#include <GL/gl.h>
#else
#if __GNUC__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif

#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMProcessing.h"
#include "XPLMDataAccess.h"
#include "XPLMMenus.h"
#include "XPLMUtilities.h"
#include "XPWidgets.h"
#include "XPStandardWidgets.h"
#include "XPLMCamera.h"
#include "XPUIGraphics.h"
#include "XPWidgetUtils.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <vector>
#include <string>

/*------------------------------------------------------------------------*/

/*
 * XPWidgetsEx.h
 *
 * Copyright 2005 Sandy Barbour and Ben Supnik
 * 
 * All rights reserved.  See license.txt for usage.
 * 
 * X-Plane SDK Version: 1.0.2                                                  
 *
 */
                                 
/************************************************************************
 * POPUP MENU PICKS
 ************************************************************************
 *
 * This code helps do a popup menu item pick.  Since x-plane must be 
 * running to track the mouse, this popup menu pick is effectively 
 * asynchronous and non-modal to the code...you call the function and 
 * some time later your callback is called.
 *
 * However, due to the way the popup pick is structured, it will appear to
 * be somewhat modal to the user in that the next click after the popup 
 * is called must belong to it.
 *
 */

/*
 * XPPopupPick_f
 * 
 * This function is called when your popup is picked.  inChoice will be the number
 * of the item picked, or -1 if no item was picked.  (You should almost always ignore
 * a -1.
 *
 */
typedef	void (* XPPopupPick_f)(int inChoice, void * inRefcon);

/*
 * XPPickPopup
 *
 * This routine creates a dynamic 'popup menu' on the fly.  If inCurrentItem is
 * non-negative, it is the item that will be under the mouse.  In this case, the
 * mouse X and Y should be the top left of a popup box if there is such a thing.
 * If inCurrentItem is -1, the popup menu appears at exactly inMouseX and inMouseY.
 *
 * You pass in the items, newline terminated ('\n') as well as a callback that is
 * called when an item is picked, and a ref con for that function.
 *
 */
static void		XPPickPopup(
						int				inMouseX,
						int				inMouseY,
						const char *	inItems,
						int				inCurrentItem,
						XPPopupPick_f	inCallback,
						void *			inRefcon);

/* Impl notes: we can dispose from the mouse up.  So...on mouse up
 * we first call the popup func but then we nuke ourselves.  Internally
 * there is a data structure that is in the refcon of the xplm window that
 * contains the callback for the user and the text, etc. */
 
/************************************************************************
 * POPUP MENU BUTTON WIDGET
 ************************************************************************
 * 
 * This widget implements a stanard pick-one-from-many-style popup menu 
 * button.  The text is taken from the current item.  The descriptor is
 * the items, newline-terminated.
 *
 * A message is sent whenever a new item is picked by the user.
 * 
 */

#define	xpWidgetClass_Popup					9
 
enum {
	// This is the item number of the current item, starting at 0.
	xpProperty_PopupCurrentItem				= 1800
};

enum {
	// This message is sent when an item is picked.
	// param 1 is the widget that was picked, param 2
	// is the item number.
	xpMessage_PopupNewItemPicked					= 1800
};

/*
 * XPCreatePopup
 *
 * This routine makes a popup widget for you.  You must provide
 * a container for this widget, like a window for it to sit in.
 *
 */
static XPWidgetID           XPCreatePopup(
                                   int                  inLeft,    
                                   int                  inTop,    
                                   int                  inRight,    
                                   int                  inBottom,    
                                   int                  inVisible,    
                                   const char *         inDescriptor,    
                                   XPWidgetID           inContainer);

static int		XPPopupButtonProc(
					XPWidgetMessage			inMessage,
					XPWidgetID				inWidget,
					intptr_t				inParam1,
					intptr_t				inParam2);


/************************************************************************
 * LISTBOX
 ************************************************************************
 *
 * This code helps do a listbox.  Since x-plane must be 
 * running to track the mouse, this listbox is effectively 
 * asynchronous and non-modal to the code...you call the function and 
 * some time later your callback is called.
 *
 * However, due to the way the listbox is structured, it will appear to
 * be somewhat modal to the user in that the next click after the listbox 
 * is called must belong to it.
 *
 */

/************************************************************************
 * LISTBOX SELECTION WIDGET
 ************************************************************************
 * 
 * This widget implements a standard pick-one-from-many-style selection menu 
 * button.  The text is taken from the current item.  The descriptor is
 * the items, newline-terminated.
 *
 * A message is sent whenever a new item is picked by the user.
 * 
 */

#define	xpWidgetClass_ListBox					10
 
enum {
	// This is the item number of the current item, starting at 0.
	xpProperty_ListBoxCurrentItem					= 1900,
	// This will add an item to the list box at the end.
	xpProperty_ListBoxAddItem						= 1901,
	// This will clear the list box and then add the items.
	xpProperty_ListBoxAddItemsWithClear				= 1902,
	// This will clear the list box.
	xpProperty_ListBoxClear							= 1903,
	// This will insert an item into the list box at the index.
	xpProperty_ListBoxInsertItem					= 1904,
	// This will delete an item from the list box at the index.
	xpProperty_ListBoxDeleteItem					= 1905,
	// This stores the pointer to the listbox data.
	xpProperty_ListBoxData							= 1906,
	// This stores the max Listbox Items.
	xpProperty_ListBoxMaxListBoxItems				= 1907,
	// This stores the highlight state.
	xpProperty_ListBoxHighlighted					= 1908,
	// This stores the scrollbar Min.
	xpProperty_ListBoxScrollBarMin					= 1909,
	// This stores the scrollbar Max.
	xpProperty_ListBoxScrollBarMax					= 1910,
	// This stores the scrollbar SliderPosition.
	xpProperty_ListBoxScrollBarSliderPosition		= 1911,
	// This stores the scrollbar ScrollBarPageAmount.
	xpProperty_ListBoxScrollBarPageAmount			= 1912
};

enum {
	// This message is sent when an item is picked.
	// param 1 is the widget that was picked, param 2
	// is the item number.
	xpMessage_ListBoxItemSelected				= 1900
};

/*
 * XPCreateListBox
 *
 * This routine makes a listbox widget for you.  You must provide
 * a container for this widget, like a window for it to sit in.
 *
 */
static XPWidgetID           XPCreateListBox(
                                   int                  inLeft,    
                                   int                  inTop,    
                                   int                  inRight,    
                                   int                  inBottom,    
                                   int                  inVisible,    
                                   const char *         inDescriptor,    
                                   XPWidgetID           inContainer);

static int		XPListBoxProc(
					XPWidgetMessage			inMessage,
					XPWidgetID				inWidget,
					intptr_t				inParam1,
					intptr_t				inParam2);

/*------------------------------------------------------------------------*/


static int MenuItem1;

static XPWidgetID TestWidgetsWidget, TestWidgetsWindow;
static XPWidgetID TestWidgetsPopup, TestWidgetsListBox;
static XPWidgetID ListboxInfoText1, ListboxInfoText2, ListboxAddItemButton, ListboxFillButton, ListboxClearButton, ListboxInsertButton, ListboxDeleteItemButton, ListboxInputTextEdit;
static XPWidgetID PopupInfoText, PopupInputTextEdit, PopupSetIndexButton;
static XPLMMenuID	id;

static char szPopupText[4096];
static char szListBoxText[4096];

static void TestWidgetsMenuHandler(void *, void *);

static void CreateTestWidgets(int x1, int y1, int w, int h);

// Handle any widget messages
static int TestWidgetsHandler(
						XPWidgetMessage			inMessage,
						XPWidgetID				inWidget,
						intptr_t				inParam1,
						intptr_t				inParam2);

/*------------------------------------------------------------------------*/

PLUGIN_API int XPluginStart(
						char *		outName,
						char *		outSig,
						char *		outDesc)
{
	int			item;

	strcpy(outName, "TestWidgets");
	strcpy(outSig, "xpsdk.examples.TestWidgets");
	strcpy(outDesc, "A plug-in that tests new widgets.");

	// Build menu
	item = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "Test Widgets", NULL, 1);

	id = XPLMCreateMenu("Test Widgets", XPLMFindPluginsMenu(), item, TestWidgetsMenuHandler, NULL);
	XPLMAppendMenuItem(id, "Open", (void *)"Open", 1);

	// Used by widget to make sure only one widgets instance created
	MenuItem1 = 0;


	// Preload Popup
	szPopupText[0] = '\0';
	strcat( szPopupText, "Some longer text line 1");
	strcat( szPopupText, ";" );
	strcat( szPopupText, "Some longer text line 2");
	strcat( szPopupText, ";" );
	strcat( szPopupText, "Some longer text line 3");
	strcat( szPopupText, ";" );
	strcat( szPopupText, "Some longer text line 4");
	strcat( szPopupText, ";" );
	strcat( szPopupText, "Some longer text line 5");
	strcat( szPopupText, ";" );
	strcat( szPopupText, "Some longer text line 6");
	strcat( szPopupText, ";" );
	strcat( szPopupText, "Some longer text line 7");
	strcat( szPopupText, ";" );

	// Preload Listbox
	szListBoxText[0] = '\0';
	strcat( szListBoxText, "1234567890123456789012345678901234567890123456789012345678901234567890");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 2");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 3");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 4");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 5");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 6");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 7");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 8");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 9");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 10");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 11");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 12");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 13");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 14");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 15");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 16");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 17");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 18");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 19");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 20");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 21");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 22");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 23");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 24");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 25");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 26");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 27");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 28");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 29");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 30");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 31");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 32");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 33");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 34");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 35");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 36");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 37");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 38");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 39");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 40");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 41");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 42");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 43");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 44");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 45");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 46");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 47");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 48");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 49");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 50");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 51");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 52");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 53");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 54");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 55");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 56");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 57");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 58");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 59");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 60");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 61");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 62");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 63");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 64");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 65");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 66");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 67");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 68");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 69");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 70");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 71");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 72");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 73");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 74");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 75");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 76");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 77");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 78");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 79");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 80");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 81");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 82");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 83");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 84");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 85");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 86");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 87");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 88");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 89");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 90");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 91");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 92");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 93");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 94");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 95");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 96");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 97");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 98");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 99");
	strcat( szListBoxText, ";" );
	strcat( szListBoxText, "Item 100");
	strcat( szListBoxText, ";" );

	return 1;
}

/*------------------------------------------------------------------------*/

PLUGIN_API void	XPluginStop(void)
{
	// Clean up
	XPLMDestroyMenu(id);
	if (MenuItem1 == 1)
	{
		XPDestroyWidget(TestWidgetsWidget, 1);
		MenuItem1 = 0;
	}
}

/*------------------------------------------------------------------------*/

PLUGIN_API void XPluginDisable(void)
{
}

/*------------------------------------------------------------------------*/

PLUGIN_API int XPluginEnable(void)
{
	return 1;
}

/*------------------------------------------------------------------------*/

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFrom, int inMsg, void * inParam)
{
}

/*------------------------------------------------------------------------*/

// Handle any menu messages, only one, to created widget dialog
void TestWidgetsMenuHandler(void * mRef, void * iRef)
{
	if (!strcmp((char *) iRef, "Open"))
	{
		if (MenuItem1 == 0)
		{
			CreateTestWidgets(300, 650, 450, 600);
			MenuItem1 = 1;
		}
		else
			if(!XPIsWidgetVisible(TestWidgetsWidget))
				XPShowWidget(TestWidgetsWidget);
	}
}

/*------------------------------------------------------------------------*/

// This creates the widgets dialog and any controls
void CreateTestWidgets(int x, int y, int w, int h)
{
	int x2 = x + w;
	int y2 = y - h;

	TestWidgetsWidget = XPCreateWidget(x, y, x2, y2,
					1,	// Visible
					"Test Widgets",	// desc
					1,		// root
					NULL,	// no container
					xpWidgetClass_MainWindow);

	XPSetWidgetProperty(TestWidgetsWidget, xpProperty_MainWindowHasCloseBoxes, 1);

	TestWidgetsWindow = XPCreateWidget(x+50, y-40, x2-50, y2+30,
					1,	// Visible
					"",	// desc
					0,		// root
					TestWidgetsWidget,
					xpWidgetClass_SubWindow);

	XPSetWidgetProperty(TestWidgetsWindow, xpProperty_SubWindowType, xpSubWindowStyle_SubWindow);

    TestWidgetsPopup = XPCreatePopup( x+70, y-50, x2-70, y-72,
                                         1, szPopupText, TestWidgetsWidget );

	PopupInfoText = XPCreateWidget(x+70, y-80, x2-70, y-102,
							1, "Popup Index 0", 0, TestWidgetsWidget,
							xpWidgetClass_TextField);

	XPSetWidgetProperty(PopupInfoText, xpProperty_TextFieldType, xpTextEntryField);

	PopupSetIndexButton = XPCreateWidget(x+70, y-110, x+170, y-132,
					1, " Set Popup Index", 0, TestWidgetsWidget,
					xpWidgetClass_Button);

	XPSetWidgetProperty(PopupSetIndexButton, xpProperty_ButtonType, xpPushButton);

	PopupInputTextEdit = XPCreateWidget(x+180, y-110, x2-70, y-132,
							1, "0", 0, TestWidgetsWidget,
							xpWidgetClass_TextField);

	XPSetWidgetProperty(PopupInputTextEdit, xpProperty_TextFieldType, xpTextEntryField);

    TestWidgetsListBox = XPCreateListBox( x+70, y-140, x2-70, y-340,
                                         1, szListBoxText, TestWidgetsWidget );

	ListboxInfoText1 = XPCreateWidget(x+70, y-350, x2-70, y-372,
							1, "", 0, TestWidgetsWidget,
							xpWidgetClass_TextField);

	XPSetWidgetProperty(ListboxInfoText1, xpProperty_TextFieldType, xpTextEntryField);

	ListboxInfoText2 = XPCreateWidget(x+70, y-390, x2-70, y-392,
							1, "", 0, TestWidgetsWidget,
							xpWidgetClass_TextField);

	XPSetWidgetProperty(ListboxInfoText2, xpProperty_TextFieldType, xpTextEntryField);

	ListboxAddItemButton = XPCreateWidget(x+70, y-400, x+130, y-422,
					1, " Add Item", 0, TestWidgetsWidget,
					xpWidgetClass_Button);

	XPSetWidgetProperty(ListboxAddItemButton, xpProperty_ButtonType, xpPushButton);

	ListboxFillButton = XPCreateWidget(x+140, y-400, x+200, y-422,
					1, " Fill LB", 0, TestWidgetsWidget,
					xpWidgetClass_Button);

	XPSetWidgetProperty(ListboxFillButton, xpProperty_ButtonType, xpPushButton);

	ListboxClearButton = XPCreateWidget(x+210, y-400, x+270, y-422,
					1, " Clear LB", 0, TestWidgetsWidget,
					xpWidgetClass_Button);

	XPSetWidgetProperty(ListboxClearButton, xpProperty_ButtonType, xpPushButton);

	ListboxInsertButton = XPCreateWidget(x+70, y-430, x+140, y-452,
					1, " Insert Item", 0, TestWidgetsWidget,
					xpWidgetClass_Button);

	XPSetWidgetProperty(ListboxInsertButton, xpProperty_ButtonType, xpPushButton);

	ListboxDeleteItemButton = XPCreateWidget(x+150, y-430, x+220, y-452,
					1, " Delete Item", 0, TestWidgetsWidget,
					xpWidgetClass_Button);

	XPSetWidgetProperty(ListboxDeleteItemButton, xpProperty_ButtonType, xpPushButton);

	ListboxInputTextEdit = XPCreateWidget(x+70, y-460, x2-70, y-492,
							1, "Add Me", 0, TestWidgetsWidget,
							xpWidgetClass_TextField);

	XPSetWidgetProperty(ListboxInputTextEdit, xpProperty_TextFieldType, xpTextEntryField);

	XPAddWidgetCallback(TestWidgetsWidget, TestWidgetsHandler);
}

/*------------------------------------------------------------------------*/

// Handle any widget messages
int	TestWidgetsHandler(
						XPWidgetMessage			inMessage,
						XPWidgetID				inWidget,
						intptr_t				inParam1,
						intptr_t				inParam2)
{
	char Buffer[256];

	// Close button pressed, only hide the widget, rather than destropying it.
	if (inMessage == xpMessage_CloseButtonPushed)
	{
		if (MenuItem1 == 1)
		{
			XPHideWidget(TestWidgetsWidget);
		}
		return 1;
	}

	// Test for a button pressed
	if (inMessage == xpMsg_PushButtonPressed)
	{
		// This test adds three items to the listbox
		if (inParam1 == (intptr_t)ListboxAddItemButton)
		{
			strcpy(Buffer, "Line 1");
			XPSetWidgetDescriptor(TestWidgetsListBox, Buffer);
			XPSetWidgetProperty(TestWidgetsListBox, xpProperty_ListBoxAddItem, 1);
			strcpy(Buffer, "Line 2");
			XPSetWidgetDescriptor(TestWidgetsListBox, Buffer);
			XPSetWidgetProperty(TestWidgetsListBox, xpProperty_ListBoxAddItem, 1);
			strcpy(Buffer, "Line 3");
			XPSetWidgetDescriptor(TestWidgetsListBox, Buffer);
			XPSetWidgetProperty(TestWidgetsListBox, xpProperty_ListBoxAddItem, 1);
			return 1;
		}

		// This test clears the list box and then adds the test items
		if (inParam1 == (intptr_t)ListboxFillButton)
		{
			XPSetWidgetDescriptor(TestWidgetsListBox, szListBoxText);
			XPSetWidgetProperty(TestWidgetsListBox, xpProperty_ListBoxAddItemsWithClear, 1);
			return 1;
		}

		// This test clears the listbox
		if (inParam1 == (intptr_t)ListboxClearButton)
		{
			XPSetWidgetProperty(TestWidgetsListBox, xpProperty_ListBoxClear, 1);
			return 1;
		}

		// This test inserts the text entered into ListboxInputTextEdit at the
		// current selected listbox item position
		if (inParam1 == (intptr_t)ListboxInsertButton)
		{
			XPGetWidgetDescriptor(ListboxInputTextEdit, Buffer, sizeof(Buffer));
			XPSetWidgetDescriptor(TestWidgetsListBox, Buffer);
			XPSetWidgetProperty(TestWidgetsListBox, xpProperty_ListBoxInsertItem, 1);
			return 1;
		}

		// This test deletes the current selected listbox item
		if (inParam1 == (intptr_t)ListboxDeleteItemButton)
		{
			XPSetWidgetProperty(TestWidgetsListBox, xpProperty_ListBoxDeleteItem, 1);
			return 1;
		}

		// This test set the popup index to the number
		// entered into the PopupInputTextEdit edit box
		if (inParam1 == (intptr_t)PopupSetIndexButton)
		{
			XPGetWidgetDescriptor(PopupInputTextEdit, Buffer, sizeof(Buffer));
			int	curItem = atoi(Buffer);
			XPSetWidgetProperty(TestWidgetsPopup, xpProperty_PopupCurrentItem, curItem);
			XPGetWidgetDescriptor(TestWidgetsPopup, Buffer, sizeof(Buffer));
			curItem = XPGetWidgetProperty(TestWidgetsPopup, xpProperty_PopupCurrentItem, NULL);
			sprintf(Buffer, "Popup Index %d", curItem);
			XPSetWidgetDescriptor(PopupInfoText, Buffer);
			return 1;
		}
	}

	// This handles the listbox item selected message
	// i.e. when you click on an item
	if (inMessage == xpMessage_ListBoxItemSelected)
	{
		XPGetWidgetDescriptor(TestWidgetsListBox, Buffer, sizeof(Buffer));
		XPSetWidgetDescriptor(ListboxInfoText1, Buffer);
		sprintf(Buffer, "ListBox Index %d", inParam2);
		XPSetWidgetDescriptor(ListboxInfoText2, Buffer);
		return 1;
	}

	// This handles the popup item picked message
	// i.e. when you selecte another popup entry.
	if (inMessage == xpMessage_PopupNewItemPicked)
	{
		XPGetWidgetDescriptor(TestWidgetsPopup, Buffer, sizeof(Buffer));
		int	curItem = XPGetWidgetProperty(TestWidgetsPopup, xpProperty_PopupCurrentItem, NULL);
		sprintf(Buffer, "Popup Index %d", curItem);
		XPSetWidgetDescriptor(PopupInfoText, Buffer);
		return 1;
	}

	return 0;
}

/*------------------------------------------------------------------------*/


/*
 * XPListBox.cpp
 *
 * Copyright 2005 Sandy Barbour and Ben Supnik
 * 
 * All rights reserved.  See license.txt for usage.
 * 
 * X-Plane SDK Version: 1.0.2                                                  
 *
 */


#define LISTBOX_ITEM_HEIGHT 12
#define IN_RECT(x, y, l, t, r, b)	\
	(((x) >= (l)) && ((x) <= (r)) && ((y) >= (b)) && ((y) <= (t)))

#if IBM
static double round(double InValue)
{
    int WholeValue;
    double Fraction;

    WholeValue = InValue;
    Fraction = InValue - (double) WholeValue;

    if (Fraction >= 0.5)
        WholeValue++;

    return (double) WholeValue;
}
#endif

/************************************************************************
 *  X-PLANE UI INFRASTRUCTURE CODE
 ************************************************************************
 *
 * This code helps provde an x-plane compatible look.  It is copied from 
 * the source code from the widgets DLL; someday listyboxes will be part of
 * this, so our listboxes are written off of the same APIs.
 *
 */
 
// Enums for x-plane native colors. 
enum {

	xpColor_MenuDarkTinge = 0,
	xpColor_MenuBkgnd,
	xpColor_MenuHilite,
	xpColor_MenuLiteTinge,
	xpColor_MenuText,
	xpColor_MenuTextDisabled,
	xpColor_SubTitleText,
	xpColor_TabFront,
	xpColor_TabBack,
	xpColor_CaptionText,
	xpColor_ListText,
	xpColor_GlassText,
	xpColor_Count
};

// Enums for the datarefs we get them from.
static const char *	kXPlaneColorNames[] = {
	"sim/graphics/colors/menu_dark_rgb",			
	"sim/graphics/colors/menu_bkgnd_rgb",			
	"sim/graphics/colors/menu_hilite_rgb",	
	"sim/graphics/colors/menu_lite_rgb",		
	"sim/graphics/colors/menu_text_rgb",			
	"sim/graphics/colors/menu_text_disabled_rgb",	
	"sim/graphics/colors/subtitle_text_rgb",
	"sim/graphics/colors/tab_front_rgb",
	"sim/graphics/colors/tab_back_rgb",
	"sim/graphics/colors/caption_text_rgb",
	"sim/graphics/colors/list_text_rgb",
	"sim/graphics/colors/glass_text_rgb"
};

// Those datarefs are only XP7; if we can't find one,
// fall back to this table of X-Plane 6 colors.
static const float	kBackupColors[xpColor_Count][3] =
{
	 { (const float)(33.0/256.0), (const float)(41.0/256.0), (const float)(44.0/256.0) },
	 { (const float)(53.0/256.0), (const float)(64.0/256.0), (const float)(68.0/256.0) },
	 { (const float)(65.0/256.0), (const float)(83.0/256.0), (const float)(89.0/256.0) },
	 { (const float)(65.0/256.0), (const float)(83.0/256.0), (const float)(89.0/256.0) },
	 { (const float)0.8, (const float)0.8, (const float)0.8 },
	 { (const float)0.4, (const float)0.4, (const float)0.4 }
};	 

// This array contains the resolved datarefs
static XPLMDataRef	gColorRefs[xpColor_Count];

// Current alpha levels to blit at.
static float		gAlphaLevel = 1.0;

// This routine sets up a color from the above table.  Pass
// in a float[3] to get the color; pass in NULL to have the
// OpenGL color be set immediately.
static void	SetupAmbientColor(int inColorID, float * outColor)
{
	// If we're running the first time, resolve all of our datarefs just once.
	static	bool	firstTime = true;
	if (firstTime)
	{
		firstTime = false;
		for (int n = 0; n <xpColor_Count; ++n)
		{
			gColorRefs[n] = XPLMFindDataRef(kXPlaneColorNames[n]);
		}
	}
	
	// If being asked to set the color immediately, allocate some storage.
	float	theColor[4];
	float * target = outColor ? outColor : theColor;
	
	// If we have a dataref, just fetch the color from the ref.
	if (gColorRefs[inColorID])
		XPLMGetDatavf(gColorRefs[inColorID], target, 0, 3);
	else {
	
		// If we didn't have a dataref, fetch the ambient cabin lighting,
		// since XP6 dims the UI with night.
		static	XPLMDataRef	ambient_r = XPLMFindDataRef("sim/graphics/misc/cockpit_light_level_r");
		static	XPLMDataRef	ambient_g = XPLMFindDataRef("sim/graphics/misc/cockpit_light_level_g");
		static	XPLMDataRef	ambient_b = XPLMFindDataRef("sim/graphics/misc/cockpit_light_level_b");
	
		// Use a backup color but dim it.
		target[0] = kBackupColors[inColorID][0] * XPLMGetDataf(ambient_r);
		target[1] = kBackupColors[inColorID][1] * XPLMGetDataf(ambient_g);
		target[2] = kBackupColors[inColorID][2] * XPLMGetDataf(ambient_b);
	}
	
	// If the user passed NULL, set the color now using the alpha level.
	if (!outColor)
	{
		theColor[3] = gAlphaLevel;
		glColor4fv(theColor);
	}
}

// Just remember alpha levels for later.
static void	SetAlphaLevels(float inAlphaLevel)
{
	gAlphaLevel = inAlphaLevel;
}

#ifndef _WINDOWS
#pragma mark -
#endif

/************************************************************************
 *  LISTBOX DATA IMPLEMENTATION
 ************************************************************************/

// This structure represents a listbox internally...it consists of arrays
// per item and some general stuff.
struct	XPListBoxData_t {
	// Per item:
	std::vector<std::string>	Items;		// The name of the item
	std::vector<int>			Lefts;		// The rectangle of the item, relative to the top left corner of the listbox/
	std::vector<int>			Rights;
};

static XPListBoxData_t *pListBoxData;

// This routine finds the item that is in a given point, or returns -1 if there is none.
// It simply trolls through all the items.
static int XPListBoxGetItemNumber(XPListBoxData_t * pListBoxData, int inX, int inY)
{
	for (unsigned int n = 0; n < pListBoxData->Items.size(); ++n)
	{
		if ((inX >= pListBoxData->Lefts[n]) && (inX < pListBoxData->Rights[n]) &&
			(inY >= (n * LISTBOX_ITEM_HEIGHT)) && (inY < ((n * LISTBOX_ITEM_HEIGHT) + LISTBOX_ITEM_HEIGHT)))
		{
			return n;
		}
	}
	return -1;	
}

static void XPListBoxFillWithData(XPListBoxData_t *pListBoxData, const char *inItems, int Width)
{
	std::string	Items(inItems);
	while (!Items.empty())
	{
		std::string::size_type split = Items.find(';');
		if (split == Items.npos)
		{
			split = Items.size();
		}

		std::string	Item = Items.substr(0, split);

		pListBoxData->Items.push_back(Item);
		pListBoxData->Lefts.push_back(0);
		pListBoxData->Rights.push_back(Width);

		if (Item.size() == Items.size())
			break;
		else
			Items = Items.substr(split+1);
	}
}

static void XPListBoxAddItem(XPListBoxData_t *pListBoxData, char *pBuffer, int Width)
{
	std::string	Item(pBuffer);

	pListBoxData->Items.push_back(Item);
	pListBoxData->Lefts.push_back(0);
	pListBoxData->Rights.push_back(Width);
}

static void XPListBoxClear(XPListBoxData_t *pListBoxData)
{
	pListBoxData->Items.clear();
	pListBoxData->Lefts.clear();
	pListBoxData->Rights.clear();
}

static void XPListBoxInsertItem(XPListBoxData_t *pListBoxData, char *pBuffer, int Width, int CurrentItem)
{
	std::string	Item(pBuffer);

	pListBoxData->Items.insert(pListBoxData->Items.begin() + CurrentItem, Item);
	pListBoxData->Lefts.insert(pListBoxData->Lefts.begin() + CurrentItem, 0);
	pListBoxData->Rights.insert(pListBoxData->Rights.begin() + CurrentItem, Width);
}

static void XPListBoxDeleteItem(XPListBoxData_t *pListBoxData, int CurrentItem)
{
	pListBoxData->Items.erase(pListBoxData->Items.begin() + CurrentItem);
	pListBoxData->Lefts.erase(pListBoxData->Lefts.begin() + CurrentItem);
	pListBoxData->Rights.erase(pListBoxData->Rights.begin() + CurrentItem);
}

#ifndef _WINDOWS
#pragma mark -
#endif

// This widget Proc implements the actual listbox.

static int		XPListBoxProc(
					XPWidgetMessage			inMessage,
					XPWidgetID				inWidget,
					intptr_t				inParam1,
					intptr_t				inParam2)
{
	static int ScrollBarSlop;

	// Select if we're in the background.
	if (XPUSelectIfNeeded(inMessage, inWidget, inParam1, inParam2, 1/*eat*/))	return 1;
	
	int Left, Top, Right, Bottom, x, y, ListBoxDataOffset, ListBoxIndex;
	char Buffer[4096];

	int IsVertical, DownBtnSize, DownPageSize, ThumbSize, UpPageSize, UpBtnSize;
	bool UpBtnSelected, DownBtnSelected, ThumbSelected, UpPageSelected, DownPageSelected;
	
	XPGetWidgetGeometry(inWidget, &Left, &Top, &Right, &Bottom);
	
	int	SliderPosition = XPGetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarSliderPosition, NULL);
	int	Min = XPGetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarMin, NULL);
	int	Max = XPGetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarMax, NULL);
	int	ScrollBarPageAmount = XPGetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarPageAmount, NULL);
	int	CurrentItem = XPGetWidgetProperty(inWidget, xpProperty_ListBoxCurrentItem, NULL);
	int	MaxListBoxItems = XPGetWidgetProperty(inWidget, xpProperty_ListBoxMaxListBoxItems, NULL);
	int	Highlighted = XPGetWidgetProperty(inWidget, xpProperty_ListBoxHighlighted, NULL);
	XPListBoxData_t	*pListBoxData = (XPListBoxData_t*) XPGetWidgetProperty(inWidget, xpProperty_ListBoxData, NULL);

	switch(inMessage) 
	{
		case xpMsg_Create:
			// Allocate mem for the structure.
			pListBoxData = new XPListBoxData_t;
			XPGetWidgetDescriptor(inWidget, Buffer, sizeof(Buffer));
			XPListBoxFillWithData(pListBoxData, Buffer, (Right - Left - 20));
			XPSetWidgetProperty(inWidget, xpProperty_ListBoxData, (intptr_t)pListBoxData);
			XPSetWidgetProperty(inWidget, xpProperty_ListBoxCurrentItem, 0);
			Min = 0;
			Max = pListBoxData->Items.size();
			ScrollBarSlop = 0;
			Highlighted = false;
			SliderPosition = Max;
			MaxListBoxItems = (Top - Bottom) / LISTBOX_ITEM_HEIGHT;
			ScrollBarPageAmount = MaxListBoxItems;
			XPSetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarSliderPosition, SliderPosition);
			XPSetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarMin, Min);
			XPSetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarMax, Max);
			XPSetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarPageAmount, ScrollBarPageAmount);
			XPSetWidgetProperty(inWidget, xpProperty_ListBoxMaxListBoxItems, MaxListBoxItems);
			XPSetWidgetProperty(inWidget, xpProperty_ListBoxHighlighted, Highlighted);
			return 1;

		case xpMsg_DescriptorChanged:
			return 1;

		case xpMsg_PropertyChanged:
			if (XPGetWidgetProperty(inWidget, xpProperty_ListBoxAddItem, NULL))
			{
				XPSetWidgetProperty(inWidget, xpProperty_ListBoxAddItem, 0);
				XPGetWidgetDescriptor(inWidget, Buffer, sizeof(Buffer));
				XPListBoxAddItem(pListBoxData, Buffer, (Right - Left - 20));
				Max = pListBoxData->Items.size();
				SliderPosition = Max;
				XPSetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarSliderPosition, SliderPosition);
				XPSetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarMax, Max);
			}

			if (XPGetWidgetProperty(inWidget, xpProperty_ListBoxAddItemsWithClear, NULL))
			{
				XPSetWidgetProperty(inWidget, xpProperty_ListBoxAddItemsWithClear, 0);
				XPGetWidgetDescriptor(inWidget, Buffer, sizeof(Buffer));
				XPListBoxClear(pListBoxData);
				XPListBoxFillWithData(pListBoxData, Buffer, (Right - Left - 20));
				Max = pListBoxData->Items.size();
				SliderPosition = Max;
				XPSetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarSliderPosition, SliderPosition);
				XPSetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarMax, Max);
			}

			if (XPGetWidgetProperty(inWidget, xpProperty_ListBoxClear, NULL))
			{
				XPSetWidgetProperty(inWidget, xpProperty_ListBoxClear, 0);
				XPSetWidgetProperty(inWidget, xpProperty_ListBoxCurrentItem, 0);
				XPListBoxClear(pListBoxData);
				Max = pListBoxData->Items.size();
				SliderPosition = Max;
				XPSetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarSliderPosition, SliderPosition);
				XPSetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarMax, Max);
			}

			if (XPGetWidgetProperty(inWidget, xpProperty_ListBoxInsertItem, NULL))
			{
				XPSetWidgetProperty(inWidget, xpProperty_ListBoxInsertItem, 0);
				XPGetWidgetDescriptor(inWidget, Buffer, sizeof(Buffer));
				XPListBoxInsertItem(pListBoxData, Buffer, (Right - Left - 20), CurrentItem);
			}

			if (XPGetWidgetProperty(inWidget, xpProperty_ListBoxDeleteItem, NULL))
			{
				XPSetWidgetProperty(inWidget, xpProperty_ListBoxDeleteItem, 0);
				if ((pListBoxData->Items.size() > 0) && (pListBoxData->Items.size() > CurrentItem))
					XPListBoxDeleteItem(pListBoxData, CurrentItem);
			}
			return 1;

		case xpMsg_Draw:
		{
			int	x, y;
			XPLMGetMouseLocation(&x, &y);
			
			XPDrawWindow(Left, Bottom, Right-20, Top, xpWindow_ListView);
			XPDrawTrack(Right-20, Bottom, Right, Top, Min, Max, SliderPosition, xpTrack_ScrollBar, Highlighted);

			XPLMSetGraphicsState(0, 1, 0,  0, 1,  0, 0);
			XPLMBindTexture2d(XPLMGetTexture(xplm_Tex_GeneralInterface), 0);
			glColor4f(1.0, 1.0, 1.0, 1.0);
				
			unsigned int ItemNumber;
			XPLMSetGraphicsState(0, 0, 0,  0, 0,  0, 0);

			// Now draw each item.
			ListBoxIndex = Max - SliderPosition;
			ItemNumber = 0;
			while (ItemNumber < MaxListBoxItems)
			{
				if (ListBoxIndex < pListBoxData->Items.size())
				{
					// Calculate the item rect in global coordinates.
					int ItemTop    = Top - (ItemNumber * LISTBOX_ITEM_HEIGHT);
					int ItemBottom = Top - ((ItemNumber * LISTBOX_ITEM_HEIGHT) + LISTBOX_ITEM_HEIGHT);

					// If we are hilited, draw the hilite bkgnd.
					if (CurrentItem == ListBoxIndex)
					{
						SetAlphaLevels(0.25);
						XPLMSetGraphicsState(0, 0, 0,  0, 1, 0, 0);
						SetupAmbientColor(xpColor_MenuHilite, NULL);
						SetAlphaLevels(1.0);
						glBegin(GL_QUADS);
						glVertex2i(Left, ItemTop);
						glVertex2i(Right-20, ItemTop);
						glVertex2i(Right-20, ItemBottom);
						glVertex2i(Left, ItemBottom);
						glEnd();						
					}
					
					float	text[3];
					SetupAmbientColor(xpColor_ListText, text);
					
					char	Buffer[512];
					int		FontWidth, FontHeight;
					int		ListBoxWidth = (Right - 20) - Left;
					strcpy(Buffer, pListBoxData->Items[ListBoxIndex++].c_str());
					XPLMGetFontDimensions(xplmFont_Basic, &FontWidth, &FontHeight, NULL);
					int		MaxChars = ListBoxWidth / FontWidth;
					Buffer[MaxChars] = 0;

					XPLMDrawString(text,
								Left, ItemBottom + 2,
								const_cast<char *>(Buffer), NULL, xplmFont_Basic);
				}
				ItemNumber++;
			}
		}
			return 1;

		case xpMsg_MouseUp:
			if (IN_RECT(MOUSE_X(inParam1), MOUSE_Y(inParam1), Right-20, Top, Right, Bottom))
			{
				Highlighted = false;
				XPSetWidgetProperty(inWidget, xpProperty_ListBoxHighlighted, Highlighted);
			}

			if (IN_RECT(MOUSE_X(inParam1), MOUSE_Y(inParam1), Left, Top, Right-20, Bottom))
			{
				if (pListBoxData->Items.size() > 0)
				{
					if (CurrentItem != -1)
						XPSetWidgetDescriptor(inWidget, pListBoxData->Items[CurrentItem].c_str());
					else
						XPSetWidgetDescriptor(inWidget, "");
					XPSendMessageToWidget(inWidget, xpMessage_ListBoxItemSelected, xpMode_UpChain, (intptr_t) inWidget, (intptr_t) CurrentItem);
				}
			}
			return 1;

		case xpMsg_MouseDown:
			if (IN_RECT(MOUSE_X(inParam1), MOUSE_Y(inParam1), Left, Top, Right-20, Bottom))
			{
				if (pListBoxData->Items.size() > 0)
				{
					XPLMGetMouseLocation(&x, &y);
					ListBoxDataOffset = XPListBoxGetItemNumber(pListBoxData, x - Left, Top - y);	
					if (ListBoxDataOffset != -1)
					{
						ListBoxDataOffset += (Max - SliderPosition);
						if (ListBoxDataOffset < pListBoxData->Items.size())
							XPSetWidgetProperty(inWidget, xpProperty_ListBoxCurrentItem, ListBoxDataOffset);
					}
				}
			}

			if (IN_RECT(MOUSE_X(inParam1), MOUSE_Y(inParam1), Right-20, Top, Right, Bottom))
			{
				XPGetTrackMetrics(Right-20, Bottom, Right, Top, Min, Max, SliderPosition, xpTrack_ScrollBar, &IsVertical, &DownBtnSize, &DownPageSize, &ThumbSize, &UpPageSize, &UpBtnSize);
				int	Min = XPGetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarMin, NULL);
				int	Max = XPGetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarMax, NULL);
				if (IsVertical)
				{
					UpBtnSelected = IN_RECT(MOUSE_X(inParam1), MOUSE_Y(inParam1), Right-20, Top, Right, Top - UpBtnSize);
					DownBtnSelected = IN_RECT(MOUSE_X(inParam1), MOUSE_Y(inParam1), Right-20, Bottom + DownBtnSize, Right, Bottom);
					UpPageSelected = IN_RECT(MOUSE_X(inParam1), MOUSE_Y(inParam1), Right-20, (Top - UpBtnSize), Right, (Bottom + DownBtnSize + DownPageSize + ThumbSize));
					DownPageSelected = IN_RECT(MOUSE_X(inParam1), MOUSE_Y(inParam1), Right-20, (Top - UpBtnSize - UpPageSize - ThumbSize), Right, (Bottom + DownBtnSize));
					ThumbSelected = IN_RECT(MOUSE_X(inParam1), MOUSE_Y(inParam1), Right-20, (Top - UpBtnSize - UpPageSize), Right, (Bottom + DownBtnSize + DownPageSize));
				}
				else
				{
					DownBtnSelected = IN_RECT(MOUSE_X(inParam1), MOUSE_Y(inParam1), Right-20, Top, Right-20 + UpBtnSize, Bottom);
					UpBtnSelected = IN_RECT(MOUSE_X(inParam1), MOUSE_Y(inParam1), Right-20 - DownBtnSize, Top, Right, Bottom);
					DownPageSelected = IN_RECT(MOUSE_X(inParam1), MOUSE_Y(inParam1), Right-20 + DownBtnSize, Top, Right - UpBtnSize - UpPageSize - ThumbSize, Bottom);
					UpPageSelected = IN_RECT(MOUSE_X(inParam1), MOUSE_Y(inParam1), Right-20 + DownBtnSize + DownPageSize + ThumbSize, Top, Right - UpBtnSize, Bottom);
					ThumbSelected = IN_RECT(MOUSE_X(inParam1), MOUSE_Y(inParam1), Right-20 + DownBtnSize + DownPageSize, Top, Right - UpBtnSize - UpPageSize, Bottom);
				}

				if (UpPageSelected)
				{
					SliderPosition+=ScrollBarPageAmount;
					if (SliderPosition > Max)
						SliderPosition = Max;
					XPSetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarSliderPosition, SliderPosition);
				}
				else if (DownPageSelected)
				{
					SliderPosition-=ScrollBarPageAmount;
					if (SliderPosition < Min)
						SliderPosition = Min;
					XPSetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarSliderPosition, SliderPosition);
				}
				else if (UpBtnSelected)
				{
					SliderPosition++;
					if (SliderPosition > Max)
						SliderPosition = Max;
					XPSetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarSliderPosition, SliderPosition);
				}
				else if (DownBtnSelected)
				{
					SliderPosition--;
					if (SliderPosition < Min)
						SliderPosition = Min;
					XPSetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarSliderPosition, SliderPosition);
				}
				else if (ThumbSelected)
				{
					if (IsVertical)
						ScrollBarSlop = Bottom + DownBtnSize + DownPageSize + (ThumbSize/2) - MOUSE_Y(inParam1);
					else
						ScrollBarSlop = Right-20 + DownBtnSize + DownPageSize + (ThumbSize/2) - MOUSE_X(inParam1);
					Highlighted = true;
					XPSetWidgetProperty(inWidget, xpProperty_ListBoxHighlighted, Highlighted);

				}
				else
				{
					Highlighted = false;
					XPSetWidgetProperty(inWidget, xpProperty_ListBoxHighlighted, Highlighted);
				}
			}		
		return 1;

	case xpMsg_MouseDrag:
		if (IN_RECT(MOUSE_X(inParam1), MOUSE_Y(inParam1), Right-20, Top, Right, Bottom))
		{
			XPGetTrackMetrics(Right-20, Bottom, Right, Top, Min, Max, SliderPosition, xpTrack_ScrollBar, &IsVertical, &DownBtnSize, &DownPageSize, &ThumbSize, &UpPageSize, &UpBtnSize);
			int	Min = XPGetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarMin, NULL);
			int	Max = XPGetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarMax, NULL);

			ThumbSelected = Highlighted;

			if (ThumbSelected)
			{
				if (inParam1 != 0)
				{				
					if (IsVertical)
					{
						y = MOUSE_Y(inParam1) + ScrollBarSlop;
						SliderPosition = round((float)((float)(y - (Bottom + DownBtnSize + ThumbSize/2)) / 
									(float)((Top - UpBtnSize - ThumbSize/2) - (Bottom + DownBtnSize + ThumbSize/2))) * Max);
					}
					else
					{
						x = MOUSE_X(inParam1) + ScrollBarSlop;
						SliderPosition = round((float)((float)(x - (Right-20 + DownBtnSize + ThumbSize/2)) / (float)((Right - UpBtnSize - ThumbSize/2) - (Right-20 + DownBtnSize + ThumbSize/2))) * Max);
					}

				}
				else
					SliderPosition = 0;

				if (SliderPosition < Min)
					SliderPosition = Min;
				if (SliderPosition > Max)
					SliderPosition = Max;
					
				XPSetWidgetProperty(inWidget, xpProperty_ListBoxScrollBarSliderPosition, SliderPosition);
			}
		}
		return 1;

		default:
			return 0;
	}	
}				

// To create a listbox, make a new widget with our listbox proc as the widget proc.
static XPWidgetID           XPCreateListBox(
                                   int                  inLeft,    
                                   int                  inTop,    
                                   int                  inRight,    
                                   int                  inBottom,    
                                   int                  inVisible,    
                                   const char *         inDescriptor,    
                                   XPWidgetID           inContainer)
{
	return XPCreateCustomWidget(
                                   inLeft,    
                                   inTop,    
                                   inRight,    
                                   inBottom,    
                                   inVisible,    
                                   inDescriptor,    
                                   0,
                                   inContainer,    
                                   XPListBoxProc);
}                                   


/*------------------------------------------------------------------------*/

/*
 * XPPopups.cpp
 *
 * Copyright 2005 Sandy Barbour and Ben Supnik
 * 
 * All rights reserved.  See license.txt for usage.
 * 
 * X-Plane SDK Version: 1.0.2                                                  
 *
 */

 /************************************************************************
 *  X-PLANE UI INFRASTRUCTURE CODE
 ************************************************************************
 *
 * This code helps provde an x-plane compatible look.  It is copied from 
 * the source code from the widgets DLL; someday popups will be part of
 * this, so our popups are written off of the same APIs.
 *
 */

enum {
	// These are for caching, do not use!!
	xpProperty_OffsetToCurrentItem			= 1898,
	xpProperty_CurrentItemLen				= 1899
};

#ifndef _WINDOWS
#pragma mark -
#endif

/************************************************************************
 *  POPUP MENU IMPLEMENTATION
 ************************************************************************/


// This structure represents a popup menu internally...it consists of arrays
// per item and some general stuff.  It is implemented as a giant window; that
// window has a ptr to this struct as its refcon.  While the whole window isn't
// drawn in, we make the window full screen so if the user clicks outside the
// popup, we capture the click and dismiss the popup.
struct	XPPopupMenu_t {

	XPLMWindowID				window;		// The window that implements us.

	// Per item:
	std::vector<std::string>	items;		// The name of the item
	std::vector<bool>			enabled;	// Is it enabled?
	std::vector<int>			lefts;		// The rectangle of the item, relative to the top left corner of the menu/
	std::vector<int>			rights;
	std::vector<int>			bottoms;
	std::vector<int>			tops;
	std::vector<int>			vstripes;	// An array of vertical stripes (relative to the left of the menu
											// for really big menus.
	int							left;		// The overall bounds of the menu in global screen coordinates.
	int							top;
	int							right;
	int							bottom;

	XPPopupPick_f				function;	// Info for our callback function.
	void *						ref;
	
};

// This routine finds the item that is in a given point, or returns -1 if there is none.
// It simply trolls through all the items.
static int XPItemForHeight(XPPopupMenu_t * pmenu, int inX, int inY)
{
	inX -= pmenu->left;
	inY -= pmenu->top;
	for (unsigned int n = 0; n < pmenu->items.size(); ++n)
	{
		if ((inX >= pmenu->lefts[n]) && (inX < pmenu->rights[n]) &&
			(inY >= pmenu->bottoms[n]) && (inY < pmenu->tops[n]))
		{
			return n;
		}
	}
	return -1;	
}

// This is the drawing hook for a popup menu.
static void XPPopupDrawWindowCB(
                                   XPLMWindowID         inWindowID,    
                                   void *               inRefcon)
{
	XPPopupMenu_t * pmenu = (XPPopupMenu_t *) inRefcon;
	int	x, y;
	XPLMGetMouseLocation(&x, &y);

	// This is the index number of the currently selected item, based
	// on where the mouse is.
	int menu_offset = XPItemForHeight(pmenu, x, y);	

	int	item_top = pmenu->top;
	unsigned int n;
	XPLMSetGraphicsState(0, 0, 0,  0, 0,  0, 0);
	
	// Draw any vertical stripes that must be drawn for multi-column menus.
	for (n = 0; n < pmenu->vstripes.size(); ++n)
	{
		SetupAmbientColor(xpColor_MenuDarkTinge, NULL);
		glBegin(GL_LINES);
		glVertex2i(pmenu->left + pmenu->vstripes[n] - 1, pmenu->top);
		glVertex2i(pmenu->left + pmenu->vstripes[n] - 1, pmenu->bottom);
		glEnd();
		SetupAmbientColor(xpColor_MenuLiteTinge, NULL);
		glBegin(GL_LINES);
		glVertex2i(pmenu->left + pmenu->vstripes[n], pmenu->top);
		glVertex2i(pmenu->left + pmenu->vstripes[n], pmenu->bottom);
		glEnd();	
	}
	
	// Now draw each item.
	for (n = 0; n < pmenu->items.size(); ++n)
	{
		// Calcualte the item rect in global coordinates.
		int item_bottom = pmenu->bottoms[n] + pmenu->top;
		int item_top    = pmenu->tops[n] + pmenu->top;
		int item_left   = pmenu->lefts[n] + pmenu->left;
		int item_right  = pmenu->rights[n] + pmenu->left;
		
		XPDrawElement(	item_left,
						item_bottom,
						item_right,
						item_top, 
						(menu_offset == n && pmenu->enabled[n])? xpElement_PushButtonLit : xpElement_PushButton, 0);

		if (!pmenu->enabled[n] && pmenu->items[n] == "-")
		{
			// Draw two lines for dividers.
			XPLMSetGraphicsState(0, 0, 0,  0, 0,  0, 0);
			SetupAmbientColor(xpColor_MenuLiteTinge, NULL);
			glBegin(GL_LINE_STRIP);
			glVertex2i(item_left, item_top - 1);
			glVertex2i(item_right, item_top - 1);
			glEnd();
			SetupAmbientColor(xpColor_MenuDarkTinge, NULL);
			glBegin(GL_LINE_STRIP);
			glVertex2i(item_left, item_top);
			glVertex2i(item_right, item_top);
			glEnd();
		} else {
			// If we are hilited, draw the hilite bkgnd.
			if (menu_offset == n && pmenu->enabled[n])
			{
				SetAlphaLevels(0.25);
				XPLMSetGraphicsState(0, 0, 0,  0, 1, 0, 0);
				SetupAmbientColor(xpColor_MenuHilite, NULL);
				SetAlphaLevels(1.0);
				glBegin(GL_QUADS);
				glVertex2i(item_left, item_top);
				glVertex2i(item_right, item_top);
				glVertex2i(item_right, item_bottom);
				glVertex2i(item_left, item_bottom);
				glEnd();						
			}
			
			// Draw the text for the menu item, taking into account
			// disabling as a color.
			float	text[3];
			SetupAmbientColor(pmenu->enabled[n] ? xpColor_MenuText : xpColor_MenuTextDisabled, text);
			
			int XPlaneVersion, XPLMVersion, HostID;
			XPLMGetVersions(&XPlaneVersion, &XPLMVersion, &HostID);
			int yOffset = 2;

			if (XPlaneVersion < 9400)
				yOffset = 2;
			else
				yOffset = 5;

			XPLMDrawString(text,
						item_left + 18, item_bottom + yOffset,
						const_cast<char *>(pmenu->items[n].c_str()), NULL, xplmFont_Menus);
		}
	}
}                                   

static void XPPopupHandleKeyCB(
                                   XPLMWindowID         inWindowID,    
                                   char                 inKey,    
                                   XPLMKeyFlags         inFlags,    
                                   char                 inVirtualKey,    
                                   void *               inRefcon,    
                                   int                  losingFocus)
{
	// Nothing to do when a key is pressed; popup menus don't use keys.
}                                     

// This is the mouse click handler.
static int XPPopupHandleMouseClickCB(
                                   XPLMWindowID         inWindowID,    
                                   int                  x,    
                                   int                  y,    
                                   XPLMMouseStatus      inMouse,    
                                   void *               inRefcon)
{
	// Normally we do nothing.  But when we get an up click we dismiss.
	if (inMouse == xplm_MouseUp)
	{
		XPPopupMenu_t * pmenu = (XPPopupMenu_t *) inRefcon;

		int menu_offset = XPItemForHeight(pmenu, x, y);	
		
		// If we got an item click and it is not enabled,
		// pretend nothing was picked.
		if (menu_offset >= 0 && !pmenu->enabled[menu_offset])
			menu_offset = -1;
			
		// Call the callback
		if (pmenu->function)
			pmenu->function(menu_offset, pmenu->ref);

		// cleanup
		delete pmenu;
		XPLMDestroyWindow(inWindowID);
	}
	return 1;
}

// This routine just has to build the popup and then the window takes care of itself.
static void		XPPickPopup(
						int				inMouseX,
						int				inMouseY,
						const char *	inItems,
						int				inCurrentItem,
						XPPopupPick_f	inCallback,
						void *			inRefcon)
{
	int	screenWidth, screenHeight;
	int	fontWidth, fontHeight;
	screenWidth = 1024;
	screenHeight = 768;
	XPLMGetFontDimensions(xplmFont_Menus, &fontWidth, &fontHeight, NULL);
	
	// Allocate mem for the structure and build a new window as big as teh screen.
	XPPopupMenu_t *	info = new XPPopupMenu_t;
	
	XPLMWindowID		windID = XPLMCreateWindow(0, screenHeight, screenWidth, 0, 1, 
		XPPopupDrawWindowCB, XPPopupHandleKeyCB, XPPopupHandleMouseClickCB, info);
	
	if (inCurrentItem < 0) inCurrentItem = 0;
	
	/************ PARSE THE MENU STRING INTO MENU ITEMS **********/
	
	// Parse the itemes into arrays.  Remember how tall they are so
	// we can calculate the geometry.
	std::vector<int>	heights;
	std::string	items(inItems);
	while (!items.empty())
	{
		std::string::size_type split = items.find(';');
		if (split == items.npos)
		{
			split = items.size();
		}

		std::string	item = items.substr(0, split);

		if (item == "-")
		{
			info->items.push_back("-");
			info->enabled.push_back(false);
			heights.push_back(2);
		} else {
			if (!item.empty() && item[0] == '(')
			{
				info->enabled.push_back(false);
				info->items.push_back(item.substr(1));
				heights.push_back(12);
			} else {
				info->enabled.push_back(true);
				info->items.push_back(item);
				heights.push_back(12);
			}
		}
		
		if (item.size() == items.size())
			break;
		else
			items = items.substr(split+1);
	}
	
	/************ PLACE THE ITEMS IN COLUMNS **********/

	unsigned int menuWidth = 0;
	int	leftOff = 0, topOff = 0;
	
	// Calculate the widest menu item anywhere.
	for (std::vector<std::string>::iterator iter = info->items.begin(); iter != info->items.end(); ++iter)
		if (iter->size() > menuWidth)
			menuWidth = iter->size();
	menuWidth *= fontWidth;
	menuWidth += 35;
	
	int cols = 1;
	int itemsInCol = 0;
	int	maxLen = 0;
	int	ourItemLeft = 0, ourItemTop = 0;
	
	// Stack up each item, building new columns as necessary.
	for( unsigned int i = 0; i < heights.size(); ++i )
	{
		itemsInCol++;
		info->lefts.push_back(leftOff);
		info->rights.push_back(leftOff + menuWidth);
		info->tops.push_back(topOff);
		info->bottoms.push_back(topOff - heights[i]);
		if (i == inCurrentItem) 
		{
			ourItemLeft = leftOff;
			ourItemTop = topOff;
		}
		topOff -= heights[i];
		if (maxLen > topOff)
			maxLen = topOff;
		
		if (topOff < -(screenHeight - 50))
		{
			itemsInCol = 0;
			cols++;
			topOff = 0;
			leftOff += menuWidth;
			info->vstripes.push_back(leftOff);
		}
	}
	
	// If we built a new column but had no items for it, throw it out.
	if (itemsInCol == 0)
	{
		cols--;
		info->vstripes.pop_back();
	}
	
	// Now place the window.  Make sure to not let it go off screen.
	info->window = windID;
	info->left = inMouseX - ourItemLeft;
	info->top = inMouseY - ourItemTop;
	info->right = inMouseX + (menuWidth * cols) - ourItemLeft;
	info->bottom = inMouseY + maxLen - ourItemTop;
	
	if (info->right > (screenWidth-10))
	{
		int deltaLeft = (info->right - (screenWidth-10));
		info->right -= deltaLeft;
		info->left -= deltaLeft;
	}
	if (info->left < 10)
	{
		int deltaRight = 10 - info->left;
		info->right += deltaRight;
		info->left += deltaRight;
	}
	if (info->bottom < 10)
	{
		int deltaUp = 10 - info->bottom;
		info->bottom += deltaUp;
		info->top += deltaUp;
	}
	if (info->top > (screenHeight-30))
	{
		int deltaDown = (info->top - (screenHeight-30));
		info->top -= deltaDown;
		info->bottom -= deltaDown;
	}
	
	info->function = inCallback;
	info->ref = inRefcon;
}

#ifndef _WINDOWS
#pragma mark -
#endif

// This routine is called by our popup menu when it is picked; we 
// have stored our widget in our refcon, so we save the new choice
// and send a widget message.
static	void 	XPPopupWidgetProc(int inChoice, void * inRefcon)
{
	if (inChoice != -1)
	{
		XPSetWidgetProperty(inRefcon, xpProperty_PopupCurrentItem, inChoice);
		XPSendMessageToWidget(inRefcon, xpMessage_PopupNewItemPicked, xpMode_UpChain, (intptr_t) inRefcon, (intptr_t) inChoice);
	}
}

// This widget Proc implements the actual button.

static int		XPPopupButtonProc(
					XPWidgetMessage			inMessage,
					XPWidgetID				inWidget,
					intptr_t				inParam1,
					intptr_t				inParam2)
{
	// Select if we're in the background.
	if (XPUSelectIfNeeded(inMessage, inWidget, inParam1, inParam2, 1/*eat*/))	return 1;
	
	int fh, fv;
	int l, t, r, b;
	char	buf[4096];
	
	XPGetWidgetGeometry(inWidget, &l, &t, &r, &b);
	XPLMGetFontDimensions(xplmFont_Basic, &fh, &fv, NULL);
	
	int	curItem = XPGetWidgetProperty(inWidget, xpProperty_PopupCurrentItem, NULL);

	switch(inMessage) {
	case xpMsg_Create:
	case xpMsg_DescriptorChanged:
	case xpMsg_PropertyChanged:
		// If our data changes, reparse our descriptor to change our current item.
		if (inMessage != xpMsg_PropertyChanged || inParam1 == xpProperty_PopupCurrentItem)
		{
			XPGetWidgetDescriptor(inWidget, buf, sizeof(buf));
			char * p = buf;
			int picksToSkip = curItem;
			while (picksToSkip > 0)
			{
				while (*p && *p != ';') ++p;
				if (*p == 0) break;
				++p;
				--picksToSkip;
			}
			char * term = p;
			while (*term && *term != ';') ++term;
			// Store an offset and length of our descriptor that will show as our current text.
			XPSetWidgetProperty(inWidget, xpProperty_OffsetToCurrentItem, p - buf);
			XPSetWidgetProperty(inWidget, xpProperty_CurrentItemLen, term - p);			
			XPSetWidgetProperty(inWidget, xpProperty_Enabled, 1);
		}
		return 1;
	case xpMsg_Draw:
		{
			float		white [4];
			float		gray [4];
			
			int itemOffset = XPGetWidgetProperty(inWidget, xpProperty_OffsetToCurrentItem, NULL);
			int itemLen = XPGetWidgetProperty(inWidget, xpProperty_CurrentItemLen, NULL);

			// Drawing time.  Find the sim version once.
			static	int sim;
			static float firstTime = true;
			static	int	charWidth;
			if (firstTime)
			{
				firstTime = false;
				int	plugin;
				XPLMHostApplicationID id;
				XPLMGetVersions(&sim, &plugin, &id);
				
				XPLMGetFontDimensions(xplmFont_Basic, &charWidth, NULL, NULL);
			}
			
			// If we are version 7 of the sim, use Sergio's great new popup item.
			// Since there is no UI element code for this, we must draw it by hand!
			if (sim >= 700)
			{
				int center = (t + b) / 2;
				XPDrawElement(	l - 4, center - 13, r + 4, center + 13, 
								xpElement_PushButton, 0);
			} else 
				// If we are version 6, use a window drag bar as a fake popup button.
				XPDrawElement(l+2, b, r-2, t, xpElement_WindowDragBarSmooth, 0);

			// Now draw the button label.
			int	titleLen = XPGetWidgetDescriptor(inWidget, buf, sizeof(buf));

			SetupAmbientColor(xpColor_MenuText, white);
			SetupAmbientColor(xpColor_MenuTextDisabled, gray);
			
			if (charWidth)
			{
				int	maxCharCapacity = (r - l - 24) / charWidth;
				if (itemLen > maxCharCapacity)
					itemLen = maxCharCapacity;
			}
			
			buf[itemOffset + itemLen] = 0;
			if (buf[itemOffset] == '(')	++itemOffset;
			titleLen = strlen(buf + itemOffset);
			
			XPLMDrawString(XPGetWidgetProperty(inWidget, xpProperty_Enabled, 0) ? white : gray, l + 4,
						(t + b) / 2 - (fv / 2) + 2,
						buf + itemOffset, NULL, xplmFont_Basic);
					
		}
		return 1;
	case xpMsg_MouseDown:
		// If the mouse is clicked, do a popup pick.
		if (XPGetWidgetProperty(inWidget, xpProperty_Enabled, 0))
		{
			XPGetWidgetDescriptor(inWidget, buf, sizeof(buf));
			
			XPPickPopup(l, t, buf, XPGetWidgetProperty(inWidget, xpProperty_PopupCurrentItem, NULL),
								XPPopupWidgetProc, inWidget);
			return 1;
		}
	default:
		return 0;
	}	
}				

// To create a popup, make a new widget with our button proc as the widget proc.
static XPWidgetID           XPCreatePopup(
                                   int                  inLeft,    
                                   int                  inTop,    
                                   int                  inRight,    
                                   int                  inBottom,    
                                   int                  inVisible,    
                                   const char *         inDescriptor,    
                                   XPWidgetID           inContainer)
{
	return XPCreateCustomWidget(
                                   inLeft,    
                                   inTop,    
                                   inRight,    
                                   inBottom,    
                                   inVisible,    
                                   inDescriptor,    
                                   0,
                                   inContainer,    
                                   XPPopupButtonProc);
}                                   
