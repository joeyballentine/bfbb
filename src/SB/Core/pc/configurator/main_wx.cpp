// bfbb_config -- the settings front end, drawn in wxWidgets. What it edits and
// why it is a separate program is in README.md beside this file.
//
// The same window main_win32.cpp draws, on a toolkit that has one on every
// host. wx wraps the platform's own controls rather than imitating them: this
// is Win32 common controls on Windows, GTK on Linux and Cocoa on macOS, and
// the buttons here are the ones everything else on the machine uses.
//
// Everything it knows about the settings comes from config_model.h. Nothing in
// this file reads or writes config.ini.

#include "config_model.h"

#include "iConfigTable.h"
#include "iPadTokens.h"

#include <SDL3/SDL.h>

#include <wx/wx.h>

#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/collpane.h>
#include <wx/gbsizer.h>
#include <wx/listbox.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>

#include <string.h>

#include <vector>

namespace
{
    // Every length here is written for a 96-DPI screen and passed through
    // FromDIP, which is per-monitor on the hosts that have per-monitor scaling
    // and a no-op on the ones that do not.
    const int kMargin = 12;
    const int kLabelWidth = 150;
    const int kValueWidth = 200;
    const int kPathWidth = 320;

    // How far a group's settings sit in from the ones they hang off.
    const int kIndent = 16;

    // Ids for the window's own controls. The rows are built at run time, so a
    // row's controls are numbered from a base and the row is recovered by
    // dividing.
    enum
    {
        kIdSections = wxID_HIGHEST + 1,
        kIdResetSection,
        kIdStartGame,
        kIdRowBase
    };

    // A binding row uses the Browse slot for Set and one more for Add.
    const int kIdsPerRow = 3;
    const int kIdRowValue = 0;
    const int kIdRowBrowse = 1;
    const int kIdRowAdd = 2;

    // One setting's controls.
    //
    // Exactly one of `check`, `choice` and `entry` is set, which is how a value
    // is read back without asking the control what it is. wxTextEntry is the
    // interface wxTextCtrl and wxComboBox share, and it is why those two are
    // one case here rather than two.
    //
    // `descriptionText` is kept because wxStaticText::Wrap edits the LABEL
    // rather than laying it out. Re-wrapping at a new width means putting the
    // original back first, so the original has to be somewhere.
    struct Row
    {
        S32 setting;

        // A binding's button, or -1 for a setting's row. `device` is only
        // meaningful beside it.
        S32 bindRow;
        ConfigModelDevice device;

        wxCheckBox* check;
        wxChoice* choice;
        wxTextEntry* entry;

        wxStaticText* description;
        wxString descriptionText;

        // How far in from the pane's left edge this row sits. Zero for a row
        // in the section itself and one indent for a row inside a group's
        // collapsible pane, and it is here because the description has to wrap
        // to what is left of the width rather than all of it.
        int indent;

        Row()
            : setting(0), bindRow(-1), device(CONFIG_MODEL_KEYBOARD), check(NULL), choice(NULL),
              entry(NULL), description(NULL), indent(0)
        {
        }
    };

    // The pages after the settings' sections: one per device.
    ConfigModelDevice DeviceOfSection(int section)
    {
        return (ConfigModelDevice)(section - ConfigModelSectionCount());
    }

    bool IsBindSection(int section)
    {
        return section >= ConfigModelSectionCount();
    }

    // What a binding row says under its box: what the button does, and what
    // an empty box means.
    wxString DescribeBinding(ConfigModelDevice device, S32 row)
    {
        char def[256];
        ConfigModelBindDescribeDefault(device, row, def, sizeof(def));

        wxString text;
        const char* does = ConfigModelBindDoes(row);
        if (does != NULL)
        {
            text << does << ". ";
        }
        text << "Default: " << def << ".";
        return text;
    }

    // -----------------------------------------------------------------
    // Capturing one input
    //
    // Keys come from wx: the dialog has the keyboard focus, and CHAR_HOOK
    // sees every key before navigation takes Tab or the arrows. The controller
    // comes from SDL, polled on a timer, because wx has no gamepad input; SDL
    // is told to report pads while it has no window of its own focused, which
    // is always here.

    wxString SidedModifier(const wxKeyEvent& event, const char* left, const char* right,
                           const char* either, int leftScan, int rightScan, bool extendedIsRight)
    {
#ifdef __WXMSW__
        // The raw flags are the message's lParam: scan code in bits 16-23,
        // the extended-key bit at 24. Right Ctrl and right Alt are extended;
        // the two Shifts differ by scan code.
        const wxUint32 flags = event.GetRawKeyFlags();
        const int scan = (int)((flags >> 16) & 0xFF);
        const bool extended = (flags & (1u << 24)) != 0;
        if (extendedIsRight)
        {
            return extended ? right : left;
        }
        if (scan == leftScan)
        {
            return left;
        }
        if (scan == rightScan)
        {
            return right;
        }
#else
        (void)event;
        (void)left;
        (void)right;
        (void)leftScan;
        (void)rightScan;
        (void)extendedIsRight;
#endif
        return either;
    }

    // The [keyboard] name for a key, or "" for one the bindings cannot name.
    wxString KeyToken(const wxKeyEvent& event)
    {
        const int code = event.GetKeyCode();

        if (code >= 'A' && code <= 'Z')
        {
            return wxString((wxChar)(code - 'A' + 'a'));
        }
        if (code >= '0' && code <= '9')
        {
            return wxString((wxChar)code);
        }
        if (code >= WXK_F1 && code <= WXK_F12)
        {
            return wxString::Format("f%d", code - WXK_F1 + 1);
        }
        if (code >= WXK_NUMPAD0 && code <= WXK_NUMPAD9)
        {
            return wxString::Format("numpad%d", code - WXK_NUMPAD0);
        }

        switch (code)
        {
        case WXK_SPACE:
            return "space";
        case WXK_RETURN:
            return "enter";
        case WXK_TAB:
            return "tab";
        case WXK_BACK:
            return "backspace";
        case WXK_UP:
            return "up";
        case WXK_DOWN:
            return "down";
        case WXK_LEFT:
            return "left";
        case WXK_RIGHT:
            return "right";
        case WXK_INSERT:
            return "insert";
        case WXK_DELETE:
            return "delete";
        case WXK_HOME:
            return "home";
        case WXK_END:
            return "end";
        case WXK_PAGEUP:
            return "pageup";
        case WXK_PAGEDOWN:
            return "pagedown";
        case WXK_CAPITAL:
            return "capslock";
        case WXK_NUMPAD_ADD:
            return "numpadplus";
        case WXK_NUMPAD_SUBTRACT:
            return "numpadminus";
        case WXK_NUMPAD_MULTIPLY:
            return "numpadstar";
        case WXK_NUMPAD_DIVIDE:
            return "numpadslash";
        case WXK_NUMPAD_DECIMAL:
            return "numpaddot";
        case WXK_SHIFT:
            return SidedModifier(event, "lshift", "rshift", "shift", 0x2A, 0x36, false);
        case WXK_CONTROL:
            return SidedModifier(event, "lctrl", "rctrl", "ctrl", 0, 0, true);
        case WXK_ALT:
            return SidedModifier(event, "lalt", "ralt", "alt", 0, 0, true);
        default:
            break;
        }

        // The punctuation keys arrive as the character on them, unshifted.
        switch (event.GetUnicodeKey())
        {
        case ',':
            return "comma";
        case '.':
            return "period";
        case '-':
            return "minus";
        case '=':
            return "equals";
        case ';':
            return "semicolon";
        case '/':
            return "slash";
        case '`':
            return "tilde";
        case '[':
            return "lbracket";
        case '\\':
            return "backslash";
        case ']':
            return "rbracket";
        case '\'':
            return "quote";
        default:
            return "";
        }
    }

    // The controller inputs held right now on any pad, as PADIN_* bits.
    U32 PadHeld()
    {
        static bool sStarted = false;
        if (!sStarted)
        {
            sStarted = true;
            SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
            SDL_InitSubSystem(SDL_INIT_GAMEPAD);
        }

        SDL_UpdateGamepads();

        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        U32 held = 0;

        for (int i = 0; i < count; i++)
        {
            SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
            if (pad == NULL)
            {
                pad = SDL_OpenGamepad(ids[i]);
            }
            if (pad == NULL)
            {
                continue;
            }

            for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++)
            {
                const S32 input = iPadInputFromSDLButton(b);
                if (input >= 0 && SDL_GetGamepadButton(pad, (SDL_GamepadButton)b))
                {
                    held |= 1u << input;
                }
            }
            if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) >= IPAD_SDL_TRIGGER_THRESHOLD)
            {
                held |= 1u << PADIN_LT;
            }
            if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) >= IPAD_SDL_TRIGGER_THRESHOLD)
            {
                held |= 1u << PADIN_RT;
            }
        }

        SDL_free(ids);
        return held;
    }

    bool AnyPad()
    {
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        SDL_free(ids);
        return count > 0;
    }

    class CaptureDialog : public wxDialog
    {
    public:
        CaptureDialog(wxWindow* parent, ConfigModelDevice device, S32 row)
            : wxDialog(parent, wxID_ANY, "Set a binding"), mDevice(device), mTimer(this),
              mHeld(0), mStatus(NULL)
        {
            wxString what = ConfigModelBindName(row);
            const char* does = ConfigModelBindDoes(row);
            if (does != NULL)
            {
                what << " (" << does << ")";
            }

            wxString prompt = device == CONFIG_MODEL_KEYBOARD
                                  ? "Press a key for " + what + "."
                                  : "Press a controller button for " + what + ".";

            wxStaticText* text = new wxStaticText(this, wxID_ANY, prompt);
            mStatus = new wxStaticText(this, wxID_ANY, "Esc cancels.");
            mStatus->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

            wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);
            outer->Add(text, wxSizerFlags().Border(wxALL, FromDIP(kMargin)));
            outer->Add(mStatus, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM,
                                                      FromDIP(kMargin)));
            outer->Add(CreateStdDialogButtonSizer(wxCANCEL),
                       wxSizerFlags().Expand().Border(wxALL, FromDIP(kMargin)));
            SetSizerAndFit(outer);
            CenterOnParent();

            Bind(wxEVT_CHAR_HOOK, &CaptureDialog::OnCharHook, this);

            if (device == CONFIG_MODEL_PAD)
            {
                // Anything already held when the dialog opened has to be let go
                // and pressed again, or the click on Set would count.
                mHeld = PadHeld();
                Bind(wxEVT_TIMER, &CaptureDialog::OnTimer, this);
                mTimer.Start(16);
                ShowPadStatus();
            }
        }

        wxString Token() const
        {
            return mToken;
        }

    private:
        void ShowPadStatus()
        {
            mStatus->SetLabel(AnyPad() ? "Esc cancels."
                                       : "No controller found. Connect one, or press Esc to cancel.");
        }

        void OnCharHook(wxKeyEvent& event)
        {
            if (event.GetKeyCode() == WXK_ESCAPE)
            {
                EndModal(wxID_CANCEL);
                return;
            }

            if (mDevice != CONFIG_MODEL_KEYBOARD)
            {
                event.Skip();
                return;
            }

            const wxString token = KeyToken(event);
            if (token.empty())
            {
                mStatus->SetLabel("That key can't be bound. Press another key, or Esc to cancel.");
                return;
            }

            mToken = token;
            EndModal(wxID_OK);
        }

        void OnTimer(wxTimerEvent&)
        {
            const U32 held = PadHeld();
            const U32 pressed = held & ~mHeld;
            mHeld = held;

            if (pressed == 0)
            {
                ShowPadStatus();
                return;
            }

            S32 count;
            const iPadBindToken* tokens = ConfigModelBindTokens(CONFIG_MODEL_PAD, &count);
            for (S32 input = 0; input < PADIN_COUNT; input++)
            {
                if ((pressed & (1u << input)) == 0)
                {
                    continue;
                }
                const char* name = iPadBindTokenName((S16)input, tokens, count);
                if (name != NULL)
                {
                    mTimer.Stop();
                    mToken = name;
                    EndModal(wxID_OK);
                    return;
                }
            }
        }

        ConfigModelDevice mDevice;
        wxTimer mTimer;
        U32 mHeld;
        wxStaticText* mStatus;
        wxString mToken;
    };

    class ConfigFrame : public wxFrame
    {
    public:
        ConfigFrame();

    private:
        void AddRow(wxWindow* parent, wxGridBagSizer* grid, int& line, S32 setting, int indent);
        void AddBindRow(int& line, ConfigModelDevice device, S32 row);
        void Capture(Row& row, bool add);
        int GroupSize(S32 master) const;
        void BuildRows();
        void ShowSection(int which);
        bool RewrapDescriptions();
        int WrapWidthOf(const Row& row) const;
        void RefreshRow(Row& row);
        void RefreshApply();

        wxString ValueOf(const Row& row) const;
        Row* RowOfEvent(const wxCommandEvent& event);

        // Everything unwritten, written -- or the window is not worth closing
        // yet. Save & Exit, Start Game and the close box all go through
        // SaveIfNeeded rather than each deciding for itself when a write is
        // needed.
        bool Save();
        bool SaveIfNeeded();
        bool MayDiscard();

        void OnSection(wxCommandEvent& event);
        void OnValueChanged(wxCommandEvent& event);
        void OnBrowse(wxCommandEvent& event);
        void OnResetSection(wxCommandEvent& event);
        void OnStartGame(wxCommandEvent& event);
        void OnApply(wxCommandEvent& event);
        void OnSave(wxCommandEvent& event);
        void OnCancel(wxCommandEvent& event);
        void OnClose(wxCloseEvent& event);
        void OnPaneSize(wxSizeEvent& event);
        void OnGroupToggled(wxCollapsiblePaneEvent& event);

        wxListBox* mSections;
        wxScrolled<wxPanel>* mPane;
        wxGridBagSizer* mGrid;
        wxButton* mApply;

        std::vector<Row> mRows;
        int mSection;

        // The width the descriptions were last wrapped at. A resize changes
        // how they wrap; a scroll does not, and this is what tells the two
        // apart -- re-wrapping forty statics on every size event is one of the
        // ways to make a window feel slow to drag.
        int mWrappedAt;
    };

    ConfigFrame::ConfigFrame()
        : wxFrame(NULL, wxID_ANY, "Battle for Bikini Bottom - Settings"), mSections(NULL),
          mPane(NULL), mGrid(NULL), mApply(NULL), mSection(0), mWrappedAt(0)
    {
        wxPanel* root = new wxPanel(this);

        mSections = new wxListBox(root, kIdSections);
        for (S32 i = 0; i < ConfigModelSectionCount(); i++)
        {
            mSections->Append(ConfigModelSectionName(i));
        }
        for (S32 d = 0; d < CONFIG_MODEL_DEVICE_COUNT; d++)
        {
            mSections->Append(ConfigModelDeviceSection((ConfigModelDevice)d));
        }
        mSections->SetSelection(0);
        mSections->SetMinSize(FromDIP(wxSize(kLabelWidth, 100)));

        mPane = new wxScrolled<wxPanel>(root, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                        wxVSCROLL | wxBORDER_THEME);
        mPane->SetScrollRate(0, FromDIP(12));

        // The pane takes the colour a list or a text field would, not the
        // dialog grey the buttons sit on: it is a document being edited rather
        // than a strip of chrome.
        mPane->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

        mGrid = new wxGridBagSizer(FromDIP(4), FromDIP(8));

        // The control column takes the slack, which is what lets a path field
        // use the whole width while a number stays a box.
        mGrid->AddGrowableCol(1, 1);

        wxBoxSizer* paneBorder = new wxBoxSizer(wxVERTICAL);
        paneBorder->Add(mGrid, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(kMargin)));
        mPane->SetSizer(paneBorder);

        wxStaticText* status =
            new wxStaticText(root, wxID_ANY, ConfigModelPath(), wxDefaultPosition, wxDefaultSize,
                             wxST_ELLIPSIZE_MIDDLE);
        status->SetToolTip(ConfigModelPath());

        wxButton* reset = new wxButton(root, kIdResetSection, "Reset section");
        wxButton* start = new wxButton(root, kIdStartGame, "Start Game");
        mApply = new wxButton(root, wxID_APPLY, "Apply");
        wxButton* cancel = new wxButton(root, wxID_CANCEL, "Cancel");
        wxButton* save = new wxButton(root, wxID_SAVE, "Save && Exit");
        save->SetDefault();

        wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
        buttons->Add(reset, wxSizerFlags().Border(wxRIGHT, FromDIP(6)));
        buttons->AddStretchSpacer();
        buttons->Add(start, wxSizerFlags().Border(wxRIGHT, FromDIP(6)));
        buttons->Add(mApply, wxSizerFlags().Border(wxRIGHT, FromDIP(6)));
        buttons->Add(cancel, wxSizerFlags().Border(wxRIGHT, FromDIP(6)));
        buttons->Add(save);

        wxBoxSizer* columns = new wxBoxSizer(wxHORIZONTAL);
        columns->Add(mSections, wxSizerFlags().Expand().Border(wxRIGHT, FromDIP(kMargin)));
        columns->Add(mPane, wxSizerFlags(1).Expand());

        wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);
        outer->Add(columns, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(kMargin)));
        outer->Add(status, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, FromDIP(kMargin)));
        outer->Add(buttons, wxSizerFlags().Expand().Border(wxALL, FromDIP(kMargin)));
        root->SetSizer(outer);

        SetClientSize(FromDIP(wxSize(760, 600)));
        SetMinSize(FromDIP(wxSize(560, 360)));
        CenterOnScreen();

#ifdef __WXMSW__
        // The game's icon, out of this executable's own resources. Explorer
        // finds it without being told; a window does not, and wx gives one the
        // stock application icon until it is handed something else.
        //
        // "#1" is how FindResource spells the numeric id `../res/bfbb_config.rc`
        // gives it. It stays a number rather than becoming a name because
        // Explorer picks the LOWEST-NUMBERED icon group as the file's icon, and
        // a named-only resource is a coin toss.
        //
        // A bundle rather than one icon, so each place Windows draws it takes
        // the size it wants out of the .ico: 16 for the title bar, larger for
        // Alt-Tab and the taskbar. One icon would be scaled into both.
        SetIcons(wxIconBundle("#1", NULL));
#endif

        // Bound ONCE, here, and not in BuildRows -- a Bind inside the function
        // that rebuilds the pane adds another handler on every section change,
        // and the fourth section visited would record each keystroke four
        // times. The row is recovered from the event's id, which is why one
        // handler can serve every row.
        Bind(wxEVT_LISTBOX, &ConfigFrame::OnSection, this, kIdSections);
        Bind(wxEVT_BUTTON, &ConfigFrame::OnResetSection, this, kIdResetSection);
        Bind(wxEVT_BUTTON, &ConfigFrame::OnStartGame, this, kIdStartGame);
        Bind(wxEVT_BUTTON, &ConfigFrame::OnApply, this, wxID_APPLY);
        Bind(wxEVT_BUTTON, &ConfigFrame::OnSave, this, wxID_SAVE);
        Bind(wxEVT_BUTTON, &ConfigFrame::OnCancel, this, wxID_CANCEL);
        Bind(wxEVT_CLOSE_WINDOW, &ConfigFrame::OnClose, this);

        mPane->Bind(wxEVT_SIZE, &ConfigFrame::OnPaneSize, this);
        mPane->Bind(wxEVT_CHECKBOX, &ConfigFrame::OnValueChanged, this);
        mPane->Bind(wxEVT_CHOICE, &ConfigFrame::OnValueChanged, this);
        mPane->Bind(wxEVT_COMBOBOX, &ConfigFrame::OnValueChanged, this);
        mPane->Bind(wxEVT_TEXT, &ConfigFrame::OnValueChanged, this);
        mPane->Bind(wxEVT_BUTTON, &ConfigFrame::OnBrowse, this);
        mPane->Bind(wxEVT_COLLAPSIBLEPANE_CHANGED, &ConfigFrame::OnGroupToggled, this);

        BuildRows();
        RefreshApply();
    }

    // Build one setting's controls into `grid`, which belongs to `parent`, and
    // advance `line` past the two grid rows it takes: the label and control,
    // then the description under them.
    //
    // Shared by the section's own grid and by each group's, so a setting looks
    // the same whether or not it is folded away under another.
    void ConfigFrame::AddRow(wxWindow* parent, wxGridBagSizer* grid, int& line, S32 i, int indent)
    {
        const iConfigSetting* s = ConfigModelSetting(i);

        Row row;
        row.setting = i;
        row.indent = indent;

        const int id = kIdRowBase + (int)mRows.size() * kIdsPerRow;
        const wxString value = ConfigModelText(i);

        const bool wide =
            (s->kind == ICONFIG_FOLDER || s->kind == ICONFIG_FONT || s->kind == ICONFIG_STRING);
        const wxSize controlSize = FromDIP(wxSize(wide ? kPathWidth : kValueWidth, -1));

        wxWindow* control = NULL;

        if (s->kind == ICONFIG_BOOL)
        {
            row.check = new wxCheckBox(parent, id + kIdRowValue, "on");
            row.check->SetValue(value.IsSameAs("on", false) || value.IsSameAs("true", false) ||
                                value.IsSameAs("yes", false) || value.IsSameAs("1", false));
            control = row.check;
        }
        else if (s->kind == ICONFIG_ENUM)
        {
            // An enum is a list and nothing else, so its control cannot be
            // typed into.
            wxArrayString words;
            char word[64];
            for (S32 c = 0; ConfigModelChoiceAt(s->choices, c, word, sizeof(word)); c++)
            {
                words.Add(word);
            }

            row.choice =
                new wxChoice(parent, id + kIdRowValue, wxDefaultPosition, controlSize, words);
            row.choice->SetStringSelection(value);

            // A file holding a word this build does not know keeps it. The
            // list has no entry to select, and quietly leaving the control on
            // the first one would write that word over the file's on the next
            // save without anybody asking for it.
            if (row.choice->GetSelection() == wxNOT_FOUND)
            {
                row.choice->Append(value);
                row.choice->SetStringSelection(value);
            }

            control = row.choice;
        }
        else if (ConfigModelWantsCombo(s))
        {
            // Every other kind with choices takes a value BESIDES them --
            // `framerate` is a number or the word "display" -- so the box stays
            // typable and the list is a shortcut to the words.
            wxArrayString words;
            char word[64];
            for (S32 c = 0; ConfigModelChoiceAt(s->choices, c, word, sizeof(word)); c++)
            {
                words.Add(word);
            }

            wxComboBox* combo = new wxComboBox(parent, id + kIdRowValue, value, wxDefaultPosition,
                                               controlSize, words);
            row.entry = combo;
            control = combo;
        }
        else
        {
            wxTextCtrl* text =
                new wxTextCtrl(parent, id + kIdRowValue, value, wxDefaultPosition, controlSize);
            row.entry = text;
            control = text;
        }

        wxStaticText* label = new wxStaticText(parent, wxID_ANY, s->name);
        label->SetMinSize(FromDIP(wxSize(kLabelWidth, -1)));

        grid->Add(label, wxGBPosition(line, 0), wxGBSpan(1, 1), wxALIGN_CENTER_VERTICAL);

        // wxEXPAND alone, and not with wxALIGN_CENTER_VERTICAL beside it: wx
        // asserts on a sizer item that asks to fill its cell and to be aligned
        // within it, because the two mean opposite things. The cell is one
        // control tall, so filling it centres it anyway.
        grid->Add(control, wxGBPosition(line, 1), wxGBSpan(1, 1), wxEXPAND);

        if (ConfigModelWantsBrowse(s))
        {
            wxButton* browse = new wxButton(parent, id + kIdRowBrowse, "Browse...",
                                            wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            grid->Add(browse, wxGBPosition(line, 2), wxGBSpan(1, 1), wxALIGN_CENTER_VERTICAL);
        }

        line++;

        char text[768];
        ConfigModelDescribe(i, text, sizeof(text));

        row.descriptionText = text;
        row.description = new wxStaticText(parent, wxID_ANY, row.descriptionText);
        row.description->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

        grid->Add(row.description, wxGBPosition(line, 0), wxGBSpan(1, 3), wxEXPAND | wxBOTTOM,
                  FromDIP(10));
        line++;

        mRows.push_back(row);
    }

    // One button's binding: its name, a box holding the binding as the file has
    // it (the default shows as grey hint text when it is empty), Set to replace
    // it with one captured input and Add to append one as an alternative.
    void ConfigFrame::AddBindRow(int& line, ConfigModelDevice device, S32 bind)
    {
        Row row;
        row.bindRow = bind;
        row.device = device;

        const int id = kIdRowBase + (int)mRows.size() * kIdsPerRow;

        wxTextCtrl* text = new wxTextCtrl(mPane, id + kIdRowValue, ConfigModelBindText(device, bind),
                                          wxDefaultPosition, FromDIP(wxSize(kValueWidth, -1)));
        char def[256];
        ConfigModelBindDescribeDefault(device, bind, def, sizeof(def));
        text->SetHint(def);
        row.entry = text;

        wxStaticText* label = new wxStaticText(mPane, wxID_ANY, ConfigModelBindName(bind));
        label->SetMinSize(FromDIP(wxSize(kLabelWidth, -1)));

        wxButton* set = new wxButton(mPane, id + kIdRowBrowse, "Set...", wxDefaultPosition,
                                     wxDefaultSize, wxBU_EXACTFIT);
        wxButton* add = new wxButton(mPane, id + kIdRowAdd, "Add...", wxDefaultPosition,
                                     wxDefaultSize, wxBU_EXACTFIT);
        set->SetToolTip("Replace the binding with the next key or button you press");
        add->SetToolTip("Add another key or button for this action");

        wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
        buttons->Add(set, wxSizerFlags().Border(wxRIGHT, FromDIP(4)));
        buttons->Add(add);

        mGrid->Add(label, wxGBPosition(line, 0), wxGBSpan(1, 1), wxALIGN_CENTER_VERTICAL);
        mGrid->Add(text, wxGBPosition(line, 1), wxGBSpan(1, 1), wxEXPAND);
        mGrid->Add(buttons, wxGBPosition(line, 2), wxGBSpan(1, 1), wxALIGN_CENTER_VERTICAL);
        line++;

        row.descriptionText = DescribeBinding(device, bind);
        row.description = new wxStaticText(mPane, wxID_ANY, row.descriptionText);
        row.description->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        mGrid->Add(row.description, wxGBPosition(line, 0), wxGBSpan(1, 3), wxEXPAND | wxBOTTOM,
                   FromDIP(10));
        line++;

        mRows.push_back(row);
    }

    // Replace or extend a binding with the next input pressed.
    void ConfigFrame::Capture(Row& row, bool add)
    {
        CaptureDialog dialog(this, row.device, row.bindRow);
        if (dialog.ShowModal() != wxID_OK)
        {
            return;
        }

        wxString value = dialog.Token();
        const wxString have = row.entry->GetValue();
        if (add && !have.empty())
        {
            value = have + ", " + value;
        }

        // SetValue sends the change event, which records it.
        row.entry->SetValue(value);
    }

    // How many settings in this section hang off `master`.
    int ConfigFrame::GroupSize(S32 master) const
    {
        int n = 0;
        for (S32 i = 0; i < ConfigModelSettingCount(); i++)
        {
            if (ConfigModelGroupOf(i) == master)
            {
                n++;
            }
        }
        return n;
    }

    // Create the current section's controls. Called on a section change and
    // nowhere else -- a resize is RewrapDescriptions and a sizer Layout, which
    // is the whole reason this is a sizer rather than arithmetic.
    //
    // A setting that other settings hang off gets a collapsible pane under it
    // holding them, closed. They are the numbers that shape what the master
    // turns on, and they are worth having without being worth reading past
    // every time.
    void ConfigFrame::BuildRows()
    {
        mPane->Freeze();

        mGrid->Clear(true);
        mRows.clear();
        mWrappedAt = 0;

        int line = 0;

        if (IsBindSection(mSection))
        {
            const ConfigModelDevice device = DeviceOfSection(mSection);

            wxStaticText* intro = new wxStaticText(
                mPane, wxID_ANY,
                device == CONFIG_MODEL_KEYBOARD
                    ? "Keys for each game button. Separate alternatives with ',', keys held "
                      "together with '+', and keys that must not be held with '!'. Movement "
                      "(WASD) and camera (IJKL) keys are fixed."
                    : "Controller inputs for each game button, by position: a is the bottom "
                      "face button on any controller. ',', '+' and '!' work as on the keyboard "
                      "page. Empty: follow input.preset.");
            intro->Wrap(FromDIP(520));
            mGrid->Add(intro, wxGBPosition(line, 0), wxGBSpan(1, 3), wxEXPAND | wxBOTTOM,
                       FromDIP(12));
            line++;

            for (S32 i = 0; i < ConfigModelBindCount(); i++)
            {
                AddBindRow(line, device, i);
            }

            mPane->Scroll(0, 0);
            mPane->Layout();
            RewrapDescriptions();
            mPane->FitInside();
            mPane->Thaw();
            return;
        }

        const char* section = ConfigModelSectionName(mSection);

        for (S32 i = 0; i < ConfigModelSettingCount(); i++)
        {
            const iConfigSetting* s = ConfigModelSetting(i);
            if (strcmp(s->section, section) != 0)
            {
                continue;
            }

            // Details are drawn by the master they belong to, below.
            if (ConfigModelGroupOf(i) >= 0)
            {
                continue;
            }

            AddRow(mPane, mGrid, line, i, 0);

            const int children = GroupSize(i);
            if (children == 0)
            {
                continue;
            }

            wxCollapsiblePane* group =
                new wxCollapsiblePane(mPane, wxID_ANY, wxString::Format("Details (%d)", children));
            mGrid->Add(group, wxGBPosition(line, 0), wxGBSpan(1, 3), wxEXPAND | wxLEFT | wxBOTTOM,
                       FromDIP(kIndent));
            line++;

            // GetPane() is the window the contents go in, not the pane itself.
            // Adding them to the pane draws them over its own header.
            wxWindow* inner = group->GetPane();

            wxGridBagSizer* innerGrid = new wxGridBagSizer(FromDIP(4), FromDIP(8));
            innerGrid->AddGrowableCol(1, 1);

            int innerLine = 0;
            for (S32 j = 0; j < ConfigModelSettingCount(); j++)
            {
                if (ConfigModelGroupOf(j) == i)
                {
                    AddRow(inner, innerGrid, innerLine, j, FromDIP(kIndent));
                }
            }

            wxBoxSizer* innerBorder = new wxBoxSizer(wxVERTICAL);
            innerBorder->Add(innerGrid, wxSizerFlags(1).Expand().Border(wxTOP, FromDIP(6)));
            inner->SetSizer(innerBorder);
        }

        mPane->Scroll(0, 0);
        mPane->Layout();
        RewrapDescriptions();
        mPane->FitInside();
        mPane->Thaw();
    }

    // Wrap every description to the pane's current width, and say whether that
    // changed anything.
    //
    // wxStaticText::Wrap edits the label rather than laying it out, so the
    // original text goes back before each re-wrap -- otherwise a window that
    // is widened keeps the breaks it was given while it was narrow.
    bool ConfigFrame::RewrapDescriptions()
    {
        const int width = mPane->GetClientSize().GetWidth() - FromDIP(kMargin) * 2;
        if (width <= 0 || width == mWrappedAt)
        {
            return false;
        }
        mWrappedAt = width;

        for (size_t i = 0; i < mRows.size(); i++)
        {
            mRows[i].description->SetLabel(mRows[i].descriptionText);
            mRows[i].description->Wrap(WrapWidthOf(mRows[i]));
        }

        return true;
    }

    // The width one row's description wraps inside: what is left of the pane
    // after its own indent. Measured off the pane rather than off the window
    // the row lives in, because a row inside a closed group has no width at
    // all and would wrap to one word per line the moment the group opened.
    int ConfigFrame::WrapWidthOf(const Row& row) const
    {
        const int width = mWrappedAt - row.indent;
        return width > FromDIP(120) ? width : FromDIP(120);
    }

    // The "(default: x)" line under a row follows what is in the control, so
    // anything that changes a value re-reads it. Only a row whose text
    // actually changed is re-laid out: that is the row crossing into or out of
    // its default, and doing it on every keystroke would shuffle the rows
    // under the caret while a path is being typed.
    void ConfigFrame::RefreshRow(Row& row)
    {
        // A binding's description says what the default is, which the value
        // does not change.
        if (row.bindRow >= 0)
        {
            return;
        }

        char text[768];
        ConfigModelDescribe(row.setting, text, sizeof(text));

        if (row.descriptionText == text)
        {
            return;
        }

        row.descriptionText = text;
        row.description->SetLabel(row.descriptionText);
        if (mWrappedAt > 0)
        {
            row.description->Wrap(WrapWidthOf(row));
        }

        mPane->Layout();
        mPane->FitInside();
    }

    void ConfigFrame::RefreshApply()
    {
        // Apply is enabled only when there is something to apply, which is the
        // only report this window makes that a write happened: after one, the
        // button goes grey.
        mApply->Enable(ConfigModelDirty());
    }

    wxString ConfigFrame::ValueOf(const Row& row) const
    {
        if (row.check != NULL)
        {
            return row.check->GetValue() ? "on" : "off";
        }
        if (row.choice != NULL)
        {
            return row.choice->GetStringSelection();
        }
        return row.entry->GetValue();
    }

    // The row an event came from, or NULL for one that did not come from a row
    // at all -- which happens while the pane is being rebuilt, since a control
    // can report its initial value before its row has been pushed.
    Row* ConfigFrame::RowOfEvent(const wxCommandEvent& event)
    {
        const int offset = event.GetId() - kIdRowBase;
        if (offset < 0)
        {
            return NULL;
        }

        const size_t index = (size_t)(offset / kIdsPerRow);
        if (index >= mRows.size())
        {
            return NULL;
        }

        return &mRows[index];
    }

    void ConfigFrame::ShowSection(int which)
    {
        mSection = which;
        mSections->SetSelection(which);
        BuildRows();
    }

    void ConfigFrame::OnSection(wxCommandEvent& event)
    {
        // A list box can report that nothing is selected -- GTK sends it when
        // the selection is cleared -- and -1 would index the section table off
        // its front.
        const int which = event.GetSelection();
        if (which < 0 || which >= ConfigModelSectionCount() + CONFIG_MODEL_DEVICE_COUNT)
        {
            return;
        }

        ShowSection(which);
    }

    void ConfigFrame::OnValueChanged(wxCommandEvent& event)
    {
        Row* row = RowOfEvent(event);
        if (row == NULL)
        {
            event.Skip();
            return;
        }

        if (row->bindRow >= 0)
        {
            // Trimmed, so a box emptied down to a space still means the default.
            wxString value = ValueOf(*row);
            value.Trim(true).Trim(false);
            ConfigModelBindSetText(row->device, row->bindRow, value.utf8_str());
            RefreshApply();
            return;
        }

        ConfigModelSetText(row->setting, ValueOf(*row).utf8_str());
        RefreshApply();
        RefreshRow(*row);
    }

    void ConfigFrame::OnBrowse(wxCommandEvent& event)
    {
        Row* row = RowOfEvent(event);
        if (row == NULL || row->entry == NULL)
        {
            event.Skip();
            return;
        }

        if (row->bindRow >= 0)
        {
            const bool add = (event.GetId() - kIdRowBase) % kIdsPerRow == kIdRowAdd;
            Capture(*row, add);
            return;
        }

        const iConfigSetting* s = ConfigModelSetting(row->setting);
        wxString picked;

        if (s->kind == ICONFIG_FONT)
        {
            wxString start = row->entry->GetValue();

            // "auto" and "off" are values here, not paths, and handing one to
            // the dialog as a filename gets it rejected rather than ignored.
            if (start.Find('/') == wxNOT_FOUND && start.Find('\\') == wxNOT_FOUND)
            {
                start.Clear();
            }

            wxFileDialog dialog(this, "Font", wxEmptyString, start,
                                "TrueType fonts (*.ttf;*.otf;*.ttc)|*.ttf;*.otf;*.ttc|"
                                "All files (*.*)|*.*",
                                wxFD_OPEN | wxFD_FILE_MUST_EXIST);
            if (dialog.ShowModal() != wxID_OK)
            {
                return;
            }
            picked = dialog.GetPath();
        }
        else
        {
            wxDirDialog dialog(this, s->name, row->entry->GetValue(),
                               wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
            if (dialog.ShowModal() != wxID_OK)
            {
                return;
            }
            picked = dialog.GetPath();
        }

        // SetValue sends the change event, and that is what records the value
        // -- so there is nothing to record here.
        row->entry->SetValue(picked);
    }

    void ConfigFrame::OnResetSection(wxCommandEvent&)
    {
        if (IsBindSection(mSection))
        {
            ConfigModelBindResetAll(DeviceOfSection(mSection));
            RefreshApply();
            BuildRows();
            return;
        }

        ConfigModelResetSection(mSection);
        RefreshApply();
        BuildRows();
    }

    bool ConfigFrame::Save()
    {
        char why[kConfigModelMaxPath + 256];
        why[0] = '\0';
        S32 section = 0;

        ConfigModelResult result = ConfigModelSave(why, sizeof(why), NULL, &section);
        RefreshApply();

        if (result == CONFIG_MODEL_OK)
        {
            return true;
        }

        if (result == CONFIG_MODEL_BAD_VALUE)
        {
            wxMessageBox(why, "Invalid value", wxOK | wxICON_WARNING, this);
            ShowSection(section);
            return false;
        }

        wxMessageBox(why, "Could not save", wxOK | wxICON_ERROR, this);
        return false;
    }

    bool ConfigFrame::SaveIfNeeded()
    {
        return !ConfigModelDirty() || Save();
    }

    // Whether it is all right to throw away what has been typed. Asked by
    // Cancel and by the close box, and only when there is something to throw
    // away.
    bool ConfigFrame::MayDiscard()
    {
        if (!ConfigModelDirty())
        {
            return true;
        }

        return wxMessageBox("Your changes have not been saved to config.ini.\n\n"
                            "Close without saving?",
                            "Unsaved changes", wxYES_NO | wxICON_WARNING, this) == wxYES;
    }

    void ConfigFrame::OnApply(wxCommandEvent&)
    {
        Save();
    }

    void ConfigFrame::OnSave(wxCommandEvent&)
    {
        if (SaveIfNeeded())
        {
            Destroy();
        }
    }

    void ConfigFrame::OnCancel(wxCommandEvent&)
    {
        if (MayDiscard())
        {
            Destroy();
        }
    }

    void ConfigFrame::OnStartGame(wxCommandEvent&)
    {
        if (!SaveIfNeeded())
        {
            return;
        }

        char why[kConfigModelMaxPath + 256];
        why[0] = '\0';

        if (!ConfigModelStartGame(why, sizeof(why)))
        {
            wxMessageBox(why, "Could not start the game", wxOK | wxICON_ERROR, this);
            return;
        }

        Destroy();
    }

    void ConfigFrame::OnClose(wxCloseEvent& event)
    {
        if (event.CanVeto() && !MayDiscard())
        {
            event.Veto();
            return;
        }

        Destroy();
    }

    void ConfigFrame::OnPaneSize(wxSizeEvent& event)
    {
        // FitInside only when the wrap actually moved, because it sets the
        // virtual size and so can produce another size event -- which with an
        // unconditional call is a window that will not settle while it is
        // being dragged.
        if (RewrapDescriptions())
        {
            mPane->Layout();
            mPane->FitInside();
        }

        event.Skip();
    }

    // Opening or closing a group changes how tall the contents are, and the
    // scrolled pane's virtual size is what its scrollbar is drawn from -- so
    // without this the pane opens a group it will not scroll down to.
    void ConfigFrame::OnGroupToggled(wxCollapsiblePaneEvent& event)
    {
        mPane->Layout();
        mPane->FitInside();
        event.Skip();
    }

} // namespace

// Outside the anonymous namespace, unlike everything above it. wxIMPLEMENT_APP
// defines wxGetApp() returning a reference to this type, and an application
// class is the one thing here wx itself names.
class ConfigApp : public wxApp
{
public:
    bool OnInit() override;
    int OnExit() override;
};

bool ConfigApp::OnInit()
{
    // Dark mode where the system is in it, and light where it is not.
    //
    // FIRST, before anything that can put a window on screen -- the error box
    // below included. Windows builds its controls differently in the two modes
    // and wx will not change a window that already exists, so a message box
    // opened ahead of this one call leaves the whole program light on a dark
    // desktop and reports nothing.
    //
    // Nothing else here has to follow: every colour this file sets comes from
    // wxSystemSettings, and wx answers those with dark values once the mode is
    // on.
    //
    // wx 3.3 added it, and a distribution's package is still 3.2 -- Ubuntu's
    // libwxgtk3.2-dev is the name of the version. Skipping it there costs
    // nothing that shows: this call is what WINDOWS needs, and Windows builds
    // the 3.3 submodule because it has no packaged wx to find. GTK reads the
    // desktop's theme on its own with or without it.
#if wxCHECK_VERSION(3, 3, 0)
    SetAppearance(Appearance::System);
#endif

    // An argument overrides the file search, for editing one config while a
    // different one is in place. Anything past the first is ignored rather
    // than refused: a shell that expanded a glob is not worth a dialog box.
    //
    // The buffer is held in a named variable rather than passed straight
    // through. utf8_str() returns one that owns its bytes and dies at the end
    // of the full expression, so `argv[1].utf8_str().data()` as an argument is
    // a pointer into freed memory by the time the callee reads it.
    wxString named;
    if (argc > 1)
    {
        named = argv[1];
    }
    const wxScopedCharBuffer path = named.utf8_str();

    char why[kConfigModelMaxPath + 256];
    why[0] = '\0';

    if (!ConfigModelOpen(named.empty() ? NULL : path.data(), why, sizeof(why)))
    {
        wxMessageBox(why, "bfbb settings", wxOK | wxICON_ERROR);
        return false;
    }

    (new ConfigFrame())->Show();
    return true;
}

int ConfigApp::OnExit()
{
    ConfigModelClose();

    // Up only if a controller binding was captured; harmless otherwise.
    SDL_Quit();
    return wxApp::OnExit();
}

wxIMPLEMENT_APP(ConfigApp);
