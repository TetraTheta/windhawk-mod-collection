// ==WindhawkMod==
// @id              disable-shell-feeds-notifications
// @name            Disable Shell Feeds Notifications
// @description     Prevents Windows 10 taskbar waits caused by Shell Feeds content notifications
// @version         0.1.0
// @author          TetraTheta
// @github          https://github.com/TetraTheta
// @include         explorer.exe
// @architecture    x86-64
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Disable Shell Feeds Notifications

Prevents Windows 10 Explorer from sending Shell Feeds lock-screen content
notifications through a synchronous COM call. This is a targeted workaround for
a taskbar hang path involving the **News and interests** infrastructure.

## Why this mod exists

Turning off News and interests does not necessarily remove every Feeds-related
call from Explorer. The taskbar can still update its internal Feeds state and
send a notification containing an empty string to clear lock-screen content.
That notification can itself activate the Shell Feeds UI infrastructure.

A Windows 10 Explorer hang dump showed the following dependency:

```text
Taskbar UI thread
  -> Feeds::FeedsDynamicContent::UpdateViewState
  -> ShellFeedsUtil::NotifyNewLockscreenContent
  -> ShellFeedsLauncher::NotifyNewLockscreenContent
  -> synchronous COM call to another Explorer thread
       -> ShellFeedsExperienceManagerImpl::NotifyNewLockscreenContent
       -> ShellFeedsUI activation
       -> COM response wait
```

The activation target was associated with
`Microsoft.Windows.Search_cw5n1h2txyewy!ShellFeedsUI` activation failures in the
event log. The News and interests policy was already disabled, and the content
being sent was empty. The unwanted work was therefore not evidence that the
news panel had been turned back on: even the content-clearing path could enter
the activation code.

If the activation cannot complete promptly, the taskbar thread can remain
waiting for the synchronous call. Taskbar actions may be delayed even while
foreground applications continue to respond. A disabled or modified Search app,
including one replaced with an executable stub, is one configuration in which
ShellFeedsUI activation can fail. This mod does not require or recommend such
modifications.

## Exactly what the mod does

The mod hooks one function in `explorer.exe`:

```text
HRESULT ShellFeedsUtil::NotifyNewLockscreenContent(PCWSTR content)
```

Every call to that function returns `S_OK` immediately, without calling the
original function. This suppresses both empty and non-empty content
notifications at the Explorer caller, before they enter the downstream
Shell Feeds launcher and synchronous COM call shown above.

The successful return deliberately tells the caller that no further handling
is needed. The mod does not create a replacement feed service, terminate a
process, poll for hangs, or install a background monitoring task. It has no
settings.

## Intended use and trade-offs

Use this mod when you do not want News and interests or its related lock-screen
content notifications, particularly when investigating taskbar hangs involving
this specific call path. Keep News and interests disabled through the normal
Windows settings or policy as well.

- Notifications through the hooked function are discarded, including requests
  to clear content. Existing content is not explicitly removed by this mod.
- This is not a complete Feeds or Windows Search blocker. Other code paths can
  still activate related components.
- The mod does not change registry policies, system files, Search indexing,
  taskbar menus, or ordinary application notifications.
- It does not repair the Search package or guarantee a fix for unrelated shell
  freezes. The diagnosed path does not establish the cause of every delayed
  jump list, Start menu, or window switch.

## Compatibility

- Windows 10 x64 only. Initialization rejects other OS versions, including
  Windows 11. The diagnostic basis is a Windows 10 22H2 hang dump; other Windows
  10 builds still require validation.
- Windhawk 1.7.1 or newer.
- Requires the exact internal Explorer function symbol. Windhawk may need to
  download matching Microsoft symbols on first use.

The hook uses a decorated symbol name, not a fixed address or byte-pattern
scan. If Windows removes or changes that symbol, initialization fails and logs
an error instead of guessing an address. Internal Windows behavior can still
change without a symbol rename, so compatibility should be checked after
Windows updates.

## Installation and verification

1. Create a new local mod in Windhawk and replace its source with this file.
2. Compile and enable it. Check the mod log for the successful hook message;
   an unsupported OS or unresolved symbol should produce an initialization
   error instead.
3. With News and interests disabled, check that Start, taskbar jump lists,
   notification-area menus, window switching, and taskbar resizing still work.
4. Continue normal use and compare any recurring delays. A quiet observation
   period alone is not proof that all shell hangs have been resolved.

Compilation alone does not validate runtime behavior. This workaround needs
testing on the Windows build where it will be used. Persistent Search or Feeds
activation errors can come from other callers and do not by themselves prove
that the hook failed.

Disable or remove this mod in Windhawk to restore the original function for
future calls. No system-file or registry restoration is required, and discarded
notifications are not replayed.
*/
// ==/WindhawkModReadme==

#include <windhawk_utils.h>

static HRESULT __cdecl NotifyNewLockscreenContent_hook(PCWSTR) { return S_OK; }

BOOL Wh_ModInit() {
  using RtlGetVersion_t = LONG(WINAPI*)(OSVERSIONINFOW*);
  const auto rtlGetVersion =
    reinterpret_cast<RtlGetVersion_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
  OSVERSIONINFOW version = {};
  version.dwOSVersionInfoSize = sizeof(version);
  if (!rtlGetVersion || rtlGetVersion(&version) != 0) {
    Wh_Log(L"Failed to determine the Windows version");
    return FALSE;
  }

  if (version.dwMajorVersion != 10 || version.dwBuildNumber < 10240 || version.dwBuildNumber >= 22000) {
    Wh_Log(L"Only Windows 10 x64 is supported");
    return FALSE;
  }

  HMODULE explorer = GetModuleHandleW(nullptr);
  if (!explorer) {
    Wh_Log(L"Failed to find the Explorer executable module");
    return FALSE;
  }

  const WindhawkUtils::SYMBOL_HOOK hooks[] = {
    {{L"?NotifyNewLockscreenContent@ShellFeedsUtil@@YAJPEBG@Z"}, nullptr, NotifyNewLockscreenContent_hook, false},
  };
  WH_HOOK_SYMBOLS_OPTIONS options = {};
  options.optionsSize = sizeof(options);
  options.noUndecoratedSymbols = TRUE;
  if (!WindhawkUtils::HookSymbols(explorer, hooks, ARRAYSIZE(hooks), &options)) {
    Wh_Log(L"Failed to hook ShellFeedsUtil::NotifyNewLockscreenContent");
    return FALSE;
  }

  Wh_Log(L"Hooked ShellFeedsUtil::NotifyNewLockscreenContent; Feeds content notifications will be suppressed");
  return TRUE;
}
