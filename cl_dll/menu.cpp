/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*
*	This product contains software technology licensed from Id
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc.
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
//
// menu.cpp
//
// generic menu handler
//
#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "com_weapons.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "draw_util.h"
#include "strl.h"

//#include "vgui_TeamFortressViewport.h"

#define MAX_MENU_STRING	512

char g_szMenuString[MAX_MENU_STRING];
char g_szPrelocalisedMenuString[MAX_MENU_STRING];

int KB_ConvertString( char *in, char **ppout );

#define MAX_COMMAND_MENU_ITEMS 128
#define MAX_COMMAND_MENU_NODES 32
#define MAX_COMMAND_MENU_NODE_ITEMS 32

struct command_menu_item_t
{
	int slot;
	char text[96];
	char command[160];
	char mapName[32];
	int teamOnly;
	int childNode;
	bool toggle;
};

struct command_menu_node_t
{
	int parentNode;
	int itemIndices[MAX_COMMAND_MENU_NODE_ITEMS];
	int itemCount;
};

static command_menu_item_t g_CommandMenuItems[MAX_COMMAND_MENU_ITEMS];
static command_menu_node_t g_CommandMenuNodes[MAX_COMMAND_MENU_NODES];
static int g_CommandMenuItemCount = 0;
static int g_CommandMenuNodeCount = 0;
static int g_CommandMenuCurrentNode = 0;
static bool g_CommandMenuActive = false;
static bool g_CommandMenuLoaded = false;

static int CommandMenu_CreateNode( int parentNode )
{
	if( g_CommandMenuNodeCount >= MAX_COMMAND_MENU_NODES )
		return -1;

	int node = g_CommandMenuNodeCount++;
	memset( &g_CommandMenuNodes[node], 0, sizeof( g_CommandMenuNodes[node] ));
	g_CommandMenuNodes[node].parentNode = parentNode;
	return node;
}

static int CommandMenu_KeyToSlot( const char *key )
{
	if( !key || !key[0] )
		return 0;
	if( key[0] >= '1' && key[0] <= '9' )
		return key[0] - '0';
	if( key[0] == '0' )
		return 10;
	return 0;
}

static int CommandMenu_AddItem( int node, const char *boundKey, const char *text,
	const char *command, const char *mapName, int teamOnly, bool toggle )
{
	if( node < 0 || node >= g_CommandMenuNodeCount ||
		g_CommandMenuItemCount >= MAX_COMMAND_MENU_ITEMS ||
		g_CommandMenuNodes[node].itemCount >= MAX_COMMAND_MENU_NODE_ITEMS )
		return -1;

	int itemIndex = g_CommandMenuItemCount++;
	command_menu_item_t &item = g_CommandMenuItems[itemIndex];
	memset( &item, 0, sizeof( item ));
	item.slot = CommandMenu_KeyToSlot( boundKey );
	item.teamOnly = teamOnly;
	item.childNode = -1;
	item.toggle = toggle;
	strlcpy( item.text, text ? text : "", sizeof( item.text ));
	strlcpy( item.command, command ? command : "", sizeof( item.command ));
	strlcpy( item.mapName, mapName ? mapName : "", sizeof( item.mapName ));

	g_CommandMenuNodes[node].itemIndices[g_CommandMenuNodes[node].itemCount++] = itemIndex;
	return itemIndex;
}

static void CommandMenu_ResetData( void )
{
	memset( g_CommandMenuItems, 0, sizeof( g_CommandMenuItems ));
	memset( g_CommandMenuNodes, 0, sizeof( g_CommandMenuNodes ));
	g_CommandMenuItemCount = 0;
	g_CommandMenuNodeCount = 0;
	g_CommandMenuCurrentNode = 0;
	g_CommandMenuLoaded = false;
}

static bool CommandMenu_ParseFile( void )
{
	CommandMenu_ResetData();
	CommandMenu_CreateNode( -1 );

	int fileLength = 0;
	byte *source = gEngfuncs.COM_LoadFile( "commandmenu.txt", 5, &fileLength );
	if( !source )
	{
		gEngfuncs.Con_Printf( "Unable to open commandmenu.txt\\n" );
		return false;
	}

	char token[1024];
	char *cursor = (char *)source;
	if( fileLength >= 3 && source[0] == 0xef && source[1] == 0xbb && source[2] == 0xbf )
		cursor += 3;

	int currentNode = 0;
	int lastItem = -1;

	while(( cursor = gEngfuncs.COM_ParseFile( cursor, token )) != NULL && token[0] )
	{
		if( !strcmp( token, "{" ))
		{
			if( lastItem >= 0 && g_CommandMenuItems[lastItem].childNode < 0 )
			{
				int child = CommandMenu_CreateNode( currentNode );
				if( child >= 0 )
				{
					g_CommandMenuItems[lastItem].childNode = child;
					currentNode = child;
				}
			}
			continue;
		}

		if( !strcmp( token, "}" ))
		{
			if( currentNode > 0 )
				currentNode = g_CommandMenuNodes[currentNode].parentNode;
			lastItem = -1;
			continue;
		}

		char mapName[32] = "";
		int teamOnly = -1;
		bool toggle = false;
		bool custom = false;

		if( !stricmp( token, "CUSTOM" ))
		{
			custom = true;
			cursor = gEngfuncs.COM_ParseFile( cursor, token );
			if( !cursor ) break;
		}
		else if( !stricmp( token, "MAP" ))
		{
			cursor = gEngfuncs.COM_ParseFile( cursor, token );
			if( !cursor ) break;
			strlcpy( mapName, token, sizeof( mapName ));
			cursor = gEngfuncs.COM_ParseFile( cursor, token );
			if( !cursor ) break;
		}
		else if( !strnicmp( token, "TEAM", 4 ))
		{
			teamOnly = atoi( token + 4 );
			cursor = gEngfuncs.COM_ParseFile( cursor, token );
			if( !cursor ) break;
		}
		else if( !strnicmp( token, "TOGGLE", 6 ))
		{
			toggle = true;
			cursor = gEngfuncs.COM_ParseFile( cursor, token );
			if( !cursor ) break;
		}

		char boundKey[16];
		strlcpy( boundKey, token, sizeof( boundKey ));

		char itemText[256];
		cursor = gEngfuncs.COM_ParseFile( cursor, itemText );
		if( !cursor ) break;

		char command[256];
		cursor = gEngfuncs.COM_ParseFile( cursor, command );
		if( !cursor ) break;

		if( custom && !stricmp( command, "!CHANGETEAM" ))
			strlcpy( command, "chooseteam", sizeof( command ));
		else if( custom && command[0] == '!' )
			command[0] = '\\0';

		lastItem = CommandMenu_AddItem( currentNode, boundKey, itemText,
			!strcmp( command, "{" ) ? "" : command, mapName, teamOnly, toggle );

		if( lastItem >= 0 && !strcmp( command, "{" ))
		{
			int child = CommandMenu_CreateNode( currentNode );
			if( child >= 0 )
			{
				g_CommandMenuItems[lastItem].childNode = child;
				currentNode = child;
			}
		}
	}

	gEngfuncs.COM_FreeFile( source );
	g_CommandMenuLoaded = g_CommandMenuNodes[0].itemCount > 0;
	if( !g_CommandMenuLoaded )
		gEngfuncs.Con_Printf( "commandmenu.txt contained no usable menu entries\\n" );
	return g_CommandMenuLoaded;
}

static bool CommandMenu_MapMatches( const char *wantedMap )
{
	if( !wantedMap || !wantedMap[0] )
		return true;

	const char *levelName = gEngfuncs.pfnGetLevelName();
	if( !levelName || !levelName[0] )
		return false;

	const char *base = strrchr( levelName, '/' );
	if( !base )
		base = strrchr( levelName, '\\\\' );
	base = base ? base + 1 : levelName;

	char currentMap[64];
	strlcpy( currentMap, base, sizeof( currentMap ));
	char *extension = strrchr( currentMap, '.' );
	if( extension )
		*extension = '\\0';

	return !stricmp( currentMap, wantedMap );
}

static bool CommandMenu_ItemVisible( const command_menu_item_t &item )
{
	if( item.teamOnly >= 0 && item.teamOnly != g_iTeamNumber )
		return false;
	return CommandMenu_MapMatches( item.mapName );
}

static int CommandMenu_FindItemBySlot( int node, int slot )
{
	if( node < 0 || node >= g_CommandMenuNodeCount )
		return -1;

	command_menu_node_t &menuNode = g_CommandMenuNodes[node];
	for( int i = 0; i < menuNode.itemCount; i++ )
	{
		int itemIndex = menuNode.itemIndices[i];
		command_menu_item_t &item = g_CommandMenuItems[itemIndex];
		if( item.slot == slot && CommandMenu_ItemVisible( item ))
			return itemIndex;
	}
	return -1;
}

static void CommandMenu_BuildDisplay( CHudMenu *hudMenu )
{
	g_szMenuString[0] = '\\0';
	strlcpy( g_szMenuString, "Command Menu\\n\\n", sizeof( g_szMenuString ));
	hudMenu->m_bitsValidSlots = 0;

	if( g_CommandMenuCurrentNode < 0 || g_CommandMenuCurrentNode >= g_CommandMenuNodeCount )
		g_CommandMenuCurrentNode = 0;

	command_menu_node_t &node = g_CommandMenuNodes[g_CommandMenuCurrentNode];
	for( int i = 0; i < node.itemCount; i++ )
	{
		command_menu_item_t &item = g_CommandMenuItems[node.itemIndices[i]];
		if( !CommandMenu_ItemVisible( item ) || item.slot < 1 || item.slot > 9 )
			continue;

		char line[160];
		const char *label = CHudTextMessage::BufferedLocaliseTextString( item.text );
		snprintf( line, sizeof( line ), "%d. %s%s\\n", item.slot,
			label ? label : item.text, item.childNode >= 0 ? "  >" : "" );
		strlcat( g_szMenuString, line, sizeof( g_szMenuString ));
		hudMenu->m_bitsValidSlots |= 1 << ( item.slot - 1 );
	}

	strlcat( g_szMenuString, g_CommandMenuCurrentNode > 0 ? "\\n0. Back\\n" : "\\n0. Close\\n",
		sizeof( g_szMenuString ));
	hudMenu->m_bitsValidSlots |= 1 << 9;
	hudMenu->m_flShutoffTime = -1;
	hudMenu->m_fWaitingForMore = FALSE;
	hudMenu->m_fMenuDisplayed = 1;
	hudMenu->m_iFlags |= HUD_DRAW;
}

static void CommandMenu_Execute( const command_menu_item_t &item )
{
	if( !item.command[0] )
		return;

	char command[224];
	if( item.toggle )
	{
		cvar_t *cvar = gEngfuncs.pfnGetCvarPointer( item.command );
		if( cvar )
			snprintf( command, sizeof( command ), "%s %d\\n", item.command, cvar->value == 0.0f ? 1 : 0 );
		else
			snprintf( command, sizeof( command ), "%s\\n", item.command );
	}
	else
	{
		snprintf( command, sizeof( command ), "%s\\n", item.command );
	}
	command[sizeof( command ) - 1] = '\\0';
	ClientCmd( command );
}

void Touch_CloseMenu()
{
	gMobileAPI.pfnTouchRemoveButton( "_menu_*" );
	gMobileAPI.pfnTouchSetClientOnly( 0 );
}

int CHudMenu :: Init( void )
{
	gHUD.AddHudElem( this );

	HOOK_MESSAGE( gHUD.m_Menu, ShowMenu );
	HOOK_MESSAGE( gHUD.m_Menu, VGUIMenu );
	HOOK_MESSAGE( gHUD.m_Menu, BuyClose );
	HOOK_MESSAGE( gHUD.m_Menu, AllowSpec );
	HOOK_COMMAND( gHUD.m_Menu, "client_buy_open", OldStyleMenuOpen );
	HOOK_COMMAND( gHUD.m_Menu, "client_buy_close", OldStyleMenuClose );
	HOOK_COMMAND( gHUD.m_Menu, "showvguimenu", ShowVGUIMenu );
	HOOK_COMMAND( gHUD.m_Menu, "+commandmenu", CommandMenuToggle );
	HOOK_COMMAND( gHUD.m_Menu, "-commandmenu", CommandMenuRelease );
	HOOK_COMMAND( gHUD.m_Menu, "commandmenu", CommandMenuToggle );
	HOOK_COMMAND( gHUD.m_Menu, "commandmenu_reload", CommandMenuReload );

	_extended_menus = CVAR_CREATE("_extended_menus", "1", FCVAR_ARCHIVE);

	InitHUDData();

	m_bAllowSpec = true; // by default, spectating is allowed

	return 1;
}

void CHudMenu :: InitHUDData( void )
{
	g_CommandMenuActive = false;
	m_fMenuDisplayed = 0;
	m_bitsValidSlots = 0;
	Reset();
}

void CHudMenu :: Reset( void )
{
	g_CommandMenuActive = false;
	g_szPrelocalisedMenuString[0] = 0;
	m_fWaitingForMore = FALSE;
}

int CHudMenu :: VidInit( void )
{
	return 1;
}

int CHudMenu :: Draw( float flTime )
{
	// check for if menu is set to disappear
	if ( m_flShutoffTime > 0 )
	{
		if ( m_flShutoffTime <= gHUD.m_flTime )
		{  // times up, shutoff
			UserCmd_OldStyleMenuClose();
			return 1;
		}
	}

	// don't draw the menu if the scoreboard is being shown
	//if ( gViewPort && gViewPort->IsScoreBoardVisible() )
		//return 1;

	// draw the menu, along the left-hand side of the screen

	// count the number of newlines
	int nlc = 0;
	int i;
	for ( i = 0; i < MAX_MENU_STRING && g_szMenuString[i] != '\0'; i++ )
	{
		if ( g_szMenuString[i] == '\n' )
			nlc++;
	}

	// center it
	int y = (ScreenHeight/2) - ((nlc/2)*12) - 40; // make sure it is above the say text
	int x = 20;

	i = 0;
	while ( i < MAX_MENU_STRING && g_szMenuString[i] != '\0' )
	{
		DrawUtils::DrawHudString( x, y, 320, g_szMenuString + i, 255, 255, 255 );
		y += 24;

		while ( i < MAX_MENU_STRING && g_szMenuString[i] != '\0' && g_szMenuString[i] != '\n' )
			i++;
		if ( g_szMenuString[i] == '\n' )
			i++;
	}

	return 1;
}

// selects an item from the menu
void CHudMenu :: SelectMenuItem( int menu_item )
{
	if( g_CommandMenuActive )
	{
		if( menu_item == 10 )
		{
			int parent = g_CommandMenuNodes[g_CommandMenuCurrentNode].parentNode;
			if( parent >= 0 )
			{
				g_CommandMenuCurrentNode = parent;
				CommandMenu_BuildDisplay( this );
			}
			else
			{
				UserCmd_OldStyleMenuClose();
			}
			return;
		}

		int itemIndex = CommandMenu_FindItemBySlot( g_CommandMenuCurrentNode, menu_item );
		if( itemIndex < 0 )
			return;

		command_menu_item_t &item = g_CommandMenuItems[itemIndex];
		if( item.childNode >= 0 )
		{
			g_CommandMenuCurrentNode = item.childNode;
			CommandMenu_BuildDisplay( this );
			return;
		}

		CommandMenu_Execute( item );
		UserCmd_OldStyleMenuClose();
		return;
	}

	// if menu_item is in a valid slot, send a menuselect command to the server
	if ( (menu_item > 0) && (m_bitsValidSlots & (1 << (menu_item-1))) )
	{
		char szbuf[32];
		sprintf( szbuf, "menuselect %d\n", menu_item );
		ClientCmd( szbuf );

		UserCmd_OldStyleMenuClose();
	}
}


// Message handler for ShowMenu message
// takes four values:
//		short: a bitfield of keys that are valid input
//		char : the duration, in seconds, the menu should stay up. -1 means is stays until something is chosen.
//		byte : a boolean, TRUE if there is more string yet to be received before displaying the menu, FALSE if it's the last string
//		string: menu string to display
// if this message is never received, then scores will simply be the combined totals of the players.
int CHudMenu :: MsgFunc_ShowMenu( const char *pszName, int iSize, void *pbuf )
{
	g_CommandMenuActive = false;
	char *temp = NULL, *menustring;

	BufferReader reader( pszName, pbuf, iSize );

	m_bitsValidSlots = reader.ReadShort();
	int DisplayTime = reader.ReadChar();
	int NeedMore = reader.ReadByte();

	if ( DisplayTime > 0 )
		m_flShutoffTime = DisplayTime + gHUD.m_flTime;
	else
		m_flShutoffTime = -1;

	if ( !m_bitsValidSlots )
	{
		UserCmd_OldStyleMenuClose(); // no valid slots means that the menu should be turned off
		return 1;
	}

	menustring = reader.ReadString();

	// menu will be replaced by scripted touch config
	// so execute it and exit
	if( _extended_menus->value != 0.0f )
	{
		if( !strncmp(menustring, "#Radio", 6 ) )
		{
			if( menustring[6] == 'A' )
			{
				ShowVGUIMenu(MENU_RADIOA); return 1;
			}
			else if( menustring[6] == 'B' )
			{
				ShowVGUIMenu(MENU_RADIOB); return 1;
			}
			else if( menustring[6] == 'C' )
			{
				ShowVGUIMenu(MENU_RADIOC); return 1;
			}
			else ShowVGUIMenu( MENU_NUMERICAL_MENU ); // we just show touch screen numbers
		}
		else ShowVGUIMenu(MENU_NUMERICAL_MENU);
	}
	else ShowVGUIMenu(MENU_NUMERICAL_MENU);

	if ( !m_fWaitingForMore ) // this is the start of a new menu
	{
		strlcpy( g_szPrelocalisedMenuString, menustring, sizeof( g_szPrelocalisedMenuString ) );
	}
	else
	{  // append to the current menu string
		strlcat( g_szPrelocalisedMenuString, menustring, sizeof( g_szPrelocalisedMenuString ) );
	}

	if ( !NeedMore )
	{  // we have the whole string, so we can localise it now
		strlcpy( g_szMenuString, gHUD.m_TextMessage.BufferedLocaliseTextString( g_szPrelocalisedMenuString ), sizeof( g_szMenuString ) );
		// Swap in characters
		if ( KB_ConvertString( g_szMenuString, &temp ) )
		{
			strlcpy( g_szMenuString, temp, sizeof( g_szMenuString ) );
			free( temp );
		}
	}

	m_fMenuDisplayed = 1;
	m_iFlags |= HUD_DRAW;

	m_fWaitingForMore = NeedMore;

	return 1;
}

int CHudMenu::MsgFunc_VGUIMenu( const char *pszName, int iSize, void *pbuf )
{
	g_CommandMenuActive = false;
	BufferReader reader( pszName, pbuf, iSize );

	int menuType = reader.ReadByte();
	m_bitsValidSlots = reader.ReadShort(); // is ignored

	ShowVGUIMenu(menuType);
	return 1;
}

int CHudMenu::MsgFunc_BuyClose(const char *pszName, int iSize, void *pbuf)
{
	Touch_CloseMenu();

	if (g_pMenu)
		g_pMenu->HideVGUIMenu();

	return 1;
}

int CHudMenu::MsgFunc_AllowSpec(const char *pszName, int iSize, void *pbuf)
{
	BufferReader reader( pszName, pbuf, iSize );

	m_bAllowSpec = (bool)reader.ReadByte();

	return 1;
}

void CHudMenu::UserCmd_OldStyleMenuOpen()
{
	m_flShutoffTime = -1; // stay open until user will not close it
	strlcpy( g_szMenuString, gHUD.m_TextMessage.BufferedLocaliseTextString("Buy"), sizeof( g_szMenuString ) );
}

void CHudMenu::UserCmd_OldStyleMenuClose()
{
	g_CommandMenuActive = false;
	m_fMenuDisplayed = 0; // no valid slots means that the menu should be turned off
	m_iFlags &= ~HUD_DRAW;

	Touch_CloseMenu();
}

void CHudMenu::UserCmd_CommandMenuToggle()
{
	if( g_CommandMenuActive )
	{
		UserCmd_OldStyleMenuClose();
		return;
	}

	if( !CommandMenu_ParseFile() )
		return;

	g_CommandMenuCurrentNode = 0;
	g_CommandMenuActive = true;
	CommandMenu_BuildDisplay( this );
}

void CHudMenu::UserCmd_CommandMenuRelease()
{
	// Intentionally left open on key release. This makes a normal tap of a
	// +commandmenu bind useful on physical keyboards and Android.
}

void CHudMenu::UserCmd_CommandMenuReload()
{
	bool reopen = g_CommandMenuActive;
	CommandMenu_ResetData();
	if( !CommandMenu_ParseFile() )
	{
		UserCmd_OldStyleMenuClose();
		return;
	}

	gEngfuncs.Con_Printf( "commandmenu.txt reloaded\\n" );
	if( reopen )
	{
		g_CommandMenuCurrentNode = 0;
		g_CommandMenuActive = true;
		CommandMenu_BuildDisplay( this );
	}
}

// lol, no real VGUI here
// it's really good only for touchscreen

void CHudMenu::ShowVGUIMenu( int menuType )
{
	int team = g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber;
	int haveCancel = team != TEAM_UNASSIGNED ? 1 : 0;

	if( g_pMenu && !UseOldTouchMenusEnabled() )
	{
		switch( menuType )
		{
		case MENU_TEAM:
		{
			int param = 0;
			if( m_bAllowSpec )
			{
				if( g_iTeamNumber == TEAM_UNASSIGNED ||
					g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].dead ||
					!g_iFreezeTimeOver )
				{
					param |= 1 << 0;
				}
			}

			if( IsASMapType() && g_iTeamNumber == TEAM_CT )
				param |= 1 << 1;

			if( haveCancel )
				param |= 1 << 2;

			g_pMenu->ShowVGUIMenu( menuType, param, 0 );
			return;
		}
		case MENU_CLASS_T:
		case MENU_CLASS_CT:
			g_pMenu->ShowVGUIMenu( menuType, gHUD.GetGameType() == GAME_CZERO, haveCancel );
			return;
		case MENU_BUY:
		case MENU_BUY_PISTOL:
		case MENU_BUY_SHOTGUN:
		case MENU_BUY_RIFLE:
		case MENU_BUY_SUBMACHINEGUN:
		case MENU_BUY_MACHINEGUN:
		case MENU_BUY_ITEM:
			g_pMenu->ShowVGUIMenu( menuType, gHUD.GetGameType() == GAME_CZERO, team );
			return;
		}
	}

	const char *szCmd;

	switch(menuType)
	{
	case MENU_TEAM:
		szCmd = "exec touch/chooseteam.cfg";
		break;
	case MENU_CLASS_T:
		szCmd = "exec touch/chooseteam_tr.cfg";
		break;
	case MENU_CLASS_CT:
		szCmd = "exec touch/chooseteam_ct.cfg";
		break;
	case MENU_BUY:
		szCmd = "exec touch/buy.cfg";
		break;
	case MENU_BUY_PISTOL:
		if( g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber == TEAM_TERRORIST )
			szCmd = "exec touch/buy_pistol_t.cfg";
		else szCmd = "exec touch/buy_pistol_ct.cfg";
		break;
	case MENU_BUY_SHOTGUN:
		if( g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber == TEAM_TERRORIST )
			szCmd = "exec touch/buy_shotgun_t.cfg";
		else szCmd = "exec touch/buy_shotgun_ct.cfg";
		break;
	case MENU_BUY_RIFLE:
		if( g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber == TEAM_TERRORIST )
			szCmd = "exec touch/buy_rifle_t.cfg";
		else szCmd ="exec touch/buy_rifle_ct.cfg";
		break;
	case MENU_BUY_SUBMACHINEGUN:
		if( g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber == TEAM_TERRORIST )
			szCmd = "exec touch/buy_submachinegun_t.cfg";
		else szCmd = "exec touch/buy_submachinegun_ct.cfg";
		break;
	case MENU_BUY_MACHINEGUN:
		if( g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber == TEAM_TERRORIST )
			szCmd = "exec touch/buy_machinegun_t.cfg";
		else szCmd = "exec touch/buy_machinegun_ct.cfg";
		break;
	case MENU_BUY_ITEM:
		if( g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber == TEAM_TERRORIST )
			szCmd = "exec touch/buy_item_t.cfg";
		else szCmd = "exec touch/buy_item_ct.cfg";
		break;
	case MENU_RADIOA:
		szCmd = "exec touch/radioa.cfg";
		break;
	case MENU_RADIOB:
		szCmd = "exec touch/radiob.cfg";
		break;
	case MENU_RADIOC:
		szCmd = "exec touch/radioc.cfg";
		break;
	case MENU_RADIOSELECTOR:
		szCmd = "exec touch/radioselector.cfg";
		break;
	case MENU_BUY_CSDM:
		szCmd = "exec touch/custom/dm_menu.cfg";
		break;
	case MENU_NUMERICAL_MENU:
#ifdef __ANDROID__
		szCmd = "exec touch/numerical_menu.cfg";
		break;
#else
		return;
#endif
	default:
		UserCmd_OldStyleMenuClose();
		return;
	}

	m_fMenuDisplayed = 1;
	ClientCmd(szCmd);
}

void CHudMenu::UserCmd_ShowVGUIMenu()
{
	if( gEngfuncs.Cmd_Argc() < 2 )
	{
		ConsolePrint("usage: showvguimenu <menuType>\n");
		return;
	}

	int menuType = atoi(gEngfuncs.Cmd_Argv(1));
	ShowVGUIMenu(menuType);
}
