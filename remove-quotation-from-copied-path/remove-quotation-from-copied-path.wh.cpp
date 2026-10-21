// ==WindhawkMod==
// @id              remove-quotation-from-copied-path
// @name            Remove Quotation from Copied Path
// @description     Copies Explorer file and folder paths without surrounding quotation marks
// @version         0.1.0
// @author          TetraTheta
// @github          https://github.com/TetraTheta
// @include         explorer.exe
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Remove Quotation from Copied Path

Explorer's **Copy as path** command copies paths without surrounding double
quotes. This also applies to `Ctrl+Shift+C` and removes the quotes from each
path when multiple files or folders are selected.
*/
// ==/WindhawkModReadme==

#include <objidl.h>
#include <windhawk_utils.h>

using GetShellItemsAsTextHGLOBAL_t = HRESULT(__cdecl*)(IDataObject*, BYTE, HGLOBAL*);
GetShellItemsAsTextHGLOBAL_t GetShellItemsAsTextHGLOBAL_orig = nullptr;

HRESULT __cdecl GetShellItemsAsTextHGLOBAL_hook(IDataObject* dataObject, BYTE flags, HGLOBAL* text) {
  // The 0x02 flag suppresses quotes around each path in Explorer's text builder.
  return GetShellItemsAsTextHGLOBAL_orig(dataObject, flags | 0x02, text);
}

BOOL Wh_ModInit() {
  HMODULE shell32 = GetModuleHandleW(L"shell32.dll");
  if (!shell32) {
    Wh_Log(L"Failed to find shell32.dll");
    return FALSE;
  }

  const WindhawkUtils::SYMBOL_HOOK hooks[] = {
    {{L"GetShellItemsAsTextHGLOBAL", L"_GetShellItemsAsTextHGLOBAL@12"},
     &GetShellItemsAsTextHGLOBAL_orig,
     GetShellItemsAsTextHGLOBAL_hook},
  };

  if (!WindhawkUtils::HookSymbols(shell32, hooks, ARRAYSIZE(hooks))) {
    Wh_Log(L"Failed to hook GetShellItemsAsTextHGLOBAL");
    return FALSE;
  }

  return TRUE;
}
