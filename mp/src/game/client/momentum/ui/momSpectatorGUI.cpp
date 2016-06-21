//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include <KeyValues.h>
#include <cdll_client_int.h>
#include <cdll_util.h>
#include <globalvars_base.h>

#include "momspectatorgui.h"

#include <vgui/ILocalize.h>
#include <vgui/IPanel.h>
#include <vgui/IScheme.h>
#include <vgui/ISurface.h>
#include <vgui_controls/ImageList.h>
#include <vgui_controls/MenuItem.h>
#include <vgui_controls/TextImage.h>

#include <stdio.h> // _snprintf define

#include "commandmenu.h"
#include "hltvcamera.h"
#include <game/client/iviewport.h>

#include "IGameUIFuncs.h" // for key bindings
#include <igameresources.h>
#include <imapoverview.h>
#include <shareddefs.h>
#include <vgui/IInput.h>
#include <vgui_controls/ImagePanel.h>
#include <vgui_controls/Menu.h>
#include <vgui_controls/Panel.h>
#include <vgui_controls/TextEntry.h>

#include "mom_player_shared.h"
#include "util/mom_util.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

CMOMSpectatorGUI *g_pMOMSpectatorGUI = nullptr;

// NB disconnect between localization text and observer mode enums
static const char *s_SpectatorModes[] = {
    "#Spec_Mode0",    // 	OBS_MODE_NONE = 0,
    "#Spec_Mode1",    // 	OBS_MODE_DEATHCAM,
    "",               // 	OBS_MODE_FREEZECAM,
    "#Spec_Mode2",    // 	OBS_MODE_FIXED,
    "#Spec_Mode3",    // 	OBS_MODE_IN_EYE,
    "#Spec_Mode4",    // 	OBS_MODE_CHASE,
    "#Spec_Mode_POI", // 	OBS_MODE_POI, PASSTIME
    "#Spec_Mode5",    // 	OBS_MODE_ROAMING,
};

using namespace vgui;

//-----------------------------------------------------------------------------
// Purpose: left and right buttons pointing buttons
//-----------------------------------------------------------------------------
class CSpecButton : public Button
{
  public:
    CSpecButton(Panel *parent, const char *panelName) : Button(parent, panelName, "") {}

  private:
    void ApplySchemeSettings(vgui::IScheme *pScheme)
    {
        Button::ApplySchemeSettings(pScheme);
        SetFont(pScheme->GetFont("Marlett", IsProportional()));
    }
};

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CMOMSpectatorMenu::CMOMSpectatorMenu(IViewPort *pViewPort) : Frame(nullptr, PANEL_SPECMENU)
{
    m_iDuckKey = BUTTON_CODE_INVALID;

    m_pViewPort = pViewPort;

    SetMouseInputEnabled(true);
    SetKeyBoardInputEnabled(true);
    SetTitleBarVisible(false); // don't draw a title bar
    SetMoveable(false);
    SetSizeable(false);
    SetProportional(true);
    SetScheme("ClientScheme");
    ListenForGameEvent("spec_target_updated");

    m_pPlayerList = new ComboBox(this, "playercombo", 10, false);
    HFont hFallbackFont = scheme()->GetIScheme(GetScheme())->GetFont("DefaultVerySmallFallBack", false);
    if (INVALID_FONT != hFallbackFont)
    {
        m_pPlayerList->SetUseFallbackFont(true, hFallbackFont);
    }

    m_pViewOptions = new ComboBox(this, "viewcombo", 10, false);
    m_pConfigSettings = new ComboBox(this, "settingscombo", 10, false);

    m_pLeftButton = new CSpecButton(this, "specprev");
    m_pLeftButton->SetText("3");
    m_pRightButton = new CSpecButton(this, "specnext");
    m_pRightButton->SetText("4");

    m_pPlayerList->SetText("");
    m_pViewOptions->SetText("#Spec_Modes");
    m_pConfigSettings->SetText("#Spec_Options");

    m_pPlayerList->SetOpenDirection(Menu::UP);
    m_pViewOptions->SetOpenDirection(Menu::UP);
    m_pConfigSettings->SetOpenDirection(Menu::UP);

    // create view config menu
    CommandMenu *menu = new CommandMenu(m_pConfigSettings, "spectatormenu", gViewPortInterface);
    menu->LoadFromFile("Resource/spectatormenu.res");
    m_pConfigSettings->SetMenu(menu); // attach menu to combo box

    // create view mode menu
    menu = new CommandMenu(m_pViewOptions, "spectatormodes", gViewPortInterface);
    menu->LoadFromFile("Resource/spectatormodes.res");
    m_pViewOptions->SetMenu(menu); // attach menu to combo box

    LoadControlSettings("Resource/UI/BottomSpectator.res");
    ListenForGameEvent("spec_target_updated");
}

void CMOMSpectatorMenu::ApplySchemeSettings(IScheme *pScheme)
{
    BaseClass::ApplySchemeSettings(pScheme);
    // need to MakeReadyForUse() on the menus so we can set their bg color before they are displayed
    m_pConfigSettings->GetMenu()->MakeReadyForUse();
    m_pViewOptions->GetMenu()->MakeReadyForUse();
    m_pPlayerList->GetMenu()->MakeReadyForUse();

    if (g_pMOMSpectatorGUI)
    {
        m_pConfigSettings->GetMenu()->SetBgColor(g_pMOMSpectatorGUI->GetBlackBarColor());
        m_pViewOptions->GetMenu()->SetBgColor(g_pMOMSpectatorGUI->GetBlackBarColor());
        m_pPlayerList->GetMenu()->SetBgColor(g_pMOMSpectatorGUI->GetBlackBarColor());
    }
}

//-----------------------------------------------------------------------------
// Purpose: makes the GUI fill the screen
//-----------------------------------------------------------------------------
void CMOMSpectatorMenu::PerformLayout()
{
    int w, h;
    GetHudSize(w, h);

    // fill the screen
    SetSize(w, GetTall());
}

//-----------------------------------------------------------------------------
// Purpose: Handles changes to combo boxes
//-----------------------------------------------------------------------------
void CMOMSpectatorMenu::OnTextChanged(KeyValues *data)
{
    Panel *panel = reinterpret_cast<Panel *>(data->GetPtr("panel"));

    ComboBox *box = dynamic_cast<ComboBox *>(panel);

    if (box == m_pConfigSettings) // don't change the text in the config setting combo
    {
        m_pConfigSettings->SetText("#Spec_Options");
    }
    else if (box == m_pPlayerList)
    {
        KeyValues *kv = box->GetActiveItemUserData();
        if (kv && GameResources())
        {
            const char *player = kv->GetString("player");

            int currentPlayerNum = GetSpectatorTarget();
            const char *currentPlayerName = GameResources()->GetPlayerName(currentPlayerNum);

            if (!FStrEq(currentPlayerName, player))
            {
                char command[128];
                Q_snprintf(command, sizeof(command), "spec_player \"%s\"", player);
                engine->ClientCmd(command);
            }
        }
    }
}

void CMOMSpectatorMenu::OnCommand(const char *command)
{
    if (!stricmp(command, "specnext"))
    {
        engine->ClientCmd("spec_next");
    }
    else if (!stricmp(command, "specprev"))
    {
        engine->ClientCmd("spec_prev");
    }
}

void CMOMSpectatorMenu::FireGameEvent(IGameEvent *event)
{
    const char *pEventName = event->GetName();

    if (Q_strcmp("spec_target_updated", pEventName) == 0)
    {
        IGameResources *gr = GameResources();
        if (!gr)
            return;

        // make sure the player combo box is up to date
        int playernum = GetSpectatorTarget();
        if (playernum < 1 || playernum > MAX_PLAYERS)
            return;

        const char *selectedPlayerName = gr->GetPlayerName(playernum);
        const char *currentPlayerName = "";
        KeyValues *kv = m_pPlayerList->GetActiveItemUserData();
        if (kv)
        {
            currentPlayerName = kv->GetString("player");
        }
        if (!FStrEq(currentPlayerName, selectedPlayerName))
        {
            for (int i = 0; i < m_pPlayerList->GetItemCount(); ++i)
            {
                KeyValues *kv = m_pPlayerList->GetItemUserData(i);
                if (kv && FStrEq(kv->GetString("player"), selectedPlayerName))
                {
                    m_pPlayerList->ActivateItemByRow(i);
                    break;
                }
            }
        }
    }
}

//-----------------------------------------------------------------------------
// Purpose: when duck is pressed it hides the active part of the GUI
//-----------------------------------------------------------------------------
void CMOMSpectatorMenu::OnKeyCodePressed(KeyCode code)
{
    if (code == m_iDuckKey)
    {
        // hide if DUCK is pressed again
        m_pViewPort->ShowPanel(this, false);
    }
}

void CMOMSpectatorMenu::ShowPanel(bool bShow)
{

    if (BaseClass::IsVisible() == bShow)
        return;

    if (bShow)
    {
        Activate();
        SetMouseInputEnabled(true);
        SetKeyBoardInputEnabled(true);
    }
    else
    {
        SetVisible(false);
        SetMouseInputEnabled(false);
        SetKeyBoardInputEnabled(false);
    }

    bool bIsEnabled = true;

    if (engine->IsHLTV() && HLTVCamera()->IsPVSLocked())
    {
        // when watching HLTV or Replay with a locked PVS, some elements are disabled
        bIsEnabled = false;
    }

    m_pLeftButton->SetVisible(bIsEnabled);
    m_pRightButton->SetVisible(bIsEnabled);
    m_pPlayerList->SetVisible(bIsEnabled);
    m_pViewOptions->SetVisible(bIsEnabled);
}

void CMOMSpectatorMenu::Update(void)
{
    IGameResources *gr = GameResources();

    Reset();

    if (m_iDuckKey == BUTTON_CODE_INVALID)
    {
        m_iDuckKey = gameuifuncs->GetButtonCodeForBind("duck");
    }

    if (!gr)
        return;

    int iPlayerIndex;
    for (iPlayerIndex = 1; iPlayerIndex <= gpGlobals->maxClients; iPlayerIndex++)
    {

        // does this slot in the array have a name?
        if (!gr->IsConnected(iPlayerIndex))
            continue;

        if (gr->IsLocalPlayer(iPlayerIndex))
            continue;

        if (!gr->IsAlive(iPlayerIndex))
            continue;

        wchar_t playerText[80], playerName[64], *team, teamText[64];
        char localizeTeamName[64];
        char szPlayerIndex[16];
        g_pVGuiLocalize->ConvertANSIToUnicode(UTIL_SafeName(gr->GetPlayerName(iPlayerIndex)), playerName,
                                              sizeof(playerName));
        const char *teamname = gr->GetTeamName(gr->GetTeam(iPlayerIndex));
        if (teamname)
        {
            Q_snprintf(localizeTeamName, sizeof(localizeTeamName), "#%s", teamname);
            team = g_pVGuiLocalize->Find(localizeTeamName);

            if (!team)
            {
                g_pVGuiLocalize->ConvertANSIToUnicode(teamname, teamText, sizeof(teamText));
                team = teamText;
            }

            g_pVGuiLocalize->ConstructString(playerText, sizeof(playerText),
                                             g_pVGuiLocalize->Find("#Spec_PlayerItem_Team"), 2, playerName, team);
        }
        else
        {
            g_pVGuiLocalize->ConstructString(playerText, sizeof(playerText), g_pVGuiLocalize->Find("#Spec_PlayerItem"),
                                             1, playerName);
        }

        Q_snprintf(szPlayerIndex, sizeof(szPlayerIndex), "%d", iPlayerIndex);

        KeyValues *kv = new KeyValues("UserData", "player", gr->GetPlayerName(iPlayerIndex), "index", szPlayerIndex);
        m_pPlayerList->AddItem(playerText, kv);
        kv->deleteThis();
    }

    // make sure the player combo box is up to date
    int playernum = GetSpectatorTarget();
    const char *selectedPlayerName = gr->GetPlayerName(playernum);
    for (iPlayerIndex = 0; iPlayerIndex < m_pPlayerList->GetItemCount(); ++iPlayerIndex)
    {
        KeyValues *kv = m_pPlayerList->GetItemUserData(iPlayerIndex);
        if (kv && FStrEq(kv->GetString("player"), selectedPlayerName))
        {
            m_pPlayerList->ActivateItemByRow(iPlayerIndex);
            break;
        }
    }

    //=============================================================================
    // HPE_BEGIN:
    // [pfreese] make sure the view mode combo box is up to date - the spectator
    // mode can be changed multiple ways
    //=============================================================================

    int specmode = GetSpectatorMode();
    m_pViewOptions->SetText(s_SpectatorModes[specmode]);

    //=============================================================================
    // HPE_END
    //=============================================================================
}

//-----------------------------------------------------------------------------
// main spectator panel

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CMOMSpectatorGUI::CMOMSpectatorGUI(IViewPort *pViewPort) : EditablePanel(nullptr, PANEL_SPECGUI)
{
    // 	m_bHelpShown = false;
    //	m_bInsetVisible = false;
    //	m_iDuckKey = KEY_NONE;
    SetSize(10, 10); // Quiet "parent not sized yet" spew
    m_bSpecScoreboard = false;

    m_pViewPort = pViewPort;
    g_pMOMSpectatorGUI = this;

    ListenForGameEvent("spec_target_updated");

    surface()->CreatePopup(GetVPanel(), false, false, false, false, false);

    m_flNextUpdateTime = -1.0f;

    // initialize dialog
    SetVisible(false);
    SetProportional(true);

    // load the new scheme early!!
    SetScheme("ClientScheme");
    SetMouseInputEnabled(false);
    SetKeyBoardInputEnabled(false);

    LoadControlSettings(GetResFile());

    m_pTopBar = FindControl<Panel>("topbar");
    m_pBottomBarBlank = FindControl<Panel>("bottombarblank");

    m_pPlayerLabel = FindControl<Label>("playerlabel");
    m_pPlayerLabel->SetVisible(false);

    m_pReplayLabel = FindControl<Label>("replaylabel");
    m_pTimeLabel = FindControl<Label>("timelabel");

    m_pCloseButton = FindControl<ImagePanel>("Close_Panel");
    m_pCloseButton->SetMouseInputEnabled(true);
    m_pCloseButton->InstallMouseHandler(this);

    TextImage *image = m_pPlayerLabel->GetTextImage();
    if (image)
    {
        HFont hFallbackFont = scheme()->GetIScheme(GetScheme())->GetFont("DefaultVerySmallFallBack", false);
        if (INVALID_FONT != hFallbackFont)
        {
            image->SetUseFallbackFont(true, hFallbackFont);
        }
    }

    SetPaintBorderEnabled(false);
    SetPaintBackgroundEnabled(false);

    InvalidateLayout();
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
CMOMSpectatorGUI::~CMOMSpectatorGUI() { g_pMOMSpectatorGUI = nullptr; }

//-----------------------------------------------------------------------------
// Purpose: Sets the colour of the top and bottom bars
//-----------------------------------------------------------------------------
void CMOMSpectatorGUI::ApplySchemeSettings(IScheme *pScheme)
{
    m_pBottomBarBlank->SetVisible(false);
    m_pTopBar->SetVisible(true);

    BaseClass::ApplySchemeSettings(pScheme);
    SetBgColor(Color(0, 0, 0, 0)); // make the background transparent
    m_pTopBar->SetBgColor(GetBlackBarColor());
    m_pBottomBarBlank->SetBgColor(GetBlackBarColor());
    SetPaintBorderEnabled(false);

    SetBorder(nullptr);
}

void CMOMSpectatorGUI::OnMousePressed(MouseCode code)
{
    if (code == MOUSE_LEFT)
    {
        VPANEL over = input()->GetMouseOver();
        if (over == m_pCloseButton->GetVPanel())
        {
            SetMouseInputEnabled(false);
            // We're piggybacking on this event because it's basically the same as the X on the mapfinished panel
            IGameEvent *pClosePanel = gameeventmanager->CreateEvent("mapfinished_panel_closed");
            if (pClosePanel)
            {
                // Fire this event so other classes can get at this
                gameeventmanager->FireEvent(pClosePanel);
            }
        }
    }
}

//-----------------------------------------------------------------------------
// Purpose: makes the GUI fill the screen
//-----------------------------------------------------------------------------
void CMOMSpectatorGUI::PerformLayout()
{
    int w, h, x, y;
    GetHudSize(w, h);

    // fill the screen
    SetBounds(0, 0, w, h);

    // stretch the bottom bar across the screen
    m_pBottomBarBlank->GetPos(x, y);
    m_pBottomBarBlank->SetSize(w, h - y);
}

//-----------------------------------------------------------------------------
// Purpose: checks spec_scoreboard cvar to see if the scoreboard should be displayed
//-----------------------------------------------------------------------------
void CMOMSpectatorGUI::OnThink()
{
    if (m_flNextUpdateTime > 0.0f && gpGlobals->curtime > m_flNextUpdateTime)
    {
        Update();
        m_flNextUpdateTime = -1.0f;
    }

    BaseClass::OnThink();
}

//-----------------------------------------------------------------------------
// Purpose: sets the image to display for the banner in the top right corner
//-----------------------------------------------------------------------------
void CMOMSpectatorGUI::SetLogoImage(const char *image)
{
    if (m_pBannerImage)
    {
        m_pBannerImage->SetImage(scheme()->GetImage(image, false));
    }
}

//-----------------------------------------------------------------------------
// Purpose: Sets the text of a control by name
//-----------------------------------------------------------------------------
void CMOMSpectatorGUI::SetLabelText(const char *textEntryName, const char *text)
{
    Label *entry = dynamic_cast<Label *>(FindChildByName(textEntryName));
    if (entry)
    {
        entry->SetText(text);
    }
}

//-----------------------------------------------------------------------------
// Purpose: Sets the text of a control by name
//-----------------------------------------------------------------------------
void CMOMSpectatorGUI::SetLabelText(const char *textEntryName, wchar_t *text)
{
    Label *entry = dynamic_cast<Label *>(FindChildByName(textEntryName));
    if (entry)
    {
        entry->SetText(text);
    }
}

//-----------------------------------------------------------------------------
// Purpose: Sets the text of a control by name
//-----------------------------------------------------------------------------
void CMOMSpectatorGUI::MoveLabelToFront(const char *textEntryName)
{
    Label *entry = dynamic_cast<Label *>(FindChildByName(textEntryName));
    if (entry)
    {
        entry->MoveToFront();
    }
}

//-----------------------------------------------------------------------------
// Purpose: shows/hides the buy menu
//-----------------------------------------------------------------------------
void CMOMSpectatorGUI::ShowPanel(bool bShow)
{
    if (bShow && !IsVisible())
    {
        m_bSpecScoreboard = false;
    }
    SetVisible(bShow);
    if (!bShow && m_bSpecScoreboard)
    {
        gViewPortInterface->ShowPanel(PANEL_SCOREBOARD, false);
    }
}

bool CMOMSpectatorGUI::ShouldShowPlayerLabel(int specmode)
{
    return ((specmode == OBS_MODE_IN_EYE) || (specmode == OBS_MODE_CHASE));
}
//-----------------------------------------------------------------------------
// Purpose: Updates the gui, rearranges elements
//-----------------------------------------------------------------------------
void CMOMSpectatorGUI::Update()
{
    int wide, tall;
    int bx, by, bwide, btall;

    GetHudSize(wide, tall);
    m_pTopBar->GetBounds(bx, by, bwide, btall);

    IGameResources *gr = GameResources();
    int specmode = GetSpectatorMode();
    int playernum = GetSpectatorTarget();

    IViewPortPanel *overview = gViewPortInterface->FindPanelByName(PANEL_OVERVIEW);

    if (overview && overview->IsVisible())
    {
        int mx, my, mwide, mtall;

        VPANEL p = overview->GetVPanel();
        vgui::ipanel()->GetPos(p, mx, my);
        vgui::ipanel()->GetSize(p, mwide, mtall);

        if (my < btall)
        {
            // reduce to bar
            m_pTopBar->SetSize(wide - (mx + mwide), btall);
            m_pTopBar->SetPos((mx + mwide), 0);
        }
        else
        {
            // full top bar
            m_pTopBar->SetSize(wide, btall);
            m_pTopBar->SetPos(0, 0);
        }
    }
    else
    {
        // full top bar
        m_pTopBar->SetSize(wide, btall); // change width, keep height
        m_pTopBar->SetPos(0, 0);
    }

    m_pPlayerLabel->SetVisible(ShouldShowPlayerLabel(specmode));

    // update player name filed, text & color

    if (playernum > 0 && playernum <= gpGlobals->maxClients && gr)
    {
        Color c = gr->GetTeamColor(gr->GetTeam(playernum)); // Player's team color

        m_pPlayerLabel->SetFgColor(c);

        wchar_t playerText[80], playerName[64];
        V_wcsncpy(playerText, L"Unable to find #Spec_PlayerItem*", sizeof(playerText));
        memset(playerName, 0x0, sizeof(playerName));

        g_pVGuiLocalize->ConvertANSIToUnicode(UTIL_SafeName(gr->GetPlayerName(playernum)), playerName,
                                              sizeof(playerName));
        g_pVGuiLocalize->ConstructString(playerText, sizeof(playerText), g_pVGuiLocalize->Find("#Spec_PlayerItem"), 1,
                                         playerName);

        m_pPlayerLabel->SetText(playerText);
    }
    else
    {
        m_pPlayerLabel->SetText("");
    }
    CMomentumPlayer *pPlayer = ToCMOMPlayer(CBasePlayer::GetLocalPlayer());
    if (pPlayer)
    {
        C_MomentumReplayGhostEntity *pReplayEnt = pPlayer->GetReplayEnt();
        if (pReplayEnt)
        {
            wchar_t wPlayerName[MAX_PLAYER_NAME_LENGTH], szPlayerInfo[128];
            g_pVGuiLocalize->ConvertANSIToUnicode(pReplayEnt->m_pszPlayerName, wPlayerName, sizeof(wPlayerName));
            swprintf(szPlayerInfo, L"%s %s", g_pVGuiLocalize->Find("#MOM_ReplayPlayer"), wPlayerName);

            SetLabelText("playerlabel", szPlayerInfo);

            char tempRunTime[BUFSIZETIME];
            wchar_t szTimeLabel[BUFSIZELOCL], wTime[BUFSIZETIME];
            mom_UTIL->FormatTime(pReplayEnt->m_RunData.m_flRunTime, tempRunTime);
            g_pVGuiLocalize->ConvertANSIToUnicode(tempRunTime, wTime, sizeof(wTime));
            g_pVGuiLocalize->ConstructString(szTimeLabel, sizeof(szTimeLabel), g_pVGuiLocalize->Find("#MOM_MF_RunTime"),
                                             1, wTime);

            SetLabelText("timelabel", szTimeLabel);

            SetLabelText("replaylabel", g_pVGuiLocalize->Find("#MOM_WatchingReplay"));
        }
        else
        {
            m_pReplayLabel->SetText("");
        }
    }

    // update extra info field
    wchar_t szEtxraInfo[1024];
    wchar_t szTitleLabel[1024];
    char tempstr[128];

    if (engine->IsHLTV())
    {
        // set spectator number and HLTV title
        Q_snprintf(tempstr, sizeof(tempstr), "Spectators : %d", HLTVCamera()->GetNumSpectators());
        g_pVGuiLocalize->ConvertANSIToUnicode(tempstr, szEtxraInfo, sizeof(szEtxraInfo));

        Q_strncpy(tempstr, HLTVCamera()->GetTitleText(), sizeof(tempstr));
        g_pVGuiLocalize->ConvertANSIToUnicode(tempstr, szTitleLabel, sizeof(szTitleLabel));
    }
    else
    {
        // otherwise show map name
        Q_FileBase(engine->GetLevelName(), tempstr, sizeof(tempstr));

        wchar_t wMapName[64];
        g_pVGuiLocalize->ConvertANSIToUnicode(tempstr, wMapName, sizeof(wMapName));
        g_pVGuiLocalize->ConstructString(szEtxraInfo, sizeof(szEtxraInfo), g_pVGuiLocalize->Find("#Spec_Map"), 1,
                                         wMapName);

        g_pVGuiLocalize->ConvertANSIToUnicode("", szTitleLabel, sizeof(szTitleLabel));
    }

    SetLabelText("extrainfo", szEtxraInfo);
    SetLabelText("titlelabel", szTitleLabel);
}

//-----------------------------------------------------------------------------
// Purpose: Updates the timer label if one exists
//-----------------------------------------------------------------------------
void CMOMSpectatorGUI::UpdateTimer()
{
    wchar_t szText[63];

    int timer = 0;

    V_swprintf_safe(szText, L"%d:%02d\n", (timer / 60), (timer % 60));

    SetLabelText("timerlabel", szText);
}

static void ForwardSpecCmdToServer(const CCommand &args)
{
    if (engine->IsPlayingDemo())
        return;

    if (args.ArgC() == 1)
    {
        // just forward the command without parameters
        engine->ServerCmd(args[0]);
    }
    else if (args.ArgC() == 2)
    {
        // forward the command with parameter
        char command[128];
        Q_snprintf(command, sizeof(command), "%s \"%s\"", args[0], args[1]);
        engine->ServerCmd(command);
    }
}
