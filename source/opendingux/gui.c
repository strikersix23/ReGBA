/* ReGBA - In-application menu
 *
 * Copyright (C) 2013 Dingoonity user Nebuleon
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licens e as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <dirent.h>
#include "common.h"
#include "port.h"

#define COLOR_BACKGROUND       RGB888_TO_RGB565(  0,  48,   0)
#define COLOR_INACTIVE_TEXT    RGB888_TO_RGB565( 64, 160,  64)
#define COLOR_INACTIVE_OUTLINE RGB888_TO_RGB565(  0,   0,   0)
#define COLOR_ACTIVE_TEXT      RGB888_TO_RGB565(255, 255, 255)
#define COLOR_ACTIVE_OUTLINE   RGB888_TO_RGB565(  0,   0,   0)
#define COLOR_TITLE_TEXT       RGB888_TO_RGB565(128, 255, 128)
#define COLOR_TITLE_OUTLINE    RGB888_TO_RGB565(  0,  96,   0)
#define COLOR_ERROR_TEXT       RGB888_TO_RGB565(255,  64,  64)
#define COLOR_ERROR_OUTLINE    RGB888_TO_RGB565( 80,   0,   0)

enum MenuEntryKind {
	KIND_OPTION,
	KIND_SUBMENU,
	KIND_DISPLAY,
	KIND_CUSTOM,
};

enum MenuDataType {
	TYPE_STRING,
	TYPE_INT32,
	TYPE_UINT32,
	TYPE_INT64,
	TYPE_UINT64,
};

struct MenuEntry;

struct Menu;

/*
 * MenuModifyFunction is the type of a function that acts on an input in the
 * menu. The function is assigned this input via the MenuEntry struct's
 * various Button<Name>Function members, InitFunction, EndFunction, etc.
 * Variables:
 *   1: On entry into the function, points to a memory location containing
 *     a pointer to the active menu. On exit from the function, the menu may
 *     be modified to a new one, in which case the function has chosen to
 *     activate that new menu; the EndFunction of the old menu is called,
 *     then the InitFunction of the new menu is called.
 * 
 *     The exception to this rule is the NULL menu. If NULL is chosen to be
 *     activated, then no InitFunction is called; additionally, the menu is
 *     exited.
 *   2: On entry into the function, points to a memory location containing the
 *     index among the active menu's Entries array corresponding to the active
 *     menu entry. On exit from the function, the menu entry index may be
 *     modified to a new one, in which case the function has chosen to
 *     activate that new menu entry. If the menu itself has changed, the code
 *     in ReGBA_Menu will activate the first item of that menu.
 */
typedef void (*MenuModifyFunction) (struct Menu**, uint32_t*);

/*
 * MenuEntryDisplayFunction is the type of a function that displays an element
 * (the name or the value, depending on which member receives a function of
 * this type) of a single menu entry.
 * Input:
 *   1: A pointer to the data for the menu entry whose part is being drawn.
 *   2: A pointer to the data for the active menu entry.
 */
typedef void (*MenuEntryDisplayFunction) (struct MenuEntry*, struct MenuEntry*);

/*
 * MenuEntryFunction is the type of a function that displays an element
 * (the name or the value, depending on which member receives a function of
 * this type) of a single menu entry.
 * Input:
 *   1: The menu entry whose part is being drawn.
 *   2: The active menu entry.
 */
typedef void (*MenuEntryFunction) (struct Menu*, struct MenuEntry*);

/*
 * MenuFunction is the type of a function that runs when a menu is being
 * initialised or finalised, depending on which member receives a function of
 * this type.
 * Input:
 *   1: The menu that is being initialised or finalised.
 */
typedef void (*MenuFunction) (struct Menu*);

/*
 * MenuPersistenceFunction is the type of a function that runs to load or save
 * the value of a persistent setting in a file.
 * Input:
 *   1: The menu entry whose setting's value is being loaded or saved.
 *   2: The text value read from the configuration file, or a buffer with 256
 *   entries to write the configuration name and value to (setting = value).
 */
typedef void (*MenuPersistenceFunction) (struct MenuEntry*, char*);

struct MenuEntry {
	enum MenuEntryKind Kind;
	char* Name;
	char* PersistentName;
	enum MenuDataType DisplayType;
	uint32_t Position;      // 0-based line number with default functions.
	                        // Custom display functions may give it a new
	                        // meaning.
	void* Target;           // With KIND_OPTION, must point to uint32_t.
	                        // With KIND_DISPLAY, must point to the data type
	                        // specified by DisplayType.
	                        // With KIND_SUBMENU, this is struct Menu*.
	// DisplayChoices and ChoiceCount are only used with KIND_OPTION.
	uint32_t ChoiceCount;
	MenuModifyFunction ButtonEnterFunction;
	MenuEntryFunction ButtonLeftFunction;
	MenuEntryFunction ButtonRightFunction;
	MenuEntryDisplayFunction DisplayNameFunction;
	MenuEntryDisplayFunction DisplayValueFunction;
	MenuPersistenceFunction LoadFunction;
	MenuPersistenceFunction SaveFunction;
	void* UserData;
	struct {
		char* Pretty;
		char* Persistent;
	} Choices[];
};

struct Menu {
	struct Menu* Parent;
	char* Title;
	MenuFunction DisplayBackgroundFunction;
	MenuFunction DisplayTitleFunction;
	MenuEntryFunction DisplayDataFunction;
	MenuModifyFunction ButtonUpFunction;
	MenuModifyFunction ButtonDownFunction;
	MenuModifyFunction ButtonLeaveFunction;
	MenuFunction InitFunction;
	MenuFunction EndFunction;
	void* UserData;
	uint32_t ActiveEntryIndex;
	struct MenuEntry* Entries[]; // Entries are ended by a NULL pointer value.
};

static void DefaultUpFunction(struct Menu** ActiveMenu, uint32_t* ActiveMenuEntryIndex)
{
	if (*ActiveMenuEntryIndex == 0)  // went over the top
	{  // go back to the bottom
		while ((*ActiveMenu)->Entries[*ActiveMenuEntryIndex] != NULL)
			(*ActiveMenuEntryIndex)++;
	}
	(*ActiveMenuEntryIndex)--;
}

static void DefaultDownFunction(struct Menu** ActiveMenu, uint32_t* ActiveMenuEntryIndex)
{
	(*ActiveMenuEntryIndex)++;
	if ((*ActiveMenu)->Entries[*ActiveMenuEntryIndex] == NULL)  // fell through the bottom
		*ActiveMenuEntryIndex = 0;  // go back to the top
}

static void DefaultRightFunction(struct Menu* ActiveMenu, struct MenuEntry* ActiveMenuEntry)
{
	if (ActiveMenuEntry->Kind == KIND_OPTION)
	{
		uint32_t* Target = (uint32_t*) ActiveMenuEntry->Target;
		(*Target)++;
		if (*Target >= ActiveMenuEntry->ChoiceCount)
			*Target = 0;
	}
}

static void DefaultLeftFunction(struct Menu* ActiveMenu, struct MenuEntry* ActiveMenuEntry)
{
	if (ActiveMenuEntry->Kind == KIND_OPTION)
	{
		uint32_t* Target = (uint32_t*) ActiveMenuEntry->Target;
		if (*Target == 0)
			*Target = ActiveMenuEntry->ChoiceCount;
		(*Target)--;
	}
}

static void DefaultEnterFunction(struct Menu** ActiveMenu, uint32_t* ActiveMenuEntryIndex)
{
	if ((*ActiveMenu)->Entries[*ActiveMenuEntryIndex]->Kind == KIND_SUBMENU)
	{
		*ActiveMenu = (struct Menu*) (*ActiveMenu)->Entries[*ActiveMenuEntryIndex]->Target;
	}
}

static void DefaultLeaveFunction(struct Menu** ActiveMenu, uint32_t* ActiveMenuEntryIndex)
{
	*ActiveMenu = (*ActiveMenu)->Parent;
}

static void DefaultDisplayNameFunction(struct MenuEntry* DrawnMenuEntry, struct MenuEntry* ActiveMenuEntry)
{
	uint32_t TextWidth = GetRenderedWidth(DrawnMenuEntry->Name);
	if (TextWidth <= GCW0_SCREEN_WIDTH - 2)
	{
		bool IsActive = (DrawnMenuEntry == ActiveMenuEntry);
		uint16_t TextColor = IsActive ? COLOR_ACTIVE_TEXT : COLOR_INACTIVE_TEXT;
		uint16_t OutlineColor = IsActive ? COLOR_ACTIVE_OUTLINE : COLOR_INACTIVE_OUTLINE;
		print_string_outline(DrawnMenuEntry->Name, TextColor, OutlineColor, 1, GetRenderedHeight(" ") * (DrawnMenuEntry->Position + 2) + 1);
	}
	else
		ReGBA_Trace("W: Hid name '%s' from the menu due to it being too long", DrawnMenuEntry->Name);
}

static void print_u64(char* Result, uint64_t Value)
{
	if (Value == 0)
		strcpy(Result, "0");
	else
	{
		uint_fast8_t Length = 0;
		uint64_t Temp = Value;
		while (Temp > 0)
		{
			Temp /= 10;
			Length++;
		}
		Result[Length] = '\0';
		while (Value > 0)
		{
			Length--;
			Result[Length] = '0' + (Value % 10);
			Value /= 10;
		}
	}
}

static void print_i64(char* Result, int64_t Value)
{
	if (Value < -9223372036854775807)
		strcpy(Result, "-9223372036854775808");
	else if (Value < 0)
	{
		Result[0] = '-';
		print_u64(Result + 1, (uint64_t) -Value);
	}
	else
		print_u64(Result, (uint64_t) Value);
}

static void DefaultDisplayValueFunction(struct MenuEntry* DrawnMenuEntry, struct MenuEntry* ActiveMenuEntry)
{
	if (DrawnMenuEntry->Kind == KIND_OPTION || DrawnMenuEntry->Kind == KIND_DISPLAY)
	{
		char* Value;
		char Temp[21];
		bool Error = false;
		if (DrawnMenuEntry->Kind == KIND_OPTION)
		{
			if (*(uint32_t*) DrawnMenuEntry->Target < DrawnMenuEntry->ChoiceCount)
				Value = DrawnMenuEntry->Choices[*(uint32_t*) DrawnMenuEntry->Target].Pretty;
			else
			{
				Value = "Out of bounds";
				Error = true;
			}
		}
		else if (DrawnMenuEntry->Kind == KIND_DISPLAY)
		{
			switch (DrawnMenuEntry->DisplayType)
			{
				case TYPE_STRING:
					Value = (char*) DrawnMenuEntry->Target;
					break;
				case TYPE_INT32:
					sprintf(Temp, "%" PRIi32, *(int32_t*) DrawnMenuEntry->Target);
					Value = Temp;
					break;
				case TYPE_UINT32:
					sprintf(Temp, "%" PRIu32, *(uint32_t*) DrawnMenuEntry->Target);
					Value = Temp;
					break;
				case TYPE_INT64:
					print_i64(Temp, *(int64_t*) DrawnMenuEntry->Target);
					Value = Temp;
					break;
				case TYPE_UINT64:
					print_u64(Temp, *(uint64_t*) DrawnMenuEntry->Target);
					Value = Temp;
					break;
				default:
					Value = "Unknown type";
					Error = true;
					break;
			}
		}
		uint32_t TextWidth = GetRenderedWidth(Value);
		if (TextWidth <= GCW0_SCREEN_WIDTH - 2)
		{
			bool IsActive = (DrawnMenuEntry == ActiveMenuEntry);
			uint16_t TextColor = Error ? COLOR_ERROR_TEXT : (IsActive ? COLOR_ACTIVE_TEXT : COLOR_INACTIVE_TEXT);
			uint16_t OutlineColor = Error ? COLOR_ERROR_OUTLINE : (IsActive ? COLOR_ACTIVE_OUTLINE : COLOR_INACTIVE_OUTLINE);
			print_string_outline(Value, TextColor, OutlineColor, GCW0_SCREEN_WIDTH - TextWidth - 1, GetRenderedHeight(" ") * (DrawnMenuEntry->Position + 2) + 1);
		}
		else
			ReGBA_Trace("W: Hid value '%s' from the menu due to it being too long", Value);
	}
}

static void DefaultDisplayBackgroundFunction(struct Menu* ActiveMenu)
{
	SDL_FillRect(OutputSurface, &ScreenRectangle, COLOR_BACKGROUND);
}

static void DefaultDisplayDataFunction(struct Menu* ActiveMenu, struct MenuEntry* ActiveMenuEntry)
{
	uint32_t DrawnMenuEntryIndex = 0;
	struct MenuEntry* DrawnMenuEntry = ActiveMenu->Entries[0];
	for (; DrawnMenuEntry != NULL; DrawnMenuEntryIndex++, DrawnMenuEntry = ActiveMenu->Entries[DrawnMenuEntryIndex])
	{
		MenuEntryDisplayFunction Function = DrawnMenuEntry->DisplayNameFunction;
		if (Function == NULL) Function = &DefaultDisplayNameFunction;
		(*Function)(DrawnMenuEntry, ActiveMenuEntry);

		Function = DrawnMenuEntry->DisplayValueFunction;
		if (Function == NULL) Function = &DefaultDisplayValueFunction;
		(*Function)(DrawnMenuEntry, ActiveMenuEntry);

		DrawnMenuEntry++;
	}
}

static void DefaultDisplayTitleFunction(struct Menu* ActiveMenu)
{
	uint32_t TextWidth = GetRenderedWidth(ActiveMenu->Title);
	if (TextWidth <= GCW0_SCREEN_WIDTH - 2)
		print_string_outline(ActiveMenu->Title, COLOR_TITLE_TEXT, COLOR_TITLE_OUTLINE, (GCW0_SCREEN_WIDTH - TextWidth) / 2, 1);
	else
		ReGBA_Trace("W: Hid title '%s' from the menu due to it being too long", ActiveMenu->Title);
}

static void DefaultLoadFunction(struct MenuEntry* ActiveMenuEntry, char* Value)
{
	uint32_t i;
	for (i = 0; i < ActiveMenuEntry->ChoiceCount; i++)
	{
		if (strcasecmp(ActiveMenuEntry->Choices[i].Persistent, Value) == 0)
		{
			*(uint32_t*) ActiveMenuEntry->Target = i;
			return;
		}
	}
	ReGBA_Trace("W: Value '%s' for option '%s' not valid; ignored", Value, ActiveMenuEntry->PersistentName);
}

static void DefaultSaveFunction(struct MenuEntry* ActiveMenuEntry, char* Value)
{
	snprintf(Value, 256, "%s = %s #%s\n", ActiveMenuEntry->PersistentName,
		ActiveMenuEntry->Choices[*((uint32_t*) ActiveMenuEntry->Target)].Persistent,
		ActiveMenuEntry->Choices[*((uint32_t*) ActiveMenuEntry->Target)].Pretty);
}

// -- Custom display --

static char* OpenDinguxButtonText[OPENDINGUX_BUTTON_COUNT] = {
	"L",
	"R",
	"D-pad Down",
	"D-pad Up",
	"D-pad Left",
	"D-pad Right",
	"Start",
	"Select",
	"B",
	"A",
	LEFT_FACE_BUTTON_NAME,
	TOP_FACE_BUTTON_NAME,
	"Analog Down",
	"Analog Up",
	"Analog Left",
	"Analog Right",
};

/*
 * Retrieves the button text for a single OpenDingux button.
 * Input:
 *   Button: The single button to describe. If this value is 0, the value is
 *   considered valid and "None" is the description text.
 * Output:
 *   Valid: A pointer to a Boolean variable which is updated with true if
 *     Button was a single button or none, or false otherwise.
 * Returns:
 *   A pointer to a null-terminated string describing Button. This string must
 *   never be freed, as it is statically allocated.
 */
static char* GetButtonText(enum OpenDingux_Buttons Button, bool* Valid)
{
	uint_fast8_t i;
	if (Button == 0)
	{
		*Valid = true;
		return "None";
	}
	else
	{
		for (i = 0; i < OPENDINGUX_BUTTON_COUNT; i++)
		{
			if (Button == 1 << i)
			{
				*Valid = true;
				return OpenDinguxButtonText[i];
			}
		}
		*Valid = false;
		return "Invalid";
	}
}

/*
 * Retrieves the button text for an OpenDingux button combination.
 * Input:
 *   Button: The buttons to describe. If this value is 0, the description text
 *   is "None". If there are multiple buttons in the bitfield, they are all
 *   added, separated by a '+' character.
 * Output:
 *   Result: A pointer to a buffer which is updated with the description of
 *   the button combination.
 */
static void GetButtonsText(enum OpenDingux_Buttons Buttons, char* Result)
{
	uint_fast8_t i;
	if (Buttons == 0)
	{
		strcpy(Result, "None");
	}
	else
	{
		Result[0] = '\0';
		bool AfterFirst = false;
		for (i = 0; i < OPENDINGUX_BUTTON_COUNT; i++)
		{
			if ((Buttons & 1 << i) != 0)
			{
				if (AfterFirst)
					strcat(Result, "+");
				AfterFirst = true;
				strcat(Result, OpenDinguxButtonText[i]);
			}
		}
	}
}

static void DisplayButtonMappingValue(struct MenuEntry* DrawnMenuEntry, struct MenuEntry* ActiveMenuEntry)
{
	bool Valid;
	char* Value = GetButtonText(*(uint32_t*) DrawnMenuEntry->Target, &Valid);

	uint32_t TextWidth = GetRenderedWidth(Value);
	if (TextWidth <= GCW0_SCREEN_WIDTH - 2)
	{
		bool IsActive = (DrawnMenuEntry == ActiveMenuEntry);
		uint16_t TextColor = Valid ? (IsActive ? COLOR_ACTIVE_TEXT : COLOR_INACTIVE_TEXT) : COLOR_ERROR_TEXT;
		uint16_t OutlineColor = Valid ? (IsActive ? COLOR_ACTIVE_OUTLINE : COLOR_INACTIVE_OUTLINE) : COLOR_ERROR_OUTLINE;
		print_string_outline(Value, TextColor, OutlineColor, GCW0_SCREEN_WIDTH - TextWidth - 1, GetRenderedHeight(" ") * (DrawnMenuEntry->Position + 2) + 1);
	}
	else
		ReGBA_Trace("W: Hid value '%s' from the menu due to it being too long", Value);
}

static void DisplayHotkeyValue(struct MenuEntry* DrawnMenuEntry, struct MenuEntry* ActiveMenuEntry)
{
	char Value[256];
	GetButtonsText(*(uint32_t*) DrawnMenuEntry->Target, Value);

	uint32_t TextWidth = GetRenderedWidth(Value);
	if (TextWidth <= GCW0_SCREEN_WIDTH - 2)
	{
		bool IsActive = (DrawnMenuEntry == ActiveMenuEntry);
		uint16_t TextColor = IsActive ? COLOR_ACTIVE_TEXT : COLOR_INACTIVE_TEXT;
		uint16_t OutlineColor = IsActive ? COLOR_ACTIVE_OUTLINE : COLOR_INACTIVE_OUTLINE;
		print_string_outline(Value, TextColor, OutlineColor, GCW0_SCREEN_WIDTH - TextWidth - 1, GetRenderedHeight(" ") * (DrawnMenuEntry->Position + 2) + 1);
	}
	else
		ReGBA_Trace("W: Hid value '%s' from the menu due to it being too long", Value);
}

// -- Custom saving --

static char OpenDinguxButtonSave[OPENDINGUX_BUTTON_COUNT] = {
	'L',
	'R',
	'v', // D-pad directions.
	'^',
	'<',
	'>', // (end)
	'S',
	's',
	'B',
	'A',
	'Y', // Using the SNES/DS/A320 mapping, this is the left face button.
	'X', // Using the SNES/DS/A320 mapping, this is the upper face button.
	'd', // Analog nub directions (GCW Zero).
	'u',
	'l',
	'r', // (end)
};

static void LoadMappingFunction(struct MenuEntry* ActiveMenuEntry, char* Value)
{
	uint32_t Mapping = 0;
	if (Value[0] != 'x')
	{
		uint_fast8_t i;
		for (i = 0; i < OPENDINGUX_BUTTON_COUNT; i++)
			if (Value[0] == OpenDinguxButtonSave[i])
			{
				Mapping = 1 << i;
				break;
			}
	}
	*(uint32_t*) ActiveMenuEntry->Target = Mapping;
}

static void SaveMappingFunction(struct MenuEntry* ActiveMenuEntry, char* Value)
{
	char Temp[32];
	Temp[0] = '\0';
	uint_fast8_t i;
	for (i = 0; i < OPENDINGUX_BUTTON_COUNT; i++)
		if (*(uint32_t*) ActiveMenuEntry->Target == 1 << i)
		{
			Temp[0] = OpenDinguxButtonSave[i];
			sprintf(&Temp[1], " #%s", OpenDinguxButtonText[i]);
			break;
		}
	if (Temp[0] == '\0')
		strcpy(Temp, "x #None");
	snprintf(Value, 256, "%s = %s\n", ActiveMenuEntry->PersistentName, Temp);
}

static void LoadHotkeyFunction(struct MenuEntry* ActiveMenuEntry, char* Value)
{
	uint32_t Hotkey = 0;
	if (Value[0] != 'x')
	{
		char* Ptr = Value;
		while (*Ptr)
		{
			uint_fast8_t i;
			for (i = 0; i < OPENDINGUX_BUTTON_COUNT; i++)
				if (*Ptr == OpenDinguxButtonSave[i])
				{
					Hotkey |= 1 << i;
					break;
				}
			Ptr++;
		}
	}
	*(uint32_t*) ActiveMenuEntry->Target = Hotkey;
}

static void SaveHotkeyFunction(struct MenuEntry* ActiveMenuEntry, char* Value)
{
	char Temp[192];
	char* Ptr = Temp;
	uint_fast8_t i;
	for (i = 0; i < OPENDINGUX_BUTTON_COUNT; i++)
		if ((*(uint32_t*) ActiveMenuEntry->Target & (1 << i)) != 0)
		{
			*Ptr++ = OpenDinguxButtonSave[i];
		}
	if (Ptr == Temp)
		strcpy(Temp, "x #None");
	else
	{
		*Ptr++ = ' ';
		*Ptr++ = '#';
		GetButtonsText(*(uint32_t*) ActiveMenuEntry->Target, Ptr);
	}
	snprintf(Value, 256, "%s = %s\n", ActiveMenuEntry->PersistentName, Temp);
}

// -- Custom actions --

static void ActionExit(struct Menu** ActiveMenu, uint32_t* ActiveMenuEntryIndex)
{
	quit();
	*ActiveMenu = NULL;
}

static void ActionReturn(struct Menu** ActiveMenu, uint32_t* ActiveMenuEntryIndex)
{
	*ActiveMenu = NULL;
}

static void ActionReset(struct Menu** ActiveMenu, uint32_t* ActiveMenuEntryIndex)
{
	reset_gba();
	reg[CHANGED_PC_STATUS] = 1;
	*ActiveMenu = NULL;
}

static void NullLeftFunction(struct Menu* ActiveMenu, struct MenuEntry* ActiveMenuEntry)
{
}

static void NullRightFunction(struct Menu* ActiveMenu, struct MenuEntry* ActiveMenuEntry)
{
}

static enum OpenDingux_Buttons GrabButton(struct Menu* ActiveMenu, char* Lines[4])
{
	enum OpenDingux_Buttons Buttons;
	// Wait for the buttons that triggered the action to be released.
	while (GetPressedOpenDinguxButtons() != 0)
	{
		DefaultDisplayBackgroundFunction(ActiveMenu);
		SDL_Flip(OutputSurface);
		usleep(5000); // for platforms that don't sync their flips
	}
	// Wait until a button is pressed.
	while ((Buttons = GetPressedOpenDinguxButtons()) == 0)
	{
		DefaultDisplayBackgroundFunction(ActiveMenu);
		uint32_t Line;
		for (Line = 0; Line < 4; Line++)
		{
			uint32_t TextWidth = GetRenderedWidth(Lines[Line]);
			if (TextWidth <= GCW0_SCREEN_WIDTH - 2)
				print_string_outline(Lines[Line], COLOR_ACTIVE_TEXT, COLOR_ACTIVE_OUTLINE, (GCW0_SCREEN_WIDTH - TextWidth) / 2, (GCW0_SCREEN_HEIGHT - GetRenderedHeight(" ") * 4) / 2 + GetRenderedHeight(" ") * Line);
			else
				ReGBA_Trace("E: '%s' doesn't fit the screen! Fix this, Nebuleon!", Lines[Line]);
		}
		SDL_Flip(OutputSurface);
		usleep(5000); // for platforms that don't sync their flips
	}
	// Accumulate buttons until they're all released.
	enum OpenDingux_Buttons ButtonTotal = Buttons;
	while ((Buttons = GetPressedOpenDinguxButtons()) != 0)
	{
		ButtonTotal |= Buttons;
		DefaultDisplayBackgroundFunction(ActiveMenu);
		SDL_Flip(OutputSurface);
		usleep(5000); // for platforms that don't sync their flips
	}
	return ButtonTotal;
}

static enum OpenDingux_Buttons GrabButtons(struct Menu* ActiveMenu, char* Lines[4])
{
	enum OpenDingux_Buttons Buttons;
	// Wait for the buttons that triggered the action to be released.
	while (GetPressedOpenDinguxButtons() != 0)
	{
		DefaultDisplayBackgroundFunction(ActiveMenu);
		SDL_Flip(OutputSurface);
		usleep(5000); // for platforms that don't sync their flips
	}
	// Wait until a button is pressed.
	while ((Buttons = GetPressedOpenDinguxButtons()) == 0)
	{
		DefaultDisplayBackgroundFunction(ActiveMenu);
		uint32_t Line;
		for (Line = 0; Line < 4; Line++)
		{
			uint32_t TextWidth = GetRenderedWidth(Lines[Line]);
			if (TextWidth <= GCW0_SCREEN_WIDTH - 2)
				print_string_outline(Lines[Line], COLOR_ACTIVE_TEXT, COLOR_ACTIVE_OUTLINE, (GCW0_SCREEN_WIDTH - TextWidth) / 2, (GCW0_SCREEN_HEIGHT - GetRenderedHeight(" ") * 4) / 2 + GetRenderedHeight(" ") * Line);
			else
				ReGBA_Trace("E: '%s' doesn't fit the screen! Fix this, Nebuleon!", Lines[Line]);
		}
		SDL_Flip(OutputSurface);
		usleep(5000); // for platforms that don't sync their flips
	}
	// Accumulate buttons until they're all released.
	enum OpenDingux_Buttons ButtonTotal = Buttons;
	while ((Buttons = GetPressedOpenDinguxButtons()) != 0)
	{
		// a) If the old buttons are a strict subset of the new buttons,
		//    add the new buttons.
		if ((Buttons | ButtonTotal) == Buttons)
			ButtonTotal |= Buttons;
		// b) If the new buttons are a strict subset of the old buttons,
		//    do nothing. (The user is releasing the buttons to return.)
		else if ((Buttons | ButtonTotal) == ButtonTotal)
			;
		// c) If the new buttons are on another path, replace the buttons
		//    completely, for example, R+X turning into R+Y.
		else
			ButtonTotal = Buttons;
		DefaultDisplayBackgroundFunction(ActiveMenu);
		SDL_Flip(OutputSurface);
		usleep(5000); // for platforms that don't sync their flips
	}
	return ButtonTotal;
}

static void ActionSetMapping(struct Menu** ActiveMenu, uint32_t* ActiveMenuEntryIndex)
{
	char Text[256];
	char* Lines[] = { &Text[0], &Text[64], &Text[128], &Text[192] };
	bool Valid;
	sprintf(Lines[0], "Setting mapping for %s", (*ActiveMenu)->Entries[*ActiveMenuEntryIndex]->Name);
	sprintf(Lines[1], "Currently %s", GetButtonText(*(uint32_t*) (*ActiveMenu)->Entries[*ActiveMenuEntryIndex]->Target, &Valid));
	strcpy(Lines[2], "Press the new button or");
	strcpy(Lines[3], "two at once to leave alone");

	enum OpenDingux_Buttons ButtonTotal = GrabButton(*ActiveMenu, Lines);
	// If there's more than one button, change nothing.
	uint_fast8_t BitCount = 0, i;
	for (i = 0; i < OPENDINGUX_BUTTON_COUNT; i++)
		if ((ButtonTotal & (1 << i)) != 0)
			BitCount++;
	if (BitCount == 1)
		*(uint32_t*) (*ActiveMenu)->Entries[*ActiveMenuEntryIndex]->Target = ButtonTotal;
}

static void ActionSetOrClearMapping(struct Menu** ActiveMenu, uint32_t* ActiveMenuEntryIndex)
{
	char Text[256];
	char* Lines[] = { &Text[0], &Text[64], &Text[128], &Text[192] };
	bool Valid;
	sprintf(Lines[0], "Setting mapping for %s", (*ActiveMenu)->Entries[*ActiveMenuEntryIndex]->Name);
	sprintf(Lines[1], "Currently %s", GetButtonText(*(uint32_t*) (*ActiveMenu)->Entries[*ActiveMenuEntryIndex]->Target, &Valid));
	strcpy(Lines[2], "Press the new button or");
	strcpy(Lines[3], "two at once to clear");

	enum OpenDingux_Buttons ButtonTotal = GrabButton(*ActiveMenu, Lines);
	// If there's more than one button, clear the mapping.
	uint_fast8_t BitCount = 0, i;
	for (i = 0; i < OPENDINGUX_BUTTON_COUNT; i++)
		if ((ButtonTotal & (1 << i)) != 0)
			BitCount++;
	*(uint32_t*) (*ActiveMenu)->Entries[*ActiveMenuEntryIndex]->Target = (BitCount == 1)
		? ButtonTotal
		: 0;
}

static void ActionSetOrClearHotkey(struct Menu** ActiveMenu, uint32_t* ActiveMenuEntryIndex)
{
	char Text[256];
	char* Lines[] = { &Text[0], &Text[64], &Text[128], &Text[192] };
	sprintf(Lines[0], "Setting hotkey for %s", (*ActiveMenu)->Entries[*ActiveMenuEntryIndex]->Name);
	GetButtonsText(*(uint32_t*) (*ActiveMenu)->Entries[*ActiveMenuEntryIndex]->Target, Lines[2]);
	sprintf(Lines[1], "Currently %s", Lines[2]);
	strcpy(Lines[2], "Press the new buttons or");
	strcpy(Lines[3], "B to clear");

	enum OpenDingux_Buttons ButtonTotal = GrabButtons(*ActiveMenu, Lines);
	*(uint32_t*) (*ActiveMenu)->Entries[*ActiveMenuEntryIndex]->Target = (ButtonTotal == OPENDINGUX_BUTTON_FACE_DOWN)
		? 0
		: ButtonTotal;
}

// -- Forward declarations --

static struct Menu MainMenu;
static struct Menu DebugMenu;

// -- Debug > Native code stats --

static struct MenuEntry NativeCodeMenu_ROPeak = {
	.Kind = KIND_DISPLAY, .Position = 0, .Name = "Read-only bytes at peak",
	.DisplayType = TYPE_UINT64, .Target = &Stats.TranslationBytesPeak[TRANSLATION_REGION_READONLY]
};

static struct MenuEntry NativeCodeMenu_RWPeak = {
	.Kind = KIND_DISPLAY, .Position = 1, .Name = "Writable bytes at peak",
	.DisplayType = TYPE_UINT64, .Target = &Stats.TranslationBytesPeak[TRANSLATION_REGION_WRITABLE]
};

static struct MenuEntry NativeCodeMenu_ROFlushed = {
	.Kind = KIND_DISPLAY, .Position = 2, .Name = "Read-only bytes flushed",
	.DisplayType = TYPE_UINT64, .Target = &Stats.TranslationBytesFlushed[TRANSLATION_REGION_READONLY]
};

static struct MenuEntry NativeCodeMenu_RWFlushed = {
	.Kind = KIND_DISPLAY, .Position = 3, .Name = "Writable bytes flushed",
	.DisplayType = TYPE_UINT64, .Target = &Stats.TranslationBytesFlushed[TRANSLATION_REGION_WRITABLE]
};

static struct Menu NativeCodeMenu = {
	.Parent = &DebugMenu, .Title = "Native code statistics",
	.Entries = { &NativeCodeMenu_ROPeak, &NativeCodeMenu_RWPeak, &NativeCodeMenu_ROFlushed, &NativeCodeMenu_RWFlushed, NULL }
};

static struct MenuEntry DebugMenu_NativeCode = {
	.Kind = KIND_SUBMENU, .Position = 0, .Name = "Native code statistics...",
	.Target = &NativeCodeMenu
};

// -- Debug > Metadata stats --

static struct MenuEntry MetadataMenu_ROFull = {
	.Kind = KIND_DISPLAY, .Position = 0, .Name = "Read-only area full",
	.DisplayType = TYPE_UINT64, .Target = &Stats.TranslationFlushCount[TRANSLATION_REGION_READONLY][FLUSH_REASON_FULL_CACHE]
};

static struct MenuEntry MetadataMenu_RWFull = {
	.Kind = KIND_DISPLAY,.Position = 1, .Name = "Writable area full",
	.DisplayType = TYPE_UINT64, .Target = &Stats.TranslationFlushCount[TRANSLATION_REGION_WRITABLE][FLUSH_REASON_FULL_CACHE]
};

static struct MenuEntry MetadataMenu_BIOSLastTag = {
	.Kind = KIND_DISPLAY, .Position = 2, .Name = "BIOS tags full",
	.DisplayType = TYPE_UINT64, .Target = &Stats.MetadataClearCount[METADATA_AREA_BIOS][CLEAR_REASON_LAST_TAG]
};

static struct MenuEntry MetadataMenu_EWRAMLastTag = {
	.Kind = KIND_DISPLAY, .Position = 3, .Name = "EWRAM tags full",
	.DisplayType = TYPE_UINT64, .Target = &Stats.MetadataClearCount[METADATA_AREA_EWRAM][CLEAR_REASON_LAST_TAG]
};

static struct MenuEntry MetadataMenu_IWRAMLastTag = {
	.Kind = KIND_DISPLAY, .Position = 4, .Name = "IWRAM tags full",
	.DisplayType = TYPE_UINT64, .Target = &Stats.MetadataClearCount[METADATA_AREA_IWRAM][CLEAR_REASON_LAST_TAG]
};

static struct MenuEntry MetadataMenu_VRAMLastTag = {
	.Kind = KIND_DISPLAY, .Position = 5, .Name = "VRAM tags full",
	.DisplayType = TYPE_UINT64, .Target = &Stats.MetadataClearCount[METADATA_AREA_VRAM][CLEAR_REASON_LAST_TAG]
};

static struct MenuEntry MetadataMenu_PartialClears = {
	.Kind = KIND_DISPLAY, .Position = 7, .Name = "Partial clears",
	.DisplayType = TYPE_UINT64, .Target = &Stats.PartialFlushCount
};

static struct Menu MetadataMenu = {
	.Parent = &DebugMenu, .Title = "Metadata clear statistics",
	.Entries = { &MetadataMenu_ROFull, &MetadataMenu_RWFull, &MetadataMenu_BIOSLastTag, &MetadataMenu_EWRAMLastTag, &MetadataMenu_IWRAMLastTag, &MetadataMenu_VRAMLastTag, &MetadataMenu_PartialClears, NULL }
};

static struct MenuEntry DebugMenu_Metadata = {
	.Kind = KIND_SUBMENU, .Position = 1, .Name = "Metadata clear statistics...",
	.Target = &MetadataMenu
};

// -- Debug > Execution stats --

static struct MenuEntry ExecutionMenu_SoundUnderruns = {
	.Kind = KIND_DISPLAY, .Position = 0, .Name = "Sound buffer underruns",
	.DisplayType = TYPE_UINT64, .Target = &Stats.SoundBufferUnderrunCount
};

static struct MenuEntry ExecutionMenu_FramesEmulated = {
	.Kind = KIND_DISPLAY, .Position = 1, .Name = "Frames emulated",
	.DisplayType = TYPE_UINT64, .Target = &Stats.TotalEmulatedFrames
};

#ifdef PERFORMANCE_IMPACTING_STATISTICS
static struct MenuEntry ExecutionMenu_ARMOps = {
	.Kind = KIND_DISPLAY, .Position = 2, .Name = "ARM opcodes decoded",
	.DisplayType = TYPE_UINT64, .Target = &Stats.ARMOpcodesDecoded
};

static struct MenuEntry ExecutionMenu_ThumbOps = {
	.Kind = KIND_DISPLAY, .Position = 3, .Name = "Thumb opcodes decoded",
	.DisplayType = TYPE_UINT64, .Target = &Stats.ThumbOpcodesDecoded
};

static struct MenuEntry ExecutionMenu_MemAccessors = {
	.Kind = KIND_DISPLAY, .Position = 4, .Name = "Memory accessors patched",
	.DisplayType = TYPE_UINT32, .Target = &Stats.WrongAddressLineCount
};
#endif

static struct Menu ExecutionMenu = {
	.Parent = &DebugMenu, .Title = "Execution statistics",
	.Entries = { &ExecutionMenu_SoundUnderruns, &ExecutionMenu_FramesEmulated
#ifdef PERFORMANCE_IMPACTING_STATISTICS
		, &ExecutionMenu_ARMOps, &ExecutionMenu_ThumbOps, &ExecutionMenu_MemAccessors
#endif
		, NULL }
};

static struct MenuEntry DebugMenu_Execution = {
	.Kind = KIND_SUBMENU, .Position = 2, .Name = "Execution statistics...",
	.Target = &ExecutionMenu
};

// -- Debug > Code reuse stats --

#ifdef PERFORMANCE_IMPACTING_STATISTICS
static struct MenuEntry ReuseMenu_OpsRecompiled = {
	.Kind = KIND_DISPLAY, .Position = 0, .Name = "Opcodes recompiled",
	.DisplayType = TYPE_UINT64, .Target = &Stats.OpcodeRecompilationCount
};

static struct MenuEntry ReuseMenu_BlocksRecompiled = {
	.Kind = KIND_DISPLAY, .Position = 1, .Name = "Blocks recompiled",
	.DisplayType = TYPE_UINT64, .Target = &Stats.BlockRecompilationCount
};

static struct MenuEntry ReuseMenu_OpsReused = {
	.Kind = KIND_DISPLAY, .Position = 2, .Name = "Opcodes reused",
	.DisplayType = TYPE_UINT64, .Target = &Stats.OpcodeReuseCount
};

static struct MenuEntry ReuseMenu_BlocksReused = {
	.Kind = KIND_DISPLAY, .Position = 3, .Name = "Blocks reused",
	.DisplayType = TYPE_UINT64, .Target = &Stats.BlockReuseCount
};

static struct Menu ReuseMenu = {
	.Parent = &DebugMenu, .Title = "Code reuse statistics",
	.Entries = { &ReuseMenu_OpsRecompiled, &ReuseMenu_BlocksRecompiled, &ReuseMenu_OpsReused, &ReuseMenu_BlocksReused, NULL }
};

static struct MenuEntry DebugMenu_Reuse = {
	.Kind = KIND_SUBMENU, .Position = 3, .Name = "Code reuse statistics...",
	.Target = &ReuseMenu
};
#endif

static struct MenuEntry ROMInfoMenu_GameName = {
	.Kind = KIND_DISPLAY, .Position = 0, .Name = "game_name =",
	.DisplayType = TYPE_STRING, .Target = gamepak_title
};

static struct MenuEntry ROMInfoMenu_GameCode = {
	.Kind = KIND_DISPLAY, .Position = 1, .Name = "game_code =",
	.DisplayType = TYPE_STRING, .Target = gamepak_code
};

static struct MenuEntry ROMInfoMenu_VendorCode = {
	.Kind = KIND_DISPLAY, .Position = 2, .Name = "vender_code =",
	.DisplayType = TYPE_STRING, .Target = gamepak_maker
};

static struct Menu ROMInfoMenu = {
	.Parent = &DebugMenu, .Title = "ROM information",
	.Entries = { &ROMInfoMenu_GameName, &ROMInfoMenu_GameCode, &ROMInfoMenu_VendorCode, NULL }
};

static struct MenuEntry DebugMenu_ROMInfo = {
	.Kind = KIND_SUBMENU, .Position = 5, .Name = "ROM information...",
	.Target = &ROMInfoMenu
};

// -- Debug --

static struct Menu DebugMenu = {
	.Parent = &MainMenu, .Title = "Performance and debugging",
	.Entries = { &DebugMenu_NativeCode, &DebugMenu_Metadata, &DebugMenu_Execution
#ifdef PERFORMANCE_IMPACTING_STATISTICS
		, &DebugMenu_Reuse
#endif
		, &DebugMenu_ROMInfo
		, NULL }
};

// -- Display Settings --

static struct MenuEntry DisplayMenu_BootSource = {
	.Kind = KIND_OPTION, .Position = 0, .Name = "Boot from", .PersistentName = "boot_from",
	.Target = &BootFromBIOS,
	.ChoiceCount = 2, .Choices = { { "Cartridge ROM", "cartridge" }, { "GBA BIOS", "gba_bios" } }
};

static struct MenuEntry DisplayMenu_FPSCounter = {
	.Kind = KIND_OPTION, .Position = 1, .Name = "FPS counter", .PersistentName = "fps_counter",
	.Target = &ShowFPS,
	.ChoiceCount = 2, .Choices = { { "Hide", "hide" }, { "Show", "show" } }
};

static struct MenuEntry DisplayMenu_ScaleMode = {
	.Kind = KIND_OPTION, .Position = 2, .Name = "Image scaling", .PersistentName = "image_size",
	.Target = &ScaleMode,
	.ChoiceCount = 3, .Choices = { { "Aspect", "aspect" }, { "Full", "fullscreen" }, { "None", "original" } }
};

static struct MenuEntry DisplayMenu_Frameskip = {
	.Kind = KIND_OPTION, .Position = 3, .Name = "Frame skipping", .PersistentName = "frameskip",
	.Target = &UserFrameskip,
	.ChoiceCount = 5, .Choices = { { "Automatic", "auto" }, { "0 (~60 FPS)", "0" }, { "1 (~30 FPS)", "1" }, { "2 (~20 FPS)", "2" }, { "3 (~15 FPS)", "3" } }
};

static struct MenuEntry DisplayMenu_FastForwardTarget = {
	.Kind = KIND_OPTION, .Position = 4, .Name = "Fast-forward target", .PersistentName = "fast_forward_target",
	.Target = &FastForwardTarget,
	.ChoiceCount = 5, .Choices = { { "2x (~120 FPS)", "2" }, { "3x (~180 FPS)", "3" }, { "4x (~240 FPS)", "4" }, { "5x (~300 FPS)", "5" }, { "6x (~360 FPS)", "6" } }
};

static struct Menu DisplayMenu = {
	.Parent = &MainMenu, .Title = "Display settings",
	.Entries = { &DisplayMenu_BootSource, &DisplayMenu_FPSCounter, &DisplayMenu_ScaleMode, &DisplayMenu_Frameskip, &DisplayMenu_FastForwardTarget }
};

// -- Button remapping --
static struct MenuEntry ButtonMappingMenu_A = {
	.Kind = KIND_OPTION, .Position = 0, .Name = "GBA A", .PersistentName = "gba_a",
	.Target = &KeypadRemapping[0],
	.ChoiceCount = 0,
	.ButtonLeftFunction = NullLeftFunction, .ButtonRightFunction = NullRightFunction,
	.ButtonEnterFunction = ActionSetMapping, .DisplayValueFunction = DisplayButtonMappingValue,
	.LoadFunction = LoadMappingFunction, .SaveFunction = SaveMappingFunction
};

static struct MenuEntry ButtonMappingMenu_B = {
	.Kind = KIND_OPTION, .Position = 1, .Name = "GBA B", .PersistentName = "gba_b",
	.Target = &KeypadRemapping[1],
	.ChoiceCount = 0,
	.ButtonLeftFunction = NullLeftFunction, .ButtonRightFunction = NullRightFunction,
	.ButtonEnterFunction = ActionSetMapping, .DisplayValueFunction = DisplayButtonMappingValue,
	.LoadFunction = LoadMappingFunction, .SaveFunction = SaveMappingFunction
};

static struct MenuEntry ButtonMappingMenu_Start = {
	.Kind = KIND_OPTION, .Position = 2, .Name = "GBA Start", .PersistentName = "gba_start",
	.Target = &KeypadRemapping[3],
	.ChoiceCount = 0,
	.ButtonLeftFunction = NullLeftFunction, .ButtonRightFunction = NullRightFunction,
	.ButtonEnterFunction = ActionSetMapping, .DisplayValueFunction = DisplayButtonMappingValue,
	.LoadFunction = LoadMappingFunction, .SaveFunction = SaveMappingFunction
};

static struct MenuEntry ButtonMappingMenu_Select = {
	.Kind = KIND_OPTION, .Position = 3, .Name = "GBA Select", .PersistentName = "gba_select",
	.Target = &KeypadRemapping[2],
	.ChoiceCount = 0,
	.ButtonLeftFunction = NullLeftFunction, .ButtonRightFunction = NullRightFunction,
	.ButtonEnterFunction = ActionSetMapping, .DisplayValueFunction = DisplayButtonMappingValue,
	.LoadFunction = LoadMappingFunction, .SaveFunction = SaveMappingFunction
};

static struct MenuEntry ButtonMappingMenu_L = {
	.Kind = KIND_OPTION, .Position = 4, .Name = "GBA L", .PersistentName = "gba_l",
	.Target = &KeypadRemapping[9],
	.ChoiceCount = 0,
	.ButtonLeftFunction = NullLeftFunction, .ButtonRightFunction = NullRightFunction,
	.ButtonEnterFunction = ActionSetMapping, .DisplayValueFunction = DisplayButtonMappingValue,
	.LoadFunction = LoadMappingFunction, .SaveFunction = SaveMappingFunction
};

static struct MenuEntry ButtonMappingMenu_R = {
	.Kind = KIND_OPTION, .Position = 5, .Name = "GBA R", .PersistentName = "gba_r",
	.Target = &KeypadRemapping[8],
	.ChoiceCount = 0,
	.ButtonLeftFunction = NullLeftFunction, .ButtonRightFunction = NullRightFunction,
	.ButtonEnterFunction = ActionSetMapping, .DisplayValueFunction = DisplayButtonMappingValue,
	.LoadFunction = LoadMappingFunction, .SaveFunction = SaveMappingFunction
};

static struct MenuEntry ButtonMappingMenu_RapidA = {
	.Kind = KIND_OPTION, .Position = 6, .Name = "Rapid-fire A", .PersistentName = "rapid_a",
	.Target = &KeypadRemapping[10],
	.ChoiceCount = 0,
	.ButtonLeftFunction = NullLeftFunction, .ButtonRightFunction = NullRightFunction,
	.ButtonEnterFunction = ActionSetOrClearMapping, .DisplayValueFunction = DisplayButtonMappingValue,
	.LoadFunction = LoadMappingFunction, .SaveFunction = SaveMappingFunction
};

static struct MenuEntry ButtonMappingMenu_RapidB = {
	.Kind = KIND_OPTION, .Position = 7, .Name = "Rapid-fire B", .PersistentName = "rapid_b",
	.Target = &KeypadRemapping[11],
	.ChoiceCount = 0,
	.ButtonLeftFunction = NullLeftFunction, .ButtonRightFunction = NullRightFunction,
	.ButtonEnterFunction = ActionSetOrClearMapping, .DisplayValueFunction = DisplayButtonMappingValue,
	.LoadFunction = LoadMappingFunction, .SaveFunction = SaveMappingFunction
};

#ifdef GCW_ZERO
static struct MenuEntry ButtonMappingMenu_AnalogSensitivity = {
	.Kind = KIND_OPTION, .Position = 9, .Name = "Analog sensitivity", .PersistentName = "analog_sensitivity",
	.Target = &AnalogSensitivity,
	.ChoiceCount = 5, .Choices = { { "Very low", "lowest" }, { "Low", "low" }, { "Medium", "medium" }, { "High", "high" }, { "Highest", "highest" } }
};
#endif

static struct Menu ButtonMappingMenu = {
	.Parent = &MainMenu, .Title = "Input settings",
	.Entries = { &ButtonMappingMenu_A, &ButtonMappingMenu_B, &ButtonMappingMenu_Start, &ButtonMappingMenu_Select, &ButtonMappingMenu_L, &ButtonMappingMenu_R, &ButtonMappingMenu_RapidA, &ButtonMappingMenu_RapidB
#ifdef GCW_ZERO
	, &ButtonMappingMenu_AnalogSensitivity
#endif
	, NULL }
};

// -- Hotkeys --

static struct MenuEntry HotkeyMenu_FastForward = {
	.Kind = KIND_OPTION, .Position = 0, .Name = "Fast-forward", .PersistentName = "hotkey_fast_forward",
	.Target = &Hotkeys[0],
	.ChoiceCount = 0,
	.ButtonLeftFunction = NullLeftFunction, .ButtonRightFunction = NullRightFunction,
	.ButtonEnterFunction = ActionSetOrClearHotkey, .DisplayValueFunction = DisplayHotkeyValue,
	.LoadFunction = LoadHotkeyFunction, .SaveFunction = SaveHotkeyFunction
};

static struct Menu HotkeyMenu = {
	.Parent = &MainMenu, .Title = "Hotkeys",
	.Entries = { &HotkeyMenu_FastForward, NULL }
};

// -- Main Menu --

static struct MenuEntry MainMenu_Display = {
	.Kind = KIND_SUBMENU, .Position = 0, .Name = "Display settings...",
	.Target = &DisplayMenu
};

static struct MenuEntry MainMenu_ButtonMapping = {
	.Kind = KIND_SUBMENU, .Position = 1, .Name = "Input settings...",
	.Target = &ButtonMappingMenu
};

static struct MenuEntry MainMenu_Hotkey = {
	.Kind = KIND_SUBMENU, .Position = 2, .Name = "Hotkeys...",
	.Target = &HotkeyMenu
};

static struct MenuEntry MainMenu_Debug = {
	.Kind = KIND_SUBMENU, .Position = 7, .Name = "Performance and debugging...",
	.Target = &DebugMenu
};

static struct MenuEntry MainMenu_Reset = {
	.Kind = KIND_CUSTOM, .Position = 9, .Name = "Reset the game",
	.ButtonEnterFunction = &ActionReset
};

static struct MenuEntry MainMenu_Return = {
	.Kind = KIND_CUSTOM, .Position = 10, .Name = "Return to the game",
	.ButtonEnterFunction = &ActionReturn
};

static struct MenuEntry MainMenu_Exit = {
	.Kind = KIND_CUSTOM, .Position = 11, .Name = "Exit",
	.ButtonEnterFunction = &ActionExit
};

static struct Menu MainMenu = {
	.Parent = NULL, .Title = "ReGBA Main Menu",
	.Entries = { &MainMenu_Display, &MainMenu_ButtonMapping, &MainMenu_Hotkey, &MainMenu_Debug, &MainMenu_Reset, &MainMenu_Return, &MainMenu_Exit, NULL }
};

u32 ReGBA_Menu(enum ReGBA_MenuEntryReason EntryReason)
{
	SDL_PauseAudio(SDL_ENABLE);
	ScaleModeUnapplied();
	struct Menu* ActiveMenu = &MainMenu;
	if (MainMenu.InitFunction != NULL)
		(*(MainMenu.InitFunction))(&MainMenu);

	while (ActiveMenu != NULL)
	{
		// Draw.
		MenuFunction DisplayBackgroundFunction = ActiveMenu->DisplayBackgroundFunction;
		if (DisplayBackgroundFunction == NULL) DisplayBackgroundFunction = DefaultDisplayBackgroundFunction;
		(*DisplayBackgroundFunction)(ActiveMenu);

		MenuFunction DisplayTitleFunction = ActiveMenu->DisplayTitleFunction;
		if (DisplayTitleFunction == NULL) DisplayTitleFunction = DefaultDisplayTitleFunction;
		(*DisplayTitleFunction)(ActiveMenu);

		MenuEntryFunction DisplayDataFunction = ActiveMenu->DisplayDataFunction;
		if (DisplayDataFunction == NULL) DisplayDataFunction = DefaultDisplayDataFunction;
		(*DisplayDataFunction)(ActiveMenu, ActiveMenu->Entries[ActiveMenu->ActiveEntryIndex]);

		SDL_Flip(OutputSurface);
		
		// Wait. (This is for platforms on which flips don't wait for vertical
		// sync.)
		usleep(5000);

		struct Menu* PreviousMenu = ActiveMenu;

		// Get input.
		enum GUI_Action Action = GetGUIAction();
		
		switch (Action)
		{
			case GUI_ACTION_ENTER:
			{
				MenuModifyFunction ButtonEnterFunction = ActiveMenu->Entries[ActiveMenu->ActiveEntryIndex]->ButtonEnterFunction;
				if (ButtonEnterFunction == NULL) ButtonEnterFunction = DefaultEnterFunction;
				(*ButtonEnterFunction)(&ActiveMenu, &ActiveMenu->ActiveEntryIndex);
				break;
			}

			case GUI_ACTION_LEAVE:
			{
				MenuModifyFunction ButtonLeaveFunction = ActiveMenu->ButtonLeaveFunction;
				if (ButtonLeaveFunction == NULL) ButtonLeaveFunction = DefaultLeaveFunction;
				(*ButtonLeaveFunction)(&ActiveMenu, &ActiveMenu->ActiveEntryIndex);
				break;
			}

			case GUI_ACTION_UP:
			{
				MenuModifyFunction ButtonUpFunction = ActiveMenu->ButtonUpFunction;
				if (ButtonUpFunction == NULL) ButtonUpFunction = DefaultUpFunction;
				(*ButtonUpFunction)(&ActiveMenu, &ActiveMenu->ActiveEntryIndex);
				break;
			}

			case GUI_ACTION_DOWN:
			{
				MenuModifyFunction ButtonDownFunction = ActiveMenu->ButtonDownFunction;
				if (ButtonDownFunction == NULL) ButtonDownFunction = DefaultDownFunction;
				(*ButtonDownFunction)(&ActiveMenu, &ActiveMenu->ActiveEntryIndex);
				break;
			}

			case GUI_ACTION_LEFT:
			{
				MenuEntryFunction ButtonLeftFunction = ActiveMenu->Entries[ActiveMenu->ActiveEntryIndex]->ButtonLeftFunction;
				if (ButtonLeftFunction == NULL) ButtonLeftFunction = DefaultLeftFunction;
				(*ButtonLeftFunction)(ActiveMenu, ActiveMenu->Entries[ActiveMenu->ActiveEntryIndex]);
				break;
			}

			case GUI_ACTION_RIGHT:
			{
				MenuEntryFunction ButtonRightFunction = ActiveMenu->Entries[ActiveMenu->ActiveEntryIndex]->ButtonRightFunction;
				if (ButtonRightFunction == NULL) ButtonRightFunction = DefaultRightFunction;
				(*ButtonRightFunction)(ActiveMenu, ActiveMenu->Entries[ActiveMenu->ActiveEntryIndex]);
				break;
			}
			
			default:
				break;
		}

		// Possibly finalise this menu and activate and initialise a new one.
		if (ActiveMenu != PreviousMenu)
		{
			if (PreviousMenu->EndFunction != NULL)
				(*(PreviousMenu->EndFunction))(PreviousMenu);
			if (ActiveMenu != NULL && ActiveMenu->InitFunction != NULL)
				(*(ActiveMenu->InitFunction))(ActiveMenu);
		}
	}

	// Avoid leaving the menu with GBA keys pressed (namely the one bound to
	// the native exit button, B).
	while (ReGBA_GetPressedButtons() != 0)
	{
		// Draw.
		SDL_Flip(OutputSurface);
		
		// Wait. (This is for platforms on which flips don't wait for vertical
		// sync.)
		usleep(5000);
	}

	SDL_PauseAudio(SDL_DISABLE);
	StatsStopFPS();
	timespec Now;
	clock_gettime(CLOCK_MONOTONIC, &Now);
	Stats.LastFPSCalculationTime = Now;
	return 0;
}

static void Menu_SaveOption(FILE_TAG_TYPE fd, struct MenuEntry *entry)
{
	char buf[257];
	MenuPersistenceFunction SaveFunction = entry->SaveFunction;
	if (SaveFunction == NULL) SaveFunction = &DefaultSaveFunction;
	(*SaveFunction)(entry, buf);
	buf[256] = '\0';

	FILE_WRITE(fd, &buf, strlen(buf) * sizeof(buf[0]));
}

static void Menu_SaveIterateRecurse(FILE_TAG_TYPE fd, struct Menu *menu)
{
	struct MenuEntry *cur;
	int i=0;

	while ((cur = menu->Entries[i++])) {
		switch (cur->Kind) {
		case KIND_SUBMENU:
			Menu_SaveIterateRecurse(fd, cur->Target);
			break;
		case KIND_OPTION:
			Menu_SaveOption(fd, cur);
			break;
		default:
			break;
		}
	}
}

static struct MenuEntry *Menu_FindByPersistentName(struct Menu *menu, char *name)
{
	struct MenuEntry *retcode = NULL;

	struct MenuEntry *cur;
	int i=0;

	while ((cur = menu->Entries[i++])) {
		switch (cur->Kind) {
		case KIND_SUBMENU:
			if ((retcode = Menu_FindByPersistentName(cur->Target, name)))
				return retcode;
			break;
		case KIND_OPTION:
			if (strcasecmp(cur->PersistentName, name) == 0)
				return cur;
			break;
		default:
			break;
		}
	}

	return retcode;
}

bool ReGBA_SaveSettings(char *cfg_name)
{
	char fname[MAX_PATH + 1];
	if (strlen(main_path) + strlen(cfg_name) + 5 /* / .cfg */ > MAX_PATH)
	{
		ReGBA_Trace("E: Somehow you hit the filename size limit :o\n");
		return false;
	}
	sprintf(fname, "%s/%s.cfg", main_path, cfg_name);
	FILE_TAG_TYPE fd;

	ReGBA_ProgressInitialise(FILE_ACTION_SAVE_GLOBAL_SETTINGS);

	FILE_OPEN(fd, fname, WRITE);
	if(FILE_CHECK_VALID(fd)) {
		Menu_SaveIterateRecurse(fd, &MainMenu);
		ReGBA_ProgressUpdate(1, 1);
		ReGBA_ProgressFinalise();
	}
	else
	{
		ReGBA_Trace("E: Couldn't open file %s for writing.\n", fname);
		ReGBA_ProgressFinalise();
		return false;
	}

	FILE_CLOSE(fd);
	return true;
}

/*
 * Fixes up impossible settings after loading them from configuration.
 */
void FixUpSettings()
{
	if (KeypadRemapping[0] == 0 || KeypadRemapping[1] == 0 || KeypadRemapping[2] == 0
	 || KeypadRemapping[3] == 0 || KeypadRemapping[4] == 0 || KeypadRemapping[5] == 0
	 || KeypadRemapping[6] == 0 || KeypadRemapping[7] == 0 || KeypadRemapping[8] == 0
	 || KeypadRemapping[9] == 0 || KeypadRemapping[12] == 0
	)
	{
		memcpy(KeypadRemapping, DefaultKeypadRemapping, sizeof(DefaultKeypadRemapping));
	}
}

void ReGBA_LoadSettings(char *cfg_name)
{
	char fname[MAX_PATH + 1];
	if (strlen(main_path) + strlen(cfg_name) + 5 /* / .cfg */ > MAX_PATH)
	{
		ReGBA_Trace("E: Somehow you hit the filename size limit :o\n");
		return;
	}
	sprintf(fname, "%s/%s.cfg", main_path, cfg_name);

	FILE_TAG_TYPE fd;

	ReGBA_ProgressInitialise(FILE_ACTION_LOAD_GLOBAL_SETTINGS);

	FILE_OPEN(fd, fname, READ);

	if(FILE_CHECK_VALID(fd)) {
		char line[257];

		while(fgets(line, 256, fd))
		{
			line[256] = '\0';

			char* opt = NULL;
			char* arg = NULL;

			char* cur = line;

			// Find the start of the option name.
			while (*cur == ' ' || *cur == '\t')
				cur++;
			// Now find where it ends.
			while (*cur && !(*cur == ' ' || *cur == '\t' || *cur == '='))
			{
				if (*cur == '#')
					continue;
				else if (opt == NULL)
					opt = cur;
				cur++;
			}
			if (opt == NULL)
				continue;
			bool WasEquals = *cur == '=';
			*cur++ = '\0';
			if (!WasEquals)
			{
				// Skip all whitespace before =.
				while (*cur == ' ' || *cur == '\t')
					cur++;
				if (*cur != '=')
					continue;
				cur++;
			}
			// Find the start of the option argument.
			while (*cur == ' ' || *cur == '\t')
				cur++;
			// Now find where it ends.
			while (*cur)
			{
				if (*cur == '#')
				{
					*cur = '\0';
					break;
				}
				else if (arg == NULL)
					arg = cur;
				cur++;
			}
			if (arg == NULL)
				continue;
			cur--;
			while (*cur == ' ' || *cur == '\t' || *cur == '\n' || *cur == '\r')
			{
				*cur = '\0';
				cur--;
			}

			struct MenuEntry* entry = Menu_FindByPersistentName(&MainMenu, opt);
			if (entry == NULL)
			{
				ReGBA_Trace("W: Option '%s' not found; ignored", opt);
			}

			MenuPersistenceFunction LoadFunction = entry->LoadFunction;
			if (LoadFunction == NULL) LoadFunction = &DefaultLoadFunction;
			(*LoadFunction)(entry, arg);
		}
		ReGBA_ProgressUpdate(1, 1);
	}
	else
	{
		ReGBA_Trace("W: Couldn't open file %s for loading.\n", fname);
	}
	FixUpSettings();
	ReGBA_ProgressFinalise();
}
