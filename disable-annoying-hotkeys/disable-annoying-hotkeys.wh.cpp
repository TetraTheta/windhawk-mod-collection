// ==WindhawkMod==
// @id              disable-annoying-hotkeys
// @name            Disable Annoying Hotkeys
// @description     Disables selected Windows hotkeys such as Win+F and Office hotkeys
// @version         0.1.0
// @author          TetraTheta
// @github          https://github.com/TetraTheta
// @include         explorer.exe
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Disable Annoying Hotkeys

This mod disables selected Windows hotkeys that are commonly registered by
Explorer:

- Feedback Hub: `Win+F`
- Office:
  - `Ctrl+Shift+Alt+Win+D`
  - `Ctrl+Shift+Alt+Win+L`
  - `Ctrl+Shift+Alt+Win+N`
  - `Ctrl+Shift+Alt+Win+O`
  - `Ctrl+Shift+Alt+Win+P`
  - `Ctrl+Shift+Alt+Win+T`
  - `Ctrl+Shift+Alt+Win+W`
  - `Ctrl+Shift+Alt+Win+X`
  - `Ctrl+Shift+Alt+Win+Y`
  - `Ctrl+Shift+Alt+Win+Space`

Both groups can be enabled or disabled separately in the mod settings. To undo
the behavior, disable the relevant setting or disable the mod.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- disableFeedbackHubHotkey: true
  $name: Disable Feedback Hub hotkey
  $description: Blocks Explorer from registering Win+F.
- disableOfficeHotkeys: true
  $name: Disable Office hotkeys
  $description: Blocks Explorer from registering Ctrl+Shift+Alt+Win Office hotkeys.
*/
// ==/WindhawkModSettings==

struct {
  bool disableFeedbackHubHotkey;
  bool disableOfficeHotkeys;
} settings;

BOOL(WINAPI* pOriginalRegisterHotKey)(HWND hWnd, int id, UINT fsModifiers, UINT vk);

constexpr UINT kFeedbackHubModifiers = MOD_WIN | MOD_NOREPEAT;
constexpr UINT kOfficeModifiers = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN | MOD_NOREPEAT;

// Office 키 목록은 Windows가 예약 등록하는 조합만 차단하기 위해 한곳에 모읍니다.
constexpr UINT kOfficeHotkeys[] = {
  'D', 'L', 'N', 'O', 'P', 'T', 'W', 'X', 'Y', VK_SPACE,
};

void LoadSettings() {
  settings.disableFeedbackHubHotkey = Wh_GetIntSetting(L"disableFeedbackHubHotkey");
  settings.disableOfficeHotkeys = Wh_GetIntSetting(L"disableOfficeHotkeys");
}

bool IsOfficeHotkey(UINT vk) {
  if (!vk) return true;
  for (UINT officeHotkey : kOfficeHotkeys) {
    if (vk == officeHotkey) return true;
  }
  return false;
}

bool ShouldBlockHotkey(UINT fsModifiers, UINT vk) {
  if (settings.disableFeedbackHubHotkey && fsModifiers == kFeedbackHubModifiers && vk == 'F') return true;
  return settings.disableOfficeHotkeys && fsModifiers == kOfficeModifiers && IsOfficeHotkey(vk);
}

BOOL WINAPI RegisterHotKeyHook(HWND hWnd, int id, UINT fsModifiers, UINT vk) {
  if (ShouldBlockHotkey(fsModifiers, vk)) {
    SetLastError(ERROR_HOTKEY_ALREADY_REGISTERED);
    return FALSE;
  }
  return pOriginalRegisterHotKey(hWnd, id, fsModifiers, vk);
}

BOOL Wh_ModInit() {
  LoadSettings();

  HMODULE user32Module = GetModuleHandle(L"user32.dll");
  if (!user32Module) {
    Wh_Log(L"GetModuleHandle(user32.dll) failed: %lu", GetLastError());
    return FALSE;
  }

  void* originalRegisterHotKey = reinterpret_cast<void*>(GetProcAddress(user32Module, "RegisterHotKey"));
  if (!originalRegisterHotKey) {
    Wh_Log(L"GetProcAddress(RegisterHotKey) failed: %lu", GetLastError());
    return FALSE;
  }

  if (!Wh_SetFunctionHook(originalRegisterHotKey, reinterpret_cast<void*>(RegisterHotKeyHook),
                          reinterpret_cast<void**>(&pOriginalRegisterHotKey))) {
    Wh_Log(L"Wh_SetFunctionHook(RegisterHotKey) failed");
    return FALSE;
  }

  return TRUE;
}

void Wh_ModSettingsChanged() { LoadSettings(); }
